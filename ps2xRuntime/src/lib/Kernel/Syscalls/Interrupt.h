#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include "ps2_syscalls.h"

namespace ps2_syscalls
{
    namespace interrupt_state
    {
        struct VSyncFlagRegistration
        {
            uint32_t flagAddr;
            uint32_t tickAddr;
        };

        extern std::mutex g_irq_handler_mutex;
        extern std::mutex g_irq_worker_mutex;
        extern std::condition_variable g_irq_worker_cv;
        extern std::mutex g_vsync_flag_mutex;
        extern std::condition_variable g_vsync_cv;
        extern std::atomic<bool> g_irq_worker_stop;
        extern std::atomic<bool> g_irq_worker_running;
        extern uint32_t g_enabled_intc_mask;
        extern uint32_t g_enabled_dmac_mask;
        extern uint64_t g_vsync_tick_counter;
        extern VSyncFlagRegistration g_vsync_registration;
        extern std::atomic<uint32_t> g_pending_intc_causes;
        constexpr int64_t kPendingIntcMaxAgeNs = 2'000'000'000; // ~2 s wall-clock (see Q1)
    }

    void dispatchDmacHandlersForCause(uint8_t *rdram, PS2Runtime *runtime, uint32_t cause);
    void raisePendingIntc(uint32_t cause);
    void drainPendingIntc(uint8_t *rdram, PS2Runtime *runtime);
    // Test-support: reset all INTC/DMAC handler bookkeeping to process-start
    // defaults so regression tests are order-independent.
    void resetInterruptHandlerState();
    using SteadyNowFn = std::function<std::chrono::steady_clock::time_point()>;
    void setPendingIntcClockForTest(SteadyNowFn fn); // nullptr restores steady_clock::now
    void EnsureVSyncWorkerRunning(uint8_t *rdram, PS2Runtime *runtime);
    uint64_t GetCurrentVSyncTick();
    void stopInterruptWorker();
    uint64_t WaitForNextVSyncTick(uint8_t *rdram, PS2Runtime *runtime);
    void WaitVSyncTick(uint8_t *rdram, PS2Runtime *runtime);
    void SetVSyncFlag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void EnableIntc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void iEnableIntc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void DisableIntc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void iDisableIntc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void AddIntcHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void AddIntcHandler2(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void RemoveIntcHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void AddDmacHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void AddDmacHandler2(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void RemoveDmacHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void EnableIntcHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void DisableIntcHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void EnableDmacHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void DisableDmacHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void EnableDmac(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void iEnableDmac(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void DisableDmac(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void iDisableDmac(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
}
