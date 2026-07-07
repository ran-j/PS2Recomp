#pragma once

#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <utility>
#include <vector>
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
        // Each entry pairs the guest tid with the parking fiber's identity token
        // (from ps2sched::current_fiber_token(), which encodes the fiber's
        // generation). signalVSyncFlag delivers the wakeup only to the exact
        // fiber that parked, so a recycled tid cannot receive a stale tick.
        // Borrowed host workers (g_currentThreadId == -1, token == 0) never park
        // here, so every stored entry has a non-zero token.
        extern std::vector<std::pair<int, uint64_t>> g_vsync_waitList;
        extern std::atomic<bool> g_irq_worker_stop;
        extern std::atomic<bool> g_irq_worker_running;
        extern std::thread g_irq_worker_thread; // joinable worker handle
        // Written by Enable/DisableIntc(Dmac) and read by dispatchIntcHandlersForCause /
        // dispatchDmacHandlersForCause from the IRQ worker thread. Atomic (rather
        // than g_irq_handler_mutex) because the dispatch call sites read the mask
        // while evaluating a function argument, i.e. before any lock is taken.
        extern std::atomic<uint32_t> g_enabled_intc_mask;
        extern std::atomic<uint32_t> g_enabled_dmac_mask;
        extern uint64_t g_vsync_tick_counter;
        extern VSyncFlagRegistration g_vsync_registration;
    }

    void dispatchDmacHandlersForCause(uint8_t *rdram, PS2Runtime *runtime, uint32_t cause);
    void EnsureVSyncWorkerRunning(uint8_t *rdram, PS2Runtime *runtime);
    uint64_t GetCurrentVSyncTick();
    void stopInterruptWorker();
    // Signal-only variant: sets the stop flag and wakes the worker but does
    // NOT join. For callers on the guest executor thread (a fiber calling
    // requestStop): joining there can deadlock against a worker blocked in
    // async_guest_begin(), whose wait predicate (g_running_fiber == nullptr)
    // cannot become true while the joining fiber is itself the running fiber.
    // The join happens later in scheduler_shutdown() on the main thread
    // (stopInterruptWorker is idempotent).
    void signalInterruptWorkerStop();
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
