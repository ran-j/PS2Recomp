#ifndef PS2RECOMP_INSTRUCTIONS_H
#define PS2RECOMP_INSTRUCTIONS_H

#include <cstdint>

namespace ps2recomp
{
    // Basic MIPS opcodes (shared with R4300i)
    enum MipsOpcodes
    {
        OPCODE_SPECIAL = 0x00,
        OPCODE_REGIMM = 0x01,
        OPCODE_J = 0x02,
        OPCODE_JAL = 0x03,
        OPCODE_BEQ = 0x04,
        OPCODE_BNE = 0x05,
        OPCODE_BLEZ = 0x06,
        OPCODE_BGTZ = 0x07,
        OPCODE_ADDI = 0x08,
        OPCODE_ADDIU = 0x09,
        OPCODE_SLTI = 0x0A,
        OPCODE_SLTIU = 0x0B,
        OPCODE_ANDI = 0x0C,
        OPCODE_ORI = 0x0D,
        OPCODE_XORI = 0x0E,
        OPCODE_LUI = 0x0F,
        OPCODE_COP0 = 0x10,
        OPCODE_COP1 = 0x11,
        OPCODE_COP2 = 0x12, // VU0 macro instructions
        OPCODE_COP3 = 0x13, // Unused on PS2
        OPCODE_BEQL = 0x14,
        OPCODE_BNEL = 0x15,
        OPCODE_BLEZL = 0x16,
        OPCODE_BGTZL = 0x17,
        OPCODE_DADDI = 0x18,
        OPCODE_DADDIU = 0x19,
        OPCODE_LDL = 0x1A,
        OPCODE_LDR = 0x1B,
        OPCODE_MMI = 0x1C, // PS2 specific multimedia instructions
        OPCODE_LQ = 0x1E,  // PS2 specific 128-bit load
        OPCODE_SQ = 0x1F,  // PS2 specific 128-bit store
        OPCODE_LB = 0x20,
        OPCODE_LH = 0x21,
        OPCODE_LWL = 0x22,
        OPCODE_LW = 0x23,
        OPCODE_LBU = 0x24,
        OPCODE_LHU = 0x25,
        OPCODE_LWR = 0x26,
        OPCODE_LWU = 0x27,
        OPCODE_SB = 0x28,
        OPCODE_SH = 0x29,
        OPCODE_SWL = 0x2A,
        OPCODE_SW = 0x2B,
        OPCODE_SDL = 0x2C,
        OPCODE_SDR = 0x2D,
        OPCODE_SWR = 0x2E,
        OPCODE_CACHE = 0x2F,
        OPCODE_LL = 0x30,
        OPCODE_LWC1 = 0x31,
        OPCODE_LWC2 = 0x32,
        OPCODE_PREF = 0x33,
        OPCODE_LLD = 0x34,
        OPCODE_LDC1 = 0x35,
        OPCODE_LDC2 = 0x36,
        OPCODE_LD = 0x37,
        OPCODE_SC = 0x38,
        OPCODE_SWC1 = 0x39,
        OPCODE_SWC2 = 0x3A,
        OPCODE_SCD = 0x3C,
        OPCODE_SDC1 = 0x3D,
        OPCODE_SDC2 = 0x3E,
        OPCODE_SD = 0x3F,
        OPCODE_LQC2 = 0x36,
        OPCODE_SQC2 = 0x3E
    };

    // SPECIAL function codes
    enum SpecialFunctions
    {
        SPECIAL_SLL = 0x00,
        SPECIAL_SRL = 0x02,
        SPECIAL_SRA = 0x03,
        SPECIAL_SLLV = 0x04,
        SPECIAL_SRLV = 0x06,
        SPECIAL_SRAV = 0x07,
        SPECIAL_JR = 0x08,
        SPECIAL_JALR = 0x09,
        SPECIAL_MOVZ = 0x0A,
        SPECIAL_MOVN = 0x0B,
        SPECIAL_SYSCALL = 0x0C,
        SPECIAL_BREAK = 0x0D,
        SPECIAL_SYNC = 0x0F,
        SPECIAL_MFHI = 0x10,
        SPECIAL_MTHI = 0x11,
        SPECIAL_MFLO = 0x12,
        SPECIAL_MTLO = 0x13,
        SPECIAL_DSLLV = 0x14,
        SPECIAL_DSRLV = 0x16,
        SPECIAL_DSRAV = 0x17,
        SPECIAL_MULT = 0x18,
        SPECIAL_MULTU = 0x19,
        SPECIAL_DIV = 0x1A,
        SPECIAL_DIVU = 0x1B,
        SPECIAL_ADD = 0x20,
        SPECIAL_ADDU = 0x21,
        SPECIAL_SUB = 0x22,
        SPECIAL_SUBU = 0x23,
        SPECIAL_AND = 0x24,
        SPECIAL_OR = 0x25,
        SPECIAL_XOR = 0x26,
        SPECIAL_NOR = 0x27,
        SPECIAL_MFSA = 0x28,
        SPECIAL_MTSA = 0x29,
        SPECIAL_SLT = 0x2A,
        SPECIAL_SLTU = 0x2B,
        SPECIAL_DADD = 0x2C,
        SPECIAL_DADDU = 0x2D,
        SPECIAL_DSUB = 0x2E,
        SPECIAL_DSUBU = 0x2F,
        SPECIAL_TGE = 0x30,
        SPECIAL_TGEU = 0x31,
        SPECIAL_TLT = 0x32,
        SPECIAL_TLTU = 0x33,
        SPECIAL_TEQ = 0x34,
        SPECIAL_TNE = 0x36,
        SPECIAL_DSLL = 0x38,
        SPECIAL_DSRL = 0x3A,
        SPECIAL_DSRA = 0x3B,
        SPECIAL_DSLL32 = 0x3C,
        SPECIAL_DSRL32 = 0x3E,
        SPECIAL_DSRA32 = 0x3F
    };

    // REGIMM function codes
    enum RegimmFunctions
    {
        REGIMM_BLTZ = 0x00,
        REGIMM_BGEZ = 0x01,
        REGIMM_BLTZL = 0x02,
        REGIMM_BGEZL = 0x03,
        REGIMM_TGEI = 0x08,
        REGIMM_TGEIU = 0x09,
        REGIMM_TLTI = 0x0A,
        REGIMM_TLTIU = 0x0B,
        REGIMM_TEQI = 0x0C,
        REGIMM_TNEI = 0x0E,
        REGIMM_BLTZAL = 0x10,
        REGIMM_BGEZAL = 0x11,
        REGIMM_BLTZALL = 0x12,
        REGIMM_BGEZALL = 0x13,
        REGIMM_MTSAB = 0x18,
        REGIMM_MTSAH = 0x19
    };

    // PS2-specific MMI function codes
    enum MMIFunctions
    {
        MMI_MADD = 0x00,
        MMI_MADDU = 0x01,
        MMI_PLZCW = 0x04,
        MMI_MMI0 = 0x08,
        MMI_MMI2 = 0x09,
        MMI_MFHI1 = 0x10,
        MMI_MTHI1 = 0x11,
        MMI_MFLO1 = 0x12,
        MMI_MTLO1 = 0x13,
        MMI_MULT1 = 0x18,
        MMI_MULTU1 = 0x19,
        MMI_DIV1 = 0x1A,
        MMI_DIVU1 = 0x1B,
        MMI_MADD1 = 0x20,
        MMI_MADDU1 = 0x21,
        MMI_MMI1 = 0x28,
        MMI_MMI3 = 0x29,
        MMI_PMFHL = 0x30,
        MMI_PMTHL = 0x31,
        MMI_PSLLH = 0x34,
        MMI_PSRLH = 0x36,
        MMI_PSRAH = 0x37,
        MMI_PSLLW = 0x3C,
        MMI_PSRLW = 0x3E,
        MMI_PSRAW = 0x3F,
        MMI_MSUB = 0x02,
        MMI_MSUBU = 0x03
    };

    // PS2-specific MMI0 function codes
    enum MMI0Functions
    {
        MMI0_PADDW = 0x00,
        MMI0_PSUBW = 0x01,
        MMI0_PCGTW = 0x02,
        MMI0_PMAXW = 0x03,
        MMI0_PADDH = 0x04,
        MMI0_PSUBH = 0x05,
        MMI0_PCGTH = 0x06,
        MMI0_PMAXH = 0x07,
        MMI0_PADDB = 0x08,
        MMI0_PSUBB = 0x09,
        MMI0_PCGTB = 0x0A,
        MMI0_PADDSW = 0x10,
        MMI0_PSUBSW = 0x11,
        MMI0_PEXTLW = 0x12,
        MMI0_PPACW = 0x13,
        MMI0_PADDSH = 0x14,
        MMI0_PSUBSH = 0x15,
        MMI0_PEXTLH = 0x16,
        MMI0_PPACH = 0x17,
        MMI0_PADDSB = 0x18,
        MMI0_PSUBSB = 0x19,
        MMI0_PEXTLB = 0x1A,
        MMI0_PPACB = 0x1B,
        MMI0_PEXT5 = 0x1E,
        MMI0_PPAC5 = 0x1F
    };

    // PS2-specific MMI1 function codes
    enum MMI1Functions
    {
        MMI1_PABSW = 0x01,
        MMI1_PCEQW = 0x02,
        MMI1_PMINW = 0x03,
        MMI1_PADSBH = 0x04,
        MMI1_PABSH = 0x05,
        MMI1_PCEQH = 0x06,
        MMI1_PMINH = 0x07,
        MMI1_PCEQB = 0x0A,
        MMI1_PADDUW = 0x10,
        MMI1_PSUBUW = 0x11,
        MMI1_PEXTUW = 0x12,
        MMI1_PADDUH = 0x14,
        MMI1_PSUBUH = 0x15,
        MMI1_PEXTUH = 0x16,
        MMI1_PADDUB = 0x18,
        MMI1_PSUBUB = 0x19,
        MMI1_PEXTUB = 0x1A,
        MMI1_QFSRV = 0x1B
    };

    // PS2-specific MMI2 function codes
    enum MMI2Functions
    {
        MMI2_PMADDW = 0x00,
        MMI2_PSLLVW = 0x02,
        MMI2_PSRLVW = 0x03,
        MMI2_PMSUBW = 0x04,
        MMI2_PMFHI = 0x08,
        MMI2_PMFLO = 0x09,
        MMI2_PINTH = 0x0A,
        MMI2_PMULTW = 0x0C,
        MMI2_PDIVW = 0x0D,
        MMI2_PCPYLD = 0x0E,
        MMI2_PAND = 0x12,
        MMI2_PXOR = 0x13,
        MMI2_PMADDH = 0x14,
        MMI2_PHMADH = 0x15,
        MMI2_PAND_ = 0x16,
        MMI2_PXOR_ = 0x17,
        MMI2_PMSUBH = 0x18,
        MMI2_PHMSBH = 0x19,
        MMI2_PEXEH = 0x1A,
        MMI2_PREVH = 0x1B,
        MMI2_PMULTH = 0x1C,
        MMI2_PDIVBW = 0x1D,
        MMI2_PEXEW = 0x1E,
        MMI2_PROT3W = 0x1F
    };

    // PS2-specific MMI3 function codes
    enum MMI3Functions
    {
        MMI3_PMADDUW = 0x00,
        MMI3_PSRAVW = 0x03,
        MMI3_PINTEH = 0x0A,
        MMI3_PMULTUW = 0x0C,
        MMI3_PDIVUW = 0x0D,
        MMI3_PCPYUD = 0x0E,
        MMI3_POR = 0x12,
        MMI3_PNOR = 0x13,
        MMI3_PEXCH = 0x1A,
        MMI3_PCPYH = 0x1B,
        MMI3_PEXCW = 0x1E,
        MMI3_PMTHI = 0x08, // Move To HI register
        MMI3_PMTLO = 0x09 // Move To LO register
    };

    // COP0 (rs field)
    enum COP0Format
    {
        COP0_MF = 0x00, // Move From COP0
        COP0_MT = 0x04, // Move To COP0
        COP0_CO = 0x10  // COP0 operation
    };

    // COP0 CO (COProcessor) function codes
    enum Cop0CoFunctions
    {
        COP0_CO_TLBR = 0x01,  // TLB Read
        COP0_CO_TLBWI = 0x02, // TLB Write Indexed
        COP0_CO_TLBWR = 0x06, // TLB Write Random
        COP0_CO_TLBP = 0x08,  // TLB Probe
        COP0_CO_ERET = 0x18,  // Return from Exception
        COP0_CO_EI = 0x38,    // Enable Interrupts
        COP0_CO_DI = 0x39     // Disable Interrupts
    };

    enum COP0Reg
    {
        COP0_REG_INDEX = 0,     // Index into TLB array
        COP0_REG_RANDOM = 1,    // Randomly generated index into TLB array
        COP0_REG_ENTRYLO0 = 2,  // Low half of TLB entry for even-numbered virtual pages
        COP0_REG_ENTRYLO1 = 3,  // Low half of TLB entry for odd-numbered virtual pages
        COP0_REG_CONTEXT = 4,   // TLB miss handler pointer
        COP0_REG_PAGEMASK = 5,  // TLB page size mask
        COP0_REG_WIRED = 6,     // Controls which TLB entries are affected by random replacement
        COP0_REG_BADVADDR = 8,  // Virtual address of most recent address-related exception
        COP0_REG_COUNT = 9,     // Timer count
        COP0_REG_ENTRYHI = 10,  // High half of TLB entry
        COP0_REG_COMPARE = 11,  // Timer compare value
        COP0_REG_STATUS = 12,   // Processor status and control
        COP0_REG_CAUSE = 13,    // Cause of last exception
        COP0_REG_EPC = 14,      // Exception program counter
        COP0_REG_PRID = 15,     // Processor identification and revision
        COP0_REG_CONFIG = 16,   // Configuration register
        COP0_REG_BADPADDR = 23, // Bad physical address
        COP0_REG_DEBUG = 24,    // Debug register
        COP0_REG_PERF = 25,     // Performance counter
        COP0_REG_TAGLO = 28,    // Low bits of cache tag
        COP0_REG_TAGHI = 29,    // High bits of cache tag
        COP0_REG_ERROREPC = 30  // Error exception program counter
    };

    // COP1 (FPU) function codes
    enum Cop1Format
    {
        COP1_MF = 0x00, // Move From FPU Register
        COP1_CF = 0x02, // Move From FPU Control Register
        COP1_MT = 0x04, // Move To FPU Register
        COP1_CT = 0x06, // Move To FPU Control Register
        COP1_BC = 0x08, // Branch on FPU Condition
        COP1_S = 0x10,  // Single Precision Operation
        COP1_W = 0x14,  // Word (integer) Operation
        COP1_BC_BCF = 0x00,
        COP1_BC_BCT = 0x01,
        COP1_L = 0x15,  // Long (64-bit integer) Operation
        COP1_D = 0x11,  // Double Precision Operation (unused on PS2 FPU?)
    };

    enum Cop1Functions
    {
        COP1_FUNC_ADD = 0x00,
        COP1_FUNC_SUB = 0x01,
        COP1_FUNC_MUL = 0x02,
        COP1_FUNC_DIV = 0x03,
        COP1_FUNC_SQRT = 0x04,
        COP1_FUNC_ABS = 0x05,
        COP1_FUNC_MOV = 0x06,
        COP1_FUNC_NEG = 0x07,
        COP1_FUNC_ROUND_L = 0x08,
        COP1_FUNC_TRUNC_L = 0x09,
        COP1_FUNC_CEIL_L = 0x0A,
        COP1_FUNC_FLOOR_L = 0x0B,
        COP1_FUNC_ROUND_W = 0x0C,
        COP1_FUNC_TRUNC_W = 0x0D,
        COP1_FUNC_CEIL_W = 0x0E,
        COP1_FUNC_FLOOR_W = 0x0F,
        COP1_FUNC_CVT_S = 0x20, // Convert to Single
        COP1_FUNC_CVT_D = 0x21, // Convert to Double
        COP1_FUNC_CVT_W = 0x24, // Convert to Word
        COP1_FUNC_CVT_L = 0x25, // Convert to Long
        // Comparisons (lowest 5 bits matter for condition, C bit set in FCR31)
        COP1_FUNC_C_F = 0x30,    // False
        COP1_FUNC_C_UN = 0x31,   // Unordered
        COP1_FUNC_C_EQ = 0x32,   // Equal
        COP1_FUNC_C_UEQ = 0x33,  // Unordered or Equal
        COP1_FUNC_C_OLT = 0x34,  // Ordered Less Than
        COP1_FUNC_C_ULT = 0x35,  // Unordered or Less Than
        COP1_FUNC_C_OLE = 0x36,  // Ordered Less Than or Equal
        COP1_FUNC_C_ULE = 0x37,  // Unordered or Less Than or Equal
        COP1_FUNC_C_SF = 0x38,   // Signaling False
        COP1_FUNC_C_NGLE = 0x39, // Not Greater or Less or Equal (Unordered)
        COP1_FUNC_C_SEQ = 0x3A,  // Signaling Equal
        COP1_FUNC_C_NGL = 0x3B,  // Not Greater or Less (Unordered or Equal)
        COP1_FUNC_C_LT = 0x3C,   // Signaling Less Than
        COP1_FUNC_C_NGE = 0x3D,  // Not Greater or Equal (Unordered or Less Than)
        COP1_FUNC_C_LE = 0x3E,   // Signaling Less Than or Equal
        COP1_FUNC_C_NGT = 0x3F,  // Not Greater Than (Unordered or Less Than or Equal)
    };
     

    // COP2 (VU0 macro) function codes
    enum Cop2Functions
    {
        COP2_QMFC2 = 0x00, // Move From Coprocessor 2 (128-bit)
        COP2_CFC2 = 0x02,  // Move Control From Coprocessor 2
        COP2_QMTC2 = 0x04, // Move To Coprocessor 2 (128-bit)
        COP2_CTC2 = 0x06,  // Move Control To Coprocessor 2
        COP2_BC2 = 0x08,   // Branch On Coprocessor 2 Condition
        COP2_CO = 0x10,    // COProcessor instructions (VU0 macro)
        COP2_BCF = 0x00,   // Branch on VU0 false
        COP2_BCT = 0x01,   // Branch on VU0 true
        COP2_BCFL = 0x02,  // Branch on VU0 false likely
        COP2_BCTL = 0x03,  // Branch on VU0 true likely
        COP2_BCEF = 0x4,   // Branch on VU0 equal flag false (new)
        COP2_BCET = 0x5,   // Branch on VU0 equal flag true (new)
        COP2_BCEFL = 0x6,  // Branch on VU0 equal flag false likely (new)
        COP2_BCETL = 0x7,  // Branch on VU0 equal flag true likely (new)
        COP2_MFC2 = 0x02,
        COP2_MTC2 = 0x0A,
        COP2_BC2F = 0x11,   // Branch on VU0 condition false
        COP2_VU0OPS = 0x12, // VU0 Special Operations
        COP2_MTVUCF = 0x13, // Move To VU0 control/flag register
        COP2_CTCVU = 0x18,  // Control To VU0 with specific functionality
        COP2_VMTIR = 0x1C,  // Move To VU0 I Register
        COP2_VCLIP = 0x1E,  // VU0 Clipping operation
        COP2_VLDQ = 0x1F    // VU0 Load/Store Quad with Decrement
    };

    // VU0 macro instruction function codes (subset - there are many more)
    enum VU0MacroFunctions
    {
        VU0_VADD = 0x00,
        VU0_VSUB = 0x01,
        VU0_VMUL = 0x02,
        VU0_VDIV = 0x03,
        VU0_VSQRT = 0x04,
        VU0_VRSQRT = 0x05,
        VU0_VMULQ = 0x06,
        VU0_VIADD = 0x10,
        VU0_VISUB = 0x11,
        VU0_VIADDI = 0x12,
        VU0_VIAND = 0x13,
        VU0_VIOR = 0x14,
        VU0_VILWR = 0x15,
        VU0_VISWR = 0x16,
        VU0_VCALLMS = 0x20,
        VU0_VCALLMSR = 0x21,
        VU0_VRGET = 0x3F // Get VU0 R register
    };

    enum VU0SpecialFormats
    {
        VU0_BC2F = 0x11,   // Branch on VU0 condition false
        VU0_MTVUCF = 0x13, // Move To VU0 control/flag register
        VU0_CTCVU = 0x18,  // Control To VU0 with specific functionality
        VU0_VMTIR = 0x1C,  // Move To VU0 I Register
        VU0_VCLIP = 0x1E,  // VU0 Clipping operation
        VU0_VLDQ = 0x1F    // VU0 Load/Store Quad with Decrement
    };

    enum VU0ControlRegisters
    {
        VU0_CR_STATUS = 0,  // Status/Control register
        VU0_CR_MAC = 1,     // MAC flags register
        VU0_CR_CLIP = 5,    // Clipping flags register
        VU0_CR_CMSAR0 = 13, // VU0 microprogram start address register
        VU0_CR_FBRST = 18   // VIF/VU0/VU1 reset register
    };

    enum VU0OPSFunctions
    {
        VU0OPS_QMFC2_NI = 0x00, // Non-incrementing QMFC2
        VU0OPS_QMFC2_I = 0x01,  // Incrementing QMFC2
        VU0OPS_QMTC2_NI = 0x02, // Non-incrementing QMTC2
        VU0OPS_QMTC2_I = 0x03,  // Incrementing QMTC2
        VU0OPS_VMFIR = 0x04,    // Move From Integer Register
        VU0OPS_VXITOP = 0x08,   // Execute Interrupt on VU0
        VU0OPS_VWAITQ = 0x3C    // Wait for Q register operations to complete
    };

    // PMFHL functions (sa field)
    enum PMFHLFunctions
    {
        PMFHL_LW = 0x00,
        PMFHL_UW = 0x01,
        PMFHL_SLW = 0x02,
        PMFHL_LH = 0x03,
        PMFHL_SH = 0x04
    };

// Instruction decoding helper macros
#define OPCODE(inst) ((inst >> 26) & 0x3F)
#define RS(inst) ((inst >> 21) & 0x1F)
#define RT(inst) ((inst >> 16) & 0x1F)
#define RD(inst) ((inst >> 11) & 0x1F)
#define SA(inst) ((inst >> 6) & 0x1F)
#define FUNCTION(inst) ((inst) & 0x3F)
#define IMMEDIATE(inst) ((inst) & 0xFFFF)
#define SIMMEDIATE(inst) ((int16_t)((inst) & 0xFFFF))
#define TARGET(inst) ((inst) & 0x3FFFFFF)

} // namespace ps2recomp

#endif // PS2RECOMP_INSTRUCTIONS_H