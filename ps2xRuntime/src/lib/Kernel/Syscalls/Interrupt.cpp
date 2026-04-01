#include "Common.h"
#include "Interrupt.h"
#include "ps2_log.h"

namespace ps2_syscalls
{
namespace interrupt_state
{
    constexpr uint32_t kIntcVblankStart = 2u;
    constexpr uint32_t kIntcVblankEnd = 3u;
    constexpr auto kVblankPeriod = std::chrono::microseconds(16667);
    constexpr int kMaxCatchupTicks = 4;

    std::mutex g_irq_handler_mutex;
    std::mutex g_irq_worker_mutex;
    std::condition_variable g_irq_worker_cv;
    std::mutex g_vsync_flag_mutex;
    std::condition_variable g_vsync_cv;
    std::atomic<bool> g_irq_worker_stop{false};
    std::atomic<bool> g_irq_worker_running{false};
    uint32_t g_enabled_intc_mask = 0xFFFFFFFFu;
    uint32_t g_enabled_dmac_mask = 0xFFFFFFFFu;
    uint64_t g_vsync_tick_counter = 0u;
    VSyncFlagRegistration g_vsync_registration{};
}

using namespace interrupt_state;

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

static uint32_t readGuestU32NoThrow(uint8_t *rdram, uint32_t addr)
{
    if (addr == 0u)
    {
        return 0u;
    }

    uint8_t *src = getMemPtr(rdram, addr);
    if (!src)
    {
        return 0u;
    }

    uint32_t value = 0u;
    std::memcpy(&value, src, sizeof(value));
    return value;
}

static uint32_t getAsyncHandlerStackTop(PS2Runtime *runtime)
{
    constexpr uint32_t kAsyncHandlerStackSize = 0x4000u;
    thread_local PS2Runtime *s_cachedRuntime = nullptr;
    thread_local uint32_t s_cachedStackTop = 0u;

    if (runtime == nullptr)
    {
        return PS2_RAM_SIZE - 0x10u;
    }

    if (s_cachedRuntime != runtime || s_cachedStackTop == 0u)
    {
        s_cachedRuntime = runtime;
        s_cachedStackTop = runtime->reserveAsyncCallbackStack(kAsyncHandlerStackSize, 16u);
    }

    return (s_cachedStackTop != 0u) ? s_cachedStackTop : (PS2_RAM_SIZE - 0x10u);
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
        std::sort(handlers.begin(), handlers.end(), [](const IrqHandlerInfo &a, const IrqHandlerInfo &b)
                  { return a.order < b.order; });
    }

    for (const IrqHandlerInfo &info : handlers)
    {
        if (!runtime->hasFunction(info.handler))
        {
            if (cause == kIntcVblankStart)
            {
                PS2_IF_AGRESSIVE_LOGS({
                    static std::atomic<uint32_t> s_missingHandlerLogCount{0u};
                    const uint32_t logIndex = s_missingHandlerLogCount.fetch_add(1u, std::memory_order_relaxed);
                    if (logIndex < 32u)
                    {
                        auto flags = std::cout.flags();
                        std::cout << "[INTC:missing] cause=" << cause
                                  << " handler=0x" << std::hex << info.handler
                                  << std::dec
                                  << " id=" << info.id
                                  << std::endl;
                        std::cout.flags(flags);
                    }
                });
            }
            continue;
        }

        try
        {
            R5900Context irqCtx{};
            SET_GPR_U32(&irqCtx, 28, info.gp);
            SET_GPR_U32(&irqCtx, 29, getAsyncHandlerStackTop(runtime));
            SET_GPR_U32(&irqCtx, 31, 0u);
            SET_GPR_U32(&irqCtx, 4, cause);
            SET_GPR_U32(&irqCtx, 5, info.arg);
            SET_GPR_U32(&irqCtx, 6, 0u);
            SET_GPR_U32(&irqCtx, 7, 0u);
            irqCtx.pc = info.handler;

            while (irqCtx.pc != 0u && runtime && !runtime->isStopRequested())
            {
                PS2Runtime::RecompiledFunction step = runtime->lookupFunction(irqCtx.pc);
                if (!step)
                {
                    break;
                }
                // Interrupt handlers must be able to preempt a guest thread that is
                // spinning on interrupt-produced state, such as a vblank counter.
                step(rdram, &irqCtx, runtime);
            }

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

void dispatchDmacHandlersForCause(uint8_t *rdram, PS2Runtime *runtime, uint32_t cause)
{
    if (!rdram || !runtime)
    {
        return;
    }

    std::vector<IrqHandlerInfo> handlers;
    {
        std::lock_guard<std::mutex> lock(g_irq_handler_mutex);
        if (cause < 32u && (g_enabled_dmac_mask & (1u << cause)) == 0u)
        {
            return;
        }

        handlers.reserve(g_dmacHandlers.size());
        for (const auto &[id, info] : g_dmacHandlers)
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
        std::sort(handlers.begin(), handlers.end(), [](const IrqHandlerInfo &a, const IrqHandlerInfo &b)
                  { return a.order < b.order; });
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
            SET_GPR_U32(&irqCtx, 28, info.gp);
            SET_GPR_U32(&irqCtx, 29, getAsyncHandlerStackTop(runtime));
            SET_GPR_U32(&irqCtx, 31, 0u);
            SET_GPR_U32(&irqCtx, 4, cause);
            SET_GPR_U32(&irqCtx, 5, info.arg);
            SET_GPR_U32(&irqCtx, 6, 0u);
            SET_GPR_U32(&irqCtx, 7, 0u);
            irqCtx.pc = info.handler;

            while (irqCtx.pc != 0u && runtime && !runtime->isStopRequested())
            {
                PS2Runtime::RecompiledFunction step = runtime->lookupFunction(irqCtx.pc);
                if (!step)
                {
                    break;
                }
                step(rdram, &irqCtx, runtime);
            }
        }
        catch (const ThreadExitException &)
        {
        }
        catch (const std::exception &e)
        {
            static uint32_t warnCount = 0;
            if (warnCount < 8u)
            {
                std::cerr << "[DMAC] handler 0x" << std::hex << info.handler
                          << " threw exception: " << e.what() << std::dec << std::endl;
                ++warnCount;
            }
        }
    }
}

static uint64_t signalVSyncFlag(uint8_t *rdram)
{
    VSyncFlagRegistration reg{};
    uint64_t tickValue = 0u;
    {
        std::lock_guard<std::mutex> lock(g_vsync_flag_mutex);
        reg = g_vsync_registration;
        tickValue = ++g_vsync_tick_counter;
    }

    g_vsync_cv.notify_all();

    if (reg.flagAddr != 0u)
    {
        writeGuestU32NoThrow(rdram, reg.flagAddr, 1u);
    }
    if (reg.tickAddr != 0u)
    {
        writeGuestU64NoThrow(rdram, reg.tickAddr, tickValue);
    }
    return tickValue;
}

static void interruptWorkerMain(uint8_t *rdram, PS2Runtime *runtime)
{
    g_currentThreadId = -1;

    using clock = std::chrono::steady_clock;
    auto nextTick = clock::now() + kVblankPeriod;

    while (runtime != nullptr && !runtime->isStopRequested())
    {
        {
            std::unique_lock<std::mutex> lock(g_irq_worker_mutex);
            if (g_irq_worker_cv.wait_until(lock, nextTick, []()
                                           { return g_irq_worker_stop.load(std::memory_order_acquire); }))
            {
                break;
            }
        }

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
            const uint64_t tickValue = signalVSyncFlag(rdram);
            ps2_stubs::dispatchGsSyncVCallback(rdram, runtime, tickValue);
            dispatchIntcHandlersForCause(rdram, runtime, kIntcVblankStart);
            std::this_thread::sleep_for(std::chrono::microseconds(500));
            dispatchIntcHandlersForCause(rdram, runtime, kIntcVblankEnd);
        }
    }

    g_irq_worker_running.store(false, std::memory_order_release);
    g_irq_worker_cv.notify_all();
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

void EnsureVSyncWorkerRunning(uint8_t *rdram, PS2Runtime *runtime)
{
    ensureInterruptWorkerRunning(rdram, runtime);
}

uint64_t GetCurrentVSyncTick()
{
    std::lock_guard<std::mutex> lock(g_vsync_flag_mutex);
    return g_vsync_tick_counter;
}

void stopInterruptWorker()
{
    g_irq_worker_stop.store(true, std::memory_order_release);
    g_irq_worker_cv.notify_all();
    std::unique_lock<std::mutex> lock(g_irq_worker_mutex);
    g_irq_worker_cv.wait_for(lock, std::chrono::milliseconds(500), []()
                             { return !g_irq_worker_running.load(std::memory_order_acquire); });
    g_vsync_cv.notify_all();
}

uint64_t WaitForNextVSyncTick(uint8_t *rdram, PS2Runtime *runtime)
{
    ensureInterruptWorkerRunning(rdram, runtime);
    std::unique_lock<std::mutex> lock(g_vsync_flag_mutex);
    uint64_t current = g_vsync_tick_counter;
    {
        PS2Runtime::GuestExecutionReleaseScope releaseGuestExecution(runtime);
        g_vsync_cv.wait(lock, [current, runtime]()
                        { return g_vsync_tick_counter > current || (runtime != nullptr && runtime->isStopRequested()); });
    }
    return g_vsync_tick_counter;
}

void WaitVSyncTick(uint8_t *rdram, PS2Runtime *runtime)
{
    (void)WaitForNextVSyncTick(rdram, runtime);
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
    if (cause == kIntcVblankStart || cause == kIntcVblankEnd)
    {
        PS2_IF_AGRESSIVE_LOGS({
            static std::atomic<uint32_t> s_enableLogCount{0u};
            const uint32_t logIndex = s_enableLogCount.fetch_add(1u, std::memory_order_relaxed);
            if (logIndex < 32u)
            {
                RUNTIME_LOG("[EnableIntc] cause=" << cause);
            }
        });
    }
    setReturnS32(ctx, KE_OK);
}

void iEnableIntc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    EnableIntc(rdram, ctx, runtime);
}

void DisableIntc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t cause = getRegU32(ctx, 4);
    if (cause < 32u)
    {
        std::lock_guard<std::mutex> lock(g_irq_handler_mutex);
        g_enabled_intc_mask &= ~(1u << cause);
    }
    if (cause == kIntcVblankStart || cause == kIntcVblankEnd)
    {
        PS2_IF_AGRESSIVE_LOGS({
            static std::atomic<uint32_t> s_disableLogCount{0u};
            const uint32_t logIndex = s_disableLogCount.fetch_add(1u, std::memory_order_relaxed);
            if (logIndex < 32u)
            {
                RUNTIME_LOG("[DisableIntc] cause=" << cause);
            }
        });
    }
    setReturnS32(ctx, KE_OK);
}

void iDisableIntc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    DisableIntc(rdram, ctx, runtime);
}

void AddIntcHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    IrqHandlerInfo info{};
    info.cause = getRegU32(ctx, 4);
    info.handler = getRegU32(ctx, 5);
    uint32_t next = getRegU32(ctx, 6);
    info.arg = getRegU32(ctx, 7);
    info.gp = getRegU32(ctx, 28);
    info.sp = getRegU32(ctx, 29);
    info.enabled = true;

    int handlerId = 0;
    {
        std::lock_guard<std::mutex> lock(g_irq_handler_mutex);
        info.order = (next == 0) ? --g_intc_head_order : ++g_intc_tail_order;
        handlerId = g_nextIntcHandlerId++;
        info.id = handlerId;
        g_intcHandlers[handlerId] = info;
    }

    if (info.cause == kIntcVblankStart)
    {
        PS2_IF_AGRESSIVE_LOGS({
            static std::atomic<uint32_t> s_addHandlerLogCount{0u};
            const uint32_t logIndex = s_addHandlerLogCount.fetch_add(1u, std::memory_order_relaxed);
            if (logIndex < 32u)
            {
                auto flags = std::cout.flags();
                std::cout << "[AddIntcHandler] cause=" << info.cause
                          << " handler=0x" << std::hex << info.handler
                          << " arg=0x" << info.arg
                          << " gp=0x" << info.gp
                          << " sp=0x" << info.sp
                          << std::dec
                          << " id=" << handlerId
                          << std::endl;
                std::cout.flags(flags);
            }
        });
    }

    ensureInterruptWorkerRunning(rdram, runtime);
    setReturnS32(ctx, handlerId);
}

void AddIntcHandler2(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    AddIntcHandler(rdram, ctx, runtime);
}

void RemoveIntcHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t cause = getRegU32(ctx, 4);
    const int handlerId = static_cast<int>(getRegU32(ctx, 5));
    if (handlerId > 0)
    {
        std::lock_guard<std::mutex> lock(g_irq_handler_mutex);
        auto it = g_intcHandlers.find(handlerId);
        if (it != g_intcHandlers.end() && it->second.cause == cause)
        {
            g_intcHandlers.erase(it);
        }
    }
    setReturnS32(ctx, KE_OK);
}

void AddDmacHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    IrqHandlerInfo info{};
    info.cause = getRegU32(ctx, 4);
    info.handler = getRegU32(ctx, 5);
    uint32_t next = getRegU32(ctx, 6);
    info.arg = getRegU32(ctx, 7);
    info.gp = getRegU32(ctx, 28);
    info.sp = getRegU32(ctx, 29);
    info.enabled = true;

    int handlerId = 0;
    {
        std::lock_guard<std::mutex> lock(g_irq_handler_mutex);
        info.order = (next == 0) ? --g_dmac_head_order : ++g_dmac_tail_order;
        handlerId = g_nextDmacHandlerId++;
        info.id = handlerId;
        g_dmacHandlers[handlerId] = info;
    }
    setReturnS32(ctx, handlerId);
}

void AddDmacHandler2(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    AddDmacHandler(rdram, ctx, runtime);
}

void RemoveDmacHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t cause = getRegU32(ctx, 4);
    const int handlerId = static_cast<int>(getRegU32(ctx, 5));
    if (handlerId > 0)
    {
        std::lock_guard<std::mutex> lock(g_irq_handler_mutex);
        auto it = g_dmacHandlers.find(handlerId);
        if (it != g_dmacHandlers.end() && it->second.cause == cause)
        {
            g_dmacHandlers.erase(it);
        }
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

void iEnableDmac(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    EnableDmac(rdram, ctx, runtime);
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

void iDisableDmac(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    DisableDmac(rdram, ctx, runtime);
}
}
