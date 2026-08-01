#ifndef PS2_VU1_DETAIL_H
#define PS2_VU1_DETAIL_H

#include <cstdint>

// Instruction field extraction helpers
static inline uint8_t DEST(uint32_t i) { return (uint8_t)((i >> 21) & 0xF); }
static inline uint8_t FT(uint32_t i) { return (uint8_t)((i >> 16) & 0x1F); }
static inline uint8_t FS(uint32_t i) { return (uint8_t)((i >> 11) & 0x1F); }
static inline uint8_t FD(uint32_t i) { return (uint8_t)((i >> 6) & 0x1F); }
static inline uint8_t BC(uint32_t i) { return (uint8_t)(i & 0x3); }

// Lower instruction field helpers
static inline uint8_t LIT(uint32_t i) { return (uint8_t)((i >> 16) & 0x1F); }
static inline uint8_t LIS(uint32_t i) { return (uint8_t)((i >> 11) & 0x1F); }
static inline uint8_t LID(uint32_t i) { return (uint8_t)((i >> 6) & 0x1F); }
static inline uint8_t VIT(uint32_t i) { return (uint8_t)((i >> 16) & 0xF); }
static inline uint8_t VIS(uint32_t i) { return (uint8_t)((i >> 11) & 0xF); }
static inline uint8_t VID(uint32_t i) { return (uint8_t)((i >> 6) & 0xF); }
static inline int16_t IMM11(uint32_t i) { return (int16_t)(int32_t)((int32_t)(i << 21) >> 21); }
static inline int16_t IMM15(uint32_t i)
{
    uint32_t lo11 = i & 0x7FF;
    uint32_t hi4 = (i >> 21) & 0xF;
    uint32_t raw = (hi4 << 11) | lo11;
    return (int16_t)(int32_t)((int32_t)(raw << 17) >> 17);
}


static inline uint8_t vuUpperVfWriteReg(uint32_t upper)
{
    const uint8_t op = upper & 0x3Fu;
    const uint8_t dest = DEST(upper);
    const uint8_t ft = FT(upper);
    const uint8_t fd = FD(upper);

    if (dest == 0u)
        return 0u;

    if (op <= 0x2Fu)
        return fd;

    if (op >= 0x3Cu)
    {
        const uint8_t specialOp = static_cast<uint8_t>((upper & 0x3u) | ((upper >> 4) & 0x7Cu));
        switch (specialOp)
        {
        // Upper special ops that write a VF register use FT as destination.
        case 0x10: // ITOF0
        case 0x11: // ITOF4
        case 0x12: // ITOF12
        case 0x13: // ITOF15
        case 0x14: // FTOI0
        case 0x15: // FTOI4
        case 0x16: // FTOI12
        case 0x17: // FTOI15
        case 0x1D: // ABS
            return ft;
        default:
            return 0u; // ACC/NOP/CLIP/etc.
        }
    }

    return 0u;
}

static inline void vuSetRegBit(uint32_t &mask, uint8_t reg)
{
    if (reg != 0u && reg < 32u)
        mask |= (1u << reg);
}

static inline void vuLowerVfReadWriteMasks(uint32_t lower, uint32_t &readMask, uint32_t &writeMask)
{
    readMask = 0u;
    writeMask = 0u;

    if (lower == 0u || lower == 0x8000033Cu)
        return;

    const uint8_t opHi = static_cast<uint8_t>((lower >> 25) & 0x7Fu);
    const uint8_t it = LIT(lower);
    const uint8_t is = LIS(lower);

    if ((lower & 0x80000000u) != 0u)
    {
        const uint8_t funct = lower & 0x3Fu;
        if (funct >= 0x3Cu && funct <= 0x3Fu)
        {
            const uint8_t specialOp = static_cast<uint8_t>((lower & 0x3u) | ((lower >> 4) & 0x7Cu));
            switch (specialOp)
            {
            case 0x30: // MOVE
            case 0x31: // MR32
                vuSetRegBit(readMask, is);
                vuSetRegBit(writeMask, it);
                return;
            case 0x34: // LQI
            case 0x36: // LQD
                vuSetRegBit(writeMask, it);
                return;
            case 0x35: // SQI
            case 0x37: // SQD
                vuSetRegBit(readMask, is);
                return;
            case 0x38: // DIV
            case 0x3A: // RSQRT
                vuSetRegBit(readMask, is);
                vuSetRegBit(readMask, it);
                return;
            case 0x39: // SQRT
                vuSetRegBit(readMask, it);
                return;
            case 0x3C: // MTIR
            case 0x3E: // ILWR source base is integer, but field source is VF for MTIR only.
                if (specialOp == 0x3C)
                    vuSetRegBit(readMask, is);
                return;
            case 0x3D: // MFIR
            case 0x64: // MFP
                vuSetRegBit(writeMask, it);
                return;
            // R-register block.  RNEXT and RGET store the random number into
            // VF[ft]; RINIT and RXOR take VF[fs]fsf as their only vector
            // operand.  RGET's instruction page states the rule this table
            // exists to serve: "When an Upper instruction in the same cycle
            // writes data to the VF[ft] register, the result of this
            // instruction is discarded with priority given to the Upper
            // instruction, regardless of whether the data is written to the
            // same field or not."
            case 0x40: // RNEXT
            case 0x41: // RGET
                vuSetRegBit(writeMask, it);
                return;
            case 0x42: // RINIT
            case 0x43: // RXOR
                vuSetRegBit(readMask, is);
                return;
            // EFU block.  Every EFU operation takes VF[is] as its only vector
            // operand and writes the P register; see the VU User's Manual EFU
            // instruction pages ("ESADD P, VF[fs]" through "EEXP P, VF[fs]fsf").
            // The read is reported whether or not the execution switch gives the
            // opcode a body today, so implementing one later cannot silently
            // reopen the hazard.  WAITP (0x7B) has no vector operand and is
            // deliberately absent, as are the unallocated slots 0x77 and 0x7F.
            case 0x70: // ESADD
            case 0x71: // ERSADD
            case 0x72: // ELENG
            case 0x73: // ERLENG
            case 0x74: // EATANxy
            case 0x75: // EATANxz
            case 0x76: // ESUM
            case 0x78: // ESQRT
            case 0x79: // ERSQRT
            case 0x7A: // ERCPR
            case 0x7C: // ESIN
            case 0x7D: // EATAN
            case 0x7E: // EEXP
                vuSetRegBit(readMask, is);
                return;
            default:
                return;
            }
        }
        return;
    }

    switch (opHi)
    {
    case 0x00: // LQ
        vuSetRegBit(writeMask, it);
        return;
    case 0x01: // SQ
        vuSetRegBit(readMask, is);
        return;
    default:
        return;
    }
}

static inline uint32_t vuLowerVfWriteMask(uint32_t lower)
{
    uint32_t reads = 0u;
    uint32_t writes = 0u;
    vuLowerVfReadWriteMasks(lower, reads, writes);
    return writes;
}

// Collects the VF registers a reordered pair has to protect: the upper half's
// source registers, whose pre-pair values it must still observe, and the
// upper half's destination register, whose lower-half write hardware discards.
// A register is collected at most once, and that is required rather than tidy:
// the caller captures and rolls back each entry in turn, so a second entry for
// an already-rolled-back register would capture the pre-pair value and replay
// it over the lower half's write.
static inline void vuAddGuardedReg(uint8_t *regs, uint8_t &count, uint8_t reg, uint32_t lowerWrites)
{
    if (reg == 0u || ((lowerWrites >> reg) & 1u) == 0u)
        return;
    for (uint8_t i = 0u; i < count; ++i)
    {
        if (regs[i] == reg)
            return;
    }
    regs[count++] = reg;
}

static inline bool vuLowerShouldRunBeforeUpper(uint32_t upper, uint32_t lower)
{
    const uint8_t upperWrite = vuUpperVfWriteReg(upper);
    if (upperWrite == 0u)
        return false;

    uint32_t lowerReads = 0u;
    uint32_t lowerWrites = 0u;
    vuLowerVfReadWriteMasks(lower, lowerReads, lowerWrites);

    const uint32_t upperBit = (1u << upperWrite);
    return ((lowerReads | lowerWrites) & upperBit) != 0u;
}

#endif