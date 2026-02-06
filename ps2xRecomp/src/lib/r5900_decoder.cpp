#include "ps2recomp/r5900_decoder.h"
#include "rabbitizer.h"
#include <iostream>

namespace ps2recomp
{

    R5900Decoder::R5900Decoder()
    {
    }

    R5900Decoder::~R5900Decoder()
    {
    }

    Instruction R5900Decoder::decodeInstruction(uint32_t address, uint32_t rawInstruction) const
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
        inst.simmediate = SIMMEDIATE(rawInstruction);
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
        inst.modificationInfo.modifiesVIR = false;
        inst.modificationInfo.modifiesVIC = false;
        inst.modificationInfo.modifiesMemory = false;
        inst.modificationInfo.modifiesControl = false;

        RabbitizerInstruction rabbitizerInst;
        RabbitizerInstructionR5900_init(&rabbitizerInst, rawInstruction, address);
        RabbitizerInstructionR5900_processUniqueId(&rabbitizerInst);

        const RabbitizerInstrDescriptor *desc = rabbitizerInst.descriptor;

        inst.isBranch = RabbitizerInstrDescriptor_isBranch(desc);
        inst.isJump = RabbitizerInstrDescriptor_isJump(desc);
        inst.isCall = RabbitizerInstruction_isFunctionCall(&rabbitizerInst);
        inst.isReturn = RabbitizerInstruction_isReturn(&rabbitizerInst) ||
                        rabbitizerInst.uniqueId == RABBITIZER_INSTR_ID_cpu_eret;
        inst.hasDelaySlot = RabbitizerInstruction_hasDelaySlot(&rabbitizerInst);

        inst.isLoad = RabbitizerInstrDescriptor_doesLoad(desc);
        inst.isStore = RabbitizerInstrDescriptor_doesStore(desc);

        inst.isMMI = inst.opcode == OPCODE_MMI;
        inst.isVU = inst.opcode == OPCODE_COP2 ||
                    inst.opcode == OPCODE_LWC2 ||
                    inst.opcode == OPCODE_LDC2 ||
                    inst.opcode == OPCODE_SWC2 ||
                    inst.opcode == OPCODE_SDC2;

        bool modifiesGpr = false;
        if (RabbitizerInstrDescriptor_modifiesRs(desc) && inst.rs != 0)
        {
            modifiesGpr = true;
        }
        if (RabbitizerInstrDescriptor_modifiesRt(desc) && inst.rt != 0)
        {
            modifiesGpr = true;
        }
        if (RabbitizerInstrDescriptor_modifiesRd(desc) && inst.rd != 0)
        {
            modifiesGpr = true;
        }
        if (RabbitizerInstrDescriptor_doesLink(desc))
        {
            modifiesGpr = true;
        }
        inst.modificationInfo.modifiesGPR = modifiesGpr;

        inst.modificationInfo.modifiesFPR = RabbitizerInstrDescriptor_modifiesFs(desc) ||
                                            RabbitizerInstrDescriptor_modifiesFt(desc) ||
                                            RabbitizerInstrDescriptor_modifiesFd(desc);

        inst.modificationInfo.modifiesMemory = RabbitizerInstrDescriptor_doesStore(desc);
        inst.modificationInfo.modifiesControl = RabbitizerInstrDescriptor_isBranch(desc) ||
                                                RabbitizerInstrDescriptor_isJump(desc) ||
                                                RabbitizerInstrDescriptor_isTrap(desc) ||
                                                RabbitizerInstrDescriptor_modifiesHI(desc) ||
                                                RabbitizerInstrDescriptor_modifiesLO(desc);

        if (rabbitizerInst.uniqueId == RABBITIZER_INSTR_ID_cpu_eret)
        {
            inst.isReturn = true;
            inst.hasDelaySlot = false;
            inst.modificationInfo.modifiesControl = true;
        }

        if (inst.opcode == OPCODE_LL || inst.opcode == OPCODE_LLD)
        {
            inst.isLoad = true;
            inst.modificationInfo.modifiesControl = true;
            if (inst.rt != 0)
            {
                inst.modificationInfo.modifiesGPR = true;
            }
        }

        if (inst.opcode == OPCODE_SC || inst.opcode == OPCODE_SCD)
        {
            inst.isStore = true;
            inst.modificationInfo.modifiesControl = true;
            if (inst.rt != 0)
            {
                inst.modificationInfo.modifiesGPR = true; // success flag to rt
            }
        }

        if (inst.opcode == OPCODE_COP2)
        {
            decodeCOP2(inst);
        }
        else if (inst.opcode == OPCODE_LWC2 || inst.opcode == OPCODE_LDC2)
        {
            inst.isVU = true;
            inst.modificationInfo.modifiesVFR = true;
        }
        else if (inst.opcode == OPCODE_SWC2 || inst.opcode == OPCODE_SDC2)
        {
            inst.isVU = true;
            inst.modificationInfo.modifiesMemory = true;
        }

        if (inst.opcode == OPCODE_LQ || inst.opcode == OPCODE_SQ)
        {
            inst.isMultimedia = true;
        }

        if (inst.isMMI || inst.isVU)
        {
            inst.isMultimedia = true;
            inst.vectorInfo.isVector = inst.isVU; // Only VU ops are truly vector
        }

        RabbitizerInstructionR5900_destroy(&rabbitizerInst);

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
        inst.isJump = true;
        inst.hasDelaySlot = true;
        inst.modificationInfo.modifiesControl = true; // PC
        if (inst.opcode == OPCODE_JAL)
        {
            inst.isCall = true;
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
            inst.modificationInfo.modifiesControl = true;
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

        uint32_t rs = inst.rs;

        if (mmiFunction == MMI_MMI0)
        {
            decodeMMI0(inst);
            return;
        }
        if (mmiFunction == MMI_MMI1)
        {
            decodeMMI1(inst);
            return;
        }
        if (mmiFunction == MMI_MMI2)
        {
            decodeMMI2(inst);
            return;
        }
        if (mmiFunction == MMI_MMI3)
        {
            decodeMMI3(inst);
            return;
        }

        switch (mmiFunction)
        {
        case MMI_PLZCW:
            // Parallel leading zero/one count word
            break;
        case MMI_MFHI1:
        case MMI_MFLO1:
            // Move from HI1/LO1 -> rd
            if (inst.rd == 0)
            {
                inst.modificationInfo.modifiesGPR = false;
            }
            inst.modificationInfo.modifiesControl = false;
            break;
        case MMI_MTHI1:
        case MMI_MTLO1:
            // Move to HI1/LO1
            inst.modificationInfo.modifiesGPR = false;
            inst.modificationInfo.modifiesControl = true;
            break;
        case MMI_PSLLH:
        case MMI_PSRLH:
        case MMI_PSRAH:
        case MMI_PSLLW:
        case MMI_PSRLW:
        case MMI_PSRAW:
            // Packed shift instructions; defaults already mark GPR modify (unless rd==0)
            break;

        // HI/LO or HI1/LO1 producers/consumers
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
            inst.modificationInfo.modifiesGPR = false; // Writes to HI/LO or HI1/LO1
            inst.modificationInfo.modifiesControl = true;
            break;
        case MMI_PMTHL:
        case MMI3_PMTHI:
        case MMI3_PMTLO:
            inst.modificationInfo.modifiesGPR = false; // Writes to HI/LO or HI1/LO1
            inst.modificationInfo.modifiesControl = true;
            break;
        case MMI_PMFHL:
            decodePMFHL(inst);
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
        } // ERET is special
        else if (format == COP0_BC)
        {
            inst.isBranch = true;
            inst.hasDelaySlot = true;
        }
    }

    void R5900Decoder::decodeCOP1(Instruction &inst) const
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
        } // FCR31
    }

    void R5900Decoder::decodeCOP2(Instruction &inst) const
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
            break; // Modifies VU state
        case COP2_CTC2:
            inst.modificationInfo.modifiesControl = true;
            break; // Modifies VU control state
        case COP2_BC:
            inst.isBranch = true;
            inst.hasDelaySlot = true;
            inst.modificationInfo.modifiesControl = true;
            break;
        case COP2_CO: // VU Macro instructions (format >= 0x10)
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
        { // Refine based on specific VU function
            uint8_t vu_func = inst.function;
            bool is_special2 = vu_func >= 0x3C;
            uint8_t vu_fhi_flo = 0;

            if (is_special2)
            {
                vu_fhi_flo = static_cast<uint8_t>((((inst.raw >> 6) & 0x1F) << 2) | (inst.raw & 0x3));
            }

            if (is_special2 && (vu_fhi_flo == VU0_S2_VDIV || vu_fhi_flo == VU0_S2_VSQRT || vu_fhi_flo == VU0_S2_VRSQRT))
            {
                inst.vectorInfo.fsf = (inst.raw >> 10) & 0x3; // Extract bits 10-11
                inst.vectorInfo.ftf = (inst.raw >> 8) & 0x3;  // Extract bits 8-9
            }

            inst.vectorInfo.vectorField = (inst.raw >> 21) & 0xF;

            inst.modificationInfo.modifiesVFR = true;     // Default: Modifies Vector Float Reg
            inst.modificationInfo.modifiesControl = true; // Default: Modifies Flags/Special Regs (Q, P, I, MAC, Clip...)

            if (is_special2) // Special2 Table
            {
                switch (vu_fhi_flo)
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
                    break; // Reads VI, writes VF
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
                    inst.modificationInfo.modifiesVFR = false; /* Modifies R */
                    break;                                     // Modifies R
                case VU0_S2_VRGET:
                    inst.modificationInfo.modifiesControl = false; /* Reads R, writes VF */
                    break;
                case VU0_S2_VRNEXT:
                    inst.modificationInfo.modifiesControl = true; /* Modifies R */
                    inst.modificationInfo.modifiesVFR = false;
                    break; // Writes R
                case VU0_S2_VWAITQ:
                    inst.modificationInfo.modifiesVFR = false;
                    inst.modificationInfo.modifiesControl = false;
                    break;
                case VU0_S2_VABS:
                case VU0_S2_VMOVE:
                case VU0_S2_VMR32:
                    inst.modificationInfo.modifiesControl = false;
                    break; // Only VF
                case VU0_S2_VNOP:
                    inst.modificationInfo.modifiesVFR = false;
                    inst.modificationInfo.modifiesControl = false;
                    break;
                case VU0_S2_VCLIPw:
                    inst.modificationInfo.modifiesVFR = false; /* Modifies Clip flags */
                    break;
                }
            }
            else // Special1 Table
            {
                if (vu_func >= VU0_S1_VIADD && vu_func <= VU0_S1_VIOR)
                {
                    inst.modificationInfo.modifiesVFR = false;
                    inst.modificationInfo.modifiesVIR = true;
                } // Integer ops
                if (vu_func == VU0_S1_VIADDI)
                {
                    inst.modificationInfo.modifiesVFR = false;
                    inst.modificationInfo.modifiesVIR = true;
                }
            }
            break;
        }
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

        int32_t offset = inst.simmediate << 2;
        return inst.address + 4 + offset;
    }

    uint32_t R5900Decoder::getJumpTarget(const Instruction &inst) const
    {
        if (!inst.isJump)
        {
            return 0;
        }

        if (inst.opcode == OPCODE_SPECIAL &&
            (inst.function == SPECIAL_JR || inst.function == SPECIAL_JALR))
        {
            // JR/JALR: target is in the rs register (can't be determined statically)
            return 0;
        }

        if (inst.opcode == OPCODE_J || inst.opcode == OPCODE_JAL)
        {
            // J/JAL: target is in the lower 26 bits, shifted left by 2
            // and combined with the upper 4 bits of PC + 4
            uint32_t pc_upper = (inst.address + 4) & 0xF0000000;
            return pc_upper | (inst.target << 2);
        }

        return 0;
    }

} // namespace ps2recomp
