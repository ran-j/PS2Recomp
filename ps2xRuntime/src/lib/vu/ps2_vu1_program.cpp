#include "ps2_vu1_program.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <set>
#include <string>
#include <utility>

#ifdef PS2X_VU_PROGRAM_EXTERN_INCLUDE
#include PS2X_VU_PROGRAM_EXTERN_INCLUDE
#endif

namespace ps2_vu_program
{
namespace
{
// Sorted by entry PC; the last element only terminates the table.
const Entry kEntries[] = {
#ifdef PS2X_VU_PROGRAM_INCLUDE
#include PS2X_VU_PROGRAM_INCLUDE
#endif
    {0xFFFFFFFFu, 0u, nullptr, nullptr, nullptr},
};

bool matches(const Entry &entry, const uint8_t *code, uint32_t codeSize)
{
    for (uint32_t index = 0; index < entry.footprintPairs; ++index)
    {
        const uint32_t offset = static_cast<uint32_t>(entry.footprint[index]) * 8u;
        uint64_t word;
        if (offset + 8u > codeSize)
            return false;
        std::memcpy(&word, code + offset, sizeof(word));
        if (word != entry.words[index])
            return false;
    }
    return true;
}

struct CacheSlot
{
    const uint8_t *code = nullptr;
    uint64_t generation = 0u;
    uint64_t epoch = 0u;
    Routine run = nullptr;
    bool valid = false;
};

std::atomic<uint64_t> g_epoch{0u};
}

void forgetRoutines()
{
    g_epoch.fetch_add(1u, std::memory_order_relaxed);
}

Routine find(const uint8_t *code, uint32_t codeSize, uint32_t pc, uint64_t generation)
{
    // Microcode changes far less often than routines are entered, so each
    // answer stands until the code generation moves on.
    thread_local std::array<CacheSlot, 2048> cache{};
    CacheSlot &slot = cache[(pc / 8u) & 2047u];
    const uint64_t epoch = g_epoch.load(std::memory_order_relaxed);
    if (slot.valid && slot.code == code && slot.generation == generation && slot.epoch == epoch)
        return slot.run;

    Routine routine = nullptr;
    const Entry *const begin = std::begin(kEntries);
    const Entry *const end = std::end(kEntries) - 1;
    const Entry *entry = std::lower_bound(begin, end, pc,
                                          [](const Entry &candidate, uint32_t value) { return candidate.pc < value; });
    for (; entry != end && entry->pc == pc; ++entry)
    {
        if (matches(*entry, code, codeSize))
        {
            routine = entry->run;
            break;
        }
    }
    slot = {code, generation, epoch, routine, true};
    return routine;
}

void recordMissing(const uint8_t *code, uint32_t codeSize, uint32_t pc)
{
    static const char *directory = std::getenv("PS2_VU_PROGRAM_PROFILE");
    if (!directory || !*directory)
        return;
    static std::mutex mutex;
    const std::lock_guard lock(mutex);
    static std::set<std::pair<uint64_t, uint32_t>> recorded;
    uint64_t hash = 14695981039346656037ull;
    for (uint32_t i = 0; i < codeSize; ++i)
        hash = (hash ^ code[i]) * 1099511628211ull;
    if (!recorded.emplace(hash, pc).second)
        return;
    char name[20];
    std::snprintf(name, sizeof(name), "%016llx", static_cast<unsigned long long>(hash));
    const std::filesystem::path root(directory);
    const std::filesystem::path image = root / (std::string("vu1-") + name + ".code");
    std::error_code error;
    std::filesystem::create_directories(root, error);
    if (!std::filesystem::exists(image, error))
        std::ofstream(image, std::ios::binary).write(reinterpret_cast<const char *>(code), codeSize);
    std::ofstream(root / "entries.txt", std::ios::app) << name << ' ' << std::hex << pc << '\n';
}

void Access::pendVf(VU1Interpreter &vu, uint8_t reg, uint8_t lanes, uint64_t ready)
{
    auto *slot = ps2_vu_detail::allocateScheduledEntry(vu.m_vfWritePipeline, vu.m_activeVfWrites, ready);
    if (!slot)
        return;
    *slot = {};
    slot->valid = true;
    slot->readyCycle = ready;
    slot->sequence = ++vu.m_nextWriteSequence;
    slot->reg = reg;
    slot->laneMask = lanes;
    std::memcpy(slot->value.data(), vu.m_state.vf[reg], sizeof(slot->value));
    for (uint32_t component = 0; component < 4u; ++component)
        if ((lanes & lane(component)) != 0u)
        {
            vu.m_vfLatestWrite[reg][component] = slot->sequence;
            vu.m_vfReady[reg][component] = ready;
        }
}

void Access::pendVi(VU1Interpreter &vu, uint8_t reg, uint64_t ready)
{
    auto *slot = ps2_vu_detail::allocateScheduledEntry(vu.m_viWritePipeline, vu.m_activeViWrites, ready);
    if (!slot)
        return;
    *slot = {};
    slot->valid = true;
    slot->readyCycle = ready;
    slot->sequence = ++vu.m_nextWriteSequence;
    slot->reg = reg;
    slot->value = vu.m_state.vi[reg];
    vu.m_viLatestWrite[reg] = slot->sequence;
    vu.m_viReady[reg] = ready;
}

void Access::pendFlags(VU1Interpreter &vu, uint64_t issue, uint32_t mac, uint32_t status,
                       uint32_t sticky, uint32_t clip, uint8_t writes)
{
    const uint64_t ready = issue + VU1Interpreter::kFmacLatency;
    auto *slot = ps2_vu_detail::allocateScheduledEntry(vu.m_flagPipeline, vu.m_activeFlags, ready);
    if (!slot)
        return;
    *slot = {};
    slot->valid = true;
    slot->issueCycle = issue;
    slot->readyCycle = ready;
    slot->mac = mac;
    slot->status = status;
    slot->extraSticky = sticky;
    slot->clip = clip;
    slot->writesMac = (writes & 1u) != 0u;
    slot->writesStatus = (writes & 2u) != 0u;
    slot->writesSticky = (writes & 4u) != 0u;
    slot->writesClip = (writes & 8u) != 0u;
}

void Access::finish(VU1Interpreter &vu, uint64_t cycle, uint32_t workingClip, uint8_t backupReg,
                    int32_t backupValue, uint64_t pairs)
{
    vu.m_cycle = cycle;
    vu.m_state.cycles = cycle;
    vu.m_workingClip = workingClip;
    vu.m_viBranchBackupValid = backupReg != 0u;
    vu.m_viBranchBackupReg = backupReg;
    vu.m_viBranchBackupValue = backupValue;
    vu.m_compiledPairsExecuted += pairs;
}

Run::Run(VU1Interpreter &vu)
    : m_vu(vu), m_state(Access::state(vu)), m_data(Access::data(vu)),
      m_base(Access::cycle(vu)), m_generation(Access::codeGeneration(vu))
{
    // Drained on entry, so every ready cycle is now.
    m_backupValue = Access::branchBackup(vu, m_backupReg);
    m_workingClip = Access::workingClip(vu);
}

void Run::exactUpper(uint32_t upper)
{
    const uint32_t fs = (upper >> 11u) & 31u, ft = (upper >> 16u) & 31u;
    const UpperInputs inputs{m_state.vf[fs], m_state.vf[ft], m_state.acc, m_state.i, m_state.q};
    m_upper = {};
    ps2_vu_detail::upper::computeUpper(upper, inputs, m_upper);
}

bool Run::progressKick(uint32_t cycle, uint32_t nextPc)
{
    // A GIF callback sees the PC and clock the interpreter would show here.
    m_state.pc = nextPc;
    m_state.cycles = m_base + cycle;
    Access::progressXgkick(m_vu);
    if (Access::xgkickActive(m_vu))
        return true;
    if (Access::codeGeneration(m_vu) != m_generation)
        m_stale = true;
    return false;
}

Run::Clock Run::stall(uint32_t cycle, uint32_t target, uint32_t pc, bool untilIdle)
{
    bool kicking = true;
    while (cycle < target || (untilIdle && kicking))
    {
        ++cycle;
        if (kicking)
            kicking = progressKick(cycle, pc);
    }
    return {cycle, kicking};
}

Run::PendingFlags *Run::commitFlags(PendingFlags *head, PendingFlags *tail, uint32_t upTo)
{
    // Results queue in issue order, so the due ones come first and only the
    // last few cycles' can still be waiting.
    PendingFlags *end = tail;
    while (end != head && end[-1].issue + 4u > upTo)
        --end;
    if (end == head)
        return head;

    // Usually every one is an FMAC result: the last sets MAC and the current
    // status bits, and all of them add to the sticky bits. That loop vectorizes.
    uint32_t conditions = 0u, other = 0u;
    for (const PendingFlags *entry = head; entry != end; ++entry)
    {
        conditions |= entry->value | entry->sticky;
        other |= entry->writes ^ 3u;
    }
    if (other == 0u)
    {
        const uint32_t now = statusOf(end[-1].value);
        m_state.mac = end[-1].value;
        m_state.status = (m_state.status & 0xFF0u) | now | ((now | statusOf(conditions)) << 6u);
        return end;
    }

    // MAC and CLIP keep the last result; the sticky bits collect every FMAC
    // result until an FSSET, so status is folded once per run of those.
    uint32_t mac = m_state.mac, status = m_state.status, clip = m_state.clip;
    uint32_t current = 0u;
    conditions = 0u;
    bool pending = false;
    const auto fold = [&] {
        const uint32_t now = statusOf(current);
        status = (status & 0xFF0u) | now | ((now | statusOf(conditions)) << 6u);
        conditions = 0u;
        pending = false;
    };
    for (; head != end; ++head)
    {
        const PendingFlags &entry = *head;
        if ((entry.writes & 1u) != 0u)
            mac = entry.value;
        if ((entry.writes & 2u) != 0u)
        {
            current = entry.value;
            conditions |= entry.value | entry.sticky;
            pending = true;
        }
        if ((entry.writes & 4u) != 0u)
        {
            if (pending)
                fold();
            status = (status & 0x03Fu) | (entry.value & 0xFC0u);
            --m_fssetQueued;
        }
        if ((entry.writes & 8u) != 0u)
            clip = entry.value;
    }
    if (pending)
        fold();
    m_state.mac = mac;
    m_state.status = status;
    m_state.clip = clip;
    return head;
}

Run::FlagRange Run::makeRoom(PendingFlags *head, PendingFlags *tail, uint32_t cycle)
{
    // Only results from the last four cycles can still be pending.
    head = commitFlags(head, tail, cycle);
    const std::ptrdiff_t pending = tail - head;
    std::memmove(m_flags.data(), head, static_cast<size_t>(pending) * sizeof(PendingFlags));
    return {m_flags.data(), m_flags.data() + pending};
}

void Run::cancelSameCycle(PendingFlags *head, PendingFlags *tail, uint32_t cycle, uint32_t writes)
{
    // A later FSSET or FCSET in the same pair overrides that pair's result.
    for (; head != tail; ++head)
        if (head->issue == cycle)
            head->writes &= ~writes;
}

Run::PendingFlags *Run::commitQ(PendingFlags *head, PendingFlags *tail, uint32_t cycle)
{
    // Q and its D/I bits land after any flag result due in the same cycle.
    // FMAC results touch other status bits, so only an FSSET, which rewrites
    // the sticky D and I bits, has to be committed first.
    auto &fdiv = Access::fdiv(m_vu);
    if (!fdiv.valid || relative(fdiv.readyCycle) > cycle)
        return head;
    if (m_fssetQueued != 0u)
        head = commitFlags(head, tail, relative(fdiv.readyCycle));
    m_state.q = fdiv.value;
    const uint32_t currentDi = fdiv.statusDi & 0x30u;
    m_state.status = (m_state.status & 0xFCFu) | currentDi | (currentDi << 6u);
    fdiv = {};
    return head;
}

void Run::commitP(uint32_t cycle)
{
    // Oldest first. A slot freed early takes the next result, so after three
    // EFU operations in a row slot order is no longer issue order.
    auto &efu = Access::efu(m_vu);
    const bool swap = efu[0].valid && efu[1].valid && efu[1].readyCycle < efu[0].readyCycle;
    for (auto *entry : {&efu[swap ? 1 : 0], &efu[swap ? 0 : 1]})
        if (entry->valid && relative(entry->readyCycle) <= cycle)
        {
            m_state.p = entry->value;
            *entry = {};
        }
}

void Run::handBack()
{
    m_flagHead = commitQ(m_flagHead, m_flagTail, m_cycle);
    commitP(m_cycle);
    m_flagHead = commitFlags(m_flagHead, m_flagTail, m_cycle);
    for (const PendingFlags *pending = m_flagHead; pending != m_flagTail; ++pending)
    {
        // Back into the interpreter's form: status bits and product conditions folded.
        const PendingFlags &entry = *pending;
        const bool fmac = (entry.writes & 3u) != 0u;
        const uint32_t status = fmac ? statusOf(entry.value) : (entry.writes & 4u) != 0u ? entry.value : 0u;
        Access::pendFlags(m_vu, m_base + entry.issue, fmac ? entry.value : 0u, status,
                          fmac ? statusOf(entry.sticky) : 0u, (entry.writes & 8u) != 0u ? entry.value : 0u,
                          static_cast<uint8_t>(entry.writes));
    }
    for (uint8_t reg = 1u; reg < 32u; ++reg)
    {
        // Lanes written by one pair share a result cycle and an entry.
        uint8_t done = 0u;
        for (uint32_t component = 0; component < 4u; ++component)
        {
            const uint32_t ready = m_vfReady[reg][component];
            if (ready <= m_cycle || (done & lane(component)) != 0u)
                continue;
            uint8_t lanes = 0u;
            for (uint32_t other = component; other < 4u; ++other)
                if (m_vfReady[reg][other] == ready)
                    lanes |= lane(other);
            done |= lanes;
            Access::pendVf(m_vu, reg, lanes, m_base + ready);
        }
    }
    for (uint8_t reg = 1u; reg < 16u; ++reg)
        if (m_viReady[reg] > m_cycle)
            Access::pendVi(m_vu, reg, m_base + m_viReady[reg]);
    Access::finish(m_vu, m_base + m_cycle, m_workingClip, static_cast<uint8_t>(m_backupReg), m_backupValue,
                   m_pairs);
}

uint32_t Run::leave(uint32_t pc)
{
    m_state.pc = pc;
    handBack();
    m_finished = true;
    return pc;
}

uint32_t Run::leaveStale()
{
    // A branch or E bit issued by the last pair still takes effect after its delay slot.
    m_state.pc = m_stalePc;
    m_state.branchPending = m_staleBranch;
    if (m_staleBranch)
        m_state.branchTarget = m_staleTarget;
    m_state.branchDelay = 0u;
    m_state.ebit = m_staleEnding;
    handBack();
    m_finished = true;
    return m_stalePc;
}

uint32_t Run::end(uint32_t pc)
{
    m_state.pc = pc;
    handBack();
    m_finished = m_ended = true;
    return pc;
}

bool run(VU1Interpreter &vu, const uint8_t *code, uint32_t codeSize, uint64_t budgetEnd, Routine routine)
{
    Run program(vu);
    for (;;)
    {
        const uint32_t pc = routine(program, budgetEnd);
        if (program.finished())
            return program.ended();
        routine = find(code, codeSize, pc, program.generation());
        if (!routine)
        {
            recordMissing(code, codeSize, pc);
            program.leave(pc);
            return false;
        }
    }
}
}
