#include "runtime/vu/interpreter/vu_executor.h"
#include "runtime/vu/vu_unit.h"
#include "runtime/gs/ps2_gif_arbiter.h"
#include "runtime/gs/gs_frontend.h"
#include "runtime/ps2_memory.h"

#include "runtime/vu/vu_decoder.h"
#include "runtime/vu/interpreter/vu_fmac.inl"
#include "runtime/vu/interpreter/vu_hazards.inl"
#include "runtime/vu/interpreter/vu_upper.inl"
#include "runtime/vu/interpreter/vu_lower.inl"
#include "runtime/vu/interpreter/vu_pair.inl"

using namespace ps2vu;

#include <algorithm>
#include <cfenv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <sstream>
#include <ps2_log.h>

VUExecutor::VUExecutor(Unit unit) : m_unit(unit)
{
    reset();
}

VUExecutor::~VUExecutor() = default;

void VUExecutor::resetScheduler()
{
    if (!m_schedulerClean)
    {
        m_flagActive = m_efuActive = m_storeActive = 0;
        m_vfWriteActive = m_viWriteActive = m_accWriteActive = 0;
        m_nextPipelineCycle = kNoPipelineEvent;
        m_pipelineEvents.reset();
        m_flagPipeline = {};
        m_fdiv = {};
        m_efu = {};
        m_storePipeline = {};
        m_vfWritePipeline = {};
        m_viWritePipeline = {};
        m_accWritePipeline = {};
        m_registerReady = {};
        m_vfCommittedWrite = {};
        m_viCommittedWrite = {};
        m_accLatestWrite = {};
        m_nextWriteSequence = 0;
        m_efuResourceReady = 0;
    }
    m_schedulerClean = true;
    m_xgkick.reset();
    m_workingClip = m_state.clip;
    m_viBranchBackupValue = 0;
    m_viBranchBackupReg = 0;
    m_viBranchBackupValid = false;
    m_stopRequested = false;
    m_pendingHaltD = false;
    m_pendingHaltT = false;
}

void VUExecutor::reset()
{
    std::memset(&m_state, 0, sizeof(m_state));
    m_state.vf[0][3] = 1.0f;
    m_state.q = 1.0f;
    m_state.r = 0x3F800000u;
    m_cycle = 0;
    resetScheduler();
}

uint32_t VUExecutor::microAddressMask() const
{
    return m_unit == Unit::VU1 ? 0x3FFFu : 0x0FFFu;
}

int32_t VUExecutor::readBranchVi(uint8_t reg) const
{
    if (reg == 0u)
        return 0;
    if (m_viBranchBackupValid && m_viBranchBackupReg == reg)
    {
        return m_viBranchBackupValue;
    }
    return m_state.vi[reg];
}

void VUExecutor::recordViWriteForBranch(uint8_t reg, int32_t oldValue)
{
    if (reg == 0u)
        return;
    m_viBranchBackupValue = oldValue;
    m_viBranchBackupReg = reg;
    m_viBranchBackupValid = true;
}

void VUExecutor::queueFsset(uint16_t immediate)
{
    for (FlagPipelineEntry &entry : m_flagPipeline)
    {
        if (entry.valid && entry.issueCycle == m_cycle)
            entry.writesStatus = false;
    }

    if (auto *entry = allocatePipelineEntry<WritebackKind::Flag>(m_flagPipeline, m_flagActive, m_cycle + kFmacLatency))
    {
        entry->issueCycle = m_cycle;
        entry->status = static_cast<uint32_t>(immediate) & 0xFC0u;
        entry->writesSticky = true;
        return;
    }
    reportReservedInstruction(false, 0xFFFFFFFEu);
}

void VUExecutor::queueClip(uint32_t clip)
{
    m_workingClip = ((m_workingClip << 6) | (clip & 0x3Fu)) & 0xFFFFFFu;
    if (auto *entry = allocatePipelineEntry<WritebackKind::Flag>(m_flagPipeline, m_flagActive, m_cycle + kFmacLatency))
    {
        entry->issueCycle = m_cycle;
        entry->clip = m_workingClip;
        entry->writesClip = true;
        return;
    }
    reportReservedInstruction(true, 0xFFFFFFFDu);
}

void VUExecutor::queueFcset(uint32_t clip)
{
    m_workingClip = clip & 0xFFFFFFu;
    for (FlagPipelineEntry &entry : m_flagPipeline)
    {
        if (entry.valid && entry.issueCycle == m_cycle)
            entry.writesClip = false;
    }
    if (auto *entry = allocatePipelineEntry<WritebackKind::Flag>(m_flagPipeline, m_flagActive, m_cycle + kFmacLatency))
    {
        entry->issueCycle = m_cycle;
        entry->clip = m_workingClip;
        entry->writesClip = true;
        return;
    }
    reportReservedInstruction(false, 0xFFFFFFFAu);
}

void VUExecutor::queueQ(float value, uint32_t latency, uint32_t statusDi)
{
    uint32_t ignoredFlags = 0u;
    value = normalizeResult(value, ignoredFlags);
    m_pipelineEvents.schedule<WritebackKind::Fdiv>(m_cycle, latency, 0);
    m_fdiv.valid = true;
    m_fdiv.readyCycle = m_cycle + latency;
    m_nextPipelineCycle = std::min(m_nextPipelineCycle, m_fdiv.readyCycle);
    m_fdiv.value = value;
    m_fdiv.statusDi = statusDi & 0x30u;
}

void VUExecutor::queueP(float value, uint32_t latency)
{
    uint32_t ignoredFlags = 0u;
    value = normalizeResult(value, ignoredFlags);
    if (auto *entry = allocatePipelineEntry<WritebackKind::Efu>(m_efu, m_efuActive, m_cycle + latency))
    {
        entry->value = value;
        m_efuResourceReady = m_cycle + (latency > 0u ? latency - 1u : 0u);
        return;
    }
    reportReservedInstruction(false, 0xFFFFFFF9u);
}

void VUExecutor::queueStore(uint32_t address, const uint32_t words[4], uint8_t laneMask)
{
    if (m_unit == Unit::VU0 && (address & 0x4000u))
    {
        writeData(address, words, laneMask);
        return;
    }
    if (auto *store = allocatePipelineEntry<WritebackKind::Store>(m_storePipeline, m_storeActive, m_cycle + 1u))
    {
        store->address = address;
        store->laneMask = laneMask;
        std::copy(words, words + 4, store->words.begin());
        return;
    }
    reportReservedInstruction(false, 0xFFFFFFFCu);
}

void VUExecutor::queueVfWrite(uint8_t reg, uint8_t laneMask, const float value[4], uint32_t latency)
{
    if (reg == 0u || laneMask == 0u)
        return;
    if (auto *write = allocatePipelineEntry<WritebackKind::Vf>(m_vfWritePipeline, m_vfWriteActive, m_cycle + latency))
    {
        write->sequence = ++m_nextWriteSequence;
        write->reg = reg;
        write->laneMask = laneMask;
        std::copy(value, value + 4, write->value.begin());
        return;
    }
    reportReservedInstruction(false, 0xFFFFFFF7u);
}

void VUExecutor::queueViWrite(uint8_t reg, int32_t value, uint32_t latency)
{
    if (reg == 0u)
        return;
    if (auto *write = allocatePipelineEntry<WritebackKind::Vi>(m_viWritePipeline, m_viWriteActive, m_cycle + latency))
    {
        write->sequence = ++m_nextWriteSequence;
        write->reg = reg;
        write->value = value;
        return;
    }
    reportReservedInstruction(false, 0xFFFFFFF6u);
}

void VUExecutor::queueAccWrite(uint8_t laneMask, const float value[4], uint32_t latency)
{
    if (laneMask == 0u)
        return;
    if (auto *write = allocatePipelineEntry<WritebackKind::Acc>(m_accWritePipeline, m_accWriteActive, m_cycle + latency))
    {
        write->sequence = ++m_nextWriteSequence;
        write->laneMask = laneMask;
        std::copy(value, value + 4, write->value.begin());
        for (uint32_t component = 0; component < 4u; ++component)
        {
            if ((laneMask & laneForComponent(component)) != 0u)
                m_accLatestWrite[component] = write->sequence;
        }
        return;
    }
    reportReservedInstruction(true, 0xFFFFFFF5u);
}

void VUExecutor::commitReadyPipelines()
{
    if (m_cycle < m_nextPipelineCycle)
        return;
    const uint64_t events = m_pipelineEvents.take(m_cycle);
    m_nextPipelineCycle = m_pipelineEvents.nextCycle(m_cycle);
    for (uint32_t pending = PipelineEvents::slots<WritebackKind::Flag>(events); pending != 0u; pending &= pending - 1u)
    {
        const uint32_t slot = std::countr_zero(pending);
        FlagPipelineEntry &entry = m_flagPipeline[slot];
        m_flagActive &= ~(1u << slot);

        if (entry.writesMac)
            m_state.mac = entry.mac;
        if (entry.writesStatus)
        {
            const uint32_t current = entry.status & 0xFu;
            m_state.status = (m_state.status & 0xFF0u) | current | ((current | entry.extraSticky) << 6);
        }
        if (entry.writesSticky)
        {
            m_state.status = (m_state.status & 0x03Fu) | (entry.status & 0xFC0u);
        }
        if (entry.writesClip)
            m_state.clip = entry.clip;
        entry.valid = false;
    }

    if (PipelineEvents::slots<WritebackKind::Fdiv>(events) != 0)
    {
        m_state.q = m_fdiv.value;
        const uint32_t currentDi = m_fdiv.statusDi & 0x30u;
        m_state.status = (m_state.status & 0xFCFu) | currentDi | (currentDi << 6);
        m_fdiv = {};
    }

    for (uint32_t pending = PipelineEvents::slots<WritebackKind::Efu>(events); pending != 0u; pending &= pending - 1u)
    {
        const uint32_t slot = std::countr_zero(pending);
        ScalarPipelineEntry &entry = m_efu[slot];
        m_efuActive &= ~(1u << slot);
        m_state.p = entry.value;
        entry.valid = false;
    }

    for (uint32_t pending = PipelineEvents::slots<WritebackKind::Store>(events); pending != 0u; pending &= pending - 1u)
    {
        const uint32_t slot = std::countr_zero(pending);
        PendingStore &store = m_storePipeline[slot];
        m_storeActive &= ~(1u << slot);
        writeData(store.address, store.words.data(), store.laneMask);
    }

    for (uint32_t pending = PipelineEvents::slots<WritebackKind::Vf>(events); pending != 0u; pending &= pending - 1u)
    {
        const uint32_t slot = std::countr_zero(pending);
        PendingVfWrite &write = m_vfWritePipeline[slot];
        m_vfWriteActive &= ~(1u << slot);
        for (uint32_t component = 0; component < 4u; ++component)
        {
            if ((write.laneMask & laneForComponent(component)) != 0u &&
                m_vfCommittedWrite[write.reg][component] <= write.sequence)
            {
                m_state.vf[write.reg][component] = write.value[component];
                m_vfCommittedWrite[write.reg][component] = write.sequence;
            }
        }
    }

    for (uint32_t pending = PipelineEvents::slots<WritebackKind::Vi>(events); pending != 0u; pending &= pending - 1u)
    {
        const uint32_t slot = std::countr_zero(pending);
        PendingViWrite &write = m_viWritePipeline[slot];
        m_viWriteActive &= ~(1u << slot);
        if (m_viCommittedWrite[write.reg] <= write.sequence)
        {
            m_state.vi[write.reg] = static_cast<int16_t>(write.value);
            m_viCommittedWrite[write.reg] = write.sequence;
        }
    }

    for (uint32_t pending = PipelineEvents::slots<WritebackKind::Acc>(events); pending != 0u; pending &= pending - 1u)
    {
        const uint32_t slot = std::countr_zero(pending);
        PendingAccWrite &write = m_accWritePipeline[slot];
        m_accWriteActive &= ~(1u << slot);
        for (uint32_t component = 0; component < 4u; ++component)
        {
            if ((write.laneMask & laneForComponent(component)) != 0u && m_accLatestWrite[component] == write.sequence)
            {
                m_state.acc[component] = write.value[component];
            }
        }
    }
}

void VUExecutor::progressXgkick(uint32_t elapsedCycles)
{
    if (!m_xgkick.active || !m_activeVuData || m_activeVuDataSize == 0u)
        return;

    m_xgkick.cycleCredit += elapsedCycles;
    while (m_xgkick.active && m_xgkick.cycleCredit >= 2u)
    {
        m_xgkick.cycleCredit -= 2u;
        if (m_xgkick.copiedBytes > XgkickPipeline::kBufferSize - 16u)
        {
            reportReservedInstruction(false, 0xFFFFFFFBu);
            m_xgkick.active = false;
            return;
        }

        const uint32_t qwordOffset = m_xgkick.copiedBytes;
        for (uint32_t i = 0; i < 16u; ++i)
        {
            const uint32_t source = (m_xgkick.sourceAddress + m_xgkick.copiedBytes + i) % m_activeVuDataSize;
            m_xgkick.packet[m_xgkick.copiedBytes + i] = m_activeVuData[source];
        }
        m_xgkick.copiedBytes += 16u;

        if (m_xgkick.currentTagEnd == 0u)
        {
            uint64_t tagLo = 0;
            std::memcpy(&tagLo, m_xgkick.packet.data() + qwordOffset, sizeof(tagLo));
            const uint32_t nloop = static_cast<uint32_t>(tagLo & 0x7FFFu);
            const uint32_t format = static_cast<uint32_t>((tagLo >> 58) & 0x3u);
            uint32_t nreg = static_cast<uint32_t>((tagLo >> 60) & 0xFu);
            if (nreg == 0u)
                nreg = 16u;

            uint64_t tagBytes = 16u;
            if (format == 0u)
                tagBytes += static_cast<uint64_t>(nloop) * nreg * 16u;
            else if (format == 1u)
                tagBytes += ((static_cast<uint64_t>(nloop) * nreg + 1u) & ~1ull) * 8u;
            else if (format == 2u)
                tagBytes += static_cast<uint64_t>(nloop) * 16u;
            else
            {
                reportReservedInstruction(false, 0xFFFFFFF8u);
                m_xgkick.active = false;
                return;
            }

            if (tagBytes > XgkickPipeline::kBufferSize - qwordOffset)
            {
                reportReservedInstruction(false, 0xFFFFFFFBu);
                m_xgkick.active = false;
                return;
            }
            m_xgkick.currentTagEnd = qwordOffset + static_cast<uint32_t>(tagBytes);
            m_xgkick.currentTagEop = ((tagLo >> 15) & 1u) != 0u;
            if (m_xgkick.currentTagEop)
                m_xgkick.totalBytes = m_xgkick.currentTagEnd;
        }

        if (m_xgkick.copiedBytes >= m_xgkick.currentTagEnd)
        {
            if (m_xgkick.currentTagEop)
                finishXgkick();
            else
            {
                
                m_xgkick.currentTagEnd = 0u;
                m_xgkick.currentTagEop = false;
            }
        }
    }
}

void VUExecutor::finishXgkick()
{
    if (!m_xgkick.active)
        return;

    if (m_services)
    {
        if (!m_services->submitGif || !m_services->submitGif(m_services->context, m_xgkick.packet.data(), m_xgkick.totalBytes))
            throw std::runtime_error("VU GIF submission failed");
    }
    else if (m_activeMemory)
        m_activeMemory->submitGifPacket(GifPathId::Path1, m_xgkick.packet.data(), m_xgkick.totalBytes);
    else if (m_activeGs)
        m_activeGs->processGIFPacket(m_xgkick.packet.data(), m_xgkick.totalBytes, 1);
    m_xgkick.active = false;
}

void VUExecutor::startXgkick(uint32_t qwordAddress)
{
    if (m_unit != Unit::VU1 || !m_activeVuData || m_activeVuDataSize < 16u)
        return;

    const uint32_t sourceAddress = (qwordAddress * 16u) % m_activeVuDataSize;
    m_xgkick.reset();
    m_xgkick.active = true;
    m_xgkick.sourceAddress = sourceAddress;
    m_xgkick.cycleCredit = 1u; 
    m_xgkick.issueCycle = m_cycle;
}

void VUExecutor::advanceOneCycle()
{
    ++m_cycle;
    m_state.cycles = m_cycle;
    
    
    commitReadyPipelines();
    progressXgkick();
}

void VUExecutor::advanceTo(uint64_t targetCycle)
{
    while (m_cycle < targetCycle)
    {
        
        
        uint64_t next = std::min(targetCycle, m_nextPipelineCycle);
        if (m_xgkick.active)
            next = std::min(next, m_cycle + (2u - m_xgkick.cycleCredit));
        next = std::max(next, m_cycle + 1u);
        const uint64_t elapsed = next - m_cycle;
        m_cycle = next;
        m_state.cycles = m_cycle;
        commitReadyPipelines();
        if (m_xgkick.active)
            progressXgkick(static_cast<uint32_t>(elapsed));
    }
}

bool VUExecutor::pipelinesPending() const
{
    return m_nextPipelineCycle != kNoPipelineEvent || m_xgkick.active;
}

void VUExecutor::flushPipelines()
{
    while (pipelinesPending())
    {
        uint64_t next = m_nextPipelineCycle;
        if (m_xgkick.active)
            next = std::min(next, m_cycle + (2u - m_xgkick.cycleCredit));
        advanceTo(std::max(next, m_cycle + 1u));
    }
}

void VUExecutor::reportReservedInstruction(bool upper, uint32_t instruction)
{
    if (m_unit == Unit::VU1)
    {
        std::ostringstream message;
        message << "Reserved VU1 " << (upper ? "upper" : "lower") << " instruction at PC=0x" << std::hex << m_state.pc
                << " word=0x" << instruction;
        throw std::runtime_error(message.str());
    }

    RUNTIME_ERROR("[VU" << (m_unit == Unit::VU1 ? "1" : "0") << " reserved " << (upper ? "upper" : "lower")
                        << "] cycle=" << m_cycle << " pc=0x" << std::hex << m_state.pc << " instruction=0x"
                        << instruction << std::dec << '\n');
    m_stopRequested = true;
}

void VUExecutor::execute(const uint8_t *vuCode, uint32_t codeSize, uint8_t *vuData, uint32_t dataSize, GS &gs,
                         PS2Memory *memory, uint32_t startPC, uint32_t top, uint32_t itop, uint32_t maxCycles)
{
    resetScheduler();
    m_state.pc = startPC & microAddressMask();
    m_state.ebit = false;
    m_state.haltAfterDelaySlot = false;
    m_state.stoppedByD = false;
    m_state.stoppedByT = false;
    m_state.top = top;
    m_state.itop = itop;
    m_state.branchPending = false;
    m_state.branchTarget = 0;
    m_state.branchDelay = 0;
    m_state.vf[0][0] = 0.0f;
    m_state.vf[0][1] = 0.0f;
    m_state.vf[0][2] = 0.0f;
    m_state.vf[0][3] = 1.0f;
    run(vuCode, codeSize, vuData, dataSize, &gs, memory, maxCycles);
}

void VUExecutor::resume(const uint8_t *vuCode, uint32_t codeSize, uint8_t *vuData, uint32_t dataSize, GS &gs,
                        PS2Memory *memory, uint32_t top, uint32_t itop, uint32_t maxCycles)
{
    m_state.top = top;
    m_state.itop = itop;
    m_state.stoppedByD = false;
    m_state.stoppedByT = false;
    run(vuCode, codeSize, vuData, dataSize, &gs, memory, maxCycles);
}

void VUExecutor::run(const uint8_t *vuCode, uint32_t codeSize, uint8_t *vuData, uint32_t dataSize, GS *gs, PS2Memory *memory,
                     uint32_t maxCycles, bool drain)
{
    m_schedulerClean = false;
    m_activeVuData = vuData;
    m_activeVuDataSize = dataSize;
    m_activeGs = gs;
    m_activeMemory = memory;

    struct RoundingScope
    {
        int previous = std::fegetround();
        RoundingScope()
        {
            if (previous == -1 || std::fesetround(FE_TOWARDZERO) != 0)
                throw std::runtime_error("Cannot select VU floating-point rounding mode");
        }
        ~RoundingScope()
        {
            if (previous != -1)
                std::fesetround(previous);
        }
    } rounding;

    const uint64_t budgetEnd = m_cycle + maxCycles;
    m_programEnded = false;
    while (m_cycle < budgetEnd && !m_stopRequested)
    {
        if (m_state.pc + 8u > codeSize)
            break;
        uint32_t words[2];
        std::memcpy(words, vuCode + m_state.pc, sizeof(words));
        const auto decoded = decodeInstructionPair(words[0], words[1], m_unit == Unit::VU1);

        if (!issuePair(
                decoded, [&](float *vf, float *acc) { execUpper(decoded.upper, vf, acc); },
                [&] { execLower(decoded.lower, vuData, dataSize, decoded.upper); }, codeSize, budgetEnd))
            break;
    }

    if (m_programEnded && drain)
    {
        flushPipelines();
        m_state.ebit = false;
        m_state.haltAfterDelaySlot = false;
        m_pendingHaltD = false;
        m_pendingHaltT = false;
    }
    m_state.cycles = m_cycle;
}
