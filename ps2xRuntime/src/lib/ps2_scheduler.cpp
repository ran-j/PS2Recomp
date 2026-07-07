// ps2_scheduler.cpp — N=1 fiber cooperative scheduler for PS2Recomp.
//
// Architecture:
//   - Exactly ONE dedicated host OS thread (g_guest_thread, the "guest executor")
//     runs all guest fibers. This eliminates cross-thread swapcontext UB
//     structurally — a ucontext_t is only ever saved and resumed on the one thread.
//   - Guest threads are mapped to FiberContext objects; each owns a PS2Fiber
//     (ucontext_t on POSIX with guard page; joinable pthread on Vita).
//   - Host threads (IRQ worker, alarm worker, RPC worker) that need to run guest
//     code acquire the guest token via AsyncGuestScope (RAII).
//   - Cooperative yield points sampled every 128 back-edges via yield_point().

#include "ps2_scheduler_internal.h"
#include "ps2_fiber.h"
#include <ps2_runtime.h>
#include <ps2_runtime_macros.h>
#include "Kernel/Syscalls/Helpers/ThreadExit.h" // canonical ThreadExitException
#include "Kernel/Syscalls/Interrupt.h" // stopInterruptWorker
#include "Kernel/Syscalls/Sync.h"      // stopAlarmWorker

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <stdexcept>

// g_currentThreadId — -1 means this host thread is not currently running a fiber.
thread_local int g_currentThreadId = -1;

// Globals
std::mutex              g_sched_mutex;
std::condition_variable g_sched_cv;
FiberContext*           g_run_queue               = nullptr;
FiberContext*           g_running_fiber            = nullptr;
bool                    g_guest_token_held_by_host = false;
// True on a host (non-fiber) worker thread that currently holds the guest token
// (acquired via async_guest_begin, released via async_guest_end). Used to assert
// the token is released by the same thread that acquired it, and so block_current
// can report whether the calling non-fiber worker owns the token.
static thread_local bool tls_holds_guest_token = false;
std::atomic<bool>       g_stop{false};
// Host workers currently blocked in async_guest_begin() waiting for the guest
// token. Read by the executor's resume predicate (it must sleep while a worker
// is parked) and by yield_point / diagnostics via ps2sched::host_token_waiters().
static std::atomic<int> g_host_token_waiters{0};
// Number of times a host worker has been GRANTED the token via
// async_guest_begin(). Written under g_sched_mutex; read by the executor's
// resume predicate (also under g_sched_mutex) to implement a fair handoff:
// the executor defers to parked workers until at least one grant happens,
// then may resume fibers again even if more workers are waiting.
static uint64_t g_host_token_grants = 0;

std::unordered_map<int, std::unique_ptr<FiberContext>> g_fiber_map;
std::thread g_guest_thread;

// True only on the single guest executor thread (g_guest_thread). Set once at the
// top of guest_executor_main, before any fiber can be resumed. Read by
// ps2fiber_on_executor_thread(). A thread_local bool is lock-free and avoids the
// (typically lock-based) std::atomic<std::thread::id> on the context-switch hot
// path. Always correct: only the executor thread ever runs guest_executor_main.
static thread_local bool tls_is_executor_thread = false;

// Thread-locals
thread_local FiberContext* tls_current_fiber   = nullptr;
thread_local uint32_t      tls_backedge_counter = 0;

// True on any guest execution context: the executor thread (all backends), or
// a fiber's own OS thread on the pthread backend (which sets tls_current_fiber
// for the lifetime of the fiber, since that thread owns the execution slot
// whenever the fiber runs). Backs both is_guest_thread() and the
// async_guest_begin() abort guard, so the two can never drift apart.
static inline bool on_guest_execution_slot()
{
    return tls_is_executor_thread || tls_current_fiber != nullptr;
}

// Fiber exit hook (set once by Thread.cpp)
void (*g_fiber_exit_hook)(int tid, uint8_t* rdram, R5900Context* ctx, PS2Runtime* rt) = nullptr;

// Stop callback — set by scheduler_set_stop_callback(); invoked by
// scheduler_shutdown() before joining g_guest_thread. Read/written under
// g_sched_mutex. nullptr-safe. g_request_runtime_stop_ctx is passed back to
// the callback unmodified, letting the caller carry its own context instead
// of relying on a file-static (e.g. PS2Runtime::run()'s `this`).
static void (*g_request_runtime_stop_fn)(void*) = nullptr;
static void* g_request_runtime_stop_ctx = nullptr;

static constexpr size_t kFiberStackBytes = 1024 * 1024; // 1 MiB

// Fatal helper — terminate on invariant violation
#define SCHED_REQUIRE(cond, msg)                                               \
    do                                                                         \
    {                                                                          \
        if (!(cond))                                                           \
        {                                                                      \
            std::fprintf(stderr, "FATAL [ps2sched]: " msg "\n");              \
            std::terminate();                                                  \
        }                                                                      \
    } while (0)

// ---------------------------------------------------------------------------
// fiber_trampoline — entry point of every PS2Fiber.
//
// Runs on g_guest_thread (the guest executor). Catches ALL exceptions so the
// executor can continue scheduling other fibers after this one exits.
// ---------------------------------------------------------------------------
static void fiber_trampoline(void* arg)
{
    FiberContext* fc = static_cast<FiberContext*>(arg);
    SCHED_REQUIRE(fc != nullptr, "fiber_trampoline null arg");
    // guest_executor_main set tls_current_fiber on the executor thread before
    // resuming us (the N=1 / ucontext design runs all guest code on that one
    // thread). On the pthread backend each fiber runs on its own OS thread, so
    // tls_current_fiber is only set on the executor thread and this check does
    // not apply — ps2fiber_thread_main sets tls_current_fiber_ptr instead.
#if !defined(PS2X_FIBER_PTHREAD)
    SCHED_REQUIRE(fc == tls_current_fiber, "fiber_trampoline fc mismatch");
#endif

    // pthread backend: each fiber runs on its own OS thread. guest_executor_main
    // sets tls_current_fiber only on the executor thread, so fiber OS threads
    // never have it set — breaking arm_park, block_current, and
    // current_fiber_token. Publish the FiberContext* on this fiber's thread now
    // so the entire blocking protocol works correctly under PS2X_FIBER_PTHREAD.
    // This is also what makes the fiber's OS thread a guest execution context
    // for on_guest_execution_slot(): it owns the execution slot whenever it
    // runs (the executor is blocked in ps2fiber_resume until the fiber yields
    // back), so is_guest_thread() reads true here and shared helpers take the
    // synchronous path instead of borrowing the token via async_guest_begin()
    // — which would park waiting for this very fiber to stop: a self-deadlock.
    // tls_current_fiber is per-OS-thread TLS and dies with the fiber's thread,
    // so no reset is needed.
#if defined(PS2X_FIBER_PTHREAD)
    tls_current_fiber = fc;
#endif

    PS2Runtime* rt    = fc->rt;    // from struct, not smuggled GPRs
    uint8_t*    rdram = fc->rdram;

    try
    {
        rt->dispatchLoop(rdram, &fc->cpu);
    }
    catch (const ThreadExitException&)
    {
        // Cooperative termination — fall through.
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "[ps2sched] fiber tid=%d uncaught std::exception: %s\n",
                     fc->tid, e.what());
    }
    catch (...)
    {
        std::fprintf(stderr, "[ps2sched] fiber tid=%d uncaught unknown exception\n", fc->tid);
    }

    // Mark Exiting BEFORE running the exit hook. If a guest exit handler
    // makes a blocking syscall (block_current -> ps2fiber_yield), the executor's
    // post-resume code sees Exiting and RE-ENQUEUES the fiber (treated like a
    // cooperative yield) instead of freeing it — the trampoline still has a live
    // stack frame here. When the wakeup arrives the fiber resumes inside the
    // hook and continues. Only after the hook returns do we transition to
    // Finished, which is the executor's signal to free the fiber.
    {
        std::lock_guard<std::mutex> lk(g_sched_mutex);
        fc->state = FiberContext::State::Exiting;
    }

    if (g_fiber_exit_hook)
    {
        // The exit hook runs guest exit handlers (arbitrary recompiled code). If
        // one calls ExitThread it throws ThreadExitException; any such throw must
        // NOT cross the swapcontext boundary below (UB). Swallow it: the fiber is
        // already exiting.
        try
        {
            g_fiber_exit_hook(fc->tid, rdram, &fc->cpu, rt);
        }
        catch (const ThreadExitException&)
        {
            // Guest exit handler called ExitThread — normal exit completion.
        }
        catch (const std::exception& e)
        {
            std::fprintf(stderr,
                         "[ps2sched] fiber tid=%d exit hook threw std::exception: %s\n",
                         fc->tid, e.what());
        }
        catch (...)
        {
            std::fprintf(stderr,
                         "[ps2sched] fiber tid=%d exit hook threw unknown exception\n",
                         fc->tid);
        }
    }

    {
        std::lock_guard<std::mutex> lk(g_sched_mutex);
        fc->state = FiberContext::State::Finished;
    }
    g_sched_cv.notify_all(); // wake join_fiber waiters / executor

    // pthread backend: clear tls_current_fiber on the fiber's OS thread before
    // handing control back to the executor. This mirrors the executor clearing it
    // after ps2fiber_resume returns (line ~211) and ensures no dangling reference
    // to fc remains in TLS after the fiber is freed.
#if defined(PS2X_FIBER_PTHREAD)
    tls_current_fiber = nullptr;
#endif

#if !defined(PS2X_FIBER_PTHREAD)
    ps2fiber_yield(); // back to guest_executor_main; never returns
    SCHED_REQUIRE(false, "fiber_trampoline resumed after Finished");
#endif
    // pthread backend: ps2fiber_thread_main returns from fn() here, sets
    // PS2Fiber::finished=true, clears TLS, and posts yield_sem — the fiber OS
    // thread then exits naturally and ps2fiber_free can pthread_join cleanly.
}

// ---------------------------------------------------------------------------
// guest_executor_main — the N=1 loop
// ---------------------------------------------------------------------------
static void guest_executor_main()
{
    // Publish our identity BEFORE anything can resume a fiber. ps2fiber_resume
    // (called below) asserts it runs on this thread via
    // ps2fiber_on_executor_thread(), which reads this TLS flag; it also makes
    // on_guest_execution_slot() (and so is_guest_thread()) true here.
    tls_is_executor_thread = true;

    std::unique_lock<std::mutex> lk(g_sched_mutex);
    // The wait predicate MUST be exactly (canQuit || canRun). A predicate that
    // short-circuits on bare g_stop can be instantly true while NEITHER the
    // break condition nor the pop condition holds (g_stop set with a non-empty
    // run queue and a parked host worker): the loop then continue's, cv.wait
    // sees the predicate already true and returns WITHOUT releasing
    // g_sched_mutex, and the executor livelocks — spinning under the lock and
    // starving the very waiters it is gated on.
    auto canQuit = []
    {
        return g_stop && g_run_queue == nullptr;
    };
    // Fair handoff to parked host workers. async_guest_begin()'s wakeup
    // predicate is (g_running_fiber == nullptr && !token_held), but the only
    // window where g_running_fiber is null is while THIS loop holds
    // g_sched_mutex between resumes. Without a waiters gate the executor's own
    // cv.wait predicate is instantly true again (the fiber is re-enqueued by
    // the post-resume code), so it re-resumes the fiber without ever sleeping,
    // and a parked host worker (e.g. the interrupt worker delivering VBlank)
    // can never win the token: the guest spins waiting for ticks the starved
    // worker cannot deliver.
    //
    // The gate must NOT be a bare (waiters == 0), though: sustained worker
    // traffic (workers looping async_guest_begin/end, as in the X2 CV-
    // contention test) would then starve fibers in the opposite direction.
    // Instead the executor defers until AT LEAST ONE worker has been granted
    // the token since it started deferring (g_host_token_grants advanced),
    // after which it may resume fibers even if more workers are waiting —
    // strict alternation under pressure, zero cost when no worker is parked.
    bool deferringToWaiters = false;
    uint64_t grantsAtDefer = 0;
    auto canRun = [&]
    {
        if (g_run_queue == nullptr ||
            g_running_fiber != nullptr ||
            g_guest_token_held_by_host)
        {
            return false;
        }
        if (g_host_token_waiters.load(std::memory_order_relaxed) == 0)
        {
            deferringToWaiters = false;
            return true;
        }
        if (!deferringToWaiters)
        {
            // A worker is parked and we have not yet deferred: start deferring
            // (sleep until a grant happens).
            deferringToWaiters = true;
            grantsAtDefer = g_host_token_grants;
            return false;
        }
        if (g_host_token_grants != grantsAtDefer)
        {
            // At least one worker got the token while we were deferring; take
            // our turn even if more workers are waiting (fairness).
            deferringToWaiters = false;
            return true;
        }
        return false;
    };
    while (true)
    {
        g_sched_cv.wait(lk, [&]
        {
            return canQuit() || canRun();
        });

        if (canQuit()) break;
        // canRun() held at the wait's final predicate evaluation and
        // g_sched_mutex has been held continuously since, so the state cannot
        // have changed. Do NOT re-call canRun() here: it is stateful (the
        // deferral bookkeeping above), and a second evaluation while workers
        // are still waiting would re-arm the deferral and skip our turn.
        // canQuit and canRun are mutually exclusive (queue null vs non-null),
        // so reaching this line means canRun held.

        FiberContext* fc = pop_head_locked();
        SCHED_REQUIRE(fc != nullptr, "executor popped null with non-empty predicate");

        fc->state = FiberContext::State::Running;
        g_running_fiber = fc;
        // Guest code runs on THIS (executor) thread, so publish the guest
        // identity here before resuming.
        tls_current_fiber = fc;
        g_currentThreadId = fc->tid;

        lk.unlock();
        ps2fiber_resume(fc->fiber); // runs until yield or finish
        lk.lock();

        tls_current_fiber = nullptr;
        g_currentThreadId = -1;
        g_running_fiber = nullptr;

        if (fc->state == FiberContext::State::Running ||
            fc->state == FiberContext::State::Exiting)
        {
            // Running: cooperative yield (maybe_yield / yield_point). The fiber
            //   normally enqueues itself before yielding; enqueue_locked is
            //   idempotent so this is a safe backstop.
            // Exiting: the exit hook yielded (e.g. a blocking syscall in a
            //   guest exit handler). It is NOT done yet — re-enqueue so it runs
            //   to completion. Do NOT free: fiber_trampoline still has a live
            //   stack frame.
            enqueue_locked(fc);
        }
        else if (fc->state == FiberContext::State::Blocked)
        {
            // A waker fired during the park window (between the fiber
            // setting state=Blocked and ps2fiber_yield returning here). It could
            // not enqueue safely (fc->next was owned by the running fiber), so it
            // set wake_pending instead. Honour it now — UNLESS the fiber was
            // suspended during the park window (a suspendCount > 0 means it
            // must stay off the run queue). The matching clear_suspend()
            // will wake it when the suspend is lifted.
            if (fc->wake_pending &&
                fc->suspendCount.load(std::memory_order_relaxed) == 0)
            {
                fc->wake_pending = false;
                enqueue_locked(fc); // sets state=Ready
            }
            // else: genuinely Blocked (or suspended); a future waker / resume
            // will enqueue it. Leave wake_pending as-is so a later resume can
            // honour it.
        }
        else if (fc->state == FiberContext::State::Finished)
        {
            // Erase the map entry while the lock is held so that a borrowed worker
            // racing in async_guest_begin cannot call StartThread on this tid and
            // insert a new FiberContext entry that we would then unconditionally clobber
            // after dropping and re-acquiring the lock. Moving out the unique_ptr
            // under the lock prevents the window between unlock and re-lock from
            // allowing tid reuse that would be silently destroyed.
            int tid = fc->tid;
            std::unique_ptr<FiberContext> dead;
            auto it = g_fiber_map.find(tid);
            if (it != g_fiber_map.end())
                dead = std::move(it->second);
            g_fiber_map.erase(tid);        // tid is now free for reuse under the lock
            PS2Fiber* deadFiber = dead ? dead->fiber : nullptr;
            if (dead) dead->fiber = nullptr; // suppress destructor double-free
            lk.unlock();
            ps2fiber_free(deadFiber);      // munmap outside the lock from a local
            // dead destructs here (FiberContext body) — fiber already freed above
            lk.lock();                     // re-acquire before next wait or break
        }
        // else Fresh: impossible after a resume.

        g_sched_cv.notify_all();
    }
    lk.unlock();
}

// ---------------------------------------------------------------------------
// wake_locked — race-safe wakeup. MUST be called with g_sched_mutex held.
// Idempotent and state-aware: safe to call whenever the caller has already
// applied whatever suspendCount/terminate gating IT cares about (make_ready /
// enqueue_external_wakeup_validated gate on suspendCount==0 before calling;
// request_terminate / clear_suspend force suspendCount to the
// value they intend first, then call this unconditionally -- see their
// comments for why gating on state==Blocked alone drops wakes that race the
// fiber's own publish/arm-park window). If fc is mid-park (state==Blocked or
// even still Running, but ps2fiber_yield has not yet returned to the
// executor), g_running_fiber still points at fc: in that window we MUST NOT
// touch fc->next, so we set wake_pending and let block_current() or the
// executor's post-resume code consume it. Otherwise (fc genuinely parked,
// off-queue, not the running fiber) enqueue normally; if already queued,
// no-op.
// ---------------------------------------------------------------------------
static void wake_locked(FiberContext* fc)
{
    // Called under g_sched_mutex. Idempotent: if already queued, nothing to do.
    if (fc->in_run_queue)
    {
        return;
    }

    if (g_running_fiber == fc)
    {
        // The fiber is still the running fiber. This covers two windows:
        //   1. mid-park: state==Blocked set (by arm_park or block_current) but
        //      ps2fiber_yield has not yet returned control to the executor.
        //   2. state==Blocked armed via arm_park(), the syscall published to
        //      an object wait-list and a waker fired before block_current().
        // In both cases fc->next is owned by the still-executing fiber, so we
        // MUST NOT enqueue_locked. Record the wakeup; block_current() (case 2)
        // or the executor post-resume code (case 1) honours wake_pending.
        fc->wake_pending = true;
    }
    else
    {
        // Fully parked (state==Blocked, not the running fiber): enqueue directly.
        enqueue_locked(fc);
    }
}

void ps2sched::scheduler_init()
{
    {
        std::lock_guard<std::mutex> lk(g_sched_mutex);
        g_run_queue                = nullptr;
        g_running_fiber            = nullptr;
        g_guest_token_held_by_host = false;
        g_stop.store(false, std::memory_order_relaxed);
        g_fiber_map.clear();
    }
    SCHED_REQUIRE(!g_guest_thread.joinable(), "scheduler_init while executor running");
    // Fresh scheduler epoch starts with zero counted guest threads; heals any
    // counter drift from a prior epoch's shutdown races. g_activeThreads (declared
    // extern in ps2_syscalls.h, already visible here via Interrupt.h/Sync.h) has
    // no concurrent decrementer at this point: the prior executor is proven
    // joined above and the new one has not been spawned yet, so a plain store
    // is sound (unlike in notifyRuntimeStop, which runs concurrently with a
    // possibly still-live fiber).
    g_activeThreads.store(0, std::memory_order_relaxed);
    // The executor thread publishes tls_is_executor_thread itself at the top of
    // guest_executor_main, before any fiber resume.
    g_guest_thread = std::thread(guest_executor_main);
}

void ps2sched::scheduler_set_stop_callback(void (*fn)(void*), void* ctx)
{
    std::lock_guard<std::mutex> lk(g_sched_mutex);
    g_request_runtime_stop_fn = fn;
    g_request_runtime_stop_ctx = fn ? ctx : nullptr;
}

void ps2sched::scheduler_shutdown()
{
    // Stop and JOIN the host workers (IRQ/vsync + alarm) FIRST, before we
    // tear down the fiber map. Those workers call enqueue_external_wakeup_validated()
    // into g_fiber_map; joining them here guarantees no such call can race the
    // teardown below, regardless of whether notifyRuntimeStop() already ran.
    // Both stop functions are idempotent (no-op if already stopped/joined).
    ps2_syscalls::stopInterruptWorker();
    ps2_syscalls::stopAlarmWorker();

    {
        std::lock_guard<std::mutex> lk(g_sched_mutex);
        g_stop.store(true, std::memory_order_release);
        // Re-terminate every fiber so blocked ones wake and unwind.
        for (auto& [tid, fc] : g_fiber_map)
        {
            if (fc->state == FiberContext::State::Blocked)
            {
                fc->terminateRequested.store(true, std::memory_order_relaxed);
                fc->suspendCount.store(0, std::memory_order_relaxed);
                wake_locked(fc.get());
            }
            else if (fc->state == FiberContext::State::Exiting)
            {
                // Exiting fibers yielded inside their exit hook (e.g. a blocking
                // syscall in a guest exit handler). They are off-queue but still
                // alive with a live trampoline stack frame. Do NOT set
                // terminateRequested (the exit path is already underway and an
                // unwind here would abandon the hook); just re-enqueue so the
                // executor runs them to completion before the quit predicate
                // (g_stop && run_queue==nullptr) is satisfied.
                fc->suspendCount.store(0, std::memory_order_relaxed);
                wake_locked(fc.get());
            }
            else
            {
                // Fresh / Ready / Running / Finished: leave terminateRequested set
                // so a still-running fiber unwinds at its next yield_point.
                fc->terminateRequested.store(true, std::memory_order_relaxed);
            }
        }
    }
    g_sched_cv.notify_all();

    // Request the runtime stop so dispatchLoop's while(!isStopRequested()) exits
    // between dispatched functions. Invoked OUTSIDE g_sched_mutex: the callback
    // runs runtime.requestStopFlagOnly(), which sets its own atomic and must not
    // nest under g_sched_mutex.
    void (*stopFn)(void*) = nullptr;
    void* stopCtx = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_sched_mutex);
        stopFn  = g_request_runtime_stop_fn;
        stopCtx = g_request_runtime_stop_ctx;
    }
    if (stopFn) stopFn(stopCtx);

    if (g_guest_thread.joinable()) g_guest_thread.join();

    // Executor has exited. No guest code runs now. Free any remaining fibers.
    std::lock_guard<std::mutex> lk(g_sched_mutex);
    if (!g_fiber_map.empty())
    {
        std::fprintf(stderr, "[ps2sched] WARNING: %zu fiber(s) remain after executor exit\n",
                     g_fiber_map.size());
    }
    for (auto& [tid, fc] : g_fiber_map)
    {
        if (fc->fiber)
        {
            ps2fiber_free(fc->fiber);
            fc->fiber = nullptr;
        }
    }
    g_fiber_map.clear();
    g_run_queue = nullptr;
}

void ps2sched::create_fiber(int tid, int priority, uint32_t entry,
                            uint32_t sp, uint32_t gp, uint32_t arg,
                            PS2Runtime* rt, uint8_t* rdram)
{
    // Refuse to create fibers once shutdown has begun. A fiber running its
    // terminate path can still reach StartThread -> create_fiber; if we allowed
    // it, the executor could partially run the new fiber during teardown and
    // leak its stack if it parks. Throwing here reuses StartThread's existing
    // allocation-failure path (it resets the thread to dormant and returns
    // KE_NO_MEMORY).
    {
        std::lock_guard<std::mutex> lk(g_sched_mutex);
        if (g_stop)
        {
            throw std::runtime_error("[ps2sched] create_fiber refused: scheduler stopping");
        }
    }

    // The R5900Context constructor zeroes all registers and sets the documented
    // reset defaults (cop0_random=47, vu0_q=1.0).
    auto fc = std::make_unique<FiberContext>();
    fc->tid      = tid;
    fc->priority = priority;
    fc->rt       = rt;
    fc->rdram    = rdram;

    fc->cpu.pc = entry;
    SET_GPR_U32(&fc->cpu, 29, sp);  // $sp
    SET_GPR_U32(&fc->cpu, 28, gp);  // $gp
    SET_GPR_U32(&fc->cpu, 4,  arg); // $a0 (passed directly, not smuggled through other registers)
    SET_GPR_U32(&fc->cpu, 31, 0u);  // $ra = 0 (returns -> pc==0 -> dispatchLoop break)

    PS2Fiber* fiber = ps2fiber_alloc(fiber_trampoline, fc.get(), kFiberStackBytes);
    if (!fiber)
    {
        throw std::runtime_error("[ps2sched] fiber allocation failed");
    }
    fc->fiber = fiber;
#if defined(PLATFORM_VITA) || defined(PS2X_FIBER_PTHREAD)
    ps2fiber_set_tid(fiber, tid);
#endif
    fc->state = FiberContext::State::Fresh;

    FiberContext* raw = fc.get();
    {
        std::lock_guard<std::mutex> lk(g_sched_mutex);
        g_fiber_map[tid] = std::move(fc);
        enqueue_locked(raw); // Fresh -> Ready
    }
    g_sched_cv.notify_all();
}

void ps2sched::request_terminate(int tid)
{
    std::lock_guard<std::mutex> lk(g_sched_mutex);
    FiberContext* fc = fiber_for(tid);
    if (!fc) return;
    fc->terminateRequested.store(true, std::memory_order_relaxed);
    // Force the suspend gate open unconditionally (not only when already
    // Blocked) so a suspended-and-parked target always becomes wakeable, and
    // so a suspend that races concurrently cannot re-close the gate behind
    // this terminate (mirrors clear_suspend's force-to-0).
    fc->suspendCount.store(0, std::memory_order_relaxed);
    // Always route through wake_locked, exactly like every other waker
    // (make_ready / enqueue_external_wakeup_validated / clear_suspend).
    // wake_locked is idempotent and state-aware:
    //   in_run_queue        -> no-op (already runnable)
    //   g_running_fiber==fc -> wake_pending=true (the fiber published to an
    //                          object wait-list and called arm_park(), state
    //                          is already Blocked, but has not yet reached
    //                          block_current()/ps2fiber_yield() -- OR it has
    //                          published but not even called arm_park() yet,
    //                          in which case state is still Running and this
    //                          is the mid-park window too)
    //   else (Blocked)      -> enqueue_locked
    // Routing unconditionally (not only when state==Blocked) is required: a
    // terminate landing in the window between a fiber publishing itself to an
    // object wait-list (state still Running) and its own arm_park()/
    // block_current() call would otherwise be recorded in terminateRequested
    // but never wake the fiber; if the object is never independently signaled
    // afterward, the fiber parks forever and TerminateThread's join_fiber()
    // hangs waiting for it to finish. A resulting spurious wake_pending on a
    // fiber that was never actually about to park is harmless: every blocking
    // wait in this codebase is a Mesa loop that re-checks its real condition
    // after waking (and, here, also checks ThreadInfo::terminated -- see
    // WaitSema/SleepThread/WaitEventFlag), and wake_pending is always consumed
    // exactly once by block_current() or by the executor's post-resume Blocked
    // handling.
    wake_locked(fc);
    g_sched_cv.notify_all();
}

void ps2sched::join_fiber(int tid)
{
    // Capture the target's identity as {tid, generation} once, under
    // g_sched_mutex, before waiting at all. This defends against tid
    // recycling (ABA): the target can finish, be erased from g_fiber_map by
    // the executor, and have `tid` reused by a brand-new, unrelated fiber
    // between our predicate evaluations below. Without the generation check,
    // a joiner whose predicate re-derives "the fiber currently at tid" on
    // every iteration would silently start waiting on (and re-applying the
    // join-priority floor to) the NEW fiber instead of detecting that its
    // original target is long gone -- an over-wait that can hang or block
    // far longer than intended. targetDone() below re-derives the *current*
    // occupant of tid every time and compares its generation against the one
    // captured here; a mismatch (or a missing entry) means the original
    // target is gone, full stop, regardless of what now lives at that tid.
    uint32_t targetGeneration = 0;
    {
        std::lock_guard<std::mutex> lk(g_sched_mutex);
        FiberContext* t = fiber_for(tid);
        if (!t) return; // already gone before we ever started waiting.
        targetGeneration = t->generation;
    }

    // Must be called with g_sched_mutex held. True once the ORIGINAL target
    // (identified by {tid, targetGeneration}) is no longer joinable: gone
    // from the map, tid recycled to a different fiber, or Finished.
    auto targetDone = [&]()
    {
        FiberContext* t = fiber_for(tid);
        return !t || t->generation != targetGeneration ||
               t->state == FiberContext::State::Finished;
    };

    // Non-fiber path: a host thread (test harness, RPC worker, etc.) called
    // TerminateThread for another tid. We cannot cooperatively yield, so just
    // wait on g_sched_cv until the target (by identity) is done.
    if (ps2fiber_current() == nullptr)
    {
        std::unique_lock<std::mutex> lk(g_sched_mutex);
        g_sched_cv.wait(lk, [&]() { return targetDone(); });
        return;
    }

    // Called from a fiber (TerminateThread of another tid). Cooperative: yield
    // until the target (by identity) is done.
    FiberContext* self = tls_current_fiber;

    while (true)
    {
        bool done = false;
        {
            std::lock_guard<std::mutex> lk(g_sched_mutex);
            FiberContext* t = fiber_for(tid);
            done = (!t || t->generation != targetGeneration ||
                    t->state == FiberContext::State::Finished);
            // Once `done` is true (including the generation-mismatch case),
            // the join-priority floor must stop being re-applied: t may now
            // be a completely unrelated fiber that happens to occupy tid, and
            // self's priority is restored below regardless.
            if (!done && self && t)
            {
                // Per-iteration floor: ensure self runs strictly AFTER the target
                // (higher priority number == lower scheduling priority). The
                // target's priority may change between iterations, so re-apply.
                const int floor = t->priority;
                if (self->priority < floor)
                {
                    if (!self->joinFloorActive)
                    {
                        // First lowering: remember the real priority to restore.
                        self->joinFloorActive = true;
                        self->joinSavedPriority = self->priority;
                    }
                    // self is the running fiber here, so it is not in the queue;
                    // just set the field for the next enqueue.
                    self->priority = floor;
                }
            }
        }
        if (done) break;
        ps2fiber_yield(); // let the executor run the target; it will finish
    }

    // Restore the joiner's real priority. joinSavedPriority reflects the latest
    // value written by any concurrent ChangeThreadPriority (update_priority keeps
    // it current while the floor is active), so we never lose a reprioritize.
    if (self)
    {
        std::lock_guard<std::mutex> lk(g_sched_mutex);
        if (self->joinFloorActive)
        {
            const int restore = self->joinSavedPriority;
            self->joinFloorActive = false;
            if (self->priority != restore)
            {
                // self is the currently running fiber and cannot be in the
                // run queue — assert defensively then update the priority.
                // It will be re-enqueued with the new priority at the next
                // cooperative yield.
                SCHED_REQUIRE(!self->in_run_queue,
                              "join_fiber: running fiber should not be in run queue");
                self->priority = restore;
            }
        }
    }
}

void ps2sched::update_priority(int tid, int newPriority)
{
    std::lock_guard<std::mutex> lk(g_sched_mutex);
    FiberContext* fc = fiber_for(tid);
    if (!fc) return;

    // If this fiber is currently inside join_fiber with a temporary priority floor
    // in effect, the game-requested priority must be recorded as the value to
    // restore on join exit, NOT written over the temporary floor. Otherwise either
    // join_fiber's restore would lose this change, or this write would let the
    // joiner outrun the target and spin the join.
    if (fc->joinFloorActive)
    {
        fc->joinSavedPriority = newPriority;
        return;
    }

    if (fc->priority == newPriority) return;
    bool wasQueued = fc->in_run_queue;
    if (wasQueued) remove_locked(fc);
    fc->priority = newPriority;
    if (wasQueued) enqueue_locked(fc);
    // If the target is the running fiber, its new priority takes effect at the next
    // yield_point (ChangeThreadPriority calls maybe_yield right after).
}

// ---------------------------------------------------------------------------
// arm_park — set state=Blocked and leaves any pending wakeup recorded by a
// waker intact (block_current consumes it). After this returns, the gates in
// make_ready / enqueue_external_wakeup_validated see state==Blocked and route through
// wake_locked, which (because the fiber is still g_running_fiber) records the
// wakeup in wake_pending. block_current() then observes wake_pending and does
// not park, so no wakeup is lost in the publish window.
// ---------------------------------------------------------------------------
void ps2sched::arm_park()
{
    FiberContext* fc = tls_current_fiber;
    if (fc == nullptr)
    {
        // Borrowed host worker. There is no fiber to arm; block_current() returns
        // a non-fiber result from this same thread. No-op here. (Callers gate this
        // call on ps2fiber_current() != nullptr, so reaching here from a non-fiber
        // should not happen; stay defensive.)
        return;
    }
    std::lock_guard<std::mutex> lk(g_sched_mutex);
    // Do NOT clear wake_pending here. arm_park may run AFTER the fiber has been
    // published to an object wait-list (see WaitSema), so a wakeup recorded by a
    // waker between publish and arm_park must survive. block_current() consumes
    // wake_pending; if none is pending it confirms Blocked.
    fc->state = FiberContext::State::Blocked;
}

// ---------------------------------------------------------------------------
// block_current — honour a wakeup that arrived in the arm/publish window.
// Returns a BlockResult enum describing the four possible outcomes.
// ---------------------------------------------------------------------------
ps2sched::BlockResult ps2sched::block_current()
{
    FiberContext* fc = tls_current_fiber;
    if (fc == nullptr)
    {
        // A borrowed IRQ/alarm/RPC worker (running guest code under
        // AsyncGuestScope) called a blocking syscall. We cannot park a host
        // worker on the fiber scheduler. Whether this worker actually holds the
        // guest token (and so must drop/reacquire it around its backoff pause)
        // is answered separately by holds_guest_token().
        return BlockResult::NonFiber;
    }
    bool throwTerminate = false;
    bool wokenInWindow  = false;
    {
        std::lock_guard<std::mutex> lk(g_sched_mutex);
        // Shutdown safety: a terminate-requested fiber must NEVER park again.
        // Throw ThreadExitException (outside the lock, below) so it unwinds the
        // fiber stack through any Mesa wait-loop. This handles the case where the
        // syscall's own info->terminated flag was not set by a caller that used
        // scheduler_shutdown() without notifyRuntimeStop(), which would otherwise
        // leave the fiber spinning forever on a count-0 semaphore.
        //
        // Deliberately gated on g_stop, NOT on terminateRequested alone (a
        // single-thread TerminateThread(otherTid) sets terminateRequested via
        // request_terminate() without setting g_stop). Every Mesa-loop caller
        // of block_current() (WaitSema, SleepThread/WakeupThread, WaitEventFlag,
        // ...) does its OWN mandatory bookkeeping cleanup -- removing itself
        // from an object's wait-list, decrementing a waiters count, etc. --
        // AFTER block_current() returns and BEFORE it checks
        // ThreadInfo::terminated and throws itself. If block_current() threw
        // directly here for a bare terminateRequested, that cleanup would be
        // skipped, leaving stale wait-list entries / wrong waiters counts on
        // an object that other, unrelated threads keep using afterward (e.g.
        // ReferSemaStatus's wait_threads count, or a future SignalSema's
        // wait-list scan). During a full g_stop shutdown this does not matter
        // (the runtime is tearing down and such objects are about to be
        // destroyed anyway), which is why the throw is safe -- and needed --
        // only in that case. request_terminate()'s unconditional wake_locked()
        // call (see its comment) already guarantees a plain terminateRequested
        // reaches every Mesa loop's own info->terminated check via a normal
        // wake, so no correctness gap remains from keeping this gated.
        if (g_stop.load(std::memory_order_relaxed) && fc->terminateRequested.load(std::memory_order_relaxed))
        {
            fc->wake_pending = false;
            fc->state = FiberContext::State::Running; // never parked
            throwTerminate = true;
        }
        // A waker may have fired between arm_park() and here (after the syscall
        // published to the object wait-list). wake_locked recorded it in
        // wake_pending. If so, consume it and DO NOT park.
        else if (fc->wake_pending)
        {
            fc->wake_pending = false;
            fc->state = FiberContext::State::Running; // we never actually parked
            wokenInWindow = true;
        }
        else
        {
            // No wakeup yet. Confirm Blocked (arm_park already set it; this is a
            // harmless re-affirmation and also covers callers that did not arm).
            fc->wake_pending = false;
            fc->state = FiberContext::State::Blocked;
        }
    }
    if (throwTerminate) throw ThreadExitException();
    if (wokenInWindow)  return BlockResult::WokenInWindow;
    ps2fiber_yield(); // executor sees Blocked; re-enqueues iff wake_pending.
    // Resumes here when the executor pops fc again (state set to Running by it).
    return BlockResult::Parked;
}

// True on a host (non-fiber) worker thread that currently holds the guest
// token. Always false on a fiber (fibers never call async_guest_begin — see
// on_guest_execution_slot's abort guard). Just reads tls_holds_guest_token,
// which async_guest_begin/async_guest_end set/clear under g_sched_mutex.
bool ps2sched::holds_guest_token()
{
    return tls_holds_guest_token;
}

// make_ready (suspendCount gate)
void ps2sched::make_ready(int tid)
{
    bool notify = false;
    {
        std::lock_guard<std::mutex> lk(g_sched_mutex);
        FiberContext* fc = fiber_for(tid);
        // No state==Blocked gate: a waker that fires in the publish/arm window
        // (state still Running, fiber still g_running_fiber) must reach
        // wake_locked so it can record wake_pending. wake_locked itself routes:
        //   in_run_queue        -> no-op (already queued)
        //   g_running_fiber==fc -> wake_pending=true (mid-park / publish window)
        //   else (Blocked)      -> enqueue_locked
        // The suspendCount gate stays: wake_locked does not check it, and a
        // suspended fiber must not be enqueued; clear_suspend wakes it when
        // the suspend is lifted.
        if (fc && fc->suspendCount.load(std::memory_order_relaxed) == 0)
        {
            wake_locked(fc);
            notify = true;
        }
    }
    if (notify) g_sched_cv.notify_all();
}

static inline uint64_t make_fiber_token(const FiberContext* fc)
{
    return (static_cast<uint64_t>(fc->generation) << 32) |
           static_cast<uint64_t>(static_cast<uint32_t>(fc->tid));
}

ps2sched::FiberToken ps2sched::current_fiber_token()
{
    FiberContext* fc = tls_current_fiber;
    return static_cast<FiberToken>(fc ? make_fiber_token(fc) : 0u);
}

::DispatchHistory& ps2sched::current_dispatch_history() noexcept
{
    if (FiberContext* fc = tls_current_fiber)
    {
        return fc->dispatchHistory;
    }
    // Persistent per-OS-thread instance: borrowed host workers, the executor
    // between fibers, and direct non-fiber callers all land here.
    thread_local ::DispatchHistory s_nonFiberHistory;
    return s_nonFiberHistory;
}

::SyscallOverrideStack& ps2sched::current_active_syscall_overrides() noexcept
{
    if (FiberContext* fc = tls_current_fiber)
    {
        return fc->syscallOverrides;
    }
    // Persistent per-OS-thread stack: each non-fiber caller (borrowed host
    // worker, direct non-fiber caller) runs its override chain to completion
    // without yielding, so a per-OS-thread stack is exactly right and still
    // catches genuine single-context self-recursion (the 0x83 case).
    thread_local ::SyscallOverrideStack s_nonFiberOverrideState;
    return s_nonFiberOverrideState;
}

void ps2sched::enqueue_external_wakeup_validated(int tid, FiberToken token)
{
    {
        std::lock_guard<std::mutex> lk(g_sched_mutex);
        FiberContext* fc = fiber_for(tid);
        // Identity check by {generation, tid} token, NOT by pointer: if `tid` was
        // recycled to a new fiber, that fiber has a different generation and the
        // token mismatches, so the stale wakeup is dropped. No state==Blocked gate:
        // WaitForNextVSyncTick has the same publish/arm window as WaitSema, so a
        // tick that fires while the fiber is still Running must reach wake_locked.
        if (fc != nullptr &&
            token != FiberToken{} &&
            make_fiber_token(fc) == static_cast<uint64_t>(token) &&
            fc->suspendCount.load(std::memory_order_relaxed) == 0)
        {
            wake_locked(fc);
        }
    }
    g_sched_cv.notify_all();
}

void ps2sched::maybe_yield()
{
    FiberContext* fc = tls_current_fiber;
    if (!fc) return; // called from a host worker: no-op
    bool yield = false;
    {
        std::lock_guard<std::mutex> lk(g_sched_mutex);
        if (g_run_queue && g_run_queue->priority < fc->priority)
        {
            enqueue_locked(fc); // enqueue Ready before yielding
            yield = true;
        }
    }
    if (yield) ps2fiber_yield();
}

void ps2sched::suspend_self()
{
    FiberContext* fc = tls_current_fiber;
    if (fc == nullptr)
    {
        // Borrowed host worker; cannot suspend a non-fiber. No-op.
        return;
    }
    fc->suspendCount.fetch_add(1, std::memory_order_relaxed);
    // Use the SAME arm_park()/block_current() protocol as every object wait
    // (WaitSema, WaitEventFlag, SleepThread, ...): a suspend CAN race a
    // concurrent waker. Thread.cpp's SuspendThread increments the PS2-visible
    // ThreadInfo::suspendCount and THEN calls suspend_self() as two separate
    // steps (they cannot be merged -- g_sched_mutex must never be nested
    // under a ThreadInfo::m, see SleepThread's "Drop info->m before ANY
    // scheduler operation" comment), so a concurrent host thread's
    // ResumeThread -> clear_suspend() can run in between. At that moment
    // fc->state is still Running (we have not reached this function yet), so
    // the resume cannot enqueue us; clear_suspend() calls wake_locked()
    // unconditionally, which records the cancellation in wake_pending
    // instead. arm_park() preserves wake_pending (does not clear it) and
    // block_current() consumes it, returning WokenInWindow without ever
    // parking. Net effect: SuspendThread behaves as suspend-immediately-
    // resumed, and fc->suspendCount is left exactly at the value
    // clear_suspend() set (its store(0) already ran before we got here), so
    // yield_point()'s suspendCount>0 gate will not spuriously re-block us
    // later on a count nobody is left to clear.
    //
    // If no such race occurs, arm_park()/block_current() park normally and
    // resume via the matching clear_suspend() wake.
    arm_park();
    block_current();
}

void ps2sched::suspend_other(int tid)
{
    std::lock_guard<std::mutex> lk(g_sched_mutex);
    FiberContext* fc = fiber_for(tid);
    if (!fc) return;
    fc->suspendCount.fetch_add(1, std::memory_order_relaxed);
    if (fc->state == FiberContext::State::Ready)
    {
        remove_locked(fc);                        // pull it out of the queue now
        fc->state = FiberContext::State::Blocked;
    }
    // If Blocked already: stays blocked (gate keeps it off-queue on wakeups).
    // Cannot be Running: only one fiber runs at a time (N=1 invariant).
}

void ps2sched::clear_suspend(int tid)
{
    bool notify = false;
    {
        std::lock_guard<std::mutex> lk(g_sched_mutex);
        FiberContext* fc = fiber_for(tid);
        if (!fc) return;
        fc->suspendCount.store(0, std::memory_order_relaxed);
        // Always route through wake_locked (not gated on state==Blocked).
        // See suspend_self() for the full race this closes: a clear_suspend()
        // that fires before the target has transitioned to Blocked (still
        // Running, mid its own ThreadInfo::suspendCount++ -> suspend_self()
        // window) must still be able to record wake_pending so
        // suspend_self()'s block_current() call observes it instead of
        // parking with no one left to wake it.
        wake_locked(fc); // race-safe enqueue / wake_pending / no-op
        notify = true;
    }
    if (notify) g_sched_cv.notify_all();
}

void ps2sched::rotate_ready_queue(int priority)
{
    std::lock_guard<std::mutex> lk(g_sched_mutex);
    FiberContext** pp = &g_run_queue;
    while (*pp && (*pp)->priority != priority)
        pp = &(*pp)->next;
    if (!*pp) return;
    FiberContext* victim = *pp; // first node at this priority
    *pp = victim->next;
    victim->next = nullptr;
    FiberContext** ins = pp; // re-insert after the last node at this priority
    while (*ins && (*ins)->priority == priority)
        ins = &(*ins)->next;
    victim->next = *ins;
    *ins = victim;
}

void ps2sched::async_guest_begin()
{
    if (on_guest_execution_slot())
    {
        std::fprintf(stderr,
                     "FATAL [ps2sched]: async_guest_begin from guest executor thread\n");
        std::terminate();
    }
    // Publish the waiter BEFORE taking the lock so the executor's canRun gate
    // (g_host_token_waiters == 0) observes it no later than its next predicate
    // evaluation, then park until the token is free.
    g_host_token_waiters.fetch_add(1, std::memory_order_relaxed);
    std::unique_lock<std::mutex> lk(g_sched_mutex);
    g_sched_cv.wait(lk, []
    {
        return g_running_fiber == nullptr && !g_guest_token_held_by_host;
    });
    g_guest_token_held_by_host = true;
    tls_holds_guest_token = true;
    // g_currentThreadId stays -1 on this host worker thread.
    g_host_token_waiters.fetch_sub(1, std::memory_order_relaxed);
    // Record the grant (under g_sched_mutex) so a deferring executor knows a
    // worker got its turn and may resume fibers again (see canRun).
    ++g_host_token_grants;
}

void ps2sched::async_guest_end()
{
    {
        std::lock_guard<std::mutex> lk(g_sched_mutex);
        // Only the worker that acquired the token may release it. A non-owner
        // reaching here means a blocking-syscall retry path called end() without
        // owning the token — a bug. Refuse to clear a token we do not own.
        SCHED_REQUIRE(g_guest_token_held_by_host,
                      "async_guest_end with token not held");
        SCHED_REQUIRE(tls_holds_guest_token,
                      "async_guest_end called by non-owner");
        g_guest_token_held_by_host = false;
        tls_holds_guest_token = false;
    }
    g_sched_cv.notify_all(); // wake the executor
}

bool ps2sched::yield_point()
{
    if ((++tls_backedge_counter & 127u) != 0u) return false; // cheap fast path

    FiberContext* fc = tls_current_fiber;
    if (!fc) return false; // running under a host worker (AsyncGuestScope): no preempt

    // 1. Terminate request -> unwind THIS fiber's own stack.
    if (fc->terminateRequested.load(std::memory_order_relaxed))
    {
        throw ThreadExitException(); // caught in fiber_trampoline
    }

    // 2. Suspend request -> block self.
    if (fc->suspendCount.load(std::memory_order_relaxed) > 0)
    {
        block_current();
        return false;
    }

    // 3. Higher-priority fiber ready -> cooperative yield (enqueue Ready first).
    // Same logic as the standalone maybe_yield(): fc here is the same current
    // fiber maybe_yield() would reload from tls_current_fiber, and it is
    // already known non-null above, so behavior is identical.
    maybe_yield();

    // 4. A host worker is parked in async_guest_begin() waiting for the guest
    // token -> cooperatively yield so it can run. The executor-side half of
    // this handoff is the g_host_token_waiters deferral in canRun; this is
    // the fiber-side half: a fiber that never blocks would otherwise keep
    // the executor inside ps2fiber_resume forever, and the executor gate
    // alone can only act BETWEEN resumes. The executor's post-resume code
    // re-enqueues us (state == Running), so no self-enqueue is needed.
    if (fc && g_host_token_waiters.load(std::memory_order_relaxed) > 0)
    {
        ps2fiber_yield();
        // Re-check terminate after resuming: scheduler_shutdown() may have
        // fired while the worker held the token.
        if (fc->terminateRequested.load(std::memory_order_relaxed))
        {
            throw ThreadExitException(); // caught in fiber_trampoline
        }
        return true;
    }
    return false;
}

int ps2sched::host_token_waiters()
{
    return g_host_token_waiters.load(std::memory_order_relaxed);
}

// True on any guest execution context: the executor thread (all backends),
// or a fiber's own OS thread on the pthread backend (set in fiber_trampoline).
// Shares on_guest_execution_slot() with the async_guest_begin() abort guard,
// so the two can never drift apart. Callers use this to decide whether they
// already own the guest execution slot or must borrow the token / avoid
// joining workers that wait on guest context.
bool ps2sched::is_guest_thread()
{
    return on_guest_execution_slot();
}

// Declared in ps2_fiber.h. Lets the fiber backend assert it only switches
// contexts on the registered executor thread without exposing the thread id.
bool ps2fiber_on_executor_thread()
{
    return tls_is_executor_thread;
}
