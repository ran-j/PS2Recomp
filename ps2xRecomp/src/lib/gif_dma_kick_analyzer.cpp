#include "ps2recomp/gif_dma_kick_analyzer.h"

#include "ps2recomp/instructions.h"
#include "ps2recomp/r5900_decoder.h"

#include <algorithm>
#include <ostream>
#include <sstream>

namespace ps2recomp
{
    namespace
    {
        uint32_t add32(uint32_t lhs, uint32_t rhs)
        {
            return lhs + rhs;
        }

        std::string formatU32Literal(uint32_t value)
        {
            std::ostringstream ss;
            ss << "0x" << std::hex << value << "u";
            return ss.str();
        }

        std::string gprU32Expression(uint32_t reg)
        {
            std::ostringstream ss;
            ss << "GPR_U32(ctx, " << std::dec << reg << ")";
            return ss.str();
        }

        std::string gifDmaKickTempName(uint32_t address, size_t slot)
        {
            std::ostringstream ss;
            ss << "gifDmaKickValue_" << std::hex << address << "_" << std::dec << slot;
            return ss.str();
        }

        bool isReturnWithDelaySlot(const Instruction &inst)
        {
            return inst.opcode == OPCODE_SPECIAL &&
                   inst.function == SPECIAL_JR &&
                   inst.rs == 31u &&
                   inst.hasDelaySlot;
        }

        bool tryMatchGifDmaStore(size_t instructionIndex,
                                 const Instruction &inst,
                                 const MemoryAccessHint &hint,
                                 const ConstantRegisterState &constants,
                                 uint32_t target,
                                 size_t slot,
                                 GifDmaKickPlan &plan)
        {
            if (inst.opcode != OPCODE_SW || !hint.hasAddress || hint.address != target)
                return false;

            plan.storeIndices[slot] = instructionIndex;
            uint32_t constantValue = 0u;
            if (constants.read(inst.rt, constantValue))
            {
                plan.values[slot] = formatU32Literal(constantValue);
            }
            else
            {
                const std::string tempName = gifDmaKickTempName(inst.address, slot);
                plan.values[slot] = tempName;
                plan.captureExpressions[slot] = gprU32Expression(inst.rt);
                plan.captures[slot] = true;
            }

            return true;
        }
    }

    ConstantRegisterState::ConstantRegisterState()
    {
        clear();
    }

    void ConstantRegisterState::clear()
    {
        known.fill(false);
        values.fill(0u);
        known[0] = true;
    }

    bool ConstantRegisterState::read(uint32_t reg, uint32_t &value) const
    {
        if (reg >= known.size() || !known[reg])
            return false;

        value = values[reg];
        return true;
    }

    void ConstantRegisterState::write(uint32_t reg, uint32_t value)
    {
        if (reg == 0 || reg >= known.size())
            return;

        known[reg] = true;
        values[reg] = value;
    }

    void ConstantRegisterState::invalidate(uint32_t reg)
    {
        if (reg == 0 || reg >= known.size())
            return;

        known[reg] = false;
        values[reg] = 0u;
    }

    bool GifDmaKickPlan::suppresses(size_t index) const
    {
        return valid && std::find(storeIndices.begin(), storeIndices.end(), index) != storeIndices.end();
    }

    bool GifDmaKickPlan::completesAt(size_t index) const
    {
        return valid && index == endIndex;
    }

    size_t GifDmaKickPlan::slotFor(size_t index) const
    {
        for (size_t i = 0; i < storeIndices.size(); ++i)
        {
            if (storeIndices[i] == index)
                return i;
        }
        return storeIndices.size();
    }

    bool isDirectMemoryAccess(const Instruction &inst)
    {
        switch (inst.opcode)
        {
        case OPCODE_LB:
        case OPCODE_LH:
        case OPCODE_LW:
        case OPCODE_LBU:
        case OPCODE_LHU:
        case OPCODE_LWU:
        case OPCODE_LQ:
        case OPCODE_LD:
        case OPCODE_LWC1:
        case OPCODE_LDC2:
        case OPCODE_SB:
        case OPCODE_SH:
        case OPCODE_SW:
        case OPCODE_SQ:
        case OPCODE_SD:
        case OPCODE_SWC1:
        case OPCODE_SDC2:
            return true;
        default:
            return false;
        }
    }

    MemoryAccessHint resolveMemoryAccessHint(const Instruction &inst, const ConstantRegisterState &constants)
    {
        MemoryAccessHint hint{};
        if (!isDirectMemoryAccess(inst))
            return hint;

        uint32_t base = 0u;
        if (!constants.read(inst.rs, base))
            return hint;

        hint.hasAddress = true;
        hint.address = add32(base, inst.simmediate);
        return hint;
    }

    void updateConstantRegisters(const Instruction &inst, ConstantRegisterState &constants)
    {
        uint32_t lhs = 0u;
        uint32_t rhs = 0u;

        switch (inst.opcode)
        {
        case OPCODE_LUI:
            constants.write(inst.rt, inst.immediate << 16);
            return;
        case OPCODE_ORI:
            if (constants.read(inst.rs, lhs))
                constants.write(inst.rt, lhs | (inst.immediate & 0xFFFFu));
            else
                constants.invalidate(inst.rt);
            return;
        case OPCODE_ADDIU:
            if (constants.read(inst.rs, lhs))
                constants.write(inst.rt, add32(lhs, inst.simmediate));
            else
                constants.invalidate(inst.rt);
            return;
        case OPCODE_ANDI:
            if (constants.read(inst.rs, lhs))
                constants.write(inst.rt, lhs & (inst.immediate & 0xFFFFu));
            else
                constants.invalidate(inst.rt);
            return;
        case OPCODE_XORI:
            if (constants.read(inst.rs, lhs))
                constants.write(inst.rt, lhs ^ (inst.immediate & 0xFFFFu));
            else
                constants.invalidate(inst.rt);
            return;
        case OPCODE_LB:
        case OPCODE_LH:
        case OPCODE_LWL:
        case OPCODE_LW:
        case OPCODE_LBU:
        case OPCODE_LHU:
        case OPCODE_LWR:
        case OPCODE_LWU:
        case OPCODE_LDL:
        case OPCODE_LDR:
        case OPCODE_LQ:
        case OPCODE_LL:
        case OPCODE_LD:
            constants.invalidate(inst.rt);
            return;
        case OPCODE_SC:
            constants.invalidate(inst.rt);
            return;
        case OPCODE_SPECIAL:
            switch (inst.function)
            {
            case SPECIAL_ADDU:
            case SPECIAL_DADDU:
                if (constants.read(inst.rs, lhs) && constants.read(inst.rt, rhs))
                    constants.write(inst.rd, add32(lhs, rhs));
                else
                    constants.invalidate(inst.rd);
                return;
            case SPECIAL_OR:
                if (constants.read(inst.rs, lhs) && constants.read(inst.rt, rhs))
                    constants.write(inst.rd, lhs | rhs);
                else
                    constants.invalidate(inst.rd);
                return;
            default:
                break;
            }
            break;
        default:
            break;
        }

        if (inst.modificationInfo.modifiesGPR || inst.modificationInfo.modifiesControl)
            constants.clear();
    }

    std::string gifDmaKickCall(const GifDmaKickPlan &plan)
    {
        std::ostringstream ss;
        ss << "runtime->kickGifDmaChainFromMMIO(rdram, ctx, "
           << plan.values[0] << ", "
           << plan.values[1] << ", "
           << plan.values[2] << ", "
           << plan.values[3] << ");";
        return ss.str();
    }

    void emitGifDmaCapture(std::ostream &out, const GifDmaKickPlan &plan, size_t slot, std::string_view indent)
    {
        if (slot >= plan.captures.size() || !plan.captures[slot])
            return;

        out << indent << "uint32_t " << plan.values[slot] << " = " << plan.captureExpressions[slot] << ";\n";
    }

    std::string gifDmaDelaySlotOverride(const Instruction &delaySlot, const GifDmaKickPlan &plan, bool emitComments)
    {
        std::ostringstream code;
        const size_t slot = plan.slotFor(plan.endIndex);
        if (emitComments)
        {
            code << "// 0x" << std::hex << delaySlot.address << ": 0x" << delaySlot.raw << std::dec;
            std::string disassembly = R5900Decoder::disassembleInstruction(delaySlot);
            if (!disassembly.empty())
                code << "  " << disassembly;
            code << " (Delay Slot)\n";
        }

        if (slot < plan.captures.size() && plan.captures[slot])
            code << "uint32_t " << plan.values[slot] << " = " << plan.captureExpressions[slot] << ";\n";
        code << gifDmaKickCall(plan);
        return code.str();
    }

    GifDmaKickPlan tryBuildGifDmaKickPlan(const std::vector<Instruction> &instructions,
                                          size_t startIndex,
                                          const ConstantRegisterState &constants,
                                          const std::unordered_set<uint32_t> &internalTargets)
    {
        static constexpr std::array<uint32_t, 4> kTargets = {
            0x1000E020u, // D_PCR
            0x1000E010u, // D_STAT
            0x1000A030u, // GIF TADR
            0x1000A000u, // GIF CHCR
        };
        static constexpr size_t kMaxScanInstructions = 32u;

        GifDmaKickPlan plan{};
        if (startIndex >= instructions.size())
            return plan;

        ConstantRegisterState scanConstants = constants;
        size_t matched = 0;
        const size_t scanEnd = std::min(instructions.size(), startIndex + kMaxScanInstructions);

        for (size_t j = startIndex; j < scanEnd; ++j)
        {
            const Instruction &inst = instructions[j];
            if (j != startIndex && matched > 0 && internalTargets.contains(inst.address))
                return {};

            const MemoryAccessHint hint = resolveMemoryAccessHint(inst, scanConstants);
            if (isDirectMemoryAccess(inst))
            {
                if (matched >= kTargets.size() ||
                    !tryMatchGifDmaStore(j, inst, hint, scanConstants, kTargets[matched], matched, plan))
                {
                    return {};
                }

                ++matched;
                updateConstantRegisters(inst, scanConstants);
                if (matched == kTargets.size())
                {
                    plan.valid = true;
                    plan.endIndex = j;
                    return plan;
                }
                continue;
            }

            if (isReturnWithDelaySlot(inst))
            {
                const size_t delayIndex = j + 1u;
                if (matched != kTargets.size() - 1u ||
                    delayIndex >= instructions.size() ||
                    instructions[delayIndex].address != inst.address + 4u ||
                    internalTargets.contains(instructions[delayIndex].address))
                {
                    return {};
                }

                const Instruction &delayInst = instructions[delayIndex];
                const MemoryAccessHint delayHint = resolveMemoryAccessHint(delayInst, scanConstants);
                if (!tryMatchGifDmaStore(delayIndex, delayInst, delayHint, scanConstants, kTargets[matched], matched, plan))
                    return {};

                plan.valid = true;
                plan.completesInDelaySlot = true;
                plan.branchIndex = j;
                plan.endIndex = delayIndex;
                return plan;
            }

            if (inst.hasDelaySlot || inst.isBranch || inst.isJump || inst.modificationInfo.modifiesControl)
                return {};

            updateConstantRegisters(inst, scanConstants);
        }

        return {};
    }
}
