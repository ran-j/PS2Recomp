#ifndef PS2_VU1_LOOP_PLAN_H
#define PS2_VU1_LOOP_PLAN_H

#include "ps2_vu1_region_plan.h"
#include <cstdlib>

namespace ps2_vu_detail {

inline const bool disableCountedLoops = std::getenv("PS2_VU_DISABLE_COUNTED_LOOPS") != nullptr;
inline const bool profileCountedLoops = std::getenv("PS2_VU_PROFILE_COUNTED_LOOPS") != nullptr;
inline thread_local uint64_t countedLoopPairs = 0u;

constexpr bool loopLowerSupported(uint32_t lower)
{
    const uint32_t opcode = lower >> 25u, direct = lower & 0x3fu;
    const uint32_t special = (lower & 3u) | ((lower >> 4u) & 0x7cu);
    // Keep unsupported flag writers and internal branches on the exact fallback.
    return lower == 0u || lower == 0x8000033cu || opcode == 0u || opcode == 1u ||
        opcode == 4u || opcode == 5u || opcode == 8u || opcode == 9u ||
        opcode == 0x10u || opcode == 0x12u || opcode == 0x13u || opcode == 0x1cu ||
        (opcode == 0x40u && (direct == 0x30u || direct == 0x31u || direct == 0x32u ||
            direct == 0x34u || direct == 0x35u ||
            (direct >= 0x3cu && ((special >= 0x30u && special <= 0x31u) ||
                               (special >= 0x34u && special <= 0x37u) ||
                               (special >= 0x3cu && special <= 0x3fu)))));
}

template <size_t N>
struct CountedLoopPlan {
    bool eligible = false;
    RegionPlan<N> body{};
    std::array<std::array<uint16_t, 4>, 32> vfNext{};
    std::array<uint16_t, 16> viNext{};
    uint16_t branchS{}, branchT{};
    uint16_t firstDiv = N;
};

template <typename Decoded, size_t N>
constexpr CountedLoopPlan<N> planCountedLoop(const std::array<Decoded, N> &decoded)
{
    CountedLoopPlan<N> loop;
    if constexpr (N < 4u || N > 32u)
        return loop;
    else
    {
        constexpr unsigned branchIndex = N - 2u;
        const uint32_t branch = decoded[branchIndex].lower;
        const int32_t offset = static_cast<int32_t>(branch << 21u) >> 21u;
        const uint32_t opcode = branch >> 25u;
        if (decoded[branchIndex].iBit || (opcode != 0x28u && opcode != 0x29u) ||
            offset != -static_cast<int32_t>(N - 1u))
            return loop;
        const unsigned branchS = (branch >> 11u) & 15u, branchT = (branch >> 16u) & 15u;
        auto straight = decoded;
        straight[branchIndex].lower = 0u;
        straight[branchIndex].lowerUsage = {};
        const auto supported = planRegion(straight, true);
        if (!supported.eligible)
            return loop;
        for (unsigned index = 0; index < N; ++index)
        {
            if (decoded[index].eBit || decoded[index].mBit ||
                decoded[index].dBit || decoded[index].tBit)
                return loop;
            if (!decoded[index].iBit && index != branchIndex &&
                !loopLowerSupported(decoded[index].lower) && !regionDiv(decoded[index].lower))
                return loop;
            if (!decoded[index].iBit && regionDiv(decoded[index].lower))
                loop.firstDiv = std::min(loop.firstDiv, static_cast<uint16_t>(index));
        }
        loop.body = planRegion(decoded, true);
        const auto &plan = loop.body;
        if (plan.cycles < 4u)
            return loop;
        if (plan.pendingQ != 0u && plan.qReady[plan.pendingQ - 1u] >
            plan.cycles + plan.issue[loop.firstDiv])
            return loop;
        // A pending entry value and the visible entry value share one local phi.
        // Keep that representation only when the previous iteration's final write
        // retires before the next write can cancel it. Otherwise the visible old
        // value must survive independently of the canceled pending value.
        for (unsigned reg = 0; reg < 32u; ++reg)
            for (unsigned lane = 0; lane < 4u; ++lane)
                if (plan.vfReady[reg][lane] > plan.cycles &&
                    plan.firstVfWrite[reg][lane] < plan.vfReady[reg][lane] - plan.cycles)
                    return loop;
        for (unsigned reg = 0; reg < 16u; ++reg)
            if (plan.viReady[reg] > plan.cycles &&
                plan.firstViWrite[reg] < plan.viReady[reg] - plan.cycles)
                return loop;
        loop.vfNext = plan.vfVisible;
        loop.viNext = plan.viVisible;
        for (unsigned index = 0; index < N; ++index)
        {
            for (const auto *write : {&plan.upper[index], &plan.lower[index]})
                if (write->mask != 0u)
                    for (unsigned lane = 0; lane < 4; ++lane)
                        if ((write->mask & (8u >> lane)) != 0u &&
                            write->sequence == plan.vfLatest[write->reg][lane])
                            loop.vfNext[write->reg][lane] = write->value;
            const auto &write = plan.vi[index];
            if (write.mask != 0u && write.sequence == plan.viLatest[write.reg])
                loop.viNext[write.reg] = write.value;
        }
        loop.branchS = plan.sources[branchIndex].viS;
        loop.branchT = plan.sources[branchIndex].viT;
        const auto &previous = plan.vi[branchIndex - 1u];
        if (previous.mask != 0u && decoded[branchIndex - 1u].lowerUsage.delaysNextBranchRead)
        {
            if (previous.reg == branchS)
                loop.branchS = plan.sources[branchIndex - 1u].viOld;
            if (previous.reg == branchT)
                loop.branchT = plan.sources[branchIndex - 1u].viOld;
        }
        // Entry versions may still be pending, but every use must follow their ready cycle.
        const auto vfReady = [&](uint16_t source, unsigned lane, unsigned issue) {
            return source >= 32u || plan.vfReady[source][lane] <= plan.cycles + issue;
        };
        const auto viReady = [&](uint16_t source, unsigned issue) {
            return source >= 16u || plan.viReady[source] <= plan.cycles + issue;
        };
        for (unsigned index = 0; index < N; ++index)
        {
            const auto &pair = decoded[index];
            const auto &source = plan.sources[index];
            for (unsigned lower = 0; lower < 2u; ++lower)
            {
                const auto &usage = lower ? pair.lowerUsage : pair.upperUsage;
                const uint32_t instruction = lower ? pair.lower : pair.upper;
                const auto &fs = lower ? source.lowerFs : source.fs;
                const auto &ft = lower ? source.lowerFt : source.ft;
                for (unsigned readIndex = 0; readIndex < usage.vfReadCount; ++readIndex)
                {
                    const auto &read = usage.vfRead[readIndex];
                    const auto &versions = read.reg == ((instruction >> 11u) & 31u) ? fs : ft;
                    for (unsigned lane = 0; lane < 4u; ++lane)
                        if ((read.lanes & (8u >> lane)) != 0u &&
                            !vfReady(versions[lane], lane, plan.issue[index]))
                            return loop;
                }
            }
            if (index == branchIndex)
            {
                if (!viReady(loop.branchS, plan.issue[index]) || !viReady(loop.branchT, plan.issue[index]))
                    return loop;
            }
            else
            {
                const auto reads = pair.lowerUsage.viRead;
                if (((reads & (1u << ((pair.lower >> 11u) & 15u))) != 0u &&
                     !viReady(source.viS, plan.issue[index])) ||
                    ((reads & (1u << ((pair.lower >> 16u) & 15u))) != 0u &&
                     !viReady(source.viT, plan.issue[index])))
                    return loop;
            }
        }
        loop.eligible = true;
        return loop;
    }
}

}
#endif
