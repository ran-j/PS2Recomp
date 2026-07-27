#include "runtime/ps2_vu1.h"
#include "runtime/ps2_memory.h"
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

void VU1Interpreter::writeDestMasked(float *dst, const float *result, uint8_t dest)
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
    // ACC-writers are FMAC ops: MAC reflects the FMAC result whether it targets vf or acc.
    computeFmacFlags(result, dest);
    writeDestMasked(m_state.acc, result, dest);
}

// MAC bit layout sourced from PCSX2 pcsx2/VUflags.cpp (VU_MAC_UPDATE / VU_STAT_UPDATE):
// nibbles from low to high are Z[3:0], S[7:4], U[11:8], O[15:12]; within each nibble the
// bit order is x,y,z,w from high to low (x uses shift 3, y shift 2, z shift 1, w shift 0).
// Z is set for +/-0.0f or a denormal (an underflow PCSX2 flushes to zero, raising both U
// and Z together); S mirrors the IEEE754 sign bit unconditionally; U is a nonzero denormal;
// O is an exponent field of all ones (inf or NaN).
// Lanes outside DEST are not evaluated and read 0 in every group, matching PCSX2's per-lane
// VU_MACx/y/z/w_CLEAR calls for lanes the FMAC op did not write.
void VU1Interpreter::computeFmacFlags(const float *result, uint8_t dest)
{
    static const uint8_t kLaneDestBit[4] = {0x8u, 0x4u, 0x2u, 0x1u}; // x, y, z, w
    uint32_t mac = 0u;

    for (int c = 0; c < 4; ++c)
    {
        if ((dest & kLaneDestBit[c]) == 0u)
            continue;

        uint32_t bits;
        std::memcpy(&bits, &result[c], sizeof(bits));
        const uint32_t exp = (bits >> 23) & 0xFFu;
        const int shift = 3 - c;

        if (bits & 0x80000000u)
            mac |= (0x0010u << shift); // S

        if (result[c] == 0.0f)
        {
            mac |= (0x0001u << shift); // Z
        }
        else if (exp == 0u)
        {
            mac |= (0x0101u << shift); // U + Z: a denormal is an underflow PCSX2 flushes to zero
        }
        else if (exp == 0xFFu)
        {
            mac |= (0x1000u << shift); // O
        }
    }

    m_state.mac = mac;

    // STATUS folds MAC the way PCSX2's VU_STAT_UPDATE does: a live bit is set if ANY dest
    // lane raised that condition this instruction. The live half is bits[3:0] (Z,S,U,O in
    // that order) and is replaced whole every FMAC; the sticky half is bits[9:6] and only
    // ever accumulates (OR), matching how FSOR/FSAND read "has this ever happened". Bits
    // 4,5,10,11 (I/D, live and sticky) are not modelled here and stay zero.
    uint32_t live = 0u;
    if (mac & 0x000Fu)
        live |= 0x1u; // Z
    if (mac & 0x00F0u)
        live |= 0x2u; // S
    if (mac & 0x0F00u)
        live |= 0x4u; // U
    if (mac & 0xF000u)
        live |= 0x8u; // O

    const uint32_t stickyPrev = (m_state.status >> 6) & 0xFu;
    const uint32_t sticky = stickyPrev | live;

    m_state.status = (live & 0x3Fu) | ((sticky & 0x3Fu) << 6);
}

VU1Interpreter::DecodedInstructionPair VU1Interpreter::decodeInstructionPair(const uint8_t *vuCode, uint32_t pc) const
{
    DecodedInstructionPair decoded;
    std::memcpy(&decoded.lower, vuCode + pc, sizeof(decoded.lower));
    std::memcpy(&decoded.upper, vuCode + pc + sizeof(decoded.lower), sizeof(decoded.upper));

    decoded.iBit = ((decoded.upper >> 31) & 1u) != 0u;
    decoded.eBit = ((decoded.upper >> 30) & 1u) != 0u;
    decoded.lowerBeforeUpper = !decoded.iBit && vuLowerShouldRunBeforeUpper(decoded.upper, decoded.lower);
    return decoded;
}

void VU1Interpreter::rebuildDecodedCodeCache(const uint8_t *vuCode, uint32_t codeSize,
                                             const PS2Memory *memory, uint64_t generation)
{
    const uint32_t pairCount = codeSize / 8u;
    m_decodedCodeCache.resize(pairCount);
    for (uint32_t i = 0; i < pairCount; ++i)
    {
        m_decodedCodeCache[i] = decodeInstructionPair(vuCode, i * 8u);
    }

    m_cachedVuCode = vuCode;
    m_cachedMemory = memory;
    m_cachedCodeSize = codeSize;
    m_cachedCodeGeneration = generation;
    m_decodedCodeCacheValid = true;
}

VU1Interpreter::DecodedInstructionPair VU1Interpreter::getDecodedInstructionPairForPc(const uint8_t *vuCode,
                                                                                      uint32_t codeSize,
                                                                                      PS2Memory *memory,
                                                                                      uint32_t pc)
{
    // Only 8-byte aligned VU instruction pairs can use the decode cache.
    if ((pc & 7u) != 0u)
    {
        return decodeInstructionPair(vuCode, pc);
    }

    const bool trackedVu1Code = vuCode == memory->getVU1Code();
    if (!trackedVu1Code)
    {
        return decodeInstructionPair(vuCode, pc);
    }

    const uint64_t generation = memory->getVU1CodeGeneration();
    const bool rebuild =
        !m_decodedCodeCacheValid ||
        m_cachedVuCode != vuCode ||
        m_cachedMemory != memory ||
        m_cachedCodeSize != codeSize ||
        m_cachedCodeGeneration != generation;

    if (rebuild)
    {
        rebuildDecodedCodeCache(vuCode, codeSize, memory, generation);
    }

    return m_decodedCodeCache[pc / 8u];
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

        const DecodedInstructionPair decoded = getDecodedInstructionPairForPc(vuCode, codeSize, memory, m_state.pc);

        // LOI is controlled by the upper I-bit.  The lower word is the float immediate.
        // DobieStation executes the upper instruction first, then commits lower into I.
        if (decoded.iBit)
        {
            // LOI is special: the upper instruction sees the old I value, then LOI loads I.
            execUpper(decoded.upper);
            std::memcpy(&m_state.i, &decoded.lower, sizeof(decoded.lower));
        }
        else if (decoded.lowerBeforeUpper)
        {
            // VU upper/lower execute as a pair.  If the upper op writes a VF register
            // that the lower op reads or also writes, Dobie runs the lower side first
            // so it observes the old VF value and the upper write has priority.
            execLower(decoded.lower, vuData, dataSize, gs, memory, decoded.upper);
            execUpper(decoded.upper);
        }
        else
        {
            execUpper(decoded.upper);
            execLower(decoded.lower, vuData, dataSize, gs, memory, decoded.upper);
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

        if (decoded.eBit)
            m_state.ebit = true;
    }
}
