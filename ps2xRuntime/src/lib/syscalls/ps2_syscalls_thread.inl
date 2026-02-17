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

void FlushCache(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    setReturnS32(ctx, KE_OK);
}

void ResetEE(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    std::cerr << "Syscall: ResetEE - requesting runtime stop" << std::endl;
    runtime->requestStop();
    setReturnS32(ctx, KE_OK);
}

void SetMemoryMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    setReturnS32(ctx, KE_OK);
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
        id = g_nextThreadId++;
        if (g_nextThreadId <= 1)
        {
            g_nextThreadId = 2;
        }
        g_threads[id] = info;
    }

    std::cout << "[CreateThread] id=" << id
              << " entry=0x" << std::hex << info->entry
              << " stack=0x" << info->stack
              << " size=0x" << info->stackSize
              << " gp=0x" << info->gp
              << " prio=" << std::dec << info->priority << std::endl;

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
        if (info->status != THS_DORMANT)
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
        std::cerr << "[StartThread] entry 0x" << std::hex << info->entry << std::dec << " is not registered" << std::endl;
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
                std::cout << "[StartThread] id=" << tid
                          << " auto-stack=0x" << std::hex << autoStack
                          << " size=0x" << info->stackSize << std::dec << std::endl;
            }
        }

        if (info->stack != 0 && info->stackSize == 0)
        {
            // Some games leave size zero in the thread param even though a stack
            // buffer is supplied; use a conservative default instead of caller SP.
            info->stackSize = 0x800u;
        }
    }

    g_activeThreads.fetch_add(1, std::memory_order_relaxed);
    try
    {
        std::thread([=]() mutable
                           {
            {
                std::string name = "PS2Thread_" + std::to_string(tid);
                ThreadNaming::SetCurrentThreadName(name);
            }
            R5900Context threadCtxCopy{};
            R5900Context *threadCtx = &threadCtxCopy;

            {
                std::lock_guard<std::mutex> lock(info->m);
                info->status = THS_RUN;
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

            SET_GPR_U32(threadCtx, 29, threadSp);
            SET_GPR_U32(threadCtx, 28, threadGp);
            SET_GPR_U32(threadCtx, 4, info->arg);
            SET_GPR_U32(threadCtx, 31, 0);
            threadCtx->pc = info->entry;

            PS2Runtime::RecompiledFunction func = runtime->lookupFunction(info->entry);
            g_currentThreadId = tid;

            std::cout << "[StartThread] id=" << tid
                      << " entry=0x" << std::hex << info->entry
                      << " sp=0x" << GPR_U32(threadCtx, 29)
                      << " gp=0x" << GPR_U32(threadCtx, 28)
                      << " arg=0x" << info->arg << std::dec << std::endl;

            bool exited = false;
            try
            {
                func(rdram, threadCtx, runtime);
            }
            catch (const ThreadExitException &)
            {
                exited = true;
            }
            catch (const std::exception &e)
            {
                std::cerr << "[StartThread] id=" << tid << " exception: " << e.what() << std::endl;
            }

            if (!exited)
            {
                std::cout << "[StartThread] id=" << tid << " returned (pc=0x"
                          << std::hex << threadCtx->pc << std::dec << ")" << std::endl;
            }

            runExitHandlersForThread(tid, rdram, threadCtx, runtime);

            uint32_t detachedAutoStack = 0;
            {
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
            if (!stillRegistered)
            {
                // ExitDeleteThread removes the record immediately; reclaim auto stack here.
                std::lock_guard<std::mutex> lock(info->m);
                if (info->ownsStack && info->stack != 0)
                {
                    detachedAutoStack = info->stack;
                    info->stack = 0;
                    info->stackSize = 0;
                    info->ownsStack = false;
                }
            }

            if (detachedAutoStack != 0 && runtime)
            {
                runtime->guestFree(detachedAutoStack);
            }

            g_activeThreads.fetch_sub(1, std::memory_order_relaxed); })
            .detach();
    }
    catch (const std::exception &e)
    {
        std::cerr << "[StartThread] failed to spawn host thread for tid=" << tid << ": " << e.what() << std::endl;
        g_activeThreads.fetch_sub(1, std::memory_order_relaxed);
        std::lock_guard<std::mutex> lock(info->m);
        info->started = false;
        info->status = THS_DORMANT;
        info->waitType = TSW_NONE;
        info->waitId = 0;
        info->wakeupCount = 0;
        info->suspendCount = 0;
        info->forceRelease = false;
        info->terminated = false;
        setReturnS32(ctx, KE_ERROR);
        return;
    }

    setReturnS32(ctx, KE_OK);
}

void ExitThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    runExitHandlersForThread(g_currentThreadId, rdram, ctx, runtime);
    auto info = ensureCurrentThreadInfo(ctx);
    if (info)
    {
        std::lock_guard<std::mutex> lock(info->m);
        info->terminated = true;
        info->forceRelease = true;
        info->status = THS_DORMANT;
        info->waitType = TSW_NONE;
        info->waitId = 0;
        info->wakeupCount = 0;
    }
    if (info)
    {
        info->cv.notify_all();
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
        info->terminated = true;
        info->forceRelease = true;
        info->status = THS_DORMANT;
        info->waitType = TSW_NONE;
        info->waitId = 0;
        info->wakeupCount = 0;
    }
    if (info)
    {
        info->cv.notify_all();
    }
    {
        std::lock_guard<std::mutex> lock(g_thread_map_mutex);
        g_threads.erase(tid);
    }
    throw ThreadExitException();
}

void TerminateThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    int tid = static_cast<int>(getRegU32(ctx, 4));
    if (tid == 0)
        tid = g_currentThreadId;

    auto info = (tid == g_currentThreadId) ? ensureCurrentThreadInfo(ctx) : lookupThreadInfo(tid);
    if (!info)
    {
        setReturnS32(ctx, KE_UNKNOWN_THID);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(info->m);
        if (info->status == THS_DORMANT)
        {
            setReturnS32(ctx, KE_DORMANT);
            return;
        }
        info->terminated = true;
        info->forceRelease = true;
        info->status = THS_DORMANT;
        info->waitType = TSW_NONE;
        info->waitId = 0;
        info->wakeupCount = 0;
    }
    info->cv.notify_all();

    if (tid == g_currentThreadId)
    {
        runExitHandlersForThread(tid, rdram, ctx, runtime);
        throw ThreadExitException();
    }
    setReturnS32(ctx, KE_OK);
}

void SuspendThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    int tid = static_cast<int>(getRegU32(ctx, 4));
    if (tid == 0)
        tid = g_currentThreadId;

    auto info = (tid == g_currentThreadId) ? ensureCurrentThreadInfo(ctx) : lookupThreadInfo(tid);
    if (!info)
    {
        setReturnS32(ctx, KE_UNKNOWN_THID);
        return;
    }

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
    info->cv.notify_all();

    if (tid == g_currentThreadId)
    {
        std::unique_lock<std::mutex> lock(info->m);
        info->cv.wait(lock, [&]()
                      { return info->suspendCount == 0 || info->terminated.load(); });
        if (info->terminated.load())
        {
            throw ThreadExitException();
        }
        info->status = THS_RUN;
    }

    setReturnS32(ctx, KE_OK);
}

void ResumeThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    int tid = static_cast<int>(getRegU32(ctx, 4));
    if (tid == 0)
        tid = g_currentThreadId;

    auto info = (tid == g_currentThreadId) ? ensureCurrentThreadInfo(ctx) : lookupThreadInfo(tid);
    if (!info)
    {
        setReturnS32(ctx, KE_UNKNOWN_THID);
        return;
    }

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
    info->cv.notify_all();
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

    if (tid == 0) // TH_SELF
    {
        tid = g_currentThreadId;
    }

    auto info = (tid == g_currentThreadId) ? ensureCurrentThreadInfo(ctx) : lookupThreadInfo(tid);
    if (!info)
    {
        setReturnS32(ctx, KE_UNKNOWN_THID);
        return;
    }

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

void SleepThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    auto info = ensureCurrentThreadInfo(ctx);
    if (!info)
    {
        setReturnS32(ctx, KE_UNKNOWN_THID);
        return;
    }

    throwIfTerminated(info);

    int ret = 0;
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

        info->cv.wait(lock, [&]()
                      { return info->wakeupCount > 0 || info->forceRelease.load() || info->terminated.load(); });

        if (info->terminated.load())
        {
            throw ThreadExitException();
        }

        info->status = THS_RUN;
        info->waitType = TSW_NONE;
        info->waitId = 0;

        if (info->forceRelease.load())
        {
            info->forceRelease = false;
            ret = KE_RELEASE_WAIT;
        }
        else
        {
            if (info->wakeupCount > 0)
                info->wakeupCount--;
            ret = 0;
        }
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

    {
        std::lock_guard<std::mutex> lock(info->m);
        if (info->status == THS_DORMANT)
        {
            setReturnS32(ctx, KE_DORMANT);
            return;
        }
        if (info->status == THS_WAIT && info->waitType == TSW_SLEEP)
        {
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
            info->cv.notify_one();
        }
        else
        {
            info->wakeupCount++;
        }
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
    if (tid == 0)
        tid = g_currentThreadId;

    auto info = (tid == g_currentThreadId) ? ensureCurrentThreadInfo(ctx) : lookupThreadInfo(tid);
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

    if (tid == 0)
        tid = g_currentThreadId;

    auto info = (tid == g_currentThreadId) ? ensureCurrentThreadInfo(ctx) : lookupThreadInfo(tid);
    if (!info)
    {
        setReturnS32(ctx, KE_UNKNOWN_THID);
        return;
    }

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

    setReturnS32(ctx, KE_OK);
}

void RotateThreadReadyQueue(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    static int logCount = 0;
    int prio = static_cast<int>(getRegU32(ctx, 4));
    if (prio == 0)
    {
        auto current = ensureCurrentThreadInfo(ctx);
        if (current)
        {
            std::lock_guard<std::mutex> lock(current->m);
            prio = (current->currentPriority > 0) ? current->currentPriority : 1;
        }
    }
    if (logCount < 16)
    {
        std::cout << "[RotateThreadReadyQueue] prio=" << prio << std::endl;
        ++logCount;
    }
    if (prio <= 0 || prio >= 128)
    {
        setReturnS32(ctx, KE_ILLEGAL_PRIORITY);
        return;
    }
    setReturnS32(ctx, KE_OK);
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
    int waitType = 0;
    int waitId = 0;

    {
        std::lock_guard<std::mutex> lock(info->m);
        if (info->status == THS_WAIT || info->status == THS_WAITSUSPEND)
        {
            wasWaiting = true;
            waitType = info->waitType;
            waitId = info->waitId;
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

    info->cv.notify_all();

    if (waitType == TSW_SEMA)
    {
        auto sema = lookupSemaInfo(waitId);
        if (sema)
        {
            sema->cv.notify_all();
        }
    }
    else if (waitType == TSW_EVENT)
    {
        auto eventFlag = lookupEventFlagInfo(waitId);
        if (eventFlag)
        {
            eventFlag->cv.notify_all();
        }
    }
    setReturnS32(ctx, KE_OK);
}

void iReleaseWaitThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    ReleaseWaitThread(rdram, ctx, runtime);
}
