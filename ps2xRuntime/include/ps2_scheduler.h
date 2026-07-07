#ifndef PS2_SCHEDULER_H
#define PS2_SCHEDULER_H

// ---------------------------------------------------------------------------
// ps2_scheduler.h — public API for the N=1 fiber cooperative scheduler.
//
// Exactly one dedicated host OS thread (g_guest_thread, the "guest executor")
// runs all guest fibers. This eliminates cross-thread swapcontext UB structurally.
//
// No <semaphore>, <thread>, <mutex>, or other heavy headers are included here;
// this keeps the header safe for Vita (VitaSDK lacks C++20 <semaphore>) and
// keeps compile times low.
// ---------------------------------------------------------------------------

#include <cstdint>

// Guest thread identity. -1 means this host thread is not currently running a fiber.
// Defined in ps2_scheduler.cpp; extern-declared here and in State.h.
extern thread_local int g_currentThreadId;

class PS2Runtime;
struct DispatchHistory;               // defined in ps2_dispatch_history.h
struct SyscallOverrideStack;          // defined in ps2_syscall_override_state.h

namespace ps2sched
{
    // Opaque identity token for a fiber, encoding its generation + tid (see
    // current_fiber_token()). A distinct type (rather than a bare uint64_t)
    // keeps a raw fiber-token bit pattern from being silently accepted wherever
    // a uint64_t is expected. Only ever compared for equality, default-
    // constructed (FiberToken{} — the "no fiber" sentinel), or passed back to
    // enqueue_external_wakeup_validated. Code that genuinely needs the
    // underlying bits (e.g. constructing/validating generation+tid) casts
    // explicitly; the bit layout and validation logic are unchanged.
    enum class FiberToken : uint64_t {};

    // --- Lifecycle ---

    // Initialise global scheduler state and create the single guest executor
    // thread. Call once before create_fiber().
    void scheduler_init();

    // Signal all fibers to terminate, join the guest executor thread, and free
    // all fibers. Safe to call from the main OS thread after the render loop exits.
    void scheduler_shutdown();

    // Register a callback that scheduler_shutdown() invokes once, just before
    // joining the guest executor thread. The runtime sets this to request a
    // stop so dispatchLoop's while(!isStopRequested()) exits between dispatched
    // functions. `ctx` is passed back to `fn` unmodified (letting the callback
    // carry its own context instead of relying on a file-static). Pass
    // fn==nullptr to clear (ctx is ignored in that case). May be called before
    // scheduler_init().
    void scheduler_set_stop_callback(void (*fn)(void*), void* ctx);

    // --- Thread lifecycle (called from Thread.cpp / Lifecycle.cpp) ---

    // Create a fiber for guest thread `tid` with the given CPU registers and
    // enqueue it as Ready. May THROW std::bad_alloc / std::runtime_error on
    // stack or fiber allocation failure. StartThread catches the throw and reports allocation failure.
    void create_fiber(int tid, int priority, uint32_t entry,
                      uint32_t sp, uint32_t gp, uint32_t arg,
                      PS2Runtime* rt, uint8_t* rdram);

    // Set terminateRequested on tid's fiber and wake it if blocked.
    void request_terminate(int tid);

    // Cooperatively wait (yielding) until tid's fiber has finished.
    // Must be called from a fiber (not from a host thread).
    void join_fiber(int tid);

    // Reorder the fiber in the run queue with its new priority.
    void update_priority(int tid, int newPriority);

    // --- Blocking / readiness (Sync.cpp, Thread.cpp, Interrupt.cpp) ---

    // Arm the park BEFORE publishing to an object wait-list. Sets the running
    // fiber's scheduler state to Blocked and clears wake_pending under
    // g_sched_mutex, so a waker that fires after the wait-list publish but before
    // block_current() sees state==Blocked and routes through wake_locked (setting
    // wake_pending) instead of dropping the wakeup. Acts on the fiber currently
    // running on the executor thread (tls_current_fiber); callers must only invoke
    // it from a fiber. No-op if called with no current fiber.
    void arm_park();

    // Result of block_current(). See block_current() below.
    enum class BlockResult
    {
        Parked,        // a fiber actually parked and was later resumed by a waker.
        WokenInWindow, // a fiber: a wakeup arrived in the arm/publish window; do
                       //   not park. Caller re-checks its wait condition.
        NonFiber       // a borrowed host worker (not a fiber): caller cannot park
                       //   on the fiber scheduler and must yield the host thread
                       //   and re-check. Whether it must also drop/reacquire the
                       //   guest token around that yield is a SEPARATE question,
                       //   answered by holds_guest_token() (below), not by this
                       //   result.
    };

    // Park the currently-running fiber. Caller MUST have called arm_park() first
    // and then published itself to the object wait-list. The fiber must release any
    // object mutexes BEFORE calling this. See BlockResult for the three outcomes.
    BlockResult block_current();

    // True iff the calling OS thread is a borrowed host worker that currently
    // holds the guest token (acquired via async_guest_begin(), not yet released
    // via async_guest_end()). Always false on a fiber. Callers that get
    // BlockResult::NonFiber from block_current() use this to decide whether they
    // must drop/reacquire the guest token around their backoff pause — dropping
    // a token this thread does not hold would be a bug.
    bool holds_guest_token();

    // Wake a blocked fiber from within another fiber (from a guest thread).
    // Gated on suspendCount == 0.
    void make_ready(int tid);

    // Wake a blocked fiber from a non-fiber host thread, but ONLY if the fiber
    // currently mapped to `tid` is still the exact fiber identified by `token`
    // (from current_fiber_token()). The token encodes the fiber's generation, so
    // a recycled tid whose new fiber has a different generation will not match
    // and the stale wakeup is dropped. Validation happens under g_sched_mutex.
    void enqueue_external_wakeup_validated(int tid, FiberToken token);

    // Opaque identity token for the fiber currently running on this thread, or
    // FiberToken{} if not on a fiber. Encodes generation + tid. Only ever
    // compared for equality / passed back to enqueue_external_wakeup_validated.
    FiberToken current_fiber_token();

    // Yield if a higher-priority fiber is ready. Enqueues self Ready BEFORE
    // yielding, so the fiber is never 'Running but off-queue'.
    void maybe_yield();

    // Suspend the current fiber (SuspendThread on self). Increments suspendCount.
    void suspend_self();

    // Suspend another fiber (SuspendThread on other). Increments suspendCount.
    void suspend_other(int tid);

    // Force the fiber's suspendCount to 0 and wake it if Blocked. Called by
    // ResumeThread when the PS2-visible ThreadInfo::suspendCount reaches 0, so
    // nested SuspendThread calls resolve in a single resume.
    void clear_suspend(int tid);

    // Rotate the equal-priority group in the run queue (RotateThreadReadyQueue).
    void rotate_ready_queue(int priority);

    // --- Async guest-code borrow ---
    // (called by IRQ worker / alarm worker / RPC worker host threads)
    // Use the AsyncGuestScope RAII guard below instead of calling these
    // directly. MUST NOT be called from a fiber.

    // Acquire the "guest token": blocks until no fiber is executing guest code.
    void async_guest_begin();

    // Release the guest token.
    void async_guest_end();

    // --- Back-edge hook — called by PS2Runtime::shouldPreemptGuestExecution() ---

    // Sampled every 128 back-edges. Checks terminateRequested, suspendCount,
    // priority, and parked host workers; may ps2fiber_yield internally.
    // Returns true iff it yielded to a parked host worker (step 4), false
    // otherwise.
    bool yield_point();

    // Number of host workers currently blocked in async_guest_begin() waiting
    // for the guest token. The executor's resume predicate is gated on this
    // (it must genuinely sleep — releasing the scheduler mutex — while a
    // worker is parked, or the worker can never win the token).
    int host_token_waiters();

    // True iff the calling OS thread is a guest execution context: the single
    // guest executor thread (which runs all fibers on the ucontext backend),
    // or a fiber's own OS thread on the pthread backend (where each fiber owns
    // the execution slot whenever it runs). Lets shared helpers that are
    // reachable BOTH from host workers (which must borrow the guest token via
    // AsyncGuestScope) AND synchronously from already-running guest code
    // (which owns the execution slot implicitly and must NOT call
    // async_guest_begin — it aborts by design there) pick the correct
    // behavior. Also used by requestStop paths: joining a host worker from
    // guest context can deadlock (the worker may be parked in
    // async_guest_begin waiting for the very fiber that is joining).
    bool is_guest_thread();

    // Diagnostic dispatch-trace ring owned by the fiber currently running on
    // this thread. Never null: returns the fiber's own ring when a fiber runs
    // here, otherwise a persistent per-OS-thread instance (borrowed host
    // worker, executor between fibers, or a direct non-fiber caller).
    // Discriminates on tls_current_fiber, the same identity used by current_fiber_token().
    ::DispatchHistory& current_dispatch_history() noexcept;

    // Stack of syscall numbers whose overrides are currently running on the
    // fiber executing on this thread. Never null: returns the fiber's own
    // stack when a fiber runs here, otherwise a persistent per-OS-thread
    // stack (borrowed host worker, executor between fibers, or a direct
    // non-fiber caller), which is what the syscall layer uses to catch
    // single-context self-recursion.
    // Discriminates on tls_current_fiber, same identity as current_dispatch_history().
    ::SyscallOverrideStack& current_active_syscall_overrides() noexcept;

} // namespace ps2sched

// RAII guard that ensures async_guest_begin/async_guest_end are always paired,
// even on exception paths. Use this in IRQ/DMAC/alarm workers instead of bare calls.
struct AsyncGuestScope
{
    AsyncGuestScope()  { ps2sched::async_guest_begin(); }
    ~AsyncGuestScope() { ps2sched::async_guest_end();   }
    AsyncGuestScope(const AsyncGuestScope&) = delete;
    AsyncGuestScope& operator=(const AsyncGuestScope&) = delete;
};

#endif // PS2_SCHEDULER_H
