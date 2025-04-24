#ifndef PS2RECOMP_INSTRUCTIONS_H
#define PS2RECOMP_INSTRUCTIONS_H

#include <cstdint>

namespace ps2recomp
{
    // Basic MIPS opcodes (shared with R4300i)
    enum MipsOpcodes
    {
        OPCODE_SPECIAL = 0x00, // Special instructions group (see SpecialFunctions)
        OPCODE_REGIMM = 0x01,  // RegImm instructions group (see RegimmFunctions)
        OPCODE_J = 0x02,       // Jump
        OPCODE_JAL = 0x03,     // Jump and Link
        OPCODE_BEQ = 0x04,     // Branch on Equal
        OPCODE_BNE = 0x05,     // Branch on Not Equal
        OPCODE_BLEZ = 0x06,    // Branch on Less Than or Equal to Zero
        OPCODE_BGTZ = 0x07,    // Branch on Greater Than Zero

        OPCODE_ADDI = 0x08,  // Add Immediate Word
        OPCODE_ADDIU = 0x09, // Add Immediate Unsigned Word
        OPCODE_SLTI = 0x0A,  // Set on Less Than Immediate
        OPCODE_SLTIU = 0x0B, // Set on Less Than Immediate Unsigned
        OPCODE_ANDI = 0x0C,  // AND Immediate
        OPCODE_ORI = 0x0D,   // OR Immediate
        OPCODE_XORI = 0x0E,  // XOR Immediate
        OPCODE_LUI = 0x0F,   // Load Upper Immediate

        OPCODE_COP0 = 0x10, // Coprocessor 0 instructions (see Cop0Format)
        OPCODE_COP1 = 0x11, // Coprocessor 1 (FPU) instructions (see Cop1Format)
        OPCODE_COP2 = 0x12, // Coprocessor 2 (VU0 Macro) instructions (see Cop2Format)
        OPCODE_COP3 = 0x13, // Unused on PS2

        OPCODE_BEQL = 0x14,  // Branch on Equal Likely
        OPCODE_BNEL = 0x15,  // Branch on Not Equal Likely
        OPCODE_BLEZL = 0x16, // Branch on Less Than or Equal to Zero Likely
        OPCODE_BGTZL = 0x17, // Branch on Greater Than Zero Likely

        OPCODE_DADDI = 0x18,  // Doubleword Add Immediate
        OPCODE_DADDIU = 0x19, // Doubleword Add Immediate Unsigned
        OPCODE_LDL = 0x1A,    // Load Doubleword Left
        OPCODE_LDR = 0x1B,    // Load Doubleword Right
        OPCODE_MMI = 0x1C,    // PS2 specific MMI instructions group (see MMIFunctions)

        OPCODE_LQ = 0x1E, // PS2 specific 128-bit load
        OPCODE_SQ = 0x1F, // PS2 specific 128-bit store

        OPCODE_LB = 0x20,  // Load Byte
        OPCODE_LH = 0x21,  // Load Halfword
        OPCODE_LWL = 0x22, // Load Word Left
        OPCODE_LW = 0x23,  // Load Word
        OPCODE_LBU = 0x24, // Load Byte Unsigned
        OPCODE_LHU = 0x25, // Load Halfword Unsigned
        OPCODE_LWR = 0x26, // Load Word Right
        OPCODE_LWU = 0x27, // Load Word Unsigned

        OPCODE_SB = 0x28,    // Store Byte
        OPCODE_SH = 0x29,    // Store Halfword
        OPCODE_SWL = 0x2A,   // Store Word Left
        OPCODE_SW = 0x2B,    // Store Word
        OPCODE_SDL = 0x2C,   // Store Doubleword Left
        OPCODE_SDR = 0x2D,   // Store Doubleword Right
        OPCODE_SWR = 0x2E,   // Store Word Right
        OPCODE_CACHE = 0x2F, // Cache Operation

        OPCODE_LL = 0x30,   // LL - Load Linked Word, maybe omitted/unused
        OPCODE_LWC1 = 0x31, // Load Word to Coprocessor 1 (FPU)
        OPCODE_LWC2 = 0x32, // Load Word to Coprocessor 2, maybe omitted/unused
        OPCODE_PREF = 0x33, // Prefetch
        OPCODE_LLD = 0x34,  // Load Linked Doubleword, maybe omitted/unused
        OPCODE_LDC1 = 0x35, // Load Doubleword to Coprocessor 1, FPU is single-precision
        OPCODE_LDC2 = 0x36, // Load Quadword to Coprocessor 2 (VU0) - Overrides standard MIPS LDC2
        OPCODE_LD = 0x37,   // Load Doubleword

        OPCODE_SC = 0x38,   // Store Conditional Word, maybe omitted/unused?
        OPCODE_SWC1 = 0x39, // Store Word from Coprocessor 1 (FPU)
        OPCODE_SWC2 = 0x3A, // SWC2 - Store Word from Coprocessor 2, maybe omitted/unused
        OPCODE_SCD = 0x3C,  // SCD - Store Conditional Doubleword, maybe omitted/unused?
        OPCODE_SDC1 = 0x3D, // SDC1 - Store Doubleword from Coprocessor 1, FPU is single-precision
        OPCODE_SDC2 = 0x3E, // PS2 specific Store Quadword from Coprocessor 2 (VU0) - Overrides standard MIPS SDC2
        OPCODE_SD = 0x3F,   // Store Doubleword

        //OPCODE_LQC2 = 0x36,
        //OPCODE_SQC2 = 0x3E
    };

    // SPECIAL Function (bits 5-0) for OPCODE_SPECIAL
    enum SpecialFunctions
    {
        SPECIAL_SLL = 0x00, // Shift Word Left Logical

        SPECIAL_SRL = 0x02,  // Shift Word Right Logical
        SPECIAL_SRA = 0x03,  // Shift Word Right Arithmetic
        SPECIAL_SLLV = 0x04, // Shift Word Left Logical Variable

        SPECIAL_SRLV = 0x06, // Shift Word Right Logical Variable
        SPECIAL_SRAV = 0x07, // Shift Word Right Arithmetic Variable

        SPECIAL_JR = 0x08,      // Jump Register
        SPECIAL_JALR = 0x09,    // Jump and Link Register
        SPECIAL_MOVZ = 0x0A,    // Move Conditional on Zero
        SPECIAL_MOVN = 0x0B,    // Move Conditional on Not Zero
        SPECIAL_SYSCALL = 0x0C, // System Call
        SPECIAL_BREAK = 0x0D,   // Breakpoint

        SPECIAL_SYNC = 0x0F, // Synchronize Shared Memory

        SPECIAL_MFHI = 0x10,  // Move From HI Register
        SPECIAL_MTHI = 0x11,  // Move To HI Register
        SPECIAL_MFLO = 0x12,  // Move From LO Register
        SPECIAL_MTLO = 0x13,  // Move To LO Register
        SPECIAL_DSLLV = 0x14, // Doubleword Shift Left Logical Variable

        SPECIAL_DSRLV = 0x16, // Doubleword Shift Right Logical Variable
        SPECIAL_DSRAV = 0x17, // Doubleword Shift Right Arithmetic Variable

        SPECIAL_MULT = 0x18,  // Multiply Word
        SPECIAL_MULTU = 0x19, // Multiply Unsigned Word
        SPECIAL_DIV = 0x1A,   // Divide Word
        SPECIAL_DIVU = 0x1B,  // Divide Unsigned Word

        SPECIAL_ADD = 0x20,  // Add Word
        SPECIAL_ADDU = 0x21, // Add Unsigned Word
        SPECIAL_SUB = 0x22,  // Subtract Word
        SPECIAL_SUBU = 0x23, // Subtract Unsigned Word
        SPECIAL_AND = 0x24,  // AND
        SPECIAL_OR = 0x25,   // OR
        SPECIAL_XOR = 0x26,  // XOR
        SPECIAL_NOR = 0x27,  // NOR

        SPECIAL_MFSA = 0x28,  // Move From SA Register (PS2 specific)
        SPECIAL_MTSA = 0x29,  // Move To SA Register (PS2 specific)
        SPECIAL_SLT = 0x2A,   // Set on Less Than
        SPECIAL_SLTU = 0x2B,  // Set on Less Than Unsigned
        SPECIAL_DADD = 0x2C,  // Doubleword Add
        SPECIAL_DADDU = 0x2D, // Doubleword Add Unsigned
        SPECIAL_DSUB = 0x2E,  // Doubleword Subtract
        SPECIAL_DSUBU = 0x2F, // Doubleword Subtract Unsigned

        SPECIAL_TGE = 0x30,  // Trap if Greater or Equal
        SPECIAL_TGEU = 0x31, // Trap if Greater or Equal Unsigned
        SPECIAL_TLT = 0x32,  // Trap if Less Than
        SPECIAL_TLTU = 0x33, // Trap if Less Than Unsigned
        SPECIAL_TEQ = 0x34,  // Trap if Equal

        SPECIAL_TNE = 0x36, // Trap if Not Equal

        SPECIAL_DSLL = 0x38, // Doubleword Shift Left Logical

        SPECIAL_DSRL = 0x3A,   // Doubleword Shift Right Logical
        SPECIAL_DSRA = 0x3B,   // Doubleword Shift Right Arithmetic
        SPECIAL_DSLL32 = 0x3C, // Doubleword Shift Left Logical +32

        SPECIAL_DSRL32 = 0x3E, // Doubleword Shift Right Logical +32
        SPECIAL_DSRA32 = 0x3F, // Doubleword Shift Right Arithmetic +32
    };

    // REGIMM RT Field (bits 20-16) for OPCODE_REGIMM
    enum RegimmFunctions
    {
        REGIMM_BLTZ = 0x00,  // Branch on Less Than Zero
        REGIMM_BGEZ = 0x01,  // Branch on Greater Than or Equal to Zero
        REGIMM_BLTZL = 0x02, // Branch on Less Than Zero Likely
        REGIMM_BGEZL = 0x03, // Branch on Greater Than or Equal to Zero Likely

        REGIMM_TGEI = 0x08,  // Trap if Greater or Equal Immediate
        REGIMM_TGEIU = 0x09, // Trap if Greater or Equal Immediate Unsigned
        REGIMM_TLTI = 0x0A,  // Trap if Less Than Immediate
        REGIMM_TLTIU = 0x0B, // Trap if Less Than Immediate Unsigned
        REGIMM_TEQI = 0x0C,  // Trap if Equal Immediate

        REGIMM_TNEI = 0x0E, // Trap if Not Equal Immediate

        REGIMM_BLTZAL = 0x10,  // Branch on Less Than Zero and Link
        REGIMM_BGEZAL = 0x11,  // Branch on Greater Than or Equal to Zero and Link
        REGIMM_BLTZALL = 0x12, // Branch on Less Than Zero and Link Likely
        REGIMM_BGEZALL = 0x13, // Branch on Greater Than or Equal to Zero and Link Likely

        REGIMM_MTSAB = 0x18, // Move To SA Register Byte (PS2 specific)
        REGIMM_MTSAH = 0x19, // Move To SA Register Halfword (PS2 specific)
    };

    // MMI Function Field (bits 5-0) for OPCODE_MMI (0x1C)
    enum MMIFunctions
    {
        MMI_MADD = 0x00,  // Multiply Add Word (to LO/HI)
        MMI_MADDU = 0x01, // Multiply Add Unsigned Word (to LO/HI)
        MMI_MSUB = 0x02,  // Multiply Subtract Word (Not listed in provided doc table, but valid MMI)
        MMI_MSUBU = 0x03, // Multiply Subtract Unsigned Word (Not listed in provided doc table, but valid MMI)
        MMI_PLZCW = 0x04, // Parallel Leading Zero/One Count Word

        MMI_MMI0 = 0x08, // Group MMI0 instructions (see MMI0Functions)
        MMI_MMI2 = 0x09, // Group MMI2 instructions (see MMI2Functions)

        MMI_MFHI1 = 0x10, // Move From HI1 Register
        MMI_MTHI1 = 0x11, // Move To HI1 Register
        MMI_MFLO1 = 0x12, // Move From LO1 Register
        MMI_MTLO1 = 0x13, // Move To LO1 Register

        MMI_MULT1 = 0x18,  // Multiply Word to LO1/HI1
        MMI_MULTU1 = 0x19, // Multiply Unsigned Word to LO1/HI1
        MMI_DIV1 = 0x1A,   // Divide Word using LO1/HI1
        MMI_DIVU1 = 0x1B,  // Divide Unsigned Word using LO1/HI1

        MMI_MADD1 = 0x20,  // Multiply Add Word to LO1/HI1
        MMI_MADDU1 = 0x21, // Multiply Add Unsigned Word to LO1/HI1

        MMI_MMI1 = 0x28, // Group MMI1 instructions (see MMI1Functions)
        MMI_MMI3 = 0x29, // Group MMI3 instructions (see MMI3Functions)

        MMI_PMFHL = 0x30, // Parallel Move From HI/LO Register (see PMFHLFunctions for 'sa' field)
        MMI_PMTHL = 0x31, // Parallel Move To HI/LO Register (see PMFHLFunctions for 'sa' field)

        MMI_PSLLH = 0x34, // Parallel Shift Left Logical Halfword

        MMI_PSRLH = 0x36, // Parallel Shift Right Logical Halfword
        MMI_PSRAH = 0x37, // Parallel Shift Right Arithmetic Halfword

        MMI_PSLLW = 0x3C, // Parallel Shift Left Logical Word

        MMI_PSRLW = 0x3E, // Parallel Shift Right Logical Word
        MMI_PSRAW = 0x3F  // Parallel Shift Right Arithmetic Word
    };

    // PS2-specific MMI0 Sub-Function Field (bits 0-5 within MMI_MMI0 group)
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

    // PS2-specific MMI1 Sub-Function Field (bits 0-5 within MMI_MMI1 group)
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

    // PS2-specific MMI2 Sub-Function Field (bits 0-5 within MMI_MMI2 group)
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
        MMI2_PMSUBH = 0x18,
        MMI2_PHMSBH = 0x19,
        MMI2_PEXEH = 0x1A,
        MMI2_PREVH = 0x1B,
        MMI2_PMULTH = 0x1C,
        MMI2_PDIVBW = 0x1D,
        MMI2_PEXEW = 0x1E,
        MMI2_PROT3W = 0x1F
    };

    // PS2-specific MMI3 Sub-Function Field (bits 0-5 within MMI_MMI3 group)
    enum MMI3Functions
    {
        MMI3_PMADDUW = 0x00,
        MMI3_PSRAVW = 0x03,
        MMI3_PMTHI = 0x08, // Move To HI register
        MMI3_PMTLO = 0x09, // Move To LO register
        MMI3_PINTEH = 0x0A,
        MMI3_PMULTUW = 0x0C,
        MMI3_PDIVUW = 0x0D,
        MMI3_PCPYUD = 0x0E,
        MMI3_POR = 0x12,
        MMI3_PNOR = 0x13,
        MMI3_PEXCH = 0x1A,
        MMI3_PCPYH = 0x1B,
        MMI3_PEXCW = 0x1E
    };

    // PMFHL/PMTHL Sub-Function Field ('sa' field, bits 10-6)
    enum PMFHLFunctions
    {
        PMFHL_LW = 0x00,  // Load Word / Lower Word
        PMFHL_UW = 0x01,  // Upper Word
        PMFHL_SLW = 0x02, // Signed Lower Word
        PMFHL_LH = 0x03,  // Load Halfword / Lower Halfword
        PMFHL_SH = 0x04   // Store Halfword / Signed Upper Halfword? (Doc name varies)
    };

    // COP0 Format Field ('fmt' or 'rs' field, bits 25-21) for OPCODE_COP0
    enum COP0Format
    {
        COP0_MF = 0x00, // Move From COP0 - MFC0 (fmt=00000)
        COP0_MT = 0x04, // Move To COP0 - MTC0 (fmt=00100)
        COP0_BC = 0x08, // BC0 group (fmt=01000) (see Cop0BranchCondition)
        COP0_CO = 0x10  // COP0 Operation group (fmt=10000) (see Cop0CoFunctions)
    };

    // COP0 CO Function Field (bits 5-0) for COP0_CO Format
    enum Cop0CoFunctions
    {
        COP0_CO_TLBR = 0x01,  // TLB Read
        COP0_CO_TLBWI = 0x02, // TLB Write Indexed

        COP0_CO_TLBWR = 0x06, // TLB Write Random

        COP0_CO_TLBP = 0x08, // TLB Probe

        COP0_CO_ERET = 0x18, // Return from Exception

        COP0_CO_EI = 0x38, // Enable Interrupts
        COP0_CO_DI = 0x39  // Disable Interrupts
    };

    // COP0 BC Condition Field ('fmt' or 'rt' field, bits 20-16) for COP0_BC Format
    enum Cop0BranchCondition
    {
        COP0_BC_BCF = 0x00,  // Branch on Condition False
        COP0_BC_BCT = 0x01,  // Branch on Condition True
        COP0_BC_BCFL = 0x02, // Branch on Condition False Likely
        COP0_BC_BCTL = 0x03, // Branch on Condition True Likely
    };

    // COP0 Register Numbers (used with MFC0/MTC0)
    enum COP0Reg
    {
        COP0_REG_INDEX = 0,    // Index into TLB array
        COP0_REG_RANDOM = 1,   // Randomly generated index into TLB array
        COP0_REG_ENTRYLO0 = 2, // Low half of TLB entry for even-numbered virtual pages
        COP0_REG_ENTRYLO1 = 3, // Low half of TLB entry for odd-numbered virtual pages
        COP0_REG_CONTEXT = 4,  // Context
        COP0_REG_PAGEMASK = 5, // TLB page size mask
        COP0_REG_WIRED = 6,    // Controls which TLB entries are affected by random replacement

        COP0_REG_BADVADDR = 8, // Virtual address of most recent address-related exception
        COP0_REG_COUNT = 9,    // Timer count
        COP0_REG_ENTRYHI = 10, // High half of TLB entry
        COP0_REG_COMPARE = 11, // Timer compare value
        COP0_REG_STATUS = 12,  // Processor status and control
        COP0_REG_CAUSE = 13,   // Cause of last exception
        COP0_REG_EPC = 14,     // Exception program counter
        COP0_REG_PRID = 15,    // Processor identification and revision
        COP0_REG_CONFIG = 16,  // Configuration register

        COP0_REG_BADPADDR = 23, // Bad physical address
        COP0_REG_DEBUG = 24,    // Debug register
        COP0_REG_PERF = 25,     // Performance counter

        COP0_REG_TAGLO = 28,   // Cache TagLo (I-Cache & D-Cache)
        COP0_REG_TAGHI = 29,   // High bits of cache tag ( Cache TagHi )
        COP0_REG_ERROREPC = 30 // Error exception program counter
    };

    // COP1 (FPU) Format Field ('fmt' or 'rs' field, bits 25-21) for OPCODE_COP1
    enum Cop1Format
    {
        COP1_MF = 0x00, // Move From FPU Register
        COP1_CF = 0x02, // Move From FPU Control Register
        COP1_MT = 0x04, // Move To FPU Register
        COP1_CT = 0x06, // Move To FPU Control Register
        COP1_BC = 0x08, // Branch on FPU Condition (see Cop1BranchCondition)

        COP1_D = 0x11, // Standard MIPS Double format; EE FPU is single-precision, (unused on PS2 FPU?)
        COP1_L = 0x15, // Long (64-bit integer) Operation (unused on PS2 FPU?)

        COP1_S = 0x10, // Single Precision Operation (see Cop1FunctionsS)
        COP1_W = 0x14  // Word (integer) Operation (see Cop1FunctionsW)
    };

    // COP1 Function Field (bits 5-0) for COP1_S Format (Single-Precision)
    enum Cop1FunctionsS
    {
        COP1_S_ADD = 0x00,  // ADD.S
        COP1_S_SUB = 0x01,  // SUB.S
        COP1_S_MUL = 0x02,  // MUL.S
        COP1_S_DIV = 0x03,  // DIV.S
        COP1_S_SQRT = 0x04, // SQRT.S
        COP1_S_ABS = 0x05,  // ABS.S
        COP1_S_MOV = 0x06,  // MOV.S
        COP1_S_NEG = 0x07,  // NEG.S

        COP1_S_ROUND_L = 0x08, // ROUND.L.S - Requires 64-bit int dest, likely unused
        COP1_S_TRUNC_L = 0x09, // TRUNC.L.S - Requires 64-bit int dest, likely unused
        COP1_S_CEIL_L = 0x0A,  // CEIL.L.S  - Requires 64-bit int dest, likely unused
        COP1_S_FLOOR_L = 0x0B, // FLOOR.L.S - Requires 64-bit int dest, likely unused
        COP1_S_ROUND_W = 0x0C, // ROUND.W.S
        COP1_S_TRUNC_W = 0x0D, // TRUNC.W.S
        COP1_S_CEIL_W = 0x0E,  // CEIL.W.S
        COP1_S_FLOOR_W = 0x0F, // FLOOR.W.S

        // --- Additional Arithmetic/Functions (from FPU.S table) ---
        COP1_S_RSQRT = 0x16, // RSQRT.S (Reciprocal Square Root)
        COP1_S_ADDA = 0x18,  // ADDA.S (Add Accumulator)
        COP1_S_SUBA = 0x19,  // SUBA.S (Subtract Accumulator)
        COP1_S_MULA = 0x1A,  // MULA.S (Multiply Accumulator)
        COP1_S_MADD = 0x1C,  // MADD.S (Multiply Add)
        COP1_S_MSUB = 0x1D,  // MSUB.S (Multiply Subtract)
        COP1_S_MADDA = 0x1E, // MADDA.S (Multiply Add Accumulator)
        COP1_S_MSUBA = 0x1F, // MSUBA.S (Multiply Subtract Accumulator)

        // --- Conversions (from FPU.S table) ---
        COP1_S_CVT_D = 0x21, // CVT.D.S - Requires double-precision FPU
        COP1_S_CVT_W = 0x24, // CVT.W.S (Convert to Word)
        COP1_S_CVT_L = 0x25, // CVT.L.S - Requires 64-bit int dest, likely unused

        // --- Min/Max (from FPU.S table) ---
        COP1_S_MAX = 0x28, // MAX.S
        COP1_S_MIN = 0x29, // MIN.S

        COP1_S_C_F = 0x30,    // C.F.S (False)
        COP1_S_C_UN = 0x31,   // C.UN.S (Unordered)
        COP1_S_C_EQ = 0x32,   // C.EQ.S (Equal)
        COP1_S_C_UEQ = 0x33,  // C.UEQ.S (Unordered or Equal)
        COP1_S_C_OLT = 0x34,  // C.OLT.S (Ordered Less Than)
        COP1_S_C_ULT = 0x35,  // C.ULT.S (Unordered or Less Than)
        COP1_S_C_OLE = 0x36,  // C.OLE.S (Ordered Less Than or Equal)
        COP1_S_C_ULE = 0x37,  // C.ULE.S (Unordered or Less Than or Equal)
        COP1_S_C_SF = 0x38,   // C.SF.S (Signaling False)
        COP1_S_C_NGLE = 0x39, // C.NGLE.S (Not Greater or Less or Equal - Unordered)
        COP1_S_C_SEQ = 0x3A,  // C.SEQ.S (Signaling Equal)
        COP1_S_C_NGL = 0x3B,  // C.NGL.S (Not Greater or Less - Unordered or Equal)
        COP1_S_C_LT = 0x3C,   // C.LT.S (Signaling Less Than)
        COP1_S_C_NGE = 0x3D,  // C.NGE.S (Not Greater or Equal - Unordered or Less Than)
        COP1_S_C_LE = 0x3E,   // C.LE.S (Signaling Less Than or Equal)
        COP1_S_C_NGT = 0x3F,  // C.NGT.S (Not Greater Than - Unordered or Less Than or Equal)
    };

    // COP1 Function Field (bits 5-0) for COP1_W Format (Word/Integer source)
    enum Cop1FunctionsW
    {
        COP1_W_CVT_S = 0x20, // CVT.S.W (Convert Word Integer to Single Float)
                             // 0x21 --- (Standard MIPS: CVT.D.W - requires double-precision)
                             // 0x24 --- (Standard MIPS: CVT.W.W - Nop?)
                             // 0x25 --- (Standard MIPS: CVT.L.W - requires 64-bit int dest)
    };

    // COP1 BC Condition Field ('fmt' or 'rt' field, bits 20-16) for COP1_BC Format
    enum Cop1BranchCondition
    {
        COP1_BC_BCF = 0x00,  // Branch on FPU Condition False (BC1F)
        COP1_BC_BCT = 0x01,  // Branch on FPU Condition True (BC1T)
        COP1_BC_BCFL = 0x02, // Branch on FPU Condition False Likely (BC1FL)
        COP1_BC_BCTL = 0x03, // Branch on FPU Condition True Likely (BC1TL)
    };

    // COP2 (VU0 Macro) Format Field ('fmt' or 'rs' field, bits 25-21) for OPCODE_COP2
    enum Cop2Format
    {
        // Note: Standard MIPS MFC2/MTC2 use fmt 0x00/0x04, EE uses QMFC2/QMTC2
        COP2_QMFC2 = 0x01, // QMFC2 (fmt=00001) - Load Quadword From Coprocessor 2
        COP2_CFC2 = 0x02,  // CFC2 (fmt=00010) - Load Control Word From Coprocessor 2
        COP2_QMTC2 = 0x05, // QMTC2 (fmt=00101) - Store Quadword To Coprocessor 2
        COP2_CTC2 = 0x06,  // CTC2 (fmt=00110) - Store Control Word To Coprocessor 2
        COP2_BC = 0x08,    // BC2 group (fmt=01000) (see Cop2BranchCondition)
        COP2_CO = 0x10,    // VU0 Macro Operation group (fmt=1xxxx)
    };

    // COP2 BC Condition Field ('fmt' or 'rt' field, bits 20-16) for COP2_BC Format
    enum Cop2BranchCondition
    {
        COP2_BC_BCF = 0x00,  // Branch on VU0 Condition False (BC2F)
        COP2_BC_BCT = 0x01,  // Branch on VU0 Condition True (BC2T)
        COP2_BC_BCFL = 0x02, // Branch on VU0 Condition False Likely (BC2FL)
        COP2_BC_BCTL = 0x03, // Branch on VU0 Condition True Likely (BC2TL)
    };

    // VU0 Macro Function Field (bits 5-0) for COP2_CO Format (Special1 Table)
    enum VU0MacroSpecial1Functions : uint8_t
    {
        VU0_S1_VADDx = 0x00,
        VU0_S1_VADDy = 0x01,
        VU0_S1_VADDz = 0x02,
        VU0_S1_VADDw = 0x03,
        VU0_S1_VSUBx = 0x04,
        VU0_S1_VSUBy = 0x05,
        VU0_S1_VSUBz = 0x06,
        VU0_S1_VSUBw = 0x07,
        VU0_S1_VMADDx = 0x08,
        VU0_S1_VMADDy = 0x09,
        VU0_S1_VMADDz = 0x0A,
        VU0_S1_VMADDw = 0x0B,
        VU0_S1_VMSUBx = 0x0C,
        VU0_S1_VMSUBy = 0x0D,
        VU0_S1_VMSUBz = 0x0E,
        VU0_S1_VMSUBw = 0x0F,
        VU0_S1_VMAXx = 0x10,
        VU0_S1_VMAXy = 0x11,
        VU0_S1_VMAXz = 0x12,
        VU0_S1_VMAXw = 0x13,
        VU0_S1_VMINIx = 0x14,
        VU0_S1_VMINIy = 0x15,
        VU0_S1_VMINIz = 0x16,
        VU0_S1_VMINIw = 0x17,
        VU0_S1_VMULx = 0x18,
        VU0_S1_VMULy = 0x19,
        VU0_S1_VMULz = 0x1A,
        VU0_S1_VMULw = 0x1B,
        VU0_S1_VMULq = 0x1C,
        VU0_S1_VMAXi = 0x1D,
        VU0_S1_VMULi = 0x1E,
        VU0_S1_VMINIi = 0x1F,
        VU0_S1_VADDq = 0x20,
        VU0_S1_VMADDq = 0x21,
        VU0_S1_VADDi = 0x22,
        VU0_S1_VMADDi = 0x23,
        VU0_S1_VSUBq = 0x24,
        VU0_S1_VMSUBq = 0x25,
        VU0_S1_VSUBi = 0x26,
        VU0_S1_VMSUBi = 0x27,
        VU0_S1_VADD = 0x28,
        VU0_S1_VMADD = 0x29,
        VU0_S1_VMUL = 0x2A,
        VU0_S1_VMAX = 0x2B,
        VU0_S1_VSUB = 0x2C,
        VU0_S1_VMSUB = 0x2D,
        VU0_S1_VOPMSUB = 0x2E,
        VU0_S1_VMINI = 0x2F,
        VU0_S1_VIADD = 0x30,
        VU0_S1_VISUB = 0x31,
        VU0_S1_VIADDI = 0x32,
        VU0_S1_VIAND = 0x34,
        VU0_S1_VIOR = 0x35,
        VU0_S1_VCALLMS = 0x38,
        VU0_S1_VCALLMSR = 0x39,
    };

    // VU0 Macro Function Field (bits 5-0) for COP2_CO Format (Special2 Table)
    enum VU0MacroSpecial2Functions : uint8_t
    {
        VU0_S2_VADDAx = 0x00,
        VU0_S2_VADDAy = 0x01,
        VU0_S2_VADDAz = 0x02,
        VU0_S2_VADDAw = 0x03,
        VU0_S2_VSUBAx = 0x04,
        VU0_S2_VSUBAy = 0x05,
        VU0_S2_VSUBAz = 0x06,
        VU0_S2_VSUBAw = 0x07,
        VU0_S2_VMADDAx = 0x08,
        VU0_S2_VMADDAy = 0x09,
        VU0_S2_VMADDAz = 0x0A,
        VU0_S2_VMADDAw = 0x0B,
        VU0_S2_VMSUBAx = 0x0C,
        VU0_S2_VMSUBAy = 0x0D,
        VU0_S2_VMSUBAz = 0x0E,
        VU0_S2_VMSUBAw = 0x0F,
        VU0_S2_VITOF0 = 0x10,
        VU0_S2_VITOF4 = 0x11,
        VU0_S2_VITOF12 = 0x12,
        VU0_S2_VITOF15 = 0x13,
        VU0_S2_VFTOI0 = 0x14,
        VU0_S2_VFTOI4 = 0x15,
        VU0_S2_VFTOI12 = 0x16,
        VU0_S2_VFTOI15 = 0x17,
        VU0_S2_VMULAx = 0x18,
        VU0_S2_VMULAy = 0x19,
        VU0_S2_VMULAz = 0x1A,
        VU0_S2_VMULAw = 0x1B,
        VU0_S2_VMULAq = 0x1C,
        VU0_S2_VABS = 0x1D,
        VU0_S2_VMULAi = 0x1E,
        VU0_S2_VCLIPw = 0x1F,
        VU0_S2_VADDAq = 0x20,
        VU0_S2_VMADDAq = 0x21,
        VU0_S2_VADDAi = 0x22,
        VU0_S2_VMADDAi = 0x23,
        VU0_S2_VSUBAq = 0x24,
        VU0_S2_VMSUBAq = 0x25,
        VU0_S2_VSUBAi = 0x26,
        VU0_S2_VMSUBAi = 0x27,
        VU0_S2_VADDA = 0x28,
        VU0_S2_VMADDA = 0x29,
        VU0_S2_VMULA = 0x2A,
        VU0_S2_VSUBA = 0x2C,
        VU0_S2_VMSUBA = 0x2D,
        VU0_S2_VOPMULA = 0x2E,
        VU0_S2_VNOP = 0x2F,
        VU0_S2_VMOVE = 0x30,
        VU0_S2_VMR32 = 0x31,
        VU0_S2_VLQI = 0x34,
        VU0_S2_VSQI = 0x35,
        VU0_S2_VLQD = 0x36,
        VU0_S2_VSQD = 0x37,
        VU0_S2_VDIV = 0x38,
        VU0_S2_VSQRT = 0x39,
        VU0_S2_VRSQRT = 0x3A,
        VU0_S2_VWAITQ = 0x3B,
        VU0_S2_VMTIR = 0x3C,
        VU0_S2_VMFIR = 0x3D,
        VU0_S2_VILWR = 0x3E,
        VU0_S2_VISWR = 0x3F,
        VU0_S2_VRNEXT = 0x40,
        VU0_S2_VRGET = 0x41,
        VU0_S2_VRINIT = 0x42,
        VU0_S2_VRXOR = 0x43
    };

    // // VU0 macro instruction function codes (subset - there are many more)
    // enum VU0MacroFunctions
    // {
    //     VU0_VADD = 0x00,
    //     VU0_VSUB = 0x01,
    //     VU0_VMUL = 0x02,
    //     VU0_VDIV = 0x03,
    //     VU0_VSQRT = 0x04,
    //     VU0_VRSQRT = 0x05,
    //     VU0_VMULQ = 0x06,
    //     VU0_VIADD = 0x10,
    //     VU0_VISUB = 0x11,
    //     VU0_VIADDI = 0x12,
    //     VU0_VIAND = 0x13,
    //     VU0_VIOR = 0x14,
    //     VU0_VILWR = 0x15,
    //     VU0_VISWR = 0x16,
    //     VU0_VCALLMS = 0x20,
    //     VU0_VCALLMSR = 0x21,
    //     VU0_VRGET = 0x3F,   // Get VU0 R register
    //     VU0_VSUB_XYZ = 0x38 // Subtract with component selection
    // };

    // enum VU0SpecialFormats
    // {
    //     VU0_BC2F = 0x11,   // Branch on VU0 condition false
    //     VU0_MTVUCF = 0x13, // Move To VU0 control/flag register
    //     VU0_CTCVU = 0x18,  // Control To VU0 with specific functionality
    //     VU0_VMTIR = 0x1C,  // Move To VU0 I Register
    //     VU0_VCLIP = 0x1E,  // VU0 Clipping operation
    //     VU0_VLDQ = 0x1F    // VU0 Load/Store Quad with Decrement
    // };

    // VU0 Control Register Numbers (used with CFC2/CTC2)
    enum VU0ControlRegisters
    {
        VU0_CR_STATUS = 0,  // Status/Control register
        VU0_CR_MAC = 1,     // MAC flags register
        VU0_CR_CLIP = 5,    // Clipping flags register //TODO maybe this is a 2 instead of 5
        VU0_CR_R = 3,       // R register (Random number)
        VU0_CR_I = 4,       // I register (Immediate)
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
        VU0OPS_VWAITQ = 0x3B,   // Wait for Q register operations to complete
        VU0OPS_VMFHL = 0x1C,    // Move from High / Low(similar to PMFHL)
        VU0OPS_VMTIR = 0x3C     // Move to VU0 I Register with function code
    };

    enum VU0OPSFunctionsExtra
    {
        VU0_VCALLMS_DIRECT = 0x00,
        VU0_VCALLMS_REG = 0x01
    };

    enum VU0SpecialMasks
    {
        VU0_FIELD_MASK = 0xF,  // Mask for vector field selection
        VU0_FIELD_SHIFT = 21,  // Shift amount for field selection
        VU0_STORE_BIT = 0x80,  // Bit indicating store operation
        VU0_SUBOP_MASK = 0x1F, // Mask for sub-operation code
        VU0_SUBOP_SHIFT = 6,   // Shift amount for sub-operation
        VU0_SQC0 = 0x8         // SQC0 instruction code
    };

    enum VU0FormatsExtra
    {
        VU0_FMT_MACRO_MOVE = 0x1, // Move between VU registers format
        VU0_FMT_VIF_STATUS = 0x5, // VIF status operations
        VU0_FMT_VCALLMS = 0x14,   // VU0 microprogram call format
        VU0_FMT_LQSQ_COP0 = 0x1B  // Load/store quad VU0 format
    };

// Instruction decoding helper macros
#define OPCODE(inst) ((inst >> 26) & 0x3F)
#define RS(inst) ((inst >> 21) & 0x1F)
#define RT(inst) ((inst >> 16) & 0x1F)
#define RD(inst) ((inst >> 11) & 0x1F)
#define SA(inst) ((inst >> 6) & 0x1F)
#define FUNCTION(inst) ((inst) & 0x3F)
#define IMMEDIATE(inst) ((inst) & 0xFFFF)
#define SIMMEDIATE(inst) ((int32_t)(int16_t)((inst) & 0xFFFF))
#define TARGET(inst) ((inst) & 0x3FFFFFF)
#define COP_FUNCT(inst) ((uint8_t)((inst) & 0x3F))                    // Function field for COPz CO instructions
#define FPU_FMT(inst) ((uint8_t)(((inst) >> 21) & 0x1F))              // Format field for FPU instructions
#define FT(inst) RT(inst)                                             // Target FPU register
#define FS(inst) RD(inst)                                             // Source FPU register
#define FD(inst) SA(inst)                                             // Destination FPU register
#define VU_I(inst) ((inst >> 25) & 0x1)                               // VU Upper I bit
#define VU_E(inst) ((inst >> 24) & 0x1)                               // VU Upper E bit
#define VU_M(inst) ((inst >> 23) & 0x1)                               // VU Upper M bit (VU0 only)
#define VU_D(inst) ((inst >> 22) & 0x1)                               // VU Upper D bit
#define VU_T(inst) ((inst >> 21) & 0x1)                               // VU Upper T bit
#define VU_DEST(inst) ((uint8_t)(((inst) >> 16) & 0xF))               // VU Destination mask
#define VU_FSF(inst) ((uint8_t)(((inst) >> 10) & 0x3))                // VU Source Field F
#define VU_FTF(inst) ((uint8_t)(((inst) >> 8) & 0x3))                 // VU Target Field F
#define VU_FD(inst) ((uint8_t)(((inst) >> 11) & 0x1F))                // VU Destination Vector Register
#define VU_FS(inst) ((uint8_t)(((inst) >> 6) & 0x1F))                 // VU Source Vector Register
#define VU_FT(inst) ((uint8_t)((inst) & 0x1F))                        // VU Target Vector Register
#define VU_IS(inst) RT(inst)                                          // VU Source Integer Register
#define VU_IT(inst) RD(inst)                                          // VU Target Integer Register
#define VU_ID(inst) SA(inst)                                          // VU Destination Integer Register
#define VU_IMM5(inst) ((uint8_t)((inst >> 6) & 0x1F))                 // VU 5-bit immediate
#define VU_IMM11(inst) ((uint16_t)((inst) & 0x7FF))                   // VU 11-bit immediate
#define VU_SIMM11(inst) ((int16_t)(((inst & 0x7FF) ^ 0x400) - 0x400)) // VU 11-bit signed immediate
#define VU_IMM15(inst) ((uint16_t)((inst) & 0x7FFF))                  // VU 15-bit immediate

} // namespace ps2recomp

#endif // PS2RECOMP_INSTRUCTIONS_H