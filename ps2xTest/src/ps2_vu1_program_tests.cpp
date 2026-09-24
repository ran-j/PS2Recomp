#include "MiniTest.h"
#include "runtime/gs/gs_frontend.h"
#include "runtime/gs/gs_types.h"
#include "runtime/ps2_memory.h"
#include "runtime/ps2_vu1.h"
#include "../../ps2xRuntime/src/lib/vu/ps2_vu1_program.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

// Compiled VU1 routines against the interpreter, on the vu_programs.py programs
// the runtime was built with, named by PS2_VU_PROGRAM_FIXTURES. With
// PS2_VU_REQUIRE_PROGRAMS, an entry without a routine fails.

namespace
{
    constexpr uint32_t kWideBudget = 65536u;
    constexpr uint32_t kKickQword = 0x3F0u;

    struct ProgramEntry
    {
        std::string name;
        uint32_t pc = 0u;
        std::vector<uint8_t> code;
    };

    std::vector<ProgramEntry> loadEntries(std::string &error)
    {
        std::vector<ProgramEntry> entries;
        const char *list = std::getenv("PS2_VU_PROGRAM_FIXTURES");
        std::stringstream directories(list ? list : "");
        std::string directory;
        while (std::getline(directories, directory, ';'))
        {
            if (directory.empty())
                continue;
            std::ifstream listing(directory + "/entries.txt");
            if (!listing)
            {
                error = "cannot read " + directory + "/entries.txt";
                return {};
            }
            ProgramEntry entry;
            while (listing >> entry.name >> std::hex >> entry.pc)
            {
                std::ifstream image(directory + "/vu1-" + entry.name + ".code", std::ios::binary);
                entry.code.assign(PS2_VU1_CODE_SIZE, 0u);
                if (!image.read(reinterpret_cast<char *>(entry.code.data()), PS2_VU1_CODE_SIZE))
                {
                    error = "cannot read the code image " + entry.name;
                    return {};
                }
                entries.push_back(entry);
            }
        }
        return entries;
    }

    // Ordinary values, with zeros, a host denormal and a huge value mixed in.
    float pattern(uint32_t n)
    {
        switch (n % 16u)
        {
        case 3u:
            return 0.0f;
        case 7u:
            return -0.0f;
        case 10u:
            return 1e-39f;
        case 13u:
            return 3.0e38f;
        default:
            return (static_cast<float>(n % 29u) - 14.0f) * 0.625f + static_cast<float>(n / 16u);
        }
    }

    VU1State initialState()
    {
        VU1State state{};
        for (uint32_t reg = 1u; reg < 32u; ++reg)
            for (uint32_t lane = 0u; lane < 4u; ++lane)
                state.vf[reg][lane] = pattern(reg * 4u + lane);
        state.vf[0][3] = 1.0f;
        // Only FMAC results reach ACC, so it never holds a denormal, an
        // infinity or a NaN, and the compiled product sums rely on that.
        const float acc[4] = {-7.25f, 0.0f, 3.0e38f, -0.0f};
        std::memcpy(state.acc, acc, sizeof(acc));
        state.q = 1.5f;
        state.p = 0.75f;
        state.i = 2.0f;
        state.r = 0x3F812345u;
        return state;
    }

    // Floats everywhere but the GIF packet the programs kick.
    std::vector<uint8_t> initialData()
    {
        std::vector<uint8_t> data(PS2_VU1_DATA_SIZE, 0u);
        for (uint32_t qword = 0u; qword < kKickQword; ++qword)
            for (uint32_t lane = 0u; lane < 4u; ++lane)
            {
                const float value = pattern(1000u + qword * 4u + lane);
                std::memcpy(&data[qword * 16u + lane * 4u], &value, sizeof(value));
            }
        const uint64_t tag = 2u | (1ull << 15) | (static_cast<uint64_t>(GIF_FMT_IMAGE) << 58);
        std::memcpy(&data[kKickQword * 16u], &tag, sizeof(tag));
        for (uint32_t byte = 0u; byte < 32u; ++byte)
            data[(kKickQword + 1u) * 16u + byte] = static_cast<uint8_t>(0xA0u + byte);
        return data;
    }

    struct Side
    {
        PS2Memory mem;
        GS gs;
        VU1Interpreter vu;
        std::vector<std::vector<uint8_t>> packets;

        bool initialize(const std::vector<uint8_t> &code, bool compiled)
        {
            if (!mem.initialize())
                return false;
            gs.init(mem.getGSVRAM(), static_cast<uint32_t>(PS2_GS_VRAM_SIZE), &mem.gs());
            std::memcpy(mem.getVU1Code(), code.data(), PS2_VU1_CODE_SIZE);
            mem.markVU1CodeModified();
            mem.setGifPacketCallback([this](const uint8_t *data, uint32_t size)
                                     { packets.emplace_back(data, data + size); });
            vu.setCompiledExecutionEnabled(compiled);
            return true;
        }

        void reset(const VU1State &initial, const std::vector<uint8_t> &data)
        {
            vu.reset();
            vu.state() = initial;
            std::memcpy(mem.getVU1Data(), data.data(), PS2_VU1_DATA_SIZE);
            packets.clear();
        }

        void start(uint32_t pc, uint32_t cycles)
        {
            vu.execute(mem.getVU1Code(), PS2_VU1_CODE_SIZE, mem.getVU1Data(), PS2_VU1_DATA_SIZE, gs, &mem,
                       pc, 0u, 0u, cycles);
        }

        void resume(uint32_t cycles)
        {
            vu.resume(mem.getVU1Code(), PS2_VU1_CODE_SIZE, mem.getVU1Data(), PS2_VU1_DATA_SIZE, gs, &mem,
                      0u, 0u, cycles);
        }

        uint64_t cycles() const { return vu.state().cycles; }
    };

    bool same(Side &native, Side &oracle)
    {
        return std::memcmp(&native.vu.state(), &oracle.vu.state(), sizeof(VU1State)) == 0 &&
               std::memcmp(native.mem.getVU1Data(), oracle.mem.getVU1Data(), PS2_VU1_DATA_SIZE) == 0 &&
               native.packets == oracle.packets;
    }

    std::string describe(const ProgramEntry &entry, uint64_t budget, const Side &oracle)
    {
        std::ostringstream text;
        text << entry.name << " entry 0x" << std::hex << entry.pc << std::dec << ", budget " << budget
             << ", cycle " << oracle.cycles();
        return text.str();
    }

    // What differs, compiled first, so that a failure says where to look.
    std::string difference(Side &native, Side &oracle)
    {
        const VU1State &a = native.vu.state(), &b = oracle.vu.state();
        std::ostringstream text;
        const auto differs = [](const void *x, const void *y, size_t size) { return std::memcmp(x, y, size) != 0; };
        const auto both = [&](const char *name, uint64_t x, uint64_t y) {
            if (x != y)
                text << ' ' << name << " 0x" << std::hex << x << "/0x" << y << std::dec;
        };
        both("pc", a.pc, b.pc);
        both("cycles", a.cycles, b.cycles);
        both("mac", a.mac, b.mac);
        both("status", a.status, b.status);
        both("clip", a.clip, b.clip);
        both("ebit", a.ebit, b.ebit);
        both("branch", a.branchPending, b.branchPending);
        both("target", a.branchTarget, b.branchTarget);
        both("delay", a.branchDelay, b.branchDelay);
        bool shown = false;
        for (uint32_t reg = 0u; reg < 32u; ++reg)
            if (differs(a.vf[reg], b.vf[reg], sizeof(a.vf[reg])))
            {
                text << " vf" << reg;
                // The first one in full, as bit patterns.
                for (const VU1State *state : {shown ? nullptr : &a, shown ? nullptr : &b})
                    if (state)
                    {
                        uint32_t bits[4];
                        std::memcpy(bits, state->vf[reg], sizeof(bits));
                        text << (state == &a ? " (" : " / ") << std::hex << bits[0] << ' ' << bits[1] << ' '
                             << bits[2] << ' ' << bits[3] << std::dec << (state == &a ? "" : ")");
                    }
                shown = true;
            }
        for (uint32_t reg = 0u; reg < 16u; ++reg)
            both(("vi" + std::to_string(reg)).c_str(), static_cast<uint32_t>(a.vi[reg]), static_cast<uint32_t>(b.vi[reg]));
        if (differs(a.acc, b.acc, sizeof(a.acc)))
            text << " acc";
        for (const auto &[name, x, y] : {std::tuple{"q", &a.q, &b.q}, {"p", &a.p, &b.p}, {"i", &a.i, &b.i}})
            if (differs(x, y, sizeof(float)))
                text << ' ' << name;
        for (uint32_t qword = 0u; qword < PS2_VU1_DATA_SIZE / 16u; ++qword)
            if (differs(native.mem.getVU1Data() + qword * 16u, oracle.mem.getVU1Data() + qword * 16u, 16u))
            {
                text << " data qword 0x" << std::hex << qword << std::dec;
                break;
            }
        both("packets", native.packets.size(), oracle.packets.size());
        return text.str();
    }

    // Record a failure like IsTrue, and say whether to go on: after the first
    // difference the rest of an entry only repeats it.
    bool check(TestCase &t, bool condition, const std::string &message)
    {
        t.IsTrue(condition, message);
        return condition;
    }

    bool matches(TestCase &t, Side &native, Side &oracle, const std::string &message)
    {
        if (same(native, oracle))
            return true;
        t.Fail(message + " (compiled/interpreted:" + difference(native, oracle) + ")");
        return false;
    }

    void checkEntry(TestCase &t, const ProgramEntry &entry, bool require)
    {
        Side native, oracle;
        if (!native.initialize(entry.code, true) || !oracle.initialize(entry.code, false))
        {
            t.Fail("VU1 fixtures should initialize");
            return;
        }
        if (require)
            t.IsTrue(ps2_vu_program::find(native.mem.getVU1Code(), PS2_VU1_CODE_SIZE, entry.pc,
                                          native.mem.getVU1CodeGeneration()) != nullptr,
                     describe(entry, 0u, oracle) + ": the entry has a compiled routine");
        const VU1State initial = initialState();
        const std::vector<uint8_t> data = initialData();

        // One uninterrupted run gives the cycle the program ends on.
        oracle.reset(initial, data);
        native.reset(initial, data);
        oracle.start(entry.pc, kWideBudget);
        native.start(entry.pc, kWideBudget);
        const uint64_t end = oracle.cycles();
        if (!check(t, end < kWideBudget, describe(entry, kWideBudget, oracle) + ": the program ends") ||
            !matches(t, native, oracle, describe(entry, kWideBudget, oracle) + ": uninterrupted run"))
            return;
        if (require)
            t.IsTrue(native.vu.compiledPairsExecuted() != 0u, describe(entry, kWideBudget, oracle) + ": ran compiled");
        const VU1State finalState = oracle.vu.state();
        const std::vector<uint8_t> finalData(oracle.mem.getVU1Data(), oracle.mem.getVU1Data() + PS2_VU1_DATA_SIZE);
        const auto finalPackets = oracle.packets;

        // A budget the program outlasts stops it at a block boundary and the
        // interpreter takes over; then the rest in uneven slices.
        std::vector<uint64_t> budgets;
        if (end > ps2_vu_program::kMinimumBudget)
        {
            const uint64_t span = end - ps2_vu_program::kMinimumBudget;
            budgets = {ps2_vu_program::kMinimumBudget, ps2_vu_program::kMinimumBudget + span / 3u,
                       ps2_vu_program::kMinimumBudget + span * 2u / 3u, end - 1u};
            budgets.erase(std::unique(budgets.begin(), budgets.end()), budgets.end());
        }
        for (const uint64_t budget : budgets)
        {
            oracle.reset(initial, data);
            native.reset(initial, data);
            oracle.start(entry.pc, static_cast<uint32_t>(budget));
            native.start(entry.pc, static_cast<uint32_t>(budget));
            if (!matches(t, native, oracle, describe(entry, budget, oracle) + ": stopped by the budget"))
                return;
            for (uint32_t slice : {1u, 2u, 3u, 5u, 8u, 13u, kWideBudget})
            {
                while (oracle.cycles() < end)
                {
                    const uint32_t cycles = static_cast<uint32_t>(std::min<uint64_t>(slice, end - oracle.cycles()));
                    oracle.resume(cycles);
                    native.resume(cycles);
                    if (!matches(t, native, oracle, describe(entry, budget, oracle) + ": resumed"))
                        return;
                    if (slice != kWideBudget)
                        break;
                }
            }
            t.IsTrue(std::memcmp(&oracle.vu.state(), &finalState, sizeof(VU1State)) == 0 &&
                         std::memcmp(oracle.mem.getVU1Data(), finalData.data(), PS2_VU1_DATA_SIZE) == 0 &&
                         oracle.packets == finalPackets,
                     describe(entry, budget, oracle) + ": sliced run ends like the uninterrupted one");
        }
    }
}

void register_ps2_vu1_program_tests()
{
    MiniTest::Case("PS2VU1Programs", [](TestCase &tc)
    {
        tc.Run("compiled routines match the interpreter on authored programs", [](TestCase &t)
        {
            std::string error;
            const auto entries = loadEntries(error);
            t.IsTrue(error.empty(), error);
            const bool require = std::getenv("PS2_VU_REQUIRE_PROGRAMS") != nullptr;
            if (require)
                t.IsTrue(!entries.empty(), "PS2_VU_PROGRAM_FIXTURES names the compiled programs");
            for (const auto &entry : entries)
                checkEntry(t, entry, require);
        });
    });
}
