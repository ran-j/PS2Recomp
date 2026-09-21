#ifndef PS2_VU1_REGION_PLAN_H
#define PS2_VU1_REGION_PLAN_H

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

namespace ps2_vu_detail {

inline constexpr uint16_t regionConstantZero = 0xffffu;

struct RegionSources {
    std::array<uint16_t, 4> fs{}, ft{}, acc{}, lowerFs{}, lowerFt{};
    uint16_t viS{}, viT{}, viOld{}, scalarI{}, clip{};
};

struct RegionWrite {
    uint16_t value{}, issue{}, ready{}, sequence{};
    uint8_t reg{}, mask{};
};

template <size_t N>
struct RegionPlan {
    bool eligible = N >= 4u && N <= 16u;
    bool readsClip = false;
    uint16_t cycles{}, sequences{}, scalarI{};
    std::array<uint16_t, N> issue{};
    std::array<RegionSources, N> sources{};
    std::array<RegionWrite, N> upper{}, lower{}, vi{};
    std::array<std::array<uint16_t, 4>, 32> vfVisible{}, vfReady{}, vfLatest{};
    std::array<uint16_t, 16> viVisible{}, viReady{}, viLatest{};
    std::array<uint16_t, 4> accVisible{}, accReady{};
    std::array<std::array<uint8_t, 4>, 32> firstVfWrite{};
    std::array<uint8_t, 16> firstViWrite{};
};

constexpr bool regionLowerSupported(uint32_t lower)
{
    const uint32_t opcode = lower >> 25u, direct = lower & 0x3fu;
    const uint32_t special = (lower & 3u) | ((lower >> 4u) & 0x7cu);
    return lower == 0u || lower == 0x8000033cu || opcode == 0u || opcode == 1u ||
        opcode == 4u || opcode == 5u || opcode == 8u || opcode == 9u ||
        opcode == 0x10u || opcode == 0x12u || opcode == 0x13u || opcode == 0x1cu ||
        (opcode == 0x40u && (direct == 0x30u || direct == 0x31u || direct == 0x32u ||
            direct == 0x34u || direct == 0x35u ||
            (direct >= 0x3cu && ((special >= 0x30u && special <= 0x31u) ||
                               (special >= 0x34u && special <= 0x37u) ||
                               (special >= 0x3cu && special <= 0x3fu)))));
}

template <typename Decoded, size_t N>
constexpr uint8_t regionTerminalBranch(const std::array<Decoded, N> &decoded)
{
    if constexpr (N >= 2u)
    {
        const auto &branch = decoded[N - 2u];
        const uint32_t opcode = branch.lower >> 25u;
        if (!branch.iBit && (opcode == 0x28u || opcode == 0x29u))
            return static_cast<uint8_t>(opcode);
    }
    return 0u;
}

template <typename Decoded, size_t N>
constexpr RegionPlan<N> planRegion(const std::array<Decoded, N> &decoded)
{
    RegionPlan<N> plan;
    auto vfPending = plan.vfVisible;
    auto viPending = plan.viVisible;
    auto accPending = plan.accVisible;
    for (unsigned reg = 0; reg < 32; ++reg)
    {
        plan.vfVisible[reg].fill(reg);
        vfPending[reg].fill(reg);
        plan.firstVfWrite[reg].fill(255u);
    }
    for (unsigned reg = 0; reg < 16; ++reg)
        plan.viVisible[reg] = viPending[reg] = reg;
    plan.accVisible.fill(32u);
    accPending.fill(32u);
    plan.firstViWrite.fill(255u);
    const auto retire = [&](uint16_t cycle) {
        for (unsigned reg = 1; reg < 32; ++reg)
            for (unsigned lane = 0; lane < 4; ++lane)
                if (plan.vfReady[reg][lane] <= cycle)
                    plan.vfVisible[reg][lane] = vfPending[reg][lane];
        for (unsigned reg = 1; reg < 16; ++reg)
            if (plan.viReady[reg] <= cycle)
                plan.viVisible[reg] = viPending[reg];
        for (unsigned lane = 0; lane < 4; ++lane)
            if (plan.accReady[lane] <= cycle)
                plan.accVisible[lane] = accPending[lane];
    };
    for (unsigned index = 0; index < N; ++index)
    {
        const auto &pair = decoded[index];
        const uint32_t branchOpcode = pair.lower >> 25u;
        const bool terminalBranch = !pair.iBit && index + 2u == N &&
            (branchOpcode == 0x28u || branchOpcode == 0x29u);
        if (pair.eBit || pair.mBit || pair.dBit || pair.tBit || pair.upperUsage.reserved ||
            (!pair.iBit && (pair.lowerUsage.reserved || (!terminalBranch && !regionLowerSupported(pair.lower)))))
            plan.eligible = false;
        const uint32_t upperOp = pair.upper & 63u;
        const uint32_t upperSpecial = (pair.upper & 3u) | ((pair.upper >> 4u) & 0x7cu);
        const bool upperWritesZero = ((pair.upper >> 21u) & 15u) != 0u &&
            (upperOp < 0x3cu ? ((pair.upper >> 6u) & 31u) == 0u :
                (((upperSpecial >= 0x10u && upperSpecial <= 0x17u) || upperSpecial == 0x1du) &&
                 ((pair.upper >> 16u) & 31u) == 0u));
        const uint32_t lowerOp = pair.lower >> 25u;
        const uint32_t lowerSpecial = (pair.lower & 3u) | ((pair.lower >> 4u) & 0x7cu);
        const bool lowerReadsZero = ((pair.lower >> 11u) & 31u) == 0u &&
            (lowerOp == 1u || (lowerOp == 0x40u && (pair.lower & 63u) >= 0x3cu &&
                (lowerSpecial == 0x30u || lowerSpecial == 0x31u || lowerSpecial == 0x35u ||
                 lowerSpecial == 0x37u || lowerSpecial == 0x3cu)));
        // The scalar oracle exposes a transient upper VF0 write to lower reads.
        if (!pair.iBit && upperWritesZero && lowerReadsZero)
            plan.eligible = false;
        uint16_t cycle = plan.cycles;
        for (const auto *usage : {&pair.upperUsage, &pair.lowerUsage})
        {
            for (unsigned readIndex = 0; readIndex < usage->vfReadCount; ++readIndex)
            {
                const auto &read = usage->vfRead[readIndex];
                for (unsigned lane = 0; lane < 4; ++lane)
                    if ((read.lanes & (8u >> lane)) != 0u)
                        cycle = std::max(cycle, plan.vfReady[read.reg][lane]);
            }
            for (unsigned reg = 1; reg < 16; ++reg)
                if ((usage->viRead & (1u << reg)) != 0u)
                    cycle = std::max(cycle, plan.viReady[reg]);
            for (unsigned lane = 0; lane < 4; ++lane)
                if ((usage->accRead & (8u >> lane)) != 0u)
                    cycle = std::max(cycle, plan.accReady[lane]);
        }
        plan.issue[index] = cycle;
        retire(cycle);
        auto &source = plan.sources[index];
        source.fs = plan.vfVisible[(pair.upper >> 11u) & 31u];
        source.ft = plan.vfVisible[(pair.upper >> 16u) & 31u];
        source.acc = plan.accVisible;
        source.scalarI = plan.scalarI;
        if (pair.lowerUsage.readsClip)
        {
            plan.readsClip = true;
            for (unsigned previous = 0; previous < index; ++previous)
                if (decoded[previous].upperUsage.writesClip && plan.issue[previous] + 4u <= cycle)
                    source.clip = previous + 1u;
        }
        source.lowerFs = plan.vfVisible[(pair.lower >> 11u) & 31u];
        source.lowerFt = plan.vfVisible[(pair.lower >> 16u) & 31u];
        source.viS = plan.viVisible[(pair.lower >> 11u) & 15u];
        source.viT = plan.viVisible[(pair.lower >> 16u) & 15u];
        if (terminalBranch && index != 0u && decoded[index - 1u].lowerUsage.delaysNextBranchRead)
        {
            const auto &previous = plan.vi[index - 1u];
            if (previous.mask != 0u)
            {
                if (previous.reg == ((pair.lower >> 11u) & 15u))
                    source.viS = plan.sources[index - 1u].viOld;
                if (previous.reg == ((pair.lower >> 16u) & 15u))
                    source.viT = plan.sources[index - 1u].viOld;
            }
        }
        const auto writeVf = [&](const auto &usage, RegionWrite &write, uint16_t value) {
            const auto &access = usage.vfWrite;
            if (access.reg == 0u || access.lanes == 0u)
                return;
            write = {value, cycle, static_cast<uint16_t>(cycle + (usage.vfLatency != 0u ? usage.vfLatency : usage.latency)),
                     static_cast<uint16_t>(++plan.sequences), access.reg, access.lanes};
            for (unsigned lane = 0; lane < 4; ++lane)
                if ((access.lanes & (8u >> lane)) != 0u)
                {
                    vfPending[access.reg][lane] = value;
                    plan.vfReady[access.reg][lane] = write.ready;
                    plan.vfLatest[access.reg][lane] = write.sequence;
                    plan.firstVfWrite[access.reg][lane] = std::min(plan.firstVfWrite[access.reg][lane], static_cast<uint8_t>(cycle));
                }
        };
        writeVf(pair.upperUsage, plan.upper[index], 33u + index * 2u);
        if (pair.lowerUsage.vfWrite.reg != pair.suppressedLowerVf)
            writeVf(pair.lowerUsage, plan.lower[index], 34u + index * 2u);
        const uint16_t viWrites = pair.lowerUsage.viWrite & 0xfffeu;
        if (viWrites != 0u)
        {
            const uint8_t reg = std::countr_zero(viWrites);
            source.viOld = plan.viVisible[reg];
            const auto &usage = pair.lowerUsage;
            const uint16_t ready = cycle + (usage.viLatency != 0u ? usage.viLatency : usage.latency);
            plan.vi[index] = {static_cast<uint16_t>(16u + index), cycle, ready,
                             static_cast<uint16_t>(++plan.sequences), reg, 1u};
            viPending[reg] = 16u + index;
            plan.viReady[reg] = ready;
            plan.viLatest[reg] = plan.vi[index].sequence;
            plan.firstViWrite[reg] = std::min(plan.firstViWrite[reg], static_cast<uint8_t>(cycle));
        }
        for (unsigned lane = 0; lane < 4; ++lane)
            if ((pair.upperUsage.accWrite & (8u >> lane)) != 0u)
            {
                accPending[lane] = 33u + index * 2u;
                plan.accReady[lane] = cycle + 1u;
            }
        plan.cycles = cycle + 1u;
        if (pair.iBit)
            plan.scalarI = index + 1u;
        plan.vfVisible[0].fill(regionConstantZero);
        plan.viVisible[0] = regionConstantZero;
    }
    retire(plan.cycles);
    for (auto &lanes : plan.firstVfWrite)
        for (auto &offset : lanes)
            offset = std::min(offset, static_cast<uint8_t>(plan.cycles));
    for (auto &offset : plan.firstViWrite)
        offset = std::min(offset, static_cast<uint8_t>(plan.cycles));
    return plan;
}

}
#endif
