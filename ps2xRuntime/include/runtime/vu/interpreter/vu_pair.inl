#pragma once
#include <cstring>
#include <type_traits>

#include "vu_inline.h"

template <class Upper, class Lower>
PS2VU_INLINE bool VUExecutor::issuePair(const DecodedInstructionPair &decoded,
                                        Upper upper,
                                        Lower lower,
                                        uint32_t codeSize,
                                        uint64_t budgetEnd)
{
    commitReadyPipelines();
    if (decoded.upperUsage.reserved || decoded.lowerUsage.reserved)
    {
        reportReservedInstruction(decoded.upperUsage.reserved,
                                  decoded.upperUsage.reserved ? decoded.upper : decoded.lower);
        return false;
    }

    uint64_t readyCycle = calculatePairReadyCycle(decoded);
    while (readyCycle > m_cycle)
    {
        if (readyCycle >= budgetEnd)
        {
            advanceTo(budgetEnd);
            return false;
        }
        advanceTo(readyCycle);
        readyCycle = calculatePairReadyCycle(decoded);
    }

    if (m_cycle >= budgetEnd)
        return false;

    if (decoded.mBit)
        m_state.mBit = true;

    uint8_t writtenVi = 0u;
    int32_t oldVi = 0;
    const uint32_t viWrites = decoded.lowerUsage.viWrite & ~1u;
    if (viWrites != 0u)
    {
        writtenVi = static_cast<uint8_t>(std::countr_zero(viWrites));
        oldVi = m_state.vi[writtenVi];
    }

    const VfAccess upperWrite = decoded.upperUsage.vfWrite;
    const VfAccess lowerWrite = decoded.lowerUsage.vfWrite;
    const bool hasUpperWrite = upperWrite.reg != 0u;
    const bool hasLowerWrite = lowerWrite.reg != 0u && decoded.suppressedLowerVf != lowerWrite.reg;
    float upperResult[4]{};
    float oldLowerVf[4]{};
    if (lowerWrite.reg != 0u)
        std::memcpy(oldLowerVf, m_state.vf[lowerWrite.reg], sizeof(oldLowerVf));

    upper(hasUpperWrite ? upperResult : m_state.vf[0], upperResult);
    if (decoded.iBit)
    {
        float immediate = 0.0f;
        std::memcpy(&immediate, &decoded.lower, sizeof(immediate));
        m_state.i = normalizeOperand(immediate);
    }
    else
        lower();

    m_viBranchBackupValid = false;

    if (hasUpperWrite)
    {
        const uint32_t latency =
            decoded.upperUsage.vfLatency != 0u ? decoded.upperUsage.vfLatency : decoded.upperUsage.latency;
        queueVfWrite(upperWrite.reg, upperWrite.lanes, upperResult, latency);
    }

    if (lowerWrite.reg != 0u)
    {
        if (hasLowerWrite)
        {
            const uint32_t latency =
                decoded.lowerUsage.vfLatency != 0u ? decoded.lowerUsage.vfLatency : decoded.lowerUsage.latency;
            queueVfWrite(lowerWrite.reg, lowerWrite.lanes, m_state.vf[lowerWrite.reg], latency);
        }

        std::memcpy(m_state.vf[lowerWrite.reg], oldLowerVf, sizeof(oldLowerVf));
    }

    if (decoded.upperUsage.accWrite != 0u)
    {
        queueAccWrite(decoded.upperUsage.accWrite, upperResult, kAccForwardLatency);
    }

    if (writtenVi != 0u)
    {
        const int32_t newVi = m_state.vi[writtenVi];
        m_state.vi[writtenVi] = oldVi;
        const uint32_t latency =
            decoded.lowerUsage.viLatency != 0u ? decoded.lowerUsage.viLatency : decoded.lowerUsage.latency;
        queueViWrite(writtenVi, newVi, latency);
    }

    markPairWrites(decoded);
    if (writtenVi != 0u && decoded.lowerUsage.delaysNextBranchRead)
        recordViWriteForBranch(writtenVi, oldVi);

    m_state.vf[0][0] = 0.0f;
    m_state.vf[0][1] = 0.0f;
    m_state.vf[0][2] = 0.0f;
    m_state.vf[0][3] = 1.0f;
    m_state.vi[0] = 0;

    uint32_t nextPc = m_state.pc + 8u;
    if (nextPc >= codeSize)
        nextPc = 0u;
    m_state.pc = nextPc;

    if (m_state.branchPending)
    {
        if (m_state.branchDelay == 0u)
        {
            m_state.pc = m_state.branchTarget & microAddressMask();
            m_state.branchPending = false;
        }
        else
        {
            --m_state.branchDelay;
        }
    }

    const bool dHalt = decoded.dBit && m_state.dBitEnabled;
    const bool tHalt = decoded.tBit && m_state.tBitEnabled;
    const bool haltBit = dHalt || tHalt;
    const bool haltBranch = haltBit && decoded.lowerUsage.pipeline == PipelineBranch;

    if (m_state.haltAfterDelaySlot)
    {
        m_state.stoppedByD = m_pendingHaltD;
        m_state.stoppedByT = m_pendingHaltT;
        m_programEnded = true;
    }
    else if (m_state.ebit)
        m_programEnded = true;
    else if (haltBit && !haltBranch)
    {
        m_state.stoppedByD = dHalt;
        m_state.stoppedByT = tHalt;
        m_programEnded = true;
    }
    else if (decoded.eBit)
        m_state.ebit = true;
    else if (haltBranch)
    {
        m_state.haltAfterDelaySlot = true;
        m_pendingHaltD = dHalt;
        m_pendingHaltT = tHalt;
    }

    advanceOneCycle();
    return !m_programEnded && m_cycle < budgetEnd && !m_stopRequested;
}
