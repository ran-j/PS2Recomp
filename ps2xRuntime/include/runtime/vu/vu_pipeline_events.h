#pragma once

#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

namespace ps2vu
{
    enum class WritebackKind : uint32_t
    {
        Flag,
        Fdiv,
        Efu,
        Store,
        Vf,
        Vi,
        Acc
    };

    class PipelineEvents
    {
      public:
        static constexpr uint64_t noEvent = std::numeric_limits<uint64_t>::max();
        static constexpr uint32_t cycleCount = 64;

        static constexpr uint32_t capacity(WritebackKind kind)
        {
            const auto index = static_cast<uint32_t>(kind);
            return offsets[index + 1] - offsets[index];
        }

        template <WritebackKind Kind> static uint32_t slots(uint64_t events)
        {
            return static_cast<uint32_t>(events >> offsets[static_cast<uint32_t>(Kind)]) &
                   ((1u << capacity(Kind)) - 1u);
        }

        template <WritebackKind Kind> void schedule(uint64_t cycle, uint32_t latency, uint32_t slot)
        {
            if (latency == 0 || latency >= cycleCount || slot >= capacity(Kind))
                throw std::logic_error("Invalid VU pipeline event");
            const uint32_t bucket = static_cast<uint32_t>((cycle + latency) & (cycleCount - 1));
            m_events[bucket] |= uint64_t{1} << (offsets[static_cast<uint32_t>(Kind)] + slot);
            m_cycles |= uint64_t{1} << bucket;
        }

        uint64_t take(uint64_t cycle)
        {
            const uint32_t bucket = static_cast<uint32_t>(cycle & (cycleCount - 1));
            m_cycles &= ~(uint64_t{1} << bucket);
            return std::exchange(m_events[bucket], 0);
        }

        uint64_t nextCycle(uint64_t cycle) const
        {
            if (m_cycles == 0)
                return noEvent;
            return cycle + std::countr_zero(std::rotr(m_cycles, static_cast<int>(cycle & (cycleCount - 1))));
        }

        void reset()
        {
            m_events.fill(0);
            m_cycles = 0;
        }

      private:
        static constexpr std::array<uint32_t, 8> offsets{0, 8, 9, 11, 19, 35, 43, 51};
        std::array<uint64_t, cycleCount> m_events{};
        uint64_t m_cycles = 0;
    };
}
