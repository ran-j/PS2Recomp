#include "ps2recomp/r5900_decoder.h"
#include <iostream>

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
        inst.isLoad = false;
        inst.isStore = false;

        // Initialize the enhanced fields
        inst.mmiType = 0;
        inst.mmiFunction = 0;
        inst.pmfhlVariation = 0;
        inst.vuFunction = 0;

        inst.vectorInfo.isVector = false;
        inst.vectorInfo.usesQReg = false;
        inst.vectorInfo.usesPReg = false;
        inst.vectorInfo.modifiesMAC = false;
        inst.vectorInfo.vectorField = 0xF; // All fields (xyzw)

        inst.modificationInfo.modifiesGPR = false;
        inst.modificationInfo.modifiesFPR = false;
        inst.modificationInfo.modifiesVFR = false;
        inst.modificationInfo.modifiesMemory = false;
        inst.modificationInfo.modifiesControl = false;

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
            inst.modificationInfo.modifiesControl = true; // PC
            break;

        case OPCODE_JAL:
            decodeJType(inst);
            inst.isJump = true;
            inst.isCall = true;
            inst.hasDelaySlot = true;
            inst.modificationInfo.modifiesGPR = true;     // $ra (r[31])
            inst.modificationInfo.modifiesControl = true; // PC
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

        case OPCODE_DADDI:
        case OPCODE_DADDIU:
            decodeIType(inst);
            if (inst.rt != 0)
                inst.modificationInfo.modifiesGPR = true;
            break;

        case OPCODE_MMI:
            decodeMMI(inst);
            inst.isMMI = true;
            inst.isMultimedia = true;
            inst.modificationInfo.modifiesGPR = true;
            break;

        case OPCODE_LQ:
            decodeIType(inst);
            inst.isLoad = true;
            inst.isMultimedia = true; // 128-bit load
            if (inst.rt != 0)
                inst.modificationInfo.modifiesGPR = true;
            break;

        case OPCODE_SQ:
            decodeIType(inst);
            inst.isStore = true;
            inst.isMultimedia = true; // 128-bit store
            inst.modificationInfo.modifiesMemory = true;
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
        case OPCODE_LDC2: // VU Load
            decodeIType(inst);
            inst.isLoad = true;
            if (inst.opcode == OPCODE_LWC1 || inst.opcode == OPCODE_LDC1)
                inst.modificationInfo.modifiesFPR = true;
            else if (inst.opcode == OPCODE_LQC2)
                inst.modificationInfo.modifiesVFR = true;
            else if (inst.rt != 0)
                inst.modificationInfo.modifiesGPR = true; // Standard GPR loads
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
        case OPCODE_SCD: // VU Store
            decodeIType(inst);
            inst.isStore = true;
            if (inst.opcode == OPCODE_SC || inst.opcode == OPCODE_SCD)
            {
                if (inst.rt != 0)
                    inst.modificationInfo.modifiesGPR = true;
            }
            break;

        case OPCODE_CACHE:
            decodeIType(inst);
            inst.modificationInfo.modifiesControl = true; // Cache state
            break;
        case OPCODE_PREF:
            decodeIType(inst);
            break;

        case OPCODE_COP0:
            decodeCOP0(inst);
            inst.modificationInfo.modifiesControl = true;
            if (inst.rs == COP0_MF && inst.rt != 0)
                inst.modificationInfo.modifiesGPR = true;
            break;

        case OPCODE_COP1:
            decodeCOP1(inst);
            // Can modify FPRs, FCRs, GPRs (MFC1/CFC1), or PC (BC1)
            if (inst.isBranch)
                inst.modificationInfo.modifiesControl = true; // PC
            if (inst.rs == COP1_MF || inst.rs == COP1_CF)
            {
                // MFC1, CFC1
                if (inst.rt != 0)
                    inst.modificationInfo.modifiesGPR = true;
            }
            else if (inst.rs == COP1_S || inst.rs == COP1_W || inst.rs == COP1_L)
            {
                inst.modificationInfo.modifiesFPR = true;
            }
            if (inst.rs == COP1_CT || inst.function >= COP1_FUNC_C_F)
            {                                                 // CTC1 or Compare
                inst.modificationInfo.modifiesControl = true; // FCR31
            }
            break;

        case OPCODE_COP2:
            decodeCOP2(inst);
            inst.isVU = true;
            inst.isMultimedia = true;
            // Can modify VFRs, VU Flags, GPRs (QMFC2/CFC2), PC (BC2), memory (VISWR)
            if (inst.isBranch)
                inst.modificationInfo.modifiesControl = true; // PC
            if (inst.rs == COP2_QMFC2 || inst.rs == COP2_CFC2)
            {
                // QMFC2, CFC2
                if (inst.rt != 0)
                    inst.modificationInfo.modifiesGPR = true;
            }
            else if (inst.rs == COP2_QMTC2 || inst.rs == COP2_CTC2)
            {
                // Modifies VU state based on GPR
            }
            else
            {
                inst.modificationInfo.modifiesVFR = true;     // Default assumption
                inst.modificationInfo.modifiesControl = true; // Flags, Q, P, I etc.
            }
            if (inst.isStore)
                inst.modificationInfo.modifiesMemory = true;
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
        if (inst.rd != 0)
            inst.modificationInfo.modifiesGPR = true;
    }

    void R5900Decoder::decodeIType(Instruction &inst) const
    {
        // I-type instructions already have all fields set correctly
        if (!inst.isStore && !inst.isBranch && inst.rt != 0)
        {
            inst.modificationInfo.modifiesGPR = true;
        }
    }

    void R5900Decoder::decodeJType(Instruction &inst) const
    {
        // J-type instructions already have all fields set correctly
        inst.modificationInfo.modifiesControl = true; // PC
        if (inst.opcode == OPCODE_JAL)
        {
            inst.modificationInfo.modifiesGPR = true; // $ra (r[31])
        }
    }

    void R5900Decoder::decodeSpecial(Instruction &inst) const
    {
        if (inst.rd != 0)
            inst.modificationInfo.modifiesGPR = true;

        switch (inst.function)
        {
        case SPECIAL_JR:
            inst.isJump = true;
            inst.hasDelaySlot = true;
            inst.modificationInfo.modifiesGPR = false;    // Doesn't modify GPR itself
            inst.modificationInfo.modifiesControl = true; // PC
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
            if (inst.rd == 0)
                inst.modificationInfo.modifiesGPR = false; // JALR $zero, $rs is like JR $rs
            else
                inst.modificationInfo.modifiesGPR = true;
            inst.modificationInfo.modifiesControl = true; // PC
            break;

        case SPECIAL_SYSCALL:
        case SPECIAL_BREAK:
            // Special handling for syscall/break
            inst.modificationInfo.modifiesGPR = false;    // No GPR change
            inst.modificationInfo.modifiesControl = true; // Changes control flow, potentially registers via handler
            break;

        case SPECIAL_MFHI:
        case SPECIAL_MFLO:
            if (inst.rd == 0)
                inst.modificationInfo.modifiesGPR = false;
            break;
        case SPECIAL_MTHI:
        case SPECIAL_MTLO:
            // HI/LO register operations
            inst.modificationInfo.modifiesGPR = false;    // Doesn't modify rd
            inst.modificationInfo.modifiesControl = true; // HI/LO
            break;

        case SPECIAL_MULT:
        case SPECIAL_MULTU:
        case SPECIAL_DIV:
        case SPECIAL_DIVU:
            // Multiplication and division operations
            inst.modificationInfo.modifiesGPR = false;    // Doesn't modify rd
            inst.modificationInfo.modifiesControl = true; // HI/LO
            break;

        case SPECIAL_ADD:
        case SPECIAL_ADDU:
        case SPECIAL_SUB:
        case SPECIAL_SUBU:
        case SPECIAL_AND:
        case SPECIAL_OR:
        case SPECIAL_XOR:
        case SPECIAL_NOR:
            // ALU operations
            if (inst.rd == 0)
                inst.modificationInfo.modifiesGPR = false;
            break;

        case SPECIAL_SLL:
        case SPECIAL_SRL:
        case SPECIAL_SRA:
        case SPECIAL_SLLV:
        case SPECIAL_SRLV:
        case SPECIAL_SRAV:
            // Shift operations
            if (inst.rd == 0)
                inst.modificationInfo.modifiesGPR = false;
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
            if (inst.rd == 0)
                inst.modificationInfo.modifiesGPR = false;
            break;

        case SPECIAL_MOVZ:
        case SPECIAL_MOVN:
            if (inst.rd == 0)
                inst.modificationInfo.modifiesGPR = false;
            break;

        // Set on Less Than
        case SPECIAL_SLT:
        case SPECIAL_SLTU:
            if (inst.rd == 0)
                inst.modificationInfo.modifiesGPR = false;
            break;

        case SPECIAL_TGE:
        case SPECIAL_TGEU:
        case SPECIAL_TLT:
        case SPECIAL_TLTU:
        case SPECIAL_TEQ:
        case SPECIAL_TNE:
            inst.modificationInfo.modifiesGPR = false;
            inst.modificationInfo.modifiesControl = true; // Control flow change via trap
            break;

        // PS2 Specific (MFSA/MTSA)
        case SPECIAL_MFSA: // Modifies rd GPR
            if (inst.rd == 0)
                inst.modificationInfo.modifiesGPR = false;
            break;
        case SPECIAL_MTSA: // Modifies SA control register
            inst.modificationInfo.modifiesGPR = false;
            inst.modificationInfo.modifiesControl = true; // SA reg
            break;

        case SPECIAL_SYNC:
            inst.modificationInfo.modifiesGPR = false;
            // Potentially modifies memory/cache visibility
            inst.modificationInfo.modifiesControl = true;
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
            // Branches
            inst.isBranch = true;
            inst.hasDelaySlot = true;
            inst.modificationInfo.modifiesControl = true; // PC potentially
            break;

        case REGIMM_BLTZAL:
        case REGIMM_BGEZAL:
        case REGIMM_BLTZALL:
        case REGIMM_BGEZALL:
            // Branch and Link
            inst.isBranch = true;
            inst.isCall = true;
            inst.hasDelaySlot = true;
            inst.modificationInfo.modifiesGPR = true;     // $ra (r[31])
            inst.modificationInfo.modifiesControl = true; // PC
            break;

        case REGIMM_TGEI:
        case REGIMM_TGEIU:
        case REGIMM_TLTI:
        case REGIMM_TLTIU:
        case REGIMM_TEQI:
        case REGIMM_TNEI:
            // Trap Instructions
            inst.modificationInfo.modifiesControl = true;
            break;

        case REGIMM_MTSAB:
        case REGIMM_MTSAH:
            // PS2 specific MTSAB/MTSAH instructions (for QMFC2/QMTC2)
            inst.isMultimedia = true;
            inst.modificationInfo.modifiesControl = true; // SA register
            break;

        default:
            // Unknown REGIMM instructions
            std::cerr << "Unknown REGIMM instruction: " << std::hex << inst.raw << std::endl;
            break;
        }
    }

    void R5900Decoder::decodeMMI(Instruction &inst) const
    {
        inst.isMMI = true;
        inst.isMultimedia = true;
        inst.modificationInfo.modifiesGPR = true; // Most MMI ops write to rd GPR
        if (inst.rd == 0)
            inst.modificationInfo.modifiesGPR = false; // Except if rd is $zero

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
            inst.modificationInfo.modifiesControl = true; // Modifies HI/LO or HI1/LO1
            break;

        case MMI_PLZCW:
            // Count leading zeros/ones
            break;

        case MMI_MFHI1:
        case MMI_MFLO1:
            // Modifies rd GPR (already set)
            break;

        case MMI_MTHI1:
        case MMI_MTLO1:
            // Secondary HI/LO register operations
            inst.modificationInfo.modifiesGPR = false;
            inst.modificationInfo.modifiesControl = true;
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
            inst.modificationInfo.modifiesGPR = false;
            inst.modificationInfo.modifiesControl = true;
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
            std::cerr << "Unknown MMI function: " << std::hex << mmiFunction << std::endl;
            break;
        }
    }

    void R5900Decoder::decodeMMI0(Instruction &inst) const
    {
        inst.mmiType = 0;
        inst.mmiFunction = inst.sa;

        uint32_t sa = inst.sa;

        switch (sa)
        {
        case MMI0_PADDW:
        case MMI0_PSUBW:
        case MMI0_PCGTW:
        case MMI0_PMAXW:
        case MMI0_PADDH:
        case MMI0_PSUBH:
        case MMI0_PCGTH:
        case MMI0_PMAXH:
        case MMI0_PADDB:
        case MMI0_PSUBB:
        case MMI0_PCGTB:
            // Arithmetic and comparison operations
            break;

        case MMI0_PADDSW:
        case MMI0_PSUBSW:
        case MMI0_PADDSH:
        case MMI0_PSUBSH:
        case MMI0_PADDSB:
        case MMI0_PSUBSB:
            // Saturated arithmetic operations
            break;

        case MMI0_PEXTLW:
        case MMI0_PPACW:
        case MMI0_PEXTLH:
        case MMI0_PPACH:
        case MMI0_PEXTLB:
        case MMI0_PPACB:
        case MMI0_PEXT5:
        case MMI0_PPAC5:
            // Data packing/unpacking operations
            break;

        default:
            // Unknown MMI0 operation
            break;
        }
    }

    void R5900Decoder::decodeMMI1(Instruction &inst) const
    {
        inst.mmiType = 1;
        inst.mmiFunction = inst.sa;

        uint32_t subFunction = inst.function & 0x3F;

        uint32_t sa = inst.sa;

        switch (sa)
        {
        case MMI1_PABSW:
        case MMI1_PABSH:
            // Absolute value operations
            break;

        case MMI1_PCEQW:
        case MMI1_PCEQH:
        case MMI1_PCEQB:
            // Equality comparison operations
            break;

        case MMI1_PMINW:
        case MMI1_PMINH:
            // Minimum value operations
            break;

        case MMI1_PADDUW:
        case MMI1_PSUBUW:
        case MMI1_PEXTUW:
        case MMI1_PADDUH:
        case MMI1_PSUBUH:
        case MMI1_PEXTUH:
        case MMI1_PADDUB:
        case MMI1_PSUBUB:
        case MMI1_PEXTUB:
            // Unsigned arithmetic and extension operations
            break;

        case MMI1_QFSRV:
            // Quadword funnel shift right variable
            inst.isMultimedia = true;
            break;

        default:
            // Unknown MMI1 operation
            break;
        }
    }

    void R5900Decoder::decodeMMI2(Instruction &inst) const
    {
        inst.mmiType = 2;
        inst.mmiFunction = inst.sa;

        if (inst.sa == MMI2_PMADDW || inst.sa == MMI2_PMSUBW ||
            inst.sa == MMI2_PMULTW || inst.sa == MMI2_PDIVW || inst.sa == MMI2_PDIVBW ||
            inst.sa == MMI2_PMADDH || inst.sa == MMI2_PHMADH ||
            inst.sa == MMI2_PMSUBH || inst.sa == MMI2_PHMSBH || inst.sa == MMI2_PMULTH)
        {
            inst.modificationInfo.modifiesControl = true; // HI/LO
        }

        uint32_t sa = inst.sa;

        switch (sa)
        {
        case MMI2_PMADDW:
        case MMI2_PMSUBW:
        case MMI2_PMADDH:
        case MMI2_PHMADH:
        case MMI2_PMSUBH:
        case MMI2_PHMSBH:
        case MMI2_PMULTH:
            // Multiply/multiply-add operations
            inst.isMultimedia = true;
            break;

        case MMI2_PSLLVW:
        case MMI2_PSRLVW:
            // Variable shift operations
            break;

        case MMI2_PMFHI:
        case MMI2_PMFLO:
            // Move from HI/LO registers
            break;

        case MMI2_PINTH:
            // Interleave half words
            break;

        case MMI2_PMULTW:
        case MMI2_PDIVW:
        case MMI2_PDIVBW:
            // Multiply/divide operations
            inst.isMultimedia = true;
            break;

        case MMI2_PCPYLD:
            // Copy lower doubleword
            break;

        case MMI2_PAND:
        case MMI2_PXOR:
            // Logical operations
            break;

        case MMI2_PEXEH:
        case MMI2_PREVH:
        case MMI2_PEXEW:
        case MMI2_PROT3W:
            // Data permutation operations
            break;

        default:
            // Unknown MMI2 operation
            break;
        }
    }

    void R5900Decoder::decodeMMI3(Instruction &inst) const
    {
        inst.mmiType = 3;
        inst.mmiFunction = inst.sa;

        uint32_t sa = inst.sa;

        switch (sa)
        {
        case MMI3_PMADDUW:
            // Unsigned multiply-add
            inst.isMultimedia = true;
            inst.modificationInfo.modifiesControl = true; // HI/LO
            break;

        case MMI3_PSRAVW:
            // Packed shift right arithmetic variable word
            break;

        case MMI3_PMTHI:
            inst.mmiFunction = MMI3_PMTHI;
            inst.modificationInfo.modifiesGPR = false;
            inst.modificationInfo.modifiesControl = true; // HI register is modified
            break;

        case MMI3_PMTLO:
            inst.mmiFunction = MMI3_PMTLO;
            inst.modificationInfo.modifiesGPR = false;
            inst.modificationInfo.modifiesControl = true; // LO register is modified
            break;

        case MMI3_PINTEH:
            // Interleave even halfwords
            break;

        case MMI3_PMULTUW:
        case MMI3_PDIVUW:
            // Unsigned multiply/divide operations
            inst.isMultimedia = true;
            inst.modificationInfo.modifiesControl = true; // HI/LO
            break;

        case MMI3_PCPYUD:
            // Copy upper doubleword
            break;

        case MMI3_POR:
        case MMI3_PNOR:
            // Logical operations
            break;

        case MMI3_PEXCH:
        case MMI3_PCPYH:
        case MMI3_PEXCW:
            // Data permutation operations
            break;

        default:
            // Unknown MMI3 operation
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
        inst.isVU = true;
        inst.isMultimedia = true;

        uint32_t rs = inst.rs; // The format field

        switch (rs)
        {
        case COP2_QMFC2: // Move From COP2 (128-bit)
        case COP2_CFC2:  // Move Control From COP2
        case COP2_QMTC2: // Move To COP2 (128-bit)
        case COP2_CTC2:  // Move Control To COP2
            // Register transfer operations
            break;

        case COP2_BC2: // Branch on COP2 condition
        {
            uint32_t rt = inst.rt; // The condition code

            if (rt == COP2_BCF || rt == COP2_BCT ||
                rt == COP2_BCFL || rt == COP2_BCTL ||
                rt == COP2_BCEF || rt == COP2_BCET ||
                rt == COP2_BCEFL || rt == COP2_BCETL)
            {
                inst.isBranch = true;
                inst.hasDelaySlot = true;
            }
        }
        break;

        case COP2_CO: // VU0 vector operations
        {
            uint32_t function = inst.function;

            switch (function)
            {
            case VU0_VADD:
            case VU0_VSUB:
            case VU0_VMUL:
                // Basic vector math operations
                break;

            case VU0_VDIV:
            case VU0_VSQRT:
            case VU0_VRSQRT:
                // Division and square root operations
                break;

            case VU0_VMULQ:
                // Multiply by Q register
                break;

            case VU0_VIADD:
            case VU0_VISUB:
            case VU0_VIADDI:
                // Integer operations
                break;

            case VU0_VIAND:
            case VU0_VIOR:
                // Logical operations
                break;

            case VU0_VILWR:
            case VU0_VISWR:
                // Load/Store operations
                inst.isLoad = (function == VU0_VILWR);
                inst.isStore = (function == VU0_VISWR);
                break;

            case VU0_VCALLMS:
            case VU0_VCALLMSR:
                // VU0 microprogram calls
                inst.isCall = true;
                break;

            case VU0_VRGET:
                // Get random number from R register
                break;

            default:
                // Other VU0 operations
                break;
            }
        }
        break;

        case COP2_CTCVU:  // Control Transfer to VU0
        case COP2_MTVUCF: // Move To VU Control/Flag register
        case COP2_VMTIR:  // Move To VU0 I Register
            // Special register operations
            break;

        case COP2_VU0OPS: // Additional VU0 operations
        {
            uint32_t function = inst.function;

            switch (function)
            {
            case VU0OPS_QMFC2_NI: // Non-incrementing QMFC2
            case VU0OPS_QMFC2_I:  // Incrementing QMFC2
            case VU0OPS_QMTC2_NI: // Non-incrementing QMTC2
            case VU0OPS_QMTC2_I:  // Incrementing QMTC2
                // Extended register transfer operations
                break;

            case VU0OPS_VMFIR: // Move From Integer Register
                // Move operations
                break;

            case VU0OPS_VXITOP: // Execute Interrupt on VU0
                break;

            case VU0OPS_VWAITQ: // Wait for Q register operations
                break;

            default:
                // Unknown VU0OPS
                break;
            }
        }
        break;

        default:
            // Unknown COP2 format
            break;
        }
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