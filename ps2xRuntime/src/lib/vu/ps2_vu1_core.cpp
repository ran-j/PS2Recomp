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
    std::memset(m_flagPipeline, 0, sizeof(m_flagPipeline));
    m_pendingFlagUpdate = {};
    m_flagPipelineHead = 0;
    m_workingMac = 0;
    m_workingStatus = 0;
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

void VU1Interpreter::updateFmacFlags(float *result, uint8_t dest)
{
    uint32_t mac = 0u;

    for (uint32_t component = 0; component < 4u; ++component)
    {
        const uint32_t laneBit = 1u << (3u - component);
        if ((dest & laneBit) == 0u)
            continue;

        uint32_t bits = 0u;
        std::memcpy(&bits, &result[component], sizeof(bits));
        const uint32_t exponent = (bits >> 23) & 0xFFu;
        const uint32_t magnitude = bits & 0x7FFFFFFFu;
        const uint32_t sign = bits & 0x80000000u;

        if (sign != 0u)
            mac |= laneBit << 4;

        if (magnitude == 0u)
        {
            mac |= laneBit;
        }
        else if (exponent == 0u)
        {
            // VU FMAC units flush denormals to signed zero and report Z+U.
            mac |= laneBit;
            mac |= laneBit << 8;
            bits = sign;
            std::memcpy(&result[component], &bits, sizeof(bits));
        }
        else if (exponent == 0xFFu)
        {
            // Infinity and NaN are represented as signed maximum finite values.
            mac |= laneBit << 12;
            bits = sign | 0x7F7FFFFFu;
            std::memcpy(&result[component], &bits, sizeof(bits));
        }
    }

    uint32_t status = 0u;
    if ((mac & 0x000Fu) != 0u)
        status |= 0x1u;
    if ((mac & 0x00F0u) != 0u)
        status |= 0x2u;
    if ((mac & 0x0F00u) != 0u)
        status |= 0x4u;
    if ((mac & 0xF000u) != 0u)
        status |= 0x8u;

    m_workingMac = mac;
    m_workingStatus = status;
    m_pendingFlagUpdate = {m_workingMac, m_workingStatus, true, false};
}

void VU1Interpreter::applyFmacDest(float *dst, float *result, uint8_t dest)
{
    updateFmacFlags(result, dest);
    applyDest(dst, result, dest);
}

void VU1Interpreter::applyFmacDestAcc(float *result, uint8_t dest)
{
    updateFmacFlags(result, dest);
    applyDestAcc(result, dest);
}

void VU1Interpreter::queueFsset(uint16_t immediate)
{
    m_workingStatus =
        (static_cast<uint32_t>(immediate) & 0xFC0u) |
        (m_workingStatus & 0x3Fu);
    m_pendingFlagUpdate = {m_workingMac, m_workingStatus, true, true};
}

void VU1Interpreter::commitFlagPipelineEntry(FlagPipelineEntry &entry)
{
    if (!entry.valid)
        return;

    if (entry.writesSticky)
    {
        m_state.status =
            (m_state.status & 0x30u) |
            (entry.status & 0xFC0u) |
            (entry.status & 0xFu);
    }
    else
    {
        const uint32_t current = entry.status & 0xFu;
        m_state.status =
            (m_state.status & 0xFF0u) |
            current |
            (current << 6);
    }
    m_state.mac = entry.mac;
    entry = {};
}

void VU1Interpreter::beginFlagPipelineCycle()
{
    commitFlagPipelineEntry(m_flagPipeline[m_flagPipelineHead]);
    m_pendingFlagUpdate = {};
}

void VU1Interpreter::endFlagPipelineCycle()
{
    if (m_pendingFlagUpdate.valid)
        m_flagPipeline[m_flagPipelineHead] = m_pendingFlagUpdate;
    m_pendingFlagUpdate = {};
    m_flagPipelineHead = (m_flagPipelineHead + 1u) % kFlagPipelineLatency;
}

void VU1Interpreter::flushFlagPipeline()
{
    for (uint32_t i = 0; i < kFlagPipelineLatency; ++i)
    {
        commitFlagPipelineEntry(m_flagPipeline[m_flagPipelineHead]);
        m_flagPipelineHead = (m_flagPipelineHead + 1u) % kFlagPipelineLatency;
    }
    m_pendingFlagUpdate = {};
}

bool VU1Interpreter::hasPendingFlagPipelineEntries() const
{
    for (const FlagPipelineEntry &entry : m_flagPipeline)
    {
        if (entry.valid)
            return true;
    }
    return false;
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

    const bool trackedVu1Code = memory != nullptr && vuCode == memory->getVU1Code();
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
    if (!hasPendingFlagPipelineEntries())
    {
        m_workingMac = m_state.mac;
        m_workingStatus = m_state.status;
    }

    bool programEnded = false;
    for (uint32_t cycle = 0; cycle < maxCycles; ++cycle)
    {
        if (m_state.pc + 8 > codeSize)
            break;

        beginFlagPipelineCycle();
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
        endFlagPipelineCycle();

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
        {
            programEnded = true;
            break;
        }

        if (decoded.eBit)
            m_state.ebit = true;
    }

    if (programEnded)
        flushFlagPipeline();
}
