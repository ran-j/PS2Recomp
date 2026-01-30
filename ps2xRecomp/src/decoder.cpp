#include "ps2recomp/decoder.h"

#include <rabbitizer.h>
#include <iostream>

namespace ps2recomp
{
    namespace
    {
        void decodeRType(Instruction &inst)
        {
            if (inst.rd != 0)
            {
                inst.modificationInfo.modifiesGPR = true;
            }
        }

        void decodeIType(Instruction &inst)
        {
            if (!inst.isStore && !inst.isBranch && inst.rt != 0)
            {
                inst.modificationInfo.modifiesGPR = true;
            }
        }

        void decodeJType(Instruction &inst)
        {
            inst.isJump = true;
            inst.hasDelaySlot = true;
            inst.modificationInfo.modifiesControl = true; // PC
            if (inst.opcode == OPCODE_JAL)
            {
                inst.isCall = true;
                inst.modificationInfo.modifiesGPR = true; // $ra (r[31])
            }
        }

        void decodeSpecial(Instruction &inst)
        {
            switch (inst.function)
            {
            case SPECIAL_JR:
                inst.isJump = true;
                inst.hasDelaySlot = true;
                inst.modificationInfo.modifiesControl = true; // PC
                if (inst.rs == 31)
                {
                    inst.isReturn = true;
                }
                break;
            case SPECIAL_JALR:
                inst.isJump = true;
                inst.isCall = true;
                inst.hasDelaySlot = true;
                inst.modificationInfo.modifiesControl = true;
                if (inst.rd != 0)
                {
                    inst.modificationInfo.modifiesGPR = true; // Link register
                }
                break;
            case SPECIAL_SYSCALL:
            case SPECIAL_BREAK:
                inst.modificationInfo.modifiesControl = true; // Control flow changes
                break;
            case SPECIAL_MFHI:
            case SPECIAL_MFLO:
                if (inst.rd != 0)
                {
                    inst.modificationInfo.modifiesGPR = true;
                }
                break;
            case SPECIAL_MTHI:
            case SPECIAL_MTLO:
                inst.modificationInfo.modifiesControl = true;
                break;
            case SPECIAL_MULT:
            case SPECIAL_MULTU:
            case SPECIAL_DIV:
            case SPECIAL_DIVU:
                inst.modificationInfo.modifiesControl = true;
                break;
            default:
                decodeRType(inst);
                break;
            }
        }

        void decodeRegimm(Instruction &inst)
        {
            inst.isBranch = true;
            inst.hasDelaySlot = true;
            inst.modificationInfo.modifiesControl = true; // PC potentially
            if (inst.rt == REGIMM_BGEZAL || inst.rt == REGIMM_BLTZAL ||
                inst.rt == REGIMM_BGEZALL || inst.rt == REGIMM_BLTZALL)
            {
                inst.isCall = true;
                inst.modificationInfo.modifiesGPR = true; // Link register ($ra)
            }
        }

        void decodeMMI0(Instruction &inst)
        {
            inst.mmiType = 0;
            inst.mmiFunction = static_cast<uint8_t>(inst.sa);
        }

        void decodeMMI1(Instruction &inst)
        {
            inst.mmiType = 1;
            inst.mmiFunction = static_cast<uint8_t>(inst.sa);
        }

        void decodeMMI2(Instruction &inst)
        {
            inst.mmiType = 2;
            inst.mmiFunction = static_cast<uint8_t>(inst.sa);
        }

        void decodeMMI3(Instruction &inst)
        {
            inst.mmiType = 3;
            inst.mmiFunction = static_cast<uint8_t>(inst.sa);
        }

        void decodePMFHL(Instruction &inst)
        {
            uint32_t saField = inst.sa;

            switch (saField)
            {
            case PMFHL_LW:
                inst.pmfhlVariation = PMFHL_LW;
                break;
            case PMFHL_UW:
                inst.pmfhlVariation = PMFHL_UW;
                break;
            case PMFHL_SLW:
                inst.pmfhlVariation = PMFHL_SLW;
                break;
            case PMFHL_LH:
                inst.pmfhlVariation = PMFHL_LH;
                break;
            case PMFHL_SH:
                inst.pmfhlVariation = PMFHL_SH;
                break;
            default:
                inst.pmfhlVariation = 0xFF;
                break;
            }
        }

        void decodeMMI(Instruction &inst)
        {
            inst.isMMI = true;
            inst.isMultimedia = true;

            inst.isMMI = true;
            inst.isMultimedia = true;
            inst.mmiFunction = static_cast<uint8_t>(inst.sa);

            // Determine MMI type groups
            if (inst.function == MMI_MMI0)
            {
                inst.mmiType = 0;
                decodeMMI0(inst);
            }
            else if (inst.function == MMI_MMI1)
            {
                inst.mmiType = 1;
                decodeMMI1(inst);
            }
            else if (inst.function == MMI_MMI2)
            {
                inst.mmiType = 2;
                decodeMMI2(inst);
            }
            else if (inst.function == MMI_MMI3)
            {
                inst.mmiType = 3;
                decodeMMI3(inst);
            }
            else
            {
                // Default to MMI0 semantics for standalone ops like PADDW
                inst.mmiType = 0;
                decodeRType(inst);
            }

            // Mark HI/LO touching ops as control modifications
            switch (inst.function)
            {
            case MMI_MADD:
            case MMI_MADDU:
            case MMI_MSUB:
            case MMI_MSUBU:
            case MMI_MADD1:
            case MMI_MADDU1:
            case MMI_MULT1:
            case MMI_MULTU1:
            case MMI_DIV1:
            case MMI_DIVU1:
            case MMI_PMTHL:
                inst.modificationInfo.modifiesControl = true;
                break;
            default:
                break;
            }
        }

        void decodeCOP0(Instruction &inst)
        {
            uint8_t format = inst.rs;
            inst.modificationInfo.modifiesControl = true; // Assume COP0 always modifies some control state
            if (format == COP0_MF && inst.rt != 0)
            {
                inst.modificationInfo.modifiesGPR = true;
            }
            else if (format == COP0_CO && inst.function == COP0_CO_ERET)
            {
                inst.isReturn = true;
                inst.hasDelaySlot = false;
            }
            else if (format == COP0_BC)
            {
                inst.isBranch = true;
                inst.hasDelaySlot = true;
            }
        }

        void decodeCOP1(Instruction &inst)
        {
            uint8_t format = inst.rs;
            if (format == COP1_MF || format == COP1_CF)
            {
                if (inst.rt != 0)
                    inst.modificationInfo.modifiesGPR = true;
            }
            else if (format == COP1_BC)
            {
                inst.isBranch = true;
                inst.hasDelaySlot = true;
                inst.modificationInfo.modifiesControl = true;
            }
            else if (format == COP1_S || format == COP1_W || format == COP1_L)
            {
                inst.modificationInfo.modifiesFPR = true;
            }
            if (format == COP1_CT || (format == COP1_S && inst.function >= COP1_S_C_F))
            {
                inst.modificationInfo.modifiesControl = true;
            }
        }

        void decodeCOP2(Instruction &inst)
        {
            uint8_t format = inst.rs;
            inst.isVU = true;
            inst.isMultimedia = true;

            switch (format)
            {
            case COP2_QMFC2:
            case COP2_CFC2:
                if (inst.rt != 0)
                    inst.modificationInfo.modifiesGPR = true;
                break;
            case COP2_QMTC2:
                inst.modificationInfo.modifiesVFR = true;
                break;
            case COP2_CTC2:
                inst.modificationInfo.modifiesControl = true;
                break;
            case COP2_BC:
                inst.isBranch = true;
                inst.hasDelaySlot = true;
                inst.modificationInfo.modifiesControl = true;
                break;
            case COP2_CO:
            case COP2_CO + 1:
            case COP2_CO + 2:
            case COP2_CO + 3:
            case COP2_CO + 4:
            case COP2_CO + 5:
            case COP2_CO + 6:
            case COP2_CO + 7:
            case COP2_CO + 8:
            case COP2_CO + 9:
            case COP2_CO + 10:
            case COP2_CO + 11:
            case COP2_CO + 12:
            case COP2_CO + 13:
            case COP2_CO + 14:
            case COP2_CO + 15:
            {
                uint8_t vu_func = inst.function;

                if (vu_func == VU0_S2_VDIV || vu_func == VU0_S2_VSQRT || vu_func == VU0_S2_VRSQRT)
                {
                    inst.vectorInfo.fsf = (inst.raw >> 10) & 0x3; // Extract bits 10-11
                    inst.vectorInfo.ftf = (inst.raw >> 8) & 0x3;  // Extract bits 8-9
                }

                inst.vectorInfo.vectorField = (inst.raw >> 21) & 0xF;

                inst.modificationInfo.modifiesVFR = true;     // Default: Modifies Vector Float Reg
                inst.modificationInfo.modifiesControl = true; // Default: Modifies Flags/Special Regs (Q, P, I, MAC, Clip...)

                if (vu_func >= 0x3C) // Special2 Table
                {
                    switch (vu_func)
                    {
                    case VU0_S2_VDIV:
                    case VU0_S2_VSQRT:
                    case VU0_S2_VRSQRT:
                        inst.vectorInfo.usesQReg = true;
                        break;
                    case VU0_S2_VMTIR:
                        inst.modificationInfo.modifiesVFR = false;
                        inst.modificationInfo.modifiesVIC = true;
                        break;
                    case VU0_S2_VMFIR:
                        inst.modificationInfo.modifiesVIR = false;
                        break;
                    case VU0_S2_VILWR:
                        inst.modificationInfo.modifiesVFR = false;
                        inst.modificationInfo.modifiesVIR = true;
                        inst.isLoad = true;
                        break;
                    case VU0_S2_VISWR:
                        inst.modificationInfo.modifiesVFR = false;
                        inst.modificationInfo.modifiesVIR = false;
                        inst.isStore = true;
                        inst.modificationInfo.modifiesMemory = true;
                        break;

                    case VU0_S2_VRINIT:
                    case VU0_S2_VRXOR:
                        inst.modificationInfo.modifiesVFR = false;
                        break;
                    case VU0_S2_VRGET:
                        inst.modificationInfo.modifiesControl = false;
                        break;
                    case VU0_S2_VRNEXT:
                        inst.modificationInfo.modifiesControl = true;
                        inst.modificationInfo.modifiesVFR = false;
                        break;
                    case VU0_S2_VABS:
                    case VU0_S2_VMOVE:
                    case VU0_S2_VMR32:
                        inst.modificationInfo.modifiesControl = false;
                        break;
                    case VU0_S2_VNOP:
                        inst.modificationInfo.modifiesVFR = false;
                        inst.modificationInfo.modifiesControl = false;
                        break;
                    case VU0_S2_VCLIPw:
                        inst.modificationInfo.modifiesVFR = false;
                        break;
                    }
                }
                else // Special1 Table
                {
                    if (vu_func >= VU0_S1_VIADD && vu_func <= VU0_S1_VIOR)
                    {
                        inst.modificationInfo.modifiesVFR = false;
                        inst.modificationInfo.modifiesVIR = true;
                    }
                    if (vu_func == VU0_S1_VIADDI)
                    {
                        inst.modificationInfo.modifiesVFR = false;
                        inst.modificationInfo.modifiesVIR = true;
                    }
                    if (vu_func == VU0_S2_VMFIR)
                    {
                        inst.modificationInfo.modifiesVIR = false;
                    }
                    if (vu_func == VU0_S2_VMTIR)
                    {
                        inst.modificationInfo.modifiesVFR = false;
                        inst.modificationInfo.modifiesVIC = true;
                    }
                    if (vu_func == VU0_S2_VILWR)
                    {
                        inst.modificationInfo.modifiesVFR = false;
                        inst.modificationInfo.modifiesVIR = true;
                        inst.isLoad = true;
                    }
                    if (vu_func == VU0_S2_VISWR)
                    {
                        inst.modificationInfo.modifiesVFR = false;
                        inst.modificationInfo.modifiesVIR = false;
                        inst.isStore = true;
                        inst.modificationInfo.modifiesMemory = true;
                    }
                    if (vu_func == VU0_S2_VDIV || vu_func == VU0_S2_VSQRT || vu_func == VU0_S2_VRSQRT)
                    {
                        inst.vectorInfo.usesQReg = true;
                    }
                }
                break;
            }
            }
        }

    } // namespace

    Instruction decodeInstruction(uint32_t address, uint32_t rawInstruction)
    {
        Instruction inst{};

        RabbitizerInstruction rabInstr;
        RabbitizerInstruction_init(&rabInstr, rawInstruction, address);

        inst.address = address;
        inst.raw = rawInstruction;
        inst.opcode = RAB_INSTR_GET_opcode(&rabInstr);
        inst.rs = RAB_INSTR_GET_rs(&rabInstr);
        inst.rt = RAB_INSTR_GET_rt(&rabInstr);
        inst.rd = RAB_INSTR_GET_rd(&rabInstr);
        inst.sa = RAB_INSTR_GET_sa(&rabInstr);
        inst.function = RAB_INSTR_GET_function(&rabInstr);
        inst.immediate = RAB_INSTR_GET_immediate(&rabInstr);
        inst.simmediate = static_cast<uint32_t>(RabbitizerInstruction_getProcessedImmediate(&rabInstr));
        inst.target = RabbitizerInstruction_getInstrIndexAsVram(&rabInstr);

        inst.vectorInfo.vectorField = 0xF; // All fields (xyzw)
        inst.hasDelaySlot = RabbitizerInstruction_hasDelaySlot(&rabInstr);
        inst.isCall = RabbitizerInstruction_isFunctionCall(&rabInstr);
        inst.isReturn = RabbitizerInstruction_isReturn(&rabInstr);
        inst.isMMI = (inst.opcode == OPCODE_MMI);
        inst.isVU = (inst.opcode == OPCODE_COP2);

        switch (inst.opcode)
        {
        case OPCODE_SPECIAL:
            decodeSpecial(inst);
            break;

        case OPCODE_REGIMM:
            decodeRegimm(inst);
            break;

        case OPCODE_J:
        case OPCODE_JAL:
            decodeJType(inst);
            break;

        case OPCODE_BEQ:
        case OPCODE_BNE:
        case OPCODE_BLEZ:
        case OPCODE_BGTZ:
        case OPCODE_BEQL:
        case OPCODE_BNEL:
        case OPCODE_BLEZL:
        case OPCODE_BGTZL:
            decodeIType(inst);
            inst.isBranch = true;
            inst.hasDelaySlot = true;
            inst.modificationInfo.modifiesControl = true; // PC potentially
            break;

        case OPCODE_DADDI:
        case OPCODE_DADDIU:
            decodeIType(inst);
            if (inst.rt != 0)
                inst.modificationInfo.modifiesGPR = true;
            break;

        case OPCODE_MMI:
            decodeMMI(inst);
            break;

        case OPCODE_LQ:
            decodeIType(inst);
            inst.isLoad = true;
            inst.isMultimedia = true;
            break;

        case OPCODE_SQ:
            decodeIType(inst);
            inst.isStore = true;
            inst.isMultimedia = true;
            inst.modificationInfo.modifiesMemory = true;
            break;

        case OPCODE_LB:
        case OPCODE_LH:
        case OPCODE_LW:
        case OPCODE_LBU:
        case OPCODE_LHU:
        case OPCODE_LWU:
        case OPCODE_LD:
            inst.isLoad = true;
            if (inst.rt != 0)
                inst.modificationInfo.modifiesGPR = true;
            break;

        case OPCODE_LWL:
        case OPCODE_LWR:
        case OPCODE_LDL:
        case OPCODE_LDR:
            inst.isLoad = true;
            if (inst.rt != 0)
                inst.modificationInfo.modifiesGPR = true;
            inst.modificationInfo.modifiesMemory = true;
            break;

        case OPCODE_LL:
        case OPCODE_LLD:
            decodeIType(inst);
            inst.isLoad = true;
            if (inst.rt != 0)
            {
                inst.modificationInfo.modifiesGPR = true;
            }
            inst.modificationInfo.modifiesControl = true;
            break;

        case OPCODE_LWC1:
            inst.isLoad = true;
            inst.modificationInfo.modifiesFPR = true;
            break;

        case OPCODE_LDC1:
        case OPCODE_LWC2:
        case OPCODE_LDC2:
            inst.isLoad = true;
            inst.isVU = true;
            inst.modificationInfo.modifiesVFR = true;
            break;

        case OPCODE_SB:
        case OPCODE_SH:
        case OPCODE_SW:
        case OPCODE_SD:
            inst.isStore = true;
            inst.modificationInfo.modifiesMemory = true;
            break;

        case OPCODE_SWL:
        case OPCODE_SWR:
        case OPCODE_SDL:
        case OPCODE_SDR:
            inst.isStore = true;
            inst.modificationInfo.modifiesMemory = true;
            break;

        case OPCODE_SWC1:
            inst.isStore = true;
            inst.modificationInfo.modifiesMemory = true;
            break;

        case OPCODE_SDC1:
        case OPCODE_SWC2:
        case OPCODE_SDC2:
            inst.isStore = true;
            inst.isVU = true;
            inst.modificationInfo.modifiesMemory = true;
            break;

        case OPCODE_SC:
        case OPCODE_SCD:
            inst.isStore = true;
            inst.modificationInfo.modifiesMemory = true;
            if (inst.rt != 0)
                inst.modificationInfo.modifiesGPR = true;
            inst.modificationInfo.modifiesControl = true;
            break;

        case OPCODE_ADDI:
        case OPCODE_ADDIU:
        case OPCODE_SLTI:
        case OPCODE_SLTIU:
        case OPCODE_ANDI:
        case OPCODE_ORI:
        case OPCODE_XORI:
        case OPCODE_LUI:
            decodeIType(inst);
            if (inst.rt != 0)
                inst.modificationInfo.modifiesGPR = true;
            break;

        case OPCODE_CACHE:
            decodeIType(inst);
            inst.modificationInfo.modifiesControl = true;
            break;

        case OPCODE_PREF:
            decodeIType(inst);
            break;

        case OPCODE_COP0:
            decodeCOP0(inst);
            break;

        case OPCODE_COP1:
            decodeCOP1(inst);
            break;

        case OPCODE_COP2:
            decodeCOP2(inst);
            break;

        default:
            decodeIType(inst);
            break;
        }

        if (inst.isMMI || inst.isVU)
        {
            inst.isMultimedia = true;
            inst.vectorInfo.isVector = inst.isVU;
        }

        RabbitizerInstruction_destroy(&rabInstr);
        return inst;
    }

    uint32_t getBranchTarget(const Instruction &inst)
    {
        if (!inst.isBranch)
        {
            return 0;
        }

        int32_t offset = inst.simmediate << 2;
        return inst.address + 4 + offset;
    }

    uint32_t getJumpTarget(const Instruction &inst)
    {
        if (!inst.isJump)
        {
            return 0;
        }

        if (inst.opcode == OPCODE_SPECIAL &&
            (inst.function == SPECIAL_JR || inst.function == SPECIAL_JALR))
        {
            return 0;
        }

        if (inst.opcode == OPCODE_J || inst.opcode == OPCODE_JAL)
        {
            return inst.target;
        }

        return 0;
    }

} // namespace ps2recomp
