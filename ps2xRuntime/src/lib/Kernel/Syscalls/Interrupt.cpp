#include "Common.h"
#include "Interrupt.h"
#include "ps2_log.h"
#include "Stubs/GS.h"
#include "ps2_fiber.h"

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
        std::vector<std::pair<int, uint64_t>> g_vsync_waitList;
        std::atomic<bool> g_irq_worker_stop{false};
        std::atomic<bool> g_irq_worker_running{false};
        std::thread g_irq_worker_thread; // joinable worker handle so stopInterruptWorker() can join it
        // See Interrupt.h: read from the IRQ worker thread without holding
        // g_irq_handler_mutex (the dispatch*HandlersForCause call sites read this
        // while evaluating a function argument), so it must be atomic rather than
        // mutex-protected like the rest of the handler bookkeeping.
        std::atomic<uint32_t> g_enabled_intc_mask{0xFFFFFFFFu};
        std::atomic<uint32_t> g_enabled_dmac_mask{0xFFFFFFFFu};
        constexpr uint32_t kAsyncHandlerStackSize = 0x4000u; // 16 KB, one pool slot
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
        // Only reachable if the callback stack pool is exhausted or runtime is
        // null; see kAsyncCallbackFallbackSp for the fallback.
        thread_local PS2Runtime *s_cachedRuntime = nullptr;
        thread_local uint32_t s_cachedStackTop = 0u;

        if (runtime == nullptr)
        {
            return kAsyncCallbackFallbackSp;
        }

        if (s_cachedRuntime != runtime || s_cachedStackTop == 0u)
        {
            s_cachedRuntime = runtime;
            s_cachedStackTop = runtime->reserveAsyncCallbackStack(kAsyncHandlerStackSize, 16u);
        }

        return (s_cachedStackTop != 0u) ? s_cachedStackTop : kAsyncCallbackFallbackSp;
    }

    // Unified INTC/DMAC dispatch: collect handlers from `handlerMap` that are
    // enabled for `cause`, then run them under AsyncGuestScope. `enabledMask`
    // is the per-cause enable bitmask (g_enabled_intc_mask or g_enabled_dmac_mask).
    // `tag` is used for exception logging ("INTC" or "DMAC").
    //
    // NOTE on guest-memory concurrency: the handler bodies invoked below run on
    // the IRQ worker thread (a real, separate host thread — see AsyncGuestScope),
    // genuinely in parallel with the main guest fiber executing on the scheduler's
    // single guest-executor thread. If a handler and the main guest code both
    // touch the same rdram address without the guest itself arranging a lock/
    // semaphore, that is an intentional characteristic of this "IRQ handlers are
    // real concurrent workers" design (it mirrors how an interrupt handler
    // touching a shared variable requires the GUEST to synchronize, exactly as
    // on real hardware) rather than a synchronization bug in the runtime. Do not
    // add locking around guest rdram accesses here to silence such reports.
    static void dispatchHandlersForCause(
        uint8_t *rdram, PS2Runtime *runtime, uint32_t cause,
        const std::unordered_map<int, IrqHandlerInfo> &handlerMap,
        uint32_t enabledMask, const char *tag)
    {
        if (!rdram || !runtime)
        {
            return;
        }

        std::vector<IrqHandlerInfo> handlers;
        {
            std::lock_guard<std::mutex> lock(g_irq_handler_mutex);
            if (cause < 32u && (enabledMask & (1u << cause)) == 0u)
            {
                return;
            }

            handlers.reserve(handlerMap.size());
            for (const auto &kv : handlerMap)
            {
                const IrqHandlerInfo &info = kv.second;
                if (!info.enabled || info.cause != cause || info.handler == 0u)
                {
                    continue;
                }
                handlers.push_back(info);
            }
            std::sort(handlers.begin(), handlers.end(), [](const IrqHandlerInfo &a, const IrqHandlerInfo &b)
                      { return a.order < b.order; });
        }

        auto runHandlers = [&](uint32_t stackTop)
        {
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
                    SET_GPR_U32(&irqCtx, 29, stackTop);
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
                        std::cerr << "[" << tag << "] handler 0x" << std::hex << info.handler
                                  << " threw exception: " << e.what() << std::dec << std::endl;
                        ++warnCount;
                    }
                }
            }
        };

        // Nothing to run: skip the token borrow and the scratch reservation.
        if (handlers.empty())
        {
            return;
        }

        // The INTC path only ever runs on the IRQ worker host thread, but the
        // DMAC path is ALSO reachable synchronously from guest code:
        // sceSifSetDma (Stubs/SIF.cpp), sceDmaSend (Stubs/Helpers/Support.h),
        // and drainCompletedDmacHandlers (ps2_runtime.cpp) all call
        // dispatchDmacHandlersForCause inline while servicing a guest syscall,
        // i.e. while the calling fiber IS the guest execution slot.
        // AsyncGuestScope's async_guest_begin() aborts by design if invoked
        // from the guest executor thread (that guard exists to catch host
        // workers mistakenly running there) - so only borrow the token when
        // this call is NOT already running on the guest thread.
        if (ps2sched::is_guest_thread())
        {
            // Inline on the calling fiber: a handler body can yield at a
            // back-edge, so a shared stack would be clobbered by another fiber
            // dispatching inline (or by a nested inline DMAC on this same
            // fiber). Reserve a fresh per-invocation scratch stack — NOT the
            // async pool: a per-fiber pool reservation would exhaust the pool's
            // 32 slots and fall back onto a shared stack, reintroducing the bug.
            // Handlers in this loop run sequentially (never nested), so one
            // reservation for the whole dispatch is correct; guestFree fires
            // here when the dispatch (and any yield inside it) completes.
            GuestScratchStack handlerStack(runtime, kAsyncHandlerStackSize);
            runHandlers(handlerStack.valid() ? handlerStack.top()
                                             : getAsyncHandlerStackTop(runtime));
        }
        else
        {
            // Host worker (INTC) under AsyncGuestScope: cannot yield, one
            // callback runs to completion, so the per-OS-thread pool cache is
            // safe, including its failure fallback (kAsyncCallbackFallbackSp
            // when the pool is exhausted or runtime is null).
            AsyncGuestScope guestScope; // token released on any exit path
            runHandlers(getAsyncHandlerStackTop(runtime));
        }
    }

    static void dispatchIntcHandlersForCause(uint8_t *rdram, PS2Runtime *runtime, uint32_t cause)
    {
        dispatchHandlersForCause(rdram, runtime, cause, g_intcHandlers,
                                  g_enabled_intc_mask.load(std::memory_order_acquire), "INTC");
    }

    void dispatchDmacHandlersForCause(uint8_t *rdram, PS2Runtime *runtime, uint32_t cause)
    {
        dispatchHandlersForCause(rdram, runtime, cause, g_dmacHandlers,
                                  g_enabled_dmac_mask.load(std::memory_order_acquire), "DMAC");
    }

    static void updateGsCsrFieldForVSync(PS2Runtime *runtime, uint64_t tickValue)
    {
        if (!runtime)
        {
            return;
        }

        constexpr uint64_t kGsCsrFieldMask = 0x2000ull;
        std::atomic<uint64_t> &csr = runtime->memory().gs().csr;
        if (tickValue & 1ull)
        {
            csr.fetch_or(kGsCsrFieldMask);
        }
        else
        {
            csr.fetch_and(~kGsCsrFieldMask);
        }
    }

    static uint64_t signalVSyncFlag(uint8_t *rdram, PS2Runtime *runtime)
    {
        VSyncFlagRegistration reg{};
        uint64_t tickValue = 0u;
        {
            std::lock_guard<std::mutex> lock(g_vsync_flag_mutex);
            reg = g_vsync_registration;
            tickValue = ++g_vsync_tick_counter;
        }

        // Wake all guest threads waiting for the next vsync tick.
        // Called from the IRQ worker (a non-guest host thread). Use the identity-
        // validated wakeup so a recycled tid cannot deliver this tick to the wrong
        // fiber: each entry carries the parking fiber's generation token.
        wakeWaiters(g_vsync_flag_mutex, g_vsync_waitList);
        updateGsCsrFieldForVSync(runtime, tickValue);

        // These two writes race, by design, with guest/test code polling the
        // same rdram words on another thread (real PS2 hardware exposes the
        // vsync flag/tick exactly this way: a plain memory-mapped word the
        // application polls, with no interlock). TSan reports this as a data
        // race because it is one under the C++ memory model, but adding a
        // mutex here would not match real hardware semantics and would still
        // require the poller to take the same lock (it can't: it's guest code
        // reading raw rdram, or test code via readGuestU32/readGuestU64,
        // neither of which we can — or should — change). Left unsynchronized
        // intentionally; do not wrap in a lock.
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
                const uint64_t tickValue = signalVSyncFlag(rdram, runtime);
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

        if (g_irq_worker_thread.joinable())
        {
            g_irq_worker_thread.join();
        }

        g_irq_worker_stop.store(false, std::memory_order_release);
        g_irq_worker_running.store(true, std::memory_order_release);
        try
        {
            g_irq_worker_thread = std::thread(interruptWorkerMain, rdram, runtime);
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

    void signalInterruptWorkerStop()
    {
        // Signal-only: no join (see Interrupt.h). The worker observes the stop
        // flag on its next CV wait / loop check and exits; scheduler_shutdown()
        // performs the join on the main thread via stopInterruptWorker().
        g_irq_worker_stop.store(true, std::memory_order_release);
        g_irq_worker_cv.notify_all();
    }

    void stopInterruptWorker()
    {
        g_irq_worker_stop.store(true, std::memory_order_release);
        g_irq_worker_cv.notify_all();

        // Join the worker to a clean stop; it checks g_irq_worker_stop on both
        // its CV wait and its while-condition, so it exits promptly. We must
        // NOT hold g_irq_worker_mutex while joining (the worker takes that
        // mutex on its CV wait — joining under it would deadlock).
        std::thread workerToJoin;
        {
            std::lock_guard<std::mutex> lock(g_irq_worker_mutex);
            if (g_irq_worker_thread.joinable())
            {
                workerToJoin = std::move(g_irq_worker_thread);
            }
        }
        if (workerToJoin.joinable())
        {
            workerToJoin.join();
        }

        // Wake any guest threads waiting on vsync during shutdown.
        wakeWaiters(g_vsync_flag_mutex, g_vsync_waitList);
    }

    uint64_t WaitForNextVSyncTick(uint8_t *rdram, PS2Runtime *runtime)
    {
        ensureInterruptWorkerRunning(rdram, runtime);

        // Opaque identity of the fiber that is about to park. Non-fiber host
        // workers get token 0 and never publish to the wait-list.
        const uint64_t selfToken = ps2sched::current_fiber_token();
        const bool onFiber = (selfToken != 0u);

        // Snapshot the tick we are waiting to advance past. A non-fiber worker
        // never publishes to g_vsync_waitList (it cannot park), so it cannot
        // rely on a single wake meaning "signalVSyncFlag ran"; it must instead
        // poll this counter directly until it changes.
        uint64_t entryTick;
        {
            std::lock_guard<std::mutex> lock(g_vsync_flag_mutex);
            entryTick = g_vsync_tick_counter;
        }

        if (!onFiber)
        {
            // Non-fiber path (IRQ/alarm worker calling back into vsync wait, or
            // a borrowed host worker): loop with bounded exponential backoff
            // (mirrors WaitSema/WaitEventFlag's non-fiber Mesa loop) until
            // g_vsync_tick_counter actually advances past entryTick or runtime
            // stop is requested. A single block_current()+backoff step could
            // return before the IRQ worker's next tick fired, handing back the
            // SAME tick the caller already observed instead of truly waiting
            // for the next one.
            NonFiberBackoff nfBackoff;
            for (;;)
            {
                const ps2sched::BlockResult br = ps2sched::block_current();
                nfBackoff.step(br);

                std::lock_guard<std::mutex> lock(g_vsync_flag_mutex);
                if (g_vsync_tick_counter != entryTick)
                {
                    return g_vsync_tick_counter;
                }
                if (runtime == nullptr || runtime->isStopRequested())
                {
                    return g_vsync_tick_counter;
                }
            }
        }

        // Publish under g_vsync_flag_mutex; arm_park after the lock is
        // released so g_sched_mutex is never nested under it.
        {
            std::lock_guard<std::mutex> lock(g_vsync_flag_mutex);
            g_vsync_waitList.emplace_back(g_currentThreadId, selfToken);
        }
        ps2sched::arm_park();

        // Block the current fiber; signalVSyncFlag calls the validated wakeup
        // from the IRQ worker thread to wake us.
        const ps2sched::BlockResult br = ps2sched::block_current();

        // A fiber woken from a real park (Parked) may have been woken by
        // scheduler_shutdown / TerminateThread rather than a vsync tick. If so,
        // unwind instead of returning a tick value. Mirrors WaitSema's terminate
        // check after wake.
        if (br == ps2sched::BlockResult::Parked)
        {
            std::shared_ptr<ThreadInfo> info = lookupThreadInfo(g_currentThreadId);
            if (info && info->terminated.load())
            {
                // Drop our wait-list entry before unwinding so a recycled tid
                // cannot inherit a stale token.
                {
                    std::lock_guard<std::mutex> clLock(g_vsync_flag_mutex);
                    auto &wl = g_vsync_waitList;
                    auto it = std::find_if(wl.begin(), wl.end(),
                                           [selfToken](const std::pair<int, uint64_t> &e)
                                           { return e.second == selfToken; });
                    if (it != wl.end()) wl.erase(it);
                }
                throw ThreadExitException();
            }
        }

        // If we were woken by something other than a vsync tick (shutdown,
        // TerminateThread, or a wakeup during the parking window), signalVSyncFlag
        // never drained us, so our entry is still queued. Remove it by fiber-token
        // identity (NOT by tid, which can recycle). A real vsync wake already
        // swapped us out, so this erase is a harmless no-op on that path.
        std::lock_guard<std::mutex> lock(g_vsync_flag_mutex);
        auto &wl = g_vsync_waitList;
        auto it = std::find_if(wl.begin(), wl.end(),
                               [selfToken](const std::pair<int, uint64_t> &e)
                               { return e.second == selfToken; });
        if (it != wl.end())
        {
            wl.erase(it);
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
            // Atomic RMW: g_enabled_intc_mask is read lock-free from the IRQ
            // worker thread (see declaration comment), so it must also be
            // written lock-free rather than under g_irq_handler_mutex.
            g_enabled_intc_mask.fetch_or(1u << cause, std::memory_order_acq_rel);
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
            g_enabled_intc_mask.fetch_and(~(1u << cause), std::memory_order_acq_rel);
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
            // See EnableIntc: g_enabled_dmac_mask is atomic for the same reason.
            g_enabled_dmac_mask.fetch_or(1u << cause, std::memory_order_acq_rel);
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
            g_enabled_dmac_mask.fetch_and(~(1u << cause), std::memory_order_acq_rel);
        }
        setReturnS32(ctx, KE_OK);
    }

    void iDisableDmac(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        DisableDmac(rdram, ctx, runtime);
    }
}
