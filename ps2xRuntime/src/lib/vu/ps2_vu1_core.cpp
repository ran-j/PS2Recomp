#include "runtime/ps2_vu1.h"
#include "ps2_vu1_detail.h"

#include <cstring>

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
    if (dest & 0x8)
        dst[0] = result[0]; // x
    if (dest & 0x4)
        dst[1] = result[1]; // y
    if (dest & 0x2)
        dst[2] = result[2]; // z
    if (dest & 0x1)
        dst[3] = result[3]; // w
}

void VU1Interpreter::applyDestAcc(const float *result, uint8_t dest)
{
    applyDest(m_state.acc, result, dest);
}

void VU1Interpreter::execute(uint8_t *vuCode, uint32_t codeSize,
                             uint8_t *vuData, uint32_t dataSize,
                             GS &gs, PS2Memory *memory,
                             uint32_t startPC, uint32_t top, uint32_t itop,
                             uint32_t maxCycles)
{
    m_state.pc = startPC & 0x3FFFu;
    m_state.ebit = false;
    m_state.top = top;
    m_state.itop = itop;
    m_state.branchPending = false;
    m_state.branchTarget = 0;
    m_state.branchDelay = 0;
    m_state.vf[0][0] = 0.0f;
    m_state.vf[0][1] = 0.0f;
    m_state.vf[0][2] = 0.0f;
    m_state.vf[0][3] = 1.0f;
    run(vuCode, codeSize, vuData, dataSize, gs, memory, maxCycles);
}

void VU1Interpreter::resume(uint8_t *vuCode, uint32_t codeSize,
                            uint8_t *vuData, uint32_t dataSize,
                            GS &gs, PS2Memory *memory,
                            uint32_t top, uint32_t itop, uint32_t maxCycles)
{
    m_state.ebit = false;
    m_state.top = top;
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

        const bool iBit = ((upper >> 31) & 1u) != 0u;
        const bool eBit = ((upper >> 30) & 1u) != 0u;
        const bool mBit = ((upper >> 29) & 1u) != 0u;
        (void)mBit;

        // LOI is controlled by the upper I-bit.  The lower word is the float immediate.
        // DobieStation executes the upper instruction first, then commits lower into I.
        const bool loi = iBit;
        if (loi)
        {
            // LOI is special: the upper instruction sees the old I value, then LOI loads I.
            execUpper(upper);
            std::memcpy(&m_state.i, &lower, 4);
        }
        else if (vuLowerShouldRunBeforeUpper(upper, lower))
        {
            // VU upper/lower execute as a pair.  If the upper op writes a VF register
            // that the lower op reads or also writes, Dobie runs the lower side first
            // so it observes the old VF value and the upper write has priority.
            execLower(lower, vuData, dataSize, gs, memory, upper);
            execUpper(upper);
        }
        else
        {
            execUpper(upper);
            execLower(lower, vuData, dataSize, gs, memory, upper);
        }

        // Enforce VF0 invariant
        m_state.vf[0][0] = 0.0f;
        m_state.vf[0][1] = 0.0f;
        m_state.vf[0][2] = 0.0f;
        m_state.vf[0][3] = 1.0f;
        // Enforce VI0 invariant
        m_state.vi[0] = 0;

        uint32_t nextPC = m_state.pc + 8;
        if (nextPC >= codeSize)
            nextPC = 0;
        m_state.pc = nextPC;

        // VU branch/jump has a delay slot. Branch handlers set a pending target;
        // we execute one sequential instruction before committing the branch.
        if (m_state.branchPending)
        {
            if (m_state.branchDelay == 0)
            {
                m_state.pc = m_state.branchTarget & 0x3FFFu;
                m_state.branchPending = false;
            }
            else
            {
                --m_state.branchDelay;
            }
        }

        if (m_state.ebit)
            break;

        if (eBit)
            m_state.ebit = true;
    }
}
