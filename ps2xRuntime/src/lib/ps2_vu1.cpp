#include "ps2_vu1.h"
#include "ps2_gs_gpu.h"
#include "ps2_gif_arbiter.h"
#include "ps2_memory.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

// Instruction field extraction helpers
static inline uint8_t DEST(uint32_t i) { return (uint8_t)((i >> 21) & 0xF); }
static inline uint8_t FT(uint32_t i)   { return (uint8_t)((i >> 16) & 0x1F); }
static inline uint8_t FS(uint32_t i)   { return (uint8_t)((i >> 11) & 0x1F); }
static inline uint8_t FD(uint32_t i)   { return (uint8_t)((i >> 6) & 0x1F); }
static inline uint8_t BC(uint32_t i)   { return (uint8_t)(i & 0x3); }

// Lower instruction field helpers
static inline uint8_t LIT(uint32_t i)  { return (uint8_t)((i >> 16) & 0x1F); }
static inline uint8_t LIS(uint32_t i)  { return (uint8_t)((i >> 11) & 0x1F); }
static inline uint8_t LID(uint32_t i)  { return (uint8_t)((i >> 6) & 0x1F); }
static inline int16_t IMM11(uint32_t i){ return (int16_t)(int32_t)((int32_t)(i << 21) >> 21); }
static inline int16_t IMM15(uint32_t i){
    uint32_t lo11 = i & 0x7FF;
    uint32_t hi4 = (i >> 21) & 0xF;
    uint32_t raw = (hi4 << 11) | lo11;
    return (int16_t)(int32_t)((int32_t)(raw << 17) >> 17);
}

VU1Interpreter::VU1Interpreter()
{
    reset();
}

void VU1Interpreter::reset()
{
    std::memset(&m_state, 0, sizeof(m_state));
    m_state.vf[0][3] = 1.0f; // VF0.w = 1.0
    m_state.q = 1.0f;
}

float VU1Interpreter::broadcast(const float *vf, uint8_t bc)
{
    return vf[bc & 3];
}

void VU1Interpreter::applyDest(float *dst, const float *result, uint8_t dest)
{
    if (dest & 0x8) dst[0] = result[0]; // x
    if (dest & 0x4) dst[1] = result[1]; // y
    if (dest & 0x2) dst[2] = result[2]; // z
    if (dest & 0x1) dst[3] = result[3]; // w
}

void VU1Interpreter::applyDestAcc(const float *result, uint8_t dest)
{
    applyDest(m_state.acc, result, dest);
}

void VU1Interpreter::execute(uint8_t *vuCode, uint32_t codeSize,
                              uint8_t *vuData, uint32_t dataSize,
                              GS &gs, PS2Memory *memory,
                              uint32_t startPC, uint32_t itop,
                              uint32_t maxCycles)
{
    m_state.pc = startPC;
    m_state.ebit = false;
    m_state.itop = itop;
    m_state.vf[0][0] = 0.0f;
    m_state.vf[0][1] = 0.0f;
    m_state.vf[0][2] = 0.0f;
    m_state.vf[0][3] = 1.0f;
    run(vuCode, codeSize, vuData, dataSize, gs, memory, maxCycles);
}

void VU1Interpreter::resume(uint8_t *vuCode, uint32_t codeSize,
                             uint8_t *vuData, uint32_t dataSize,
                             GS &gs, PS2Memory *memory,
                             uint32_t itop, uint32_t maxCycles)
{
    m_state.ebit = false;
    m_state.itop = itop;
    run(vuCode, codeSize, vuData, dataSize, gs, memory, maxCycles);
}

void VU1Interpreter::run(uint8_t *vuCode, uint32_t codeSize,
                          uint8_t *vuData, uint32_t dataSize,
                          GS &gs, PS2Memory *memory, uint32_t maxCycles)
{
    for (uint32_t cycle = 0; cycle < maxCycles; ++cycle)
    {
        if (m_state.pc + 8 > codeSize)
            break;

        uint32_t lower, upper;
        std::memcpy(&lower, vuCode + m_state.pc, 4);
        std::memcpy(&upper, vuCode + m_state.pc + 4, 4);

        bool eBit = (upper >> 30) & 1;
        bool mBit = (upper >> 31) & 1;
        (void)mBit;

        // LOI: if bit 31 of lower is set, the upper word is an immediate float loaded into I
        bool loi = (lower >> 31) & 1;
        if (loi)
        {
            std::memcpy(&m_state.i, &upper, 4);
        }
        else
        {
            execUpper(upper);
        }
        execLower(lower & 0x7FFFFFFF, vuData, dataSize, gs, memory, upper);

        // Enforce VF0 invariant
        m_state.vf[0][0] = 0.0f;
        m_state.vf[0][1] = 0.0f;
        m_state.vf[0][2] = 0.0f;
        m_state.vf[0][3] = 1.0f;
        // Enforce VI0 invariant
        m_state.vi[0] = 0;

        uint32_t nextPC = m_state.pc + 8;
        if (nextPC >= codeSize) nextPC = 0;
        m_state.pc = nextPC;

        if (m_state.ebit)
            break;

        if (eBit)
            m_state.ebit = true;
    }
}

// ============================================================================
// Upper instructions (FMAC pipeline)
// ============================================================================
void VU1Interpreter::execUpper(uint32_t instr)
{
    uint8_t dest = DEST(instr);
    uint8_t ft = FT(instr);
    uint8_t fs = FS(instr);
    uint8_t fd = FD(instr);
    uint8_t op = instr & 0x3F;

    float *vd = m_state.vf[fd];
    const float *vs = m_state.vf[fs];
    const float *vt = m_state.vf[ft];
    float result[4];

    // Upper opcode decoding (bits 5:0 of upper word)
    switch (op)
    {
    case 0x00: case 0x01: case 0x02: case 0x03: // ADDbc
    {
        float bc = broadcast(vt, op & 3);
        for (int c = 0; c < 4; c++) result[c] = vs[c] + bc;
        applyDest(vd, result, dest);
        return;
    }
    case 0x04: case 0x05: case 0x06: case 0x07: // SUBbc
    {
        float bc = broadcast(vt, op & 3);
        for (int c = 0; c < 4; c++) result[c] = vs[c] - bc;
        applyDest(vd, result, dest);
        return;
    }
    case 0x08: case 0x09: case 0x0A: case 0x0B: // MADDbc
    {
        float bc = broadcast(vt, op & 3);
        for (int c = 0; c < 4; c++) result[c] = m_state.acc[c] + vs[c] * bc;
        applyDest(vd, result, dest);
        return;
    }
    case 0x0C: case 0x0D: case 0x0E: case 0x0F: // MSUBbc
    {
        float bc = broadcast(vt, op & 3);
        for (int c = 0; c < 4; c++) result[c] = m_state.acc[c] - vs[c] * bc;
        applyDest(vd, result, dest);
        return;
    }
    case 0x10: case 0x11: case 0x12: case 0x13: // MAXbc
    {
        float bc = broadcast(vt, op & 3);
        for (int c = 0; c < 4; c++) result[c] = (vs[c] > bc) ? vs[c] : bc;
        applyDest(vd, result, dest);
        return;
    }
    case 0x14: case 0x15: case 0x16: case 0x17: // MINIbc
    {
        float bc = broadcast(vt, op & 3);
        for (int c = 0; c < 4; c++) result[c] = (vs[c] < bc) ? vs[c] : bc;
        applyDest(vd, result, dest);
        return;
    }
    case 0x18: case 0x19: case 0x1A: case 0x1B: // MULbc
    {
        float bc = broadcast(vt, op & 3);
        for (int c = 0; c < 4; c++) result[c] = vs[c] * bc;
        applyDest(vd, result, dest);
        return;
    }
    case 0x1C: // MULq
        for (int c = 0; c < 4; c++) result[c] = vs[c] * m_state.q;
        applyDest(vd, result, dest);
        return;
    case 0x1D: // MAXi
        for (int c = 0; c < 4; c++) result[c] = (vs[c] > m_state.i) ? vs[c] : m_state.i;
        applyDest(vd, result, dest);
        return;
    case 0x1E: // MULi
        for (int c = 0; c < 4; c++) result[c] = vs[c] * m_state.i;
        applyDest(vd, result, dest);
        return;
    case 0x1F: // MINIi
        for (int c = 0; c < 4; c++) result[c] = (vs[c] < m_state.i) ? vs[c] : m_state.i;
        applyDest(vd, result, dest);
        return;
    case 0x20: // ADDq
        for (int c = 0; c < 4; c++) result[c] = vs[c] + m_state.q;
        applyDest(vd, result, dest);
        return;
    case 0x21: // MADDq
        for (int c = 0; c < 4; c++) result[c] = m_state.acc[c] + vs[c] * m_state.q;
        applyDest(vd, result, dest);
        return;
    case 0x22: // ADDi
        for (int c = 0; c < 4; c++) result[c] = vs[c] + m_state.i;
        applyDest(vd, result, dest);
        return;
    case 0x23: // MADDi
        for (int c = 0; c < 4; c++) result[c] = m_state.acc[c] + vs[c] * m_state.i;
        applyDest(vd, result, dest);
        return;
    case 0x24: // SUBq
        for (int c = 0; c < 4; c++) result[c] = vs[c] - m_state.q;
        applyDest(vd, result, dest);
        return;
    case 0x25: // MSUBq
        for (int c = 0; c < 4; c++) result[c] = m_state.acc[c] - vs[c] * m_state.q;
        applyDest(vd, result, dest);
        return;
    case 0x26: // SUBi
        for (int c = 0; c < 4; c++) result[c] = vs[c] - m_state.i;
        applyDest(vd, result, dest);
        return;
    case 0x27: // MSUBi
        for (int c = 0; c < 4; c++) result[c] = m_state.acc[c] - vs[c] * m_state.i;
        applyDest(vd, result, dest);
        return;
    case 0x28: // ADD
        for (int c = 0; c < 4; c++) result[c] = vs[c] + vt[c];
        applyDest(vd, result, dest);
        return;
    case 0x29: // MADD
        for (int c = 0; c < 4; c++) result[c] = m_state.acc[c] + vs[c] * vt[c];
        applyDest(vd, result, dest);
        return;
    case 0x2A: // MUL
        for (int c = 0; c < 4; c++) result[c] = vs[c] * vt[c];
        applyDest(vd, result, dest);
        return;
    case 0x2B: // MAX
        for (int c = 0; c < 4; c++) result[c] = (vs[c] > vt[c]) ? vs[c] : vt[c];
        applyDest(vd, result, dest);
        return;
    case 0x2C: // SUB
        for (int c = 0; c < 4; c++) result[c] = vs[c] - vt[c];
        applyDest(vd, result, dest);
        return;
    case 0x2D: // MSUB
        for (int c = 0; c < 4; c++) result[c] = m_state.acc[c] - vs[c] * vt[c];
        applyDest(vd, result, dest);
        return;
    case 0x2E: // OPMSUB
        result[0] = m_state.acc[0] - vs[1] * vt[2];
        result[1] = m_state.acc[1] - vs[2] * vt[0];
        result[2] = m_state.acc[2] - vs[0] * vt[1];
        result[3] = 0.0f;
        applyDest(vd, result, dest);
        return;
    case 0x2F: // MINI
        for (int c = 0; c < 4; c++) result[c] = (vs[c] < vt[c]) ? vs[c] : vt[c];
        applyDest(vd, result, dest);
        return;

    // Special1 group (0x3C..0x3F with secondary field)
    case 0x3C: case 0x3D: case 0x3E: case 0x3F:
    {
        uint8_t special = (instr >> 6) & 0x1F;
        uint8_t sop = (instr & 0x3) | ((instr >> 4) & 0x3C);
        (void)sop;

        switch (instr & 0x3F)
        {
        case 0x3C: // Special1 (ADDAx..ADDAw, SUBAx..SUBAw, MADDAx..MADDAw, MSUBAx..MSUBAw, etc.)
        {
            uint8_t funct = (instr >> 6) & 0x1F;
            uint8_t bc2 = (instr >> 0) & 0x3;
            (void)bc2;
            switch (funct)
            {
            case 0x00: case 0x01: case 0x02: case 0x03: // ADDAbc
            {
                float bc = broadcast(vt, funct & 3);
                for (int c = 0; c < 4; c++) result[c] = vs[c] + bc;
                applyDestAcc(result, dest);
                return;
            }
            case 0x04: case 0x05: case 0x06: case 0x07: // SUBAbc
            {
                float bc = broadcast(vt, funct & 3);
                for (int c = 0; c < 4; c++) result[c] = vs[c] - bc;
                applyDestAcc(result, dest);
                return;
            }
            case 0x08: case 0x09: case 0x0A: case 0x0B: // MADDAbc
            {
                float bc = broadcast(vt, funct & 3);
                for (int c = 0; c < 4; c++) result[c] = m_state.acc[c] + vs[c] * bc;
                applyDestAcc(result, dest);
                return;
            }
            case 0x0C: case 0x0D: case 0x0E: case 0x0F: // MSUBAbc
            {
                float bc = broadcast(vt, funct & 3);
                for (int c = 0; c < 4; c++) result[c] = m_state.acc[c] - vs[c] * bc;
                applyDestAcc(result, dest);
                return;
            }
            case 0x10: // ITOF0
                for (int c = 0; c < 4; c++) { int32_t iv; std::memcpy(&iv, &vs[c], 4); result[c] = (float)iv; }
                applyDest(vd, result, dest);
                return;
            case 0x11: // ITOF4
                for (int c = 0; c < 4; c++) { int32_t iv; std::memcpy(&iv, &vs[c], 4); result[c] = (float)iv / 16.0f; }
                applyDest(vd, result, dest);
                return;
            case 0x12: // ITOF12
                for (int c = 0; c < 4; c++) { int32_t iv; std::memcpy(&iv, &vs[c], 4); result[c] = (float)iv / 4096.0f; }
                applyDest(vd, result, dest);
                return;
            case 0x13: // ITOF15
                for (int c = 0; c < 4; c++) { int32_t iv; std::memcpy(&iv, &vs[c], 4); result[c] = (float)iv / 32768.0f; }
                applyDest(vd, result, dest);
                return;
            case 0x14: // FTOI0
                for (int c = 0; c < 4; c++) { int32_t iv = (int32_t)vs[c]; std::memcpy(&result[c], &iv, 4); }
                applyDest(vd, result, dest);
                return;
            case 0x15: // FTOI4
                for (int c = 0; c < 4; c++) { int32_t iv = (int32_t)(vs[c] * 16.0f); std::memcpy(&result[c], &iv, 4); }
                applyDest(vd, result, dest);
                return;
            case 0x16: // FTOI12
                for (int c = 0; c < 4; c++) { int32_t iv = (int32_t)(vs[c] * 4096.0f); std::memcpy(&result[c], &iv, 4); }
                applyDest(vd, result, dest);
                return;
            case 0x17: // FTOI15
                for (int c = 0; c < 4; c++) { int32_t iv = (int32_t)(vs[c] * 32768.0f); std::memcpy(&result[c], &iv, 4); }
                applyDest(vd, result, dest);
                return;
            case 0x18: case 0x19: case 0x1A: case 0x1B: // MULAbc
            {
                float bc = broadcast(vt, funct & 3);
                for (int c = 0; c < 4; c++) result[c] = vs[c] * bc;
                applyDestAcc(result, dest);
                return;
            }
            case 0x1C: // MULAq
                for (int c = 0; c < 4; c++) result[c] = vs[c] * m_state.q;
                applyDestAcc(result, dest);
                return;
            case 0x1D: // ABS
                for (int c = 0; c < 4; c++) result[c] = std::fabs(vs[c]);
                applyDest(vd, result, dest);
                return;
            case 0x1E: // MULAi
                for (int c = 0; c < 4; c++) result[c] = vs[c] * m_state.i;
                applyDestAcc(result, dest);
                return;
            case 0x1F: // CLIP
            {
                float w = std::fabs(vt[3]);
                uint32_t flags = 0;
                if (vs[0] > +w) flags |= 0x01;
                if (vs[0] < -w) flags |= 0x02;
                if (vs[1] > +w) flags |= 0x04;
                if (vs[1] < -w) flags |= 0x08;
                if (vs[2] > +w) flags |= 0x10;
                if (vs[2] < -w) flags |= 0x20;
                m_state.clip = (m_state.clip << 6) | flags;
                return;
            }
            default:
                return;
            }
        }
        case 0x3D: // Special2 (ADDAq, MADDAq, ADDAi, MADDAi, ADDA, MADDA, MULA, OPMULA, ...)
        {
            uint8_t funct = (instr >> 6) & 0x1F;
            switch (funct)
            {
            case 0x00: // ADDAq
                for (int c = 0; c < 4; c++) result[c] = vs[c] + m_state.q;
                applyDestAcc(result, dest);
                return;
            case 0x01: // MADDAq
                for (int c = 0; c < 4; c++) result[c] = m_state.acc[c] + vs[c] * m_state.q;
                applyDestAcc(result, dest);
                return;
            case 0x02: // ADDAi
                for (int c = 0; c < 4; c++) result[c] = vs[c] + m_state.i;
                applyDestAcc(result, dest);
                return;
            case 0x03: // MADDAi
                for (int c = 0; c < 4; c++) result[c] = m_state.acc[c] + vs[c] * m_state.i;
                applyDestAcc(result, dest);
                return;
            case 0x04: // SUBAq
                for (int c = 0; c < 4; c++) result[c] = vs[c] - m_state.q;
                applyDestAcc(result, dest);
                return;
            case 0x05: // MSUBAq
                for (int c = 0; c < 4; c++) result[c] = m_state.acc[c] - vs[c] * m_state.q;
                applyDestAcc(result, dest);
                return;
            case 0x06: // SUBAi
                for (int c = 0; c < 4; c++) result[c] = vs[c] - m_state.i;
                applyDestAcc(result, dest);
                return;
            case 0x07: // MSUBAi
                for (int c = 0; c < 4; c++) result[c] = m_state.acc[c] - vs[c] * m_state.i;
                applyDestAcc(result, dest);
                return;
            case 0x08: // ADDA
                for (int c = 0; c < 4; c++) result[c] = vs[c] + vt[c];
                applyDestAcc(result, dest);
                return;
            case 0x09: // MADDA
                for (int c = 0; c < 4; c++) result[c] = m_state.acc[c] + vs[c] * vt[c];
                applyDestAcc(result, dest);
                return;
            case 0x0A: // MULA
                for (int c = 0; c < 4; c++) result[c] = vs[c] * vt[c];
                applyDestAcc(result, dest);
                return;
            case 0x0C: // SUBA
                for (int c = 0; c < 4; c++) result[c] = vs[c] - vt[c];
                applyDestAcc(result, dest);
                return;
            case 0x0D: // MSUBA
                for (int c = 0; c < 4; c++) result[c] = m_state.acc[c] - vs[c] * vt[c];
                applyDestAcc(result, dest);
                return;
            case 0x0E: // OPMULA
                result[0] = vs[1] * vt[2];
                result[1] = vs[2] * vt[0];
                result[2] = vs[0] * vt[1];
                result[3] = 0.0f;
                applyDestAcc(result, dest);
                return;
            case 0x0F: // NOP
                return;
            default:
                return;
            }
        }
        case 0x3E: // Special (more upper ops, rarely used)
            return;
        case 0x3F: // Special (upper NOP typically)
            return;
        }
        return;
    }

    case 0x30: case 0x31: case 0x32: case 0x33: // iadd-like upper? No, these are valid upper ops
    default:
        // NOP / unimplemented upper
        return;
    }
}

// ============================================================================
// Lower instructions
// ============================================================================
void VU1Interpreter::execLower(uint32_t instr, uint8_t *vuData, uint32_t dataSize, GS &gs, PS2Memory *memory, uint32_t upperInstr)
{
    (void)upperInstr;
    if (instr == 0x00000000 || instr == 0x8000033C) // NOP
        return;

    uint8_t opHi = (instr >> 25) & 0x7F;

    // The lower instruction encoding uses bits 31:25 for the primary opcode
    switch (opHi)
    {
    case 0x00: // LQ (Load Quadword from VU data memory)
    {
        uint8_t it = LIT(instr);
        uint8_t is = LIS(instr);
        uint8_t dest = (instr >> 21) & 0xF;
        int16_t imm = IMM11(instr);
        uint32_t addr = ((uint32_t)(int32_t)(m_state.vi[is] + imm)) * 16u;
        addr &= (dataSize - 1);
        if (addr + 16 <= dataSize)
        {
            float tmp[4];
            std::memcpy(tmp, vuData + addr, 16);
            applyDest(m_state.vf[it], tmp, dest);
        }
        return;
    }
    case 0x01: // SQ (Store Quadword to VU data memory)
    {
        uint8_t is = LIS(instr);
        uint8_t it = LIT(instr);
        uint8_t dest = (instr >> 21) & 0xF;
        int16_t imm = IMM11(instr);
        uint32_t addr = ((uint32_t)(int32_t)(m_state.vi[it] + imm)) * 16u;
        addr &= (dataSize - 1);
        if (addr + 16 <= dataSize)
        {
            float tmp[4];
            std::memcpy(tmp, vuData + addr, 16);
            if (dest & 0x8) tmp[0] = m_state.vf[is][0];
            if (dest & 0x4) tmp[1] = m_state.vf[is][1];
            if (dest & 0x2) tmp[2] = m_state.vf[is][2];
            if (dest & 0x1) tmp[3] = m_state.vf[is][3];
            std::memcpy(vuData + addr, tmp, 16);
        }
        return;
    }
    case 0x04: // ILW (Integer Load Word from VU data memory)
    {
        uint8_t it = LIT(instr);
        uint8_t is = LIS(instr);
        uint8_t dest = (instr >> 21) & 0xF;
        int16_t imm = IMM11(instr);
        uint32_t addr = ((uint32_t)(int32_t)(m_state.vi[is] + imm)) * 16u;
        addr &= (dataSize - 1);
        if (addr + 16 <= dataSize)
        {
            int comp = 0;
            if (dest & 0x8) comp = 0;
            else if (dest & 0x4) comp = 1;
            else if (dest & 0x2) comp = 2;
            else comp = 3;
            uint32_t v;
            std::memcpy(&v, vuData + addr + comp * 4, 4);
            if (it != 0) m_state.vi[it] = (int32_t)(int16_t)(v & 0xFFFF);
        }
        return;
    }
    case 0x05: // ISW (Integer Store Word to VU data memory)
    {
        uint8_t it = LIT(instr);
        uint8_t is = LIS(instr);
        uint8_t dest = (instr >> 21) & 0xF;
        int16_t imm = IMM11(instr);
        uint32_t addr = ((uint32_t)(int32_t)(m_state.vi[is] + imm)) * 16u;
        addr &= (dataSize - 1);
        if (addr + 16 <= dataSize)
        {
            uint32_t val = (uint32_t)(uint16_t)(m_state.vi[it] & 0xFFFF);
            if (dest & 0x8) std::memcpy(vuData + addr + 0, &val, 4);
            if (dest & 0x4) std::memcpy(vuData + addr + 4, &val, 4);
            if (dest & 0x2) std::memcpy(vuData + addr + 8, &val, 4);
            if (dest & 0x1) std::memcpy(vuData + addr + 12, &val, 4);
        }
        return;
    }
    case 0x08: // IADDIU
    {
        uint8_t it = LIT(instr);
        uint8_t is = LIS(instr);
        int16_t imm = (int16_t)(instr & 0x7FF) | ((instr >> 10) & 0x7800);
        if (it != 0)
            m_state.vi[it] = (int16_t)(m_state.vi[is] + imm);
        return;
    }
    case 0x09: // ISUBIU
    {
        uint8_t it = LIT(instr);
        uint8_t is = LIS(instr);
        int16_t imm = (int16_t)(instr & 0x7FF) | ((instr >> 10) & 0x7800);
        if (it != 0)
            m_state.vi[it] = (int16_t)(m_state.vi[is] - imm);
        return;
    }
    case 0x10: // FCEQ
    {
        uint32_t imm24 = instr & 0xFFFFFF;
        if (1 != 0) m_state.vi[1] = ((m_state.clip & 0xFFFFFF) == imm24) ? 1 : 0;
        return;
    }
    case 0x11: // FCSET
    {
        m_state.clip = instr & 0xFFFFFF;
        return;
    }
    case 0x12: // FCAND
    {
        uint32_t imm24 = instr & 0xFFFFFF;
        if (1 != 0) m_state.vi[1] = ((m_state.clip & imm24) != 0) ? 1 : 0;
        return;
    }
    case 0x13: // FCOR
    {
        uint32_t imm24 = instr & 0xFFFFFF;
        if (1 != 0) m_state.vi[1] = ((m_state.clip | imm24) == 0xFFFFFF) ? 1 : 0;
        return;
    }
    case 0x14: // FSEQ
    {
        uint16_t imm12 = instr & 0xFFF;
        if (1 != 0) m_state.vi[1] = ((m_state.status & 0xFFF) == imm12) ? 1 : 0;
        return;
    }
    case 0x15: // FSSET
    {
        m_state.status = (instr >> 6) & 0xFC0;
        return;
    }
    case 0x16: // FSAND
    {
        uint16_t imm12 = instr & 0xFFF;
        if (1 != 0) m_state.vi[1] = (int32_t)(m_state.status & imm12);
        return;
    }
    case 0x17: // FSOR
    {
        uint16_t imm12 = instr & 0xFFF;
        if (1 != 0) m_state.vi[1] = ((m_state.status | imm12) == 0xFFF) ? 1 : 0;
        return;
    }
    case 0x18: // FMAND
    {
        uint8_t it = LIT(instr);
        uint8_t is = LIS(instr);
        if (it != 0) m_state.vi[it] = (int32_t)(m_state.mac & (uint32_t)(uint16_t)m_state.vi[is]);
        return;
    }
    case 0x1A: // FMEQ
    {
        uint8_t it = LIT(instr);
        uint8_t is = LIS(instr);
        if (it != 0) m_state.vi[it] = ((m_state.mac & 0xFFFF) == (uint32_t)(uint16_t)m_state.vi[is]) ? 1 : 0;
        return;
    }
    case 0x1C: // FMOR
    {
        uint8_t it = LIT(instr);
        uint8_t is = LIS(instr);
        if (it != 0) m_state.vi[it] = (int32_t)(m_state.mac | (uint32_t)(uint16_t)m_state.vi[is]);
        return;
    }
    case 0x20: // B (unconditional branch)
    {
        int16_t imm = IMM11(instr);
        uint32_t target = (m_state.pc + 8 + imm * 8) & 0x3FFF;
        // Simplified branch delay: set PC so next iteration lands on target
        m_state.pc = target - 8;
        return;
    }
    case 0x21: // BAL (Branch and link)
    {
        uint8_t it = LIT(instr);
        int16_t imm = IMM11(instr);
        uint32_t target = (m_state.pc + 8 + imm * 8) & 0x3FFF;
        if (it != 0) m_state.vi[it] = (int32_t)((m_state.pc + 16) / 8);
        m_state.pc = target - 8;
        return;
    }
    case 0x24: // JR
    {
        uint8_t is = LIS(instr);
        uint32_t target = ((uint32_t)(uint16_t)m_state.vi[is] * 8u) & 0x3FFF;
        m_state.pc = target - 8;
        return;
    }
    case 0x25: // JALR
    {
        uint8_t it = LIT(instr);
        uint8_t is = LIS(instr);
        uint32_t target = ((uint32_t)(uint16_t)m_state.vi[is] * 8u) & 0x3FFF;
        if (it != 0) m_state.vi[it] = (int32_t)((m_state.pc + 16) / 8);
        m_state.pc = target - 8;
        return;
    }
    case 0x28: // IBEQ
    {
        uint8_t it = LIT(instr);
        uint8_t is = LIS(instr);
        int16_t imm = IMM11(instr);
        if ((int16_t)m_state.vi[is] == (int16_t)m_state.vi[it])
        {
            uint32_t target = (m_state.pc + 8 + imm * 8) & 0x3FFF;
            m_state.pc = target - 8;
        }
        return;
    }
    case 0x29: // IBNE
    {
        uint8_t it = LIT(instr);
        uint8_t is = LIS(instr);
        int16_t imm = IMM11(instr);
        if ((int16_t)m_state.vi[is] != (int16_t)m_state.vi[it])
        {
            uint32_t target = (m_state.pc + 8 + imm * 8) & 0x3FFF;
            m_state.pc = target - 8;
        }
        return;
    }
    case 0x2C: // IBLTZ
    {
        uint8_t is = LIS(instr);
        int16_t imm = IMM11(instr);
        if ((int16_t)m_state.vi[is] < 0)
        {
            uint32_t target = (m_state.pc + 8 + imm * 8) & 0x3FFF;
            m_state.pc = target - 8;
        }
        return;
    }
    case 0x2D: // IBGTZ
    {
        uint8_t is = LIS(instr);
        int16_t imm = IMM11(instr);
        if ((int16_t)m_state.vi[is] > 0)
        {
            uint32_t target = (m_state.pc + 8 + imm * 8) & 0x3FFF;
            m_state.pc = target - 8;
        }
        return;
    }
    case 0x2E: // IBLEZ
    {
        uint8_t is = LIS(instr);
        int16_t imm = IMM11(instr);
        if ((int16_t)m_state.vi[is] <= 0)
        {
            uint32_t target = (m_state.pc + 8 + imm * 8) & 0x3FFF;
            m_state.pc = target - 8;
        }
        return;
    }
    case 0x2F: // IBGEZ
    {
        uint8_t is = LIS(instr);
        int16_t imm = IMM11(instr);
        if ((int16_t)m_state.vi[is] >= 0)
        {
            uint32_t target = (m_state.pc + 8 + imm * 8) & 0x3FFF;
            m_state.pc = target - 8;
        }
        return;
    }

    case 0x40: // Lower special (opcode in bits 5:0)
    {
        uint8_t funct = instr & 0x3F;
        uint8_t it = LIT(instr);
        uint8_t is = LIS(instr);
        uint8_t id = LID(instr);
        uint8_t dest = (instr >> 21) & 0xF;

        switch (funct)
        {
        case 0x30: // IADD
            if (id != 0)
                m_state.vi[id] = (int16_t)(m_state.vi[is] + m_state.vi[it]);
            return;
        case 0x31: // ISUB
            if (id != 0)
                m_state.vi[id] = (int16_t)(m_state.vi[is] - m_state.vi[it]);
            return;
        case 0x32: // IADDI
        {
            int16_t imm5 = (int16_t)((int32_t)((instr >> 6) & 0x1F) << 27 >> 27);
            if (it != 0)
                m_state.vi[it] = (int16_t)(m_state.vi[is] + imm5);
            return;
        }
        case 0x34: // IAND
            if (id != 0)
                m_state.vi[id] = m_state.vi[is] & m_state.vi[it];
            return;
        case 0x35: // IOR
            if (id != 0)
                m_state.vi[id] = m_state.vi[is] | m_state.vi[it];
            return;

        case 0x3C: // Lower special2
        {
            uint8_t funct2 = (instr >> 6) & 0x1F;
            switch (funct2)
            {
            case 0x00: // MOVE
            {
                float tmp[4];
                std::memcpy(tmp, m_state.vf[is], 16);
                applyDest(m_state.vf[it], tmp, dest);
                return;
            }
            case 0x01: // MR32 (rotate right by 32 bits = shift xyzw -> yzwx)
            {
                float tmp[4] = { m_state.vf[is][1], m_state.vf[is][2], m_state.vf[is][3], m_state.vf[is][0] };
                applyDest(m_state.vf[it], tmp, dest);
                return;
            }
            case 0x03: // MFIR (Move From Integer Register)
            {
                float result[4];
                int32_t val = (int32_t)(int16_t)(m_state.vi[is] & 0xFFFF);
                std::memcpy(&result[0], &val, 4);
                result[1] = result[0]; result[2] = result[0]; result[3] = result[0];
                applyDest(m_state.vf[it], result, dest);
                return;
            }
            case 0x04: // MTIR (Move To Integer Register)
            {
                int comp = 0;
                if (dest & 0x8) comp = 0;
                else if (dest & 0x4) comp = 1;
                else if (dest & 0x2) comp = 2;
                else comp = 3;
                uint32_t fval;
                std::memcpy(&fval, &m_state.vf[is][comp], 4);
                if (it != 0) m_state.vi[it] = (int32_t)(int16_t)(fval & 0xFFFF);
                return;
            }
            case 0x05: // RNEXT
                return;
            case 0x06: // RGET
                return;
            case 0x07: // RINIT
                return;
            case 0x10: // LQI (Load Quadword, post-increment)
            {
                uint32_t addr = ((uint32_t)(uint16_t)m_state.vi[is]) * 16u;
                addr &= (dataSize - 1);
                if (addr + 16 <= dataSize)
                {
                    float tmp[4];
                    std::memcpy(tmp, vuData + addr, 16);
                    applyDest(m_state.vf[it], tmp, dest);
                }
                if (is != 0) m_state.vi[is] = (int16_t)(m_state.vi[is] + 1);
                return;
            }
            case 0x11: // SQI (Store Quadword, post-increment)
            {
                uint32_t addr = ((uint32_t)(uint16_t)m_state.vi[it]) * 16u;
                addr &= (dataSize - 1);
                if (addr + 16 <= dataSize)
                {
                    float tmp[4];
                    std::memcpy(tmp, vuData + addr, 16);
                    if (dest & 0x8) tmp[0] = m_state.vf[is][0];
                    if (dest & 0x4) tmp[1] = m_state.vf[is][1];
                    if (dest & 0x2) tmp[2] = m_state.vf[is][2];
                    if (dest & 0x1) tmp[3] = m_state.vf[is][3];
                    std::memcpy(vuData + addr, tmp, 16);
                }
                if (it != 0) m_state.vi[it] = (int16_t)(m_state.vi[it] + 1);
                return;
            }
            case 0x12: // LQD (Load Quadword, pre-decrement)
            {
                if (is != 0) m_state.vi[is] = (int16_t)(m_state.vi[is] - 1);
                uint32_t addr = ((uint32_t)(uint16_t)m_state.vi[is]) * 16u;
                addr &= (dataSize - 1);
                if (addr + 16 <= dataSize)
                {
                    float tmp[4];
                    std::memcpy(tmp, vuData + addr, 16);
                    applyDest(m_state.vf[it], tmp, dest);
                }
                return;
            }
            case 0x13: // SQD (Store Quadword, pre-decrement)
            {
                if (it != 0) m_state.vi[it] = (int16_t)(m_state.vi[it] - 1);
                uint32_t addr = ((uint32_t)(uint16_t)m_state.vi[it]) * 16u;
                addr &= (dataSize - 1);
                if (addr + 16 <= dataSize)
                {
                    float tmp[4];
                    std::memcpy(tmp, vuData + addr, 16);
                    if (dest & 0x8) tmp[0] = m_state.vf[is][0];
                    if (dest & 0x4) tmp[1] = m_state.vf[is][1];
                    if (dest & 0x2) tmp[2] = m_state.vf[is][2];
                    if (dest & 0x1) tmp[3] = m_state.vf[is][3];
                    std::memcpy(vuData + addr, tmp, 16);
                }
                return;
            }
            case 0x14: // DIV
            {
                int fsf = (instr >> 21) & 0x3;
                int ftf = (instr >> 23) & 0x3;
                float num = m_state.vf[is][fsf];
                float den = m_state.vf[it][ftf];
                if (den != 0.0f)
                    m_state.q = num / den;
                else
                    m_state.q = (num >= 0.0f) ? std::numeric_limits<float>::max() : -std::numeric_limits<float>::max();
                return;
            }
            case 0x15: // SQRT
            {
                int ftf = (instr >> 23) & 0x3;
                float val = m_state.vf[it][ftf];
                m_state.q = std::sqrt(std::fabs(val));
                return;
            }
            case 0x16: // RSQRT
            {
                int fsf = (instr >> 21) & 0x3;
                int ftf = (instr >> 23) & 0x3;
                float num = m_state.vf[is][fsf];
                float den = std::sqrt(std::fabs(m_state.vf[it][ftf]));
                if (den != 0.0f)
                    m_state.q = num / den;
                else
                    m_state.q = std::numeric_limits<float>::max();
                return;
            }
            case 0x17: // WAITQ
                return;
            case 0x18: // ESADD
                return;
            case 0x19: // ERSADD
                return;
            case 0x1B: // ELENG
            {
                float s = m_state.vf[is][0]*m_state.vf[is][0] + m_state.vf[is][1]*m_state.vf[is][1] + m_state.vf[is][2]*m_state.vf[is][2];
                m_state.p = std::sqrt(s);
                return;
            }
            case 0x1C: // ERCPR
            {
                int fsf = (instr >> 21) & 0x3;
                float val = m_state.vf[is][fsf];
                m_state.p = (val != 0.0f) ? (1.0f / val) : std::numeric_limits<float>::max();
                return;
            }
            case 0x1D: // ERLENG
            {
                float s = m_state.vf[is][0]*m_state.vf[is][0] + m_state.vf[is][1]*m_state.vf[is][1] + m_state.vf[is][2]*m_state.vf[is][2];
                float len = std::sqrt(s);
                m_state.p = (len != 0.0f) ? (1.0f / len) : std::numeric_limits<float>::max();
                return;
            }
            case 0x1E: // WAITP
                return;
            case 0x1A: // EATAN / EATANxy / EATANxz
                return;
            case 0x1F: // MFP (Move From P register)
            {
                float result[4] = { m_state.p, m_state.p, m_state.p, m_state.p };
                applyDest(m_state.vf[it], result, dest);
                return;
            }
            default:
                return;
            }
        }
        case 0x3D: // XGKICK — send GIF packet from VU1 data memory
        {
            uint32_t addr = ((uint32_t)(uint16_t)m_state.vi[is]) * 16u;
            addr &= (dataSize - 1);
            // Walk the GIF packet to find its total size
            uint32_t pktOff = addr;
            uint32_t totalBytes = 0;
            bool done = false;
            for (int safety = 0; safety < 256 && !done; ++safety)
            {
                if (pktOff + 16 > dataSize) break;
                uint64_t tagLo;
                std::memcpy(&tagLo, vuData + pktOff, 8);
                uint32_t nloop = (uint32_t)(tagLo & 0x7FFF);
                uint8_t flg = (uint8_t)((tagLo >> 58) & 0x3);
                uint32_t nreg = (uint32_t)((tagLo >> 60) & 0xF);
                if (nreg == 0) nreg = 16;
                bool eop = (tagLo >> 15) & 1;

                uint32_t pktSize = 16; // GIF tag
                if (flg == 0) // PACKED
                    pktSize += nloop * nreg * 16;
                else if (flg == 1) // REGLIST
                {
                    uint32_t regs = nloop * nreg;
                    pktSize += regs * 8;
                    if (regs & 1) pktSize += 8; // pad to 128-bit
                }
                else if (flg == 2) // IMAGE
                    pktSize += nloop * 16;

                pktOff += pktSize;
                totalBytes += pktSize;
                if (eop) done = true;
            }
            if (totalBytes > 0 && addr + totalBytes <= dataSize)
            {
                if (memory)
                    memory->submitGifPacket(GifPathId::Path1, vuData + addr, totalBytes);
                else
                    gs.processGIFPacket(vuData + addr, totalBytes);
            }
            return;
        }
        case 0x3E: // XTOP
        {
            if (it != 0) m_state.vi[it] = (int32_t)m_state.itop;
            return;
        }
        case 0x3F: // XITOP
        {
            if (it != 0) m_state.vi[it] = (int32_t)m_state.itop;
            return;
        }
        default:
            return;
        }
    }
    default:
        break;
    }
}
