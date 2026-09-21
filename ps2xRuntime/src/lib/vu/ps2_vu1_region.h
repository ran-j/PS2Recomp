#ifndef PS2_VU1_REGION_H
#define PS2_VU1_REGION_H

#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "ps2_vu1_region_retirement.h"

namespace ps2_vu_detail {

struct RegionCounters {
    uint64_t total[2]{};
    uint64_t candidate[2]{};
    uint64_t accepted[2]{};
    uint64_t acceptedByLength[17][2]{};
    ~RegionCounters()
    {
        if (!std::getenv("PS2_VU_PROFILE_REGIONS"))
            return;
        for (unsigned unit = 0; unit < 2; ++unit)
        {
            std::fprintf(stderr, "VU%u regions: total=%llu candidate=%llu accepted=%llu pairs\n",
                unit, static_cast<unsigned long long>(total[unit]),
                static_cast<unsigned long long>(candidate[unit]),
                static_cast<unsigned long long>(accepted[unit]));
            for (unsigned length = 4u; length <= 16u; length += 4u)
                if (acceptedByLength[length][unit] != 0u)
                    std::fprintf(stderr, "VU%u region%u=%llu pairs\n", unit, length,
                        static_cast<unsigned long long>(acceptedByLength[length][unit]));
        }
    }
};
inline thread_local RegionCounters regionCounters;
inline const bool profileRegions = std::getenv("PS2_VU_PROFILE_REGIONS") != nullptr;

constexpr bool regionBudgetFits(uint64_t start, uint64_t budgetEnd, unsigned cycles)
{
    // The last issue can retain results for four further cycles. Leave clock
    // wrap to the ordinary executor instead of wrapping deferred timestamps.
    return start <= UINT64_MAX - cycles - 4u &&
        budgetEnd >= start && budgetEnd - start >= cycles;
}

template <typename State, typename Flags, typename VfWrites, typename ViWrites,
          typename VfSequences, typename ViSequences>
#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
void retireRegionInputs(State &state, uint64_t start, Flags &flags, VfWrites &vfWrites,
                        ViWrites &viWrites, const VfSequences &vfSequences,
                        const ViSequences &viSequences, uint32_t &activeFlags,
                        uint32_t &activeVf, uint32_t &activeVi,
                        const std::array<std::array<uint8_t, 4>, 32> &firstWrite,
                        const std::array<uint8_t, 16> &firstViWrite)
{
    // No instruction in this region observes flags. Retain their chronological
    // sticky accumulation while moving retirement before the arithmetic.
    for (unsigned offset = 1u; offset <= 4u; ++offset)
    {
        const uint64_t cycle = start + offset;
        for (uint32_t active = activeFlags & (3u << (2u * (cycle & 3u)));
             active != 0u; active &= active - 1u)
        {
            const unsigned index = std::countr_zero(active);
            const auto &entry = flags[index];
            if (entry.readyCycle > cycle)
                continue;
            if (entry.writesMac)
                state.mac = entry.mac;
            if (entry.writesStatus)
            {
                const uint32_t current = entry.status & 0xfu;
                state.status = (state.status & 0xff0u) | current | ((current | entry.extraSticky) << 6u);
            }
            if (entry.writesSticky)
                state.status = (state.status & 0x3fu) | (entry.status & 0xfc0u);
            if (entry.writesClip)
                state.clip = entry.clip;
            activeFlags &= ~(1u << index);
        }
    }
    for (uint32_t active = activeVf; active != 0u; active &= active - 1u)
    {
        const unsigned index = std::countr_zero(active);
        const auto &write = vfWrites[index];
        if (write.readyCycle > start + 4u)
            continue;
        // A later issue can cancel a pending lane before it becomes visible.
        const uint32_t offset = write.readyCycle > start ? write.readyCycle - start : 0u;
        retireRegionVector(state.vf[write.reg], write.value.data(), vfSequences[write.reg].data(),
                           write.sequence, write.laneMask, firstWrite[write.reg].data(), offset);
        activeVf &= ~(1u << index);
    }
    for (uint32_t active = activeVi; active != 0u; active &= active - 1u)
    {
        const unsigned index = std::countr_zero(active);
        const auto &write = viWrites[index];
        if (write.readyCycle > start + 4u)
            continue;
        if (viSequences[write.reg] == write.sequence &&
            write.readyCycle <= start + firstViWrite[write.reg])
            state.vi[write.reg] = static_cast<int16_t>(write.value);
        activeVi &= ~(1u << index);
    }
}

}
#endif
