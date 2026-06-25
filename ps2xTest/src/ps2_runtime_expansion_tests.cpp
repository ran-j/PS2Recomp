#include "MiniTest.h"
#include "ps2recomp/code_generator.h"
#include "ps2recomp/instructions.h"
#include "ps2recomp/r5900_decoder.h"
#include "ps2recomp/types.h"
#include "ps2_runtime.h"
#include "runtime/ps2_memory.h"
#include "ps2_syscalls.h"
#include "ps2_stubs.h"
#include "runtime/ps2_gs_gpu.h"
#include "runtime/ps2_gs_psmct32.h"
#include "ps2_runtime_macros.h"
#include "Stubs/MPEG.h"
#include "Stubs/CD.h"
#include "Stubs/Audio.h"
#include "Stubs/GS.h"
#include "Stubs/VU.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <exception>
#include <string>
#include <thread>
#include <vector>

using namespace ps2recomp;
using namespace ps2_syscalls;

namespace
{
    constexpr uint32_t COP0_CAUSE_BD = 0x80000000u;
    constexpr uint32_t COP0_CAUSE_EXCCODE_MASK = 0x0000007Cu;
    constexpr uint32_t COP0_STATUS_EXL = 0x00000002u;
    constexpr uint32_t COP0_STATUS_BEV = 0x00400000u;
    constexpr uint32_t EXCEPTION_VECTOR_GENERAL = 0x80000080u;
    constexpr uint32_t EXCEPTION_VECTOR_BOOT = 0xBFC00200u;

    constexpr int KE_OK = 0;

    void setRegU32(R5900Context &ctx, int reg, uint32_t value)
    {
        ctx.r[reg] = _mm_set_epi64x(0, static_cast<int64_t>(value));
    }

    int32_t getRegS32(const R5900Context &ctx, int reg)
    {
        return static_cast<int32_t>(::getRegU32(&ctx, reg));
    }

    uint32_t makeVifCmd(uint8_t opcode, uint8_t num, uint16_t imm)
    {
        return (static_cast<uint32_t>(opcode) << 24) |
               (static_cast<uint32_t>(num) << 16) |
               static_cast<uint32_t>(imm);
    }

    uint32_t makeVuLq(uint8_t dest, uint8_t targetVf, uint8_t baseVi, int16_t imm)
    {
        return (static_cast<uint32_t>(dest & 0xFu) << 21) |
               (static_cast<uint32_t>(targetVf & 0x1Fu) << 16) |
               (static_cast<uint32_t>(baseVi & 0x1Fu) << 11) |
               (static_cast<uint32_t>(imm) & 0x7FFu);
    }

    uint32_t makeVuSq(uint8_t dest, uint8_t sourceVf, uint8_t baseVi, int16_t imm)
    {
        return (0x01u << 25) |
               (static_cast<uint32_t>(dest & 0xFu) << 21) |
               (static_cast<uint32_t>(baseVi & 0x1Fu) << 16) |
               (static_cast<uint32_t>(sourceVf & 0x1Fu) << 11) |
               (static_cast<uint32_t>(imm) & 0x7FFu);
    }

    uint32_t makeVuAdd(uint8_t dest, uint8_t fd, uint8_t fs, uint8_t ft)
    {
        return (static_cast<uint32_t>(dest & 0xFu) << 21) |
               (static_cast<uint32_t>(ft & 0x1Fu) << 16) |
               (static_cast<uint32_t>(fs & 0x1Fu) << 11) |
               (static_cast<uint32_t>(fd & 0x1Fu) << 6) |
               0x28u;
    }

    void writeVuInstructionPair(uint8_t *code, uint32_t pc, uint32_t lower, uint32_t upper)
    {
        std::memcpy(code + pc, &lower, sizeof(lower));
        std::memcpy(code + pc + sizeof(lower), &upper, sizeof(upper));
    }

    bool hasSignedRdWrite(const std::string &generated, uint8_t rd)
    {
        if (rd == 0u)
        {
            return false;
        }

        const std::string needle = "SET_GPR_S32(ctx, " + std::to_string(rd) + ",";
        return generated.find(needle) != std::string::npos;
    }

    template <typename Predicate>
    bool waitUntil(Predicate pred, std::chrono::milliseconds timeout)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (pred())
            {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return pred();
    }

    uint32_t frameOffsetBytes(uint32_t x, uint32_t y, uint32_t fbw)
    {
        return GSPSMCT32::addrPSMCT32(0u, (fbw != 0u) ? fbw : 1u, x, y);
    }

    void testRuntimeWorkerLoop(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (!ctx || !runtime)
        {
            return;
        }

        // Keep touching guest memory so teardown races are easier to catch.
        (void)Ps2FastRead64(rdram, static_cast<uint32_t>(0x01FFFFF8u + (ctx->insn_count & 0x7u)));
        ++ctx->insn_count;

        if (runtime->isStopRequested())
        {
            ctx->pc = 0u;
            return;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    std::atomic<int32_t> gSerializedGuestActive{0};
    std::atomic<int32_t> gSerializedGuestMaxActive{0};
    std::atomic<int32_t> gPreemptionPolicyEntryCount{0};
    std::atomic<bool> gPreemptionPolicyAllowFirstProbe{false};
    std::atomic<bool> gPreemptionPolicyPeerRan{false};

    void testSerializedGuestStep(uint8_t *, R5900Context *ctx, PS2Runtime *)
    {
        const int32_t active = gSerializedGuestActive.fetch_add(1, std::memory_order_acq_rel) + 1;
        int32_t observedMax = gSerializedGuestMaxActive.load(std::memory_order_relaxed);
        while (observedMax < active &&
               !gSerializedGuestMaxActive.compare_exchange_weak(
                   observedMax,
                   active,
                   std::memory_order_release,
                   std::memory_order_relaxed))
        {
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        gSerializedGuestActive.fetch_sub(1, std::memory_order_acq_rel);
        if (ctx)
        {
            ctx->pc = 0u;
        }
    }

    void testPreemptionPolicyStep(uint8_t *, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (!ctx || !runtime)
        {
            return;
        }

        const int32_t entryIndex = gPreemptionPolicyEntryCount.fetch_add(1, std::memory_order_acq_rel) + 1;
        if (entryIndex == 1)
        {
            while (!gPreemptionPolicyAllowFirstProbe.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }

            bool shouldPreempt = false;
            for (int attempt = 0; attempt < 256 &&
                                  !shouldPreempt;
                 ++attempt)
            {
                shouldPreempt = runtime->shouldPreemptGuestExecution();
            }
            setRegU32(*ctx, 2, shouldPreempt ? 1u : 0u);
        }
        else
        {
            gPreemptionPolicyPeerRan.store(true, std::memory_order_release);
            setRegU32(*ctx, 2, 2u);
        }

        ctx->pc = 0u;
    }

    void testResumeOwnerFallbackHandler(uint8_t *, R5900Context *ctx, PS2Runtime *)
    {
        if (ctx)
        {
            setRegU32(*ctx, 2, 0x00ABC123u);
            ctx->pc = 0u;
        }
    }

    void testResumeNextFunctionHandler(uint8_t *, R5900Context *ctx, PS2Runtime *)
    {
        if (ctx)
        {
            setRegU32(*ctx, 2, 0x00555555u);
            ctx->pc = 0u;
        }
    }

    constexpr uint32_t kAsyncCounterAddr = 0x2400u;

    void testWaitForAsyncCounter(uint8_t *rdram, R5900Context *ctx, PS2Runtime *)
    {
        if (!rdram || !ctx)
        {
            return;
        }

        uint32_t counter = 0u;
        do
        {
            std::memcpy(&counter, rdram + kAsyncCounterAddr, sizeof(counter));
            if (counter == 0u)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        } while (counter == 0u);

        ctx->pc = 0u;
    }

    void testSignalAsyncCounter(uint8_t *rdram, R5900Context *ctx, PS2Runtime *)
    {
        if (rdram)
        {
            const uint32_t counter = 1u;
            std::memcpy(rdram + kAsyncCounterAddr, &counter, sizeof(counter));
        }

        if (ctx)
        {
            ctx->pc = 0u;
        }
    }

    std::atomic<uint32_t> gAsyncCallbackObservedSp{0u};
    std::atomic<uint32_t> gAsyncCallbackObservedGp{0u};

    void testRecordAsyncCallbackStack(uint8_t *, R5900Context *ctx, PS2Runtime *)
    {
        if (!ctx)
        {
            return;
        }

        gAsyncCallbackObservedSp.store(::getRegU32(ctx, 29), std::memory_order_release);
        gAsyncCallbackObservedGp.store(::getRegU32(ctx, 28), std::memory_order_release);
        ctx->pc = 0u;
    }

    std::atomic<uint32_t> gMpegStreamCallbackCount{0u};
    std::atomic<uint32_t> gMpegStreamCallbackMpeg{0u};
    std::atomic<uint32_t> gMpegStreamCallbackType{0u};
    std::atomic<uint32_t> gMpegStreamCallbackDataAddr{0u};
    std::atomic<uint32_t> gMpegStreamCallbackLen{0u};
    std::atomic<uint32_t> gMpegStreamCallbackUserData{0u};

    void testRecordMpegStreamCallback(uint8_t *rdram, R5900Context *ctx, PS2Runtime *)
    {
        if (!rdram || !ctx)
        {
            return;
        }

        const uint32_t cbData = ::getRegU32(ctx, 5);
        uint32_t type = 0u;
        uint32_t dataAddr = 0u;
        uint32_t len = 0u;
        std::memcpy(&type, rdram + cbData + 0x00u, sizeof(type));
        std::memcpy(&dataAddr, rdram + cbData + 0x08u, sizeof(dataAddr));
        std::memcpy(&len, rdram + cbData + 0x0Cu, sizeof(len));

        gMpegStreamCallbackMpeg.store(::getRegU32(ctx, 4), std::memory_order_release);
        gMpegStreamCallbackType.store(type, std::memory_order_release);
        gMpegStreamCallbackDataAddr.store(dataAddr, std::memory_order_release);
        gMpegStreamCallbackLen.store(len, std::memory_order_release);
        gMpegStreamCallbackUserData.store(::getRegU32(ctx, 6), std::memory_order_release);
        gMpegStreamCallbackCount.fetch_add(1u, std::memory_order_acq_rel);
        ctx->pc = 0u;
    }

}

void register_ps2_runtime_expansion_tests()
{
    MiniTest::Case("PS2RuntimeExpansion", [](TestCase &tc)
    {
        tc.Run("differential decoder/codegen gpr-write contract for MULT and DIV families", [](TestCase &t)
        {
            R5900Decoder decoder;
            CodeGenerator generator({}, {});

            const struct
            {
                const char *name;
                uint32_t raw;
            } cases[] = {
                {"MULT rd!=0", (OPCODE_SPECIAL << 26) | (4u << 21) | (5u << 16) | (3u << 11) | SPECIAL_MULT},
                {"MULT rd==0", (OPCODE_SPECIAL << 26) | (4u << 21) | (5u << 16) | (0u << 11) | SPECIAL_MULT},
                {"DIV rd!=0", (OPCODE_SPECIAL << 26) | (6u << 21) | (7u << 16) | (9u << 11) | SPECIAL_DIV},
                {"MMI MULT1 rd!=0", (OPCODE_MMI << 26) | (8u << 21) | (9u << 16) | (10u << 11) | MMI_MULT1},
                {"MMI DIV1 rd!=0", (OPCODE_MMI << 26) | (8u << 21) | (9u << 16) | (10u << 11) | MMI_DIV1},
            };

            for (size_t i = 0; i < std::size(cases); ++i)
            {
                const Instruction inst = decoder.decodeInstruction(0x1000u + static_cast<uint32_t>(i * 4u), cases[i].raw);
                const std::string generated = generator.translateInstruction(inst);
                const bool emittedRdWrite = hasSignedRdWrite(generated, inst.rd);

                t.Equals(emittedRdWrite, inst.modificationInfo.modifiesGPR,
                         std::string("decoder/codegen mismatch for ") + cases[i].name);
                t.IsTrue(inst.modificationInfo.modifiesControl,
                         std::string("HI/LO control side-effect missing for ") + cases[i].name);
            }
        });

        tc.Run("guest execution is serialized per runtime", [](TestCase &t)
        {
            PS2Runtime runtime;
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0u);
            gSerializedGuestActive.store(0, std::memory_order_release);
            gSerializedGuestMaxActive.store(0, std::memory_order_release);

            constexpr uint32_t kEntries[] = {
                0x120000u,
                0x130000u,
                0x140000u,
                0x150000u,
            };
            constexpr size_t kEntryCount = sizeof(kEntries) / sizeof(kEntries[0]);
            R5900Context contexts[kEntryCount]{};
            std::vector<std::thread> workers;
            workers.reserve(kEntryCount);

            for (size_t i = 0; i < kEntryCount; ++i)
            {
                runtime.registerFunction(kEntries[i], &testSerializedGuestStep);
                contexts[i].pc = kEntries[i];
            }

            for (size_t i = 0; i < kEntryCount; ++i)
            {
                workers.emplace_back([&, i]()
                {
                    runtime.dispatchLoop(rdram.data(), &contexts[i]);
                });
            }

            for (std::thread &worker : workers)
            {
                if (worker.joinable())
                {
                    worker.join();
                }
            }

            t.Equals(gSerializedGuestActive.load(std::memory_order_acquire), 0,
                     "serialized guest dispatch should leave no active workers");
            t.Equals(gSerializedGuestMaxActive.load(std::memory_order_acquire), 1,
                     "dispatchLoop should not execute guest code concurrently on one runtime");
        });

        tc.Run("wake handoff lets a contending guest thread acquire before returning", [](TestCase &t)
        {
            PS2Runtime runtime;
            std::atomic<bool> peerRan{false};
            std::thread peer;
            bool peerWaiting = false;
            bool peerRanWhileMainHeld = false;
            bool peerRanAfterHandoff = false;

            {
                PS2Runtime::GuestExecutionScope mainScope(&runtime);
                peer = std::thread([&]()
                {
                    PS2Runtime::GuestExecutionScope peerScope(&runtime);
                    peerRan.store(true, std::memory_order_release);
                });

                peerWaiting = waitUntil([&]()
                {
                    return runtime.guestExecutionWaiterCountForTesting() > 0u;
                }, std::chrono::milliseconds(100));

                peerRanWhileMainHeld = peerRan.load(std::memory_order_acquire);
                runtime.yieldGuestExecutionAfterWake();
                peerRanAfterHandoff = peerRan.load(std::memory_order_acquire);
            }

            if (peer.joinable())
            {
                peer.join();
            }

            t.IsTrue(peerWaiting, "peer guest thread should contend while the waker owns guest execution");
            t.IsFalse(peerRanWhileMainHeld, "peer guest thread should not run before the waker yields execution");
            t.IsTrue(peerRanAfterHandoff, "wake handoff should let the peer acquire guest execution before returning");
        });

        tc.Run("guest preemption policy requests a dispatcher handoff when another guest thread contends", [](TestCase &t)
        {
            PS2Runtime runtime;
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0u);
            constexpr uint32_t kFirstEntry = 0x190000u;
            constexpr uint32_t kSecondEntry = 0x1A0000u;

            gPreemptionPolicyEntryCount.store(0, std::memory_order_release);
            gPreemptionPolicyAllowFirstProbe.store(false, std::memory_order_release);
            gPreemptionPolicyPeerRan.store(false, std::memory_order_release);

            runtime.registerFunction(kFirstEntry, &testPreemptionPolicyStep);
            runtime.registerFunction(kSecondEntry, &testPreemptionPolicyStep);

            R5900Context firstCtx{};
            R5900Context secondCtx{};
            firstCtx.pc = kFirstEntry;
            secondCtx.pc = kSecondEntry;

            std::thread firstWorker([&]()
            {
                runtime.dispatchLoop(rdram.data(), &firstCtx);
            });

            const bool firstEntered = waitUntil([&]()
            {
                return gPreemptionPolicyEntryCount.load(std::memory_order_acquire) >= 1;
            }, std::chrono::milliseconds(100));

            std::thread secondWorker([&]()
            {
                runtime.dispatchLoop(rdram.data(), &secondCtx);
            });

            const bool secondContending = waitUntil([&]()
            {
                return runtime.guestExecutionWaiterCountForTesting() > 0u;
            }, std::chrono::milliseconds(100));

            gPreemptionPolicyAllowFirstProbe.store(true, std::memory_order_release);

            if (firstWorker.joinable())
            {
                firstWorker.join();
            }
            if (secondWorker.joinable())
            {
                secondWorker.join();
            }

            t.IsTrue(firstEntered, "first guest worker should enter before probing for preemption");
            t.IsTrue(secondContending, "second guest worker should contend for guest execution before the first returns");
            t.IsTrue(gPreemptionPolicyPeerRan.load(std::memory_order_acquire),
                     "second guest worker should run after the first returns to the dispatcher");
            t.Equals(getRegU32(&firstCtx, 2), 1u,
                     "first guest worker should observe that the runtime requested preemption under contention");
        });

        tc.Run("lookupFunction aliases internal resume PCs to nearest owner", [](TestCase &t)
        {
            PS2Runtime runtime;
            runtime.registerFunction(0x1000u, &testResumeOwnerFallbackHandler);
            runtime.registerFunction(0x1100u, &testResumeNextFunctionHandler);

            R5900Context ctx{};
            ctx.pc = 0x1010u;
            auto fn = runtime.lookupFunction(ctx.pc);
            fn(nullptr, &ctx, &runtime);

            t.Equals(::getRegU32(&ctx, 2), 0x00ABC123u,
                     "internal resume PC should dispatch to its owner function");
        });

        tc.Run("lookupFunction aliases final-function resume PCs inside code regions", [](TestCase &t)
        {
            PS2Runtime runtime;
            runtime.memory().registerCodeRegion(0x2000u, 0x2100u);
            runtime.registerFunction(0x2000u, &testResumeOwnerFallbackHandler);

            R5900Context ctx{};
            ctx.pc = 0x2010u;
            auto fn = runtime.lookupFunction(ctx.pc);
            fn(nullptr, &ctx, &runtime);

            t.Equals(::getRegU32(&ctx, 2), 0x00ABC123u,
                     "last function should own resumable PCs within its code region");
        });

        tc.Run("vblank intc handlers can preempt serialized guest execution", [](TestCase &t)
        {
            notifyRuntimeStop();

            PS2Runtime runtime;
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0u);
            constexpr uint32_t kBusyEntry = 0x160000u;
            constexpr uint32_t kIntcHandlerEntry = 0x170000u;

            runtime.registerFunction(kBusyEntry, &testWaitForAsyncCounter);
            runtime.registerFunction(kIntcHandlerEntry, &testSignalAsyncCounter);

            R5900Context addCtx{};
            setRegU32(addCtx, 4, 2u);
            setRegU32(addCtx, 5, kIntcHandlerEntry);
            setRegU32(addCtx, 6, 0u);
            setRegU32(addCtx, 7, 0u);
            AddIntcHandler(rdram.data(), &addCtx, &runtime);
            t.IsTrue(getRegS32(addCtx, 2) > 0, "AddIntcHandler should register a VBlank handler");

            R5900Context busyCtx{};
            busyCtx.pc = kBusyEntry;
            std::atomic<bool> workerDone{false};
            std::atomic<bool> workerThrew{false};

            std::thread worker([&]()
            {
                try
                {
                    runtime.dispatchLoop(rdram.data(), &busyCtx);
                }
                catch (...)
                {
                    workerThrew.store(true, std::memory_order_release);
                }
                workerDone.store(true, std::memory_order_release);
            });

            ps2_syscalls::EnsureVSyncWorkerRunning(rdram.data(), &runtime);

            const bool finished = waitUntil([&]()
            {
                return workerDone.load(std::memory_order_acquire);
            }, std::chrono::milliseconds(250));

            if (!finished)
            {
                const uint32_t counter = 1u;
                std::memcpy(rdram.data() + kAsyncCounterAddr, &counter, sizeof(counter));
            }

            if (worker.joinable())
            {
                worker.join();
            }

            runtime.requestStop();
            notifyRuntimeStop();

            uint32_t counter = 0u;
            std::memcpy(&counter, rdram.data() + kAsyncCounterAddr, sizeof(counter));

            t.IsFalse(workerThrew.load(std::memory_order_acquire),
                      "busy dispatch worker should not throw while VBlank handlers fire");
            t.IsTrue(finished,
                     "VBlank interrupt handlers should run even while a guest thread is spinning");
            t.Equals(counter, 1u, "VBlank handler should publish the awaited counter value");
        });

        tc.Run("GS async callbacks keep a dedicated stack when guest heap is exhausted", [](TestCase &t)
        {
            notifyRuntimeStop();

            PS2Runtime runtime;
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0u);
            constexpr uint32_t kCallbackEntry = 0x180000u;
            constexpr uint32_t kCallerGp = 0x0036A7F0u;
            constexpr uint32_t kCallerSp = 0x00123450u;
            constexpr uint32_t kAsyncStackFloor = 0x01F00000u;

            runtime.configureGuestHeap(kAsyncStackFloor, kAsyncStackFloor);
            runtime.registerFunction(kCallbackEntry, &testRecordAsyncCallbackStack);
            ps2_stubs::resetGsSyncVCallbackState();
            gAsyncCallbackObservedSp.store(0u, std::memory_order_release);
            gAsyncCallbackObservedGp.store(0u, std::memory_order_release);

            R5900Context registerCtx{};
            registerCtx.pc = 0x00101900u;
            setRegU32(registerCtx, 4, kCallbackEntry);
            setRegU32(registerCtx, 28, kCallerGp);
            setRegU32(registerCtx, 29, kCallerSp);
            ps2_stubs::sceGsSyncVCallback(rdram.data(), &registerCtx, &runtime);

            ps2_stubs::dispatchGsSyncVCallback(rdram.data(), &runtime, 1u);

            const uint32_t observedSp = gAsyncCallbackObservedSp.load(std::memory_order_acquire);
            const uint32_t observedGp = gAsyncCallbackObservedGp.load(std::memory_order_acquire);

            t.IsTrue(observedSp != 0u, "callback should execute");
            t.Equals(observedGp, kCallerGp, "callback should preserve the registered GP");
            t.IsTrue(observedSp != kCallerSp, "callback should not reuse the registering thread stack");
            t.IsTrue(observedSp >= kAsyncStackFloor,
                     "callback should switch to the reserved async stack pool");

            runtime.requestStop();
            notifyRuntimeStop();
        });

        tc.Run("MPEG init and callback stubs return success instead of TODO errors", [](TestCase &t)
        {
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0u);
            ps2_stubs::resetMpegStubState();

            R5900Context initCtx{};
            ps2_stubs::sceMpegInit(rdram.data(), &initCtx, nullptr);
            t.Equals(getRegS32(initCtx, 2), 0,
                     "sceMpegInit should succeed so games can continue through movie setup");

            R5900Context addCtx0{};
            setRegU32(addCtx0, 4, 0x00123000u);
            setRegU32(addCtx0, 5, 1u);
            setRegU32(addCtx0, 6, 0x00124000u);
            setRegU32(addCtx0, 7, 0u);
            ps2_stubs::sceMpegAddCallback(rdram.data(), &addCtx0, nullptr);
            t.Equals(getRegS32(addCtx0, 2), 1,
                     "first sceMpegAddCallback should hand back a non-error callback handle");

            R5900Context addCtx1{};
            setRegU32(addCtx1, 4, 0x00123000u);
            setRegU32(addCtx1, 5, 2u);
            setRegU32(addCtx1, 6, 0x00124010u);
            setRegU32(addCtx1, 7, 0u);
            ps2_stubs::sceMpegAddCallback(rdram.data(), &addCtx1, nullptr);
            t.Equals(getRegS32(addCtx1, 2), 2,
                     "subsequent sceMpegAddCallback calls should keep succeeding");

            R5900Context reinitCtx{};
            ps2_stubs::sceMpegInit(rdram.data(), &reinitCtx, nullptr);

            R5900Context addAfterReinit{};
            setRegU32(addAfterReinit, 4, 0x00123000u);
            setRegU32(addAfterReinit, 5, 3u);
            setRegU32(addAfterReinit, 6, 0x00124020u);
            setRegU32(addAfterReinit, 7, 0u);
            ps2_stubs::sceMpegAddCallback(rdram.data(), &addAfterReinit, nullptr);
            t.Equals(getRegS32(addAfterReinit, 2), 1,
                     "sceMpegInit should reset MPEG callback bookkeeping between runs");
        });

        tc.Run("sceMpegDemuxPssRing dispatches registered video and audio stream callbacks", [](TestCase &t)
        {
            PS2Runtime runtime;
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0u);
            ps2_stubs::resetMpegStubState();

            constexpr uint32_t kMpegAddr = 0x00123000u;
            constexpr uint32_t kCallbackEntry = 0x00124000u;
            constexpr uint32_t kVideoUserData = 0x11223344u;
            constexpr uint32_t kAudioUserData = 0x55667788u;
            constexpr uint32_t kVideoPacketAddr = 0x00128000u;
            constexpr uint32_t kAudioPacketAddr = 0x00129000u;

            runtime.registerFunction(kCallbackEntry, &testRecordMpegStreamCallback);

            auto registerGenericCallback = [&](uint32_t callbackType, uint32_t userData)
            {
                R5900Context addCtx{};
                setRegU32(addCtx, 4, kMpegAddr);
                setRegU32(addCtx, 5, callbackType);
                setRegU32(addCtx, 6, kCallbackEntry);
                setRegU32(addCtx, 7, userData);
                ps2_stubs::sceMpegAddCallback(rdram.data(), &addCtx, &runtime);
            };

            auto registerStreamCallback = [&](uint32_t streamType, uint32_t userData)
            {
                R5900Context addCtx{};
                setRegU32(addCtx, 4, kMpegAddr);
                setRegU32(addCtx, 5, streamType);
                setRegU32(addCtx, 6, 0u);
                setRegU32(addCtx, 7, kCallbackEntry);
                setRegU32(addCtx, 8, userData);
                ps2_stubs::sceMpegAddStrCallback(rdram.data(), &addCtx, &runtime);
            };

            auto writePesPacket = [&](uint32_t addr, uint8_t streamId, const std::vector<uint8_t> &payload)
            {
                const uint16_t packetLen = static_cast<uint16_t>(payload.size() + 3u);
                std::vector<uint8_t> packet = {
                    0x00u, 0x00u, 0x01u, streamId,
                    static_cast<uint8_t>(packetLen >> 8u),
                    static_cast<uint8_t>(packetLen & 0xFFu),
                    0x80u, 0x00u, 0x00u};
                packet.insert(packet.end(), payload.begin(), payload.end());
                std::memcpy(rdram.data() + addr, packet.data(), packet.size());
                return static_cast<uint32_t>(packet.size());
            };

            registerGenericCallback(0u, 0xDEAD0000u);
            registerGenericCallback(2u, 0xDEAD0002u);
            registerStreamCallback(0u, kVideoUserData);
            registerStreamCallback(2u, kAudioUserData);

            const std::vector<uint8_t> videoPayload = {
                0x00u, 0x00u, 0x01u, 0xB3u, 0x14u, 0x00u, 0xF0u, 0x13u};
            const uint32_t videoPacketSize = writePesPacket(kVideoPacketAddr, 0xE0u, videoPayload);

            gMpegStreamCallbackCount.store(0u, std::memory_order_release);
            R5900Context videoDemuxCtx{};
            setRegU32(videoDemuxCtx, 4, kMpegAddr);
            setRegU32(videoDemuxCtx, 5, kVideoPacketAddr);
            setRegU32(videoDemuxCtx, 6, videoPacketSize);
            setRegU32(videoDemuxCtx, 7, kVideoPacketAddr);
            setRegU32(videoDemuxCtx, 8, videoPacketSize);
            ps2_stubs::sceMpegDemuxPssRing(rdram.data(), &videoDemuxCtx, &runtime);

            t.Equals(getRegS32(videoDemuxCtx, 2), static_cast<int32_t>(videoPacketSize),
                     "sceMpegDemuxPssRing should consume the video PES packet");
            t.Equals(gMpegStreamCallbackCount.load(std::memory_order_acquire), 1u,
                     "registered video stream callback should be invoked");
            t.Equals(gMpegStreamCallbackMpeg.load(std::memory_order_acquire), kMpegAddr,
                     "video callback should receive the MPEG handle");
            t.Equals(gMpegStreamCallbackType.load(std::memory_order_acquire), 0u,
                     "video callback data should report M2V stream type");
            t.Equals(gMpegStreamCallbackDataAddr.load(std::memory_order_acquire), kVideoPacketAddr + 9u,
                     "video callback data should point at PES payload");
            t.Equals(gMpegStreamCallbackLen.load(std::memory_order_acquire),
                     static_cast<uint32_t>(videoPayload.size()),
                     "video callback data should report PES payload length");
            t.Equals(gMpegStreamCallbackUserData.load(std::memory_order_acquire), kVideoUserData,
                     "video callback should receive registered user data");

            const std::vector<uint8_t> audioPayload = {0x80u, 0x01u, 0x02u, 0x03u, 0x04u, 0x05u};
            const uint32_t audioPacketSize = writePesPacket(kAudioPacketAddr, 0xBDu, audioPayload);

            gMpegStreamCallbackCount.store(0u, std::memory_order_release);
            R5900Context audioDemuxCtx{};
            setRegU32(audioDemuxCtx, 4, kMpegAddr);
            setRegU32(audioDemuxCtx, 5, kAudioPacketAddr);
            setRegU32(audioDemuxCtx, 6, audioPacketSize);
            setRegU32(audioDemuxCtx, 7, kAudioPacketAddr);
            setRegU32(audioDemuxCtx, 8, audioPacketSize);
            ps2_stubs::sceMpegDemuxPssRing(rdram.data(), &audioDemuxCtx, &runtime);

            t.Equals(getRegS32(audioDemuxCtx, 2), static_cast<int32_t>(audioPacketSize),
                     "sceMpegDemuxPssRing should consume the audio PES packet");
            t.Equals(gMpegStreamCallbackCount.load(std::memory_order_acquire), 1u,
                     "registered audio stream callback should be invoked");
            t.Equals(gMpegStreamCallbackType.load(std::memory_order_acquire), 2u,
                     "audio callback data should report PCM stream type");
            t.Equals(gMpegStreamCallbackDataAddr.load(std::memory_order_acquire), kAudioPacketAddr + 9u,
                     "audio callback data should point at PES payload");
            t.Equals(gMpegStreamCallbackLen.load(std::memory_order_acquire),
                     static_cast<uint32_t>(audioPayload.size()),
                     "audio callback data should report PES payload length");
            t.Equals(gMpegStreamCallbackUserData.load(std::memory_order_acquire), kAudioUserData,
                     "audio callback should receive registered user data");

            ps2_stubs::notifyMpegCdStreamEof();

            gMpegStreamCallbackCount.store(0u, std::memory_order_release);
            R5900Context afterEofDemuxCtx{};
            setRegU32(afterEofDemuxCtx, 4, kMpegAddr);
            setRegU32(afterEofDemuxCtx, 5, kVideoPacketAddr);
            setRegU32(afterEofDemuxCtx, 6, videoPacketSize);
            setRegU32(afterEofDemuxCtx, 7, kVideoPacketAddr);
            setRegU32(afterEofDemuxCtx, 8, videoPacketSize);
            ps2_stubs::sceMpegDemuxPssRing(rdram.data(), &afterEofDemuxCtx, &runtime);

            t.Equals(getRegS32(afterEofDemuxCtx, 2), static_cast<int32_t>(videoPacketSize),
                     "post-EOF demux should continue consuming caller data");
            t.Equals(gMpegStreamCallbackCount.load(std::memory_order_acquire), 0u,
                     "post-EOF demux should not feed callbacks again");

            R5900Context resetCtx{};
            setRegU32(resetCtx, 4, kMpegAddr);
            ps2_stubs::sceMpegReset(rdram.data(), &resetCtx, &runtime);

            gMpegStreamCallbackCount.store(0u, std::memory_order_release);
            R5900Context afterResetDemuxCtx{};
            setRegU32(afterResetDemuxCtx, 4, kMpegAddr);
            setRegU32(afterResetDemuxCtx, 5, kVideoPacketAddr);
            setRegU32(afterResetDemuxCtx, 6, videoPacketSize);
            setRegU32(afterResetDemuxCtx, 7, kVideoPacketAddr);
            setRegU32(afterResetDemuxCtx, 8, videoPacketSize);
            ps2_stubs::sceMpegDemuxPssRing(rdram.data(), &afterResetDemuxCtx, &runtime);

            t.Equals(getRegS32(afterResetDemuxCtx, 2), static_cast<int32_t>(videoPacketSize),
                     "post-EOF reset demux should still drain caller data");
            t.Equals(gMpegStreamCallbackCount.load(std::memory_order_acquire), 0u,
                     "post-EOF reset demux should not restart callbacks on stale data");

            ps2_stubs::notifyMpegCdStreamStart();

            gMpegStreamCallbackCount.store(0u, std::memory_order_release);
            R5900Context afterNewStreamDemuxCtx{};
            setRegU32(afterNewStreamDemuxCtx, 4, kMpegAddr);
            setRegU32(afterNewStreamDemuxCtx, 5, kVideoPacketAddr);
            setRegU32(afterNewStreamDemuxCtx, 6, videoPacketSize);
            setRegU32(afterNewStreamDemuxCtx, 7, kVideoPacketAddr);
            setRegU32(afterNewStreamDemuxCtx, 8, videoPacketSize);
            ps2_stubs::sceMpegDemuxPssRing(rdram.data(), &afterNewStreamDemuxCtx, &runtime);

            t.Equals(getRegS32(afterNewStreamDemuxCtx, 2), static_cast<int32_t>(videoPacketSize),
                     "new CD stream demux should reopen an ended MPEG handle");
            t.Equals(gMpegStreamCallbackCount.load(std::memory_order_acquire), 1u,
                     "new CD stream demux should allow callbacks on a reused MPEG handle");

            constexpr uint32_t kMpegWorkAddr = 0x00130000u;
            R5900Context createCtx{};
            setRegU32(createCtx, 4, kMpegAddr);
            setRegU32(createCtx, 5, kMpegWorkAddr);
            setRegU32(createCtx, 6, 0x2000u);
            ps2_stubs::sceMpegCreate(rdram.data(), &createCtx, &runtime);
            t.IsTrue(::getRegU32(&createCtx, 2) != 0u,
                     "sceMpegCreate should reopen the MPEG handle after an ended reset");

            gMpegStreamCallbackCount.store(0u, std::memory_order_release);
            R5900Context afterCreateDemuxCtx{};
            setRegU32(afterCreateDemuxCtx, 4, kMpegAddr);
            setRegU32(afterCreateDemuxCtx, 5, kVideoPacketAddr);
            setRegU32(afterCreateDemuxCtx, 6, videoPacketSize);
            setRegU32(afterCreateDemuxCtx, 7, kVideoPacketAddr);
            setRegU32(afterCreateDemuxCtx, 8, videoPacketSize);
            ps2_stubs::sceMpegDemuxPssRing(rdram.data(), &afterCreateDemuxCtx, &runtime);

            t.Equals(gMpegStreamCallbackCount.load(std::memory_order_acquire), 1u,
                     "new MPEG create should allow callbacks for the next stream");

            runtime.requestStop();
        });

        tc.Run("MPEG playback stays active during a temporary demux pause before CD EOF", [](TestCase &t)
        {
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0u);
            ps2_stubs::resetMpegStubState();

            constexpr uint32_t kMpegAddr = 0x00123000u;
            constexpr uint32_t kPacketAddr = 0x00128000u;
            constexpr uint32_t kImageAddr = 0x00130000u;
            const std::vector<uint8_t> es = {
                0x00u, 0x00u, 0x01u, 0xB3u, 0x01u, 0x00u, 0x10u, 0x12u, 0xFFu, 0xFFu, 0xE0u, 0x18u,
                0x00u, 0x00u, 0x01u, 0xB5u, 0x14u, 0x8Au, 0x00u, 0x01u, 0x00u, 0x17u, 0x00u, 0x00u,
                0x01u, 0xB8u, 0x00u, 0x08u, 0x00u, 0x40u, 0x00u, 0x00u, 0x01u, 0x00u, 0x00u, 0x0Fu,
                0xFFu, 0xF8u, 0x00u, 0x00u, 0x01u, 0xB5u, 0x8Fu, 0xFFu, 0xF3u, 0x41u, 0x80u, 0x00u,
                0x00u, 0x01u, 0x01u, 0x13u, 0xF8u, 0x7Du, 0x29u, 0x48u, 0x88u, 0x00u, 0x00u, 0x01u,
                0xB3u, 0x01u, 0x00u, 0x10u, 0x12u, 0xFFu, 0xFFu, 0xE0u, 0x18u, 0x00u, 0x00u, 0x01u,
                0xB5u, 0x14u, 0x8Au, 0x00u, 0x01u, 0x00u, 0x17u, 0x00u, 0x00u, 0x01u, 0xB8u, 0x00u,
                0x08u, 0x00u, 0xC0u, 0x00u, 0x00u, 0x01u, 0x00u, 0x00u, 0x0Fu, 0xFFu, 0xF8u, 0x00u,
                0x00u, 0x01u, 0xB5u, 0x8Fu, 0xFFu, 0xF3u, 0x41u, 0x80u, 0x00u, 0x00u, 0x01u, 0x01u,
                0x13u, 0xF8u, 0x7Du, 0x29u, 0x48u, 0x88u, 0x00u, 0x00u, 0x01u, 0xB3u, 0x01u, 0x00u,
                0x10u, 0x12u, 0xFFu, 0xFFu, 0xE0u, 0x18u, 0x00u, 0x00u, 0x01u, 0xB5u, 0x14u, 0x8Au,
                0x00u, 0x01u, 0x00u, 0x17u, 0x00u, 0x00u, 0x01u, 0xB8u, 0x00u, 0x08u, 0x01u, 0x40u,
                0x00u, 0x00u, 0x01u, 0x00u, 0x00u, 0x0Fu, 0xFFu, 0xF8u, 0x00u, 0x00u, 0x01u, 0xB5u,
                0x8Fu, 0xFFu, 0xF3u, 0x41u, 0x80u, 0x00u, 0x00u, 0x01u, 0x01u, 0x13u, 0xF8u, 0x7Du,
                0x29u, 0x48u, 0x88u};

            std::vector<uint8_t> packet = {
                0x00u, 0x00u, 0x01u, 0xE0u,
                0x00u, static_cast<uint8_t>(es.size() + 3u),
                0x80u, 0x00u, 0x00u};
            packet.insert(packet.end(), es.begin(), es.end());
            std::memcpy(rdram.data() + kPacketAddr, packet.data(), packet.size());

            R5900Context demuxCtx{};
            setRegU32(demuxCtx, 4, kMpegAddr);
            setRegU32(demuxCtx, 5, kPacketAddr);
            setRegU32(demuxCtx, 6, static_cast<uint32_t>(packet.size()));
            setRegU32(demuxCtx, 7, kPacketAddr);
            setRegU32(demuxCtx, 8, static_cast<uint32_t>(packet.size()));
            ps2_stubs::sceMpegDemuxPssRing(rdram.data(), &demuxCtx, nullptr);

            R5900Context pictureCtx{};
            setRegU32(pictureCtx, 4, kMpegAddr);
            setRegU32(pictureCtx, 5, kImageAddr);
            ps2_stubs::sceMpegGetPicture(rdram.data(), &pictureCtx, nullptr);
            t.Equals(Ps2FastRead32(rdram.data(), kMpegAddr + 0x00u), 16u,
                     "test stream should decode one frame before the pause");
            t.Equals(Ps2FastRead32(rdram.data(), kMpegAddr + 0x08u), 0u,
                     "first decoded frame should report frame index zero");

            std::this_thread::sleep_for(std::chrono::milliseconds(650));

            R5900Context isEndCtx{};
            setRegU32(isEndCtx, 4, kMpegAddr);
            ps2_stubs::sceMpegIsEnd(rdram.data(), &isEndCtx, nullptr);
            t.Equals(getRegS32(isEndCtx, 2), 0,
                     "a temporary demux pause must not end an active stream before CD EOF");

            ps2_stubs::sceMpegGetPicture(rdram.data(), &pictureCtx, nullptr);
            t.Equals(Ps2FastRead32(rdram.data(), kMpegAddr + 0x08u), 1u,
                     "temporary non-EOF starvation should keep movie frame progress moving");

            ps2_stubs::sceMpegGetPicture(rdram.data(), &pictureCtx, nullptr);
            t.Equals(Ps2FastRead32(rdram.data(), kMpegAddr + 0x08u), 2u,
                     "repeated temporary starvation should continue advancing from the held frame");
        });

        tc.Run("sceMpegGetPicture releases an old waiter when the CD stream restarts", [](TestCase &t)
        {
            PS2Runtime runtime;
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0u);
            ps2_stubs::resetMpegStubState();
            ps2_stubs::notifyMpegCdStreamStart();

            constexpr uint32_t kMpegAddr = 0x00123000u;
            constexpr uint32_t kImageAddr = 0x00130000u;
            R5900Context pictureCtx{};
            setRegU32(pictureCtx, 4, kMpegAddr);
            setRegU32(pictureCtx, 5, kImageAddr);

            std::atomic<bool> returned{false};
            std::thread waiter([&]()
            {
                ps2_stubs::sceMpegGetPicture(rdram.data(), &pictureCtx, &runtime);
                returned.store(true, std::memory_order_release);
            });

            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            t.IsFalse(returned.load(std::memory_order_acquire),
                      "sceMpegGetPicture should wait while the current stream still has no frame");

            ps2_stubs::notifyMpegCdStreamStart();
            const bool released = waitUntil(
                [&]() { return returned.load(std::memory_order_acquire); },
                std::chrono::milliseconds(30));

            runtime.requestStop();
            waiter.join();
            t.IsTrue(released,
                     "a new sceCdStStart generation should release a waiter owned by the previous movie");
        });

        tc.Run("sceMpegGetPicture yields during active stream starvation before CD EOF", [](TestCase &t)
        {
            PS2Runtime runtime;
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0u);
            ps2_stubs::resetMpegStubState();
            ps2_stubs::notifyMpegCdStreamStart();

            constexpr uint32_t kMpegAddr = 0x00123000u;
            constexpr uint32_t kPssAddr = 0x0012C000u;
            constexpr uint32_t kImageAddr = 0x00130000u;
            const uint8_t incompletePssStart[] = {0x00u, 0x00u, 0x01u};
            std::memcpy(rdram.data() + kPssAddr, incompletePssStart, sizeof(incompletePssStart));

            R5900Context demuxCtx{};
            setRegU32(demuxCtx, 4, kMpegAddr);
            setRegU32(demuxCtx, 5, kPssAddr);
            setRegU32(demuxCtx, 6, sizeof(incompletePssStart));
            setRegU32(demuxCtx, 7, kPssAddr);
            setRegU32(demuxCtx, 8, sizeof(incompletePssStart));
            ps2_stubs::sceMpegDemuxPssRing(rdram.data(), &demuxCtx, &runtime);

            R5900Context pictureCtx{};
            setRegU32(pictureCtx, 4, kMpegAddr);
            setRegU32(pictureCtx, 5, kImageAddr);

            std::atomic<bool> returned{false};
            std::thread waiter([&]()
            {
                ps2_stubs::sceMpegGetPicture(rdram.data(), &pictureCtx, &runtime);
                returned.store(true, std::memory_order_release);
            });

            const bool yielded = waitUntil(
                [&]() { return returned.load(std::memory_order_acquire); },
                std::chrono::milliseconds(200));

            runtime.requestStop();
            waiter.join();
            t.IsTrue(yielded,
                     "active non-EOF streams must return control when no decoded frame is currently available");

            R5900Context isEndCtx{};
            setRegU32(isEndCtx, 4, kMpegAddr);
            ps2_stubs::sceMpegIsEnd(rdram.data(), &isEndCtx, nullptr);
            t.Equals(getRegS32(isEndCtx, 2), 0,
                     "yielding without a frame must not mark the active stream ended");
        });

        tc.Run("movie startup MPEG and audio stubs return safe progress values", [](TestCase &t)
        {
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0u);
            ps2_stubs::resetMpegStubState();
            ps2_stubs::resetAudioStubState();

            R5900Context firstIsEndCtx{};
            setRegU32(firstIsEndCtx, 4, 0x00123000u);
            ps2_stubs::sceMpegIsEnd(rdram.data(), &firstIsEndCtx, nullptr);
            t.Equals(getRegS32(firstIsEndCtx, 2), 0,
                     "sceMpegIsEnd should allow one synthetic frame before reporting end");

            R5900Context demuxCtx{};
            setRegU32(demuxCtx, 4, 0x00123000u);
            setRegU32(demuxCtx, 5, 0x00400000u);
            setRegU32(demuxCtx, 6, 0x00004000u);
            setRegU32(demuxCtx, 7, 0x00410000u);
            ps2_stubs::sceMpegDemuxPssRing(rdram.data(), &demuxCtx, nullptr);
            t.Equals(getRegS32(demuxCtx, 2), 0x4000,
                     "sceMpegDemuxPssRing should consume the provided input instead of trapping");

            R5900Context getPictureCtx{};
            setRegU32(getPictureCtx, 4, 0x00123000u);
            setRegU32(getPictureCtx, 5, 0x00124000u);
            setRegU32(getPictureCtx, 6, 440u);
            ps2_stubs::sceMpegGetPicture(rdram.data(), &getPictureCtx, nullptr);
            t.Equals(Ps2FastRead32(rdram.data(), 0x00123000u + 0x00u), 320u,
                     "sceMpegGetPicture should seed a safe movie width");
            t.Equals(Ps2FastRead32(rdram.data(), 0x00123000u + 0x04u), 240u,
                     "sceMpegGetPicture should seed a safe movie height");
            t.Equals(Ps2FastRead32(rdram.data(), 0x00123000u + 0x08u), 0u,
                     "first synthetic picture should preserve frameCount==0 for guest setup");

            R5900Context secondIsEndCtx{};
            setRegU32(secondIsEndCtx, 4, 0x00123000u);
            ps2_stubs::sceMpegIsEnd(rdram.data(), &secondIsEndCtx, nullptr);
            t.Equals(getRegS32(secondIsEndCtx, 2), 0,
                     "sceMpegIsEnd should keep the decode thread alive and let the guest stop playback");

            constexpr uint32_t pssEndAddr = 0x00128000u;
            constexpr uint32_t stackAddr = 0x00129000u;
            const uint8_t programEnd[] = {0x00u, 0x00u, 0x01u, 0xB9u};
            std::memcpy(rdram.data() + pssEndAddr, programEnd, sizeof(programEnd));
            std::memcpy(rdram.data() + stackAddr + 0x10u, "\x04\x00\x00\x00", 4u);

            R5900Context endDemuxCtx{};
            setRegU32(endDemuxCtx, 29, stackAddr);
            setRegU32(endDemuxCtx, 4, 0x00123000u);
            setRegU32(endDemuxCtx, 5, pssEndAddr);
            setRegU32(endDemuxCtx, 6, sizeof(programEnd));
            setRegU32(endDemuxCtx, 7, pssEndAddr);
            ps2_stubs::sceMpegDemuxPssRing(rdram.data(), &endDemuxCtx, nullptr);

            R5900Context endIsEndCtx{};
            setRegU32(endIsEndCtx, 4, 0x00123000u);
            ps2_stubs::sceMpegIsEnd(rdram.data(), &endIsEndCtx, nullptr);
            t.Equals(getRegS32(endIsEndCtx, 2), 1,
                     "sceMpegIsEnd should report end after a demuxed MPEG program end code");

            ps2_stubs::resetMpegStubState();
            constexpr uint32_t wrappedEndBase = 0x0012A000u;
            rdram[wrappedEndBase + 0u] = 0x00u;
            rdram[wrappedEndBase + 1u] = 0x01u;
            rdram[wrappedEndBase + 2u] = 0xB9u;
            rdram[wrappedEndBase + 3u] = 0x00u;

            R5900Context wrappedEndDemuxCtx{};
            setRegU32(wrappedEndDemuxCtx, 4, 0x00123000u);
            setRegU32(wrappedEndDemuxCtx, 5, wrappedEndBase + 3u);
            setRegU32(wrappedEndDemuxCtx, 6, 4u);
            setRegU32(wrappedEndDemuxCtx, 7, wrappedEndBase);
            setRegU32(wrappedEndDemuxCtx, 8, 4u);
            ps2_stubs::sceMpegDemuxPssRing(rdram.data(), &wrappedEndDemuxCtx, nullptr);

            R5900Context wrappedEndIsEndCtx{};
            setRegU32(wrappedEndIsEndCtx, 4, 0x00123000u);
            ps2_stubs::sceMpegIsEnd(rdram.data(), &wrappedEndIsEndCtx, nullptr);
            t.Equals(getRegS32(wrappedEndIsEndCtx, 2), 1,
                     "sceMpegDemuxPssRing should use the ABI fifth argument in t0 for wrapped rings");

            ps2_stubs::resetMpegStubState();
            constexpr uint32_t eofMpegAddr = 0x0012B000u;
            constexpr uint32_t eofPssAddr = 0x0012C000u;
            const uint8_t incompletePssStart[] = {0x00u, 0x00u, 0x01u};
            std::memcpy(rdram.data() + eofPssAddr, incompletePssStart, sizeof(incompletePssStart));

            R5900Context eofDemuxCtx{};
            setRegU32(eofDemuxCtx, 4, eofMpegAddr);
            setRegU32(eofDemuxCtx, 5, eofPssAddr);
            setRegU32(eofDemuxCtx, 6, sizeof(incompletePssStart));
            setRegU32(eofDemuxCtx, 7, eofPssAddr);
            setRegU32(eofDemuxCtx, 8, sizeof(incompletePssStart));
            ps2_stubs::sceMpegDemuxPssRing(rdram.data(), &eofDemuxCtx, nullptr);
            t.Equals(getRegS32(eofDemuxCtx, 2), static_cast<int32_t>(sizeof(incompletePssStart)),
                     "sceMpegDemuxPssRing should accept partial trailing stream data");

            R5900Context eofBeforeStopCtx{};
            setRegU32(eofBeforeStopCtx, 4, eofMpegAddr);
            ps2_stubs::sceMpegIsEnd(rdram.data(), &eofBeforeStopCtx, nullptr);
            t.Equals(getRegS32(eofBeforeStopCtx, 2), 0,
                     "sceMpegIsEnd should not report end until the CD stream terminates");

            R5900Context cdStopCtx{};
            ps2_stubs::sceCdStStop(rdram.data(), &cdStopCtx, nullptr);

            R5900Context eofAfterStopCtx{};
            setRegU32(eofAfterStopCtx, 4, eofMpegAddr);
            ps2_stubs::sceMpegIsEnd(rdram.data(), &eofAfterStopCtx, nullptr);
            t.Equals(getRegS32(eofAfterStopCtx, 2), 1,
                     "sceCdStStop should finalize active MPEG playback so movie threads can advance");

            R5900Context remoteInitCtx{};
            ps2_stubs::sceSdRemoteInit(rdram.data(), &remoteInitCtx, nullptr);
            t.Equals(getRegS32(remoteInitCtx, 2), 0,
                     "sceSdRemoteInit should succeed so Veronica can set up movie audio");

            R5900Context blockTransCtx{};
            const uint32_t blockTransSp = 0x00100000u;
            setRegU32(blockTransCtx, 29, blockTransSp);
            setRegU32(blockTransCtx, 4, 1u);
            setRegU32(blockTransCtx, 5, 0x80E0u);
            setRegU32(blockTransCtx, 6, 1u);
            setRegU32(blockTransCtx, 7, 2u);
            std::memcpy(rdram.data() + blockTransSp + 0x10u, "\x40\x23\x01\x00", 4u);
            std::memcpy(rdram.data() + blockTransSp + 0x14u, "\x00\x30\x00\x00", 4u);
            std::memcpy(rdram.data() + blockTransSp + 0x18u, "\x40\x27\x01\x00", 4u);
            ps2_stubs::sceSdRemote(rdram.data(), &blockTransCtx, nullptr);
            t.Equals(getRegU32(&blockTransCtx, 2), 0x00012740u,
                     "sceSdRemote block transfer should resume from the configured IOP pause position");

            R5900Context statusCtx{};
            setRegU32(statusCtx, 29, blockTransSp);
            setRegU32(statusCtx, 4, 1u);
            setRegU32(statusCtx, 5, 0x80F0u);
            setRegU32(statusCtx, 6, 1u);
            setRegU32(statusCtx, 7, 0u);
            std::memset(rdram.data() + blockTransSp + 0x10u, 0, 12u);
            ps2_stubs::sceSdRemote(rdram.data(), &statusCtx, nullptr);
            t.Equals(getRegU32(&statusCtx, 2), 0x00012B40u,
                     "sceSdRemote status polling should advance the emulated SPU transfer head");

            for (uint32_t i = 0u; i < 11u; ++i)
            {
                ps2_stubs::sceSdRemote(rdram.data(), &statusCtx, nullptr);
            }
            t.Equals(getRegU32(&statusCtx, 2), 0x00012740u,
                     "sceSdRemote status polling should wrap inside the configured IOP ring");

            R5900Context setParamCtx{};
            setRegU32(setParamCtx, 29, blockTransSp);
            setRegU32(setParamCtx, 4, 1u);
            setRegU32(setParamCtx, 5, 0x8010u);
            setRegU32(setParamCtx, 6, 0x0F81u);
            setRegU32(setParamCtx, 7, 0u);
            ps2_stubs::sceSdRemote(rdram.data(), &setParamCtx, nullptr);
            t.Equals(getRegU32(&setParamCtx, 2), 0x00012740u,
                     "sceSdRemote set-param calls should not trap or disturb the movie audio state");
        });
         
        tc.Run("IPU init skips missing optional helper instead of dispatching the default trap", [](TestCase &t)
        {
            PS2Runtime runtime;
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0u);
            R5900Context ctx{};
            ctx.pc = 0x0010B470u;

            ps2_stubs::sceIpuInit(rdram.data(), &ctx, &runtime);

            t.IsFalse(runtime.isStopRequested(),
                      "sceIpuInit should tolerate the missing optional SetD4 helper");
            t.Equals(runtime.memory().read32(0x10002010u), 0x40000000u,
                     "sceIpuInit should still program IPU_CTRL");
            t.Equals(runtime.memory().read32(0x10002000u), 0u,
                     "sceIpuInit should leave IPU_CMD reset after initialization");
        });

        tc.Run("sprintf consumes EE varargs from a2 a3 t0 and preserves width formatting", [](TestCase &t)
        {
            PS2Runtime runtime;
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0u);
            R5900Context ctx{};

            constexpr uint32_t kDestAddr = 0x00002000u;
            constexpr uint32_t kFormatAddr = 0x00002100u;
            constexpr char kFormat[] = "rm_%1d%02d%1d.rdx";

            std::memcpy(rdram.data() + kFormatAddr, kFormat, sizeof(kFormat));
            setRegU32(ctx, 4, kDestAddr);
            setRegU32(ctx, 5, kFormatAddr);
            setRegU32(ctx, 6, 0u); // a2
            setRegU32(ctx, 7, 3u); // a3
            setRegU32(ctx, 8, 1u); // t0

            ps2_stubs::sprintf(rdram.data(), &ctx, &runtime);

            const std::string rendered(reinterpret_cast<const char *>(rdram.data() + kDestAddr));
            t.Equals(rendered, std::string("rm_0031.rdx"),
                     "sprintf should read the third variadic integer from t0 and honor %02d");
            t.Equals(getRegS32(ctx, 2), static_cast<int32_t>(rendered.size()),
                     "sprintf should return the rendered length");
        });

        tc.Run("multiply-add matrix writes rd only when R5900 requires it", [](TestCase &t)
        {
            R5900Decoder decoder;
            CodeGenerator generator({}, {});

            const struct
            {
                const char *name;
                uint32_t raw;
                bool expectedRdWrite;
            } cases[] = {
                {"MULTU rd!=0", (OPCODE_SPECIAL << 26) | (2u << 21) | (3u << 16) | (11u << 11) | SPECIAL_MULTU, true},
                {"MMI MADD rd!=0", (OPCODE_MMI << 26) | (2u << 21) | (3u << 16) | (12u << 11) | MMI_MADD, true},
                {"MMI MADDU rd!=0", (OPCODE_MMI << 26) | (2u << 21) | (3u << 16) | (13u << 11) | MMI_MADDU, true},
                {"MMI MADD1 rd!=0", (OPCODE_MMI << 26) | (2u << 21) | (3u << 16) | (14u << 11) | MMI_MADD1, true},
                {"MMI MADDU1 rd!=0", (OPCODE_MMI << 26) | (2u << 21) | (3u << 16) | (15u << 11) | MMI_MADDU1, true},
                {"MMI DIVU1 rd!=0", (OPCODE_MMI << 26) | (2u << 21) | (3u << 16) | (16u << 11) | MMI_DIVU1, false},
            };

            for (size_t i = 0; i < std::size(cases); ++i)
            {
                const Instruction inst = decoder.decodeInstruction(0x2000u + static_cast<uint32_t>(i * 4u), cases[i].raw);
                const std::string generated = generator.translateInstruction(inst);
                const bool emittedRdWrite = hasSignedRdWrite(generated, inst.rd);

                t.Equals(inst.modificationInfo.modifiesGPR, cases[i].expectedRdWrite,
                         std::string("decoder rd-write metadata mismatch for ") + cases[i].name);
                t.Equals(emittedRdWrite, cases[i].expectedRdWrite,
                         std::string("codegen rd-write mismatch for ") + cases[i].name);
            }
        });

        tc.Run("SignalException marks EPC and BD for delay-slot exceptions", [](TestCase &t)
        {
            PS2Runtime runtime;
            R5900Context ctx{};

            ctx.pc = 0x2000u;
            ctx.branch_pc = 0x1FFCu;
            ctx.in_delay_slot = true;
            ctx.cop0_status = 0u;
            ctx.cop0_cause = 0u;

            runtime.SignalException(&ctx, EXCEPTION_ADDRESS_ERROR_LOAD);

            t.Equals(ctx.cop0_epc, 0x1FFCu, "delay-slot exception should capture branch_pc in EPC");
            t.IsTrue((ctx.cop0_cause & COP0_CAUSE_BD) != 0u, "delay-slot exception should set CAUSE.BD");
            t.Equals(ctx.cop0_cause & COP0_CAUSE_EXCCODE_MASK,
                     (static_cast<uint32_t>(EXCEPTION_ADDRESS_ERROR_LOAD) << 2) & COP0_CAUSE_EXCCODE_MASK,
                     "CAUSE.EXCCODE should match exception");
            t.IsTrue((ctx.cop0_status & COP0_STATUS_EXL) != 0u, "exception should set STATUS.EXL");
            t.Equals(ctx.pc, EXCEPTION_VECTOR_GENERAL, "exception should jump to general vector when BEV=0");
            t.IsFalse(ctx.in_delay_slot, "exception delivery should clear delay-slot state");
        });

        tc.Run("SignalException uses current pc without BD and honors BEV vector", [](TestCase &t)
        {
            PS2Runtime runtime;
            R5900Context ctx{};

            ctx.pc = 0x3000u;
            ctx.in_delay_slot = false;
            ctx.cop0_status = COP0_STATUS_BEV;
            ctx.cop0_cause = COP0_CAUSE_BD;

            runtime.SignalException(&ctx, EXCEPTION_ADDRESS_ERROR_STORE);

            t.Equals(ctx.cop0_epc, 0x3000u, "non-delay exception should capture current pc in EPC");
            t.IsTrue((ctx.cop0_cause & COP0_CAUSE_BD) == 0u, "non-delay exception should clear CAUSE.BD");
            t.Equals(ctx.pc, EXCEPTION_VECTOR_BOOT, "BEV=1 should route exception to boot vector");
        });

        tc.Run("handleSyscall rejects invocation in delay slot", [](TestCase &t)
        {
            PS2Runtime runtime;
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0u);
            R5900Context ctx{};
            ctx.in_delay_slot = true;

            bool threw = false;
            try
            {
                runtime.handleSyscall(rdram.data(), &ctx, 0x3Cu);
            }
            catch (const std::runtime_error &)
            {
                threw = true;
            }

            t.IsTrue(threw, "syscall from delay slot should throw to preserve block atomicity");
        });

        tc.Run("VIF MSCAL and MSCNT toggle DBF and keep TOPS/ITOPS coherent", [](TestCase &t)
        {
            PS2Memory mem;
            t.IsTrue(mem.initialize(), "PS2Memory initialize should succeed");

            mem.vif1_regs.base = 4u;
            mem.vif1_regs.ofst = 2u;
            mem.vif1_regs.tops = 4u;
            mem.vif1_regs.itops = 0x21u;
            mem.vif1_regs.stat &= ~(1u << 7); // DBF = 0

            uint32_t callbackPc = 0xFFFFFFFFu;
            uint32_t callbackTop = 0xFFFFFFFFu;
            uint32_t callbackItop = 0xFFFFFFFFu;
            uint32_t callbackCount = 0u;
            mem.setVu1MscalCallback([&](uint32_t startPC, uint32_t top, uint32_t itop)
            {
                callbackPc = startPC;
                callbackTop = top;
                callbackItop = itop;
                ++callbackCount;
            });

            const uint32_t mscal = makeVifCmd(0x14u, 0u, 3u); // start PC = 3 * 8
            mem.processVIF1Data(reinterpret_cast<const uint8_t *>(&mscal), sizeof(mscal));

            t.Equals(callbackCount, 1u, "MSCAL should invoke VU1 callback exactly once");
            t.Equals(callbackPc, 24u, "MSCAL should pass startPC=imm*8");
            t.Equals(callbackTop, 4u, "MSCAL callback should receive current TOPS");
            t.Equals(callbackItop, 0x21u, "MSCAL callback should receive pending ITOPS");
            t.Equals(mem.vif1_regs.top, 4u, "MSCAL should latch TOP from TOPS");
            t.Equals(mem.vif1_regs.itop, 0x21u, "MSCAL should latch ITOP from ITOPS");
            t.IsTrue((mem.vif1_regs.stat & (1u << 7)) != 0u, "MSCAL should toggle DBF on");
            t.Equals(mem.vif1_regs.tops, 6u, "DBF=1 should make TOPS=BASE+OFST");

            const uint32_t mscnt = makeVifCmd(0x17u, 0u, 0u);
            mem.processVIF1Data(reinterpret_cast<const uint8_t *>(&mscnt), sizeof(mscnt));

            t.Equals(callbackCount, 1u, "MSCNT should not invoke MSCAL callback");
            t.IsTrue((mem.vif1_regs.stat & (1u << 7)) == 0u, "MSCNT should toggle DBF back off");
            t.Equals(mem.vif1_regs.tops, 4u, "DBF=0 should make TOPS=BASE");
            t.Equals(mem.vif1_regs.top, 6u, "MSCNT should latch TOP from current TOPS before toggling");
            t.Equals(mem.vif1_regs.itop, 0x21u, "MSCNT should keep latching ITOP from ITOPS");
        });

        tc.Run("VU0 microprogram executes against VU0 code and data memory", [](TestCase &t)
        {
            PS2Runtime runtime;
            t.IsTrue(runtime.memory().initialize(), "PS2Memory initialize should succeed");
            t.IsTrue(runtime.syncCoreSubsystems(), "runtime core subsystems should bind");

            uint8_t *const code = runtime.memory().getVU0Code();
            uint8_t *const data = runtime.memory().getVU0Data();
            std::memset(code, 0, PS2_VU0_CODE_SIZE);
            std::memset(data, 0, PS2_VU0_DATA_SIZE);

            const float input[4] = {1.0f, 2.0f, 3.0f, 4.0f};
            std::memcpy(data, input, sizeof(input));

            constexpr uint32_t kVuNop = 0x0000003Fu;
            constexpr uint32_t kVuEndNop = 0x4000003Fu;
            writeVuInstructionPair(code, 0u, makeVuLq(0xFu, 1u, 0u, 0), kVuNop);
            writeVuInstructionPair(code, 8u, 0u, makeVuAdd(0xFu, 2u, 1u, 1u));
            writeVuInstructionPair(code, 16u, makeVuSq(0xFu, 2u, 0u, 1), kVuEndNop);

            R5900Context ctx;
            runtime.executeVU0Microprogram(runtime.memory().getRDRAM(), &ctx, 0u);

            float output[4]{};
            std::memcpy(output, data + 16u, sizeof(output));
            t.Equals(output[0], 2.0f, "VU0 output x should be doubled");
            t.Equals(output[1], 4.0f, "VU0 output y should be doubled");
            t.Equals(output[2], 6.0f, "VU0 output z should be doubled");
            t.Equals(output[3], 8.0f, "VU0 output w should be doubled");

            alignas(16) float vf2[4]{};
            _mm_storeu_ps(vf2, ctx.vu0_vf[2]);
            t.Equals(vf2[0], 2.0f, "VU0 VF2.x should copy back to CPU context");
            t.Equals(static_cast<uint32_t>(ctx.vi[0]), 0u, "VU0 VI0 should remain zero");
        });

        tc.Run("GS sprite draw applies XYOFFSET and fully-outside scissor should not render", [](TestCase &t)
        {
            std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE, 0u);
            GS gs;
            gs.init(vram.data(), static_cast<uint32_t>(vram.size()), nullptr);

            const uint64_t frame1 =
                (0ull << 0) |   // FBP
                (1ull << 16) |  // FBW
                (0ull << 24) |  // PSM CT32
                (0ull << 32);   // FBMSK

            const uint64_t zbuf1 = (1ull << 32);

            gs.writeRegister(GS_REG_FRAME_1, frame1);
            gs.writeRegister(GS_REG_ZBUF_1, zbuf1);
            gs.writeRegister(GS_REG_TEST_1, 0x30000ull);

            // XYOFFSET=1,1 pixels (16.4 fixed point).
            const uint64_t xyoffset = (16ull) | (16ull << 32);
            gs.writeRegister(GS_REG_XYOFFSET_1, xyoffset);

            // Scissor initially includes pixel (1,1).
            const uint64_t scissorInside = (0ull) | (3ull << 16) | (0ull << 32) | (3ull << 48);
            gs.writeRegister(GS_REG_SCISSOR_1, scissorInside);

            gs.writeRegister(GS_REG_PRIM, static_cast<uint64_t>(GS_PRIM_SPRITE));
            gs.writeRegister(GS_REG_RGBAQ, 0xFF3214C8ull); // RGBA=(200,20,50,255)

            // With XYOFFSET=(1,1), vertex at (2,2) draws to pixel (1,1).
            const uint64_t xyz = (32ull) | (32ull << 16) | (0ull << 32);
            gs.writeRegister(GS_REG_XYZ2, xyz);
            gs.writeRegister(GS_REG_XYZ2, xyz);

            const uint32_t insideOff = frameOffsetBytes(1u, 1u, 1u);
            t.Equals(vram[insideOff + 0u], static_cast<uint8_t>(200u), "inside draw should write R");
            t.Equals(vram[insideOff + 1u], static_cast<uint8_t>(20u), "inside draw should write G");
            t.Equals(vram[insideOff + 2u], static_cast<uint8_t>(50u), "inside draw should write B");
            t.Equals(vram[insideOff + 3u], static_cast<uint8_t>(255u), "inside draw should write A");

            std::memset(vram.data(), 0, 1024u);

            // Move scissor so target pixel is fully outside.
            const uint64_t scissorOutside = (3ull) | (4ull << 16) | (3ull << 32) | (4ull << 48);
            gs.writeRegister(GS_REG_SCISSOR_1, scissorOutside);
            gs.writeRegister(GS_REG_XYZ2, xyz);
            gs.writeRegister(GS_REG_XYZ2, xyz);

            bool anyWrite = false;
            for (size_t i = 0; i < 1024u; ++i)
            {
                if (vram[i] != 0u)
                {
                    anyWrite = true;
                    break;
                }
            }
            t.IsFalse(anyWrite, "fully-outside sprite should not render any pixel");
        });

        tc.Run("GS alpha blend uses ALPHA register FIX factor", [](TestCase &t)
        {
            std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE, 0u);
            GS gs;
            gs.init(vram.data(), static_cast<uint32_t>(vram.size()), nullptr);

            const uint64_t frame1 =
                (0ull << 0) |   // FBP
                (1ull << 16) |  // FBW
                (0ull << 24) |  // PSM CT32
                (0ull << 32);   // FBMSK
            const uint64_t zbuf1 = (1ull << 32);
            gs.writeRegister(GS_REG_FRAME_1, frame1);
            gs.writeRegister(GS_REG_ZBUF_1, zbuf1);
            gs.writeRegister(GS_REG_SCISSOR_1, (0ull) | (4ull << 16) | (0ull << 32) | (4ull << 48));
            gs.writeRegister(GS_REG_XYOFFSET_1, 0ull);
            gs.writeRegister(GS_REG_TEST_1, 0x30000ull);

            const uint32_t pxOff = frameOffsetBytes(1u, 1u, 1u);
            vram[pxOff + 0u] = 40u;
            vram[pxOff + 1u] = 40u;
            vram[pxOff + 2u] = 40u;
            vram[pxOff + 3u] = 255u;

            // ABE on sprite prim.
            gs.writeRegister(GS_REG_PRIM, static_cast<uint64_t>(GS_PRIM_SPRITE) | (1ull << 6));

            // ALPHA: (A-B)*FIX/128 + D
            // A=Cs(0), B=Cd(1), C=FIX(2), D=Cd(1), FIX=64.
            const uint64_t alpha = (0ull << 0) | (1ull << 2) | (2ull << 4) | (1ull << 6) | (64ull << 32);
            gs.writeRegister(GS_REG_ALPHA_1, alpha);
            gs.writeRegister(GS_REG_RGBAQ, 0xFFC8C8C8ull); // src RGB = 200

            const uint64_t xyz = (16ull) | (16ull << 16) | (0ull << 32); // pixel (1,1)
            gs.writeRegister(GS_REG_XYZ2, xyz);
            gs.writeRegister(GS_REG_XYZ2, xyz);

            // ((200 - 40) * 64 >> 7) + 40 = 120
            t.Equals(vram[pxOff + 0u], static_cast<uint8_t>(120u), "alpha blend should update R with FIX factor");
            t.Equals(vram[pxOff + 1u], static_cast<uint8_t>(120u), "alpha blend should update G with FIX factor");
            t.Equals(vram[pxOff + 2u], static_cast<uint8_t>(120u), "alpha blend should update B with FIX factor");
        });

        tc.Run("notifyRuntimeStop joins guest worker threads before teardown", [](TestCase &t)
        {
            notifyRuntimeStop();
            PS2Runtime runtime;
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0u);

            constexpr uint32_t kEntry = 0x250000u;
            constexpr uint32_t kThreadParamAddr = 0x2600u;
            const uint32_t threadParam[7] = {
                0u,          // attr
                kEntry,      // entry
                0x00100000u, // stack
                0x00000400u, // stack size
                0x00110000u, // gp
                8u,          // priority
                0u           // option
            };

            runtime.registerFunction(kEntry, &testRuntimeWorkerLoop);
            std::memcpy(rdram.data() + kThreadParamAddr, threadParam, sizeof(threadParam));

            R5900Context createCtx{};
            setRegU32(createCtx, 4, kThreadParamAddr);
            CreateThread(rdram.data(), &createCtx, &runtime);
            const int32_t tid = getRegS32(createCtx, 2);
            t.IsTrue(tid > 0, "CreateThread should succeed for teardown-join test");

            R5900Context startCtx{};
            setRegU32(startCtx, 4, static_cast<uint32_t>(tid));
            setRegU32(startCtx, 5, 0u);
            StartThread(rdram.data(), &startCtx, &runtime);
            t.Equals(getRegS32(startCtx, 2), KE_OK, "StartThread should launch worker");

            const bool started = waitUntil([&]()
            {
                return g_activeThreads.load(std::memory_order_relaxed) > 0;
            }, std::chrono::milliseconds(500));
            t.IsTrue(started, "worker thread should become active");

            runtime.requestStop();
            const bool drained = waitUntil([&]()
            {
                return g_activeThreads.load(std::memory_order_relaxed) == 0;
            }, std::chrono::milliseconds(2000));
            t.IsTrue(drained, "requestStop should drain all guest worker threads");

            notifyRuntimeStop();
        });

        tc.Run("Semaphore poll/signal remains stable under host-thread contention", [](TestCase &t)
        {
            notifyRuntimeStop();
            PS2Runtime runtime;
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0u);

            constexpr uint32_t kParamAddr = 0x2000u;
            const uint32_t semaParam[6] = {
                0u, // count
                1u, // max_count
                1u, // init_count
                0u, // wait_threads
                0u, // attr
                0u  // option
            };
            std::memcpy(rdram.data() + kParamAddr, semaParam, sizeof(semaParam));

            R5900Context createCtx{};
            setRegU32(createCtx, 4, kParamAddr);
            CreateSema(rdram.data(), &createCtx, &runtime);
            const int32_t sid = getRegS32(createCtx, 2);
            t.IsTrue(sid > 0, "CreateSema should return a valid sid");

            std::atomic<int32_t> pollOkCount{0};
            std::atomic<int32_t> signalOkCount{0};
            std::atomic<bool> pollerThrew{false};
            std::atomic<bool> signalerThrew{false};

            std::thread poller([&]()
            {
                try
                {
                    for (int i = 0; i < 64; ++i)
                    {
                        R5900Context pollCtx{};
                        setRegU32(pollCtx, 4, static_cast<uint32_t>(sid));
                        PollSema(rdram.data(), &pollCtx, &runtime);
                        if (getRegS32(pollCtx, 2) == sid)
                        {
                            pollOkCount.fetch_add(1, std::memory_order_relaxed);
                        }
                    }
                }
                catch (...)
                {
                    pollerThrew.store(true, std::memory_order_release);
                }
            });

            std::thread signaler([&]()
            {
                try
                {
                    for (int i = 0; i < 64; ++i)
                    {
                        R5900Context signalCtx{};
                        setRegU32(signalCtx, 4, static_cast<uint32_t>(sid));
                        SignalSema(rdram.data(), &signalCtx, &runtime);
                        if (getRegS32(signalCtx, 2) == sid)
                        {
                            signalOkCount.fetch_add(1, std::memory_order_relaxed);
                        }
                    }
                }
                catch (...)
                {
                    signalerThrew.store(true, std::memory_order_release);
                }
            });

            if (poller.joinable())
            {
                poller.join();
            }
            if (signaler.joinable())
            {
                signaler.join();
            }

            t.IsFalse(pollerThrew.load(std::memory_order_acquire),
                      "PollSema worker thread should not throw");
            t.IsFalse(signalerThrew.load(std::memory_order_acquire),
                      "SignalSema worker thread should not throw");
            t.IsTrue(pollOkCount.load(std::memory_order_relaxed) > 0,
                     "contended PollSema should observe at least one successful acquire");
            t.IsTrue(signalOkCount.load(std::memory_order_relaxed) > 0,
                     "contended SignalSema should observe successful releases");

            constexpr uint32_t kStatusAddr = 0x2100u;
            R5900Context referCtx{};
            setRegU32(referCtx, 4, static_cast<uint32_t>(sid));
            setRegU32(referCtx, 5, kStatusAddr);
            ReferSemaStatus(rdram.data(), &referCtx, &runtime);
            t.Equals(getRegS32(referCtx, 2), KE_OK, "ReferSemaStatus should succeed after contention");

            int32_t finalCount = 0;
            std::memcpy(&finalCount, rdram.data() + kStatusAddr + 0u, sizeof(finalCount));
            t.IsTrue(finalCount >= 0 && finalCount <= 1, "semaphore count should remain within [0, max_count]");

            runtime.requestStop();
            notifyRuntimeStop();
        });

        tc.Run("WaitEventFlag AND-mode is stable under concurrent setters", [](TestCase &t)
        {
            notifyRuntimeStop();
            PS2Runtime runtime;
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0u);

            constexpr uint32_t kEventParamAddr = 0x2400u;
            constexpr uint32_t kResBitsAddr = 0x2410u;
            const uint32_t eventParam[3] = {0u, 0u, 0u};
            std::memcpy(rdram.data() + kEventParamAddr, eventParam, sizeof(eventParam));

            R5900Context createCtx{};
            setRegU32(createCtx, 4, kEventParamAddr);
            CreateEventFlag(rdram.data(), &createCtx, &runtime);
            const int32_t eid = getRegS32(createCtx, 2);
            t.IsTrue(eid > 0, "CreateEventFlag should return a valid id");

            std::atomic<bool> waiterDone{false};
            std::atomic<int32_t> waiterRet{-9999};
            std::atomic<uint32_t> waiterBits{0u};
            std::atomic<bool> waiterThrew{false};
            std::atomic<bool> setterAThrew{false};
            std::atomic<bool> setterBThrew{false};

            std::thread waiter([&]()
            {
                try
                {
                    R5900Context waitCtx{};
                    setRegU32(waitCtx, 4, static_cast<uint32_t>(eid));
                    setRegU32(waitCtx, 5, 0x3u); // wait for bit0 and bit1 (AND mode)
                    setRegU32(waitCtx, 6, 0u);   // AND, no clear
                    setRegU32(waitCtx, 7, kResBitsAddr);
                    WaitEventFlag(rdram.data(), &waitCtx, &runtime);
                    waiterRet.store(getRegS32(waitCtx, 2), std::memory_order_relaxed);
                    uint32_t bits = 0u;
                    std::memcpy(&bits, rdram.data() + kResBitsAddr, sizeof(bits));
                    waiterBits.store(bits, std::memory_order_relaxed);
                }
                catch (...)
                {
                    waiterThrew.store(true, std::memory_order_release);
                }
                waiterDone.store(true, std::memory_order_release);
            });

            std::thread setterA([&]()
            {
                try
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    R5900Context setCtx{};
                    setRegU32(setCtx, 4, static_cast<uint32_t>(eid));
                    setRegU32(setCtx, 5, 0x1u);
                    SetEventFlag(rdram.data(), &setCtx, &runtime);
                }
                catch (...)
                {
                    setterAThrew.store(true, std::memory_order_release);
                }
            });

            std::thread setterB([&]()
            {
                try
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(15));
                    R5900Context setCtx{};
                    setRegU32(setCtx, 4, static_cast<uint32_t>(eid));
                    setRegU32(setCtx, 5, 0x2u);
                    SetEventFlag(rdram.data(), &setCtx, &runtime);
                }
                catch (...)
                {
                    setterBThrew.store(true, std::memory_order_release);
                }
            });

            const bool woke = waitUntil([&]()
            {
                return waiterDone.load(std::memory_order_acquire);
            }, std::chrono::milliseconds(500));

            if (setterA.joinable())
            {
                setterA.join();
            }
            if (setterB.joinable())
            {
                setterB.join();
            }
            if (waiter.joinable())
            {
                waiter.join();
            }

            t.IsFalse(waiterThrew.load(std::memory_order_acquire),
                      "WaitEventFlag waiter thread should not throw");
            t.IsFalse(setterAThrew.load(std::memory_order_acquire),
                      "SetEventFlag setterA thread should not throw");
            t.IsFalse(setterBThrew.load(std::memory_order_acquire),
                      "SetEventFlag setterB thread should not throw");
            t.IsTrue(woke, "WaitEventFlag AND waiter should wake after both bits are published");
            t.Equals(waiterRet.load(std::memory_order_relaxed), KE_OK, "WaitEventFlag should return KE_OK");
            t.IsTrue((waiterBits.load(std::memory_order_relaxed) & 0x3u) == 0x3u,
                     "WaitEventFlag result bits should include both concurrently-set bits");

            R5900Context deleteCtx{};
            setRegU32(deleteCtx, 4, static_cast<uint32_t>(eid));
            DeleteEventFlag(rdram.data(), &deleteCtx, &runtime);
            runtime.requestStop();
            notifyRuntimeStop();
        });

        tc.Run("sceVu0ApplyMatrix uses libvux matrix math with the imported EE ABI", [](TestCase &t)
        {
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0u);
            R5900Context ctx{};

            constexpr uint32_t kOutAddr = 0x00100000u;
            constexpr uint32_t kMatrixAddr = 0x00100040u;
            constexpr uint32_t kSrcAddr = 0x00100080u;

            const float matrix[16] = {
                1.0f, 2.0f, 3.0f, 4.0f,
                5.0f, 6.0f, 7.0f, 8.0f,
                9.0f, 10.0f, 11.0f, 12.0f,
                13.0f, 14.0f, 15.0f, 16.0f,
            };
            const float src[4] = {1.0f, 2.0f, 3.0f, 1.0f};
            std::memcpy(rdram.data() + kMatrixAddr, matrix, sizeof(matrix));
            std::memcpy(rdram.data() + kSrcAddr, src, sizeof(src));

            setRegU32(ctx, 4, kOutAddr);
            setRegU32(ctx, 5, kMatrixAddr);
            setRegU32(ctx, 6, kSrcAddr);

            ps2_stubs::sceVu0ApplyMatrix(rdram.data(), &ctx, nullptr);

            float out[4]{};
            std::memcpy(out, rdram.data() + kOutAddr, sizeof(out));
            t.Equals(out[0], 51.0f, "sceVu0ApplyMatrix should compute X with libvux layout");
            t.Equals(out[1], 58.0f, "sceVu0ApplyMatrix should compute Y with libvux layout");
            t.Equals(out[2], 65.0f, "sceVu0ApplyMatrix should compute Z with libvux layout");
            t.Equals(out[3], 72.0f, "sceVu0ApplyMatrix should compute W with libvux layout");
            t.Equals(getRegS32(ctx, 2), 0, "sceVu0ApplyMatrix should report success");
        });

        tc.Run("sceVu0TransposeMatrix transposes a 4x4 matrix with dst/src ABI", [](TestCase &t)
        {
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0u);
            R5900Context ctx{};

            constexpr uint32_t kDstAddr = 0x00100100u;
            constexpr uint32_t kSrcAddr = 0x00100140u;
            const float src[16] = {
                1.0f, 2.0f, 3.0f, 4.0f,
                5.0f, 6.0f, 7.0f, 8.0f,
                9.0f, 10.0f, 11.0f, 12.0f,
                13.0f, 14.0f, 15.0f, 16.0f,
            };
            std::memcpy(rdram.data() + kSrcAddr, src, sizeof(src));

            setRegU32(ctx, 4, kDstAddr);
            setRegU32(ctx, 5, kSrcAddr);

            ps2_stubs::sceVu0TransposeMatrix(rdram.data(), &ctx, nullptr);

            float out[16]{};
            std::memcpy(out, rdram.data() + kDstAddr, sizeof(out));
            t.Equals(out[0], 1.0f, "transpose should preserve [0][0]");
            t.Equals(out[1], 5.0f, "transpose should swap row 0 col 1");
            t.Equals(out[2], 9.0f, "transpose should swap row 0 col 2");
            t.Equals(out[3], 13.0f, "transpose should swap row 0 col 3");
            t.Equals(out[4], 2.0f, "transpose should swap row 1 col 0");
            t.Equals(out[5], 6.0f, "transpose should preserve [1][1]");
            t.Equals(out[10], 11.0f, "transpose should preserve [2][2]");
            t.Equals(out[12], 4.0f, "transpose should swap row 3 col 0");
            t.Equals(out[15], 16.0f, "transpose should preserve [3][3]");
            t.Equals(getRegS32(ctx, 2), 0, "sceVu0TransposeMatrix should report success");
        });

        tc.Run("sceVif1PkReset preserves the packet base pointer and clears open tag state", [](TestCase &t)
        {
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0u);
            R5900Context ctx{};

            constexpr uint32_t kStateAddr = 0x00100200u;
            constexpr uint32_t kBaseAddr = 0x00101000u;

            setRegU32(ctx, 4, kStateAddr);
            setRegU32(ctx, 5, kBaseAddr);
            ps2_stubs::sceVif1PkInit(rdram.data(), &ctx, nullptr);

            const uint32_t dirtyCurrent = kBaseAddr + 0x40u;
            const uint32_t dirtyPending = 0x12345678u;
            const uint32_t dirtyDirectOpen = 0x00ABCDEFu;
            const uint32_t dirtyGifOpen = 0x00112233u;
            std::memcpy(rdram.data() + kStateAddr + 0u, &dirtyCurrent, sizeof(dirtyCurrent));
            std::memcpy(rdram.data() + kStateAddr + 8u, &dirtyPending, sizeof(dirtyPending));
            std::memcpy(rdram.data() + kStateAddr + 12u, &dirtyDirectOpen, sizeof(dirtyDirectOpen));
            std::memcpy(rdram.data() + kStateAddr + 20u, &dirtyGifOpen, sizeof(dirtyGifOpen));

            std::memset(&ctx, 0, sizeof(ctx));
            setRegU32(ctx, 4, kStateAddr);
            ps2_stubs::sceVif1PkReset(rdram.data(), &ctx, nullptr);

            uint32_t current = 0u;
            uint32_t base = 0u;
            uint32_t pending = 0u;
            uint32_t directOpen = 0u;
            uint32_t gifOpen = 0u;
            std::memcpy(&current, rdram.data() + kStateAddr + 0u, sizeof(current));
            std::memcpy(&base, rdram.data() + kStateAddr + 4u, sizeof(base));
            std::memcpy(&pending, rdram.data() + kStateAddr + 8u, sizeof(pending));
            std::memcpy(&directOpen, rdram.data() + kStateAddr + 12u, sizeof(directOpen));
            std::memcpy(&gifOpen, rdram.data() + kStateAddr + 20u, sizeof(gifOpen));

            t.Equals(current, kBaseAddr, "sceVif1PkReset should restore current pointer to the packet base");
            t.Equals(base, kBaseAddr, "sceVif1PkReset should preserve the packet base pointer");
            t.Equals(pending, 0u, "sceVif1PkReset should clear pending count tracking");
            t.Equals(directOpen, 0u, "sceVif1PkReset should clear direct-code open state");
            t.Equals(gifOpen, 0u, "sceVif1PkReset should clear GIF-tag open state");
            t.Equals(::getRegU32(&ctx, 2), kBaseAddr, "sceVif1PkReset should return the packet base pointer");
        });

        tc.Run("sceVif1PkCloseDirectCode encodes DIRECT length in qwords", [](TestCase &t)
        {
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0u);
            R5900Context ctx{};

            constexpr uint32_t kStateAddr = 0x00100400u;
            constexpr uint32_t kBaseAddr = 0x00102000u;

            setRegU32(ctx, 4, kStateAddr);
            setRegU32(ctx, 5, kBaseAddr);
            ps2_stubs::sceVif1PkInit(rdram.data(), &ctx, nullptr);

            std::memset(&ctx, 0, sizeof(ctx));
            setRegU32(ctx, 4, kStateAddr);
            setRegU32(ctx, 5, 0u);
            ps2_stubs::sceVif1PkCnt(rdram.data(), &ctx, nullptr);

            std::memset(&ctx, 0, sizeof(ctx));
            setRegU32(ctx, 4, kStateAddr);
            setRegU32(ctx, 5, 0u);
            ps2_stubs::sceVif1PkOpenDirectCode(rdram.data(), &ctx, nullptr);

            std::memset(&ctx, 0, sizeof(ctx));
            setRegU32(ctx, 4, kStateAddr);
            setRegU32(ctx, 5, 4u); // reserve one qword worth of GIF payload
            ps2_stubs::sceVif1PkReserve(rdram.data(), &ctx, nullptr);

            std::memset(&ctx, 0, sizeof(ctx));
            setRegU32(ctx, 4, kStateAddr);
            ps2_stubs::sceVif1PkCloseDirectCode(rdram.data(), &ctx, nullptr);

            uint32_t directCmd = 0u;
            std::memcpy(&directCmd, rdram.data() + kBaseAddr + 12u, sizeof(directCmd));
            t.Equals(directCmd, 0x50000001u, "sceVif1PkCloseDirectCode should store a 1-QW DIRECT length");
        });
    });
}
