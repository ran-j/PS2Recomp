// ---------------------------------------------------------------------------
// Scheduler / runtime regression tests grounded in how real recompiled PS2
// guests exercise the N=1 fiber scheduler. Each suite here targets a bug that
// the synthetic scheduler suites cannot see because their fibers
// are well-behaved (they yield cooperatively, park promptly, and shut down
// from the main thread). Real recompiled guests spin across function
// dispatches, monopolize the executor, and call requestStop() from guest
// context — these tests reproduce those shapes with bounded waits (every wait
// has a timeout and an escape hatch: a regression FAILS, it does not hang).
// ---------------------------------------------------------------------------

#include "MiniTest.h"
#include "SchedTestSupport.h"
#include "ps2_runtime.h"
#include "ps2_syscalls.h"
#include "ps2_stubs.h"
#include "ps2_runtime_macros.h"
#include "Kernel/Syscalls/Interrupt.h"
#include "Kernel/Syscalls/System.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Kernel/Syscalls/Helpers/State.h" // g_threads / g_thread_map_mutex / g_activeThreads

using namespace ps2_syscalls;
using namespace ps2x_test;

namespace
{
    // These helpers move test control/result words between the test main
    // thread and guest fibers (which may run on other OS threads under the
    // pthread fiber backend). Use atomic accesses so that cross-thread
    // traffic is well-defined and the suite runs clean under TSan; the
    // addresses used are all 4-byte aligned, as std::atomic_ref requires.
    void rdramWrite32Raw(uint8_t *rdram, uint32_t addr, uint32_t val)
    {
        std::atomic_ref<uint32_t>(*reinterpret_cast<uint32_t *>(rdram + addr)).store(val);
    }

    uint32_t rdramRead32Raw(const uint8_t *rdram, uint32_t addr)
    {
        return std::atomic_ref<uint32_t>(
                   *reinterpret_cast<uint32_t *>(const_cast<uint8_t *>(rdram) + addr))
            .load();
    }

    void rdramWrite32(std::vector<uint8_t> &rdram, uint32_t addr, uint32_t val)
    {
        rdramWrite32Raw(rdram.data(), addr, val);
    }

    uint32_t rdramRead32(const std::vector<uint8_t> &rdram, uint32_t addr)
    {
        return rdramRead32Raw(rdram.data(), addr);
    }

    // Create a semaphore via CreateSema (EE layout: count, max_count, init_count).
    int32_t createSchedSema(uint8_t *rdram, PS2Runtime *runtime, int initCount, int maxCount)
    {
        constexpr uint32_t kSemaParamAddr = 0x2F40u;
        const uint32_t params[6] = {
            0u,
            static_cast<uint32_t>(maxCount),
            static_cast<uint32_t>(initCount),
            0u,
            0u,
            0u,
        };
        std::memcpy(rdram + kSemaParamAddr, params, sizeof(params));

        R5900Context cCtx{};
        setRegU32(cCtx, 4, kSemaParamAddr);
        ps2_syscalls::CreateSema(rdram, &cCtx, runtime);
        return getRegS32(cCtx, 2);
    }

    void deleteSchedSema(uint8_t *rdram, PS2Runtime *runtime, int32_t sid)
    {
        if (sid <= 0)
        {
            return;
        }
        R5900Context dCtx{};
        setRegU32(dCtx, 4, static_cast<uint32_t>(sid));
        ps2_syscalls::DeleteSema(rdram, &dCtx, runtime);
    }

    void signalSchedSema(uint8_t *rdram, PS2Runtime *runtime, int32_t sid)
    {
        if (sid <= 0)
        {
            return;
        }
        R5900Context sCtx{};
        setRegU32(sCtx, 4, static_cast<uint32_t>(sid));
        ps2_syscalls::SignalSema(rdram, &sCtx, runtime);
    }

    // Create + start a fiber worker thread; returns tid or -1.
    int32_t startSchedWorker(uint8_t *rdram, PS2Runtime *runtime,
                             uint32_t entryAddr, int priority,
                             uint32_t stackAddr, uint32_t stackSize)
    {
        constexpr uint32_t kThreadParamAddr = 0x2E40u;
        const uint32_t threadParam[7] = {
            0u,
            entryAddr,
            stackAddr,
            stackSize,
            0u,
            static_cast<uint32_t>(priority),
            0u,
        };
        std::memcpy(rdram + kThreadParamAddr, threadParam, sizeof(threadParam));

        R5900Context createCtx{};
        setRegU32(createCtx, 4, kThreadParamAddr);
        ps2_syscalls::CreateThread(rdram, &createCtx, runtime);
        const int32_t tid = getRegS32(createCtx, 2);
        if (tid <= 0)
        {
            return -1;
        }

        R5900Context startCtx{};
        setRegU32(startCtx, 4, static_cast<uint32_t>(tid));
        setRegU32(startCtx, 5, 0u);
        ps2_syscalls::StartThread(rdram, &startCtx, runtime);
        if (getRegS32(startCtx, 2) != 0)
        {
            return -1;
        }
        return tid;
    }

    // ------------------------------------------------------------------
    // Control words (kernel-area scratch, disjoint from other suites)
    // ------------------------------------------------------------------
    constexpr uint32_t kThStop  = 0x00060000u; // 1 => guest loops exit
    constexpr uint32_t kThSidX  = 0x00060010u; // ping-pong sema X
    constexpr uint32_t kThSidY  = 0x00060014u; // ping-pong sema Y
    constexpr uint32_t kThFlag  = 0x00060020u; // starvation probe flag
    constexpr uint32_t kThCount = 0x00060024u; // ping-pong loop counter
    constexpr uint32_t kThSent  = 0x00060028u; // DMAC handler sentinel

    // ------------------------------------------------------------------
    // Guest step functions (registered as recompiled functions)
    // ------------------------------------------------------------------

    // Ping-pong pair: one permit circulates between semas X and Y, so the run
    // queue is (almost) never empty and the executor never runs out of ready
    // fibers — the shape that starved parked host workers before the
    // g_host_token_waiters executor gate.
    static void stepPingA(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        while (rdramRead32Raw(rdram, kThStop) == 0u)
        {
            const int32_t sidX = static_cast<int32_t>(rdramRead32Raw(rdram, kThSidX));
            const int32_t sidY = static_cast<int32_t>(rdramRead32Raw(rdram, kThSidY));
            R5900Context w{};
            setRegU32(w, 4, static_cast<uint32_t>(sidX));
            ps2_syscalls::WaitSema(rdram, &w, runtime);
            rdramWrite32Raw(rdram, kThCount, rdramRead32Raw(rdram, kThCount) + 1u);
            R5900Context s{};
            setRegU32(s, 4, static_cast<uint32_t>(sidY));
            ps2_syscalls::SignalSema(rdram, &s, runtime);
        }
        ctx->pc = 0u;
    }

    // A guest loop that never blocks and never returns to the dispatch loop:
    // its only scheduling hook is the recompiler-emitted back-edge call to
    // shouldPreemptGuestExecution() (yield_point). Without yield_point step 4
    // this fiber holds the executor inside ps2fiber_resume forever and a
    // parked host worker starves no matter what the executor predicate does.
    static void stepSpinShouldPreempt(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        while (rdramRead32Raw(rdram, kThStop) == 0u)
        {
            rdramWrite32Raw(rdram, kThCount, rdramRead32Raw(rdram, kThCount) + 1u);
            runtime->shouldPreemptGuestExecution(); // recompiled back-edge hook
        }
        ctx->pc = 0u;
    }

    // Cross-dispatch spin pair: each function returns to the dispatch loop
    // with ctx->pc aimed at the other. Neither body ever calls the emitted
    // back-edge hook (mimicking generated code whose loop back-edge is a
    // call/return across function boundaries), so the ONLY place a yield can
    // happen is the dispatch loop itself.
    static void stepCrossDispatchA(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        rdramWrite32Raw(rdram, kThCount, rdramRead32Raw(rdram, kThCount) + 1u);
        ctx->pc = (rdramRead32Raw(rdram, kThStop) == 0u) ? 0x00700400u : 0u;
    }

    static void stepCrossDispatchB(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ctx->pc = (rdramRead32Raw(rdram, kThStop) == 0u) ? 0x00700300u : 0u;
    }

    // RPC-server shape (sceSifRpcInit + sceSifSetRpcQueue + sceSifRpcLoop):
    // the generated caller leaves ctx->pc at the loop entry, so if the stub
    // returns without blocking the dispatch loop re-enters it forever.
    static void stepRpcServerLoop(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ps2_stubs::sceSifRpcLoop(rdram, ctx, runtime);
        // ctx->pc intentionally left at this function's address: the real
        // guest loop re-enters sceSifRpcLoop after any (stray) wakeup.
    }

    static void stepWriteFlagAndExit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        rdramWrite32Raw(rdram, kThFlag, 1u);
        ctx->pc = 0u;
    }

    // Guest-context stop shape: spin (no yields) until the REAL interrupt
    // worker parks for the guest token, then call requestStop() from inside
    // the fiber - exactly what the unimplemented-function default handler
    // does when a guest thread faults. kThStop is a spin escape hatch.
    static void stepGuestContextStop(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        while (ps2sched::host_token_waiters() == 0 &&
               rdramRead32Raw(rdram, kThStop) == 0u)
        {
            // busy spin: no yield points, so the parked worker stays parked
        }
        runtime->requestStop(); // notifyRuntimeStop() from GUEST context
        rdramWrite32Raw(rdram, kThFlag, 1u);
        ctx->pc = 0u;
    }

    // DMAC handler body: bump the sentinel and finish.
    static void stepDmacHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        rdramWrite32Raw(rdram, kThSent, rdramRead32Raw(rdram, kThSent) + 1u);
        ctx->pc = 0u;
    }

    // sceSifSetDma shape: a guest syscall path that dispatches DMAC handlers
    // SYNCHRONOUSLY from the fiber servicing the syscall.
    static void stepDmacFromGuest(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ps2_syscalls::dispatchDmacHandlersForCause(rdram, runtime, 5u);
        rdramWrite32Raw(rdram, kThFlag, 1u);
        ctx->pc = 0u;
    }

    static void stepPingB(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        while (rdramRead32Raw(rdram, kThStop) == 0u)
        {
            const int32_t sidX = static_cast<int32_t>(rdramRead32Raw(rdram, kThSidX));
            const int32_t sidY = static_cast<int32_t>(rdramRead32Raw(rdram, kThSidY));
            R5900Context w{};
            setRegU32(w, 4, static_cast<uint32_t>(sidY));
            ps2_syscalls::WaitSema(rdram, &w, runtime);
            R5900Context s{};
            setRegU32(s, 4, static_cast<uint32_t>(sidX));
            ps2_syscalls::SignalSema(rdram, &s, runtime);
        }
        ctx->pc = 0u;
    }

    // ------------------------------------------------------------------
    // SchedulerRecoveryIsolation helpers
    //
    // The diagnostic dispatch-trace ring (formerly a single process-wide
    // `thread_local DispatchHistory`) is now owned per-fiber by FiberContext.
    // These step functions call PS2Runtime::debugCurrentDispatchTrace() -
    // which resolves to whichever fiber (or per-OS-thread fallback) is
    // running on the calling thread - so they MUST run inside a fiber's step
    // function. Calling it from the test/main thread instead would silently
    // observe the host fallback ring, not any fiber's history, and the tests
    // would pass vacuously without ever exercising the fix. All assertions
    // are therefore made on rdram words written by the fibers themselves;
    // the main thread only reads rdram after the fibers finish.
    // ------------------------------------------------------------------

    // No-op registrant for marker PCs: never dispatched via ctx->pc, only
    // looked up directly (lookupFunction) to push a PC into the calling
    // fiber's dispatch-trace ring. Registered purely to avoid the noisy
    // "no exact recompiled function" cerr log that an unregistered PC would
    // trigger (harmless to test state, but kept clean per the design's risk
    // list).
    static void stepNoop(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)rdram; (void)ctx; (void)runtime;
    }

    // Fiber A: push its own marker, hand off to B via sema, then park until B
    // hands back. On resume A inspects its OWN trace. A fiber resuming out of
    // WaitSema continues at the point of the call - it does NOT re-enter
    // dispatchLoop - so the only writer into A's ring between A's push and
    // A's post-resume read is B, and (under the fix) B writes into its OWN
    // ring, never A's.
    static void stepIsoA(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        runtime->lookupFunction(0x00700A80u); // push A's marker into A's ring

        R5900Context s{};
        setRegU32(s, 4, rdramRead32Raw(rdram, 0x00061004u)); // sidB
        ps2_syscalls::SignalSema(rdram, &s, runtime);        // let B run

        R5900Context w{};
        setRegU32(w, 4, rdramRead32Raw(rdram, 0x00061000u)); // sidA
        ps2_syscalls::WaitSema(rdram, &w, runtime);          // park; B runs meanwhile

        const std::string trace = runtime->debugCurrentDispatchTrace(); // A resumes: inspect A's own ring
        rdramWrite32Raw(rdram, 0x00061010u,
            (trace.find("700b80") != std::string::npos ||
             trace.find("700b00") != std::string::npos) ? 1u : 0u); // kRiAForeign
        rdramWrite32Raw(rdram, 0x00061018u,
            (trace.find("700a80") != std::string::npos) ? 1u : 0u); // kRiAOwn
        ctx->pc = 0u;
    }

    // Fiber B: wait for A's handoff, push its own marker, inspect its OWN
    // trace, then signal A back.
    static void stepIsoB(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        R5900Context w{};
        setRegU32(w, 4, rdramRead32Raw(rdram, 0x00061004u)); // sidB
        ps2_syscalls::WaitSema(rdram, &w, runtime);

        runtime->lookupFunction(0x00700B80u); // push B's marker into B's ring
        const std::string trace = runtime->debugCurrentDispatchTrace();
        rdramWrite32Raw(rdram, 0x00061014u,
            (trace.find("700a80") != std::string::npos ||
             trace.find("700a00") != std::string::npos) ? 1u : 0u); // kRiBForeign
        rdramWrite32Raw(rdram, 0x0006101Cu,
            (trace.find("700b80") != std::string::npos) ? 1u : 0u); // kRiBOwn

        R5900Context s{};
        setRegU32(s, 4, rdramRead32Raw(rdram, 0x00061000u)); // sidA
        ps2_syscalls::SignalSema(rdram, &s, runtime);        // wake A
        ctx->pc = 0u;
    }

    // R2 probe: same entry PC for both the original fiber (run==0) and the
    // fiber that reuses its tid after teardown (run==1). Both fibers get
    // 0x00700C00 pushed into THEIR OWN ring automatically by dispatchLoop
    // before this body runs, so "the ring started empty" cannot be tested as
    // trace=="(empty)" (it never is, even under the fix). Instead we check
    // whether the ring, at entry, already contains the OTHER fiber's distinct
    // marker (0x00700C80, pushed only by run==0 after this check) - that is
    // the signal that would only appear via cross-fiber leakage of the old
    // shared thread_local.
    static void stepReuseProbe(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        constexpr uint32_t kReuseMarker = 0x00700C80u;
        const uint32_t run = rdramRead32Raw(rdram, 0x00062000u); // kReuseRun
        const bool inheritedPrevMarker =
            (runtime->debugCurrentDispatchTrace().find("700c80") != std::string::npos);

        if (run == 0u)
        {
            rdramWrite32Raw(rdram, 0x00062004u, inheritedPrevMarker ? 0u : 1u); // kReuseNotInherited0
            runtime->lookupFunction(kReuseMarker);
            rdramWrite32Raw(rdram, 0x00062008u,
                (runtime->debugCurrentDispatchTrace().find("700c80") != std::string::npos) ? 1u : 0u); // kReuseNonEmpty0
        }
        else
        {
            rdramWrite32Raw(rdram, 0x0006200Cu, inheritedPrevMarker ? 0u : 1u); // kReuseNotInherited1
        }
        ctx->pc = 0u;
    }

    // ------------------------------------------------------------------
    // SchedulerStackIsolation control words and addresses (disjoint from
    // other suites, which use <= 0x00062xxx)
    // ------------------------------------------------------------------
    constexpr uint32_t kSiSemA     = 0x00063000u; // ping-pong sema A
    constexpr uint32_t kSiSemB     = 0x00063004u; // ping-pong sema B
    constexpr uint32_t kSiSysA     = 0x00063008u; // override syscall number, fiber A
    constexpr uint32_t kSiSysB     = 0x0006300Cu; // override syscall number, fiber B
    constexpr uint32_t kSiTopA     = 0x00063010u; // B1: invoke $sp seen by A
    constexpr uint32_t kSiTopB     = 0x00063014u; // B1: invoke $sp seen by B
    constexpr uint32_t kSiDmacTopA = 0x00063018u; // B3: handler $sp seen by A
    constexpr uint32_t kSiDmacTopB = 0x0006301Cu; // B3: handler $sp seen by B

    // ---- I1 (B1): RPC/override invoke stack isolation ----------------------
    // Invoked BY rpcInvokeFunction; ctx is its internal `tmp`, so $29 is the
    // scratch-stack top and $31 is rpcInvokeFunction's return sentinel.
    static void stepInvokeRecordA(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        rdramWrite32Raw(rdram, kSiTopA, getRegU32(ctx, 29));      // record A's scratch top
        R5900Context s{}; setRegU32(s, 4, rdramRead32Raw(rdram, kSiSemB));
        ps2_syscalls::SignalSema(rdram, &s, runtime);            // let B proceed
        R5900Context w{}; setRegU32(w, 4, rdramRead32Raw(rdram, kSiSemA));
        ps2_syscalls::WaitSema(rdram, &w, runtime);              // PARK inside invoke; A's stack stays live
        ctx->pc = getRegU32(ctx, 31);                            // resume: return to sentinel -> invoke loop ends
    }
    static void stepInvokeRecordB(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        rdramWrite32Raw(rdram, kSiTopB, getRegU32(ctx, 29));      // A's scratch STILL live here
        R5900Context s{}; setRegU32(s, 4, rdramRead32Raw(rdram, kSiSemA));
        ps2_syscalls::SignalSema(rdram, &s, runtime);            // wake A
        ctx->pc = getRegU32(ctx, 31);
    }
    static void stepInvokeEntryA(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ps2_syscalls::dispatchNumericSyscall(rdramRead32Raw(rdram, kSiSysA), rdram, ctx, runtime);
        ctx->pc = 0u;
    }
    static void stepInvokeEntryB(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        R5900Context w{}; setRegU32(w, 4, rdramRead32Raw(rdram, kSiSemB));
        ps2_syscalls::WaitSema(rdram, &w, runtime);              // wait until A recorded + parked
        ps2_syscalls::dispatchNumericSyscall(rdramRead32Raw(rdram, kSiSysB), rdram, ctx, runtime);
        ctx->pc = 0u;
    }

    // ---- I2 (B3): inline DMAC handler stack isolation ----------------------
    // Runs AS a DMAC handler; ctx is runHandlers' irqCtx, so $29 is the
    // reserved handler-stack top.
    static void stepDmacRecordA(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        rdramWrite32Raw(rdram, kSiDmacTopA, getRegU32(ctx, 29));
        R5900Context s{}; setRegU32(s, 4, rdramRead32Raw(rdram, kSiSemB));
        ps2_syscalls::SignalSema(rdram, &s, runtime);
        R5900Context w{}; setRegU32(w, 4, rdramRead32Raw(rdram, kSiSemA));
        ps2_syscalls::WaitSema(rdram, &w, runtime);              // PARK inside dispatch; A's handler stack live
        ctx->pc = 0u;                                            // handler returns
    }
    static void stepDmacRecordB(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        rdramWrite32Raw(rdram, kSiDmacTopB, getRegU32(ctx, 29)); // A's handler stack STILL live
        R5900Context s{}; setRegU32(s, 4, rdramRead32Raw(rdram, kSiSemA));
        ps2_syscalls::SignalSema(rdram, &s, runtime);            // wake A
        ctx->pc = 0u;
    }
    static void stepDmacDispatchA(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ps2_syscalls::dispatchDmacHandlersForCause(rdram, runtime, 5u); // inline on fiber
        ctx->pc = 0u;
    }
    static void stepDmacDispatchB(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        R5900Context w{}; setRegU32(w, 4, rdramRead32Raw(rdram, kSiSemB));
        ps2_syscalls::WaitSema(rdram, &w, runtime);              // wait until A recorded + parked
        ps2_syscalls::dispatchDmacHandlersForCause(rdram, runtime, 6u);
        ctx->pc = 0u;
    }

    // ------------------------------------------------------------------
    // SchedulerOverrideIsolation helpers
    //
    // dispatchSyscallOverride's reentrancy guard (System.cpp) used to be one
    // process-wide `thread_local std::vector`. Under the N=1 fiber scheduler
    // that vector is keyed to the ONE guest executor OS thread, shared by every
    // fiber. So while fiber A is parked INSIDE its override for syscall N (N
    // still on the shared vector because A hasn't returned to pop it), fiber B
    // issuing the same N sees N "active", is treated as reentrant, and its
    // override is SILENTLY SKIPPED in favor of the builtin. The fix moves the
    // stack into FiberContext (per-fiber), so B has its own empty stack and its
    // override runs. Wrong-dispatch, not memory-safety.
    //
    // The handler PARKS via WaitSema from inside rpcInvokeFunction (inside the
    // handler invoke). On ucontext the fiber's whole C++ stack is frozen at the
    // swap, so nesting depth is irrelevant (same mechanism stepIsoA relies on).
    // RED ONLY ON build-ucontext: on the pthread backend each fiber is its own
    // OS thread, the old thread_local is accidentally per-fiber, and this test
    // is vacuously green even unfixed.
    // ------------------------------------------------------------------
    constexpr uint32_t kOvSyscall     = 0x79u;       // free syscall #, benign TODO fallback
    constexpr uint32_t kOvHandler     = 0x00700D00u; // registered override handler
    constexpr uint32_t kOvEntryA      = 0x00700D80u;
    constexpr uint32_t kOvEntryB      = 0x00700E00u;
    constexpr uint32_t kOvSidStartB   = 0x00063020u; // A -> B: "A has parked inside its override"
    constexpr uint32_t kOvSidResumeA  = 0x00063024u; // B -> A: "you may resume"
    constexpr uint32_t kOvAEntered    = 0x00063028u; // A's handler was entered (liveness)
    constexpr uint32_t kOvARan        = 0x0006302Cu; // A's handler resumed & completed (liveness)
    constexpr uint32_t kOvBRan        = 0x00063030u; // B's handler ran => override NOT skipped (THE signal)

    // One handler for both fibers; $a0 selects mode. mode 1 = A (park), 2 = B (mark).
    static void stepOverrideHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t mode = getRegU32(ctx, 4); // $a0, forwarded by rpcInvokeFunction
        if (mode == 1u)
        {
            rdramWrite32Raw(rdram, kOvAEntered, 1u);
            R5900Context s{}; setRegU32(s, 4, rdramRead32Raw(rdram, kOvSidStartB));
            ps2_syscalls::SignalSema(rdram, &s, runtime);   // release B
            R5900Context w{}; setRegU32(w, 4, rdramRead32Raw(rdram, kOvSidResumeA));
            ps2_syscalls::WaitSema(rdram, &w, runtime);      // PARK: N is on A's override stack
            rdramWrite32Raw(rdram, kOvARan, 1u);             // resumed
        }
        else
        {
            rdramWrite32Raw(rdram, kOvBRan, 1u);             // B's override actually ran
        }
        setReturnU32(ctx, 0u);
        ctx->pc = getRegU32(ctx, 31);                        // return through rpcInvoke sentinel
    }

    static void stepOverrideA(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setRegU32(*ctx, 4, 1u);                               // mode A (park)
        runtime->handleSyscall(rdram, ctx, kOvSyscall);      // -> dispatchSyscallOverride -> handler parks
        ctx->pc = 0u;
    }

    static void stepOverrideB(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        R5900Context w{}; setRegU32(w, 4, rdramRead32Raw(rdram, kOvSidStartB));
        ps2_syscalls::WaitSema(rdram, &w, runtime);          // proceed only after A has parked
        setRegU32(*ctx, 4, 2u);                               // mode B (mark)
        runtime->handleSyscall(rdram, ctx, kOvSyscall);      // fix: B's override runs; bug: skipped -> builtin
        R5900Context s{}; setRegU32(s, 4, rdramRead32Raw(rdram, kOvSidResumeA));
        ps2_syscalls::SignalSema(rdram, &s, runtime);        // wake A regardless -> clean drain in both cases
        ctx->pc = 0u;
    }

    // ------------------------------------------------------------------
    // SchedulerJoinStarvation control words + step functions.
    //
    // Reproduces the TerminateThread hang caused by join_fiber flooring the
    // joiner one level BELOW the target (t->priority + 1) instead of AT the
    // target's level. With the +1 floor, an equal-priority sibling that stays
    // runnable after the target exits keeps the joiner (now one level lower)
    // permanently off the run-queue head, so join_fiber never re-observes the
    // target as finished and TerminateThread never returns.
    // ------------------------------------------------------------------
    constexpr uint32_t kJsTargetTid    = 0x00062000u; // int32: target (B) tid, read by joiner A
    constexpr uint32_t kJsJoinReturned = 0x00062004u; // 1 => A's TerminateThread(B) returned
    constexpr uint32_t kJsBStarted     = 0x00062008u; // 1 => target B is running

    // Target B (prio 10): announce running, then spin on the recompiler
    // back-edge hook until terminated. B never self-exits — it is killed by
    // A's TerminateThread(B), which guarantees B is still joinable when A
    // enters join_fiber (so the priority floor is actually exercised). The
    // yield_point inside shouldPreemptGuestExecution throws ThreadExitException
    // once terminateRequested is set (by A, or by scheduler_shutdown on
    // teardown), unwinding B cleanly. kThStop is a belt-and-suspenders escape.
    static void stepJoinStarveTarget(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        rdramWrite32Raw(rdram, kJsBStarted, 1u);
        while (rdramRead32Raw(rdram, kThStop) == 0u)
        {
            runtime->shouldPreemptGuestExecution(); // yield_point: throws on terminate
        }
        ctx->pc = 0u;
    }

    // Joiner A (prio 5, strictly higher than B and the prio-10 pingers):
    // TerminateThread(B) routes through request_terminate + join_fiber. The
    // floor demotes A to the target's level (10, fixed) or one below (11,
    // buggy). Only the fixed floor lets A rotate back to the run-queue head
    // (FIFO among the prio-10 group) to observe B finished and return; the
    // buggy floor strands A below the ever-runnable pingers forever. A sets
    // kJsJoinReturned only AFTER TerminateThread returns.
    static void stepJoinStarveJoiner(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const int32_t targetTid = static_cast<int32_t>(rdramRead32Raw(rdram, kJsTargetTid));
        R5900Context tcx{};
        setRegU32(tcx, 4, static_cast<uint32_t>(targetTid));
        ps2_syscalls::TerminateThread(rdram, &tcx, runtime); // -> join_fiber(targetTid)
        rdramWrite32Raw(rdram, kJsJoinReturned, 1u);
        ctx->pc = 0u;
    }

} // anonymous namespace

// ---------------------------------------------------------------------------
// Token handoff: a host worker parked in async_guest_begin() must win the
// guest token within bounded time even while guest fibers keep the run queue
// non-empty. Regression test for the executor resume predicate: without the
// g_host_token_waiters gate the executor re-resumes fibers without ever
// releasing g_sched_mutex into a cv wait, so the parked worker never runs
// (the VBlank-starvation shape: a guest polling for vsync ticks in a
// semaphore/flag main loop keeps the run queue non-empty, so the starved
// interrupt worker cannot deliver the ticks the guest is waiting on).
// ---------------------------------------------------------------------------
void register_scheduler_token_handoff_tests()
{
    MiniTest::Case("SchedulerTokenHandoff", [](TestCase &tc)
    {
        tc.Run("H1: parked host worker wins the token while fibers ping-pong", [](TestCase &t)
        {
            SchedFixture fx;
            PS2Runtime &runtime = fx.runtime;
            std::vector<uint8_t> &rdram = fx.rdram;

            runtime.registerFunction(0x00700000u, &stepPingA);
            runtime.registerFunction(0x00700100u, &stepPingB);

            const int32_t sidX = createSchedSema(rdram.data(), &runtime, 1, 1); // seeded permit
            const int32_t sidY = createSchedSema(rdram.data(), &runtime, 0, 1);
            t.IsTrue(sidX > 0 && sidY > 0, "H1: semas created");
            if (sidX <= 0 || sidY <= 0)
            {
                return;
            }
            rdramWrite32(rdram, kThStop, 0u);
            rdramWrite32(rdram, kThSidX, static_cast<uint32_t>(sidX));
            rdramWrite32(rdram, kThSidY, static_cast<uint32_t>(sidY));
            rdramWrite32(rdram, kThCount, 0u);

            const int32_t tidA = startSchedWorker(rdram.data(), &runtime,
                                                  0x00700000u, 10, 0x00510000u, 0x2000u);
            const int32_t tidB = startSchedWorker(rdram.data(), &runtime,
                                                  0x00700100u, 10, 0x00514000u, 0x2000u);
            t.IsTrue(tidA > 0 && tidB > 0, "H1: ping-pong fibers started");
            if (tidA <= 0 || tidB <= 0)
            {
                rdramWrite32(rdram, kThStop, 1u);
                signalSchedSema(rdram.data(), &runtime, sidX);
                signalSchedSema(rdram.data(), &runtime, sidY);
                deleteSchedSema(rdram.data(), &runtime, sidX);
                deleteSchedSema(rdram.data(), &runtime, sidY);
                return;
            }

            // Wait until the ping-pong is demonstrably live.
            const bool spinning = waitUntil([&]()
            {
                return rdramRead32(rdram, kThCount) >= 3u;
            }, std::chrono::milliseconds(2000));
            t.IsTrue(spinning, "H1: ping-pong fibers are running");

            // Host worker (interrupt-worker shape): parks for the guest token.
            std::atomic<bool> tokenAcquired{false};
            std::atomic<bool> workerDone{false};
            std::thread worker([&]()
            {
                g_currentThreadId = -1; // non-fiber host worker
                ps2sched::async_guest_begin();
                tokenAcquired.store(true, std::memory_order_release);
                ps2sched::async_guest_end();
                workerDone.store(true, std::memory_order_release);
            });

            // THE regression assertion: the worker must win the token while the
            // fibers are still ping-ponging (bounded wait — a starved worker
            // fails here instead of hanging the binary).
            const bool acquiredWhileBusy = waitUntil([&]()
            {
                return tokenAcquired.load(std::memory_order_acquire);
            }, std::chrono::milliseconds(3000));

            // Escape hatch + orderly teardown (runs regardless of the verdict:
            // once the fibers exit, the executor idles and the worker's
            // async_guest_begin proceeds, so join() below cannot hang).
            rdramWrite32(rdram, kThStop, 1u);
            signalSchedSema(rdram.data(), &runtime, sidX);
            signalSchedSema(rdram.data(), &runtime, sidY);

            const bool drained = waitUntil([&]()
            {
                return g_activeThreads.load(std::memory_order_acquire) <= 0;
            }, std::chrono::milliseconds(3000));

            const bool workerFinished = waitUntil([&]()
            {
                return workerDone.load(std::memory_order_acquire);
            }, std::chrono::milliseconds(3000));
            if (worker.joinable())
            {
                worker.join();
            }

            t.IsTrue(acquiredWhileBusy,
                     "H1: parked host worker acquired the guest token while fibers were busy "
                     "(executor must sleep while g_host_token_waiters > 0)");
            t.IsTrue(drained, "H1: ping-pong fibers drained after stop");
            t.IsTrue(workerFinished, "H1: host worker completed");

            const uint32_t loops = rdramRead32(rdram, kThCount);
            t.IsTrue(loops >= 3u, "H1: guest made progress during the test");

            deleteSchedSema(rdram.data(), &runtime, sidX);
            deleteSchedSema(rdram.data(), &runtime, sidY);
        });
        tc.Run("H2: spinning fiber yields at yield_point when a host worker is parked", [](TestCase &t)
        {
            SchedFixture fx;
            PS2Runtime &runtime = fx.runtime;
            std::vector<uint8_t> &rdram = fx.rdram;

            runtime.registerFunction(0x00700200u, &stepSpinShouldPreempt);
            rdramWrite32(rdram, kThStop, 0u);
            rdramWrite32(rdram, kThCount, 0u);

            const int32_t tid = startSchedWorker(rdram.data(), &runtime,
                                                 0x00700200u, 10, 0x00518000u, 0x2000u);
            t.IsTrue(tid > 0, "H2: spin fiber started");
            if (tid <= 0)
            {
                return;
            }

            const bool spinning = waitUntil([&]()
            {
                return rdramRead32(rdram, kThCount) >= 1000u;
            }, std::chrono::milliseconds(2000));
            t.IsTrue(spinning, "H2: fiber is spinning through yield_point samples");

            std::atomic<bool> tokenAcquired{false};
            std::atomic<bool> workerDone{false};
            std::thread worker([&]()
            {
                g_currentThreadId = -1; // non-fiber host worker
                ps2sched::async_guest_begin();
                tokenAcquired.store(true, std::memory_order_release);
                ps2sched::async_guest_end();
                workerDone.store(true, std::memory_order_release);
            });

            // THE regression assertion: without yield_point step 4 the fiber
            // never leaves ps2fiber_resume, so the worker cannot win the token
            // while the spin is live (bounded wait; escape hatch below).
            const bool acquiredWhileSpinning = waitUntil([&]()
            {
                return tokenAcquired.load(std::memory_order_acquire);
            }, std::chrono::milliseconds(3000));

            // Escape hatch + teardown: end the spin; the fiber exits, the
            // executor idles, and the parked worker (if still parked) proceeds.
            rdramWrite32(rdram, kThStop, 1u);

            const bool drained = waitUntil([&]()
            {
                return g_activeThreads.load(std::memory_order_acquire) <= 0;
            }, std::chrono::milliseconds(3000));
            const bool workerFinished = waitUntil([&]()
            {
                return workerDone.load(std::memory_order_acquire);
            }, std::chrono::milliseconds(3000));
            if (worker.joinable())
            {
                worker.join();
            }

            t.IsTrue(acquiredWhileSpinning,
                     "H2: parked host worker acquired the token while the fiber was spinning "
                     "(yield_point must yield when host_token_waiters > 0)");
            t.IsTrue(drained, "H2: spin fiber drained after stop");
            t.IsTrue(workerFinished, "H2: host worker completed");
        });
        tc.Run("H3: cross-dispatch spin yields via the dispatch loop preempt hook", [](TestCase &t)
        {
            SchedFixture fx;
            PS2Runtime &runtime = fx.runtime;
            std::vector<uint8_t> &rdram = fx.rdram;

            runtime.registerFunction(0x00700300u, &stepCrossDispatchA);
            runtime.registerFunction(0x00700400u, &stepCrossDispatchB);
            rdramWrite32(rdram, kThStop, 0u);
            rdramWrite32(rdram, kThCount, 0u);

            const int32_t tid = startSchedWorker(rdram.data(), &runtime,
                                                 0x00700300u, 10, 0x0051C000u, 0x2000u);
            t.IsTrue(tid > 0, "H3: cross-dispatch fiber started");
            if (tid <= 0)
            {
                return;
            }

            const bool spinning = waitUntil([&]()
            {
                return rdramRead32(rdram, kThCount) >= 1000u;
            }, std::chrono::milliseconds(2000));
            t.IsTrue(spinning, "H3: fiber is spinning across function dispatches");

            std::atomic<bool> tokenAcquired{false};
            std::atomic<bool> workerDone{false};
            std::thread worker([&]()
            {
                g_currentThreadId = -1; // non-fiber host worker
                ps2sched::async_guest_begin();
                tokenAcquired.store(true, std::memory_order_release);
                ps2sched::async_guest_end();
                workerDone.store(true, std::memory_order_release);
            });

            // THE regression assertion: the function bodies never call the
            // back-edge hook, so only the dispatch-loop preempt can yield.
            const bool acquiredWhileSpinning = waitUntil([&]()
            {
                return tokenAcquired.load(std::memory_order_acquire);
            }, std::chrono::milliseconds(3000));

            // Escape hatch + teardown.
            rdramWrite32(rdram, kThStop, 1u);

            const bool drained = waitUntil([&]()
            {
                return g_activeThreads.load(std::memory_order_acquire) <= 0;
            }, std::chrono::milliseconds(3000));
            const bool workerFinished = waitUntil([&]()
            {
                return workerDone.load(std::memory_order_acquire);
            }, std::chrono::milliseconds(3000));
            if (worker.joinable())
            {
                worker.join();
            }

            t.IsTrue(acquiredWhileSpinning,
                     "H3: parked host worker acquired the token during a cross-dispatch spin "
                     "(dispatchLoop must call shouldPreemptGuestExecution)");
            t.IsTrue(drained, "H3: cross-dispatch fiber drained after stop");
            t.IsTrue(workerFinished, "H3: host worker completed");
        });
    }); // MiniTest::Case("SchedulerTokenHandoff")
}

// ---------------------------------------------------------------------------
// sceSifRpcLoop must PARK its thread, not return. The real kernel loop is
// `while (1) { SleepThread(); serve; }`; with all SIF RPC HLE'd host-side no
// wakeup ever arrives, so a stub that returns turns the RPC server thread
// into a hot spin that monopolizes the N=1 executor (any SIF-RPC server
// thread — pad, memcard, audio, filesystem — then starves the main thread
// forever).
// ---------------------------------------------------------------------------
void register_scheduler_rpc_loop_park_tests()
{
    MiniTest::Case("SchedulerRpcLoopPark", [](TestCase &tc)
    {
        tc.Run("R1: sceSifRpcLoop parks its fiber and does not monopolize the executor", [](TestCase &t)
        {
            SchedFixture fx;
            PS2Runtime &runtime = fx.runtime;
            std::vector<uint8_t> &rdram = fx.rdram;

            runtime.registerFunction(0x00700500u, &stepRpcServerLoop);
            runtime.registerFunction(0x00700600u, &stepWriteFlagAndExit);
            rdramWrite32(rdram, kThFlag, 0u);

            // RPC server first (it runs first and, unfixed, never lets go).
            const int32_t serverTid = startSchedWorker(rdram.data(), &runtime,
                                                       0x00700500u, 10, 0x00520000u, 0x2000u);
            // Probe fiber at the SAME priority: only a genuine park (not a
            // priority preempt) can let it run.
            const int32_t probeTid = startSchedWorker(rdram.data(), &runtime,
                                                      0x00700600u, 10, 0x00524000u, 0x2000u);
            t.IsTrue(serverTid > 0 && probeTid > 0, "R1: both fibers started");
            if (serverTid <= 0 || probeTid <= 0)
            {
                return;
            }

            // THE regression assertion: the probe fiber runs within bounded
            // time, i.e. the RPC server fiber parked in SleepThread instead of
            // re-entering the stub forever.
            const bool probeRan = waitUntil([&]()
            {
                return rdramRead32(rdram, kThFlag) == 1u;
            }, std::chrono::milliseconds(3000));

            t.IsTrue(probeRan,
                     "R1: equal-priority fiber ran while sceSifRpcLoop was active "
                     "(the RPC server fiber must park via SleepThread)");

            // Teardown: scheduler_shutdown() terminates the parked server
            // fiber (SleepThread observes terminateRequested and unwinds).
            // Bounded even on regression: shutdown's terminate also unwinds a
            // hot-spinning server at its next dispatch-loop yield point.
            ps2sched::scheduler_shutdown();
            t.Equals(g_activeThreads.load(std::memory_order_acquire), 0,
                     "R1: all fibers terminated after shutdown");
        });
    }); // MiniTest::Case("SchedulerRpcLoopPark")
}

// ---------------------------------------------------------------------------
// requestStop() from GUEST context (a fiber on the executor thread) must not
// join host workers: a worker parked in async_guest_begin() waits for
// g_running_fiber == nullptr, which can never happen while the joining fiber
// IS the running fiber - a deadlock (reached whenever the unimplemented-
// function default fault handler calls requestStop from a fiber while an
// interrupt worker is parked for the token). notifyRuntimeStop must
// signal-only in that case; scheduler_shutdown() joins later on the main
// thread.
// ---------------------------------------------------------------------------
void register_scheduler_guest_context_stop_tests()
{
    MiniTest::Case("SchedulerGuestContextStop", [](TestCase &tc)
    {
        tc.Run("G1: requestStop from a fiber does not deadlock against a parked worker", [](TestCase &t)
        {
            notifyRuntimeStop();
            ps2sched::scheduler_init();
            PS2Runtime runtime;
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0u);

            runtime.registerFunction(0x00700700u, &stepGuestContextStop);
            rdramWrite32(rdram, kThStop, 0u);
            rdramWrite32(rdram, kThFlag, 0u);

            // Real interrupt worker: each VBlank tick it takes AsyncGuestScope
            // (even with zero INTC handlers), parking in async_guest_begin()
            // while our spinning fiber holds the executor.
            EnsureVSyncWorkerRunning(rdram.data(), &runtime);

            const int32_t tid = startSchedWorker(rdram.data(), &runtime,
                                                 0x00700700u, 10, 0x00528000u, 0x2000u);
            t.IsTrue(tid > 0, "G1: fiber started");
            if (tid <= 0)
            {
                ps2sched::scheduler_shutdown(); runtime.requestStop(); return;
            }

            // THE regression assertion: the fiber observes the parked worker,
            // calls requestStop() from guest context, and RETURNS (writing the
            // flag). Unfixed, notifyRuntimeStop joins the parked worker from
            // the fiber and wedges before the flag write.
            const bool stopReturned = waitUntil([&]()
            {
                return rdramRead32(rdram, kThFlag) == 1u;
            }, std::chrono::milliseconds(5000));

            t.IsTrue(stopReturned,
                     "G1: requestStop() invoked on the guest executor returned "
                     "(worker stop must be signal-only from guest context)");

            if (!stopReturned)
            {
                // The fiber is wedged inside the join; the executor cannot be
                // shut down. Return WITHOUT scheduler_shutdown so the failure
                // above is reported instead of hanging here. (The next suite's
                // scheduler_init will SCHED_REQUIRE-abort the process - a loud
                // bounded failure, never a silent hang.)
                return;
            }

            const bool drained = waitUntil([&]()
            {
                return g_activeThreads.load(std::memory_order_acquire) <= 0;
            }, std::chrono::milliseconds(3000));
            t.IsTrue(drained, "G1: fiber drained after guest-context stop");

            // Main-thread shutdown performs the real joins (idempotent stops).
            ps2sched::scheduler_shutdown();
            runtime.requestStop();
        });
    }); // MiniTest::Case("SchedulerGuestContextStop")
}

// ---------------------------------------------------------------------------
// Async callback stacks must live in kernel-reserved memory, never at
// top-of-RAM. Games place their main stack at top-of-RAM via SetupThread
// (SDK crt0 places the main stack at the top of user RAM, and the boot $sp
// defaults to PS2_RAM_SIZE - 0x10); the old pool
// carved down from PS2_RAM_SIZE, so host-dispatched guest callbacks (GS
// vsync / INTC / alarms) ran ON the guest's live main stack and both sides'
// register spills corrupted each other (garbage callback pointers, $ra
// clobbers near the stack top).
// ---------------------------------------------------------------------------
void register_runtime_async_stack_pool_tests()
{
    MiniTest::Case("RuntimeAsyncStackPool", [](TestCase &tc)
    {
        tc.Run("S1: callback stack pool is kernel-area and disjoint from top-of-RAM", [](TestCase &t)
        {
            constexpr uint32_t kPoolFloor = 0x00080000u;
            constexpr uint32_t kPoolTop = 0x00100000u;
            constexpr uint32_t kStackSize = 0x4000u;

            PS2Runtime runtime;

            // First reservation: this is the stack the GS vsync-callback path
            // grabs at boot. Under the old top-of-RAM pool it was 0x1FFFFF0 -
            // inside the guest's main stack.
            const uint32_t first = runtime.reserveAsyncCallbackStack(kStackSize, 16u);
            t.Equals(first, kPoolTop - 0x10u, "S1: first reservation tops the kernel pool");
            t.IsTrue(first < kPoolTop, "S1: first reservation is below the ELF load base");
            t.IsTrue(first >= kPoolFloor, "S1: first reservation is above the pool floor");

            // Exhaust the pool: every reservation must stay inside
            // [kPoolFloor, kPoolTop), and the pool must hold exactly
            // (kPoolTop - kPoolFloor) / kStackSize stacks before returning 0 (bounded loop).
            uint32_t count = 1u;
            bool allInPool = (first >= kPoolFloor && first < kPoolTop);
            for (uint32_t i = 0u; i < 64u; ++i)
            {
                const uint32_t top = runtime.reserveAsyncCallbackStack(kStackSize, 16u);
                if (top == 0u)
                {
                    break;
                }
                ++count;
                allInPool = allInPool && (top >= kPoolFloor && top < kPoolTop);
            }
            t.IsTrue(allInPool, "S1: every reservation stays inside the kernel pool");
            t.Equals(count, (kPoolTop - kPoolFloor) / kStackSize,
                     "S1: pool capacity matches [0x80000, 0x100000) / 16KB");

            runtime.requestStop();
        });
    }); // MiniTest::Case("RuntimeAsyncStackPool")
}

// ---------------------------------------------------------------------------
// dispatchDmacHandlersForCause is reachable from BOTH sides of the guest
// token: from host workers (IRQ worker, DMA completion paths) AND
// synchronously from guest syscall context (sceSifSetDma calls it inline
// while a fiber services the syscall). It borrowed the token via
// AsyncGuestScope unconditionally, and async_guest_begin() aborts by design
// on the guest executor thread - so a guest program arming a DMAC handler
// and then calling sceSifSetDma killed the process deterministically
// ("FATAL [ps2sched]: async_guest_begin from guest executor thread"; any
// guest that arms a DMAC handler and then issues a SIF transfer — e.g. a
// sound-driver init path — hits this).
// ---------------------------------------------------------------------------
void register_scheduler_dmac_guest_dispatch_tests()
{
    MiniTest::Case("SchedulerDmacGuestDispatch", [](TestCase &tc)
    {
        tc.Run("M1: DMAC dispatch from guest syscall context runs handlers without aborting", [](TestCase &t)
        {
            SchedFixture fx;
            PS2Runtime &runtime = fx.runtime;
            std::vector<uint8_t> &rdram = fx.rdram;

            runtime.registerFunction(0x00700800u, &stepDmacHandler);
            runtime.registerFunction(0x00700900u, &stepDmacFromGuest);
            rdramWrite32(rdram, kThFlag, 0u);
            rdramWrite32(rdram, kThSent, 0u);

            // Arm a DMAC handler on cause 5 (SIF0), like sceSifSetDma clients do.
            R5900Context ac{};
            setRegU32(ac, 4, 5u);           // cause
            setRegU32(ac, 5, 0x00700800u);  // handler
            setRegU32(ac, 6, 0u);           // next
            setRegU32(ac, 7, 0u);           // arg
            ps2_syscalls::AddDmacHandler(rdram.data(), &ac, &runtime);
            t.IsTrue(getRegS32(ac, 2) > 0, "M1: DMAC handler registered");

            // Guest-context dispatch: the fiber calls
            // dispatchDmacHandlersForCause synchronously. Against the
            // unconditional AsyncGuestScope this std::terminate()s the
            // process - a loud bounded failure, never a hang.
            const int32_t tid = startSchedWorker(rdram.data(), &runtime,
                                                 0x00700900u, 10, 0x0052C000u, 0x2000u);
            t.IsTrue(tid > 0, "M1: dispatching fiber started");
            if (tid <= 0)
            {
                return;
            }

            const bool done = waitUntil([&]()
            {
                return rdramRead32(rdram, kThFlag) == 1u;
            }, std::chrono::milliseconds(3000));
            t.IsTrue(done, "M1: guest-context dispatch returned");
            t.Equals(rdramRead32(rdram, kThSent), 1u,
                     "M1: handler ran exactly once from guest context");

            const bool drained = waitUntil([&]()
            {
                return g_activeThreads.load(std::memory_order_acquire) <= 0;
            }, std::chrono::milliseconds(3000));
            t.IsTrue(drained, "M1: fiber drained");

            // Host-thread path must still borrow the token and work: dispatch
            // the same cause from this (non-executor) thread.
            ps2_syscalls::dispatchDmacHandlersForCause(rdram.data(), &runtime, 5u);
            t.Equals(rdramRead32(rdram, kThSent), 2u,
                     "M1: handler also runs via the borrowed-token host path");
        });
    }); // MiniTest::Case("SchedulerDmacGuestDispatch")
}

// ---------------------------------------------------------------------------
// The diagnostic dispatch-trace ring (the "trace=..." string logged by
// lookupFunction/reportMissingFunction/dispatchLoop) used to be a single
// process-wide `thread_local DispatchHistory` in ps2_runtime.cpp. Under the
// N=1 fiber scheduler that thread_local is keyed to the ONE guest executor
// OS thread, not to any individual fiber - so EVERY fiber that ever ran on
// that thread shared and overwrote the same ring. Two concrete failures fall
// out of that:
//   R1 (isolation): while fiber A is legitimately still alive (parked in
//      WaitSema, about to resume), a second live fiber B pushes into the
//      "same" ring, so A's post-resume trace is contaminated with B's PCs -
//      a diagnostic reading a completely unrelated fiber's history.
//   R2 (freshness on tid reuse): after a fiber exits and its tid is
//      genuinely recycled by CreateThread's tid allocator, the new fiber at
//      that tid inherits the old fiber's leftover ring contents, because the
//      thread_local lives on the OS thread, not the (now-destroyed) fiber.
// The fix embeds the ring directly in FiberContext (fresh at construction,
// destroyed with the fiber; see ps2_scheduler_internal.h), so both failure
// modes are structurally impossible: there is no shared, outlives-the-fiber
// storage left to leak through.
// ---------------------------------------------------------------------------
void register_scheduler_recovery_isolation_tests()
{
    MiniTest::Case("SchedulerRecoveryIsolation", [](TestCase &tc)
    {
        tc.Run("R1: dispatch-trace ring is isolated across two live interleaved fibers", [](TestCase &t)
        {
            SchedFixture fx;
            PS2Runtime &runtime = fx.runtime;
            std::vector<uint8_t> &rdram = fx.rdram;

            constexpr uint32_t kEntryA = 0x00700A00u;
            constexpr uint32_t kMarkerA = 0x00700A80u;
            constexpr uint32_t kEntryB = 0x00700B00u;
            constexpr uint32_t kMarkerB = 0x00700B80u;
            constexpr uint32_t kRiSidA = 0x00061000u;
            constexpr uint32_t kRiSidB = 0x00061004u;
            constexpr uint32_t kRiAForeign = 0x00061010u; // A saw B's marker? (bug => 1)
            constexpr uint32_t kRiBForeign = 0x00061014u; // B saw A's marker? (bug => 1)
            constexpr uint32_t kRiAOwn = 0x00061018u;      // A saw its own marker? (must be 1)
            constexpr uint32_t kRiBOwn = 0x0006101Cu;      // B saw its own marker? (must be 1)

            runtime.registerFunction(kEntryA, &stepIsoA);
            runtime.registerFunction(kEntryB, &stepIsoB);
            runtime.registerFunction(kMarkerA, &stepNoop);
            runtime.registerFunction(kMarkerB, &stepNoop);

            // Both semas start at 0: A signals B then waits on A; B waits on B
            // then signals A - a one-shot ping-pong handoff, not a loop.
            const int32_t sidA = createSchedSema(rdram.data(), &runtime, 0, 1);
            const int32_t sidB = createSchedSema(rdram.data(), &runtime, 0, 1);
            t.IsTrue(sidA > 0 && sidB > 0, "R1: semas created");
            if (sidA <= 0 || sidB <= 0)
            {
                return;
            }
            rdramWrite32(rdram, kRiSidA, static_cast<uint32_t>(sidA));
            rdramWrite32(rdram, kRiSidB, static_cast<uint32_t>(sidB));
            rdramWrite32(rdram, kRiAForeign, 0u);
            rdramWrite32(rdram, kRiBForeign, 0u);
            rdramWrite32(rdram, kRiAOwn, 0u);
            rdramWrite32(rdram, kRiBOwn, 0u);

            const int32_t tidA = startSchedWorker(rdram.data(), &runtime,
                                                  kEntryA, 10, 0x00530000u, 0x2000u);
            const int32_t tidB = startSchedWorker(rdram.data(), &runtime,
                                                  kEntryB, 10, 0x00534000u, 0x2000u);
            t.IsTrue(tidA > 0 && tidB > 0, "R1: both fibers started");
            if (tidA <= 0 || tidB <= 0)
            {
                deleteSchedSema(rdram.data(), &runtime, sidA);
                deleteSchedSema(rdram.data(), &runtime, sidB);
                return;
            }

            const bool drained = waitUntil([&]()
            {
                return g_activeThreads.load(std::memory_order_acquire) <= 0;
            }, std::chrono::milliseconds(3000));
            t.IsTrue(drained, "R1: both fibers completed the handoff and exited");

            // THE regression assertions: under the shared thread_local, A and
            // B push into the SAME ring, so each fiber's post-handoff trace
            // is contaminated with the other's markers.
            t.Equals(rdramRead32(rdram, kRiAForeign), 0u,
                     "R1: fiber A's trace must not contain fiber B's markers");
            t.Equals(rdramRead32(rdram, kRiBForeign), 0u,
                     "R1: fiber B's trace must not contain fiber A's markers");
            t.Equals(rdramRead32(rdram, kRiAOwn), 1u,
                     "R1: fiber A's trace must contain fiber A's own marker");
            t.Equals(rdramRead32(rdram, kRiBOwn), 1u,
                     "R1: fiber B's trace must contain fiber B's own marker");

            deleteSchedSema(rdram.data(), &runtime, sidA);
            deleteSchedSema(rdram.data(), &runtime, sidB);
        });

        tc.Run("R2: dispatch-trace ring is fresh after genuine tid reuse", [](TestCase &t)
        {
            SchedFixture fx;
            PS2Runtime &runtime = fx.runtime;
            std::vector<uint8_t> &rdram = fx.rdram;

            constexpr uint32_t kEntryReuse = 0x00700C00u;
            constexpr uint32_t kReuseMarker = 0x00700C80u;
            constexpr uint32_t kReuseRun = 0x00062000u;             // 0 => first fiber, 1 => reused
            constexpr uint32_t kReuseNotInherited0 = 0x00062004u;   // fiber1 did not inherit a prior marker (expect 1)
            constexpr uint32_t kReuseNonEmpty0 = 0x00062008u;       // fiber1 pushed its own marker (expect 1)
            constexpr uint32_t kReuseNotInherited1 = 0x0006200Cu;   // fiber2 (reused tid) did not inherit fiber1's marker (expect 1)

            runtime.registerFunction(kEntryReuse, &stepReuseProbe);
            runtime.registerFunction(kReuseMarker, &stepNoop);
            rdramWrite32(rdram, kReuseRun, 0u);
            rdramWrite32(rdram, kReuseNotInherited0, 0u);
            rdramWrite32(rdram, kReuseNonEmpty0, 0u);
            rdramWrite32(rdram, kReuseNotInherited1, 0u);

            const int32_t T1 = startSchedWorker(rdram.data(), &runtime,
                                                kEntryReuse, 10, 0x00538000u, 0x2000u);
            t.IsTrue(T1 > 0, "R2: fiber 1 started");
            if (T1 <= 0)
            {
                return;
            }

            const bool drained1 = waitUntil([&]()
            {
                return g_activeThreads.load(std::memory_order_acquire) <= 0;
            }, std::chrono::milliseconds(3000));
            t.IsTrue(drained1, "R2: fiber 1 fully torn down (FiberContext destroyed)");

            // Force GENUINE tid reuse: erase T1 from the kernel thread map
            // (DeleteThread requires THS_DORMANT, which on_fiber_exit already
            // set), then seed the tid allocator so the next CreateThread hands
            // out exactly T1 again. Both operations are serialized under
            // g_thread_map_mutex, the same lock CreateThread's allocator holds.
            {
                R5900Context d{};
                setRegU32(d, 4, static_cast<uint32_t>(T1));
                ps2_syscalls::DeleteThread(rdram.data(), &d, &runtime);
            }
            {
                std::lock_guard<std::mutex> lk(g_thread_map_mutex);
                g_nextThreadId = T1;
            }

            rdramWrite32(rdram, kReuseRun, 1u);
            const int32_t T2 = startSchedWorker(rdram.data(), &runtime,
                                                kEntryReuse, 10, 0x0053C000u, 0x2000u);
            t.Equals(T2, T1, "R2: tid genuinely recycled (not merely a fresh, different tid)");

            const bool drained2 = waitUntil([&]()
            {
                return g_activeThreads.load(std::memory_order_acquire) <= 0;
            }, std::chrono::milliseconds(3000));
            t.IsTrue(drained2, "R2: fiber 2 (reused tid) completed");

            // THE regression assertion: under the shared thread_local, fiber 2
            // (running on the same executor OS thread) sees fiber 1's leftover
            // 0x00700c80 marker still sitting in the one global ring.
            t.Equals(rdramRead32(rdram, kReuseNotInherited0), 1u,
                     "R2: fiber 1 (first ever on this tid) starts with no prior marker");
            t.Equals(rdramRead32(rdram, kReuseNonEmpty0), 1u,
                     "R2: fiber 1 successfully pushed its own marker");
            t.Equals(rdramRead32(rdram, kReuseNotInherited1), 1u,
                     "R2: fiber 2 (reused tid) must NOT inherit fiber 1's marker");
        });
    }); // MiniTest::Case("SchedulerRecoveryIsolation")
}

// ---------------------------------------------------------------------------
// Shared-guest-stack bug class (B1/B3/B4): rpcInvokeFunction (RPC/override
// invoke), inline DMAC handler dispatch, and MPEG stream callbacks each ran
// recompiled guest code on a per-OS-thread `thread_local` scratch stack. Under
// the N=1 fiber scheduler every fiber runs on the ONE guest-executor OS
// thread, so that thread_local is a SINGLE stack shared by every fiber (and
// by one fiber re-entering the same path). A fiber can yield mid-invoke (the
// invoked guest body hits a back-edge / blocking syscall), so a second fiber
// entering the same path sets $sp to the SAME top and overwrites the parked
// fiber's live frames. Fixed by GuestScratchStack: a fresh, RAII-released
// guest-heap reservation per invocation (see ps2_runtime.h), so interleaved
// or re-entrant invocations always run on disjoint stacks.
//
// I1/I2 are RED on build-ucontext against the old shared thread_local
// (topA == topB) and GREEN with the fix (disjoint reservations). On the
// pthread backend each fiber is its own OS thread, so the old thread_local is
// already per-fiber and both tests pass even unfixed — mirrors the existing
// SchedulerRecoveryIsolation banner note. I3 exercises the GuestScratchStack
// contract directly (stands in for B4 — dispatchGuestStreamCallback is
// anon-namespace-internal and reachable only through the full MPEG decode
// path, disproportionate to stand up for a unit test; B4's site uses the same
// helper proven here and at I1/I2).
//
// LOAD-BEARING CHOREOGRAPHY: a sequential (non-overlapping) two-fiber test
// FALSE-PASSES even on the fix, because guestMalloc recycles a just-freed
// address — if fiber A frees before B allocates, topA == topB regardless of
// the fix. Both reservations must be LIVE SIMULTANEOUSLY: fiber A parks
// INSIDE its invoke/dispatch (its GuestScratchStack not yet destructed) via a
// semaphore wait executed from within the invoked guest body / handler body,
// while fiber B allocates and records. Do not "simplify" this overlap away.
// ---------------------------------------------------------------------------
void register_scheduler_stack_isolation_tests()
{
    MiniTest::Case("SchedulerStackIsolation", [](TestCase &tc)
    {
        // I1 --- B1: two fibers interleaving through rpcInvokeFunction must run
        // on disjoint scratch stacks. rpcInvokeFunction gets its stack from a
        // fresh, per-invocation GuestScratchStack reservation (see
        // ps2_runtime.h), so two interleaved invocations never share $sp
        // (topA != topB).
        tc.Run("I1: rpc/override invoke stack is isolated across interleaved fibers", [](TestCase &t)
        {
            SchedFixture fx;
            PS2Runtime &runtime = fx.runtime;
            std::vector<uint8_t> &rdram = fx.rdram;

            constexpr uint32_t kSysA = 0x00005A01u, kSysB = 0x00005A02u;
            constexpr uint32_t kEntryA = 0x00701000u, kEntryB = 0x00701100u;
            constexpr uint32_t kInvA = 0x00701200u, kInvB = 0x00701300u;

            runtime.registerFunction(kEntryA, &stepInvokeEntryA);
            runtime.registerFunction(kEntryB, &stepInvokeEntryB);
            runtime.registerFunction(kInvA,  &stepInvokeRecordA);
            runtime.registerFunction(kInvB,  &stepInvokeRecordB);

            // Register two DISTINCT syscall overrides (distinct numbers dodge
            // B2's shared s_activeSyscallOverrides reentrancy guard; that bug
            // is tracked separately by SchedulerOverrideIsolation).
            { R5900Context s{}; setRegU32(s,4,kSysA); setRegU32(s,5,kInvA);
              ps2_syscalls::SetSyscall(rdram.data(), &s, &runtime); }
            { R5900Context s{}; setRegU32(s,4,kSysB); setRegU32(s,5,kInvB);
              ps2_syscalls::SetSyscall(rdram.data(), &s, &runtime); }

            const int32_t sidA = createSchedSema(rdram.data(), &runtime, 0, 1);
            const int32_t sidB = createSchedSema(rdram.data(), &runtime, 0, 1);
            t.IsTrue(sidA > 0 && sidB > 0, "I1: semas created");
            rdramWrite32(rdram, kSiSemA, static_cast<uint32_t>(sidA));
            rdramWrite32(rdram, kSiSemB, static_cast<uint32_t>(sidB));
            rdramWrite32(rdram, kSiSysA, kSysA);
            rdramWrite32(rdram, kSiSysB, kSysB);
            rdramWrite32(rdram, kSiTopA, 0u);
            rdramWrite32(rdram, kSiTopB, 0u);

            const int32_t tidA = startSchedWorker(rdram.data(), &runtime, kEntryA, 10, 0x00540000u, 0x2000u);
            const int32_t tidB = startSchedWorker(rdram.data(), &runtime, kEntryB, 10, 0x00544000u, 0x2000u);
            t.IsTrue(tidA > 0 && tidB > 0, "I1: both fibers started");

            const bool drained = waitUntil([&]{ return g_activeThreads.load(std::memory_order_acquire) <= 0; },
                                           std::chrono::milliseconds(3000));
            t.IsTrue(drained, "I1: both fibers completed the handoff and exited");

            const uint32_t topA = rdramRead32(rdram, kSiTopA);
            const uint32_t topB = rdramRead32(rdram, kSiTopB);
            t.IsTrue(topA != 0u && topB != 0u, "I1: both invokes reserved a scratch stack");
            t.IsTrue(topA != topB,
                     "I1: interleaved invokes must run on DISJOINT scratch stacks");

            // Cleanup: erase the global overrides so later suites are unaffected.
            { R5900Context s{}; setRegU32(s,4,kSysA); setRegU32(s,5,0u);
              ps2_syscalls::SetSyscall(rdram.data(), &s, &runtime); }
            { R5900Context s{}; setRegU32(s,4,kSysB); setRegU32(s,5,0u);
              ps2_syscalls::SetSyscall(rdram.data(), &s, &runtime); }
            deleteSchedSema(rdram.data(), &runtime, sidA);
            deleteSchedSema(rdram.data(), &runtime, sidB);
        });

        // I2 --- B3: two fibers interleaving through inline DMAC dispatch must
        // run handlers on disjoint stacks. RED on ucontext against the shared
        // getAsyncHandlerStackTop cache (dmacTopA == dmacTopB).
        tc.Run("I2: inline DMAC handler stack is isolated across interleaved fibers", [](TestCase &t)
        {
            SchedFixture fx;
            PS2Runtime &runtime = fx.runtime;
            std::vector<uint8_t> &rdram = fx.rdram;

            constexpr uint32_t kEntryA = 0x00701400u, kEntryB = 0x00701500u;
            constexpr uint32_t kHndA = 0x00701600u, kHndB = 0x00701700u;

            runtime.registerFunction(kEntryA, &stepDmacDispatchA);
            runtime.registerFunction(kEntryB, &stepDmacDispatchB);
            runtime.registerFunction(kHndA,  &stepDmacRecordA);
            runtime.registerFunction(kHndB,  &stepDmacRecordB);

            int32_t hidA = -1, hidB = -1;
            { R5900Context a{}; setRegU32(a,4,5u); setRegU32(a,5,kHndA); setRegU32(a,6,0u); setRegU32(a,7,0u);
              ps2_syscalls::AddDmacHandler(rdram.data(), &a, &runtime); hidA = getRegS32(a, 2); }
            { R5900Context a{}; setRegU32(a,4,6u); setRegU32(a,5,kHndB); setRegU32(a,6,0u); setRegU32(a,7,0u);
              ps2_syscalls::AddDmacHandler(rdram.data(), &a, &runtime); hidB = getRegS32(a, 2); }
            t.IsTrue(hidA > 0 && hidB > 0, "I2: DMAC handlers registered");

            const int32_t sidA = createSchedSema(rdram.data(), &runtime, 0, 1);
            const int32_t sidB = createSchedSema(rdram.data(), &runtime, 0, 1);
            rdramWrite32(rdram, kSiSemA, static_cast<uint32_t>(sidA));
            rdramWrite32(rdram, kSiSemB, static_cast<uint32_t>(sidB));
            rdramWrite32(rdram, kSiDmacTopA, 0u);
            rdramWrite32(rdram, kSiDmacTopB, 0u);

            const int32_t tidA = startSchedWorker(rdram.data(), &runtime, kEntryA, 10, 0x00548000u, 0x2000u);
            const int32_t tidB = startSchedWorker(rdram.data(), &runtime, kEntryB, 10, 0x0054C000u, 0x2000u);
            t.IsTrue(tidA > 0 && tidB > 0, "I2: both fibers started");

            const bool drained = waitUntil([&]{ return g_activeThreads.load(std::memory_order_acquire) <= 0; },
                                           std::chrono::milliseconds(3000));
            t.IsTrue(drained, "I2: both fibers completed dispatch and exited");

            const uint32_t topA = rdramRead32(rdram, kSiDmacTopA);
            const uint32_t topB = rdramRead32(rdram, kSiDmacTopB);
            t.IsTrue(topA != 0u && topB != 0u, "I2: both dispatches reserved a handler stack");
            t.IsTrue(topA != topB,
                     "I2: interleaved inline DMAC dispatches must run on DISJOINT stacks");

            // Cleanup: RemoveDmacHandler reads cause=$a0, handlerId=$a1.
            { R5900Context r{}; setRegU32(r,4,5u); setRegU32(r,5,static_cast<uint32_t>(hidA));
              ps2_syscalls::RemoveDmacHandler(rdram.data(), &r, &runtime); }
            { R5900Context r{}; setRegU32(r,4,6u); setRegU32(r,5,static_cast<uint32_t>(hidB));
              ps2_syscalls::RemoveDmacHandler(rdram.data(), &r, &runtime); }
            deleteSchedSema(rdram.data(), &runtime, sidA);
            deleteSchedSema(rdram.data(), &runtime, sidB);
        });

        // I3 --- shared-mechanism contract (stands in for B4). dispatchGuest-
        // StreamCallback is anon-namespace-internal and standing up MPEG demux
        // state to trigger it from a fiber is disproportionate; instead assert
        // the GuestScratchStack contract directly: two LIVE reservations are
        // disjoint. B4's site uses this exact helper, and the fiber-level proof
        // that it isolates under interleaving is I1/I2.
        tc.Run("I3: two live GuestScratchStack reservations are disjoint", [](TestCase &t)
        {
            PS2Runtime runtime;
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0u);
            constexpr uint32_t kSize = 0x4000u;

            GuestScratchStack a(&runtime, kSize);
            GuestScratchStack b(&runtime, kSize); // b alive WHILE a is alive
            t.IsTrue(a.valid() && b.valid(), "I3: both reservations succeeded");
            t.IsTrue(a.top() != b.top(), "I3: live reservations have distinct tops");
            const uint32_t hi = (a.top() > b.top()) ? a.top() : b.top();
            const uint32_t lo = (a.top() > b.top()) ? b.top() : a.top();
            t.IsTrue(hi - lo >= kSize, "I3: reserved ranges do not overlap");
            // After scope exit both free; a subsequent reservation may recycle
            // an address (expected) — which is exactly why I1/I2 keep both live.

            runtime.requestStop();
        });
    }); // MiniTest::Case("SchedulerStackIsolation")
}

// ---------------------------------------------------------------------------
// B2: dispatchSyscallOverride reentrancy-guard bug -- see the helper
// banner above for the shared-thread_local mechanism and pthread-backend
// caveat. This case proves fiber B's override actually runs while A is
// parked inside its own override.
// ---------------------------------------------------------------------------
void register_scheduler_override_isolation_tests()
{
    MiniTest::Case("SchedulerOverrideIsolation", [](TestCase &tc)
    {
        tc.Run("O1: a fiber parked inside a syscall override must not suppress another fiber's override for the same syscall",
               [](TestCase &t)
        {
            SchedFixture fx;
            PS2Runtime &runtime = fx.runtime;
            std::vector<uint8_t> &rdram = fx.rdram;

            runtime.registerFunction(kOvEntryA, &stepOverrideA);
            runtime.registerFunction(kOvEntryB, &stepOverrideB);
            runtime.registerFunction(kOvHandler, &stepOverrideHandler);

            // Register the override directly (avoids SetSyscall's guest-memory
            // mirroring side effects); State.h is already included.
            {
                std::lock_guard<std::mutex> lk(g_syscall_override_mutex);
                g_syscall_overrides[kOvSyscall] = kOvHandler;
            }

            const int32_t sidStartB  = createSchedSema(rdram.data(), &runtime, 0, 1);
            const int32_t sidResumeA = createSchedSema(rdram.data(), &runtime, 0, 1);
            t.IsTrue(sidStartB > 0 && sidResumeA > 0, "O1: semas created");
            if (sidStartB <= 0 || sidResumeA <= 0)
            {
                { std::lock_guard<std::mutex> lk(g_syscall_override_mutex); g_syscall_overrides.erase(kOvSyscall); }
                return;
            }
            rdramWrite32(rdram, kOvSidStartB,  static_cast<uint32_t>(sidStartB));
            rdramWrite32(rdram, kOvSidResumeA, static_cast<uint32_t>(sidResumeA));
            rdramWrite32(rdram, kOvAEntered, 0u);
            rdramWrite32(rdram, kOvARan,     0u);
            rdramWrite32(rdram, kOvBRan,     0u);

            const int32_t tidA = startSchedWorker(rdram.data(), &runtime,
                                                  kOvEntryA, 10, 0x00550000u, 0x2000u);
            const int32_t tidB = startSchedWorker(rdram.data(), &runtime,
                                                  kOvEntryB, 10, 0x00554000u, 0x2000u);
            t.IsTrue(tidA > 0 && tidB > 0, "O1: both fibers started");
            if (tidA <= 0 || tidB <= 0)
            {
                deleteSchedSema(rdram.data(), &runtime, sidStartB);
                deleteSchedSema(rdram.data(), &runtime, sidResumeA);
                { std::lock_guard<std::mutex> lk(g_syscall_override_mutex); g_syscall_overrides.erase(kOvSyscall); }
                return;
            }

            const bool drained = waitUntil([&]()
            {
                return g_activeThreads.load(std::memory_order_acquire) <= 0;
            }, std::chrono::milliseconds(3000));
            // A timeout here means the PARK mechanism failed (WaitSema nested in
            // the override invoke) — NOT the B2 guard. Debug the park, not the fix.
            t.IsTrue(drained, "O1: both fibers completed the handoff and exited");

            // Liveness: prove A really parked inside its override and resumed —
            // otherwise kOvBRan could be satisfied vacuously.
            t.Equals(rdramRead32(rdram, kOvAEntered), 1u,
                     "O1: fiber A entered its override handler");
            t.Equals(rdramRead32(rdram, kOvARan), 1u,
                     "O1: fiber A resumed from its park inside the override");

            // THE regression assertion. Bug: B's override is skipped as
            // 'reentrant' (A's N still on the shared stack) => kOvBRan == 0.
            t.Equals(rdramRead32(rdram, kOvBRan), 1u,
                     "O1: fiber B's own override must run while fiber A is parked inside the same syscall's override");

            deleteSchedSema(rdram.data(), &runtime, sidStartB);
            deleteSchedSema(rdram.data(), &runtime, sidResumeA);
            { std::lock_guard<std::mutex> lk(g_syscall_override_mutex); g_syscall_overrides.erase(kOvSyscall); }
        });
    }); // MiniTest::Case("SchedulerOverrideIsolation")
}

// ---------------------------------------------------------------------------
// TerminateThread must not hang when the target has an equal-priority sibling
// that stays runnable after the target exits.
//
// join_fiber() applies a temporary priority floor to the joiner so it runs
// strictly AFTER the target. The floor must be the target's OWN level, not
// one below it: PS2 EE priority is "lower number = higher priority", and the
// run queue is FIFO within a level. Flooring the joiner one level below the
// target (t->priority + 1) drops it beneath every equal-priority sibling of
// the target. If any such sibling stays runnable after the target finishes,
// the joiner never regains the run-queue head, never re-observes the target
// as finished, and join_fiber() — hence TerminateThread — never returns.
//
// Shape (all guest fibers, N=1 cooperative executor):
//   S1,S2 (prio 10): a one-permit semaphore ping-pong. Exactly one of the two
//                    is runnable at any instant, so the prio-10 level is
//                    continuously "hot" for the whole test, including after
//                    the target exits.
//   B     (prio 10): the terminate target. Spins until killed by A.
//   A     (prio  5): calls TerminateThread(B). join_fiber floors A to B's
//                    level. Fixed (floor = 10): A ties the pingers and, by
//                    FIFO rotation, cycles back to the head to see B finished
//                    and returns. Buggy (floor = 11): A sits below the ever-
//                    runnable pingers forever and TerminateThread hangs.
//
// The assertion is a bounded-time poll on A's "join returned" flag, so the
// buggy build FAILS via timeout instead of hanging the binary. Teardown stops
// the pingers and drains to zero active threads in BOTH outcomes (and the
// SchedFixture destructor's scheduler_shutdown is the final backstop), so no
// runnable fiber is ever leaked into the rest of the suite.
// ---------------------------------------------------------------------------
void register_scheduler_join_starvation_tests()
{
    MiniTest::Case("SchedulerJoinStarvation", [](TestCase &tc)
    {
        tc.Run("J1: TerminateThread returns when the target has an equal-priority sibling that outlives it",
               [](TestCase &t)
        {
            SchedFixture fx;
            PS2Runtime &runtime = fx.runtime;
            std::vector<uint8_t> &rdram = fx.rdram;

            constexpr uint32_t kEntryPingA  = 0x00760000u;
            constexpr uint32_t kEntryPingB  = 0x00760100u;
            constexpr uint32_t kEntryTarget = 0x00760200u;
            constexpr uint32_t kEntryJoiner = 0x00760300u;

            runtime.registerFunction(kEntryPingA,  &stepPingA);
            runtime.registerFunction(kEntryPingB,  &stepPingB);
            runtime.registerFunction(kEntryTarget, &stepJoinStarveTarget);
            runtime.registerFunction(kEntryJoiner, &stepJoinStarveJoiner);

            // One permit circulates X -> Y -> X, keeping the prio-10 level hot.
            const int32_t sidX = createSchedSema(rdram.data(), &runtime, 1, 1);
            const int32_t sidY = createSchedSema(rdram.data(), &runtime, 0, 1);
            t.IsTrue(sidX > 0 && sidY > 0, "J1: ping-pong semas created");
            if (sidX <= 0 || sidY <= 0)
            {
                deleteSchedSema(rdram.data(), &runtime, sidX);
                deleteSchedSema(rdram.data(), &runtime, sidY);
                return;
            }
            rdramWrite32(rdram, kThSidX, static_cast<uint32_t>(sidX));
            rdramWrite32(rdram, kThSidY, static_cast<uint32_t>(sidY));
            rdramWrite32(rdram, kThStop, 0u);
            rdramWrite32(rdram, kThCount, 0u);
            rdramWrite32(rdram, kJsBStarted, 0u);
            rdramWrite32(rdram, kJsJoinReturned, 0u);
            rdramWrite32(rdram, kJsTargetTid, 0u);

            // Pingers first (prio 10), then confirm the level is circulating.
            const int32_t tidS1 = startSchedWorker(rdram.data(), &runtime, kEntryPingA, 10, 0x00560000u, 0x2000u);
            const int32_t tidS2 = startSchedWorker(rdram.data(), &runtime, kEntryPingB, 10, 0x00562000u, 0x2000u);
            t.IsTrue(tidS1 > 0 && tidS2 > 0, "J1: ping-pong pair started");

            const bool pingLive = waitUntil([&]()
            {
                return rdramRead32(rdram, kThCount) >= 3u;
            }, std::chrono::milliseconds(1000));
            t.IsTrue(pingLive, "J1: prio-10 ping-pong is circulating");

            // Target B (prio 10) — never self-exits; killed by A's Terminate.
            const int32_t tidB = startSchedWorker(rdram.data(), &runtime, kEntryTarget, 10, 0x00564000u, 0x2000u);
            t.IsTrue(tidB > 0, "J1: target fiber started");
            const bool bRunning = waitUntil([&]()
            {
                return rdramRead32(rdram, kJsBStarted) == 1u;
            }, std::chrono::milliseconds(1000));
            t.IsTrue(bRunning, "J1: target fiber is running");

            // Joiner A (prio 5) terminates B; join_fiber floors A to B's level.
            rdramWrite32(rdram, kJsTargetTid, static_cast<uint32_t>(tidB));
            const int32_t tidA = startSchedWorker(rdram.data(), &runtime, kEntryJoiner, 5, 0x00566000u, 0x2000u);
            t.IsTrue(tidA > 0, "J1: joiner fiber started");

            // THE regression assertion: TerminateThread(B) returns within a
            // bounded number of scheduler rounds. Buggy (+1 floor): A is
            // stranded below the pingers and this never flips -> timeout ->
            // FAIL (binary still exits; teardown below quiesces everything).
            const bool joinReturned = waitUntil([&]()
            {
                return rdramRead32(rdram, kJsJoinReturned) == 1u;
            }, std::chrono::milliseconds(3000));
            t.IsTrue(joinReturned,
                     "J1: TerminateThread(target) returned — join floor must be the "
                     "target's own priority level, not target+1");

            // ---- Teardown (runs in BOTH pass and fail outcomes) ----
            // Stop the pingers and release any WaitSema-parked pinger so it
            // re-checks kThStop and exits. Once the prio-10 level drains, a
            // stranded (buggy-case) joiner also regains the head, observes B
            // finished, and returns — so g_activeThreads reaches zero here,
            // proving clean quiescence without leaking a runnable fiber.
            rdramWrite32(rdram, kThStop, 1u);
            for (int i = 0; i < 4; ++i)
            {
                signalSchedSema(rdram.data(), &runtime, sidX);
                signalSchedSema(rdram.data(), &runtime, sidY);
            }
            const bool drained = waitUntil([&]()
            {
                return g_activeThreads.load(std::memory_order_acquire) <= 0;
            }, std::chrono::milliseconds(3000));
            t.IsTrue(drained, "J1: all fibers quiesced after teardown");

            deleteSchedSema(rdram.data(), &runtime, sidX);
            deleteSchedSema(rdram.data(), &runtime, sidY);
        });
    }); // MiniTest::Case("SchedulerJoinStarvation")
}
