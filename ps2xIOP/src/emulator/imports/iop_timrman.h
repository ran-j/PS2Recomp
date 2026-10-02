#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ps2x::iop::detail
{
    struct IopCpuState;
    class IopGuestExecutor;

    class IopTimrman
    {
    public:
        void reset() noexcept;

        [[nodiscard]] bool dispatchImport(uint16_t ordinal, IopCpuState &cpu, uint64_t currentCycle);
        void serviceDue(uint64_t currentCycle, IopGuestExecutor &executor);
        [[nodiscard]] uint64_t nextEventCycle(uint64_t fallback) const noexcept;

    private:
        struct Callback
        {
            uint32_t function = 0u;
            uint32_t common = 0u;
            uint32_t gp = 0u;
        };

        struct Timer
        {
            uint32_t address = 0u;
            uint8_t sources = 0u;
            uint8_t width = 0u;
            uint16_t maxPrescale = 0u;
            uint8_t irq = 0u;
            uint8_t users = 0u;

            uint32_t source = 1u;
            uint32_t prescale = 1u;
            uint32_t setupMode = 0u;
            uint32_t liveMode = 0u;
            uint32_t counterBase = 0u;
            uint32_t compare = 0u;
            uint64_t counterBaseCycle = 0u;
            uint64_t compareCycle = UINT64_MAX;
            uint64_t overflowCycle = UINT64_MAX;
            bool configured = false;
            bool running = false;

            Callback compareCallback;
            Callback overflowCallback;
        };

        [[nodiscard]] Timer *timerFromId(uint32_t timerId) noexcept;
        [[nodiscard]] const Timer *timerFromId(uint32_t timerId) const noexcept;
        [[nodiscard]] static uint32_t timerId(size_t index) noexcept;
        [[nodiscard]] static uint64_t ticksToCycles(const Timer &timer, uint64_t ticks) noexcept;
        [[nodiscard]] static uint64_t elapsedTicks(const Timer &timer, uint64_t currentCycle) noexcept;
        [[nodiscard]] static uint32_t counterValue(const Timer &timer, uint64_t currentCycle) noexcept;
        static void schedule(Timer &timer, uint64_t currentCycle) noexcept;
        static void stop(Timer &timer, uint64_t currentCycle) noexcept;

        std::array<Timer, 6> m_timers{};
        uint32_t m_holdMode = 0u;
        bool m_servicing = false;
    };
}
