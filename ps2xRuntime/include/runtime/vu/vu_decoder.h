#pragma once
#include "vu_fields.h"
#include "vu_types.h"
#include <algorithm>
#include <array>
#include <bit>

namespace ps2vu
{
    inline constexpr uint32_t kFmacLatency = 4;
    constexpr uint8_t laneForComponent(uint32_t c)
    {
        return static_cast<uint8_t>(1u << (3u - c));
    }

    constexpr void addVfRead(InstructionUsage &usage, uint8_t reg, uint8_t lanes)
    {
        if (lanes == 0u)
            return;
        for (uint32_t index = 0; index < usage.vfReadCount; ++index)
        {
            if (usage.vfRead[index].reg == reg)
            {
                usage.vfRead[index].lanes |= lanes;
                return;
            }
        }
        if (usage.vfReadCount < usage.vfRead.size())
            usage.vfRead[usage.vfReadCount++] = {reg, lanes};
    }

    constexpr void addVfWrite(InstructionUsage &usage, uint8_t reg, uint8_t lanes)
    {
        if (reg == 0u || lanes == 0u)
            return;
        if (usage.vfWrite.reg == 0u)
            usage.vfWrite = {reg, lanes};
        else if (usage.vfWrite.reg == reg)
            usage.vfWrite.lanes |= lanes;
    }

    constexpr InstructionUsage decodeUpperUsage(uint32_t upper)
    {
        InstructionUsage usage;
        usage.pipeline = PipelineFmac;
        usage.latency = kFmacLatency;

        const uint8_t op = static_cast<uint8_t>(upper & 0x3Fu);
        const uint8_t dest = destMask(upper);
        const uint8_t fs = vfSource(upper);
        const uint8_t ft = vfTarget(upper);
        const uint8_t fd = vfDestination(upper);

        if (op <= 0x2Fu)
        {
            addVfRead(usage, fs, dest);
            addVfWrite(usage, fd, dest);
            if (op <= 0x1Bu)
                addVfRead(usage, ft, laneForComponent(op & 3u));
            else if (op >= 0x28u)
                addVfRead(usage, ft, op == 0x2Eu ? 0xEu : dest);
            if (op == 0x08u || op == 0x09u || op == 0x0Au || op == 0x0Bu || op == 0x0Cu || op == 0x0Du || op == 0x0Eu ||
                op == 0x0Fu || op == 0x21u || op == 0x23u || op == 0x25u || op == 0x27u || op == 0x29u || op == 0x2Du ||
                op == 0x2Eu)
            {
                usage.accRead = dest;
            }
            return usage;
        }

        if (op >= 0x3Cu)
        {
            const uint8_t special = static_cast<uint8_t>((upper & 3u) | ((upper >> 4) & 0x7Cu));
            const bool writesAcc = special <= 0x0Fu || (special >= 0x18u && special <= 0x1Cu) || special == 0x1Eu ||
                                   (special >= 0x20u && special <= 0x2Au) || (special >= 0x2Cu && special <= 0x2Eu);
            if (writesAcc)
            {
                addVfRead(usage, fs, dest);
                if (special <= 0x1Bu)
                    addVfRead(usage, ft, laneForComponent(special & 3u));
                else if ((special >= 0x28u && special <= 0x2Eu))
                    addVfRead(usage, ft, special == 0x2Eu ? 0xEu : dest);
                usage.accWrite = dest;
                if ((special >= 0x08u && special <= 0x0Fu) || special == 0x21u || special == 0x23u ||
                    special == 0x25u || special == 0x27u || special == 0x29u || special == 0x2Du)
                {
                    usage.accRead = dest;
                }
            }
            else if (special >= 0x10u && special <= 0x17u)
            {
                addVfRead(usage, fs, dest);
                addVfWrite(usage, ft, dest);
            }
            else if (special == 0x1Du)
            {
                addVfRead(usage, fs, dest);
                addVfWrite(usage, ft, dest);
            }
            else if (special == 0x1Fu)
            {
                addVfRead(usage, fs, 0xEu);
                addVfRead(usage, ft, 0x1u);
                usage.writesClip = true;
            }
            else if (special != 0x2Fu && special != 0x30u)
            {
                usage.reserved = true;
            }
            return usage;
        }

        usage.reserved = true;
        return usage;
    }

    constexpr InstructionUsage decodeLowerUsage(uint32_t lower, bool vu1)
    {
        InstructionUsage usage;
        if (lower == 0u || lower == 0x8000033Cu)
            return usage;

        const uint8_t opHi = static_cast<uint8_t>((lower >> 25) & 0x7Fu);
        const uint8_t vfT = vfTarget(lower);
        const uint8_t vfS = vfSource(lower);
        const uint8_t viT = viTarget(lower);
        const uint8_t viS = viSource(lower);
        const uint8_t viD = viDestination(lower);
        const uint8_t dest = destMask(lower);
        auto readVi = [&](uint8_t reg)
        {
            if (reg != 0u)
                usage.viRead |= static_cast<uint16_t>(1u << reg);
        };
        auto writeVi = [&](uint8_t reg)
        {
            if (reg != 0u)
                usage.viWrite |= static_cast<uint16_t>(1u << reg);
        };

        switch (opHi)
        {
        case 0x00:
            usage.pipeline = PipelineLsu;
            usage.latency = 4u;
            readVi(viS);
            addVfWrite(usage, vfT, dest);
            return usage;
        case 0x01:
            usage.pipeline = PipelineLsu;
            usage.latency = 1u;
            readVi(viT);
            addVfRead(usage, vfS, dest);
            return usage;
        case 0x04:
            usage.pipeline = PipelineLsu;
            usage.latency = 4u;
            readVi(viS);
            writeVi(viT);
            return usage;
        case 0x05:
            usage.pipeline = PipelineLsu;
            usage.latency = 1u;
            readVi(viS);
            readVi(viT);
            return usage;
        case 0x08:
        case 0x09:
            usage.pipeline = PipelineIalu;
            usage.latency = 1u;
            usage.delaysNextBranchRead = true;
            readVi(viS);
            writeVi(viT);
            return usage;
        case 0x10:
        case 0x12:
        case 0x13:
            usage.pipeline = PipelineIalu;
            usage.latency = 1u;
            usage.readsClip = true;
            writeVi(1u);
            return usage;
        case 0x11:
            usage.pipeline = PipelineFmac;
            usage.latency = kFmacLatency;
            usage.writesClip = true;
            return usage;
        case 0x14:
        case 0x16:
        case 0x17:
            usage.pipeline = PipelineIalu;
            usage.latency = 1u;
            writeVi(viT);
            return usage;
        case 0x15:
            usage.pipeline = PipelineFmac;
            usage.latency = kFmacLatency;
            return usage;
        case 0x18:
        case 0x1A:
        case 0x1B:
            usage.pipeline = PipelineIalu;
            usage.latency = 1u;
            readVi(viS);
            writeVi(viT);
            return usage;
        case 0x1C:
            usage.pipeline = PipelineIalu;
            usage.latency = 1u;
            usage.readsClip = true;
            writeVi(viT);
            return usage;
        case 0x20:
            usage.pipeline = PipelineBranch;
            return usage;
        case 0x21:
            usage.pipeline = PipelineBranch;
            usage.latency = 1u;
            writeVi(viT);
            return usage;
        case 0x24:
            usage.pipeline = PipelineBranch;
            readVi(viS);
            return usage;
        case 0x25:
            usage.pipeline = PipelineBranch;
            usage.latency = 1u;
            readVi(viS);
            writeVi(viT);
            return usage;
        case 0x28:
        case 0x29:
            usage.pipeline = PipelineBranch;
            readVi(viS);
            readVi(viT);
            return usage;
        case 0x2C:
        case 0x2D:
        case 0x2E:
        case 0x2F:
            usage.pipeline = PipelineBranch;
            readVi(viS);
            return usage;
        case 0x40:
            break;
        default:
            usage.reserved = true;
            return usage;
        }

        const uint8_t direct = static_cast<uint8_t>(lower & 0x3Fu);
        if (direct == 0x30u || direct == 0x31u || direct == 0x34u || direct == 0x35u)
        {
            usage.pipeline = PipelineIalu;
            usage.latency = 1u;
            usage.delaysNextBranchRead = true;
            readVi(viS);
            readVi(viT);
            writeVi(viD);
            return usage;
        }
        if (direct == 0x32u)
        {
            usage.pipeline = PipelineIalu;
            usage.latency = 1u;
            usage.delaysNextBranchRead = true;
            readVi(viS);
            writeVi(viT);
            return usage;
        }
        if (direct < 0x3Cu)
        {
            usage.reserved = true;
            return usage;
        }

        const uint8_t special = static_cast<uint8_t>((lower & 3u) | ((lower >> 4) & 0x7Cu));
        switch (special)
        {
        case 0x30:
        case 0x31:
            usage.pipeline = PipelineFmac;
            usage.latency = 4u;
            addVfRead(usage, vfS, special == 0x31u ? 0xFu : dest);
            addVfWrite(usage, vfT, dest);
            break;
        case 0x34:
        case 0x36:
            usage.pipeline = PipelineLsu;
            usage.latency = 4u;
            usage.viLatency = 1u;
            usage.delaysNextBranchRead = true;
            readVi(viS);
            writeVi(viS);
            addVfWrite(usage, vfT, dest);
            break;
        case 0x35:
        case 0x37:
            usage.pipeline = PipelineLsu;
            usage.latency = 1u;
            usage.delaysNextBranchRead = true;
            readVi(viT);
            writeVi(viT);
            addVfRead(usage, vfS, dest);
            break;
        case 0x38:
            usage.pipeline = PipelineFdiv;
            usage.latency = 7u;
            addVfRead(usage, vfS, laneForComponent((lower >> 21) & 3u));
            addVfRead(usage, vfT, laneForComponent((lower >> 23) & 3u));
            break;
        case 0x39:
            usage.pipeline = PipelineFdiv;
            usage.latency = 7u;
            addVfRead(usage, vfT, laneForComponent((lower >> 23) & 3u));
            break;
        case 0x3A:
            usage.pipeline = PipelineFdiv;
            usage.latency = 13u;
            addVfRead(usage, vfS, laneForComponent((lower >> 21) & 3u));
            addVfRead(usage, vfT, laneForComponent((lower >> 23) & 3u));
            break;
        case 0x3B:
            usage.pipeline = PipelineFdiv;
            usage.waitQ = true;
            break;
        case 0x3C:
            usage.pipeline = PipelineIalu;
            usage.latency = 1u;
            usage.delaysNextBranchRead = true;
            addVfRead(usage, vfS, laneForComponent((lower >> 21) & 3u));
            writeVi(viT);
            break;
        case 0x3D:
            usage.pipeline = PipelineFmac;
            usage.latency = 4u;
            readVi(viS);
            addVfWrite(usage, vfT, dest);
            break;
        case 0x3E:
            usage.pipeline = PipelineLsu;
            usage.latency = 4u;
            readVi(viS);
            writeVi(viT);
            break;
        case 0x3F:
            usage.pipeline = PipelineLsu;
            usage.latency = 1u;
            readVi(viS);
            readVi(viT);
            break;
        case 0x40:
        case 0x41:
            usage.pipeline = PipelineFmac;
            usage.latency = 4u;
            addVfWrite(usage, vfT, dest);
            break;
        case 0x42:
        case 0x43:
            usage.pipeline = PipelineIalu;
            usage.latency = 1u;
            addVfRead(usage, vfS, laneForComponent((lower >> 21) & 3u));
            break;
        case 0x64:
            if (!vu1)
            {
                usage.reserved = true;
                break;
            }
            usage.pipeline = PipelineFmac;
            usage.latency = 4u;
            addVfWrite(usage, vfT, dest);
            break;
        case 0x68:
        case 0x69:
            usage.pipeline = PipelineIalu;
            usage.latency = 1u;
            writeVi(viT);
            break;
        case 0x6C:
            if (!vu1)
            {
                usage.reserved = true;
                break;
            }
            usage.pipeline = PipelineXgkick;
            usage.latency = 2u;
            readVi(viS);
            break;
        case 0x70:
        case 0x71:
        case 0x72:
        case 0x73:
        case 0x74:
        case 0x75:
        case 0x76:
        case 0x77:
        case 0x78:
        case 0x79:
        case 0x7A:
        case 0x7C:
        case 0x7D:
            if (!vu1)
            {
                usage.reserved = true;
                break;
            }
            usage.pipeline = PipelineEfu;
            switch (special)
            {
            case 0x70:
                usage.latency = 11u;
                break;
            case 0x71:
            case 0x72:
            case 0x77:
                usage.latency = 18u;
                break;
            case 0x73:
                usage.latency = 24u;
                break;
            case 0x74:
            case 0x75:
            case 0x7C:
                usage.latency = 54u;
                break;
            case 0x76:
            case 0x78:
            case 0x7A:
                usage.latency = 12u;
                break;
            case 0x79:
                usage.latency = 29u;
                break;
            case 0x7D:
                usage.latency = 44u;
                break;
            default:
                break;
            }
            if (special >= 0x70u && special <= 0x73u)
                addVfRead(usage, vfS, 0xEu);
            else if (special == 0x74u)
                addVfRead(usage, vfS, 0xCu);
            else if (special == 0x75u)
                addVfRead(usage, vfS, 0xAu);
            else if (special == 0x76u)
                addVfRead(usage, vfS, 0xFu);
            else
                addVfRead(usage, vfS, laneForComponent((lower >> 21) & 3u));
            break;
        case 0x7B:
            if (!vu1)
            {
                usage.reserved = true;
                break;
            }
            usage.pipeline = PipelineEfu;
            usage.waitP = true;
            break;
        default:
            usage.reserved = true;
            break;
        }
        return usage;
    }

    constexpr void addDependency(DecodedInstructionPair &decoded, uint32_t index)
    {
        for (uint32_t i = 0; i < decoded.readDependencyCount; ++i)
            if (decoded.readDependencies[i] == index)
                return;
        decoded.readDependencies[decoded.readDependencyCount++] = static_cast<uint8_t>(index);
    }

    constexpr DecodedInstructionPair decodeInstructionPair(uint32_t lower, uint32_t upper, bool vu1 = true)
    {
        DecodedInstructionPair decoded;
        decoded.lower = lower;
        decoded.upper = upper;
        decoded.iBit = (decoded.upper & 0x80000000u) != 0u;
        decoded.eBit = (decoded.upper & 0x40000000u) != 0u;
        decoded.mBit = (decoded.upper & 0x20000000u) != 0u;
        decoded.dBit = (decoded.upper & 0x10000000u) != 0u;
        decoded.tBit = (decoded.upper & 0x08000000u) != 0u;
        decoded.upperUsage = decodeUpperUsage(decoded.upper);
        if (!decoded.iBit)
            decoded.lowerUsage = decodeLowerUsage(decoded.lower, vu1);

        const uint8_t upperWriteReg = decoded.upperUsage.vfWrite.reg;
        if (upperWriteReg != 0u && decoded.lowerUsage.vfWrite.reg == upperWriteReg)
            decoded.suppressedLowerVf = upperWriteReg;

        for (uint32_t unit = 0; unit != 2; ++unit)
        {
            const auto &usage = unit == 0 ? decoded.upperUsage : decoded.lowerUsage;
            for (uint32_t i = 0; i < usage.vfReadCount; ++i)
            {
                const auto &access = usage.vfRead[i];
                for (uint32_t component = 0; component < 4u; ++component)
                    if ((access.lanes & laneForComponent(component)) != 0u)
                        addDependency(decoded, 4u * access.reg + component);
            }
            for (uint32_t regs = usage.viRead & ~1u; regs != 0u; regs &= regs - 1u)
                addDependency(decoded, kViReadyBase + std::countr_zero(regs));
            for (uint32_t component = 0; component < 4u; ++component)
                if ((usage.accRead & laneForComponent(component)) != 0u)
                    addDependency(decoded, kAccReadyBase + component);
        }
        return decoded;
    }

}
