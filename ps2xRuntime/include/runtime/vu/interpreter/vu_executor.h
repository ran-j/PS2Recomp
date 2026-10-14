#ifndef PS2_VU_EXECUTOR_H
#define PS2_VU_EXECUTOR_H

#include "runtime/vu/vu_types.h"
#include "runtime/vu/vu_state.h"
#include "runtime/vu/vu_pipeline_events.h"
#include <memory>
#include <array>
#include <bit>
#include <cstddef>
#include <limits>
#include <cstdint>

class GS;
class PS2Memory;
namespace ps2vu
{
    class Unit;
    struct MemoryServices;
}

class VUExecutor
{
public:
    enum class Unit : uint8_t
    {
        VU0,
        VU1
    };

    explicit VUExecutor(Unit unit = Unit::VU1);
    ~VUExecutor();

    void reset();

    void execute(const uint8_t *vuCode, uint32_t codeSize, uint8_t *vuData, uint32_t dataSize, GS &gs,
                 PS2Memory *memory = nullptr, uint32_t startPC = 0, uint32_t top = 0, uint32_t itop = 0,
                 uint32_t maxCycles = 65536);

    void resume(const uint8_t *vuCode, uint32_t codeSize, uint8_t *vuData, uint32_t dataSize, GS &gs,
                PS2Memory *memory = nullptr, uint32_t top = 0, uint32_t itop = 0, uint32_t maxCycles = 65536);

    VU1State &state()
    {
        return m_state;
    }
    const VU1State &state() const
    {
        return m_state;
    }

private:
    friend class ps2vu::Unit;
    ps2vu::MemoryServices *m_services = nullptr;
    void readData(uint32_t address, void *words);
    void writeData(uint32_t address, const uint32_t *words, uint8_t lanes);
    using InstructionUsage = ps2vu::InstructionUsage;
    using DecodedInstructionPair = ps2vu::DecodedInstructionPair;
    using VfAccess = ps2vu::VfAccess;
    using enum ps2vu::Pipeline;
    static constexpr auto kRegisterReadyCount = ps2vu::kRegisterReadyCount;
    static constexpr auto kViReadyBase = ps2vu::kViReadyBase;
    static constexpr auto kAccReadyBase = ps2vu::kAccReadyBase;

    struct FlagPipelineEntry
    {
        uint64_t issueCycle = 0;
        uint32_t mac = 0;
        uint32_t status = 0;
        uint32_t extraSticky = 0;
        uint32_t clip = 0;
        bool valid = false;
        bool writesMac = false;
        bool writesStatus = false;
        bool writesSticky = false;
        bool writesClip = false;
    };

    struct ScalarPipelineEntry
    {
        uint64_t readyCycle = 0;
        float value = 0.0f;
        uint32_t statusDi = 0;
        bool valid = false;
    };

    struct PendingStore
    {
        uint32_t address = 0;
        std::array<uint32_t, 4> words{};
        uint8_t laneMask = 0;
        bool valid = false;
    };

    struct PendingVfWrite
    {
        uint64_t sequence = 0;
        std::array<float, 4> value{};
        uint8_t reg = 0;
        uint8_t laneMask = 0;
        bool valid = false;
    };

    struct PendingViWrite
    {
        uint64_t sequence = 0;
        int32_t value = 0;
        uint8_t reg = 0;
        bool valid = false;
    };

    struct PendingAccWrite
    {
        uint64_t sequence = 0;
        std::array<float, 4> value{};
        uint8_t laneMask = 0;
        bool valid = false;
    };

    struct XgkickPipeline
    {
        static constexpr uint32_t kBufferSize = 0x10000u;
        std::array<uint8_t, kBufferSize> packet{};
        uint32_t sourceAddress = 0;
        uint32_t totalBytes = 0;
        uint32_t copiedBytes = 0;
        uint32_t currentTagEnd = 0;
        uint32_t cycleCredit = 0;
        uint64_t issueCycle = 0;
        bool active = false;
        bool currentTagEop = false;

        void reset()
        {
            sourceAddress = totalBytes = copiedBytes = currentTagEnd = cycleCredit = 0;
            issueCycle = 0;
            active = currentTagEop = false;
        }
    };

    static constexpr uint32_t kFmacLatency = 4u;
    static constexpr uint32_t kAccForwardLatency = 1u;
    static constexpr uint32_t kMaxFlagEntries = ps2vu::PipelineEvents::capacity(ps2vu::WritebackKind::Flag);
    static constexpr uint32_t kMaxPendingStores = ps2vu::PipelineEvents::capacity(ps2vu::WritebackKind::Store);
    static constexpr uint32_t kMaxPendingVfWrites = ps2vu::PipelineEvents::capacity(ps2vu::WritebackKind::Vf);
    static constexpr uint32_t kMaxPendingViWrites = ps2vu::PipelineEvents::capacity(ps2vu::WritebackKind::Vi);
    static constexpr uint32_t kMaxPendingAccWrites = ps2vu::PipelineEvents::capacity(ps2vu::WritebackKind::Acc);

    bool m_programEnded = false;
    Unit m_unit;
    VU1State m_state;

    std::array<FlagPipelineEntry, kMaxFlagEntries> m_flagPipeline{};
    ScalarPipelineEntry m_fdiv{};
    std::array<ScalarPipelineEntry, ps2vu::PipelineEvents::capacity(ps2vu::WritebackKind::Efu)> m_efu{};
    std::array<PendingStore, kMaxPendingStores> m_storePipeline{};
    std::array<PendingVfWrite, kMaxPendingVfWrites> m_vfWritePipeline{};
    std::array<PendingViWrite, kMaxPendingViWrites> m_viWritePipeline{};
    std::array<PendingAccWrite, kMaxPendingAccWrites> m_accWritePipeline{};

    uint32_t m_flagActive = 0;
    uint32_t m_efuActive = 0;
    uint32_t m_storeActive = 0;
    uint32_t m_vfWriteActive = 0;
    uint32_t m_viWriteActive = 0;
    uint32_t m_accWriteActive = 0;

    static constexpr uint64_t kNoPipelineEvent = std::numeric_limits<uint64_t>::max();
    uint64_t m_nextPipelineCycle = kNoPipelineEvent;
    ps2vu::PipelineEvents m_pipelineEvents;
    bool m_schedulerClean = true;

    XgkickPipeline m_xgkick{};

    std::array<uint64_t, kRegisterReadyCount> m_registerReady{};
    std::array<std::array<uint64_t, 4>, 32> m_vfCommittedWrite{};
    std::array<uint64_t, 16> m_viCommittedWrite{};
    std::array<uint64_t, 4> m_accLatestWrite{};

    uint64_t m_cycle = 0;
    uint64_t m_nextWriteSequence = 0;
    uint64_t m_efuResourceReady = 0;
    uint32_t m_workingClip = 0;
    struct UpperOperands
    {
        float vs[4], vt[4], acc[4], q, i;
    } m_upperOperands{};
    int32_t m_viBranchBackupValue = 0;
    uint8_t m_viBranchBackupReg = 0;
    bool m_viBranchBackupValid = false;
    uint8_t *m_activeVuData = nullptr;
    uint32_t m_activeVuDataSize = 0;
    GS *m_activeGs = nullptr;
    PS2Memory *m_activeMemory = nullptr;
    bool m_stopRequested = false;
    bool m_pendingHaltD = false;
    bool m_pendingHaltT = false;

    template <class Upper, class Lower>
    bool issuePair(const DecodedInstructionPair &decoded,
                   Upper upper,
                   Lower lower,
                   uint32_t codeSize,
                   uint64_t budgetEnd);

    void run(const uint8_t *vuCode,
             uint32_t codeSize,
             uint8_t *vuData,
             uint32_t dataSize,
             GS *gs,
             PS2Memory *memory,
             uint32_t maxCycles,
             bool drain = true);

    template <class Word>
    void execUpper(Word instr, float *vfResult, float *accResult);
    template <class Word>
    void execLower(Word instr, uint8_t *vuData, uint32_t dataSize, uint32_t upperInstr);

    void applyDest(float *dst, const float *result, uint8_t dest);
    template <class Word>
    void applyFmacDest(Word instruction, float *dst, float *result, uint8_t dest);
    template <class Word>
    void normalizeFmacResult(Word instruction, float *result, uint8_t dest, uint8_t laneFlags[4]);
    template <class Word>
    bool calculateFmacExactResult(Word instruction, uint32_t component, long double &result) const;
    uint8_t normalizeFmacExactResult(float &value, long double exactResult) const;
    template <class Word>
    uint32_t calculateFmacProductSticky(Word instruction, uint8_t dest) const;
    void updateFmacFlags(const uint8_t laneFlags[4], uint8_t dest, uint32_t extraSticky);
    void queueFsset(uint16_t immediate);
    void queueClip(uint32_t clip);
    void queueFcset(uint32_t clip);
    void queueQ(float value, uint32_t latency, uint32_t statusDi);
    void queueP(float value, uint32_t latency);
    void queueStore(uint32_t address, const uint32_t words[4], uint8_t laneMask);
    void queueVfWrite(uint8_t reg, uint8_t laneMask, const float value[4], uint32_t latency);
    void queueViWrite(uint8_t reg, int32_t value, uint32_t latency);
    void queueAccWrite(uint8_t laneMask, const float value[4], uint32_t latency);
    void startXgkick(uint32_t qwordAddress);

    template <ps2vu::WritebackKind Kind, typename Entry, std::size_t Capacity>
    Entry *allocatePipelineEntry(std::array<Entry, Capacity> &entries, uint32_t &active, uint64_t readyCycle)
    {
        static_assert(Capacity == ps2vu::PipelineEvents::capacity(Kind));
        const uint32_t slot = std::countr_zero(~active);
        if (slot >= Capacity)
            return nullptr;
        m_pipelineEvents.schedule<Kind>(m_cycle, static_cast<uint32_t>(readyCycle - m_cycle), slot);
        active |= 1u << slot;
        Entry &entry = entries[slot];
        if constexpr (Kind == ps2vu::WritebackKind::Flag)
        {
            entry.valid = true;
            entry.writesMac = entry.writesStatus = entry.writesSticky = entry.writesClip = false;
        }
        else if constexpr (Kind == ps2vu::WritebackKind::Efu)
        {
            entry.valid = true;
            entry.readyCycle = readyCycle;
        }
        if (readyCycle < m_nextPipelineCycle)
            m_nextPipelineCycle = readyCycle;
        return &entry;
    }

    void resetScheduler();
    void commitReadyPipelines();
    void advanceOneCycle();
    void advanceTo(uint64_t targetCycle);
    void flushPipelines();
    void progressXgkick(uint32_t elapsedCycles = 1u);
    void finishXgkick();
    uint64_t calculatePairReadyCycle(const DecodedInstructionPair &decoded) const;
    void markPairWrites(const DecodedInstructionPair &decoded);
    bool pipelinesPending() const;

    static float normalizeOperand(float value)
    {
        uint32_t bits = std::bit_cast<uint32_t>(value);
        const uint32_t exponent = bits & 0x7F800000u;
        if (exponent == 0u)
            bits &= 0x80000000u;
        else if (exponent == 0x7F800000u)
            bits = (bits & 0x80000000u) | 0x7F7FFFFFu;
        return std::bit_cast<float>(bits);
    }
    float normalizeResult(float value, uint32_t &laneFlags) const;
    uint32_t microAddressMask() const;
    int32_t readBranchVi(uint8_t reg) const;
    void recordViWriteForBranch(uint8_t reg, int32_t oldValue);
    void reportReservedInstruction(bool upper, uint32_t instruction);
};

#endif
