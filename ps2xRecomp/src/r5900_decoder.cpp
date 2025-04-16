#include "ps2recomp/r5900_decoder.h"

namespace ps2recomp
{

    R5900Decoder::R5900Decoder()
    {
    }

    R5900Decoder::~R5900Decoder()
    {
    }

    Instruction R5900Decoder::decodeInstruction(uint32_t address, uint32_t rawInstruction)
    {
        Instruction inst;

        inst.address = address;
        inst.raw = rawInstruction;
        inst.opcode = OPCODE(rawInstruction);
        inst.rs = RS(rawInstruction);
        inst.rt = RT(rawInstruction);
        inst.rd = RD(rawInstruction);
        inst.sa = SA(rawInstruction);
        inst.function = FUNCTION(rawInstruction);
        inst.immediate = IMMEDIATE(rawInstruction);
        inst.target = TARGET(rawInstruction);

        inst.isMMI = false;
        inst.isVU = false;
        inst.isBranch = false;
        inst.isJump = false;
        inst.isCall = false;
        inst.isReturn = false;
        inst.hasDelaySlot = false;
        inst.isMultimedia = false;

        switch (inst.opcode)
        {
        case OPCODE_SPECIAL:
            decodeSpecial(inst);
            break;

        case OPCODE_REGIMM:
            decodeRegimm(inst);
            break;

        case OPCODE_J:
            decodeJType(inst);
            inst.isJump = true;
            inst.hasDelaySlot = true;
            break;

        case OPCODE_JAL:
            decodeJType(inst);
            inst.isJump = true;
            inst.isCall = true;
            inst.hasDelaySlot = true;
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
            break;

        case OPCODE_MMI:
            decodeMMI(inst);
            inst.isMMI = true;
            inst.isMultimedia = true;
            break;

        case OPCODE_LQ:
            decodeIType(inst);
            inst.isLoad = true;
            inst.isMultimedia = true; // 128-bit load
            break;

        case OPCODE_SQ:
            decodeIType(inst);
            inst.isStore = true;
            inst.isMultimedia = true; // 128-bit store
            break;

        case OPCODE_LB:
        case OPCODE_LH:
        case OPCODE_LWL:
        case OPCODE_LW:
        case OPCODE_LBU:
        case OPCODE_LHU:
        case OPCODE_LWR:
        case OPCODE_LWU:
        case OPCODE_LD:
        case OPCODE_LDL:
        case OPCODE_LDR:
        case OPCODE_LL:
        case OPCODE_LWC1:
        case OPCODE_LDC1:
        case OPCODE_LWC2:
        case OPCODE_LDC2:
            decodeIType(inst);
            inst.isLoad = true;
            break;

        case OPCODE_SB:
        case OPCODE_SH:
        case OPCODE_SWL:
        case OPCODE_SW:
        case OPCODE_SWR:
        case OPCODE_SD:
        case OPCODE_SDL:
        case OPCODE_SDR:
        case OPCODE_SC:
        case OPCODE_SWC1:
        case OPCODE_SDC1:
        case OPCODE_SWC2:
        case OPCODE_SDC2:
        case OPCODE_SCD:
            decodeIType(inst);
            inst.isStore = true;
            break;

        case OPCODE_COP0:
            decodeCOP0(inst);
            break;

        case OPCODE_COP1:
            decodeCOP1(inst);
            break;

        case OPCODE_COP2:
            decodeCOP2(inst);
            inst.isVU = true;
            inst.isMultimedia = true;
            break;

        case OPCODE_PREF:
        case OPCODE_CACHE:
            // Prefetch and cache operations
            decodeIType(inst);
            break;

        default:
            // Default to I-type for most other instructions
            decodeIType(inst);
            break;
        }

        return inst;
    }

    void R5900Decoder::decodeRType(Instruction &inst) const
    {
        // R-type instructions already have all fields set correctly
    }

    void R5900Decoder::decodeIType(Instruction &inst) const
    {
        // I-type instructions already have all fields set correctly
    }

    void R5900Decoder::decodeJType(Instruction &inst) const
    {
        // J-type instructions already have all fields set correctly
    }

    void R5900Decoder::decodeSpecial(Instruction &inst) const
    {
        switch (inst.function)
        {
        case SPECIAL_JR:
            inst.isJump = true;
            inst.hasDelaySlot = true;
            if (inst.rs == 31)
            {
                // jr $ra is typically a return
                inst.isReturn = true;
            }
            break;

        case SPECIAL_JALR:
            inst.isJump = true;
            inst.isCall = true;
            inst.hasDelaySlot = true;
            break;

        case SPECIAL_SYSCALL:
        case SPECIAL_BREAK:
            // Special handling for syscall/break
            break;

        case SPECIAL_MFHI:
        case SPECIAL_MTHI:
        case SPECIAL_MFLO:
        case SPECIAL_MTLO:
            // HI/LO register operations
            break;

        case SPECIAL_MULT:
        case SPECIAL_MULTU:
        case SPECIAL_DIV:
        case SPECIAL_DIVU:
            // Multiplication and division operations
            inst.isMultimedia = true;
            break;

        case SPECIAL_ADD:
        case SPECIAL_ADDU:
        case SPECIAL_SUB:
        case SPECIAL_SUBU:
        case SPECIAL_AND:
        case SPECIAL_OR:
        case SPECIAL_XOR:
        case SPECIAL_NOR:
        case SPECIAL_SLT:
        case SPECIAL_SLTU:
            // ALU operations
            break;

        case SPECIAL_SLL:
        case SPECIAL_SRL:
        case SPECIAL_SRA:
        case SPECIAL_SLLV:
        case SPECIAL_SRLV:
        case SPECIAL_SRAV:
            // Shift operations
            break;

        // 64-bit specific operations
        case SPECIAL_DADD:
        case SPECIAL_DADDU:
        case SPECIAL_DSUB:
        case SPECIAL_DSUBU:
        case SPECIAL_DSLL:
        case SPECIAL_DSRL:
        case SPECIAL_DSRA:
        case SPECIAL_DSLL32:
        case SPECIAL_DSRL32:
        case SPECIAL_DSRA32:
        case SPECIAL_DSLLV:
        case SPECIAL_DSRLV:
        case SPECIAL_DSRAV:
            // 64-bit operations
            break;

        default:
            // Other R-type instructions
            break;
        }
    }

    void R5900Decoder::decodeRegimm(Instruction &inst) const
    {
        uint32_t rt = inst.rt;

        switch (rt)
        {
        case REGIMM_BLTZ:
        case REGIMM_BGEZ:
        case REGIMM_BLTZL:
        case REGIMM_BGEZL:
            inst.isBranch = true;
            inst.hasDelaySlot = true;
            break;

        case REGIMM_BLTZAL:
        case REGIMM_BGEZAL:
        case REGIMM_BLTZALL:
        case REGIMM_BGEZALL:
            inst.isBranch = true;
            inst.isCall = true;
            inst.hasDelaySlot = true;
            break;

        case REGIMM_MTSAB:
        case REGIMM_MTSAH:
            // PS2 specific MTSAB/MTSAH instructions (for QMFC2/QMTC2)
            inst.isMultimedia = true;
            break;

        default:
            // Other REGIMM instructions
            break;
        }
    }

    void R5900Decoder::decodeMMI(Instruction &inst) const
    {
        inst.isMMI = true;
        inst.isMultimedia = true;

        // The function field is actually determined by the lowest 6 bits (as in R-type)
        uint32_t mmiFunction = inst.function;

        // Categorize the MMI instruction type based on the rs field
        uint32_t rs = inst.rs;

        switch (mmiFunction)
        {
        case MMI_MADD:
        case MMI_MADDU:
        case MMI_MADD1:
        case MMI_MADDU1:
            // Multiply-add operations
            break;

        case MMI_PLZCW:
            // Count leading zeros/ones
            break;

        case MMI_MFHI1:
        case MMI_MTHI1:
        case MMI_MFLO1:
        case MMI_MTLO1:
            // Secondary HI/LO register operations
            break;

        case MMI_MULT1:
        case MMI_MULTU1:
        case MMI_DIV1:
        case MMI_DIVU1:
            // Secondary multiply/divide operations
            break;

        case MMI_MMI0:
            // First set of multimedia instructions
            decodeMMI0(inst);
            break;

        case MMI_MMI1:
            // Second set of multimedia instructions
            decodeMMI1(inst);
            break;

        case MMI_MMI2:
            // Third set of multimedia instructions
            decodeMMI2(inst);
            break;

        case MMI_MMI3:
            // Fourth set of multimedia instructions
            decodeMMI3(inst);
            break;

        case MMI_PMFHL:
            // PMFHL variations based on sa field
            decodePMFHL(inst);
            break;

        case MMI_PMTHL:
            // PMTHL operations
            break;

        case MMI_PSLLH:
        case MMI_PSRLH:
        case MMI_PSRAH:
        case MMI_PSLLW:
        case MMI_PSRLW:
        case MMI_PSRAW:
            // SIMD shift operations
            break;

        default:
            // Unknown or unsupported MMI function
            break;
        }
    }

    void R5900Decoder::decodeCOP0(Instruction &inst) const
    {
        // COP0 (System Control) instructions
        uint32_t rs = inst.rs; // Actually the cop0 format field

        if (rs == COP0_MF)
        {
            // Move From COP0 register
        }
        else if (rs == COP0_MT)
        {
            // Move To COP0 register
        }
        else if (rs == COP0_CO)
        {
            // COProcessor operations
            uint32_t function = inst.function;

            if (function == COP0_CO_ERET)
            {
                inst.isReturn = true;
                inst.hasDelaySlot = true;
            }
            else if (function == COP0_CO_TLBR ||
                     function == COP0_CO_TLBWI ||
                     function == COP0_CO_TLBWR ||
                     function == COP0_CO_TLBP)
            {
                // TLB operations
            }
            else if (function == COP0_CO_EI || function == COP0_CO_DI)
            {
                // Enable/Disable Interrupts
            }
        }
    }

    void R5900Decoder::decodeCOP1(Instruction &inst) const
    {
        // COP1 (FPU) instructions
        uint32_t rs = inst.rs; // The FPU format field

        if (rs == COP1_MF)
        {
            // Move From FPU register
        }
        else if (rs == COP1_CF)
        {
            // Move From FPU Control register
        }
        else if (rs == COP1_MT)
        {
            // Move To FPU register
        }
        else if (rs == COP1_CT)
        {
            // Move To FPU Control register
        }
        else if (rs == COP1_BC)
        {
            // FPU Branch on Condition
            uint32_t rt = inst.rt; // The condition code
            if (rt == COP1_BC_BCF || rt == COP1_BC_BCT)
            {
                inst.isBranch = true;
                inst.hasDelaySlot = true;
            }
        }
        else if (rs == COP1_S || rs == COP1_W)
        {
            // FPU operations (single precision or word)
            uint32_t function = inst.function;
            // Decode specific FPU operation based on function field
        }
    }

    void R5900Decoder::decodeCOP2(Instruction &inst) const
    {
        // COP2 (VU0 macro mode) instructions
        inst.isVU = true;
        inst.isMultimedia = true;

        uint32_t rs = inst.rs; // The VU0 format field

        if (rs == COP2_MFC2)
        {
            // Move From COP2 register
        }
        else if (rs == COP2_CFC2)
        {
            // Move From COP2 Control register
        }
        else if (rs == COP2_MTC2)
        {
            // Move To COP2 register
        }
        else if (rs == COP2_CTC2)
        {
            // Move To COP2 Control register
        }
        else if (rs == COP2_BCF || rs == COP2_BCT)
        {
            // VU0 Branch on Condition
            inst.isBranch = true;
            inst.hasDelaySlot = true;
        }
        else
        {
            // VU0 vector operations
            // These would need detailed decoding based on function field
        }
    }

    void R5900Decoder::decodeMMI0(Instruction &inst) const
    {
        // Decode MMI0 subfunctions (based on function field)
        uint32_t subFunction = inst.function & 0x3F;

        // The implementation would set appropriate flags or properties based on the specific MMI0 operation
    }

    void R5900Decoder::decodeMMI1(Instruction &inst) const
    {
        // Decode MMI1 subfunctions (based on function field)
        uint32_t subFunction = inst.function & 0x3F;

        // The implementation would set appropriate flags or properties based on the specific MMI1 operation
    }

    void R5900Decoder::decodeMMI2(Instruction &inst) const
    {
        // Decode MMI2 subfunctions (based on function field)
        uint32_t subFunction = inst.function & 0x3F;

        // The implementation would set appropriate flags or properties based on the specific MMI2 operation
    }

    void R5900Decoder::decodeMMI3(Instruction &inst) const
    {
        // Decode MMI3 subfunctions (based on function field)
        uint32_t subFunction = inst.function & 0x3F;

        // The implementation would set appropriate flags or properties based on the specific MMI3 operation
    }

    void R5900Decoder::decodePMFHL(Instruction &inst) const
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

    bool R5900Decoder::isBranchInstruction(const Instruction &inst) const
    {
        return inst.isBranch;
    }

    bool R5900Decoder::isJumpInstruction(const Instruction &inst) const
    {
        return inst.isJump;
    }

    bool R5900Decoder::isCallInstruction(const Instruction &inst) const
    {
        return inst.isCall;
    }

    bool R5900Decoder::isReturnInstruction(const Instruction &inst) const
    {
        return inst.isReturn;
    }

    bool R5900Decoder::isMMIInstruction(const Instruction &inst) const
    {
        return inst.isMMI;
    }

    bool R5900Decoder::isVUInstruction(const Instruction &inst) const
    {
        return inst.isVU;
    }

    bool R5900Decoder::isStore(const Instruction &inst) const
    {
        return inst.isStore;
    }

    bool R5900Decoder::isLoad(const Instruction &inst) const
    {
        return inst.isLoad;
    }

    bool R5900Decoder::hasDelaySlot(const Instruction &inst) const
    {
        return inst.hasDelaySlot;
    }

    uint32_t R5900Decoder::getBranchTarget(const Instruction &inst) const
    {
        if (!inst.isBranch)
        {
            return 0;
        }

        // Calculate branch target: PC + 4 + (sign-extended immediate << 2)
        int32_t offset = static_cast<int16_t>(inst.immediate) << 2;
        return inst.address + 4 + offset;
    }

    uint32_t R5900Decoder::getJumpTarget(const Instruction &inst) const
    {
        if (!inst.isJump)
        {
            return 0;
        }

        if (inst.opcode == OPCODE_J || inst.opcode == OPCODE_JAL)
        {
            // J/JAL: target is in the lower 26 bits, shifted left by 2
            // and combined with the upper 4 bits of PC + 4
            uint32_t pc_upper = (inst.address + 4) & 0xF0000000;
            return pc_upper | (inst.target << 2);
        }
        else if (inst.opcode == OPCODE_SPECIAL &&
                 (inst.function == SPECIAL_JR || inst.function == SPECIAL_JALR))
        {
            // JR/JALR: target is in the rs register (can't be determined statically)
            return 0;
        }

        return 0;
    }

} // namespace ps2recomp