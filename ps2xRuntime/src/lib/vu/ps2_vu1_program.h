#ifndef PS2_VU1_PROGRAM_H
#define PS2_VU1_PROGRAM_H

// VU1 programs compiled whole, MSCAL entry to E bit: pairs issue in order under
// the interpreter's stall rules, results land at issue, and whatever is still
// in flight on return goes back into the interpreter's pipelines.

#include "ps2_vu1_exec.inl"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>

// NEON fast paths. Defining PS2X_VU_PROGRAM_PORTABLE runs the path other
// hosts take, so it can be tested on an ARM machine.
#if defined(__aarch64__) && !defined(PS2X_VU_PROGRAM_PORTABLE)
#define PS2_VU_PROGRAM_NEON 1
#include <arm_neon.h>
#else
#define PS2_VU_PROGRAM_NEON 0
#endif

namespace ps2_vu_program
{
using Unit = VU1Interpreter::Unit;
class Run;

// A routine is the code reachable from one entry without following calls or
// register jumps. It returns the PC to continue at; the driver finds the
// routine for that PC or hands the rest back to the interpreter.
using Routine = uint32_t (*)(Run &, uint64_t budgetEnd);

struct Entry
{
    uint32_t pc;
    uint32_t footprintPairs;
    const uint16_t *footprint; // pair indices the routine was built from
    const uint64_t *words;     // their instruction words, in the same order
    Routine run;
};

// Programs are entered only when the budget clearly outlasts them, so a
// cycle budget does not end one part-way in ordinary use.
inline constexpr uint64_t kMinimumBudget = 4096u;

Routine find(const uint8_t *code, uint32_t codeSize, uint32_t pc, uint64_t generation);

// find() remembers its answers by code address and generation, which a new
// interpreter or memory can repeat; a VU1 reset starts the answers over.
void forgetRoutines();

// With PS2_VU_PROGRAM_PROFILE set, save the microcode image and entry of a
// routine that has no compiled version, for compile_vu_programs.py.
void recordMissing(const uint8_t *code, uint32_t codeSize, uint32_t pc);

// Run from the current PC, starting with First. Returns true when the E-bit
// delay slot issued, false when the interpreter has to continue.
bool run(VU1Interpreter &vu, const uint8_t *code, uint32_t codeSize, uint64_t budgetEnd, Routine first);

struct Access
{
    using Decoded = VU1Interpreter::DecodedInstructionPair;
    using Usage = VU1Interpreter::InstructionUsage;
    using ScalarEntry = VU1Interpreter::ScalarPipelineEntry;
    static constexpr uint8_t kFdiv = VU1Interpreter::PipelineFdiv;
    static constexpr uint8_t kEfu = VU1Interpreter::PipelineEfu;
    static constexpr uint8_t kXgkick = VU1Interpreter::PipelineXgkick;
    static constexpr uint8_t kBranch = VU1Interpreter::PipelineBranch;

    static constexpr Decoded decode(uint32_t lower, uint32_t upper)
    {
        return VU1Interpreter::decodeInstructionWords(lower, upper, Unit::VU1);
    }

    static VU1State &state(VU1Interpreter &vu) { return vu.m_state; }
    static uint64_t cycle(const VU1Interpreter &vu) { return vu.m_cycle; }
    static void setCycle(VU1Interpreter &vu, uint64_t cycle) { vu.m_cycle = cycle; }
    static uint8_t *data(VU1Interpreter &vu) { return vu.m_activeVuData; }
    static bool xgkickActive(const VU1Interpreter &vu) { return vu.m_xgkick.active; }
    static void startXgkick(VU1Interpreter &vu, uint32_t address) { vu.startXgkick(address); }
    static void progressXgkick(VU1Interpreter &vu) { vu.progressXgkick(); }
    static ScalarEntry &fdiv(VU1Interpreter &vu) { return vu.m_fdiv; }
    static std::array<ScalarEntry, 2> &efu(VU1Interpreter &vu) { return vu.m_efu; }
    static uint64_t efuResourceReady(const VU1Interpreter &vu) { return vu.m_efuResourceReady; }
    static void queueQ(VU1Interpreter &vu, float value, uint32_t latency, uint32_t statusDi)
    {
        vu.queueQ(value, latency, statusDi);
    }
    static void queueP(VU1Interpreter &vu, float value, uint32_t latency) { vu.queueP(value, latency); }
    static uint64_t codeGeneration(const VU1Interpreter &vu)
    {
        return vu.m_activeMemory ? vu.m_activeMemory->getVU1CodeGeneration() : 0u;
    }
    static uint32_t workingClip(const VU1Interpreter &vu) { return vu.m_workingClip; }

    // Nothing issued earlier can still change state or stall a pair.
    static bool drained(const VU1Interpreter &vu)
    {
        return vu.m_unit == Unit::VU1 && vu.m_useCompiledExecution && !vu.pipelinesPending() &&
               !vu.m_state.branchPending && !vu.m_state.ebit && !vu.m_state.haltAfterDelaySlot &&
               !vu.m_stopRequested && !vu.m_pendingHaltD && !vu.m_pendingHaltT &&
               !vu.m_state.dBitEnabled && !vu.m_state.tBitEnabled && vu.m_activeVuData &&
               vu.m_activeVuDataSize == 16384u && vu.m_activeMemory;
    }

    static int32_t branchBackup(const VU1Interpreter &vu, uint32_t &reg)
    {
        reg = vu.m_viBranchBackupValid ? vu.m_viBranchBackupReg : 0u;
        return vu.m_viBranchBackupValue;
    }

    static void pendVf(VU1Interpreter &vu, uint8_t reg, uint8_t lanes, uint64_t ready);
    static void pendVi(VU1Interpreter &vu, uint8_t reg, uint64_t ready);
    static void pendFlags(VU1Interpreter &vu, uint64_t issue, uint32_t mac, uint32_t status,
                          uint32_t sticky, uint32_t clip, uint8_t writes);
    static void finish(VU1Interpreter &vu, uint64_t cycle, uint32_t workingClip, uint8_t backupReg,
                       int32_t backupValue, uint64_t pairs);
};

inline constexpr uint8_t lane(uint32_t component) { return static_cast<uint8_t>(8u >> component); }

// A block's pairs, which run straight through from pc. Generated code names
// a pair by its block and position, so each pair can see the ones before it.
template <size_t N>
struct Block
{
    uint32_t pc;
    uint64_t words[N]; // lower | upper << 32
};

// No compiled pair writes a VF or VI register with a longer latency.
inline constexpr uint32_t kMaxWriteLatency = 4u;

constexpr uint32_t vfWriteLatency(const Access::Usage &usage)
{
    return usage.vfLatency != 0u ? usage.vfLatency : usage.latency;
}

constexpr uint32_t viWriteLatency(const Access::Usage &usage)
{
    return usage.viLatency != 0u ? usage.viLatency : usage.latency;
}

// The VI register a pair writes, if any; pairs write at most one.
constexpr uint8_t viWritten(const Access::Usage &usage)
{
    const uint16_t writes = usage.viWrite & 0xFFFEu;
    return writes != 0u ? static_cast<uint8_t>(std::countr_zero(writes)) : 0u;
}

// Reads of pair Index that cannot stall: last written in the block at least its
// latency ago, or unwritten and past any write pending on entry. Bits 0-1 are
// upper VF reads, 2-3 lower VF reads, and 4 + n VI register n.
template <const auto &Code, uint32_t Index>
constexpr uint32_t settledReads()
{
    const auto decodeAt = [](uint32_t index) {
        const uint64_t word = Code.words[index];
        return Access::decode(static_cast<uint32_t>(word), static_cast<uint32_t>(word >> 32u));
    };
    const uint32_t window = std::min(Index, kMaxWriteLatency - 1u);
    const auto vfLaneSettled = [&](uint8_t reg, uint8_t laneBit) {
        for (uint32_t back = 1u; back <= window; ++back)
        {
            const auto earlier = decodeAt(Index - back);
            const auto &upper = earlier.upperUsage, &lower = earlier.lowerUsage;
            // A lower write to the register the upper half also writes is dropped.
            if (upper.vfWrite.reg == reg && (upper.vfWrite.lanes & laneBit) != 0u)
                return back >= vfWriteLatency(upper);
            if (lower.vfWrite.reg == reg && lower.vfWrite.reg != upper.vfWrite.reg &&
                (lower.vfWrite.lanes & laneBit) != 0u)
                return back >= vfWriteLatency(lower);
        }
        return Index >= kMaxWriteLatency - 1u;
    };
    const auto vfSettled = [&](uint8_t reg, uint8_t lanes) {
        for (uint32_t component = 0u; component < 4u; ++component)
            if ((lanes & lane(component)) != 0u && !vfLaneSettled(reg, lane(component)))
                return false;
        return true;
    };
    const auto viSettled = [&](uint8_t reg) {
        for (uint32_t back = 1u; back <= window; ++back)
        {
            const auto earlier = decodeAt(Index - back);
            if (viWritten(earlier.lowerUsage) == reg)
                return back >= viWriteLatency(earlier.lowerUsage);
        }
        return Index >= kMaxWriteLatency - 1u;
    };

    const auto current = decodeAt(Index);
    uint32_t settled = 0u;
    for (uint32_t read = 0u; read < 2u; ++read)
    {
        if (read < current.upperUsage.vfReadCount &&
            vfSettled(current.upperUsage.vfRead[read].reg, current.upperUsage.vfRead[read].lanes))
            settled |= 1u << read;
        if (read < current.lowerUsage.vfReadCount &&
            vfSettled(current.lowerUsage.vfRead[read].reg, current.lowerUsage.vfRead[read].lanes))
            settled |= 4u << read;
    }
    for (uint8_t reg = 1u; reg < 16u; ++reg)
        if (viSettled(reg))
            settled |= 16u << reg;
    return settled;
}

struct UpperInputs
{
    const float *left, *right, *accumulator;
    float scalarI, scalarQ;
    float fs(unsigned component) const { return left[component]; }
    float ft(unsigned component) const { return right[component]; }
    float acc(unsigned component) const { return accumulator[component]; }
    float i() const { return scalarI; }
    float q() const { return scalarQ; }
    bool compiledFastPath() const { return true; }
};

struct UpperSink
{
    float value[4];
    uint32_t mac = 0u, status = 0u, sticky = 0u, clipBits = 0u;
    bool hasFmac = false, hasClip = false;
    PS2_VU_FORCE_INLINE void vf(unsigned, unsigned mask, const float *result)
    {
        for (unsigned component = 0; component < 4u; ++component)
            if ((mask & lane(component)) != 0u)
                value[component] = result[component];
    }
    PS2_VU_FORCE_INLINE void acc(unsigned mask, const float *result) { vf(0u, mask, result); }
    PS2_VU_FORCE_INLINE void fmac(uint32_t macFlags, uint32_t statusFlags, uint32_t extraSticky)
    {
        mac = macFlags;
        status = statusFlags;
        sticky = extraSticky;
        hasFmac = true;
    }
    PS2_VU_FORCE_INLINE void clip(uint32_t sixBits)
    {
        clipBits = sixBits;
        hasClip = true;
    }
    void reserved(uint32_t) {}
};

// The opcode as computeUpper classifies it: the special form for 0x3C-0x3F.
constexpr uint32_t upperCode(uint32_t upper)
{
    const uint32_t op = upper & 0x3Fu;
    return op < 0x3Cu ? op : (upper & 3u) | ((upper >> 4u) & 0x7Cu);
}

constexpr bool upperIsNop(uint32_t upper)
{
    return (upper & 0x3Fu) >= 0x3Cu && (upperCode(upper) == 0x2Fu || upperCode(upper) == 0x30u);
}

constexpr bool upperReadsQ(uint32_t upper)
{
    const uint32_t code = upperCode(upper);
    return code == 0x1Cu || code == 0x20u || code == 0x21u || code == 0x24u || code == 0x25u;
}

// MAX, MINI, ITOF, FTOI, ABS and CLIP publish no FMAC flags; their code is small.
constexpr bool upperPublishesFlags(uint32_t upper)
{
    const uint32_t code = upperCode(upper);
    if ((code >= 0x10u && code <= 0x17u) || code == 0x1Du || code == 0x1Fu)
        return false;
    if ((upper & 0x3Fu) < 0x3Cu)
        return code != 0x2Bu && code != 0x2Fu;
    return code != 0x2Fu && code != 0x30u;
}

// Queued FMAC conditions stay in the MAC register's layout, one bit per lane
// in each of the Z, S, U and O groups; status bits are folded out on commit.
constexpr uint32_t statusOf(uint32_t mac)
{
    return ((mac & 0x000Fu) != 0u ? 1u : 0u) | ((mac & 0x00F0u) != 0u ? 2u : 0u) |
           ((mac & 0x0F00u) != 0u ? 4u : 0u) | ((mac & 0xF000u) != 0u ? 8u : 0u);
}

constexpr uint32_t spreadStatus(uint32_t status)
{
    return (status & 1u) | ((status & 2u) << 3u) | ((status & 4u) << 6u) | ((status & 8u) << 9u);
}

#if PS2_VU_PROGRAM_NEON
namespace fast
{
// Lane conditions in the MAC register's layout, limited to the Dest lanes.
template <uint8_t Dest>
PS2_VU_FORCE_INLINE uint32_t layout(uint32x4_t zero, uint32x4_t sign, uint32x4_t underflow)
{
    const uint32x4_t zeroBits = {(Dest & 8u) ? 0x8u : 0u, (Dest & 4u) ? 0x4u : 0u, (Dest & 2u) ? 0x2u : 0u,
                                 (Dest & 1u) ? 0x1u : 0u};
    const uint32x4_t bits = vorrq_u32(vandq_u32(zero, zeroBits), vandq_u32(sign, vshlq_n_u32(zeroBits, 4)));
    return vaddvq_u32(vorrq_u32(bits, vandq_u32(underflow, vshlq_n_u32(zeroBits, 8))));
}

// All ones in the lanes outside Dest, which no range check may reject.
template <uint8_t Dest>
PS2_VU_FORCE_INLINE uint32x4_t inactive()
{
    return uint32x4_t{(Dest & 8u) ? 0u : ~0u, (Dest & 4u) ? 0u : ~0u, (Dest & 2u) ? 0u : ~0u,
                      (Dest & 1u) ? 0u : ~0u};
}

PS2_VU_FORCE_INLINE uint32x4_t negative(uint32x4_t bits)
{
    return vcltzq_s32(vreinterpretq_s32_u32(bits));
}

// Denormal operands read as zero of the same sign. Infinity and NaN patterns
// stay as they are: every result they reach fails the range checks, so the
// exact path, which clamps them, handles those lanes.
PS2_VU_FORCE_INLINE float32x4_t flushDenormals(float32x4_t value)
{
    const uint32x4_t tiny = vcaltq_f32(value, vdupq_n_f32(std::numeric_limits<float>::min()));
    return vreinterpretq_f32_u32(vbicq_u32(vreinterpretq_u32_f32(value), vshrq_n_u32(tiny, 1)));
}
}
#endif

class Run
{
public:
    explicit Run(VU1Interpreter &vu);
    Run(const Run &) = delete;
    Run &operator=(const Run &) = delete;

    bool finished() const { return m_finished; }
    bool ended() const { return m_ended; }
    uint64_t generation() const { return m_generation; }

    // Stop before the pair at pc and let the interpreter continue.
    uint32_t leave(uint32_t pc);

private:
    friend class Frame;

    struct PendingFlags
    {
        uint32_t issue;
        uint32_t writes; // 1 MAC, 2 status, 4 sticky (FSSET), 8 CLIP
        uint32_t value;  // the MAC flags, FSSET's sticky bits or the CLIP flags
        uint32_t sticky; // an FMAC's product conditions, laid out like MAC
    };
    // Results queue in a flat buffer. Each block reserves room for two per
    // pair on entry, and compile_vu_programs.py keeps blocks short enough
    // for that to fit next to the few results not yet due.
    static constexpr uint32_t kFlagSlots = 512u;
    static constexpr uint32_t kMaxBlockPairs = 128u;
    static_assert(kFlagSlots >= 2u * kMaxBlockPairs + 16u);

    struct Clock
    {
        uint32_t cycle;
        bool kicking;
    };

    struct FlagRange
    {
        PendingFlags *head, *tail;
    };

    uint32_t relative(uint64_t absolute) const
    {
        return absolute > m_base ? static_cast<uint32_t>(absolute - m_base) : 0u;
    }

    // Rare paths. The Frame passes what they need and takes back what they change.
    // exactUpper is the interpreter's full arithmetic, only needed for results
    // near the range limits; it leaves its result in m_upper.
    void exactUpper(uint32_t upper);
    Clock stall(uint32_t cycle, uint32_t target, uint32_t pc, bool untilIdle);
    bool progressKick(uint32_t cycle, uint32_t nextPc);
    PendingFlags *commitFlags(PendingFlags *head, PendingFlags *tail, uint32_t upTo);
    FlagRange makeRoom(PendingFlags *head, PendingFlags *tail, uint32_t cycle);
    void cancelSameCycle(PendingFlags *head, PendingFlags *tail, uint32_t cycle, uint32_t writes);
    PendingFlags *commitQ(PendingFlags *head, PendingFlags *tail, uint32_t cycle);
    void commitP(uint32_t cycle);
    void handBack();
    uint32_t leaveStale();
    uint32_t end(uint32_t pc);

    VU1Interpreter &m_vu;
    VU1State &m_state;
    uint8_t *m_data;
    uint64_t m_base;
    uint64_t m_generation;
    std::array<std::array<uint32_t, 4>, 32> m_vfReady{};
    std::array<uint32_t, 16> m_viReady{};
    // Not cleared: a run starts every MSCAL and only ever reads what it queued.
    std::array<PendingFlags, kFlagSlots> m_flags;
    UpperSink m_upper;
    // FSSET results in the queue. Only those have to land in order with Q.
    uint32_t m_fssetQueued = 0u;
    // The Frame's state as of the last routine return.
    uint64_t m_pairs = 0u;
    uint32_t m_cycle = 0u;
    PendingFlags *m_flagHead = m_flags.data();
    PendingFlags *m_flagTail = m_flags.data();
    uint32_t m_workingClip = 0u;
    int32_t m_backupValue = 0;
    uint32_t m_backupReg = 0u;
    bool m_kickActive = false;
    bool m_stale = false;
    bool m_finished = false;
    bool m_ended = false;
    // Where to resume if a GIF callback rewrote microcode after a pair.
    uint32_t m_stalePc = 0u;
    uint32_t m_staleTarget = 0u;
    bool m_staleBranch = false;
    bool m_staleEnding = false;
};

class Frame
{
public:
    PS2_VU_FORCE_INLINE Frame(Run &run, uint64_t budgetEnd)
        : m_run(run), m_state(run.m_state), m_data(run.m_data), m_base(run.m_base),
          m_limit(budgetEnd > run.m_base ? budgetEnd - run.m_base : 0u), m_pairs(run.m_pairs),
          m_cycle(run.m_cycle), m_flagHead(run.m_flagHead), m_flagTail(run.m_flagTail),
          m_workingClip(run.m_workingClip), m_backupValue(run.m_backupValue), m_backupReg(run.m_backupReg),
          m_kickActive(run.m_kickActive)
    {
    }

    // A block runs without cycle checks only while it cannot reach the budget.
    // Counts a block's pairs up front, which is close enough for statistics
    // and keeps a counter update out of every pair.
    template <uint32_t Margin, uint32_t Pairs>
    PS2_VU_FORCE_INLINE bool fits()
    {
        static_assert(Pairs <= Run::kMaxBlockPairs, "compile_vu_programs.py should have split this block");
        if (m_cycle + static_cast<uint64_t>(Margin) >= m_limit)
            return false;
        // An upper instruction and FSSET or FCSET queue at most two results a pair.
        if (m_flagTail + 2u * Pairs > m_run.m_flags.data() + Run::kFlagSlots) [[unlikely]]
        {
            const Run::FlagRange range = m_run.makeRoom(m_flagHead, m_flagTail, m_cycle);
            m_flagHead = range.head;
            m_flagTail = range.tail;
        }
        m_pairs += Pairs;
        return true;
    }

    PS2_VU_FORCE_INLINE bool taken() const { return m_taken; }
    PS2_VU_FORCE_INLINE uint32_t jumpTarget() const { return m_jumpTarget; }

    // Issue pair Index of a block. NextPc is where the interpreter's PC would
    // stand after it; it only matters if a GIF callback rewrites microcode, in
    // which case this returns false and the routine must return leaveStale().
    template <const auto &Code, uint32_t Index>
    PS2_VU_FORCE_INLINE bool pair(uint32_t nextPc)
    {
        constexpr uint64_t word = Code.words[Index];
        return issue<(Code.pc + Index * 8u) & 0x3FFFu, static_cast<uint32_t>(word), static_cast<uint32_t>(word >> 32u),
                     settledReads<Code, Index>()>(nextPc);
    }

    // Return to the driver, which continues with the routine at pc.
    PS2_VU_FORCE_INLINE uint32_t exit(uint32_t pc)
    {
        save();
        return pc;
    }

    // Stop before the pair at pc and let the interpreter continue.
    PS2_VU_FORCE_INLINE uint32_t leave(uint32_t pc)
    {
        save();
        return m_run.leave(pc);
    }

    PS2_VU_FORCE_INLINE uint32_t leaveStale()
    {
        save();
        return m_run.leaveStale();
    }

    // The E-bit delay slot has issued; the interpreter flushes what remains.
    PS2_VU_FORCE_INLINE uint32_t end(uint32_t pc)
    {
        save();
        return m_run.end(pc);
    }

private:
    PS2_VU_FORCE_INLINE void save()
    {
        m_run.m_pairs = m_pairs;
        m_run.m_cycle = m_cycle;
        m_run.m_flagHead = m_flagHead;
        m_run.m_flagTail = m_flagTail;
        m_run.m_workingClip = m_workingClip;
        m_run.m_backupValue = m_backupValue;
        m_run.m_backupReg = m_backupReg;
        m_run.m_kickActive = m_kickActive;
    }

    // Lane helpers take the mask as a template argument and unfold without
    // loops; routines inline thousands of them and loops cost compile time.
    template <uint8_t Mask>
    static PS2_VU_FORCE_INLINE uint32_t readyOf(const uint32_t *lanes, uint32_t ready)
    {
#if PS2_VU_PROGRAM_NEON
        if constexpr (Mask == 0xFu)
            return std::max(ready, vmaxvq_u32(vld1q_u32(lanes)));
#endif
        if constexpr ((Mask & 8u) != 0u)
            ready = std::max(ready, lanes[0]);
        if constexpr ((Mask & 4u) != 0u)
            ready = std::max(ready, lanes[1]);
        if constexpr ((Mask & 2u) != 0u)
            ready = std::max(ready, lanes[2]);
        if constexpr ((Mask & 1u) != 0u)
            ready = std::max(ready, lanes[3]);
        return ready;
    }

    template <uint8_t Mask, class T>
    static PS2_VU_FORCE_INLINE void applyLanes(T *target, const T *value)
    {
        if constexpr ((Mask & 8u) != 0u)
            target[0] = value[0];
        if constexpr ((Mask & 4u) != 0u)
            target[1] = value[1];
        if constexpr ((Mask & 2u) != 0u)
            target[2] = value[2];
        if constexpr ((Mask & 1u) != 0u)
            target[3] = value[3];
    }

    template <uint8_t Mask>
    static PS2_VU_FORCE_INLINE void setReady(uint32_t *lanes, uint32_t ready)
    {
        const uint32_t value[4] = {ready, ready, ready, ready};
        applyLanes<Mask>(lanes, value);
    }

    PS2_VU_FORCE_INLINE uint32_t relative(uint64_t absolute) const
    {
        return absolute > m_base ? static_cast<uint32_t>(absolute - m_base) : 0u;
    }

    template <uint32_t Pc, uint32_t Lower, uint32_t Upper, uint32_t Settled>
    PS2_VU_FORCE_INLINE bool issue(uint32_t nextPc);

    // The latest cycle an operand of the pair becomes ready, ignoring the
    // clock and the reads settledReads found cannot stall.
    template <uint32_t Lower, uint32_t Upper, uint32_t Settled>
    PS2_VU_FORCE_INLINE uint32_t pairReady() const;

    template <uint32_t Pc, uint32_t Lower, uint32_t Upper>
    PS2_VU_FORCE_INLINE bool stale(uint32_t nextPc);

    template <uint32_t Upper>
    PS2_VU_FORCE_INLINE void upper(float *value);

#if PS2_VU_PROGRAM_NEON
    template <uint32_t Upper>
    PS2_VU_FORCE_INLINE bool fastUpper(float *value);
#endif

    template <uint32_t Upper>
    PS2_VU_FORCE_INLINE uint32_t plainUpper(float *value);

    template <uint32_t Pc, uint32_t Lower>
    PS2_VU_FORCE_INLINE void lower(float *vfResult, int32_t &viResult);

    // Room was reserved when the block was entered.
    PS2_VU_FORCE_INLINE void pushFlags(uint32_t writes, uint32_t value, uint32_t sticky)
    {
        *m_flagTail++ = {m_cycle, writes, value, sticky};
    }

    PS2_VU_FORCE_INLINE void flagsAt()
    {
        if (m_flagHead != m_flagTail && m_flagHead->issue + 4u <= m_cycle)
            m_flagHead = m_run.commitFlags(m_flagHead, m_flagTail, m_cycle);
    }

    PS2_VU_FORCE_INLINE void cancelSameCycle(uint32_t writes)
    {
        m_run.cancelSameCycle(m_flagHead, m_flagTail, m_cycle, writes);
    }

    PS2_VU_FORCE_INLINE bool qDue() const
    {
        const auto &fdiv = Access::fdiv(m_run.m_vu);
        return fdiv.valid && relative(fdiv.readyCycle) <= m_cycle;
    }

    PS2_VU_FORCE_INLINE void commitQ() { m_flagHead = m_run.commitQ(m_flagHead, m_flagTail, m_cycle); }
    PS2_VU_FORCE_INLINE void commitP() { m_run.commitP(m_cycle); }
    PS2_VU_FORCE_INLINE void syncClock() { Access::setCycle(m_run.m_vu, m_base + m_cycle); }

    PS2_VU_FORCE_INLINE int32_t branchVi(uint8_t reg) const
    {
        if (reg == 0u)
            return 0;
        return m_backupReg == reg ? m_backupValue : m_state.vi[reg];
    }

    Run &m_run;
    VU1State &m_state;
    uint8_t *m_data;
    uint64_t m_base;
    uint64_t m_limit;
    uint64_t m_pairs;
    uint32_t m_cycle;
    Run::PendingFlags *m_flagHead;
    Run::PendingFlags *m_flagTail;
    uint32_t m_workingClip;
    uint32_t m_jumpTarget = 0u;
    int32_t m_backupValue;
    uint32_t m_backupReg;
    bool m_taken = false;
    bool m_kickActive;
};

template <uint32_t Lower, uint32_t Upper, uint32_t Settled>
PS2_VU_FORCE_INLINE uint32_t Frame::pairReady() const
{
    static constexpr Access::Decoded d = Access::decode(Lower, Upper);
    const auto &vfReady = m_run.m_vfReady;
    uint32_t ready = 0u;
    if constexpr (d.upperUsage.vfReadCount > 0u && d.upperUsage.vfRead[0].reg != 0u && (Settled & 1u) == 0u)
        ready = readyOf<d.upperUsage.vfRead[0].lanes>(vfReady[d.upperUsage.vfRead[0].reg].data(), ready);
    if constexpr (d.upperUsage.vfReadCount > 1u && d.upperUsage.vfRead[1].reg != 0u && (Settled & 2u) == 0u)
        ready = readyOf<d.upperUsage.vfRead[1].lanes>(vfReady[d.upperUsage.vfRead[1].reg].data(), ready);
    if constexpr (d.lowerUsage.vfReadCount > 0u && d.lowerUsage.vfRead[0].reg != 0u && (Settled & 4u) == 0u)
        ready = readyOf<d.lowerUsage.vfRead[0].lanes>(vfReady[d.lowerUsage.vfRead[0].reg].data(), ready);
    if constexpr (d.lowerUsage.vfReadCount > 1u && d.lowerUsage.vfRead[1].reg != 0u && (Settled & 8u) == 0u)
        ready = readyOf<d.lowerUsage.vfRead[1].lanes>(vfReady[d.lowerUsage.vfRead[1].reg].data(), ready);
    constexpr uint16_t viReads = (d.upperUsage.viRead | d.lowerUsage.viRead) & 0xFFFEu & ~(Settled >> 4u);
    [&]<size_t... Reg>(std::index_sequence<Reg...>) PS2_VU_INLINE_LAMBDA {
        ((((viReads >> Reg) & 1u) != 0u ? (ready = std::max(ready, m_run.m_viReady[Reg])) : 0u), ...);
    }(std::make_index_sequence<16>{});
    constexpr auto lower = d.lowerUsage;
    if constexpr (lower.pipeline == Access::kFdiv || lower.waitQ)
    {
        const auto &fdiv = Access::fdiv(m_run.m_vu);
        if (fdiv.valid)
            ready = std::max(ready, relative(fdiv.readyCycle));
    }
    if constexpr (lower.waitP)
    {
        const std::array<Access::ScalarEntry, 2> &efu = Access::efu(m_run.m_vu);
        if (efu[0].valid)
            ready = std::max(ready, relative(efu[0].readyCycle));
        if (efu[1].valid)
            ready = std::max(ready, relative(efu[1].readyCycle));
    }
    else if constexpr (lower.pipeline == Access::kEfu)
        ready = std::max(ready, relative(Access::efuResourceReady(m_run.m_vu)));
    return ready;
}

template <uint32_t Pc, uint32_t Lower, uint32_t Upper, uint32_t Settled>
PS2_VU_FORCE_INLINE bool Frame::issue(uint32_t nextPc)
{
    static constexpr Access::Decoded decoded = Access::decode(Lower, Upper);
    static_assert(!decoded.upperUsage.reserved && !decoded.lowerUsage.reserved,
                  "reserved instruction in a compiled VU program");
    static_assert(vfWriteLatency(decoded.upperUsage) <= kMaxWriteLatency &&
                      (decoded.lowerUsage.vfWrite.reg == 0u || vfWriteLatency(decoded.lowerUsage) <= kMaxWriteLatency) &&
                      (viWritten(decoded.lowerUsage) == 0u || viWriteLatency(decoded.lowerUsage) <= kMaxWriteLatency),
                  "settledReads assumes no register write takes longer than kMaxWriteLatency");
    constexpr bool immediate = (Upper & 0x80000000u) != 0u;
    constexpr bool kick = !immediate && decoded.lowerUsage.pipeline == Access::kXgkick;

    // While PATH1 is busy every stalled cycle feeds it; XGKICK waits for it.
    // Only the last step depends on the clock, which keeps its chain short.
    const uint32_t ready = pairReady<Lower, Upper, Settled>();
    const bool kicking = m_kickActive;
    if (kicking) [[unlikely]]
    {
        const Run::Clock clock = m_run.stall(m_cycle, ready, Pc, kick);
        m_cycle = clock.cycle;
        m_kickActive = clock.kicking;
    }
    else
        m_cycle = std::max(m_cycle, ready);

    // Both halves read the state from before the pair; results land after.
    constexpr uint8_t upperReg = decoded.upperUsage.vfWrite.reg;
    constexpr uint8_t upperLanes = decoded.upperUsage.vfWrite.lanes;
    constexpr uint8_t accLanes = decoded.upperUsage.accWrite;
    float upperValue[4];
    if constexpr (!upperIsNop(Upper))
    {
        if constexpr (upperReadsQ(Upper))
            if (qDue())
                commitQ();
        upper<Upper>(upperValue);
    }

    float lowerValue[4];
    int32_t viValue = 0;
    constexpr uint16_t viWrites = decoded.lowerUsage.viWrite & 0xFFFEu;
    constexpr uint8_t viReg = viWrites != 0u ? static_cast<uint8_t>(std::countr_zero(viWrites)) : 0u;
    constexpr uint8_t lowerReg = decoded.lowerUsage.vfWrite.reg;
    constexpr uint8_t lowerLanes = decoded.lowerUsage.vfWrite.lanes;
    if constexpr (immediate)
    {
        float value;
        constexpr uint32_t bits = Lower;
        std::memcpy(&value, &bits, sizeof(value));
        m_state.i = ps2_vu_detail::upper::normalizeOperand(value);
    }
    else
        lower<Pc, Lower>(lowerValue, viValue);

    if constexpr (lowerReg != 0u && lowerReg != upperReg)
    {
        constexpr uint32_t latency = decoded.lowerUsage.vfLatency != 0u ? decoded.lowerUsage.vfLatency
                                                                        : decoded.lowerUsage.latency;
        applyLanes<lowerLanes>(m_state.vf[lowerReg], lowerValue);
        setReady<lowerLanes>(m_run.m_vfReady[lowerReg].data(), m_cycle + latency);
    }
    if constexpr (upperReg != 0u)
    {
        constexpr uint32_t latency = decoded.upperUsage.vfLatency != 0u ? decoded.upperUsage.vfLatency
                                                                        : decoded.upperUsage.latency;
        applyLanes<upperLanes>(m_state.vf[upperReg], upperValue);
        setReady<upperLanes>(m_run.m_vfReady[upperReg].data(), m_cycle + latency);
    }
    if constexpr (accLanes != 0u)
        applyLanes<accLanes>(m_state.acc, upperValue);
    if constexpr (viReg != 0u)
    {
        constexpr uint32_t latency = decoded.lowerUsage.viLatency != 0u ? decoded.lowerUsage.viLatency
                                                                        : decoded.lowerUsage.latency;
        const int32_t old = m_state.vi[viReg];
        m_state.vi[viReg] = static_cast<int16_t>(viValue);
        m_run.m_viReady[viReg] = m_cycle + latency;
        // A conditional branch right after reads the value from before this write.
        if constexpr (decoded.lowerUsage.delaysNextBranchRead)
        {
            m_backupReg = viReg;
            m_backupValue = old;
        }
        else
            m_backupReg = 0u;
    }
    else
        m_backupReg = 0u;

    ++m_cycle;
    if (kicking || m_kickActive) [[unlikely]]
    {
        if (m_kickActive)
            m_kickActive = m_run.progressKick(m_cycle, nextPc);
        if (m_run.m_stale)
            return stale<Pc, Lower, Upper>(nextPc);
    }
    return true;
}

template <uint32_t Pc, uint32_t Lower, uint32_t Upper>
PS2_VU_FORCE_INLINE bool Frame::stale(uint32_t nextPc)
{
    // Resume exactly where the interpreter would fetch the next pair.
    static constexpr Access::Decoded decoded = Access::decode(Lower, Upper);
    constexpr bool immediate = (Upper & 0x80000000u) != 0u;
    constexpr uint32_t op = Lower >> 25u;
    constexpr bool branch = !immediate && decoded.lowerUsage.pipeline == Access::kBranch;
    constexpr int32_t imm11 = static_cast<int32_t>(Lower << 21u) >> 21;
    m_run.m_stalePc = nextPc;
    m_run.m_staleBranch = branch && m_taken;
    m_run.m_staleTarget = (op == 0x24u || op == 0x25u) ? m_jumpTarget
                                                       : ((Pc + 8u + static_cast<uint32_t>(imm11 * 8)) & 0x3FFFu);
    m_run.m_staleEnding = (Upper & 0x40000000u) != 0u;
    return false;
}

template <uint32_t Upper>
PS2_VU_FORCE_INLINE void Frame::upper(float *value)
{
    if constexpr (!upperPublishesFlags(Upper))
    {
        const uint32_t clip = plainUpper<Upper>(value);
        if constexpr ((Upper & 0x3Fu) >= 0x3Cu && upperCode(Upper) == 0x1Fu)
        {
            m_workingClip = ((m_workingClip << 6u) | (clip & 0x3Fu)) & 0xFFFFFFu;
            pushFlags(8u, m_workingClip, 0u);
        }
    }
    else
    {
#if PS2_VU_PROGRAM_NEON
        if (fastUpper<Upper>(value))
            return;
#endif
        m_run.exactUpper(Upper);
        const UpperSink &exact = m_run.m_upper;
        std::memcpy(value, exact.value, sizeof(exact.value));
        if (exact.hasFmac)
            pushFlags(3u, exact.mac, spreadStatus(exact.sticky));
    }
}

#if PS2_VU_PROGRAM_NEON
// The upper instructions without FMAC flags, with computeUpper's operands and
// comparisons. Returns CLIP's six bits.
template <uint32_t Upper>
PS2_VU_FORCE_INLINE uint32_t Frame::plainUpper(float *value)
{
    constexpr uint32_t code = upperCode(Upper);
    constexpr bool special = (Upper & 0x3Fu) >= 0x3Cu;
    constexpr uint8_t fs = static_cast<uint8_t>((Upper >> 11u) & 31u);
    constexpr uint8_t ft = static_cast<uint8_t>((Upper >> 16u) & 31u);
    using ps2_vu_detail::normalizeFmacOperands;
    if constexpr (special && code >= 0x10u && code <= 0x13u) // ITOF0, ITOF4, ITOF12, ITOF15
    {
        // Scaling by a power of two is exact, so one rounding matches the interpreter's two.
        const int32x4_t raw = vreinterpretq_s32_f32(vld1q_f32(m_state.vf[fs]));
        if constexpr (code == 0x10u)
            vst1q_f32(value, vcvtq_f32_s32(raw));
        else
            vst1q_f32(value, vcvtq_n_f32_s32(raw, code == 0x11u ? 4 : code == 0x12u ? 12 : 15));
    }
    else if constexpr (special && code >= 0x14u && code <= 0x17u) // FTOI0, FTOI4, FTOI12, FTOI15
    {
        // Truncates and saturates like the interpreter's double conversion.
        const float32x4_t source = normalizeFmacOperands(vld1q_f32(m_state.vf[fs]));
        int32x4_t result;
        if constexpr (code == 0x14u)
            result = vcvtq_s32_f32(source);
        else
            result = vcvtq_n_s32_f32(source, code == 0x15u ? 4 : code == 0x16u ? 12 : 15);
        vst1q_f32(value, vreinterpretq_f32_s32(result));
    }
    else if constexpr (special && code == 0x1Du) // ABS
        vst1q_f32(value, vabsq_f32(normalizeFmacOperands(vld1q_f32(m_state.vf[fs]))));
    else if constexpr (special && code == 0x1Fu) // CLIP, on the raw bit patterns
    {
        uint32_t w;
        std::memcpy(&w, &m_state.vf[ft][3], sizeof(w));
        const int32_t limit = (w & 0x7F800000u) != 0u ? static_cast<int32_t>(w & 0x7FFFFFFFu) : 0x007FFFFF;
        const int32x4_t bits = vreinterpretq_s32_f32(vld1q_f32(m_state.vf[fs]));
        const int32x4_t bound = vdupq_n_s32(limit);
        const uint32x4_t above = vcgtq_s32(bits, bound);
        const uint32x4_t below = vcgtq_s32(veorq_s32(bits, vdupq_n_s32(std::numeric_limits<int32_t>::min())), bound);
        const uint32x4_t aboveBits = {0x01u, 0x04u, 0x10u, 0u};
        const uint32x4_t belowBits = {0x02u, 0x08u, 0x20u, 0u};
        return vaddvq_u32(vorrq_u32(vandq_u32(above, aboveBits), vandq_u32(below, belowBits)));
    }
    else // MAX and MINI with a broadcast, I or a whole vector
    {
        static_assert(!special, "unexpected upper instruction without FMAC flags");
        const float32x4_t left = normalizeFmacOperands(vld1q_f32(m_state.vf[fs]));
        float32x4_t right;
        if constexpr (code <= 0x17u)
            right = vld1q_dup_f32(&m_state.vf[ft][code & 3u]);
        else if constexpr (code == 0x1Du || code == 0x1Fu)
            right = vdupq_n_f32(m_state.i);
        else
            right = vld1q_f32(m_state.vf[ft]);
        right = normalizeFmacOperands(right);
        constexpr bool max = code <= 0x13u || code == 0x1Du || code == 0x2Bu;
        const uint32x4_t pick = max ? vcgtq_f32(left, right) : vcltq_f32(left, right);
        vst1q_f32(value, vbslq_f32(pick, left, right));
    }
    return 0u;
}
#else
// The same instructions lane by lane, in computeUpper's arithmetic. Inlining
// computeUpper itself cost more compile time than the rest of a routine.
template <uint32_t Upper>
PS2_VU_FORCE_INLINE uint32_t Frame::plainUpper(float *value)
{
    using ps2_vu_detail::upper::normalizeOperand;
    constexpr uint32_t code = upperCode(Upper);
    constexpr bool special = (Upper & 0x3Fu) >= 0x3Cu;
    constexpr uint8_t fs = static_cast<uint8_t>((Upper >> 11u) & 31u);
    constexpr uint8_t ft = static_cast<uint8_t>((Upper >> 16u) & 31u);
    const float *source = m_state.vf[fs];
    if constexpr (special && code >= 0x10u && code <= 0x17u) // ITOF and FTOI
    {
        constexpr float scale = (code & 3u) == 0u ? 1.0f : (code & 3u) == 1u ? 16.0f : (code & 3u) == 2u ? 4096.0f : 32768.0f;
        for (uint32_t lane = 0u; lane < 4u; ++lane)
        {
            int32_t bits;
            if constexpr (code <= 0x13u)
            {
                std::memcpy(&bits, &source[lane], sizeof(bits));
                value[lane] = static_cast<float>(bits) / scale;
            }
            else
            {
                bits = ps2_vu_detail::upper::floatToInt(normalizeOperand(source[lane]), scale);
                std::memcpy(&value[lane], &bits, sizeof(bits));
            }
        }
    }
    else if constexpr (special && code == 0x1Du) // ABS
    {
        for (uint32_t lane = 0u; lane < 4u; ++lane)
            value[lane] = std::fabs(normalizeOperand(source[lane]));
    }
    else if constexpr (special && code == 0x1Fu) // CLIP, on the raw bit patterns
    {
        uint32_t w;
        std::memcpy(&w, &m_state.vf[ft][3], sizeof(w));
        const int32_t limit = (w & 0x7F800000u) != 0u ? static_cast<int32_t>(w & 0x7FFFFFFFu) : 0x007FFFFF;
        uint32_t flags = 0u;
        for (uint32_t lane = 0u; lane < 3u; ++lane)
        {
            int32_t bits;
            std::memcpy(&bits, &source[lane], sizeof(bits));
            if (bits > limit)
                flags |= 1u << (lane * 2u);
            if ((bits ^ std::numeric_limits<int32_t>::min()) > limit)
                flags |= 2u << (lane * 2u);
        }
        return flags;
    }
    else // MAX and MINI with a broadcast, I or a whole vector
    {
        static_assert(!special, "unexpected upper instruction without FMAC flags");
        constexpr bool max = code <= 0x13u || code == 0x1Du || code == 0x2Bu;
        for (uint32_t lane = 0u; lane < 4u; ++lane)
        {
            const float left = normalizeOperand(source[lane]);
            float right;
            if constexpr (code <= 0x17u)
                right = normalizeOperand(m_state.vf[ft][code & 3u]);
            else if constexpr (code == 0x1Du || code == 0x1Fu)
                right = normalizeOperand(m_state.i);
            else
                right = normalizeOperand(m_state.vf[ft][lane]);
            value[lane] = max ? (left > right ? left : right) : (left < right ? left : right);
        }
    }
    return 0u;
}
#endif

#if PS2_VU_PROGRAM_NEON
// computeUpper's vector cases with the same range checks, but with the flags
// built straight into the MAC layout. Results the checks do not clear take
// the exact path.
template <uint32_t Upper>
PS2_VU_FORCE_INLINE bool Frame::fastUpper(float *value)
{
    constexpr uint32_t code = upperCode(Upper);
    constexpr uint8_t dest = static_cast<uint8_t>((Upper >> 21u) & 15u);
    constexpr uint8_t fs = static_cast<uint8_t>((Upper >> 11u) & 31u);
    constexpr uint8_t ft = static_cast<uint8_t>((Upper >> 16u) & 31u);
    // OPMULA and OPMSUB take the cross-product lanes fs.yzx and ft.zxy. Their w
    // result is a constant zero, which the exact path handles.
    constexpr bool cross = code == 0x2Eu && (dest & 1u) == 0u;
    constexpr bool accumulate = (Upper & 0x3Fu) >= 0x3Cu;
    constexpr bool simpleAdd = code <= 3u || code == 0x20u || code == 0x22u || code == 0x28u;
    constexpr bool simpleSub = (code >= 4u && code <= 7u) || code == 0x24u || code == 0x26u || code == 0x2Cu;
    constexpr bool simpleMul = (code >= 0x18u && code <= 0x1Cu) || code == 0x1Eu || code == 0x2Au || (cross && accumulate);
    constexpr bool productSum = (code >= 8u && code <= 0xFu) || code == 0x21u || code == 0x23u ||
                                code == 0x25u || code == 0x27u || code == 0x29u || code == 0x2Du || (cross && !accumulate);
    if constexpr (!simpleAdd && !simpleSub && !simpleMul && !productSum)
        return false;
    else
    {
        using fast::flushDenormals;
        const uint32x4_t magnitudeMask = vdupq_n_u32(0x7FFFFFFFu);
        float32x4_t left = flushDenormals(vld1q_f32(m_state.vf[fs]));
        float32x4_t right;
        if constexpr (code <= 0xFu || (code >= 0x18u && code <= 0x1Bu))
            right = vld1q_dup_f32(&m_state.vf[ft][code & 3u]);
        else if constexpr (code == 0x1Cu || code == 0x20u || code == 0x21u || code == 0x24u || code == 0x25u)
            right = vdupq_n_f32(m_state.q);
        else if constexpr (code == 0x1Eu || code == 0x22u || code == 0x23u || code == 0x26u || code == 0x27u)
            right = vdupq_n_f32(m_state.i);
        else
            right = vld1q_f32(m_state.vf[ft]);
        right = flushDenormals(right);
        if constexpr (cross)
        {
            // fs.yzxw and ft.zxyw; GCC only has __builtin_shufflevector from 12 on.
            left = vcopyq_laneq_f32(vcopyq_laneq_f32(vextq_f32(left, left, 1), 2, left, 0), 3, left, 3);
            right = vcopyq_laneq_f32(vcopyq_laneq_f32(vextq_f32(right, right, 3), 0, right, 2), 3, right, 3);
        }
        const uint32x4_t leftBits = vreinterpretq_u32_f32(left);
        const uint32x4_t rightBits = vreinterpretq_u32_f32(right);

        if constexpr (!productSum)
        {
            float32x4_t result;
            if constexpr (simpleAdd)
                result = vaddq_f32(left, right);
            else if constexpr (simpleSub)
                result = vsubq_f32(left, right);
            else
                result = vmulq_f32(left, right);
            const uint32x4_t bits = vreinterpretq_u32_f32(result);
            const uint32x4_t magnitude = vandq_u32(bits, magnitudeMask);
            const uint32x4_t leftZero = vceqzq_u32(vandq_u32(leftBits, magnitudeMask));
            const uint32x4_t rightZero = vceqzq_u32(vandq_u32(rightBits, magnitudeMask));
            uint32x4_t exactZero;
            if constexpr (simpleMul)
                exactZero = vorrq_u32(leftZero, rightZero);
            else
            {
                const uint32x4_t opposing = simpleAdd ? veorq_u32(rightBits, vdupq_n_u32(0x80000000u)) : rightBits;
                exactZero = vorrq_u32(vandq_u32(leftZero, rightZero), vceqq_u32(leftBits, opposing));
            }
            const uint32x4_t zero = vceqzq_u32(magnitude);
            const uint32x4_t normal = vandq_u32(vcgtq_u32(magnitude, vdupq_n_u32(0x00800000u)),
                                                vcltq_u32(magnitude, vdupq_n_u32(0x7F7FFFFFu)));
            // Boundary results keep the exact checks: tiny results rounded to
            // zero or MIN_NORMAL, and overflow to FLT_MAX.
            const uint32x4_t safe = vorrq_u32(normal, vandq_u32(zero, exactZero));
            if (vminvq_u32(vorrq_u32(safe, fast::inactive<dest>())) == 0u)
                return false;
            vst1q_f32(value, result);
            if constexpr (dest != 0u)
                pushFlags(3u, fast::layout<dest>(zero, fast::negative(bits), vdupq_n_u32(0u)), 0u);
            return true;
        }
        else
        {
            constexpr bool subtract =
                (code >= 0xCu && code <= 0xFu) || code == 0x25u || code == 0x27u || code == 0x2Du || cross;
            // Only FMAC results reach ACC, and those are already in range.
            const float32x4_t acc = vld1q_f32(m_state.acc);
            const float32x4_t result = subtract ? vfmsq_f32(acc, left, right) : vfmaq_f32(acc, left, right);
            const uint32x4_t bits = vreinterpretq_u32_f32(result);
            const uint32x4_t exponentMask = vdupq_n_u32(0xFFu);
            const uint32x4_t resultExponent = vandq_u32(vshrq_n_u32(bits, 23), exponentMask);
            const uint32x4_t leftExponent = vandq_u32(vshrq_n_u32(leftBits, 23), exponentMask);
            const uint32x4_t rightExponent = vandq_u32(vshrq_n_u32(rightBits, 23), exponentMask);
            const uint32x4_t productBound = vaddq_u32(leftExponent, rightExponent);
            const uint32x4_t zeroProduct = vorrq_u32(vceqzq_u32(leftExponent), vceqzq_u32(rightExponent));
            const uint32x4_t productSafe = vandq_u32(vcleq_u32(productBound, vdupq_n_u32(379u)),
                                                     vcleq_u32(productBound, vaddq_u32(resultExponent, vdupq_n_u32(146u))));
            const uint32x4_t resultSafe = vandq_u32(vcgeq_u32(resultExponent, vdupq_n_u32(2u)),
                                                    vcleq_u32(resultExponent, vdupq_n_u32(253u)));
            const uint32x4_t zero = vceqzq_u32(vandq_u32(bits, magnitudeMask));
            const uint32x4_t safe = vorrq_u32(vandq_u32(resultSafe, vorrq_u32(zeroProduct, productSafe)),
                                              vandq_u32(zero, zeroProduct));
            if (vminvq_u32(vorrq_u32(safe, fast::inactive<dest>())) == 0u)
                return false;
            vst1q_f32(value, result);
            if constexpr (dest != 0u)
            {
                // The accepted exponent bound excludes product overflow. A tiny
                // product is exact zero only when one multiplicand is zero.
                const uint32x4_t product = vreinterpretq_u32_f32(vabsq_f32(vmulq_f32(left, right)));
                const uint32x4_t tiny = vcltq_u32(product, vdupq_n_u32(0x00800000u));
                const uint32_t conditions = fast::layout<dest>(tiny, fast::negative(veorq_u32(leftBits, rightBits)),
                                                               vbicq_u32(tiny, zeroProduct));
                pushFlags(3u, fast::layout<dest>(zero, fast::negative(bits), vdupq_n_u32(0u)), conditions);
            }
            return true;
        }
    }
}
#endif

template <uint32_t Pc, uint32_t Lower>
PS2_VU_FORCE_INLINE void Frame::lower(float *vfResult, int32_t &viResult)
{
    constexpr uint32_t opcode = Lower >> 25u;
    constexpr uint8_t dest = static_cast<uint8_t>((Lower >> 21u) & 15u);
    constexpr uint8_t vfT = static_cast<uint8_t>((Lower >> 16u) & 31u);
    constexpr uint8_t vfS = static_cast<uint8_t>((Lower >> 11u) & 31u);
    constexpr uint8_t viT = vfT & 15u;
    constexpr uint8_t viS = vfS & 15u;
    constexpr int32_t imm11 = static_cast<int32_t>(Lower << 21u) >> 21;
    constexpr uint32_t pcMask = 0x3FFFu;
    constexpr uint32_t relativeTarget = (Pc + 8u + static_cast<uint32_t>(imm11 * 8)) & pcMask;
    const auto address = [](int32_t qword) PS2_VU_INLINE_LAMBDA {
        return (static_cast<uint32_t>(qword) * 16u) & 0x3FF0u;
    };
    const auto store = [&](uint32_t addr, const uint32_t *words) PS2_VU_INLINE_LAMBDA {
        uint32_t lanes[4];
        std::memcpy(lanes, m_data + addr, sizeof(lanes));
        applyLanes<dest>(lanes, words);
        std::memcpy(m_data + addr, lanes, sizeof(lanes));
    };
    const auto storeVector = [&](uint32_t addr) PS2_VU_INLINE_LAMBDA {
        uint32_t words[4];
        std::memcpy(words, m_state.vf[vfS], sizeof(words));
        store(addr, words);
    };
    constexpr uint32_t integerLane = (dest & 8u) ? 0u : (dest & 4u) ? 1u : (dest & 2u) ? 2u : 3u;
    const auto broadcast = [&](float value) PS2_VU_INLINE_LAMBDA {
        vfResult[0] = vfResult[1] = vfResult[2] = vfResult[3] = value;
    };

    if constexpr (Lower == 0u || Lower == 0x8000033Cu)
        return;
    else if constexpr (opcode == 0x00u) // LQ
        std::memcpy(vfResult, m_data + address(m_state.vi[viS] + imm11), 16u);
    else if constexpr (opcode == 0x01u) // SQ
        storeVector(address(m_state.vi[viT] + imm11));
    else if constexpr (opcode == 0x04u) // ILW
    {
        uint32_t value;
        std::memcpy(&value, m_data + address(m_state.vi[viS] + imm11) + integerLane * 4u, 4u);
        viResult = static_cast<int16_t>(value & 0xFFFFu);
    }
    else if constexpr (opcode == 0x05u) // ISW
    {
        const uint32_t value = static_cast<uint16_t>(m_state.vi[viT] & 0xFFFF);
        const uint32_t words[4] = {value, value, value, value};
        store(address(m_state.vi[viS] + imm11), words);
    }
    else if constexpr (opcode == 0x08u || opcode == 0x09u) // IADDIU, ISUBIU
    {
        constexpr int32_t imm15 = static_cast<int16_t>((Lower & 0x7FFu) | ((Lower >> 10u) & 0x7800u));
        viResult = static_cast<int16_t>(opcode == 0x08u ? m_state.vi[viS] + imm15 : m_state.vi[viS] - imm15);
    }
    else if constexpr (opcode == 0x10u) // FCEQ
    {
        flagsAt();
        viResult = (m_state.clip & 0xFFFFFFu) == (Lower & 0xFFFFFFu) ? 1 : 0;
    }
    else if constexpr (opcode == 0x11u) // FCSET
    {
        m_workingClip = Lower & 0xFFFFFFu;
        cancelSameCycle(8u);
        pushFlags(8u, m_workingClip, 0u);
    }
    else if constexpr (opcode == 0x12u) // FCAND
    {
        flagsAt();
        viResult = (m_state.clip & Lower & 0xFFFFFFu) != 0u ? 1 : 0;
    }
    else if constexpr (opcode == 0x13u) // FCOR
    {
        flagsAt();
        viResult = ((m_state.clip | Lower) & 0xFFFFFFu) == 0xFFFFFFu ? 1 : 0;
    }
    else if constexpr (opcode >= 0x14u && opcode <= 0x17u) // FSEQ, FSSET, FSAND, FSOR
    {
        constexpr uint32_t imm12 = (((Lower >> 21u) & 1u) << 11u) | (Lower & 0x7FFu);
        if constexpr (opcode == 0x15u)
        {
            cancelSameCycle(2u);
            pushFlags(4u, imm12 & 0xFC0u, 0u);
            ++m_run.m_fssetQueued;
        }
        else
        {
            if (qDue())
                commitQ();
            flagsAt();
            const uint32_t status = m_state.status & 0xFFFu;
            if constexpr (opcode == 0x14u)
                viResult = status == imm12 ? 1 : 0;
            else if constexpr (opcode == 0x16u)
                viResult = static_cast<int32_t>(status & imm12);
            else
                viResult = static_cast<int32_t>(status | imm12);
        }
    }
    else if constexpr (opcode == 0x18u || opcode == 0x1Au || opcode == 0x1Bu) // FMEQ, FMAND, FMOR
    {
        flagsAt();
        const uint32_t mask = static_cast<uint16_t>(m_state.vi[viS]);
        if constexpr (opcode == 0x18u)
            viResult = (m_state.mac & 0xFFFFu) == mask ? 1 : 0;
        else if constexpr (opcode == 0x1Au)
            viResult = static_cast<int32_t>(m_state.mac & mask);
        else
            viResult = static_cast<int32_t>(m_state.mac | mask);
    }
    else if constexpr (opcode == 0x1Cu) // FCGET
    {
        flagsAt();
        viResult = static_cast<int32_t>(m_state.clip & 0x0FFFu);
    }
    else if constexpr (opcode == 0x20u || opcode == 0x21u) // B, BAL
    {
        // The interpreter leaves the last taken target in the state; so do we.
        m_taken = true;
        m_state.branchTarget = relativeTarget;
        if constexpr (opcode == 0x21u)
            viResult = static_cast<int32_t>((Pc + 16u) / 8u);
    }
    else if constexpr (opcode == 0x24u || opcode == 0x25u) // JR, JALR
    {
        // Register jumps do not use the delayed operand of conditional branches.
        m_taken = true;
        m_jumpTarget = (static_cast<uint32_t>(static_cast<uint16_t>(m_state.vi[viS])) * 8u) & pcMask;
        m_state.branchTarget = m_jumpTarget;
        if constexpr (opcode == 0x25u)
            viResult = static_cast<int32_t>((Pc + 16u) / 8u);
    }
    else if constexpr (opcode == 0x28u || opcode == 0x29u || (opcode >= 0x2Cu && opcode <= 0x2Fu))
    {
        const int16_t s = static_cast<int16_t>(branchVi(viS));
        if constexpr (opcode == 0x28u) // IBEQ
            m_taken = s == static_cast<int16_t>(branchVi(viT));
        else if constexpr (opcode == 0x29u) // IBNE
            m_taken = s != static_cast<int16_t>(branchVi(viT));
        else if constexpr (opcode == 0x2Cu) // IBLTZ
            m_taken = s < 0;
        else if constexpr (opcode == 0x2Du) // IBGTZ
            m_taken = s > 0;
        else if constexpr (opcode == 0x2Eu) // IBLEZ
            m_taken = s <= 0;
        else // IBGEZ
            m_taken = s >= 0;
        if (m_taken)
            m_state.branchTarget = relativeTarget;
    }
    else if constexpr (opcode == 0x40u)
    {
        constexpr uint32_t direct = Lower & 0x3Fu;
        constexpr uint32_t special = (Lower & 3u) | ((Lower >> 4u) & 0x7Cu);
        using ps2_vu_detail::upper::normalizeOperand;
        if constexpr (direct == 0x30u) // IADD
            viResult = static_cast<int16_t>(m_state.vi[viS] + m_state.vi[viT]);
        else if constexpr (direct == 0x31u) // ISUB
            viResult = static_cast<int16_t>(m_state.vi[viS] - m_state.vi[viT]);
        else if constexpr (direct == 0x32u) // IADDI
        {
            constexpr int32_t imm5 = static_cast<int32_t>(((Lower >> 6u) & 0x1Fu) << 27u) >> 27;
            viResult = static_cast<int16_t>(m_state.vi[viS] + imm5);
        }
        else if constexpr (direct == 0x34u) // IAND
            viResult = m_state.vi[viS] & m_state.vi[viT];
        else if constexpr (direct == 0x35u) // IOR
            viResult = m_state.vi[viS] | m_state.vi[viT];
        else if constexpr (special == 0x30u) // MOVE
            std::memcpy(vfResult, m_state.vf[vfS], 16u);
        else if constexpr (special == 0x31u) // MR32
        {
            const float *source = m_state.vf[vfS];
            vfResult[0] = source[1];
            vfResult[1] = source[2];
            vfResult[2] = source[3];
            vfResult[3] = source[0];
        }
        else if constexpr (special == 0x34u) // LQI
        {
            std::memcpy(vfResult, m_data + address(static_cast<uint16_t>(m_state.vi[viS])), 16u);
            viResult = static_cast<int16_t>(m_state.vi[viS] + 1);
        }
        else if constexpr (special == 0x35u) // SQI
        {
            storeVector(address(static_cast<uint16_t>(m_state.vi[viT])));
            viResult = static_cast<int16_t>(m_state.vi[viT] + 1);
        }
        else if constexpr (special == 0x36u) // LQD
        {
            const int32_t value = viS != 0u ? static_cast<int16_t>(m_state.vi[viS] - 1) : 0;
            viResult = value;
            std::memcpy(vfResult, m_data + address(static_cast<uint16_t>(value)), 16u);
        }
        else if constexpr (special == 0x37u) // SQD
        {
            const int32_t value = viT != 0u ? static_cast<int16_t>(m_state.vi[viT] - 1) : 0;
            viResult = value;
            storeVector(address(static_cast<uint16_t>(value)));
        }
        else if constexpr (special == 0x38u || special == 0x39u || special == 0x3Au) // DIV, SQRT, RSQRT
        {
            constexpr uint32_t fsf = (Lower >> 21u) & 3u, ftf = (Lower >> 23u) & 3u;
            if (qDue())
                commitQ();
            syncClock();
            if constexpr (special == 0x38u)
            {
                const float num = normalizeOperand(m_state.vf[vfS][fsf]);
                const float den = normalizeOperand(m_state.vf[vfT][ftf]);
                uint32_t statusDi = 0u;
                float result;
                if (den == 0.0f)
                {
                    statusDi = num == 0.0f ? 0x10u : 0x20u;
                    result = std::signbit(num) != std::signbit(den) ? -std::numeric_limits<float>::max()
                                                                    : std::numeric_limits<float>::max();
                }
                else
                    result = num / den;
                uint32_t ignored = 0u;
                result = ps2_vu_detail::upper::normalizeResult(result, ignored);
                Access::queueQ(m_run.m_vu, result, 7u, statusDi);
            }
            else if constexpr (special == 0x39u)
            {
                const float value = normalizeOperand(m_state.vf[vfT][ftf]);
                Access::queueQ(m_run.m_vu, std::sqrt(std::fabs(value)), 7u, value < 0.0f ? 0x10u : 0u);
            }
            else
            {
                const float num = normalizeOperand(m_state.vf[vfS][fsf]);
                const float radicand = normalizeOperand(m_state.vf[vfT][ftf]);
                const float den = std::sqrt(std::fabs(radicand));
                uint32_t statusDi = radicand < 0.0f ? 0x10u : 0u;
                float result;
                if (den != 0.0f)
                    result = num / den;
                else
                {
                    statusDi = num == 0.0f ? 0x10u : 0x20u;
                    result = std::signbit(num) ? -std::numeric_limits<float>::max()
                                               : std::numeric_limits<float>::max();
                }
                uint32_t ignored = 0u;
                result = ps2_vu_detail::upper::normalizeResult(result, ignored);
                Access::queueQ(m_run.m_vu, result, 13u, statusDi);
            }
        }
        else if constexpr (special == 0x3Bu || special == 0x7Bu) // WAITQ, WAITP
        {
        }
        else if constexpr (special == 0x3Cu) // MTIR
        {
            uint32_t bits;
            std::memcpy(&bits, &m_state.vf[vfS][(Lower >> 21u) & 3u], 4u);
            viResult = static_cast<int16_t>(bits & 0xFFFFu);
        }
        else if constexpr (special == 0x3Du) // MFIR
        {
            const int32_t value = static_cast<int16_t>(m_state.vi[viS] & 0xFFFF);
            float bits;
            std::memcpy(&bits, &value, 4u);
            broadcast(bits);
        }
        else if constexpr (special == 0x3Eu) // ILWR
        {
            uint32_t value;
            std::memcpy(&value, m_data + address(static_cast<uint16_t>(m_state.vi[viS])) + integerLane * 4u, 4u);
            viResult = static_cast<int16_t>(value & 0xFFFFu);
        }
        else if constexpr (special == 0x3Fu) // ISWR
        {
            const uint32_t value = static_cast<uint16_t>(m_state.vi[viT] & 0xFFFF);
            const uint32_t words[4] = {value, value, value, value};
            store(address(static_cast<uint16_t>(m_state.vi[viS])), words);
        }
        else if constexpr (special == 0x64u) // MFP
        {
            commitP();
            broadcast(m_state.p);
        }
        else if constexpr (special == 0x68u) // XTOP
            viResult = static_cast<int32_t>(m_state.top & 0x3FFu);
        else if constexpr (special == 0x69u) // XITOP
            viResult = static_cast<int32_t>(m_state.itop & 0x3FFu);
        else if constexpr (special == 0x6Cu) // XGKICK
        {
            syncClock();
            Access::startXgkick(m_run.m_vu, static_cast<uint16_t>(m_state.vi[viS]));
            m_kickActive = Access::xgkickActive(m_run.m_vu);
        }
        else if constexpr (special >= 0x70u && special <= 0x7Du) // EFU
        {
            const float *source = m_state.vf[vfS];
            const float x = normalizeOperand(source[0]), y = normalizeOperand(source[1]);
            const float z = normalizeOperand(source[2]);
            const float component = normalizeOperand(source[(Lower >> 21u) & 3u]);
            float result = 0.0f;
            uint32_t latency = 0u;
            if constexpr (special == 0x70u) // ESADD
            {
                result = x * x + y * y + z * z;
                latency = 11u;
            }
            else if constexpr (special == 0x71u) // ERSADD
            {
                const float sum = x * x + y * y + z * z;
                result = sum != 0.0f ? 1.0f / sum : sum;
                latency = 18u;
            }
            else if constexpr (special == 0x72u) // ELENG
            {
                result = std::sqrt(x * x + y * y + z * z);
                latency = 18u;
            }
            else if constexpr (special == 0x73u) // ERLENG
            {
                const float length = std::sqrt(x * x + y * y + z * z);
                result = length != 0.0f ? 1.0f / length : length;
                latency = 24u;
            }
            else if constexpr (special == 0x74u) // EATANxy
            {
                result = x != 0.0f ? ps2_vu_detail::vuEatan(y / x) : 0.0f;
                latency = 54u;
            }
            else if constexpr (special == 0x75u) // EATANxz
            {
                result = x != 0.0f ? ps2_vu_detail::vuEatan(z / x) : 0.0f;
                latency = 54u;
            }
            else if constexpr (special == 0x76u) // ESUM
            {
                // Summed from 0.0f like the interpreter, which matters when every lane is -0.
                result = 0.0f + x + y + z + normalizeOperand(source[3]);
                latency = 12u;
            }
            else if constexpr (special == 0x77u) // ERSQRT
            {
                result = component;
                if (result >= 0.0f)
                {
                    result = std::sqrt(result);
                    if (result != 0.0f)
                        result = 1.0f / result;
                }
                latency = 18u;
            }
            else if constexpr (special == 0x78u) // ESQRT
            {
                result = component >= 0.0f ? std::sqrt(component) : component;
                latency = 12u;
            }
            else if constexpr (special == 0x79u) // ESIN
            {
                result = ps2_vu_detail::vuEsin(component);
                latency = 29u;
            }
            else if constexpr (special == 0x7Au) // ERCPR
            {
                result = component != 0.0f ? 1.0f / component : component;
                latency = 12u;
            }
            else if constexpr (special == 0x7Cu) // EATAN
            {
                result = ps2_vu_detail::vuEatan(component);
                latency = 54u;
            }
            else if constexpr (special == 0x7Du) // EEXP
            {
                result = ps2_vu_detail::vuEexp(component);
                latency = 44u;
            }
            else
                static_assert(special == 0x70u, "unsupported EFU instruction in a compiled VU program");
            commitP();
            syncClock();
            Access::queueP(m_run.m_vu, result, latency);
        }
        else
            static_assert(special == 0x30u, "unsupported lower instruction in a compiled VU program");
    }
    else
        static_assert(opcode == 0x00u, "unsupported lower instruction in a compiled VU program");
}

}
#endif
