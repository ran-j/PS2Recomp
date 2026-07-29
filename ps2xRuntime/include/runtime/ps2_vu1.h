#ifndef PS2_VU1_H
#define PS2_VU1_H

#include <cstdint>
#include <vector>

class GS;
class PS2Memory;

struct VU1State
{
    float vf[32][4];
    int32_t vi[16];
    float acc[4];
    float q;
    float p;
    float i;
    uint32_t pc;
    uint32_t mac;
    uint32_t clip;
    uint32_t status;
    bool ebit;
    uint32_t top;  // VIF1 TOP visible to VU1 XTOP
    uint32_t itop; // VIF1 ITOP visible to VU1 XITOP

    bool branchPending;
    uint32_t branchTarget;
    uint32_t branchDelay;
};

class VU1Interpreter
{
public:
    enum class StopReason : uint8_t
    {
        None,
        EndBit,
        PcOutOfRange,
        CycleLimit
    };

    VU1Interpreter();

    void reset();

    void execute(uint8_t *vuCode, uint32_t codeSize,
                 uint8_t *vuData, uint32_t dataSize,
                 GS &gs, PS2Memory *memory = nullptr,
                 uint32_t startPC = 0, uint32_t top = 0, uint32_t itop = 0,
                 uint32_t maxCycles = 65536);

    void resume(uint8_t *vuCode, uint32_t codeSize,
                uint8_t *vuData, uint32_t dataSize,
                GS &gs, PS2Memory *memory = nullptr,
                uint32_t top = 0, uint32_t itop = 0, uint32_t maxCycles = 65536);

    VU1State &state() { return m_state; }
    const VU1State &state() const { return m_state; }
    uint64_t runCount() const { return m_runCount; }
    uint64_t instructionPairCount() const { return m_instructionPairCount; }
    uint64_t xgkickCount() const { return m_xgkickCount; }
    StopReason lastStopReason() const { return m_lastStopReason; }
    uint32_t lastExecutedPc() const { return m_lastExecutedPc; }
    uint32_t lastLowerInstruction() const { return m_lastLowerInstruction; }
    uint32_t lastUpperInstruction() const { return m_lastUpperInstruction; }
    uint64_t unsupportedUpperCount() const { return m_unsupportedUpperCount; }
    uint64_t unsupportedLowerCount() const { return m_unsupportedLowerCount; }
    uint32_t lastUnsupportedUpper() const { return m_lastUnsupportedUpper; }
    uint32_t lastUnsupportedLower() const { return m_lastUnsupportedLower; }

private:
    struct DecodedInstructionPair
    {
        uint32_t lower = 0;
        uint32_t upper = 0;
        bool iBit = false;
        bool eBit = false;
        bool lowerBeforeUpper = false;
    };

    VU1State m_state;
    std::vector<DecodedInstructionPair> m_decodedCodeCache;
    const uint8_t *m_cachedVuCode = nullptr;
    const PS2Memory *m_cachedMemory = nullptr;
    uint32_t m_cachedCodeSize = 0;
    uint64_t m_cachedCodeGeneration = 0;
    bool m_decodedCodeCacheValid = false;
    uint64_t m_runCount = 0;
    uint64_t m_instructionPairCount = 0;
    uint64_t m_xgkickCount = 0;
    StopReason m_lastStopReason = StopReason::None;
    uint32_t m_lastExecutedPc = 0;
    uint32_t m_lastLowerInstruction = 0;
    uint32_t m_lastUpperInstruction = 0;
    uint64_t m_unsupportedUpperCount = 0;
    uint64_t m_unsupportedLowerCount = 0;
    uint32_t m_lastUnsupportedUpper = 0;
    uint32_t m_lastUnsupportedLower = 0;

    void run(uint8_t *vuCode, uint32_t codeSize,
             uint8_t *vuData, uint32_t dataSize,
             GS &gs, PS2Memory *memory, uint32_t maxCycles);

    DecodedInstructionPair decodeInstructionPair(const uint8_t *vuCode, uint32_t pc) const;
    DecodedInstructionPair getDecodedInstructionPairForPc(const uint8_t *vuCode, uint32_t codeSize,
                                                          PS2Memory *memory, uint32_t pc);
    void rebuildDecodedCodeCache(const uint8_t *vuCode, uint32_t codeSize,
                                 const PS2Memory *memory, uint64_t generation);

    void execUpper(uint32_t instr);
    void execLower(uint32_t instr, uint8_t *vuData, uint32_t dataSize, GS &gs, PS2Memory *memory, uint32_t upperInstr);
    void recordUnsupportedUpper(uint32_t instr);
    void recordUnsupportedLower(uint32_t instr);

    void applyDest(float *dst, const float *result, uint8_t dest);
    void applyDestAcc(const float *result, uint8_t dest);
    float broadcast(const float *vf, uint8_t bc);
};

#endif
