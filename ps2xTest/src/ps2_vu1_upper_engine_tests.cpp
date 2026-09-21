#include "MiniTest.h"
#include "runtime/gs/gs_frontend.h"
#include "runtime/ps2_memory.h"
#include "runtime/ps2_vu1.h"
#include "../../ps2xRuntime/src/lib/vu/ps2_vu1_upper_engine.h"

#include <array>
#include <cfenv>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {

struct EngineInputs {
    const VU1State &state;
    unsigned source, target;
    bool fast;
    float fs(unsigned lane) const { return state.vf[source][lane]; }
    float ft(unsigned lane) const { return state.vf[target][lane]; }
    float acc(unsigned lane) const { return state.acc[lane]; }
    float i() const { return state.i; }
    float q() const { return state.q; }
    bool compiledFastPath() const { return fast; }
};

struct EngineSink {
    VU1State &state;
    unsigned fmacCalls = 0u, clipCalls = 0u, vfCalls = 0u, accCalls = 0u, reservedCalls = 0u;
    static void write(float *out, uint8_t mask, const float *values) {
        for (unsigned lane = 0; lane < 4u; ++lane)
            if (mask & (8u >> lane)) out[lane] = values[lane];
    }
    void vf(uint8_t reg, uint8_t mask, const float *values) {
        ++vfCalls;
        write(state.vf[reg], mask, values);
    }
    void acc(uint8_t mask, const float *values) {
        ++accCalls;
        write(state.acc, mask, values);
    }
    void fmac(uint32_t mac, uint32_t status, uint32_t extraSticky) {
        ++fmacCalls;
        state.mac = mac;
        state.status = (state.status & 0xff0u) | (status & 15u) |
                       (((status & 15u) | extraSticky) << 6u);
    }
    void clip(uint32_t sixBits) {
        ++clipCalls;
        state.clip = ((state.clip << 6u) | (sixBits & 63u)) & 0xffffffu;
    }
    void reserved(uint32_t) { ++reservedCalls; }
};

float rawFloat(uint32_t bits) {
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

VU1State initialState(unsigned seed) {
    constexpr uint32_t values[] = {
        0u, 0x80000000u, 1u, 0x807fffffu, 0x00800000u, 0x80800001u,
        0x3f800000u, 0xbf800000u, 0x3f000000u, 0xbf7fffffu,
        0x7f7fffffu, 0xff7fffffu, 0x7f800000u, 0xff800000u,
        0x7fc12345u, 0xffc12345u, 0x4f000000u, 0xcf000000u,
        0x00800001u, 0x3eaaaaabu, 0x7f000000u, 0x00400000u
    };
    VU1State state{};
    for (unsigned reg = 1u; reg < 32u; ++reg)
        for (unsigned lane = 0; lane < 4u; ++lane)
            state.vf[reg][lane] = rawFloat(values[(seed * 5u + reg * 3u + lane * 7u) % std::size(values)]);
    state.vf[0][3] = 1.0f;
    for (unsigned lane = 0; lane < 4u; ++lane)
        state.acc[lane] = rawFloat(values[(seed * 7u + lane * 3u) % std::size(values)]);
    state.i = rawFloat(values[(seed * 3u + 6u) % std::size(values)]);
    state.q = rawFloat(values[(seed * 5u + 7u) % std::size(values)]);
    if (seed == 6u) {
        const float left[] = {1.0f, -1.0f, std::numeric_limits<float>::min(), -std::numeric_limits<float>::min()};
        const float right[] = {1.0f, -1.0f, 2.0f, -2.0f};
        std::memcpy(state.vf[1], left, sizeof(left));
        std::memcpy(state.vf[2], right, sizeof(right));
        for (unsigned lane = 0; lane < 4u; ++lane)
            state.acc[lane] = -(left[lane] * right[lane]);
    }
    state.mac = 0x1357u;
    state.status = 0xa50u;
    state.clip = 0x36c9a5u;
    return state;
}

bool equalOutputs(const VU1State &a, const VU1State &b) {
    return std::memcmp(a.vf, b.vf, sizeof(a.vf)) == 0 &&
           std::memcmp(a.acc, b.acc, sizeof(a.acc)) == 0 &&
           a.mac == b.mac && a.status == b.status && a.clip == b.clip;
}

} // namespace

void register_ps2_vu1_upper_engine_tests() {
    MiniTest::Case("PS2VU1UpperEngine", [](TestCase &tc) {
        tc.Run("private arithmetic matches raw scalar execution for all upper opcodes", [](TestCase &t) {
            PS2Memory memory;
            t.IsTrue(memory.initialize(), "memory initializes");
            GS gs;
            gs.init(memory.getGSVRAM(), static_cast<uint32_t>(PS2_GS_VRAM_SIZE), &memory.gs());
            constexpr uint32_t nop = 0x2ffu;
            std::array<uint32_t, 8> code{0u, nop, 0u, nop, 0u, nop, 0u, nop};
            const int oldRounding = std::fegetround();
            struct RestoreRounding { int mode; ~RestoreRounding() { std::fesetround(mode); } } restore{oldRounding};
            std::fesetround(FE_TOWARDZERO);
            unsigned comparisons = 0u;
            for (unsigned special = 0; special < 2u; ++special) {
                for (unsigned op = 0; op < (special ? 0x31u : 0x30u); ++op) {
                    if (special && op == 0x2bu) continue;
                    for (unsigned mask = 0; mask < 16u; ++mask) {
                        for (unsigned seed = 0; seed < 8u; ++seed) {
                            for (unsigned alias = 0; alias < 4u; ++alias) {
                                const unsigned fs = 1u;
                                const unsigned ft = special && alias == 1u ? 1u : special && alias == 3u ? 0u : 2u;
                                const unsigned fd = alias == 0u ? 3u : alias == 1u ? fs : alias == 2u ? ft : 0u;
                                const uint32_t instr = (mask << 21u) | (ft << 16u) | (fs << 11u) |
                                    (special ? ((op & 0x7cu) << 4u) | (op & 3u) | 0x3cu : (fd << 6u) | op);
                                code[1] = instr;
                                const VU1State initial = initialState(seed);
                                VU1Interpreter oracle;
                                oracle.setCompiledExecutionEnabled(false);
                                oracle.state() = initial;
                                oracle.execute(reinterpret_cast<uint8_t *>(code.data()), sizeof(code),
                                    memory.getVU1Data(), PS2_VU1_DATA_SIZE, gs, &memory, 0u, 0u, 0u, 4u);
                                for (bool fast : {false, true}) {
                                    VU1State output = initial;
                                    EngineInputs inputs{output, fs, ft, fast};
                                    EngineSink sink{output};
                                    ps2_vu_detail::upper::computeUpper(instr, inputs, sink);
                                    output.vf[0][0] = output.vf[0][1] = output.vf[0][2] = 0.0f;
                                    output.vf[0][3] = 1.0f;
                                    if (sink.reservedCalls || !equalOutputs(output, oracle.state())) {
                                        char message[180];
                                        std::snprintf(message, sizeof(message),
                                            "opcode=%08x seed=%u alias=%u fast=%u mac=%x/%x status=%x/%x clip=%x/%x",
                                            instr, seed, alias, fast, output.mac, oracle.state().mac,
                                            output.status, oracle.state().status, output.clip, oracle.state().clip);
                                        t.Fail(message);
                                        return;
                                    }
                                    if (mask == 0u && sink.fmacCalls != 0u) {
                                        t.Fail("an empty destination must not publish FMAC flags");
                                        return;
                                    }
                                    ++comparisons;
                                }
                            }
                        }
                    }
                }
            }
            t.Equals(comparisons, 98304u, "all opcode/mask/operand/alias/fast-path cases ran");
        });
        tc.Run("NOP has no operand reads or publication", [](TestCase &t) {
            struct Inputs {
                mutable unsigned reads = 0u;
                float fs(unsigned) const { ++reads; return 0.0f; }
                float ft(unsigned) const { ++reads; return 0.0f; }
                float acc(unsigned) const { ++reads; return 0.0f; }
                float i() const { ++reads; return 0.0f; }
                float q() const { ++reads; return 0.0f; }
                bool compiledFastPath() const { ++reads; return true; }
            } inputs;
            VU1State state{};
            EngineSink sink{state};
            ps2_vu_detail::upper::computeUpper(0x2ffu, inputs, sink);
            ps2_vu_detail::upper::computeUpper(0x33cu, inputs, sink);
            t.Equals(inputs.reads, 0u, "both NOP encodings leave operands unread");
            t.Equals(sink.vfCalls + sink.accCalls + sink.fmacCalls + sink.clipCalls + sink.reservedCalls,
                     0u, "NOP does not publish a result");
        });
    });
}
