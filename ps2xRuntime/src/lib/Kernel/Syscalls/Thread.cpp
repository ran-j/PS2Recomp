#include "Common.h"
#include "Thread.h"
#include "ps2_scheduler_internal.h"

namespace ps2_syscalls
{
    static void applySuspendStatusLocked(ThreadInfo &info)
    {
        if (info.waitType != TSW_NONE)
        {
            info.status = THS_WAITSUSPEND;
        }
        else
        {
            info.status = THS_SUSPEND;
        }
    }

    static void runExitHandlersForThread(int tid, uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (!runtime || !ctx)
            return;

        std::vector<ExitHandlerEntry> handlers;
        {
            std::lock_guard<std::mutex> lock(g_exit_handler_mutex);
            auto it = g_exit_handlers.find(tid);
            if (it == g_exit_handlers.end())
                return;
            handlers = std::move(it->second);
            g_exit_handlers.erase(it);
        }

        for (const auto &handler : handlers)
        {
            if (!handler.func)
                continue;
            try
            {
                rpcInvokeFunction(rdram, ctx, runtime, handler.func, handler.arg, 0, 0, 0, nullptr);
            }
            catch (const ThreadExitException &)
            {
                // ignore
            }
            catch (const std::exception &)
            {
            }
        }
    }

    // -----------------------------------------------------------------------
    // on_fiber_exit — called by fiber_trampoline (via g_fiber_exit_hook)
    // after dispatchLoop returns.  Runs exit handlers and resets ThreadInfo.
    // -----------------------------------------------------------------------
    static void on_fiber_exit(int tid, uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime)
    {
        auto info = lookupThreadInfo(tid);

        runExitHandlersForThread(tid, rdram, ctx, runtime);

        uint32_t detachedAutoStack = 0;
        if (info) {
            std::lock_guard<std::mutex> lock(info->m);
            info->started = false;
            info->status = THS_DORMANT;
            info->waitType = TSW_NONE;
            info->waitId = 0;
            info->wakeupCount = 0;
            info->suspendCount = 0;
            info->forceRelease = false;
            info->terminated = false;
        }

        bool stillRegistered = false;
        {
            std::lock_guard<std::mutex> lock(g_thread_map_mutex);
            stillRegistered = (g_threads.find(tid) != g_threads.end());
        }
        if (!stillRegistered && info) {
            std::lock_guard<std::mutex> lock(info->m);
            if (info->ownsStack && info->stack != 0) {
                detachedAutoStack = info->stack;
                info->stack = 0;
                info->stackSize = 0;
                info->ownsStack = false;
            }
        }
        if (detachedAutoStack != 0 && runtime) {
            runtime->guestFree(detachedAutoStack);
        }

        // Consume this thread's active-thread token exactly once. If `info` is
        // null the g_threads entry was already removed by whoever also took the
        // token (notifyRuntimeStop reaping residual guest threads, or
        // ExitDeleteThread erasing its own entry), so this exit must NOT
        // decrement again. When `info` is present consumeActiveToken's atomic
        // exchange is the sole arbiter: a concurrent notifyRuntimeStop() holding
        // the same shared_ptr races here and exactly one side wins the
        // true->false transition and performs the single fetch_sub.
        consumeActiveToken(info);
    }

    static std::once_flag s_fiber_exit_hook_once;
    static void ensureFiberExitHookRegistered() {
        std::call_once(s_fiber_exit_hook_once, [](){
            g_fiber_exit_hook = on_fiber_exit;
        });
    }

    void FlushCache(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, KE_OK);
    }

    void iFlushCache(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        FlushCache(rdram, ctx, runtime);
    }

    void EnableCache(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, KE_OK);
    }

    void DisableCache(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, KE_OK);
    }

    void ResetEE(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        std::cerr << "Syscall: ResetEE - requesting runtime stop" << std::endl;
        // runtime->requestStop();
        setReturnS32(ctx, KE_OK);
    }

    void SetMemoryMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, KE_OK);
    }

    void InitThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        // This is a common ps2sdk helper that some games link against.
        setReturnS32(ctx, 1);
    }

    void CreateThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t paramAddr = getRegU32(ctx, 4); // $a0 points to ThreadParam
        if (paramAddr == 0u)
        {
            std::cerr << "CreateThread error: null ThreadParam pointer" << std::endl;
            setReturnS32(ctx, KE_ERROR);
            return;
        }

        const uint32_t *param = reinterpret_cast<const uint32_t *>(getConstMemPtr(rdram, paramAddr));

        if (!param)
        {
            std::cerr << "CreateThread error: invalid ThreadParam address 0x" << std::hex << paramAddr << std::dec << std::endl;
            setReturnS32(ctx, KE_ERROR);
            return;
        }

        auto info = std::make_shared<ThreadInfo>();
        info->attr = param[0];
        info->entry = param[1];
        info->stack = param[2];
        info->stackSize = param[3];

        auto looksLikeGuestPtr = [](uint32_t v) -> bool
        {
            if (v == 0)
            {
                return true;
            }
            const uint32_t norm = v & 0x1FFFFFFFu;
            return norm < PS2_RAM_SIZE && norm >= 0x10000u;
        };

        auto looksLikePriority = [](uint32_t v) -> bool
        {
            // Typical EE priorities are very small integers (1..127).
            return v <= 0x400u;
        };

        const uint32_t gpA = param[4];
        const uint32_t prioA = param[5];
        const uint32_t gpB = param[5];
        const uint32_t prioB = param[4];

        // Prefer the standard EE layout (gp at +0x10, priority at +0x14),
        // but keep a fallback for callsites that used the swapped decode.
        if (looksLikeGuestPtr(gpA) && looksLikePriority(prioA))
        {
            info->gp = gpA;
            info->priority = prioA;
        }
        else if (looksLikeGuestPtr(gpB) && looksLikePriority(prioB))
        {
            info->gp = gpB;
            info->priority = prioB;
        }
        else
        {
            info->gp = gpA;
            info->priority = prioA;
        }

        info->option = param[6];
        if (info->priority == 0)
        {
            info->priority = 1;
        }
        if (info->priority >= 128)
        {
            info->priority = 127;
        }
        info->currentPriority = static_cast<int>(info->priority);

        int id = 0;
        {
            std::lock_guard<std::mutex> lock(g_thread_map_mutex);
            // Keep IDs in the classic low range used by patched libkernel helpers.
            for (int attempts = 0; attempts < 0xFE; ++attempts)
            {
                if (g_nextThreadId < 2 || g_nextThreadId > 0xFF)
                {
                    g_nextThreadId = 2;
                }

                const int candidate = g_nextThreadId;
                g_nextThreadId = (g_nextThreadId >= 0xFF) ? 2 : (g_nextThreadId + 1);

                if (g_threads.find(candidate) == g_threads.end())
                {
                    id = candidate;
                    break;
                }
            }

            if (id == 0)
            {
                setReturnS32(ctx, KE_ERROR);
                return;
            }

            g_threads[id] = info;
        }

        setReturnS32(ctx, id);
    }

    void DeleteThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int tid = static_cast<int>(getRegU32(ctx, 4)); // $a0
        if (tid == 0)
        {
            setReturnS32(ctx, KE_ILLEGAL_THID);
            return;
        }

        auto info = lookupThreadInfo(tid);
        if (!info)
        {
            setReturnS32(ctx, KE_UNKNOWN_THID);
            return;
        }

        uint32_t autoStackToFree = 0;
        {
            std::lock_guard<std::mutex> lock(info->m);
            if (info->started || info->status != THS_DORMANT)
            {
                setReturnS32(ctx, KE_NOT_DORMANT);
                return;
            }

            if (info->ownsStack && info->stack != 0)
            {
                autoStackToFree = info->stack;
                info->stack = 0;
                info->stackSize = 0;
                info->ownsStack = false;
            }
        }

        {
            std::lock_guard<std::mutex> lock(g_thread_map_mutex);
            g_threads.erase(tid);
        }

        {
            std::lock_guard<std::mutex> lock(g_exit_handler_mutex);
            g_exit_handlers.erase(tid);
        }

        if (runtime && autoStackToFree != 0)
        {
            runtime->guestFree(autoStackToFree);
        }

        setReturnS32(ctx, KE_OK);
    }

    void StartThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int tid = static_cast<int>(getRegU32(ctx, 4)); // $a0 = thread id
        uint32_t arg = getRegU32(ctx, 5);              // $a1 = user arg
        if (tid == 0)
        {
            setReturnS32(ctx, KE_ILLEGAL_THID);
            return;
        }

        auto info = lookupThreadInfo(tid);
        if (!info)
        {
            std::cerr << "StartThread error: unknown thread id " << tid << std::endl;
            setReturnS32(ctx, KE_UNKNOWN_THID);
            return;
        }

        if (!runtime || !runtime->hasFunction(info->entry))
        {
            std::cerr << "[StartThread] entry 0x" << std::hex << info->entry << std::dec
                      << " is not registered" << std::endl;
            setReturnS32(ctx, KE_ERROR);
            return;
        }
        if (runtime->isStopRequested())
        {
            setReturnS32(ctx, KE_ERROR);
            return;
        }

        const uint32_t callerSp = getRegU32(ctx, 29);
        const uint32_t callerGp = getRegU32(ctx, 28);

        {
            std::lock_guard<std::mutex> lock(info->m);
            if (info->started || info->status != THS_DORMANT)
            {
                setReturnS32(ctx, KE_NOT_DORMANT);
                return;
            }

            info->started = true;
            info->status = THS_READY;
            info->arg = arg;
            info->terminated = false;
            info->forceRelease = false;
            info->waitType = TSW_NONE;
            info->waitId = 0;
            info->wakeupCount = 0;
            info->suspendCount = 0;
            if (info->stack == 0 && info->stackSize != 0)
            {
                const uint32_t autoStack = runtime->guestMalloc(info->stackSize, 16u);
                if (autoStack != 0)
                {
                    info->stack = autoStack;
                    info->ownsStack = true;
                }
            }
            if (info->stack != 0 && info->stackSize == 0)
            {
                info->stackSize = 0x800u;
            }
        }

        uint32_t threadSp = callerSp;
        if (info->stack)
        {
            const uint32_t stackSize = (info->stackSize != 0) ? info->stackSize : 0x800u;
            threadSp = (info->stack + stackSize) & ~0xFu;
        }
        uint32_t threadGp = info->gp;
        const uint32_t normalizedGp = threadGp & 0x1FFFFFFFu;
        if (threadGp == 0 || normalizedGp < 0x10000u || normalizedGp >= PS2_RAM_SIZE)
        {
            threadGp = callerGp;
        }

        ensureFiberExitHookRegistered();
        // Mint this thread's active-thread token (see mintActiveToken for the
        // memory-ordering rationale).
        mintActiveToken(info);

        // Create the fiber and enqueue it Ready. Throws on allocation failure or if the scheduler is shutting down; the catch below reports it as KE_NO_MEMORY.
        try
        {
            ps2sched::create_fiber(tid,
                                   info->currentPriority > 0 ? info->currentPriority
                                                             : static_cast<int>(info->priority),
                                   info->entry, threadSp, threadGp, arg, runtime, rdram);
        }
        catch (const std::exception& e)
        {
            // Undo the g_activeThreads increment and reset thread state. Consume
            // the token we just minted; consumeActiveToken's exchange guards
            // against a concurrent notifyRuntimeStop() that reaped this same
            // ThreadInfo.
            consumeActiveToken(info);
            {
                std::lock_guard<std::mutex> lock(info->m);
                info->started = false;
                info->status  = THS_DORMANT;
            }
            std::cerr << "[StartThread] create_fiber failed: " << e.what() << std::endl;
            setReturnS32(ctx, KE_NO_MEMORY);
            return;
        }

        // Update ThreadInfo status to READY now that the fiber is enqueued.
        // Guard against a race where TerminateThread ran between create_fiber
        // and here: if the thread was already transitioned to THS_DORMANT by
        // the terminate path, do not revert it to THS_READY.
        {
            std::lock_guard<std::mutex> lock(info->m);
            if (info->status != THS_DORMANT)
                info->status = THS_READY;
        }

        // Yield if the new thread has higher or equal priority.
        ps2sched::maybe_yield();
        setReturnS32(ctx, KE_OK);
    }

    void ExitThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        runExitHandlersForThread(g_currentThreadId, rdram, ctx, runtime);
        auto info = ensureCurrentThreadInfo(ctx);
        if (info)
        {
            std::lock_guard<std::mutex> lock(info->m);
            markSelfExitingLocked(*info);
        }
        throw ThreadExitException();
    }

    void ExitDeleteThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int tid = g_currentThreadId;
        runExitHandlersForThread(tid, rdram, ctx, runtime);
        auto info = ensureCurrentThreadInfo(ctx);
        if (info)
        {
            std::lock_guard<std::mutex> lock(info->m);
            markSelfExitingLocked(*info);
        }
        {
            std::lock_guard<std::mutex> lock(g_thread_map_mutex);
            g_threads.erase(tid);
        }
        // We just erased our own g_threads entry, so the on_fiber_exit that runs
        // after this throw will see a null ThreadInfo and skip the token consume.
        // Consume the active-thread token here instead, so g_activeThreads is
        // decremented exactly once for this started thread.
        consumeActiveToken(info);
        throw ThreadExitException();
    }

    void TerminateThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int tid = static_cast<int>(getRegU32(ctx, 4));
        auto info = resolveSelfOrThread(ctx, tid);
        if (!info) return;

        {
            std::lock_guard<std::mutex> lock(info->m);
            if (info->status == THS_DORMANT)
            {
                setReturnS32(ctx, KE_DORMANT);
                return;
            }
            info->terminated = true;
            info->forceRelease = true;
        }

        if (tid == g_currentThreadId)
        {
            runExitHandlersForThread(tid, rdram, ctx, runtime);
            throw ThreadExitException();
        }
        else
        {
            ps2sched::request_terminate(tid);
            ps2sched::join_fiber(tid);
        }

        setReturnS32(ctx, KE_OK);
    }

    void SuspendThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int tid = static_cast<int>(getRegU32(ctx, 4));
        auto info = resolveSelfOrThread(ctx, tid);
        if (!info) return;

        {
            std::lock_guard<std::mutex> lock(info->m);
            if (info->status == THS_DORMANT)
            {
                setReturnS32(ctx, KE_DORMANT);
                return;
            }
            info->suspendCount++;
            applySuspendStatusLocked(*info);
        }

        if (tid == g_currentThreadId)
        {
            // Drive the scheduler gate through fc->suspendCount via
            // suspend_self(), NOT block_current() directly. suspend_self()
            // increments FiberContext::suspendCount and parks the fiber; the
            // matching ResumeThread -> clear_suspend() zeroes it and re-
            // enqueues when it reaches 0. info->suspendCount (incremented above)
            // remains the PS2-visible count for status reporting.
            if (info->terminated.load()) throw ThreadExitException();
            ps2sched::suspend_self(); // parks until clear_suspend() wakes us
            if (info->terminated.load()) throw ThreadExitException();
            {
                std::lock_guard<std::mutex> lock(info->m);
                info->status = THS_RUN;
            }
        }
        else
        {
            ps2sched::suspend_other(tid);
        }

        setReturnS32(ctx, KE_OK);
    }

    void ResumeThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int tid = static_cast<int>(getRegU32(ctx, 4));
        auto info = resolveSelfOrThread(ctx, tid);
        if (!info) return;

        {
            std::lock_guard<std::mutex> lock(info->m);
            if (info->status == THS_DORMANT)
            {
                setReturnS32(ctx, KE_DORMANT);
                return;
            }
            if (info->suspendCount <= 0)
            {
                setReturnS32(ctx, KE_NOT_SUSPEND);
                return;
            }
            info->suspendCount--;
            if (info->suspendCount == 0)
            {
                if (info->waitType != TSW_NONE)
                {
                    info->status = THS_WAIT;
                }
                else
                {
                    info->status = (tid == g_currentThreadId) ? THS_RUN : THS_READY;
                }
            }
        }

        // ThreadInfo::suspendCount is the PS2-visible nesting count.
        // FiberContext::suspendCount is the scheduler parking gate. When the
        // PS2 count reaches 0 the thread must run again, so force the scheduler
        // gate to 0 in one shot (handles nested SuspendThread correctly).
        {
            int sc;
            {
                std::lock_guard<std::mutex> lock(info->m);
                sc = info->suspendCount;
            }
            if (sc == 0) {
                ps2sched::clear_suspend(tid); // fc->suspendCount = 0 + wake if Blocked
                ps2sched::maybe_yield();
            }
        }

        setReturnS32(ctx, KE_OK);
    }

    void GetThreadId(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, g_currentThreadId);
    }

    void ReferThreadStatus(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int tid = static_cast<int>(getRegU32(ctx, 4));
        uint32_t statusAddr = getRegU32(ctx, 5);

        auto info = resolveSelfOrThread(ctx, tid);
        if (!info) return;

        ee_thread_status_t *status = reinterpret_cast<ee_thread_status_t *>(getMemPtr(rdram, statusAddr));
        if (!status)
        {
            setReturnS32(ctx, KE_ERROR);
            return;
        }

        std::lock_guard<std::mutex> lock(info->m);
        status->status = info->status;
        status->func = info->entry;
        status->stack = info->stack;
        status->stack_size = info->stackSize;
        status->gp_reg = info->gp;
        status->initial_priority = info->priority;
        status->current_priority = info->currentPriority;
        status->attr = info->attr;
        status->option = info->option;
        status->waitType = info->waitType;
        status->waitId = info->waitId;
        status->wakeupCount = info->wakeupCount;
        setReturnS32(ctx, KE_OK);
    }

    void iReferThreadStatus(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ReferThreadStatus(rdram, ctx, runtime);
    }

    void SleepThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        // Guest identity is keyed off g_currentThreadId, NOT fiber-ness (see
        // WaitSema/WaitEventFlag in Sync.cpp for the full rationale). onFiber
        // remains the gate only for the genuinely scheduler-only parts below
        // (arm_park / the fiber wait loop's parking); a non-fiber thread
        // carrying a real guest tid (g_currentThreadId != -1) still gets
        // THS_WAIT bookkeeping via a ThreadInfo, so it is targetable by
        // WakeupThread/ReleaseWaitThread, and takes the bounded-backoff retry
        // loop below instead of parking, re-checking wakeupCount exactly like
        // the fiber path.
        const bool onFiber = (ps2fiber_current() != nullptr);
        std::shared_ptr<ThreadInfo> info =
            (g_currentThreadId != -1) ? ensureCurrentThreadInfo(ctx) : nullptr;
        if (onFiber && !info)
        {
            // A real fiber must have a ThreadInfo; failure to create one is a
            // genuine error.
            setReturnS32(ctx, KE_UNKNOWN_THID);
            return;
        }

        throwIfTerminated(info); // null-safe

        int ret = 0;

        if (!info)
        {
            // Fully borrowed host worker (g_currentThreadId == -1): PS2
            // interrupt context cannot sleep on a PS2 thread it does not own.
            // There is no ThreadInfo / wakeupCount to consult. Park-and-retry
            // once with bounded backoff, then return OK so the worker does
            // not livelock the emulator.
            ps2sched::block_current();
            nonFiberBlockBackoff();
            setReturnS32(ctx, 0);
            return;
        }

        std::unique_lock<std::mutex> lock(info->m);

        if (info->wakeupCount > 0)
        {
            info->wakeupCount--;
            info->status = THS_RUN;
            info->waitType = TSW_NONE;
            info->waitId = 0;
            ret = 0;
        }
        else
        {
            info->status = THS_WAIT;
            info->waitType = TSW_SLEEP;
            info->waitId = 0;
            info->forceRelease = false;

            NonFiberBackoff nfBackoff; // unused for fibers; ramps for non-fiber waiters

            for (;;)
            {
                // Drop info->m before ANY scheduler operation so g_sched_mutex is
                // never nested under info->m.
                lock.unlock();
                // Arm on every iteration: block_current() consumes wake_pending,
                // so a wake arriving in the new publish/arm window would be missed
                // if we skipped re-arming on subsequent iterations.
                //
                // Non-fiber (but identified) waiter: bounded exponential backoff
                // so a never-satisfied condition cannot busy-spin the CPU.
                const ps2sched::BlockResult br = nfBackoff.wait(onFiber);

                lock.lock();

                // 1. Terminate wins unconditionally (shutdown / TerminateThread).
                if (info->terminated.load())
                    throw ThreadExitException();

                // 2. ReleaseWaitThread forced us out of the wait.
                if (info->forceRelease.load())
                {
                    info->forceRelease = false;
                    ret = KE_RELEASE_WAIT;
                    break;
                }

                // 3. Genuine WakeupThread: a permit is available.
                if (info->wakeupCount > 0)
                {
                    --info->wakeupCount;
                    ret = 0;
                    break;
                }

                // 4. Spurious wake (e.g. ResumeThread / clear_suspend with no
                //    pending wakeup): stay asleep. Re-affirm wait state and loop.
                info->status = THS_WAIT;
                info->waitType = TSW_SLEEP;
                info->waitId = 0;
            }

            info->status = THS_RUN;
            info->waitType = TSW_NONE;
            info->waitId = 0;
        }

        lock.unlock();
        waitWhileSuspended(info);
        setReturnS32(ctx, ret);
    }

    void WakeupThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int tid = static_cast<int>(getRegU32(ctx, 4));
        if (tid == 0)
        {
            setReturnS32(ctx, KE_ILLEGAL_THID);
            return;
        }
        if (tid == g_currentThreadId)
        {
            setReturnS32(ctx, KE_ILLEGAL_THID);
            return;
        }

        auto info = lookupThreadInfo(tid);
        if (!info)
        {
            setReturnS32(ctx, KE_UNKNOWN_THID);
            return;
        }

        bool wasWaiting = false;
        {
            std::lock_guard<std::mutex> lock(info->m);
            if (info->status == THS_DORMANT)
            {
                setReturnS32(ctx, KE_DORMANT);
                return;
            }
            if (info->status == THS_WAIT && info->waitType == TSW_SLEEP)
            {
                wasWaiting = true;
                if (info->suspendCount > 0)
                {
                    info->status = THS_SUSPEND;
                }
                else
                {
                    info->status = THS_READY;
                }
                info->waitType = TSW_NONE;
                info->waitId = 0;
                info->wakeupCount++;
            }
            else
            {
                info->wakeupCount++;
            }
        }

        // If the thread was sleeping, make it ready and yield if higher priority.
        if (wasWaiting) {
            ps2sched::make_ready(tid);
            ps2sched::maybe_yield();
        }

        setReturnS32(ctx, KE_OK);
    }

    void iWakeupThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        WakeupThread(rdram, ctx, runtime);
    }

    void CancelWakeupThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int tid = static_cast<int>(getRegU32(ctx, 4));
        auto info = resolveSelfOrThread(ctx, tid);
        if (!info) return;

        int previous = 0;
        {
            std::lock_guard<std::mutex> lock(info->m);
            previous = info->wakeupCount;
            info->wakeupCount = 0;
        }
        setReturnS32(ctx, previous);
    }

    void iCancelWakeupThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int tid = static_cast<int>(getRegU32(ctx, 4));
        if (tid == 0)
        {
            setReturnS32(ctx, KE_ILLEGAL_THID);
            return;
        }

        auto info = lookupThreadInfo(tid);
        if (!info)
        {
            setReturnS32(ctx, KE_UNKNOWN_THID);
            return;
        }

        int previous = 0;
        {
            std::lock_guard<std::mutex> lock(info->m);
            previous = info->wakeupCount;
            info->wakeupCount = 0;
        }
        setReturnS32(ctx, previous);
    }

    void ChangeThreadPriority(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int tid = static_cast<int>(getRegU32(ctx, 4));
        int newPrio = static_cast<int>(getRegU32(ctx, 5));

        auto info = resolveSelfOrThread(ctx, tid);
        if (!info) return;

        {
            std::lock_guard<std::mutex> lock(info->m);
            if (info->status == THS_DORMANT)
            {
                setReturnS32(ctx, KE_DORMANT);
                return;
            }

            if (newPrio == 0)
            {
                newPrio = (info->currentPriority > 0) ? info->currentPriority : 1;
            }
            if (newPrio <= 0 || newPrio >= 128)
            {
                setReturnS32(ctx, KE_ILLEGAL_PRIORITY);
                return;
            }

            info->currentPriority = newPrio;
        }

        ps2sched::update_priority(tid, newPrio);
        ps2sched::maybe_yield();

        setReturnS32(ctx, KE_OK);
    }

    void iChangeThreadPriority(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ChangeThreadPriority(rdram, ctx, runtime);
    }

    void RotateThreadReadyQueue(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int prio = static_cast<int>(getRegU32(ctx, 4));
        if (prio == 0)
        {
            if (g_currentThreadId == -1)
            {
                setReturnS32(ctx, KE_ILLEGAL_THID);
                return;
            }
            auto current = ensureCurrentThreadInfo(ctx);
            if (current)
            {
                std::lock_guard<std::mutex> lock(current->m);
                prio = (current->currentPriority > 0) ? current->currentPriority : 1;
            }
        }
        if (prio <= 0 || prio >= 128)
        {
            setReturnS32(ctx, KE_ILLEGAL_PRIORITY);
            return;
        }

        // Rotate the equal-priority group in the fiber run queue.
        ps2sched::rotate_ready_queue(prio);
        ps2sched::maybe_yield();

        setReturnS32(ctx, KE_OK);
    }

    void iRotateThreadReadyQueue(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        RotateThreadReadyQueue(rdram, ctx, runtime);
    }

    void ReleaseWaitThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int tid = static_cast<int>(getRegU32(ctx, 4));
        if (tid == 0 || tid == g_currentThreadId)
        {
            setReturnS32(ctx, KE_ILLEGAL_THID);
            return;
        }

        auto info = lookupThreadInfo(tid);
        if (!info)
        {
            setReturnS32(ctx, KE_UNKNOWN_THID);
            return;
        }

        bool wasWaiting = false;

        {
            std::lock_guard<std::mutex> lock(info->m);
            if (info->status == THS_WAIT || info->status == THS_WAITSUSPEND)
            {
                wasWaiting = true;
                info->forceRelease = true;
                info->waitType = TSW_NONE;
                info->waitId = 0;
                if (info->suspendCount > 0)
                {
                    info->status = THS_SUSPEND;
                }
                else
                {
                    info->status = THS_READY;
                }
            }
        }

        if (!wasWaiting)
        {
            setReturnS32(ctx, KE_NOT_WAIT);
            return;
        }

        // Make the released thread ready and yield if it has higher priority.
        ps2sched::make_ready(tid);
        ps2sched::maybe_yield();

        setReturnS32(ctx, KE_OK);
    }

    void iReleaseWaitThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ReleaseWaitThread(rdram, ctx, runtime);
    }
}
