namespace
{
    constexpr uint32_t kIntcVblankStart = 2u;
    constexpr uint32_t kIntcVblankEnd = 3u;
    constexpr auto kVblankPeriod = std::chrono::microseconds(16667);
    constexpr int kMaxCatchupTicks = 4;

    struct VSyncFlagRegistration
    {
        uint32_t flagAddr = 0;
        uint32_t tickAddr = 0;
    };

    static std::mutex g_irq_handler_mutex;
    static std::mutex g_irq_worker_mutex;
    static std::mutex g_vsync_flag_mutex;
    static std::atomic<bool> g_irq_worker_stop{false};
    static std::atomic<bool> g_irq_worker_running{false};
    static uint32_t g_enabled_intc_mask = 0xFFFFFFFFu;
    static uint32_t g_enabled_dmac_mask = 0xFFFFFFFFu;
    static uint64_t g_vsync_tick_counter = 0u;
    static VSyncFlagRegistration g_vsync_registration{};
}

static void writeGuestU32NoThrow(uint8_t *rdram, uint32_t addr, uint32_t value)
{
    if (addr == 0u)
    {
        return;
    }

    uint8_t *dst = getMemPtr(rdram, addr);
    if (!dst)
    {
        return;
    }
    std::memcpy(dst, &value, sizeof(value));
}

static void writeGuestU64NoThrow(uint8_t *rdram, uint32_t addr, uint64_t value)
{
    if (addr == 0u)
    {
        return;
    }

    uint8_t *dst = getMemPtr(rdram, addr);
    if (!dst)
    {
        return;
    }
    std::memcpy(dst, &value, sizeof(value));
}

static void dispatchIntcHandlersForCause(uint8_t *rdram, PS2Runtime *runtime, uint32_t cause)
{
    if (!rdram || !runtime)
    {
        return;
    }

    std::vector<IrqHandlerInfo> handlers;
    {
        std::lock_guard<std::mutex> lock(g_irq_handler_mutex);
        if (cause < 32u && (g_enabled_intc_mask & (1u << cause)) == 0u)
        {
            return;
        }

        handlers.reserve(g_intcHandlers.size());
        for (const auto &[id, info] : g_intcHandlers)
        {
            (void)id;
            if (!info.enabled)
            {
                continue;
            }
            if (info.cause != cause)
            {
                continue;
            }
            if (info.handler == 0u)
            {
                continue;
            }
            handlers.push_back(info);
        }
    }

    for (const IrqHandlerInfo &info : handlers)
    {
        if (!runtime->hasFunction(info.handler))
        {
            continue;
        }

        try
        {
            R5900Context irqCtx{};
            const uint32_t sp = (info.sp != 0u) ? info.sp : (PS2_RAM_SIZE - 0x10u);
            SET_GPR_U32(&irqCtx, 28, info.gp);
            SET_GPR_U32(&irqCtx, 29, sp);
            SET_GPR_U32(&irqCtx, 31, 0u);
            SET_GPR_U32(&irqCtx, 4, cause);
            SET_GPR_U32(&irqCtx, 5, info.arg);
            SET_GPR_U32(&irqCtx, 6, 0u);
            SET_GPR_U32(&irqCtx, 7, 0u);
            irqCtx.pc = info.handler;

            PS2Runtime::RecompiledFunction func = runtime->lookupFunction(info.handler);
            func(rdram, &irqCtx, runtime);
        }
        catch (const ThreadExitException &)
        {
        }
        catch (const std::exception &e)
        {
            static uint32_t warnCount = 0;
            if (warnCount < 8u)
            {
                std::cerr << "[INTC] handler 0x" << std::hex << info.handler
                          << " threw exception: " << e.what() << std::dec << std::endl;
                ++warnCount;
            }
        }
    }
}

static void signalVSyncFlag(uint8_t *rdram, uint64_t tickValue)
{
    VSyncFlagRegistration reg{};
    {
        std::lock_guard<std::mutex> lock(g_vsync_flag_mutex);
        reg = g_vsync_registration;
        g_vsync_registration = {};
        g_vsync_tick_counter = tickValue;
    }

    if (reg.flagAddr != 0u)
    {
        writeGuestU32NoThrow(rdram, reg.flagAddr, 1u);
    }
    if (reg.tickAddr != 0u)
    {
        writeGuestU64NoThrow(rdram, reg.tickAddr, tickValue);
    }
}

static void interruptWorkerMain(uint8_t *rdram, PS2Runtime *runtime)
{
    using clock = std::chrono::steady_clock;
    auto nextTick = clock::now() + kVblankPeriod;

    while (!g_irq_worker_stop.load(std::memory_order_acquire) &&
           runtime != nullptr &&
           !runtime->isStopRequested())
    {
        std::this_thread::sleep_until(nextTick);

        const auto now = clock::now();
        int ticksToProcess = 0;
        while (now >= nextTick && ticksToProcess < kMaxCatchupTicks)
        {
            ++ticksToProcess;
            nextTick += kVblankPeriod;
        }
        if (ticksToProcess == 0)
        {
            continue;
        }

        for (int i = 0; i < ticksToProcess; ++i)
        {
            uint64_t tickValue = 0u;
            {
                std::lock_guard<std::mutex> lock(g_vsync_flag_mutex);
                tickValue = ++g_vsync_tick_counter;
            }

            signalVSyncFlag(rdram, tickValue);
            dispatchIntcHandlersForCause(rdram, runtime, kIntcVblankStart);
            dispatchIntcHandlersForCause(rdram, runtime, kIntcVblankEnd);
        }
    }

    g_irq_worker_running.store(false, std::memory_order_release);
}

static void ensureInterruptWorkerRunning(uint8_t *rdram, PS2Runtime *runtime)
{
    if (!rdram || !runtime)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(g_irq_worker_mutex);
    if (g_irq_worker_running.load(std::memory_order_acquire))
    {
        return;
    }

    g_irq_worker_stop.store(false, std::memory_order_release);
    g_irq_worker_running.store(true, std::memory_order_release);
    try
    {
        std::thread(interruptWorkerMain, rdram, runtime).detach();
    }
    catch (...)
    {
        g_irq_worker_running.store(false, std::memory_order_release);
    }
}

void stopInterruptWorker()
{
    g_irq_worker_stop.store(true, std::memory_order_release);
    for (int i = 0; i < 100 && g_irq_worker_running.load(std::memory_order_acquire); ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void SetVSyncFlag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t flagAddr = getRegU32(ctx, 4);
    const uint32_t tickAddr = getRegU32(ctx, 5);

    {
        std::lock_guard<std::mutex> lock(g_vsync_flag_mutex);
        g_vsync_registration.flagAddr = flagAddr;
        g_vsync_registration.tickAddr = tickAddr;
    }

    writeGuestU32NoThrow(rdram, flagAddr, 0u);
    writeGuestU64NoThrow(rdram, tickAddr, 0u);
    ensureInterruptWorkerRunning(rdram, runtime);
    setReturnS32(ctx, KE_OK);
}

void EnableIntc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t cause = getRegU32(ctx, 4);
    if (cause < 32u)
    {
        std::lock_guard<std::mutex> lock(g_irq_handler_mutex);
        g_enabled_intc_mask |= (1u << cause);
    }
    setReturnS32(ctx, KE_OK);
}

void DisableIntc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t cause = getRegU32(ctx, 4);
    if (cause < 32u)
    {
        std::lock_guard<std::mutex> lock(g_irq_handler_mutex);
        g_enabled_intc_mask &= ~(1u << cause);
    }
    setReturnS32(ctx, KE_OK);
}

void AddIntcHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    IrqHandlerInfo info{};
    info.cause = getRegU32(ctx, 4);
    info.handler = getRegU32(ctx, 5);
    info.arg = getRegU32(ctx, 7);
    info.gp = getRegU32(ctx, 28);
    info.sp = getRegU32(ctx, 29);
    info.enabled = true;

    int handlerId = 0;
    {
        std::lock_guard<std::mutex> lock(g_irq_handler_mutex);
        handlerId = g_nextIntcHandlerId++;
        g_intcHandlers[handlerId] = info;
    }

    ensureInterruptWorkerRunning(rdram, runtime);
    setReturnS32(ctx, handlerId);
}

void RemoveIntcHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const int handlerId = static_cast<int>(getRegU32(ctx, 5));
    if (handlerId > 0)
    {
        std::lock_guard<std::mutex> lock(g_irq_handler_mutex);
        g_intcHandlers.erase(handlerId);
    }
    setReturnS32(ctx, KE_OK);
}

void AddDmacHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    IrqHandlerInfo info{};
    info.cause = getRegU32(ctx, 4);
    info.handler = getRegU32(ctx, 5);
    info.arg = getRegU32(ctx, 7);
    info.gp = getRegU32(ctx, 28);
    info.sp = getRegU32(ctx, 29);
    info.enabled = true;

    int handlerId = 0;
    {
        std::lock_guard<std::mutex> lock(g_irq_handler_mutex);
        handlerId = g_nextDmacHandlerId++;
        g_dmacHandlers[handlerId] = info;
    }
    setReturnS32(ctx, handlerId);
}

void RemoveDmacHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const int handlerId = static_cast<int>(getRegU32(ctx, 5));
    if (handlerId > 0)
    {
        std::lock_guard<std::mutex> lock(g_irq_handler_mutex);
        g_dmacHandlers.erase(handlerId);
    }
    setReturnS32(ctx, KE_OK);
}

void EnableIntcHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const int handlerId = static_cast<int>(getRegU32(ctx, 5));
    {
        std::lock_guard<std::mutex> lock(g_irq_handler_mutex);
        if (auto it = g_intcHandlers.find(handlerId); it != g_intcHandlers.end())
        {
            it->second.enabled = true;
        }
    }
    setReturnS32(ctx, KE_OK);
}

void DisableIntcHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const int handlerId = static_cast<int>(getRegU32(ctx, 5));
    {
        std::lock_guard<std::mutex> lock(g_irq_handler_mutex);
        if (auto it = g_intcHandlers.find(handlerId); it != g_intcHandlers.end())
        {
            it->second.enabled = false;
        }
    }
    setReturnS32(ctx, KE_OK);
}

void EnableDmacHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const int handlerId = static_cast<int>(getRegU32(ctx, 5));
    {
        std::lock_guard<std::mutex> lock(g_irq_handler_mutex);
        if (auto it = g_dmacHandlers.find(handlerId); it != g_dmacHandlers.end())
        {
            it->second.enabled = true;
        }
    }
    setReturnS32(ctx, KE_OK);
}

void DisableDmacHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const int handlerId = static_cast<int>(getRegU32(ctx, 5));
    {
        std::lock_guard<std::mutex> lock(g_irq_handler_mutex);
        if (auto it = g_dmacHandlers.find(handlerId); it != g_dmacHandlers.end())
        {
            it->second.enabled = false;
        }
    }
    setReturnS32(ctx, KE_OK);
}

void EnableDmac(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t cause = getRegU32(ctx, 4);
    if (cause < 32u)
    {
        std::lock_guard<std::mutex> lock(g_irq_handler_mutex);
        g_enabled_dmac_mask |= (1u << cause);
    }
    setReturnS32(ctx, KE_OK);
}

void DisableDmac(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t cause = getRegU32(ctx, 4);
    if (cause < 32u)
    {
        std::lock_guard<std::mutex> lock(g_irq_handler_mutex);
        g_enabled_dmac_mask &= ~(1u << cause);
    }
    setReturnS32(ctx, KE_OK);
}
