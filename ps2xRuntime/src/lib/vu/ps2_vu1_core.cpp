#include "ps2_vu1_exec.inl"
#include "runtime/ps2_vu1.h"
#include "runtime/gs/ps2_gif_arbiter.h"
#include "runtime/gs/gs_frontend.h"
#include "runtime/ps2_memory.h"
#include "ps2_vu1_detail.h"
#include "ps2_vu1_capture.h"

#include <algorithm>
#include <bit>
#include <cfenv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <tuple>
#include <ps2_log.h>

namespace
{
    void recordUncompiledBlock(const char *path, unsigned unit, const uint8_t *code)
    {
        static std::mutex mutex;
        const std::lock_guard lock(mutex);
        static std::ofstream output(path);
        static std::set<std::pair<unsigned, std::array<uint64_t, 4>>> recorded;
        if (!output)
            return;
        if (recorded.empty())
            output << "VU-BLOCKS 1\n";
        std::array<uint64_t, 4> words;
        std::memcpy(words.data(), code, sizeof(words));
        if (!recorded.emplace(unit, words).second)
            return;
        output << std::dec << unit;
        for (const auto word : words)
            output << ' ' << std::hex << word;
        output << '\n';
        output.flush();
    }

}

VU1Interpreter::VU1Interpreter(Unit unit)
    : m_unit(unit)
{
    const char *execution = std::getenv("PS2_VU_EXECUTION");
    m_useCompiledExecution = execution == nullptr || std::strcmp(execution, "interpreter") != 0;
    reset();
}

void VU1Interpreter::resetScheduler()
{
    m_flagPipeline = {};
    m_fdiv = {};
    m_efu = {};
    m_storePipeline = {};
    m_vfWritePipeline = {};
    m_viWritePipeline = {};
    m_accWritePipeline = {};
    m_activeFlags = m_activeStores = m_activeVfWrites = m_activeViWrites = m_activeAccWrites = 0u;
    m_xgkick = {};
    m_vfReady = {};
    m_viReady = {};
    m_accReady = {};
    m_vfLatestWrite = {};
    m_viLatestWrite = {};
    m_accLatestWrite = {};
    m_nextWriteSequence = 0;
    m_efuResourceReady = 0;
    m_workingClip = m_state.clip;
    m_viBranchBackupValue = 0;
    m_viBranchBackupReg = 0;
    m_viBranchBackupValid = false;
    m_stopRequested = false;
    m_pendingHaltD = false;
    m_pendingHaltT = false;
}

void VU1Interpreter::reset()
{
    std::memset(&m_state, 0, sizeof(m_state));
    m_state.vf[0][3] = 1.0f;
    m_state.q = 1.0f;
    m_state.r = 0x3F800000u;
    m_cycle = 0;
    resetScheduler();
}

void VU1Interpreter::progressXgkick()
{
    if (!m_xgkick.active || !m_activeVuData || m_activeVuDataSize == 0u)
        return;

    ++m_xgkick.cycleCredit;
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
        const uint32_t source = (m_xgkick.sourceAddress + qwordOffset) % m_activeVuDataSize;
        const uint32_t firstBytes = std::min(16u, m_activeVuDataSize - source);
        std::memcpy(m_xgkick.packet.data() + qwordOffset, m_activeVuData + source, firstBytes);
        if (firstBytes < 16u)
            std::memcpy(m_xgkick.packet.data() + qwordOffset + firstBytes,
                        m_activeVuData, 16u - firstBytes);
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
                // The next transferred qword is another GIFtag.
                m_xgkick.currentTagEnd = 0u;
                m_xgkick.currentTagEop = false;
            }
        }
    }
}

void VU1Interpreter::finishXgkick()
{
    if (!m_xgkick.active)
        return;

    if (m_activeMemory)
        m_activeMemory->submitGifPacket(GifPathId::Path1, m_xgkick.packet.data(), m_xgkick.totalBytes);
    else if (m_activeGs)
        m_activeGs->processGIFPacket(m_xgkick.packet.data(), m_xgkick.totalBytes);
    m_xgkick.active = false;
}

void VU1Interpreter::startXgkick(uint32_t qwordAddress)
{
    if (m_unit != Unit::VU1 || !m_activeVuData || m_activeVuDataSize < 16u)
        return;

    const uint32_t sourceAddress = (qwordAddress * 16u) % m_activeVuDataSize;
    // Every submitted byte is copied before use; clearing the packet buffer
    // on each kick adds 64 KiB of unrelated writes.
    m_xgkick.totalBytes = 0;
    m_xgkick.copiedBytes = 0;
    m_xgkick.currentTagEnd = 0;
    m_xgkick.currentTagEop = false;
    m_xgkick.active = true;
    m_xgkick.sourceAddress = sourceAddress;
    m_xgkick.cycleCredit = 1u; // XGKICK's issue cycle counts toward PATH1.
    m_xgkick.issueCycle = m_cycle;
}

bool VU1Interpreter::pipelinesPending() const
{
    if (m_fdiv.valid || m_xgkick.active)
        return true;
    for (const ScalarPipelineEntry &entry : m_efu)
        if (entry.valid)
            return true;
    return (m_activeFlags | m_activeStores | m_activeVfWrites | m_activeViWrites | m_activeAccWrites) != 0u;
}

void VU1Interpreter::flushPipelines()
{
    while (pipelinesPending())
        advanceOneCycle();
}

uint64_t VU1Interpreter::calculatePairReadyCycle(const DecodedInstructionPair &decoded) const
{
    return calculatePairReadyCycleInline(decoded);
}

void VU1Interpreter::markPairWrites(const DecodedInstructionPair &decoded)
{
    markPairWritesInline(decoded);
}

VU1Interpreter::DecodedInstructionPair VU1Interpreter::decodeInstructionPair(const uint8_t *vuCode, uint32_t pc) const
{
    uint32_t lower, upper;
    std::memcpy(&lower, vuCode + pc, sizeof(lower));
    std::memcpy(&upper, vuCode + pc + sizeof(lower), sizeof(upper));
    return decodeInstructionWords(lower, upper, m_unit);
}

VU1Interpreter::DecodedInstructionPair VU1Interpreter::getDecodedInstructionPairForPc(
    const uint8_t *vuCode, uint32_t codeSize, PS2Memory *memory, uint32_t pc)
{
    if ((pc & 7u) != 0u)
        return decodeInstructionPair(vuCode, pc);

    const bool trackedVu1Code = memory != nullptr &&
                                ((m_unit == Unit::VU1 && vuCode == memory->getVU1Code()) ||
                                 (m_unit == Unit::VU0 && vuCode == memory->getVU0Code()));
    if (!trackedVu1Code)
        return decodeInstructionPair(vuCode, pc);

    const uint64_t generation = m_unit == Unit::VU1 ? memory->getVU1CodeGeneration() : memory->getVU0CodeGeneration();
    if (!m_decodedCodeCacheValid ||
        m_cachedVuCode != vuCode ||
        m_cachedMemory != memory ||
        m_cachedCodeSize != codeSize ||
        m_cachedCodeGeneration != generation)
    {
        rebuildDecodedCodeCache(vuCode, codeSize, memory, generation);
    }
    const uint32_t pairIndex = pc / 8u;
    if (pairIndex >= kMaxDecodedPairs)
        return decodeInstructionPair(vuCode, pc);
    return m_decodedCodeCache[pairIndex];
}

void VU1Interpreter::reportReservedInstruction(bool upper, uint32_t instruction)
{
    RUNTIME_ERROR(
        "[VU" << (m_unit == Unit::VU1 ? "1" : "0")
              << " reserved " << (upper ? "upper" : "lower")
              << "] cycle=" << m_cycle
              << " pc=0x" << std::hex << m_state.pc
              << " instruction=0x" << instruction
              << std::dec << '\n');
    m_stopRequested = true;
}

void VU1Interpreter::execute(uint8_t *vuCode, uint32_t codeSize,
                             uint8_t *vuData, uint32_t dataSize,
                             GS &gs, PS2Memory *memory,
                             uint32_t startPC, uint32_t top, uint32_t itop,
                             uint32_t maxCycles)
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
    static const char *captureDirectory = std::getenv("PS2_VU_CAPTURE_DIR");
    if (captureDirectory && *captureDirectory && memory) {
        // execute() has an empty pipeline; resumed states need a fuller snapshot.
        const uint64_t generation = m_unit == Unit::VU1
            ? memory->getVU1CodeGeneration() : memory->getVU0CodeGeneration();
        ps2_vu_detail::captureProgramStart(captureDirectory, m_unit == Unit::VU1 ? 1u : 0u,
                            vuCode, codeSize, generation, vuData, dataSize, m_state);
    }
    run(vuCode, codeSize, vuData, dataSize, gs, memory, maxCycles);
}

void VU1Interpreter::resume(uint8_t *vuCode, uint32_t codeSize,
                            uint8_t *vuData, uint32_t dataSize,
                            GS &gs, PS2Memory *memory,
                            uint32_t top, uint32_t itop, uint32_t maxCycles)
{
    m_state.top = top;
    m_state.itop = itop;
    m_state.stoppedByD = false;
    m_state.stoppedByT = false;
    static const char *captureDirectory = std::getenv("PS2_VU_CAPTURE_DIR");
    // A drained continuation has no hidden writes or branch forwarding to serialize.
    if (captureDirectory && *captureDirectory && memory && !pipelinesPending() &&
        !m_viBranchBackupValid && !m_state.branchPending && !m_stopRequested)
        ps2_vu_detail::captureProgramStart(captureDirectory, m_unit == Unit::VU1 ? 1u : 0u,
            vuCode, codeSize, m_unit == Unit::VU1 ? memory->getVU1CodeGeneration() : memory->getVU0CodeGeneration(),
            vuData, dataSize, m_state);
    run(vuCode, codeSize, vuData, dataSize, gs, memory, maxCycles);
}

#ifdef PS2X_VU_AOT_EXTERN_INCLUDE
#include PS2X_VU_AOT_EXTERN_INCLUDE
#endif

VU1Interpreter::CompiledBlock VU1Interpreter::findCompiledBlock(
    const uint8_t *code, uint32_t size, Unit unit)
{
#ifdef PS2X_VU_AOT_INCLUDE
    struct Entry {
        Unit unit;
        std::array<uint64_t, 4> words;
        CompiledBlock run;
        uint32_t bytes;
    };
    static const Entry entries[] = {
#include PS2X_VU_AOT_INCLUDE
    };
    if (size < 8u)
        return nullptr;
    static const bool pairsOnly = [] {
        const char *mode = std::getenv("PS2_VU_EXECUTION");
        return mode && std::strcmp(mode, "pairs") == 0;
    }();
    uint64_t first;
    std::memcpy(&first, code, sizeof(first));
    const auto *entry = std::lower_bound(std::begin(entries), std::end(entries), first,
        [](const Entry &entry, uint64_t word) { return entry.words[0] < word; });
    CompiledBlock pair = nullptr;
    for (; entry != std::end(entries) && entry->words[0] == first; ++entry) {
        if (entry->unit != unit)
            continue;
        if (entry->bytes == 8u)
            pair = entry->run;
        else if (!pairsOnly && size >= entry->bytes &&
                 std::memcmp(code, entry->words.data(), entry->bytes) == 0)
            return entry->run;
    }
    return pair;
#endif
    return nullptr;
}

void VU1Interpreter::run(uint8_t *vuCode, uint32_t codeSize,
                         uint8_t *vuData, uint32_t dataSize,
                         GS &gs, PS2Memory *memory, uint32_t maxCycles)
{
    static const bool profileExecution = std::getenv("PS2_VU_PROFILE_EXECUTION") != nullptr;
    static const char *blockProfile = std::getenv("PS2_VU_AOT_PROFILE");
    const uint64_t pairsBefore = m_compiledPairsExecuted + m_interpretedPairsExecuted;
    m_activeVuData = vuData;
    m_activeVuDataSize = dataSize;
    m_activeGs = &gs;
    m_activeMemory = memory;

    const int previousRoundingMode = std::fegetround();
    const bool useVuRounding = std::fesetround(FE_TOWARDZERO) == 0;
    const uint64_t budgetEnd = m_cycle + maxCycles;
    bool programEnded = false;
    const bool trackedCode = memory != nullptr && codeSize <= kMaxDecodedPairs * 8u &&
        vuCode == (m_unit == Unit::VU1 ? memory->getVU1Code() : memory->getVU0Code());
    if (trackedCode && (!m_decodedCodeCacheValid || m_cachedVuCode != vuCode ||
                        m_cachedMemory != memory || m_cachedCodeSize != codeSize))
        rebuildDecodedCodeCache(vuCode, codeSize, memory,
            m_unit == Unit::VU1 ? memory->getVU1CodeGeneration() : memory->getVU0CodeGeneration());
    commitReadyPipelines();
    while (m_cycle < budgetEnd && !m_stopRequested)
    {
        // advanceOneCycle already committed this boundary, including stalls.
        if (m_state.pc + 8u > codeSize)
            break;

        DecodedInstructionPair uncached;
        const DecodedInstructionPair *pair = nullptr;
        if (trackedCode && (m_state.pc & 7u) == 0u)
        {
            const uint64_t generation = m_unit == Unit::VU1
                ? memory->getVU1CodeGeneration() : memory->getVU0CodeGeneration();
            if (generation != m_cachedCodeGeneration)
                rebuildDecodedCodeCache(vuCode, codeSize, memory, generation);
            pair = &m_decodedCodeCache[m_state.pc / 8u];
        }
        else
        {
            uncached = getDecodedInstructionPairForPc(vuCode, codeSize, memory, m_state.pc);
            pair = &uncached;
        }
        const DecodedInstructionPair &decoded = *pair;
        const auto compiled = trackedCode && (m_state.pc & 7u) == 0u && m_useCompiledExecution
            ? m_compiledCodeCache[m_state.pc / 8u] : nullptr;
        if (compiled)
            programEnded = compiled(*this, budgetEnd);
        else
        {
            if (blockProfile && m_state.pc + 32ull <= codeSize)
                recordUncompiledBlock(blockProfile, m_unit == Unit::VU1 ? 1u : 0u,
                                      vuCode + m_state.pc);
            programEnded = runDecodedPair<false>(decoded, budgetEnd, codeSize);
        }
        if (programEnded)
            break;
    }

    if (programEnded)
    {
        flushPipelines();
        m_state.ebit = false;
        m_state.haltAfterDelaySlot = false;
        m_pendingHaltD = false;
        m_pendingHaltT = false;
    }
    m_state.cycles = m_cycle;
    if (useVuRounding && previousRoundingMode != -1)
        std::fesetround(previousRoundingMode);
    const uint64_t pairsAfter = m_compiledPairsExecuted + m_interpretedPairsExecuted;
    if (profileExecution && (pairsBefore >> 24u) != (pairsAfter >> 24u))
        std::fprintf(stderr, "[VU%u execution] native=%llu fallback=%llu (%.1f%% native)\n",
                     m_unit == Unit::VU1 ? 1u : 0u,
                     static_cast<unsigned long long>(m_compiledPairsExecuted),
                     static_cast<unsigned long long>(m_interpretedPairsExecuted),
                     100.0 * m_compiledPairsExecuted / pairsAfter);
}
