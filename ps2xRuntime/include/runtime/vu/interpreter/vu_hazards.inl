#pragma once
#include "vu_inline.h"
#include <cmath>
#include <cstring>
#include <limits>

inline uint64_t VUExecutor::calculatePairReadyCycle(const DecodedInstructionPair &decoded) const
{
    uint64_t ready = m_cycle;
    for (uint32_t i = 0; i < decoded.readDependencyCount; ++i)
        ready = std::max(ready, m_registerReady[decoded.readDependencies[i]]);

    if (decoded.lowerUsage.pipeline == PipelineFdiv && m_fdiv.valid)
        ready = std::max(ready, m_fdiv.readyCycle);
    if (decoded.lowerUsage.pipeline == PipelineEfu)
        ready = std::max(ready, m_efuResourceReady);
    if (decoded.lowerUsage.waitQ && m_fdiv.valid)
        ready = std::max(ready, m_fdiv.readyCycle);
    if (decoded.lowerUsage.waitP)
    {
        for (const ScalarPipelineEntry &entry : m_efu)
            if (entry.valid)
                ready = std::max(ready, entry.readyCycle - 1u);
    }
    if (decoded.lowerUsage.pipeline == PipelineXgkick && m_xgkick.active)
        ready = std::max(ready, m_cycle + 1u);
    return ready;
}

inline void VUExecutor::markPairWrites(const DecodedInstructionPair &decoded)
{
    const VfAccess lowerWrite = decoded.lowerUsage.vfWrite;
    if (lowerWrite.reg != 0u && decoded.suppressedLowerVf != lowerWrite.reg)
    {
        const uint32_t latency =
            decoded.lowerUsage.vfLatency != 0u ? decoded.lowerUsage.vfLatency : decoded.lowerUsage.latency;
        for (uint32_t component = 0; component < 4u; ++component)
        {
            if ((lowerWrite.lanes & ps2vu::laneForComponent(component)) != 0u)
                m_registerReady[4u * lowerWrite.reg + component] = m_cycle + latency;
        }
    }

    const VfAccess upperWrite = decoded.upperUsage.vfWrite;
    if (upperWrite.reg != 0u)
    {
        const uint32_t latency =
            decoded.upperUsage.vfLatency != 0u ? decoded.upperUsage.vfLatency : decoded.upperUsage.latency;
        for (uint32_t component = 0; component < 4u; ++component)
        {
            if ((upperWrite.lanes & ps2vu::laneForComponent(component)) != 0u)
                m_registerReady[4u * upperWrite.reg + component] = m_cycle + latency;
        }
    }

    for (uint32_t regs = decoded.lowerUsage.viWrite & ~1u; regs != 0u; regs &= regs - 1u)
        m_registerReady[kViReadyBase + std::countr_zero(regs)] =
            m_cycle + (decoded.lowerUsage.viLatency != 0u ? decoded.lowerUsage.viLatency : decoded.lowerUsage.latency);
    for (uint32_t component = 0; component < 4u; ++component)
    {
        if ((decoded.upperUsage.accWrite & ps2vu::laneForComponent(component)) != 0u)
            m_registerReady[kAccReadyBase + component] = m_cycle + kAccForwardLatency;
    }
}
