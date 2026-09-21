#include "MiniTest.h"
#include "runtime/gs/ps2_gif_arbiter.h"
#include "runtime/gs/gs_frontend.h"
#include "runtime/gs/ps2_gs_psmct32.h"
#include "runtime/ps2_memory.h"
#include "runtime/ps2_vu1.h"
#include "../../ps2xRuntime/src/lib/vu/ps2_vu1_fmac.h"
#include "../../ps2xRuntime/src/lib/vu/ps2_vu1_fmac_neon.h"
#include "../../ps2xRuntime/src/lib/vu/ps2_vu1_region.h"
#include "../../ps2xRuntime/src/lib/vu/ps2_vu1_loop_plan.h"

#include <cfenv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace
{
    constexpr uint32_t kVuUpperNop = 0x000002FFu;

    struct Vu1Fixture
    {
        PS2Memory mem;
        GS gs;
        uint8_t *code = nullptr;
        uint8_t *data = nullptr;

        bool initialize()
        {
            if (!mem.initialize())
                return false;
            gs.init(mem.getGSVRAM(), static_cast<uint32_t>(PS2_GS_VRAM_SIZE), &mem.gs());
            code = mem.getVU1Code();
            data = mem.getVU1Data();
            std::memset(code, 0, PS2_VU1_CODE_SIZE);
            std::memset(data, 0, PS2_VU1_DATA_SIZE);
            return code != nullptr && data != nullptr;
        }
    };

    uint32_t makeVifCmd(uint8_t opcode, uint8_t num, uint16_t imm)
    {
        return (static_cast<uint32_t>(opcode) << 24) |
               (static_cast<uint32_t>(num) << 16) |
               static_cast<uint32_t>(imm);
    }

    uint64_t makeGifTag(uint16_t nloop, uint8_t flg, uint8_t nreg, bool eop = true)
    {
        uint64_t tag = static_cast<uint64_t>(nloop & 0x7FFFu);
        if (eop)
            tag |= (1ull << 15);
        tag |= (static_cast<uint64_t>(flg & 0x3u) << 58);
        tag |= (static_cast<uint64_t>(nreg & 0xFu) << 60);
        return tag;
    }

    uint32_t makeVuLowerSpecial(uint8_t specialOp, uint8_t is, uint8_t it = 0u, uint8_t id = 0u, uint8_t dest = 0u)
    {
        return (0x40u << 25) |
               (static_cast<uint32_t>(dest & 0xFu) << 21) |
               (static_cast<uint32_t>(it & 0x1Fu) << 16) |
               (static_cast<uint32_t>(is & 0x1Fu) << 11) |
               (static_cast<uint32_t>(id & 0x1Fu) << 6) |
               (static_cast<uint32_t>(specialOp & 0x7Cu) << 4) |
               static_cast<uint32_t>(specialOp & 0x3u) |
               0x3Cu;
    }

    uint32_t makeVuLowerDirect(uint8_t funct, uint8_t is, uint8_t it = 0u, uint8_t id = 0u, uint8_t dest = 0u)
    {
        return (0x40u << 25) |
               (static_cast<uint32_t>(dest & 0xFu) << 21) |
               (static_cast<uint32_t>(it & 0x1Fu) << 16) |
               (static_cast<uint32_t>(is & 0x1Fu) << 11) |
               (static_cast<uint32_t>(id & 0x1Fu) << 6) |
               static_cast<uint32_t>(funct & 0x3Fu);
    }

    uint32_t makeVuUpper(uint8_t op, uint8_t dest, uint8_t ft, uint8_t fs, uint8_t fd)
    {
        return (static_cast<uint32_t>(dest & 0xFu) << 21) |
               (static_cast<uint32_t>(ft & 0x1Fu) << 16) |
               (static_cast<uint32_t>(fs & 0x1Fu) << 11) |
               (static_cast<uint32_t>(fd & 0x1Fu) << 6) |
               static_cast<uint32_t>(op & 0x3Fu);
    }

    uint32_t makeVuUpperSpecial(uint8_t specialOp, uint8_t dest, uint8_t ft, uint8_t fs)
    {
        return (static_cast<uint32_t>(dest & 0xFu) << 21) |
               (static_cast<uint32_t>(ft & 0x1Fu) << 16) |
               (static_cast<uint32_t>(fs & 0x1Fu) << 11) |
               (static_cast<uint32_t>(specialOp & 0x7Cu) << 4) |
               static_cast<uint32_t>(specialOp & 0x3u) |
               0x3Cu;
    }

    uint32_t makeVuFlagImmediate(uint8_t opcode, uint8_t targetVi, uint16_t immediate)
    {
        return (static_cast<uint32_t>(opcode & 0x7Fu) << 25) |
               (static_cast<uint32_t>((immediate >> 11) & 0x1u) << 21) |
               (static_cast<uint32_t>(targetVi & 0xFu) << 16) |
               static_cast<uint32_t>(immediate & 0x7FFu);
    }

    uint32_t makeVuFlagRegister(uint8_t opcode, uint8_t targetVi, uint8_t sourceVi)
    {
        return (static_cast<uint32_t>(opcode & 0x7Fu) << 25) |
               (static_cast<uint32_t>(targetVi & 0xFu) << 16) |
               (static_cast<uint32_t>(sourceVi & 0xFu) << 11);
    }

    uint32_t makeVuLq(uint8_t dest, uint8_t targetVf, uint8_t baseVi, int16_t imm)
    {
        return (static_cast<uint32_t>(dest & 0xFu) << 21) |
               (static_cast<uint32_t>(targetVf & 0x1Fu) << 16) |
               (static_cast<uint32_t>(baseVi & 0xFu) << 11) |
               (static_cast<uint32_t>(imm) & 0x7FFu);
    }

    uint32_t makeVuSq(uint8_t dest, uint8_t sourceVf, uint8_t baseVi, int16_t imm)
    {
        return (0x01u << 25) |
               (static_cast<uint32_t>(dest & 0xFu) << 21) |
               (static_cast<uint32_t>(baseVi & 0xFu) << 16) |
               (static_cast<uint32_t>(sourceVf & 0x1Fu) << 11) |
               (static_cast<uint32_t>(imm) & 0x7FFu);
    }

    uint32_t makeVuIaddiu(uint8_t it, uint8_t is, int16_t imm)
    {
        return (0x08u << 25) |
               (static_cast<uint32_t>(it & 0xFu) << 16) |
               (static_cast<uint32_t>(is & 0xFu) << 11) |
               (static_cast<uint32_t>(imm) & 0x7FFu);
    }

    uint32_t makeVuBranch(int16_t imm)
    {
        return (0x20u << 25) | (static_cast<uint32_t>(imm) & 0x7FFu);
    }

    uint32_t makeVuJr(uint8_t is)
    {
        return (0x24u << 25) |
               (static_cast<uint32_t>(is & 0xFu) << 11);
    }

    uint32_t makeVuIbne(uint8_t is, uint8_t it, int16_t imm)
    {
        return (0x29u << 25) |
               (static_cast<uint32_t>(it & 0xFu) << 16) |
               (static_cast<uint32_t>(is & 0xFu) << 11) |
               (static_cast<uint32_t>(imm) & 0x7FFu);
    }

    uint32_t makeVuIlw(uint8_t dest, uint8_t targetVi, uint8_t baseVi, int16_t imm)
    {
        return (0x04u << 25) |
               (static_cast<uint32_t>(dest & 0xFu) << 21) |
               (static_cast<uint32_t>(targetVi & 0xFu) << 16) |
               (static_cast<uint32_t>(baseVi & 0xFu) << 11) |
               (static_cast<uint32_t>(imm) & 0x7FFu);
    }

    uint32_t makeVuDiv(uint8_t fs, uint8_t ft, uint8_t fsf, uint8_t ftf)
    {
        return makeVuLowerSpecial(0x38u, fs, ft, 0u, static_cast<uint8_t>(((ftf & 0x3u) << 2) | (fsf & 0x3u)));
    }

    uint32_t makeVuSqrt(uint8_t ft, uint8_t ftf)
    {
        return makeVuLowerSpecial(0x39u, 0u, ft, 0u, static_cast<uint8_t>((ftf & 0x3u) << 2));
    }

    void writeVuInstructionPair(uint8_t *code, uint32_t pc, uint32_t lower, uint32_t upper)
    {
        std::memcpy(code + pc, &lower, sizeof(lower));
        std::memcpy(code + pc + sizeof(lower), &upper, sizeof(upper));
    }

    uint64_t packVuInstructionPair(uint32_t lower, uint32_t upper)
    {
        return static_cast<uint64_t>(lower) | (static_cast<uint64_t>(upper) << 32);
    }

    void writeTrackedVuInstructionPair(Vu1Fixture &fx, uint32_t pc, uint32_t lower, uint32_t upper)
    {
        fx.mem.write64(PS2_VU1_CODE_BASE + pc, packVuInstructionPair(lower, upper));
    }

    void appendU32(std::vector<uint8_t> &bytes, uint32_t value)
    {
        const uint8_t *src = reinterpret_cast<const uint8_t *>(&value);
        bytes.insert(bytes.end(), src, src + sizeof(value));
    }

    void uploadVu1Mpg(PS2Memory &mem, uint16_t instructionAddress, uint32_t lower, uint32_t upper)
    {
        std::vector<uint8_t> packet;
        appendU32(packet, makeVifCmd(0x4Au, 1u, instructionAddress));
        appendU32(packet, lower);
        appendU32(packet, upper);
        mem.processVIF1Data(packet.data(), static_cast<uint32_t>(packet.size()));
    }

    void writeCountedLoopCode(Vu1Fixture &fx, uint32_t base = 0u)
    {
        writeVuInstructionPair(fx.code, base + 0u, makeVuLq(14u, 27u, 10u, 3),
            makeVuUpper(0x2bu, 14u, 21u, 21u, 28u));
        writeVuInstructionPair(fx.code, base + 8u, makeVuLq(14u, 25u, 11u, 4),
            makeVuUpper(0x2au, 14u, 16u, 26u, 20u));
        writeVuInstructionPair(fx.code, base + 16u, makeVuLq(12u, 23u, 12u, 4),
            makeVuUpper(0x2au, 14u, 17u, 24u, 21u));
        writeVuInstructionPair(fx.code, base + 24u, makeVuLowerSpecial(0x35u, 19u, 12u, 0u, 12u),
            makeVuUpper(0x2au, 12u, 18u, 22u, 19u));
        writeVuInstructionPair(fx.code, base + 32u, makeVuLowerSpecial(0x35u, 28u, 11u, 0u, 14u),
            makeVuUpperSpecial(0x13u, 14u, 26u, 27u));
        writeVuInstructionPair(fx.code, base + 40u, makeVuIbne(13u, 10u, -6),
            makeVuUpperSpecial(0x13u, 14u, 24u, 25u));
        writeVuInstructionPair(fx.code, base + 48u, makeVuLowerSpecial(0x35u, 20u, 10u, 0u, 14u),
            makeVuUpperSpecial(0x13u, 12u, 22u, 23u));
    }

    void writeEarlyCounterLoopCode(Vu1Fixture &fx)
    {
        writeVuInstructionPair(fx.code, 0u, makeVuLowerSpecial(0x34u, 10u, 26u, 0u, 14u),
            makeVuUpperSpecial(0x09u, 14u, 23u, 19u));
        writeVuInstructionPair(fx.code, 8u, 0x8000033cu,
            makeVuUpperSpecial(0x0au, 14u, 23u, 20u));
        writeVuInstructionPair(fx.code, 16u, 0x8000033cu,
            makeVuUpper(0x0bu, 14u, 23u, 21u, 25u));
        writeVuInstructionPair(fx.code, 24u, 0x8000033cu,
            makeVuUpper(0x10u, 15u, 0u, 24u, 23u));
        writeVuInstructionPair(fx.code, 32u, makeVuLowerSpecial(0x30u, 22u, 25u, 0u, 1u),
            makeVuUpperSpecial(0x18u, 15u, 26u, 15u));
        writeVuInstructionPair(fx.code, 40u, makeVuIaddiu(11u, 11u, 5),
            makeVuUpperSpecial(0x09u, 15u, 26u, 16u));
        writeVuInstructionPair(fx.code, 48u, 0x8000033cu,
            makeVuUpper(0x0au, 15u, 26u, 17u, 24u));
        writeVuInstructionPair(fx.code, 56u, makeVuIbne(12u, 11u, -8),
            makeVuUpperSpecial(0x1bu, 14u, 0u, 22u));
        writeVuInstructionPair(fx.code, 64u, makeVuSq(15u, 25u, 11u, -7),
            makeVuUpperSpecial(0x08u, 14u, 23u, 18u));
    }

    void writeVuQword(uint8_t *data, uint32_t qwordIndex, const float values[4])
    {
        std::memcpy(data + qwordIndex * 16u, values, sizeof(float) * 4u);
    }

    void readVuQword(const uint8_t *data, uint32_t qwordIndex, float values[4])
    {
        std::memcpy(values, data + qwordIndex * 16u, sizeof(float) * 4u);
    }

    void writeTerminalBranchFixture(uint8_t *code, uint32_t start, uint8_t opcode,
                                    uint8_t previousVi, int16_t displacement)
    {
        writeVuInstructionPair(code, start, makeVuIlw(8u, 8u, 0u, 3),
            makeVuUpper(0x28u, 12u, 17u, 16u, 18u));
        writeVuInstructionPair(code, start + 8u, makeVuIaddiu(previousVi, 0u, 9),
            makeVuUpper(0x29u, 3u, 17u, 16u, 19u));
        writeVuInstructionPair(code, start + 16u,
            (uint32_t{opcode} << 25u) | (4u << 16u) | (5u << 11u) | (uint32_t(displacement) & 0x7ffu),
            makeVuUpper(0x2au, 12u, 17u, 18u, 20u));
        writeVuInstructionPair(code, start + 24u, makeVuLowerSpecial(0x35u, 18u, 6u, 0u, 12u),
            makeVuUpperSpecial(0x1fu, 14u, 21u, 22u));
    }
}

void register_ps2_vu1_tests()
{
    MiniTest::Case("PS2VU1", [](TestCase &tc)
    {
        tc.Run("terminal conditional regions preserve branch bypass and delayed tails", [](TestCase &t)
        {
            for (auto unit : {VU1Interpreter::Unit::VU0, VU1Interpreter::Unit::VU1})
            {
                Vu1Fixture candidateMemory, oracleMemory;
                t.IsTrue(candidateMemory.initialize() && oracleMemory.initialize(), "branch fixtures must initialize");
                const uint32_t codeSize = unit == VU1Interpreter::Unit::VU1 ? PS2_VU1_CODE_SIZE : PS2_VU0_CODE_SIZE;
                const uint32_t dataSize = unit == VU1Interpreter::Unit::VU1 ? PS2_VU1_DATA_SIZE : PS2_VU0_DATA_SIZE;
                if (unit == VU1Interpreter::Unit::VU0)
                    for (auto *fx : {&candidateMemory, &oracleMemory})
                    {
                        fx->code = fx->mem.getVU0Code();
                        fx->data = fx->mem.getVU0Data();
                    }
                for (uint8_t opcode : {0x28u, 0x29u})
                    for (uint8_t previousVi : {4u, 5u})
                        for (int16_t displacement : {3, -7})
                            for (uint32_t start : {64u, codeSize - 32u})
                                for (bool equal : {false, true})
                                    for (bool incomingWrite : {false, true})
                                        for (uint32_t budget : {0u, 1u, 2u, 4u, 5u, 6u, 7u, 9u})
                                        {
                                            for (auto *fx : {&candidateMemory, &oracleMemory})
                                            {
                                                for (uint32_t pc = 0; pc < codeSize; pc += 8u)
                                                    writeVuInstructionPair(fx->code, pc, 0x8000033cu, kVuUpperNop);
                                                writeTerminalBranchFixture(fx->code, start, opcode, previousVi, displacement);
                                                const uint32_t target = (start + 24u + displacement * 8u) & (codeSize - 1u);
                                                for (uint32_t next : {(start + 32u) & (codeSize - 1u), target})
                                                    writeVuInstructionPair(fx->code, next, makeVuIbne(6u, 7u, 2), kVuUpperNop);
                                                if (incomingWrite)
                                                    writeVuInstructionPair(fx->code, start - 8u,
                                                        makeVuIlw(8u, previousVi, 0u, 3), kVuUpperNop);
                                                for (uint32_t byte = 0; byte < dataSize; ++byte)
                                                    fx->data[byte] = static_cast<uint8_t>(byte * 29u + 11u);
                                            }
                                            VU1Interpreter candidate(unit), oracle(unit);
                                            oracle.setCompiledExecutionEnabled(false);
                                            uint32_t random = 0x753bc411u;
                                            for (unsigned reg = 1; reg < 32u; ++reg)
                                                for (unsigned lane = 0; lane < 4u; ++lane)
                                                {
                                                    random = random * 1664525u + 1013904223u;
                                                    std::memcpy(&candidate.state().vf[reg][lane], &random, sizeof(random));
                                                }
                                            for (unsigned lane = 0; lane < 4u; ++lane)
                                                candidate.state().acc[lane] = candidate.state().vf[23u][lane];
                                            candidate.state().vi[5] = 3;
                                            candidate.state().vi[4] = equal ? 3 : 9;
                                            candidate.state().vi[6] = candidate.state().vi[7] = -32768;
                                            candidate.state().branchTarget = 0x1a8u;
                                            candidate.state().branchDelay = 7u;
                                            oracle.state() = candidate.state();
                                            const auto execute = [&](VU1Interpreter &vu, Vu1Fixture &fx, uint32_t pc, uint32_t cycles) {
                                                vu.execute(fx.code, codeSize, fx.data, dataSize, fx.gs, &fx.mem, pc, 0u, 0u, cycles);
                                            };
                                            const auto resume = [&](VU1Interpreter &vu, Vu1Fixture &fx, uint32_t cycles) {
                                                vu.resume(fx.code, codeSize, fx.data, dataSize, fx.gs, &fx.mem, 0u, 0u, cycles);
                                            };
                                            const uint64_t before = ps2_vu_detail::terminalBranchPairs;
                                            if (incomingWrite)
                                            {
                                                candidate.setCompiledExecutionEnabled(false);
                                                execute(candidate, candidateMemory, start - 8u, 1u);
                                                execute(oracle, oracleMemory, start - 8u, 1u);
                                                candidate.setCompiledExecutionEnabled(true);
                                                resume(candidate, candidateMemory, budget);
                                                resume(oracle, oracleMemory, budget);
                                            }
                                            else
                                            {
                                                execute(candidate, candidateMemory, start, budget);
                                                execute(oracle, oracleMemory, start, budget);
                                            }
                                            if (std::getenv("PS2_VU_REQUIRE_BRANCH_TAILS"))
                                                t.Equals(ps2_vu_detail::terminalBranchPairs - before, budget >= 6u ? uint64_t{4} : uint64_t{0},
                                                    "only a complete six-cycle terminal region should execute the forced path");
                                            const auto compare = [&] {
                                                t.IsTrue(std::memcmp(&candidate.state(), &oracle.state(), sizeof(VU1State)) == 0,
                                                    "taken/untaken PC, branch backup, flags and masked registers must match raw execution");
                                                t.IsTrue(std::memcmp(candidateMemory.data, oracleMemory.data, dataSize) == 0,
                                                    "the delay-slot store must execute exactly once and preserve all other bytes");
                                            };
                                            compare();
                                            for (unsigned tail = 0; tail < 8u; ++tail)
                                            {
                                                resume(candidate, candidateMemory, 1u);
                                                resume(oracle, oracleMemory, 1u);
                                                compare();
                                            }
                                        }
            }
        });

        tc.Run("terminal branches consume delayed CLIP checks without integer bypass", [](TestCase &t)
        {
            for (auto unit : {VU1Interpreter::Unit::VU0, VU1Interpreter::Unit::VU1})
                for (unsigned phase = 0; phase < 8u; ++phase)
                    for (uint32_t firstClip : {0u, 1u})
                        for (uint32_t opcode : {0x28u, 0x29u})
                            for (uint32_t budget : {1u, 2u, 3u, 4u, 5u, 7u})
                            {
                                Vu1Fixture candidateMemory, oracleMemory;
                                t.IsTrue(candidateMemory.initialize() && oracleMemory.initialize(), "flag branch fixtures initialize");
                                const uint32_t codeSize = unit == VU1Interpreter::Unit::VU1 ? PS2_VU1_CODE_SIZE : PS2_VU0_CODE_SIZE;
                                const uint32_t dataSize = unit == VU1Interpreter::Unit::VU1 ? PS2_VU1_DATA_SIZE : PS2_VU0_DATA_SIZE;
                                const uint32_t start = (phase + 3u) * 8u;
                                for (auto *fx : {&candidateMemory, &oracleMemory})
                                {
                                    if (unit == VU1Interpreter::Unit::VU0)
                                    {
                                        fx->code = fx->mem.getVU0Code();
                                        fx->data = fx->mem.getVU0Data();
                                    }
                                    for (uint32_t pc = 0; pc < codeSize; pc += 8u)
                                        writeVuInstructionPair(fx->code, pc, 0x8000033cu, kVuUpperNop);
                                    writeVuInstructionPair(fx->code, phase * 8u, 0x22000000u | firstClip, kVuUpperNop);
                                    writeVuInstructionPair(fx->code, (phase + 1u) * 8u, 0x22222222u, kVuUpperNop);
                                    writeVuInstructionPair(fx->code, (phase + 2u) * 8u, 0x22555555u, kVuUpperNop);
                                    writeVuInstructionPair(fx->code, start, 0x38020000u, makeVuUpperSpecial(0x1fu, 15u, 2u, 1u));
                                    writeVuInstructionPair(fx->code, start + 8u, 0x24000001u, kVuUpperNop);
                                    writeVuInstructionPair(fx->code, start + 16u, (opcode << 25u) | 0x00030802u, kVuUpperNop);
                                    writeVuInstructionPair(fx->code, start + 24u, 0x0b010000u, makeVuUpperSpecial(0x1fu, 15u, 4u, 3u));
                                    for (uint32_t byte = 0; byte < dataSize; ++byte)
                                        fx->data[byte] = static_cast<uint8_t>(byte * 13u + 7u);
                                }
                                VU1Interpreter candidate(unit), oracle(unit);
                                for (uint32_t reg = 1; reg <= 4u; ++reg)
                                    for (uint32_t lane = 0; lane < 4u; ++lane)
                                        candidate.state().vf[reg][lane] = static_cast<float>(static_cast<int>(reg + lane) - 4);
                                candidate.state().clip = 0xabcdefu;
                                candidate.state().vi[1] = firstClip ^ 1u;
                                candidate.state().vi[3] = 1;
                                oracle.state() = candidate.state();
                                candidate.setCompiledExecutionEnabled(false);
                                oracle.setCompiledExecutionEnabled(false);
                                candidate.execute(candidateMemory.code, codeSize, candidateMemory.data, dataSize,
                                    candidateMemory.gs, &candidateMemory.mem, 0u, 0u, 0u, phase + 3u);
                                oracle.execute(oracleMemory.code, codeSize, oracleMemory.data, dataSize,
                                    oracleMemory.gs, &oracleMemory.mem, 0u, 0u, 0u, phase + 3u);
                                candidate.setCompiledExecutionEnabled(true);
                                const uint64_t before = ps2_vu_detail::terminalBranchPairs;
                                for (unsigned step = 0; step < 9u; ++step)
                                {
                                    if (step != 0u) candidate.setCompiledExecutionEnabled(false);
                                    const uint32_t cycles = step == 0u ? budget : 1u;
                                    candidate.resume(candidateMemory.code, codeSize, candidateMemory.data, dataSize,
                                        candidateMemory.gs, &candidateMemory.mem, 0u, 0u, cycles);
                                    oracle.resume(oracleMemory.code, codeSize, oracleMemory.data, dataSize,
                                        oracleMemory.gs, &oracleMemory.mem, 0u, 0u, cycles);
                                    t.IsTrue(std::memcmp(&candidate.state(), &oracle.state(), sizeof(VU1State)) == 0,
                                        "the immediately preceding CLIP result must feed branch selection and every raw flag tail");
                                    t.IsTrue(std::memcmp(candidateMemory.data, oracleMemory.data, dataSize) == 0,
                                        "the branch delay-slot store must use the new flag-check value");
                                    if (step == 0u && std::getenv("PS2_VU_REQUIRE_BRANCH_TAILS"))
                                        t.Equals(ps2_vu_detail::terminalBranchPairs - before, budget >= 4u ? uint64_t{4} : uint64_t{0},
                                            "both CLIP reads and the terminal branch must execute in one native region");
                                    if (step == 0u && budget == 4u)
                                    {
                                        const bool taken = opcode == 0x28u ? firstClip == 1u : firstClip != 1u;
                                        t.Equals(candidate.state().pc, start + (taken ? 40u : 32u), "the branch uses the newly visible CLIP predicate");
                                        t.Equals(candidate.state().vi[2], int32_t{0xdef}, "the earlier FCGET still sees the entry CLIP value");
                                    }
                                }
                            }
        });

        tc.Run("terminal branch and delay-slot code edits invalidate cached selection", [](TestCase &t)
        {
            for (uint32_t offset : {16u, 24u})
            {
                Vu1Fixture candidateMemory, oracleMemory;
                t.IsTrue(candidateMemory.initialize() && oracleMemory.initialize(), "branch edit fixtures must initialize");
                for (auto *fx : {&candidateMemory, &oracleMemory})
                {
                    for (uint32_t pc = 0; pc < PS2_VU1_CODE_SIZE; pc += 8u)
                        writeVuInstructionPair(fx->code, pc, 0x8000033cu, kVuUpperNop);
                    writeTerminalBranchFixture(fx->code, 64u, 0x29u, 5u, 3);
                }
                VU1Interpreter candidate, oracle;
                oracle.setCompiledExecutionEnabled(false);
                candidate.state().vi[4] = candidate.state().vi[5] = 3;
                oracle.state() = candidate.state();
                const uint64_t before = ps2_vu_detail::terminalBranchPairs;
                candidate.execute(candidateMemory.code, PS2_VU1_CODE_SIZE, candidateMemory.data, PS2_VU1_DATA_SIZE,
                    candidateMemory.gs, &candidateMemory.mem, 64u, 0u, 0u, 6u);
                oracle.execute(oracleMemory.code, PS2_VU1_CODE_SIZE, oracleMemory.data, PS2_VU1_DATA_SIZE,
                    oracleMemory.gs, &oracleMemory.mem, 64u, 0u, 0u, 6u);
                if (std::getenv("PS2_VU_REQUIRE_BRANCH_TAILS"))
                    t.Equals(ps2_vu_detail::terminalBranchPairs - before, uint64_t{4}, "the original branch region must be cached");
                for (auto *fx : {&candidateMemory, &oracleMemory})
                    writeTrackedVuInstructionPair(*fx, 64u + offset, makeVuIaddiu(15u, 0u, 777), kVuUpperNop);
                candidate.state().pc = oracle.state().pc = 64u;
                const uint64_t edited = ps2_vu_detail::terminalBranchPairs;
                for (unsigned step = 0; step < 10u; ++step)
                {
                    const uint32_t budget = step == 0u ? 6u : 1u;
                    candidate.resume(candidateMemory.code, PS2_VU1_CODE_SIZE, candidateMemory.data, PS2_VU1_DATA_SIZE,
                        candidateMemory.gs, &candidateMemory.mem, 0u, 0u, budget);
                    oracle.resume(oracleMemory.code, PS2_VU1_CODE_SIZE, oracleMemory.data, PS2_VU1_DATA_SIZE,
                        oracleMemory.gs, &oracleMemory.mem, 0u, 0u, budget);
                    t.IsTrue(std::memcmp(&candidate.state(), &oracle.state(), sizeof(VU1State)) == 0,
                        "edited branch or delay slot must match raw execution and all resumed tails");
                    t.IsTrue(std::memcmp(candidateMemory.data, oracleMemory.data, PS2_VU1_DATA_SIZE) == 0,
                        "editing the delay-slot store must remove its effect");
                }
                t.Equals(candidate.state().vi[15], 777, "the replacement instruction must execute");
                t.Equals(ps2_vu_detail::terminalBranchPairs - edited, uint64_t{0}, "an edited terminal region must fall back");
            }
        });

        tc.Run("natural loops keep flag-reading bodies on the exact fallback", [](TestCase &t)
        {
            for (auto unit : {VU1Interpreter::Unit::VU0, VU1Interpreter::Unit::VU1})
                for (unsigned budget : {4u, 8u, 19u})
                {
                    Vu1Fixture nativeMemory, oracleMemory;
                    t.IsTrue(nativeMemory.initialize() && oracleMemory.initialize(), "flag loop fixtures initialize");
                    const uint32_t codeSize = unit == VU1Interpreter::Unit::VU1 ? PS2_VU1_CODE_SIZE : PS2_VU0_CODE_SIZE;
                    const uint32_t dataSize = unit == VU1Interpreter::Unit::VU1 ? PS2_VU1_DATA_SIZE : PS2_VU0_DATA_SIZE;
                    for (auto *fx : {&nativeMemory, &oracleMemory})
                    {
                        if (unit == VU1Interpreter::Unit::VU0)
                        {
                            fx->code = fx->mem.getVU0Code();
                            fx->data = fx->mem.getVU0Data();
                        }
                        writeVuInstructionPair(fx->code, 0u, 0x24000001u, kVuUpperNop);
                        writeVuInstructionPair(fx->code, 8u, 0x8000033cu, kVuUpperNop);
                        writeVuInstructionPair(fx->code, 16u, makeVuIbne(1u, 0u, -3), kVuUpperNop);
                        writeVuInstructionPair(fx->code, 24u, 0x8000033cu, kVuUpperNop);
                    }
                    VU1Interpreter native(unit), oracle(unit);
                    oracle.setCompiledExecutionEnabled(false);
                    native.state().clip = 1u;
                    oracle.state() = native.state();
                    const uint64_t before = ps2_vu_detail::countedLoopPairs;
                    native.execute(nativeMemory.code, codeSize, nativeMemory.data, dataSize,
                        nativeMemory.gs, &nativeMemory.mem, 0u, 0u, 0u, budget);
                    oracle.execute(oracleMemory.code, codeSize, oracleMemory.data, dataSize,
                        oracleMemory.gs, &oracleMemory.mem, 0u, 0u, 0u, budget);
                    t.Equals(ps2_vu_detail::countedLoopPairs - before, uint64_t{0},
                        "broader bounded-region support must not enable unsupported loop flag timelines");
                    for (unsigned tail = 0; tail < 8u; ++tail)
                    {
                        t.IsTrue(std::memcmp(&native.state(), &oracle.state(), sizeof(VU1State)) == 0,
                            "FCAND and its immediate backedge must preserve the visible CLIP version");
                        native.resume(nativeMemory.code, codeSize, nativeMemory.data, dataSize,
                            nativeMemory.gs, &nativeMemory.mem, 0u, 0u, 1u);
                        oracle.resume(oracleMemory.code, codeSize, oracleMemory.data, dataSize,
                            oracleMemory.gs, &oracleMemory.mem, 0u, 0u, 1u);
                    }
                }
        });

        tc.Run("natural loops without counters yield at every exact budget", [](TestCase &t)
        {
            for (auto unit : {VU1Interpreter::Unit::VU0, VU1Interpreter::Unit::VU1})
                for (unsigned budget = 0; budget <= 25u; ++budget)
                {
                    Vu1Fixture nativeMemory, oracleMemory;
                    t.IsTrue(nativeMemory.initialize() && oracleMemory.initialize(), "uncounted loop fixtures initialize");
                    const uint32_t codeSize = unit == VU1Interpreter::Unit::VU1 ? PS2_VU1_CODE_SIZE : PS2_VU0_CODE_SIZE;
                    const uint32_t dataSize = unit == VU1Interpreter::Unit::VU1 ? PS2_VU1_DATA_SIZE : PS2_VU0_DATA_SIZE;
                    for (auto *fx : {&nativeMemory, &oracleMemory})
                    {
                        if (unit == VU1Interpreter::Unit::VU0)
                        {
                            fx->code = fx->mem.getVU0Code();
                            fx->data = fx->mem.getVU0Data();
                        }
                        writeVuInstructionPair(fx->code, 0u, 0x40000000u,
                            makeVuUpper(0x22u, 15u, 0u, 2u, 1u) | 0x80000000u);
                        writeVuInstructionPair(fx->code, 8u, 0x40400000u,
                            makeVuUpper(0x1eu, 15u, 0u, 2u, 3u) | 0x80000000u);
                        writeVuInstructionPair(fx->code, 16u, (0x28u << 25u) | (-3 & 0x7ff), kVuUpperNop);
                        writeVuInstructionPair(fx->code, 24u, 0x8000033cu, kVuUpperNop);
                    }
                    VU1Interpreter native(unit), oracle(unit);
                    oracle.setCompiledExecutionEnabled(false);
                    native.state().i = 5.0f;
                    for (unsigned lane = 0; lane < 4u; ++lane)
                        native.state().vf[2][lane] = static_cast<float>(lane + 1u);
                    oracle.state() = native.state();
                    const uint64_t before = ps2_vu_detail::countedLoopPairs;
                    native.execute(nativeMemory.code, codeSize, nativeMemory.data, dataSize,
                        nativeMemory.gs, &nativeMemory.mem, 0u, 0u, 0u, budget);
                    oracle.execute(oracleMemory.code, codeSize, oracleMemory.data, dataSize,
                        oracleMemory.gs, &oracleMemory.mem, 0u, 0u, 0u, budget);
                    if (std::getenv("PS2_VU_REQUIRE_NATURAL_LOOPS"))
                        t.Equals(ps2_vu_detail::countedLoopPairs - before, uint64_t{budget / 4u * 4u},
                            "an always-taken loop needs no counter to honor its exact budget");
                    for (unsigned tail = 0; tail < 8u; ++tail)
                    {
                        t.IsTrue(std::memcmp(&native.state(), &oracle.state(), sizeof(VU1State)) == 0,
                            "bounded infinite backedges retain I and pending pipeline tails");
                        native.resume(nativeMemory.code, codeSize, nativeMemory.data, dataSize,
                            nativeMemory.gs, &nativeMemory.mem, 0u, 0u, 1u);
                        oracle.resume(oracleMemory.code, codeSize, oracleMemory.data, dataSize,
                            oracleMemory.gs, &oracleMemory.mem, 0u, 0u, 1u);
                    }
                }
        });

        tc.Run("natural loops carry old and new I across stalled iterations", [](TestCase &t)
        {
            for (auto unit : {VU1Interpreter::Unit::VU0, VU1Interpreter::Unit::VU1})
                for (unsigned opcode : {0x28u, 0x29u})
                    for (uint32_t immediate : {0x40400000u, 0x7fffffffu, 0x80000001u})
                        for (unsigned phase : {0u, 1u, 2u, 3u})
                            for (unsigned trips : {1u, 2u, 7u})
                                for (unsigned budget : {0u, 1u, 8u, 12u, 14u, 15u, 16u, 29u, 30u, 31u, 45u, 46u, 120u})
                                {
                                    Vu1Fixture nativeMemory, oracleMemory;
                                    t.IsTrue(nativeMemory.initialize() && oracleMemory.initialize(), "natural loop fixtures initialize");
                                    const uint32_t codeSize = unit == VU1Interpreter::Unit::VU1 ? PS2_VU1_CODE_SIZE : PS2_VU0_CODE_SIZE;
                                    const uint32_t dataSize = unit == VU1Interpreter::Unit::VU1 ? PS2_VU1_DATA_SIZE : PS2_VU0_DATA_SIZE;
                                    const uint32_t start = phase * 8u;
                                    for (auto *fx : {&nativeMemory, &oracleMemory})
                                    {
                                        if (unit == VU1Interpreter::Unit::VU0)
                                        {
                                            fx->code = fx->mem.getVU0Code();
                                            fx->data = fx->mem.getVU0Data();
                                        }
                                        for (unsigned pc = 0; pc < codeSize; pc += 8u)
                                            writeVuInstructionPair(fx->code, pc, 0x8000033cu, kVuUpperNop);
                                        writeEarlyCounterLoopCode(*fx);
                                        std::memmove(fx->code + start, fx->code, 56u);
                                        for (unsigned pc = 0; pc < start; pc += 8u)
                                            writeVuInstructionPair(fx->code, pc, 0x8000033cu, kVuUpperNop);
                                        writeVuInstructionPair(fx->code, start + 56u, 0x40000000u,
                                            makeVuUpper(0x1eu, 15u, 0u, 16u, 27u) | 0x80000000u);
                                        writeVuInstructionPair(fx->code, start + 64u, immediate,
                                            makeVuUpper(0x22u, 12u, 0u, 17u, 28u) | 0x80000000u);
                                        writeVuInstructionPair(fx->code, start + 72u, 0x8000033cu,
                                            makeVuUpper(0x1eu, 12u, 0u, 28u, 29u));
                                        writeVuInstructionPair(fx->code, start + 80u,
                                            (opcode << 25u) | (11u << 16u) | (12u << 11u) | (-11 & 0x7ff),
                                            makeVuUpperSpecial(0x1bu, 14u, 0u, 22u));
                                        writeVuInstructionPair(fx->code, start + 88u, makeVuSq(15u, 25u, 11u, -7),
                                            makeVuUpperSpecial(0x08u, 14u, 23u, 18u));
                                        for (unsigned byte = 0; byte < dataSize; ++byte)
                                            fx->data[byte] = static_cast<uint8_t>(byte * 23u + 5u);
                                    }
                                    VU1Interpreter native(unit), oracle(unit);
                                    oracle.setCompiledExecutionEnabled(false);
                                    uint32_t random = 0x783bd519u;
                                    for (unsigned reg = 1; reg < 32u; ++reg)
                                        for (unsigned lane = 0; lane < 4u; ++lane)
                                        {
                                            random = random * 1664525u + 1013904223u;
                                            native.state().vf[reg][lane] = std::bit_cast<float>(random);
                                        }
                                    native.state().i = 5.0f;
                                    native.state().vi[10] = 11;
                                    native.state().vi[11] = 32766;
                                    const unsigned expectedTrips = opcode == 0x29u ? trips : trips == 1u ? 1u : 2u;
                                    native.state().vi[12] = static_cast<int16_t>(32766 +
                                        (opcode == 0x29u ? 5u * trips : trips == 1u ? 10u : 5u));
                                    native.state().branchTarget = 0x1238u;
                                    native.state().status = 0xa50u;
                                    native.state().clip = 0xa5a5a5u;
                                    oracle.state() = native.state();
                                    const uint64_t before = ps2_vu_detail::countedLoopPairs;
                                    native.execute(nativeMemory.code, codeSize, nativeMemory.data, dataSize,
                                        nativeMemory.gs, &nativeMemory.mem, 0u, 0u, 0u, phase + budget);
                                    oracle.execute(oracleMemory.code, codeSize, oracleMemory.data, dataSize,
                                        oracleMemory.gs, &oracleMemory.mem, 0u, 0u, 0u, phase + budget);
                                    if (std::getenv("PS2_VU_REQUIRE_NATURAL_LOOPS"))
                                        t.Equals(ps2_vu_detail::countedLoopPairs - before,
                                            uint64_t{12u} * std::min(expectedTrips, budget / 15u),
                                            "complete stalled iterations must enter the retained natural-loop path");
                                    for (unsigned tail = 0; tail < 9u; ++tail)
                                    {
                                        t.IsTrue(std::memcmp(&native.state(), &oracle.state(), sizeof(VU1State)) == 0,
                                            "LOI old-I arithmetic, loop-carried I, branches and all delayed flags must match raw state");
                                        t.IsTrue(std::memcmp(nativeMemory.data, oracleMemory.data, dataSize) == 0,
                                            "loop-carried I must retain exact stored result bits");
                                        native.resume(nativeMemory.code, codeSize, nativeMemory.data, dataSize,
                                            nativeMemory.gs, &nativeMemory.mem, 0u, 0u, 1u);
                                        oracle.resume(oracleMemory.code, codeSize, oracleMemory.data, dataSize,
                                            oracleMemory.gs, &oracleMemory.mem, 0u, 0u, 1u);
                                    }
                                }
        });

        tc.Run("static region timestamps reject wrap and retire at the clock limit", [](TestCase &t)
        {
            constexpr uint64_t limit = UINT64_MAX;
            t.IsTrue(ps2_vu_detail::regionBudgetFits(limit - 8u, limit, 4u),
                     "the highest safe four-cycle region must fit");
            t.IsTrue(!ps2_vu_detail::regionBudgetFits(limit - 7u, limit, 4u),
                     "pending timestamps near wrap must force ordinary execution");
            t.IsTrue(!ps2_vu_detail::regionBudgetFits(10u, 13u, 4u) &&
                     !ps2_vu_detail::regionBudgetFits(limit - 8u, 3u, 4u),
                     "short and wrapped deadlines must reject without mutation");
            struct Flag {
                uint64_t readyCycle{};
                uint32_t mac{}, status{}, extraSticky{}, clip{};
                bool writesMac{}, writesStatus{}, writesSticky{}, writesClip{};
            };
            struct VectorWrite {
                uint64_t readyCycle{}, sequence{};
                unsigned reg{};
                uint8_t laneMask{};
                std::array<float, 4> value{};
            };
            struct IntegerWrite {
                uint64_t readyCycle{}, sequence{};
                unsigned reg{};
                int32_t value{};
            };
            VU1State state{};
            std::array<Flag, 8> flags{};
            std::array<VectorWrite, 1> vectors{};
            std::array<IntegerWrite, 1> integers{};
            std::array<std::array<uint64_t, 4>, 32> vfSequences{};
            std::array<uint64_t, 16> viSequences{};
            std::array<std::array<uint8_t, 4>, 32> firstWrite{};
            std::array<uint8_t, 16> firstViWrite{};
            constexpr uint64_t start = limit - 8u;
            uint32_t activeFlags = 0u, activeVf = 1u, activeVi = 1u;
            for (unsigned offset = 1u; offset <= 4u; ++offset)
            {
                const unsigned slot = 2u * ((start + offset) & 3u);
                flags[slot] = {start + offset, offset, offset, 0u, offset,
                               true, true, false, true};
                activeFlags |= 1u << slot;
            }
            vectors[0] = {start + 3u, 9u, 1u, 12u, {3.0f, 7.0f, 0.0f, 0.0f}};
            vfSequences[1] = {9u, 9u, 0u, 0u};
            firstWrite[1] = {2u, 3u, 4u, 4u};
            integers[0] = {start + 4u, 10u, 1u, 0x8000};
            viSequences[1] = 10u;
            firstViWrite[1] = 4u;
            ps2_vu_detail::retireRegionInputs(state, start, flags, vectors, integers,
                vfSequences, viSequences, activeFlags, activeVf, activeVi, firstWrite, firstViWrite);
            t.Equals(activeFlags | activeVf | activeVi, 0u,
                     "all four incoming timestamp slots must retire once");
            t.Equals(state.mac, 4u, "chronological MAC retirement must retain the last event");
            t.Equals(state.clip, 4u, "chronological CLIP retirement must retain the last event");
            t.Equals(state.status, 0x1c4u, "all retired flag sticky bits must accumulate");
            t.Equals(state.vf[1][0], 0.0f, "a write before incoming readiness must cancel its lane");
            t.Equals(state.vf[1][1], 7.0f, "an equal-cycle write must retain incoming visibility");
            t.Equals(state.vi[1], -32768, "equal-cycle VI visibility must sign-truncate");
        });
        tc.Run("static regions preserve pending writes and exact budget boundaries", [](TestCase &t)
        {
            for (unsigned scenario = 0; scenario < 3; ++scenario)
                for (unsigned budget : {3u, 4u, 5u})
                {
                    Vu1Fixture fx;
                    t.IsTrue(fx.initialize(), "VU1 fixture should initialize");
                    writeVuInstructionPair(fx.code, 0u, 0x8000033cu, makeVuUpper(0x28u, 0xfu, 3u, 2u, 1u));
                    for (unsigned pair = 1; pair <= 4; ++pair)
                        writeVuInstructionPair(fx.code, pair * 8u, 0x8000033cu, kVuUpperNop);
                    const uint32_t upper = scenario == 2u
                        ? makeVuUpper(0x28u, 0xfu, 3u, 1u, 6u)
                        : makeVuUpper(0x28u, 0xfu, 5u, 4u, 1u);
                    writeVuInstructionPair(fx.code, scenario == 0u ? 16u : 32u, 0x8000033cu, upper);
                    VU1Interpreter native, oracle;
                    oracle.setCompiledExecutionEnabled(false);
                    for (unsigned reg = 0; reg < 7; ++reg)
                        for (unsigned lane = 0; lane < 4; ++lane)
                            native.state().vf[reg][lane] = reg == 1u ? 11.0f : static_cast<float>(reg);
                    oracle.state() = native.state();
                    for (auto *vu : {&native, &oracle})
                        vu->execute(fx.code, PS2_VU1_CODE_SIZE, fx.data, PS2_VU1_DATA_SIZE,
                                    fx.gs, &fx.mem, 0u, 0u, 0u, 1u);
                    const uint64_t accepted = ps2_vu_detail::regionCounters.accepted[1];
                    native.resume(fx.code, PS2_VU1_CODE_SIZE, fx.data, PS2_VU1_DATA_SIZE,
                                  fx.gs, &fx.mem, 0u, 0u, budget);
                    oracle.resume(fx.code, PS2_VU1_CODE_SIZE, fx.data, PS2_VU1_DATA_SIZE,
                                  fx.gs, &fx.mem, 0u, 0u, budget);
                    t.IsTrue(std::memcmp(&native.state(), &oracle.state(), sizeof(VU1State)) == 0,
                             "native region must publish the same visible state at the requested budget");
                    if (std::getenv("PS2_VU_REQUIRE_REGIONS"))
                        t.Equals(ps2_vu_detail::regionCounters.accepted[1] - accepted,
                                 budget >= 4u ? uint64_t{4u} : uint64_t{0u},
                                 "the native fixture must force the region path only when the complete budget fits");
                    for (unsigned tail = 0; tail < 5; ++tail)
                    {
                        native.resume(fx.code, PS2_VU1_CODE_SIZE, fx.data, PS2_VU1_DATA_SIZE,
                                      fx.gs, &fx.mem, 0u, 0u, 1u);
                        oracle.resume(fx.code, PS2_VU1_CODE_SIZE, fx.data, PS2_VU1_DATA_SIZE,
                                      fx.gs, &fx.mem, 0u, 0u, 1u);
                        t.IsTrue(std::memcmp(&native.state(), &oracle.state(), sizeof(VU1State)) == 0,
                                 "every outgoing pending-write boundary must remain identical");
                    }
                }
        });
        tc.Run("static regions preserve LSU forwarding and following branch backups", [](TestCase &t)
        {
            for (unsigned budget : {3u, 4u, 5u})
            {
                Vu1Fixture nativeMemory, oracleMemory;
                t.IsTrue(nativeMemory.initialize() && oracleMemory.initialize(), "VU1 fixtures should initialize");
                for (auto *fx : {&nativeMemory, &oracleMemory})
                {
                    writeVuInstructionPair(fx->code, 0u, makeVuIlw(8u, 1u, 0u, 0), kVuUpperNop);
                    writeVuInstructionPair(fx->code, 8u, makeVuIaddiu(1u, 0u, 7), kVuUpperNop);
                    writeVuInstructionPair(fx->code, 16u, makeVuLq(15u, 6u, 0u, 1), kVuUpperNop);
                    writeVuInstructionPair(fx->code, 24u, makeVuSq(15u, 2u, 0u, 2), makeVuUpper(0x28u, 15u, 5u, 4u, 9u));
                    writeVuInstructionPair(fx->code, 32u, makeVuIaddiu(1u, 1u, 1), kVuUpperNop);
                    writeVuInstructionPair(fx->code, 40u, makeVuIbne(1u, 2u, 2), kVuUpperNop);
                    const uint32_t loadedVi = 5u;
                    std::memcpy(fx->data, &loadedVi, sizeof(loadedVi));
                    const float loadedVf[4] = {13.0f, 17.0f, 19.0f, 23.0f};
                    writeVuQword(fx->data, 1u, loadedVf);
                }
                VU1Interpreter native, oracle;
                oracle.setCompiledExecutionEnabled(false);
                for (unsigned reg = 1; reg < 10; ++reg)
                    for (unsigned lane = 0; lane < 4; ++lane)
                        native.state().vf[reg][lane] = static_cast<float>(reg * 4u + lane);
                native.state().vi[1] = 11;
                native.state().vi[2] = 7;
                oracle.state() = native.state();
                native.execute(nativeMemory.code, PS2_VU1_CODE_SIZE, nativeMemory.data, PS2_VU1_DATA_SIZE,
                               nativeMemory.gs, &nativeMemory.mem, 0u, 0u, 0u, 1u);
                oracle.execute(oracleMemory.code, PS2_VU1_CODE_SIZE, oracleMemory.data, PS2_VU1_DATA_SIZE,
                               oracleMemory.gs, &oracleMemory.mem, 0u, 0u, 0u, 1u);
                const uint64_t accepted = ps2_vu_detail::regionCounters.accepted[1];
                for (unsigned step = 0; step < 7; ++step)
                {
                    const unsigned cycles = step == 0u ? budget : 1u;
                    native.resume(nativeMemory.code, PS2_VU1_CODE_SIZE, nativeMemory.data, PS2_VU1_DATA_SIZE,
                                  nativeMemory.gs, &nativeMemory.mem, 0u, 0u, cycles);
                    oracle.resume(oracleMemory.code, PS2_VU1_CODE_SIZE, oracleMemory.data, PS2_VU1_DATA_SIZE,
                                  oracleMemory.gs, &oracleMemory.mem, 0u, 0u, cycles);
                    t.IsTrue(std::memcmp(&native.state(), &oracle.state(), sizeof(VU1State)) == 0,
                             "LSU and branch state must match at every resumed boundary");
                    t.IsTrue(std::memcmp(nativeMemory.data, oracleMemory.data, PS2_VU1_DATA_SIZE) == 0,
                             "region stores must preserve data memory ordering");
                    if (step == 0u && std::getenv("PS2_VU_REQUIRE_REGIONS"))
                        t.Equals(ps2_vu_detail::regionCounters.accepted[1] - accepted,
                                 budget >= 4u ? uint64_t{4u} : uint64_t{0u}, "LSU fixture must force the fast path");
                }
            }
        });
        tc.Run("static regions preserve accumulator and flag history", [](TestCase &t)
        {
            for (unsigned budget : {3u, 4u, 5u})
            {
                Vu1Fixture fx;
                t.IsTrue(fx.initialize(), "VU1 fixture should initialize");
                writeVuInstructionPair(fx.code, 0u, makeVuFlagImmediate(0x15u, 0u, 0xac0u), kVuUpperNop);
                writeVuInstructionPair(fx.code, 8u, (0x11u << 25u) | 0x123456u, kVuUpperNop);
                writeVuInstructionPair(fx.code, 16u, 0x8000033cu, makeVuUpperSpecial(0x2au, 15u, 3u, 2u));
                writeVuInstructionPair(fx.code, 24u, 0x8000033cu, makeVuUpper(0x29u, 15u, 5u, 4u, 6u));
                writeVuInstructionPair(fx.code, 32u, 0x8000033cu, makeVuUpperSpecial(0x1fu, 15u, 8u, 7u));
                writeVuInstructionPair(fx.code, 40u, 0x8000033cu, makeVuUpperSpecial(0x1fu, 15u, 7u, 8u));
                VU1Interpreter native, oracle;
                oracle.setCompiledExecutionEnabled(false);
                for (unsigned reg = 1; reg < 9; ++reg)
                    for (unsigned lane = 0; lane < 4; ++lane)
                        native.state().vf[reg][lane] = static_cast<float>(reg * (lane + 1u));
                native.state().vf[7][0] = -40.0f;
                native.state().vf[8][2] = 80.0f;
                native.state().status = 0x30u;
                oracle.state() = native.state();
                for (auto *vu : {&native, &oracle})
                    vu->execute(fx.code, PS2_VU1_CODE_SIZE, fx.data, PS2_VU1_DATA_SIZE,
                                fx.gs, &fx.mem, 0u, 0u, 0u, 2u);
                const uint64_t accepted = ps2_vu_detail::regionCounters.accepted[1];
                for (unsigned step = 0; step < 7; ++step)
                {
                    for (auto *vu : {&native, &oracle})
                        vu->resume(fx.code, PS2_VU1_CODE_SIZE, fx.data, PS2_VU1_DATA_SIZE,
                                   fx.gs, &fx.mem, 0u, 0u, step == 0u ? budget : 1u);
                    t.IsTrue(std::memcmp(&native.state(), &oracle.state(), sizeof(VU1State)) == 0,
                             "ACC forwarding and chronological MAC/STATUS/CLIP must match");
                    if (step == 0u && std::getenv("PS2_VU_REQUIRE_REGIONS"))
                        t.Equals(ps2_vu_detail::regionCounters.accepted[1] - accepted,
                                 budget >= 4u ? uint64_t{4u} : uint64_t{0u}, "flag fixture must force the fast path");
                }
            }
        });
        tc.Run("static SSA regions retain stalled dependencies and deferred tails", [](TestCase &t)
        {
            for (unsigned budget : {12u, 13u, 14u})
            {
                Vu1Fixture nativeMemory, oracleMemory;
                t.IsTrue(nativeMemory.initialize() && oracleMemory.initialize(), "VU1 fixtures should initialize");
                for (auto *fx : {&nativeMemory, &oracleMemory})
                {
                    writeVuInstructionPair(fx->code, 0u, makeVuFlagImmediate(0x15u, 0u, 0xac0u), kVuUpperNop);
                    writeVuInstructionPair(fx->code, 8u, (0x11u << 25u) | 0x123456u, kVuUpperNop);
                    writeVuInstructionPair(fx->code, 16u, makeVuIaddiu(1u, 0u, 7), makeVuUpperSpecial(0x2au, 15u, 3u, 2u));
                    writeVuInstructionPair(fx->code, 24u, makeVuSq(15u, 2u, 0u, 1), makeVuUpper(0x29u, 15u, 5u, 4u, 6u));
                    writeVuInstructionPair(fx->code, 32u, makeVuLowerSpecial(0x34u, 2u, 8u, 0u, 4u), makeVuUpper(0x28u, 8u, 3u, 2u, 8u));
                    writeVuInstructionPair(fx->code, 40u, makeVuLq(15u, 12u, 0u, 1), makeVuUpper(0x28u, 8u, 5u, 4u, 6u));
                    writeVuInstructionPair(fx->code, 48u, 0x8000033cu, makeVuUpper(0x29u, 15u, 6u, 4u, 9u));
                    writeVuInstructionPair(fx->code, 56u, 0x8000033cu, makeVuUpper(0x28u, 15u, 7u, 8u, 10u));
                    writeVuInstructionPair(fx->code, 64u, makeVuIlw(8u, 3u, 0u, 1), makeVuUpperSpecial(0x1fu, 15u, 8u, 7u));
                    writeVuInstructionPair(fx->code, 72u, makeVuIaddiu(1u, 1u, 1), makeVuUpper(0x28u, 15u, 9u, 10u, 11u));
                    writeVuInstructionPair(fx->code, 80u, makeVuIbne(1u, 4u, 2), kVuUpperNop);
                }
                VU1Interpreter native, oracle;
                oracle.setCompiledExecutionEnabled(false);
                for (unsigned reg = 1; reg < 14; ++reg)
                    for (unsigned lane = 0; lane < 4; ++lane)
                        native.state().vf[reg][lane] = static_cast<float>(reg * (lane + 1u));
                native.state().vi[1] = 11;
                native.state().vi[2] = 1;
                native.state().vi[4] = 7;
                oracle.state() = native.state();
                native.execute(nativeMemory.code, PS2_VU1_CODE_SIZE, nativeMemory.data, PS2_VU1_DATA_SIZE,
                               nativeMemory.gs, &nativeMemory.mem, 0u, 0u, 0u, 2u);
                oracle.execute(oracleMemory.code, PS2_VU1_CODE_SIZE, oracleMemory.data, PS2_VU1_DATA_SIZE,
                               oracleMemory.gs, &oracleMemory.mem, 0u, 0u, 0u, 2u);
                const uint64_t accepted = ps2_vu_detail::regionCounters.accepted[1];
                for (unsigned step = 0; step < 10; ++step)
                {
                    const unsigned cycles = step == 0u ? budget : 1u;
                    native.resume(nativeMemory.code, PS2_VU1_CODE_SIZE, nativeMemory.data, PS2_VU1_DATA_SIZE,
                                  nativeMemory.gs, &nativeMemory.mem, 0u, 0u, cycles);
                    oracle.resume(oracleMemory.code, PS2_VU1_CODE_SIZE, oracleMemory.data, PS2_VU1_DATA_SIZE,
                                  oracleMemory.gs, &oracleMemory.mem, 0u, 0u, cycles);
                    t.IsTrue(std::memcmp(&native.state(), &oracle.state(), sizeof(VU1State)) == 0,
                             "SSA forwarding, stalls, canceled lanes and pending flags must survive each boundary");
                    t.IsTrue(std::memcmp(nativeMemory.data, oracleMemory.data, PS2_VU1_DATA_SIZE) == 0,
                             "SSA store/load ordering must match the raw interpreter");
                    if (step == 0u && std::getenv("PS2_VU_REQUIRE_REGIONS"))
                        t.Equals(ps2_vu_detail::regionCounters.accepted[1] - accepted,
                                 budget >= 13u ? uint64_t{8u} : uint64_t{4u},
                                 "the whole region must run when it fits; a shorter budget uses the shared four-pair entry");
                }
            }
        });
        tc.Run("static clip readers observe incoming and local delayed flag versions", [](TestCase &t)
        {
            for (unsigned phase = 0; phase < 8u; ++phase)
                for (unsigned budget : {1u, 3u, 4u, 7u, 11u, 12u, 13u})
                {
                    Vu1Fixture nativeMemory, oracleMemory;
                    t.IsTrue(nativeMemory.initialize() && oracleMemory.initialize(), "clip fixtures initialize");
                    const uint32_t upper[12] = {
                        makeVuUpperSpecial(0x1fu, 15u, 2u, 1u),
                        makeVuUpperSpecial(0x1fu, 15u, 4u, 3u), kVuUpperNop, kVuUpperNop,
                        makeVuUpperSpecial(0x1fu, 15u, 1u, 2u), kVuUpperNop, kVuUpperNop,
                        makeVuUpperSpecial(0x1fu, 15u, 3u, 4u), kVuUpperNop, kVuUpperNop,
                        makeVuUpperSpecial(0x1fu, 15u, 3u, 1u), kVuUpperNop};
                    const uint32_t lower[12] = {
                        0x38020000u, 0x38030000u, 0x38040000u, 0x38050000u,
                        0x38060000u, 0x38070000u, 0x24100000u, 0x0b010000u,
                        0x26feffffu, 0x0b010001u, 0x20ffffffu, 0x38080000u};
                    for (auto *fx : {&nativeMemory, &oracleMemory})
                    {
                        for (unsigned index = 0; index < phase + 24u; ++index)
                            writeVuInstructionPair(fx->code, index * 8u, 0x8000033cu, kVuUpperNop);
                        writeVuInstructionPair(fx->code, phase * 8u, 0x22123456u, kVuUpperNop);
                        writeVuInstructionPair(fx->code, (phase + 1u) * 8u,
                            makeVuFlagImmediate(0x15u, 0u, 0xac0u), makeVuUpperSpecial(0x1fu, 15u, 4u, 1u));
                        writeVuInstructionPair(fx->code, (phase + 2u) * 8u,
                            0x22654321u, makeVuUpperSpecial(0x1fu, 15u, 2u, 3u));
                        for (unsigned index = 0; index < 12u; ++index)
                            writeVuInstructionPair(fx->code, (phase + 3u + index) * 8u, lower[index], upper[index]);
                        for (unsigned index = 0; index < 8u; ++index)
                            writeVuInstructionPair(fx->code, (phase + 15u + index) * 8u,
                                0x0b000000u | ((index < 7u ? index + 2u : 1u) << 16u) | (index + 2u), kVuUpperNop);
                    }
                    VU1Interpreter native, oracle;
                    oracle.setCompiledExecutionEnabled(false);
                    for (unsigned reg = 1; reg < 5u; ++reg)
                        for (unsigned lane = 0; lane < 4u; ++lane)
                            native.state().vf[reg][lane] = static_cast<float>(
                                (static_cast<int>(reg * 3u + lane + phase) % 9) - 4);
                    native.state().vf[2][3] = 1.0f;
                    native.state().vf[4][3] = 2.0f;
                    native.state().clip = 0xabcdefu;
                    native.state().status = 0x135u;
                    oracle.state() = native.state();
                    native.execute(nativeMemory.code, PS2_VU1_CODE_SIZE, nativeMemory.data, PS2_VU1_DATA_SIZE,
                        nativeMemory.gs, &nativeMemory.mem, 0u, 0u, 0u, phase + 3u);
                    oracle.execute(oracleMemory.code, PS2_VU1_CODE_SIZE, oracleMemory.data, PS2_VU1_DATA_SIZE,
                        oracleMemory.gs, &oracleMemory.mem, 0u, 0u, 0u, phase + 3u);
                    const uint64_t accepted = ps2_vu_detail::regionCounters.accepted[1];
                    for (unsigned step = 0; step < 9u; ++step)
                    {
                        const unsigned cycles = step == 0u ? budget : 1u;
                        if (step != 0u) native.setCompiledExecutionEnabled(false);
                        native.resume(nativeMemory.code, PS2_VU1_CODE_SIZE, nativeMemory.data, PS2_VU1_DATA_SIZE,
                            nativeMemory.gs, &nativeMemory.mem, 0u, 0u, cycles);
                        oracle.resume(oracleMemory.code, PS2_VU1_CODE_SIZE, oracleMemory.data, PS2_VU1_DATA_SIZE,
                            oracleMemory.gs, &oracleMemory.mem, 0u, 0u, cycles);
                        t.IsTrue(std::memcmp(&native.state(), &oracle.state(), sizeof(VU1State)) == 0,
                            "CLIP/FCSET issue history and every visible delayed flag version match");
                        t.IsTrue(std::memcmp(nativeMemory.data, oracleMemory.data, PS2_VU1_DATA_SIZE) == 0,
                            "flag-dependent stores match at every raw continuation");
                        if (step == 0u && std::getenv("PS2_VU_REQUIRE_REGIONS"))
                            t.Equals(ps2_vu_detail::regionCounters.accepted[1] - accepted,
                                budget >= 12u ? uint64_t{12} : budget >= 4u ? uint64_t{4} : uint64_t{0},
                                "clip readers genuinely take the complete region or bounded first-four fallback");
                        if (step == 0u && budget >= 12u)
                        {
                            t.Equals(native.state().vi[2], int32_t{0xdef}, "first reader sees entry CLIP");
                            t.Equals(native.state().vi[3], int32_t{0x456}, "second reader sees first pending FCSET");
                            t.Equals(native.state().vi[5], int32_t{0x321}, "fourth reader sees final pending FCSET");
                        }
                    }
                }
        });
        tc.Run("static SSA LOI retains old I operands and publishes normalized immediates", [](TestCase &t)
        {
            for (unsigned phase = 0u; phase < 8u; ++phase)
                for (unsigned budget : {1u, 3u, 4u, 7u, 8u, 9u})
                {
                    Vu1Fixture nativeMemory, oracleMemory;
                    t.IsTrue(nativeMemory.initialize() && oracleMemory.initialize(), "VU1 fixtures should initialize");
                    const uint32_t upper[] = {
                        makeVuUpper(0x22u, 15u, 0u, 1u, 3u) | 0x80000000u,
                        makeVuUpper(0x1eu, 15u, 0u, 2u, 4u),
                        makeVuUpper(0x1eu, 15u, 0u, 1u, 5u) | 0x80000000u,
                        makeVuUpper(0x22u, 15u, 0u, 2u, 6u) | 0x80000000u,
                        makeVuUpper(0x22u, 15u, 0u, 1u, 7u),
                        kVuUpperNop | 0x80000000u,
                        makeVuUpper(0x1eu, 15u, 0u, 2u, 8u),
                        makeVuUpper(0x1eu, 15u, 0u, 1u, 9u) | 0x80000000u};
                    const uint32_t lower[] = {0x40e00000u, 0x8000033cu, 0xffc12345u,
                        0x80000001u, 0x8000033cu, 0x3f000000u, 0x8000033cu, 0x7f800000u};
                    for (auto *fx : {&nativeMemory, &oracleMemory})
                    {
                        for (unsigned index = 0u; index < phase + 18u; ++index)
                            writeVuInstructionPair(fx->code, index * 8u, 0x8000033cu, kVuUpperNop);
                        for (unsigned index = 0u; index < 8u; ++index)
                            writeVuInstructionPair(fx->code, (phase + index) * 8u, lower[index], upper[index]);
                    }
                    VU1Interpreter native, oracle;
                    native.setCompiledExecutionEnabled(false);
                    oracle.setCompiledExecutionEnabled(false);
                    native.state().i = 3.5f;
                    for (unsigned lane = 0u; lane < 4u; ++lane)
                    {
                        native.state().vf[1][lane] = static_cast<float>(lane + 1u);
                        native.state().vf[2][lane] = -static_cast<float>(lane + 2u);
                    }
                    oracle.state() = native.state();
                    if (phase != 0u)
                    {
                        native.execute(nativeMemory.code, PS2_VU1_CODE_SIZE, nativeMemory.data, PS2_VU1_DATA_SIZE,
                            nativeMemory.gs, &nativeMemory.mem, 0u, 0u, 0u, phase);
                        oracle.execute(oracleMemory.code, PS2_VU1_CODE_SIZE, oracleMemory.data, PS2_VU1_DATA_SIZE,
                            oracleMemory.gs, &oracleMemory.mem, 0u, 0u, 0u, phase);
                    }
                    native.setCompiledExecutionEnabled(true);
                    const uint64_t accepted = ps2_vu_detail::regionCounters.accepted[1];
                    for (unsigned step = 0u; step < 9u; ++step)
                    {
                        if (step != 0u)
                            native.setCompiledExecutionEnabled(false);
                        const unsigned cycles = step == 0u ? budget : 1u;
                        native.resume(nativeMemory.code, PS2_VU1_CODE_SIZE, nativeMemory.data, PS2_VU1_DATA_SIZE,
                            nativeMemory.gs, &nativeMemory.mem, 0u, 0u, cycles);
                        oracle.resume(oracleMemory.code, PS2_VU1_CODE_SIZE, oracleMemory.data, PS2_VU1_DATA_SIZE,
                            oracleMemory.gs, &oracleMemory.mem, 0u, 0u, cycles);
                        t.IsTrue(std::memcmp(&native.state(), &oracle.state(), sizeof(VU1State)) == 0,
                            "old I operands, normalization and delayed flags must match at every boundary");
                        t.IsTrue(std::memcmp(nativeMemory.data, oracleMemory.data, PS2_VU1_DATA_SIZE) == 0,
                            "LOI bits must not execute as a lower instruction");
                        if (step == 0u && std::getenv("PS2_VU_REQUIRE_REGIONS"))
                            t.Equals(ps2_vu_detail::regionCounters.accepted[1] - accepted,
                                budget >= 8u ? uint64_t{8u} : (budget >= 4u ? uint64_t{4u} : uint64_t{0u}),
                                "LOI fixture must force the longest fitting native SSA region");
                    }
                }
        });
        tc.Run("counted loops retain register versions and exact delayed exits", [](TestCase &t)
        {
            for (unsigned trips : {1u, 2u, 9u})
                for (int initial : {13, 32766})
                    for (bool alias : {false, true})
                        for (unsigned budget : {0u, 1u, 6u, 7u, 8u, 13u, 14u, 15u, 20u, 21u, 22u, 70u, 71u})
                        {
                            Vu1Fixture nativeMemory, oracleMemory;
                            t.IsTrue(nativeMemory.initialize() && oracleMemory.initialize(), "loop fixtures initialize");
                            for (auto *fx : {&nativeMemory, &oracleMemory})
                            {
                                for (unsigned pc = 0; pc < 0x400u; pc += 8u)
                                    writeVuInstructionPair(fx->code, pc, 0x8000033cu, kVuUpperNop);
                                writeCountedLoopCode(*fx);
                                for (unsigned offset = 0; offset < PS2_VU1_DATA_SIZE; offset += 4u)
                                {
                                    const uint32_t value = 0x8010203u ^ (offset * 0x10203u);
                                    std::memcpy(fx->data + offset, &value, sizeof(value));
                                }
                            }
                            VU1Interpreter native, oracle;
                            oracle.setCompiledExecutionEnabled(false);
                            for (unsigned reg = 1; reg < 32u; ++reg)
                                for (unsigned lane = 0; lane < 4u; ++lane)
                                    native.state().vf[reg][lane] = static_cast<float>(reg * (lane + 1u)) * 0.03125f;
                            native.state().vi[10] = static_cast<int16_t>(initial);
                            native.state().vi[11] = static_cast<int16_t>(initial + (alias ? 0 : 7));
                            native.state().vi[12] = static_cast<int16_t>(initial + (alias ? 0 : 15));
                            native.state().vi[13] = static_cast<int16_t>(initial + trips - 1u);
                            native.state().status = 0xa50u;
                            native.state().clip = 0xabcdeu;
                            oracle.state() = native.state();
                            const uint64_t accepted = ps2_vu_detail::countedLoopPairs;
                            native.execute(nativeMemory.code, PS2_VU1_CODE_SIZE, nativeMemory.data, PS2_VU1_DATA_SIZE,
                                           nativeMemory.gs, &nativeMemory.mem, 0u, 0u, 0u, budget);
                            oracle.execute(oracleMemory.code, PS2_VU1_CODE_SIZE, oracleMemory.data, PS2_VU1_DATA_SIZE,
                                           oracleMemory.gs, &oracleMemory.mem, 0u, 0u, 0u, budget);
                            if (std::getenv("PS2_VU_REQUIRE_COUNTED_LOOPS"))
                                t.Equals(ps2_vu_detail::countedLoopPairs - accepted,
                                    static_cast<uint64_t>(7u * std::min(trips, budget / 7u)),
                                    "complete iterations execute in the retained loop path");
                            for (unsigned step = 0; step < 10u; ++step)
                            {
                                const bool stateEqual = std::memcmp(&native.state(), &oracle.state(), sizeof(VU1State)) == 0;
                                const bool dataEqual = std::memcmp(nativeMemory.data, oracleMemory.data, PS2_VU1_DATA_SIZE) == 0;
                                if (!stateEqual || !dataEqual)
                                    std::fprintf(stderr, "loop mismatch trips=%u initial=%d alias=%u budget=%u step=%u pc=%u/%u cycle=%llu/%llu\n",
                                        trips, initial, alias, budget, step, native.state().pc, oracle.state().pc,
                                        static_cast<unsigned long long>(native.state().cycles),
                                        static_cast<unsigned long long>(oracle.state().cycles));
                                t.IsTrue(stateEqual, "visible and pending loop results match raw execution at each boundary");
                                t.IsTrue(dataEqual, "loop loads and delay-slot stores retain alias ordering");
                                native.resume(nativeMemory.code, PS2_VU1_CODE_SIZE, nativeMemory.data, PS2_VU1_DATA_SIZE,
                                              nativeMemory.gs, &nativeMemory.mem, 0u, 0u, 1u);
                                oracle.resume(oracleMemory.code, PS2_VU1_CODE_SIZE, oracleMemory.data, PS2_VU1_DATA_SIZE,
                                              oracleMemory.gs, &oracleMemory.mem, 0u, 0u, 1u);
                            }
                        }
        });
        tc.Run("counted loops reenter through pending tails with raw operand bits", [](TestCase &t)
        {
            uint32_t random = 0x61e04abdu;
            const auto next = [&]() {
                random ^= random << 13u;
                random ^= random >> 17u;
                random ^= random << 5u;
                return random;
            };
            for (unsigned trial = 0u; trial < 128u; ++trial)
            {
                Vu1Fixture nativeMemory, oracleMemory;
                t.IsTrue(nativeMemory.initialize() && oracleMemory.initialize(), "reentry fixtures initialize");
                for (auto *fx : {&nativeMemory, &oracleMemory})
                    writeCountedLoopCode(*fx);
                for (unsigned offset = 0u; offset < PS2_VU1_DATA_SIZE; offset += 4u)
                {
                    const uint32_t value = next();
                    std::memcpy(nativeMemory.data + offset, &value, sizeof(value));
                    std::memcpy(oracleMemory.data + offset, &value, sizeof(value));
                }
                VU1Interpreter native, oracle;
                oracle.setCompiledExecutionEnabled(false);
                for (unsigned reg = 1u; reg < 32u; ++reg)
                    for (unsigned lane = 0u; lane < 4u; ++lane)
                        native.state().vf[reg][lane] = std::bit_cast<float>(next());
                for (unsigned lane = 0u; lane < 4u; ++lane)
                    native.state().acc[lane] = std::bit_cast<float>(next());
                const int16_t initial = static_cast<int16_t>(next());
                native.state().vi[10] = initial;
                native.state().vi[11] = static_cast<int16_t>(next());
                native.state().vi[12] = static_cast<int16_t>(next());
                native.state().vi[13] = static_cast<int16_t>(initial + 24);
                native.state().status = next() & 0xfffu;
                native.state().mac = next() & 0xffffu;
                native.state().clip = next() & 0xffffffu;
                oracle.state() = native.state();
                unsigned step = 0u;
                for (unsigned budget : {7u, 7u, 14u, 1u, 6u, 21u, 2u, 5u, 7u, 7u, 70u, 7u})
                {
                    const uint64_t accepted = ps2_vu_detail::countedLoopPairs;
                    if (step == 0u)
                    {
                        native.execute(nativeMemory.code, PS2_VU1_CODE_SIZE, nativeMemory.data, PS2_VU1_DATA_SIZE,
                                       nativeMemory.gs, &nativeMemory.mem, 0u, 0u, 0u, budget);
                        oracle.execute(oracleMemory.code, PS2_VU1_CODE_SIZE, oracleMemory.data, PS2_VU1_DATA_SIZE,
                                       oracleMemory.gs, &oracleMemory.mem, 0u, 0u, 0u, budget);
                    }
                    else
                    {
                        native.resume(nativeMemory.code, PS2_VU1_CODE_SIZE, nativeMemory.data, PS2_VU1_DATA_SIZE,
                                      nativeMemory.gs, &nativeMemory.mem, 0u, 0u, budget);
                        oracle.resume(oracleMemory.code, PS2_VU1_CODE_SIZE, oracleMemory.data, PS2_VU1_DATA_SIZE,
                                      oracleMemory.gs, &oracleMemory.mem, 0u, 0u, budget);
                    }
                    t.IsTrue(std::memcmp(&native.state(), &oracle.state(), sizeof(VU1State)) == 0,
                             "retained loop reentry preserves all visible state and the +1/+2/+3 tails");
                    t.IsTrue(std::memcmp(nativeMemory.data, oracleMemory.data, PS2_VU1_DATA_SIZE) == 0,
                             "raw bit patterns preserve exact load/store and masked-lane results");
                    if (step < 3u && std::getenv("PS2_VU_REQUIRE_COUNTED_LOOPS"))
                        t.Equals(ps2_vu_detail::countedLoopPairs - accepted, static_cast<uint64_t>(budget),
                                 "each initial bounded resume reenters the retained loop with pending VF writes");
                    ++step;
                }
            }
        });
        tc.Run("counted-loop branches use the preceding VI write backup", [](TestCase &t)
        {
            for (int initial : {13, 32767})
                for (int distance : {0, 1})
                    for (unsigned budget : {6u, 7u, 14u, 21u})
                    {
                        Vu1Fixture nativeMemory, oracleMemory;
                        t.IsTrue(nativeMemory.initialize() && oracleMemory.initialize(), "branch fixtures initialize");
                        for (auto *fx : {&nativeMemory, &oracleMemory})
                        {
                            writeCountedLoopCode(*fx);
                            writeVuInstructionPair(fx->code, 40u, makeVuIbne(11u, 10u, -6),
                                makeVuUpperSpecial(0x13u, 14u, 24u, 25u));
                        }
                        VU1Interpreter native, oracle;
                        oracle.setCompiledExecutionEnabled(false);
                        native.state().vi[10] = static_cast<int16_t>(initial);
                        native.state().vi[11] = static_cast<int16_t>(initial + distance);
                        native.state().vi[12] = 30;
                        oracle.state() = native.state();
                        const uint64_t accepted = ps2_vu_detail::countedLoopPairs;
                        native.execute(nativeMemory.code, PS2_VU1_CODE_SIZE, nativeMemory.data, PS2_VU1_DATA_SIZE,
                                       nativeMemory.gs, &nativeMemory.mem, 0u, 0u, 0u, budget);
                        oracle.execute(oracleMemory.code, PS2_VU1_CODE_SIZE, oracleMemory.data, PS2_VU1_DATA_SIZE,
                                       oracleMemory.gs, &oracleMemory.mem, 0u, 0u, 0u, budget);
                        if (std::getenv("PS2_VU_REQUIRE_COUNTED_LOOPS"))
                            t.Equals(ps2_vu_detail::countedLoopPairs - accepted,
                                static_cast<uint64_t>(7u * (distance == 0 ? std::min(1u, budget / 7u) : budget / 7u)),
                                "the branch-backup fixture enters the retained loop");
                        for (unsigned step = 0; step < 10u; ++step)
                        {
                            t.IsTrue(std::memcmp(&native.state(), &oracle.state(), sizeof(VU1State)) == 0,
                                     "IBNE compares the old VI value before SQI and its delay slot");
                            t.IsTrue(std::memcmp(nativeMemory.data, oracleMemory.data, PS2_VU1_DATA_SIZE) == 0,
                                     "branch backup does not reorder stores");
                            native.resume(nativeMemory.code, PS2_VU1_CODE_SIZE, nativeMemory.data, PS2_VU1_DATA_SIZE,
                                          nativeMemory.gs, &nativeMemory.mem, 0u, 0u, 1u);
                            oracle.resume(oracleMemory.code, PS2_VU1_CODE_SIZE, oracleMemory.data, PS2_VU1_DATA_SIZE,
                                          oracleMemory.gs, &oracleMemory.mem, 0u, 0u, 1u);
                        }
                    }
        });
        tc.Run("counted loops yield to code replacement between budgets", [](TestCase &t)
        {
            for (uint32_t patchPC : {0u, 32u, 40u, 48u})
            {
                Vu1Fixture nativeMemory, oracleMemory;
                t.IsTrue(nativeMemory.initialize() && oracleMemory.initialize(), "invalidation fixtures initialize");
                for (auto *fx : {&nativeMemory, &oracleMemory})
                    writeCountedLoopCode(*fx);
                VU1Interpreter native, oracle;
                oracle.setCompiledExecutionEnabled(false);
                native.state().vi[10] = 13;
                native.state().vi[11] = 20;
                native.state().vi[12] = 30;
                native.state().vi[13] = 100;
                oracle.state() = native.state();
                for (unsigned step = 0; step < 2u; ++step)
                {
                    const uint64_t accepted = ps2_vu_detail::countedLoopPairs;
                    if (step == 0u)
                    {
                        native.execute(nativeMemory.code, PS2_VU1_CODE_SIZE, nativeMemory.data, PS2_VU1_DATA_SIZE,
                                       nativeMemory.gs, &nativeMemory.mem, 0u, 0u, 0u, 21u);
                        oracle.execute(oracleMemory.code, PS2_VU1_CODE_SIZE, oracleMemory.data, PS2_VU1_DATA_SIZE,
                                       oracleMemory.gs, &oracleMemory.mem, 0u, 0u, 0u, 21u);
                    }
                    else
                    {
                        for (auto *fx : {&nativeMemory, &oracleMemory})
                            writeTrackedVuInstructionPair(*fx, patchPC, makeVuIaddiu(15u, 0u, 123), kVuUpperNop);
                        native.resume(nativeMemory.code, PS2_VU1_CODE_SIZE, nativeMemory.data, PS2_VU1_DATA_SIZE,
                                      nativeMemory.gs, &nativeMemory.mem, 0u, 0u, 7u);
                        oracle.resume(oracleMemory.code, PS2_VU1_CODE_SIZE, oracleMemory.data, PS2_VU1_DATA_SIZE,
                                      oracleMemory.gs, &oracleMemory.mem, 0u, 0u, 7u);
                    }
                    t.IsTrue(std::memcmp(&native.state(), &oracle.state(), sizeof(VU1State)) == 0,
                             "code generation invalidates the old loop before another iteration");
                    t.IsTrue(std::memcmp(nativeMemory.data, oracleMemory.data, PS2_VU1_DATA_SIZE) == 0,
                             "invalidation preserves pending stores and VF tails");
                    if (std::getenv("PS2_VU_REQUIRE_COUNTED_LOOPS"))
                        t.Equals(ps2_vu_detail::countedLoopPairs - accepted, step == 0u ? uint64_t{21u} : uint64_t{0u},
                                 "only the original code image uses the compiled loop");
                }
                t.Equals(native.state().vi[15], 123, "replacement instruction executes");
            }
        });
        tc.Run("counted loops fall back while XGKICK can replace code", [](TestCase &t)
        {
            Vu1Fixture nativeMemory, oracleMemory;
            t.IsTrue(nativeMemory.initialize() && oracleMemory.initialize(), "XGKICK fixtures initialize");
            std::vector<std::vector<uint8_t>> nativePackets, oraclePackets;
            const auto setup = [&](Vu1Fixture &fx, std::vector<std::vector<uint8_t>> &packets) {
                writeVuInstructionPair(fx.code, 0u, makeVuLowerSpecial(0x6cu, 14u), kVuUpperNop);
                writeCountedLoopCode(fx, 8u);
                const uint64_t tag = makeGifTag(4u, GIF_FMT_IMAGE, 0u, true);
                std::memcpy(fx.data + 800u * 16u, &tag, sizeof(tag));
                fx.mem.setGifPacketCallback([&fx, &packets](const uint8_t *data, uint32_t size) {
                    packets.emplace_back(data, data + size);
                    writeTrackedVuInstructionPair(fx, 16u, makeVuIaddiu(15u, 0u, 321), kVuUpperNop);
                });
            };
            setup(nativeMemory, nativePackets);
            setup(oracleMemory, oraclePackets);
            VU1Interpreter native, oracle;
            oracle.setCompiledExecutionEnabled(false);
            native.state().vi[10] = 13;
            native.state().vi[11] = 20;
            native.state().vi[12] = 30;
            native.state().vi[13] = 100;
            native.state().vi[14] = 800;
            oracle.state() = native.state();
            const uint64_t accepted = ps2_vu_detail::countedLoopPairs;
            native.execute(nativeMemory.code, PS2_VU1_CODE_SIZE, nativeMemory.data, PS2_VU1_DATA_SIZE,
                           nativeMemory.gs, &nativeMemory.mem, 0u, 0u, 0u, 1u);
            oracle.execute(oracleMemory.code, PS2_VU1_CODE_SIZE, oracleMemory.data, PS2_VU1_DATA_SIZE,
                           oracleMemory.gs, &oracleMemory.mem, 0u, 0u, 0u, 1u);
            for (unsigned step = 0; step < 4u; ++step)
            {
                native.resume(nativeMemory.code, PS2_VU1_CODE_SIZE, nativeMemory.data, PS2_VU1_DATA_SIZE,
                              nativeMemory.gs, &nativeMemory.mem, 0u, 0u, 7u);
                oracle.resume(oracleMemory.code, PS2_VU1_CODE_SIZE, oracleMemory.data, PS2_VU1_DATA_SIZE,
                              oracleMemory.gs, &oracleMemory.mem, 0u, 0u, 7u);
                t.IsTrue(std::memcmp(&native.state(), &oracle.state(), sizeof(VU1State)) == 0,
                         "live PATH1 progress retains exact callback and code-change boundaries");
                t.IsTrue(std::memcmp(nativeMemory.data, oracleMemory.data, PS2_VU1_DATA_SIZE) == 0,
                         "PATH1 fallback preserves loop store ordering");
                t.IsTrue(nativePackets == oraclePackets, "PATH1 captures exact bytes at the original cycles");
                t.Equals(ps2_vu_detail::countedLoopPairs - accepted, uint64_t{0u},
                         "live XGKICK and then the changed code reject the compiled loop");
            }
            t.Equals(nativePackets.size(), size_t{1u}, "XGKICK callback actually ran");
            t.Equals(native.state().vi[15], 321, "callback replacement executed");
        });
        tc.Run("counted loops reject cyclic cancellation before visibility", [](TestCase &t)
        {
            for (bool integer : {false, true})
                for (unsigned budget : {7u, 14u, 21u, 22u, 23u, 24u})
                {
                    Vu1Fixture nativeMemory, oracleMemory;
                    t.IsTrue(nativeMemory.initialize() && oracleMemory.initialize(), "cancellation fixtures initialize");
                    for (auto *fx : {&nativeMemory, &oracleMemory})
                    {
                        for (unsigned index = 0; index < 16u; ++index)
                            writeVuInstructionPair(fx->code, index * 8u, 0x8000033cu, kVuUpperNop);
                        if (integer)
                        {
                            for (unsigned index : {0u, 2u, 4u})
                                writeVuInstructionPair(fx->code, index * 8u, makeVuIlw(8u, 14u, 0u, 2), kVuUpperNop);
                        }
                        else
                        {
                            for (unsigned index : {0u, 3u, 6u})
                                writeVuInstructionPair(fx->code, index * 8u, 0x8000033cu,
                                    makeVuUpper(0x28u, 8u, 17u, 16u, 20u));
                        }
                        writeVuInstructionPair(fx->code, 40u, makeVuIbne(13u, 10u, -6), kVuUpperNop);
                        writeVuInstructionPair(fx->code, 48u, makeVuLowerSpecial(0x35u, 25u, 10u, 0u, 8u),
                            integer ? kVuUpperNop : makeVuUpper(0x28u, 8u, 17u, 16u, 20u));
                        const uint32_t value = 567u;
                        std::memcpy(fx->data + 32u, &value, sizeof(value));
                    }
                    VU1Interpreter native, oracle;
                    oracle.setCompiledExecutionEnabled(false);
                    native.state().vi[10] = 20;
                    native.state().vi[13] = 22;
                    native.state().vi[14] = 123;
                    native.state().vf[16][0] = 1.0f;
                    native.state().vf[17][0] = 2.0f;
                    native.state().vf[20][0] = 7.0f;
                    oracle.state() = native.state();
                    const uint64_t accepted = ps2_vu_detail::countedLoopPairs;
                    native.execute(nativeMemory.code, PS2_VU1_CODE_SIZE, nativeMemory.data, PS2_VU1_DATA_SIZE,
                                   nativeMemory.gs, &nativeMemory.mem, 0u, 0u, 0u, budget);
                    oracle.execute(oracleMemory.code, PS2_VU1_CODE_SIZE, oracleMemory.data, PS2_VU1_DATA_SIZE,
                                   oracleMemory.gs, &oracleMemory.mem, 0u, 0u, 0u, budget);
                    t.Equals(ps2_vu_detail::countedLoopPairs - accepted, uint64_t{0u},
                             "a next-iteration write that cancels an incoming tail rejects loop retention");
                    for (unsigned step = 0; step < 6u; ++step)
                    {
                        t.IsTrue(std::memcmp(&native.state(), &oracle.state(), sizeof(VU1State)) == 0,
                                 "canceled tail values must not become architecturally visible");
                        native.resume(nativeMemory.code, PS2_VU1_CODE_SIZE, nativeMemory.data, PS2_VU1_DATA_SIZE,
                                      nativeMemory.gs, &nativeMemory.mem, 0u, 0u, 1u);
                        oracle.resume(oracleMemory.code, PS2_VU1_CODE_SIZE, oracleMemory.data, PS2_VU1_DATA_SIZE,
                                      oracleMemory.gs, &oracleMemory.mem, 0u, 0u, 1u);
                    }
                }
        });
        tc.Run("early counter loops retain ACC and exact masked flag tails", [](TestCase &t)
        {
            uint32_t random = 0x7293bd41u;
            const auto next = [&]() {
                random ^= random << 13u;
                random ^= random >> 17u;
                random ^= random << 5u;
                return random;
            };
            for (int initial : {13, 32760})
                for (unsigned trips : {1u, 2u, 7u})
                    for (unsigned budget : {0u, 1u, 8u, 9u, 10u, 17u, 18u, 19u, 26u, 27u, 28u, 63u, 64u})
                    {
                        Vu1Fixture nativeMemory, oracleMemory;
                        t.IsTrue(nativeMemory.initialize() && oracleMemory.initialize(), "early counter fixtures initialize");
                        for (auto *fx : {&nativeMemory, &oracleMemory})
                        {
                            for (unsigned pc = 0u; pc < 0x400u; pc += 8u)
                                writeVuInstructionPair(fx->code, pc, 0x8000033cu, kVuUpperNop);
                            writeEarlyCounterLoopCode(*fx);
                            writeVuInstructionPair(fx->code, 72u, makeVuIbne(11u, 12u, 2), kVuUpperNop);
                        }
                        for (unsigned offset = 0u; offset < PS2_VU1_DATA_SIZE; offset += 4u)
                        {
                            const uint32_t value = next();
                            std::memcpy(nativeMemory.data + offset, &value, sizeof(value));
                            std::memcpy(oracleMemory.data + offset, &value, sizeof(value));
                        }
                        VU1Interpreter native, oracle;
                        oracle.setCompiledExecutionEnabled(false);
                        for (unsigned reg = 1u; reg < 32u; ++reg)
                            for (unsigned lane = 0u; lane < 4u; ++lane)
                                native.state().vf[reg][lane] = std::bit_cast<float>(next());
                        for (unsigned lane = 0u; lane < 4u; ++lane)
                            native.state().acc[lane] = std::bit_cast<float>(next());
                        native.state().vi[10] = static_cast<int16_t>(initial - 2);
                        native.state().vi[11] = static_cast<int16_t>(initial);
                        native.state().vi[12] = static_cast<int16_t>(initial + 5 * trips);
                        native.state().mac = next() & 0xffffu;
                        native.state().status = next() & 0xfffu;
                        oracle.state() = native.state();
                        const uint64_t accepted = ps2_vu_detail::countedLoopPairs;
                        native.execute(nativeMemory.code, PS2_VU1_CODE_SIZE, nativeMemory.data, PS2_VU1_DATA_SIZE,
                                       nativeMemory.gs, &nativeMemory.mem, 0u, 0u, 0u, budget);
                        oracle.execute(oracleMemory.code, PS2_VU1_CODE_SIZE, oracleMemory.data, PS2_VU1_DATA_SIZE,
                                       oracleMemory.gs, &oracleMemory.mem, 0u, 0u, 0u, budget);
                        if (std::getenv("PS2_VU_REQUIRE_COUNTED_LOOPS"))
                            t.Equals(ps2_vu_detail::countedLoopPairs - accepted,
                                     static_cast<uint64_t>(9u * std::min(trips, budget / 9u)),
                                     "whole nine-pair iterations use retained ACC and early-counter execution");
                        for (unsigned step = 0u; step < 10u; ++step)
                        {
                            t.IsTrue(std::memcmp(&native.state(), &oracle.state(), sizeof(VU1State)) == 0,
                                     "ACC, masked VF results and FMAC flags match at every delayed boundary");
                            t.IsTrue(std::memcmp(nativeMemory.data, oracleMemory.data, PS2_VU1_DATA_SIZE) == 0,
                                     "SQ uses the updated signed counter after LQI and masked MOVE");
                            native.resume(nativeMemory.code, PS2_VU1_CODE_SIZE, nativeMemory.data, PS2_VU1_DATA_SIZE,
                                          nativeMemory.gs, &nativeMemory.mem, 0u, 0u, 1u);
                            oracle.resume(oracleMemory.code, PS2_VU1_CODE_SIZE, oracleMemory.data, PS2_VU1_DATA_SIZE,
                                          oracleMemory.gs, &oracleMemory.mem, 0u, 0u, 1u);
                        }
                    }
        });
        tc.Run("early counter loops reenter and invalidate every late pair", [](TestCase &t)
        {
            for (unsigned patchPC : {32u, 40u, 48u, 56u, 64u})
            {
                Vu1Fixture nativeMemory, oracleMemory;
                t.IsTrue(nativeMemory.initialize() && oracleMemory.initialize(), "early counter reentry fixtures initialize");
                for (auto *fx : {&nativeMemory, &oracleMemory})
                    writeEarlyCounterLoopCode(*fx);
                VU1Interpreter native, oracle;
                oracle.setCompiledExecutionEnabled(false);
                native.state().vi[10] = 22;
                native.state().vi[11] = 24;
                native.state().vi[12] = 200;
                for (unsigned reg = 1u; reg < 32u; ++reg)
                    for (unsigned lane = 0u; lane < 4u; ++lane)
                        native.state().vf[reg][lane] = static_cast<float>(reg + lane) * 0.125f;
                oracle.state() = native.state();
                for (unsigned step = 0u; step < 4u; ++step)
                {
                    const uint64_t accepted = ps2_vu_detail::countedLoopPairs;
                    if (step == 3u)
                        for (auto *fx : {&nativeMemory, &oracleMemory})
                            writeTrackedVuInstructionPair(*fx, patchPC, makeVuIaddiu(15u, 0u, 123), kVuUpperNop);
                    native.resume(nativeMemory.code, PS2_VU1_CODE_SIZE, nativeMemory.data, PS2_VU1_DATA_SIZE,
                                  nativeMemory.gs, &nativeMemory.mem, 0u, 0u, 9u);
                    oracle.resume(oracleMemory.code, PS2_VU1_CODE_SIZE, oracleMemory.data, PS2_VU1_DATA_SIZE,
                                  oracleMemory.gs, &oracleMemory.mem, 0u, 0u, 9u);
                    t.IsTrue(std::memcmp(&native.state(), &oracle.state(), sizeof(VU1State)) == 0,
                             "resumed ACC/flag/VF tails and late counter-loop edits match raw execution");
                    t.IsTrue(std::memcmp(nativeMemory.data, oracleMemory.data, PS2_VU1_DATA_SIZE) == 0,
                             "late-pair invalidation retains exact load/store order");
                    if (std::getenv("PS2_VU_REQUIRE_COUNTED_LOOPS"))
                        t.Equals(ps2_vu_detail::countedLoopPairs - accepted, step < 3u ? uint64_t{9u} : uint64_t{0u},
                                 "nine-pair reentry is active until any late instruction changes");
                }
                t.Equals(native.state().vi[15], 123, "late replacement executes");
            }
        });
        tc.Run("upper ADD applies the destination mask", [](TestCase &t)
        {
            Vu1Fixture fx;
            t.IsTrue(fx.initialize(), "VU1 fixture should initialize");

            writeVuInstructionPair(fx.code, 0u, 0u, makeVuUpper(0x28u, 0xAu, 2u, 1u, 3u)); // ADD.xz vf3, vf1, vf2

            VU1Interpreter vu1;
            vu1.state().vf[1][0] = 1.0f;
            vu1.state().vf[1][1] = 2.0f;
            vu1.state().vf[1][2] = 3.0f;
            vu1.state().vf[1][3] = 4.0f;
            vu1.state().vf[2][0] = 10.0f;
            vu1.state().vf[2][1] = 20.0f;
            vu1.state().vf[2][2] = 30.0f;
            vu1.state().vf[2][3] = 40.0f;
            vu1.state().vf[3][0] = -1.0f;
            vu1.state().vf[3][1] = -2.0f;
            vu1.state().vf[3][2] = -3.0f;
            vu1.state().vf[3][3] = -4.0f;

            vu1.execute(fx.code, PS2_VU1_CODE_SIZE, fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem, 0u, 0u, 0u, 1u);

            t.Equals(vu1.state().vf[3][0], -1.0f,
                     "FMAC destination must remain hidden before its four-cycle writeback");
            vu1.resume(fx.code, PS2_VU1_CODE_SIZE, fx.data, PS2_VU1_DATA_SIZE,
                       fx.gs, &fx.mem, 0u, 0u, 3u);
            t.Equals(vu1.state().vf[3][0], 11.0f, "ADD.x should write x");
            t.Equals(vu1.state().vf[3][1], -2.0f, "ADD.xz should preserve y");
            t.Equals(vu1.state().vf[3][2], 33.0f, "ADD.xz should write z");
            t.Equals(vu1.state().vf[3][3], -4.0f, "ADD.xz should preserve w");
        });

        tc.Run("LOI commits the lower immediate after the upper instruction", [](TestCase &t)
        {
            Vu1Fixture fx;
            t.IsTrue(fx.initialize(), "VU1 fixture should initialize");

            const float newI = 7.0f;
            uint32_t lowerImmediate = 0u;
            std::memcpy(&lowerImmediate, &newI, sizeof(newI));
            const uint32_t upperAddiWithIBit = makeVuUpper(0x22u, 0xFu, 0u, 1u, 2u) | 0x80000000u; // ADDi.xyzw vf2, vf1
            writeVuInstructionPair(fx.code, 0u, lowerImmediate, upperAddiWithIBit);

            VU1Interpreter vu1;
            vu1.state().i = 2.0f;
            vu1.state().vf[1][0] = 1.0f;
            vu1.state().vf[1][1] = 2.0f;
            vu1.state().vf[1][2] = 3.0f;
            vu1.state().vf[1][3] = 4.0f;

            vu1.execute(fx.code, PS2_VU1_CODE_SIZE, fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem, 0u, 0u, 0u, 1u);

            t.Equals(vu1.state().vf[2][0], 0.0f,
                     "ADDi result should remain in the FMAC pipeline");
            t.Equals(vu1.state().i, 7.0f,
                     "LOI should become visible after the upper from the same pair");
            vu1.resume(fx.code, PS2_VU1_CODE_SIZE, fx.data, PS2_VU1_DATA_SIZE,
                       fx.gs, &fx.mem, 0u, 0u, 3u);
            t.Equals(vu1.state().vf[2][0], 3.0f, "ADDi should use old I for x");
            t.Equals(vu1.state().vf[2][1], 4.0f, "ADDi should use old I for y");
            t.Equals(vu1.state().vf[2][2], 5.0f, "ADDi should use old I for z");
            t.Equals(vu1.state().vf[2][3], 6.0f, "ADDi should use old I for w");
            t.Equals(vu1.state().i, 7.0f, "LOI should commit lower immediate into I after upper execution");
        });

        tc.Run("ITOF converts the raw signed integer bits without float normalization", [](TestCase &t)
        {
            Vu1Fixture fx;
            t.IsTrue(fx.initialize(), "VU1 fixture should initialize");

            writeVuInstructionPair(
                fx.code, 0u, 0u,
                makeVuUpperSpecial(0x10u, 0xFu, 2u, 1u));

            VU1Interpreter vu1;
            const int32_t raw[4] = {1, -16, 4096, -32768};
            std::memcpy(vu1.state().vf[1], raw, sizeof(raw));
            vu1.execute(fx.code, PS2_VU1_CODE_SIZE,
                        fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem,
                        0u, 0u, 0u, 4u);

            t.Equals(vu1.state().vf[2][0], 1.0f,
                     "ITOF0 must not flush an integer bit pattern that resembles a denormal");
            t.Equals(vu1.state().vf[2][1], -16.0f,
                     "ITOF0 must preserve negative integer bit patterns");
            t.Equals(vu1.state().vf[2][2], 4096.0f,
                     "ITOF0 should convert positive fixed-point source bits");
            t.Equals(vu1.state().vf[2][3], -32768.0f,
                     "ITOF0 should convert negative fixed-point source bits");
        });

        tc.Run("MTIR decodes fsf as a component selector", [](TestCase &t)
        {
            Vu1Fixture fx;
            t.IsTrue(fx.initialize(), "VU1 fixture should initialize");

            for (uint32_t component = 0; component < 4u; ++component)
            {
                writeVuInstructionPair(
                    fx.code, component * 8u,
                    makeVuLowerSpecial(0x3Cu, 1u,
                                       static_cast<uint8_t>(component + 2u),
                                       0u,
                                       static_cast<uint8_t>(component)),
                    kVuUpperNop);
            }

            VU1Interpreter vu1;
            const uint32_t raw[4] = {0x00001111u, 0x00002222u, 0x00003333u, 0x00004444u};
            std::memcpy(vu1.state().vf[1], raw, sizeof(raw));
            vu1.execute(fx.code, PS2_VU1_CODE_SIZE,
                        fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem,
                        0u, 0u, 0u, 4u);

            t.Equals(vu1.state().vi[2], 0x1111,
                     "MTIR fsf=x should read VF.x");
            t.Equals(vu1.state().vi[3], 0x2222,
                     "MTIR fsf=y should read VF.y");
            t.Equals(vu1.state().vi[4], 0x3333,
                     "MTIR fsf=z should read VF.z");
            t.Equals(vu1.state().vi[5], 0x4444,
                     "MTIR fsf=w should read VF.w");
        });

        tc.Run("LQ and SQ use VI qword addressing and destination masks", [](TestCase &t)
        {
            Vu1Fixture fx;
            t.IsTrue(fx.initialize(), "VU1 fixture should initialize");

            const float sourceQw[4] = {10.0f, 20.0f, 30.0f, 40.0f};
            const float destQw[4] = {-1.0f, -2.0f, -3.0f, -4.0f};
            writeVuQword(fx.data, 3u, sourceQw);
            writeVuQword(fx.data, 5u, destQw);
            writeVuInstructionPair(fx.code, 0u, makeVuLq(0x5u, 4u, 1u, 1), kVuUpperNop); // LQ.yw vf4, 1(vi1)
            writeVuInstructionPair(fx.code, 8u, makeVuSq(0xAu, 4u, 2u, 1), kVuUpperNop); // SQ.xz vf4, 1(vi2)

            VU1Interpreter vu1;
            vu1.state().vi[1] = 2;
            vu1.state().vi[2] = 4;
            vu1.state().vf[4][0] = 100.0f;
            vu1.state().vf[4][1] = 200.0f;
            vu1.state().vf[4][2] = 300.0f;
            vu1.state().vf[4][3] = 400.0f;

            vu1.execute(fx.code, PS2_VU1_CODE_SIZE, fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem, 0u, 0u, 0u, 3u);

            t.Equals(vu1.state().vf[4][1], 200.0f,
                     "LQ result should remain hidden before its fourth cycle");
            vu1.resume(fx.code, PS2_VU1_CODE_SIZE, fx.data, PS2_VU1_DATA_SIZE,
                       fx.gs, &fx.mem, 0u, 0u, 1u);
            t.Equals(vu1.state().vf[4][0], 100.0f, "LQ.yw should preserve x");
            t.Equals(vu1.state().vf[4][1], 20.0f, "LQ.yw should load y");
            t.Equals(vu1.state().vf[4][2], 300.0f, "LQ.yw should preserve z");
            t.Equals(vu1.state().vf[4][3], 40.0f, "LQ.yw should load w");

            float stored[4] = {};
            readVuQword(fx.data, 5u, stored);
            t.Equals(stored[0], 100.0f, "SQ.xz should store x");
            t.Equals(stored[1], -2.0f, "SQ.xz should preserve y");
            t.Equals(stored[2], 300.0f, "SQ.xz should store z");
            t.Equals(stored[3], -4.0f, "SQ.xz should preserve w");
            t.Equals(vu1.state().cycles, static_cast<uint64_t>(4u),
                     "disjoint LQ/SQ lanes should issue without delaying LQ writeback");
        });

        tc.Run("integer lower ops keep VI0 hardwired to zero", [](TestCase &t)
        {
            Vu1Fixture fx;
            t.IsTrue(fx.initialize(), "VU1 fixture should initialize");

            writeVuInstructionPair(fx.code, 0u, makeVuIaddiu(2u, 1u, 5), kVuUpperNop);      // IADDIU vi2, vi1, 5
            writeVuInstructionPair(fx.code, 8u, makeVuIaddiu(0u, 2u, 7), kVuUpperNop);      // IADDIU vi0, vi2, 7
            writeVuInstructionPair(fx.code, 16u, makeVuLowerDirect(0x30u, 2u, 1u, 3u), kVuUpperNop); // IADD vi3, vi2, vi1

            VU1Interpreter vu1;
            vu1.state().vi[0] = 99;
            vu1.state().vi[1] = 10;

            vu1.execute(fx.code, PS2_VU1_CODE_SIZE, fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem, 0u, 0u, 0u, 3u);

            t.Equals(vu1.state().vi[2], 15, "IADDIU should add signed immediate to VI source");
            t.Equals(vu1.state().vi[3], 25, "IADD should add VI source registers");
            t.Equals(vu1.state().vi[0], 0, "VI0 should remain hardwired to zero");
        });

        tc.Run("XTOP and XITOP expose VIF TOP values to VI registers", [](TestCase &t)
        {
            Vu1Fixture fx;
            t.IsTrue(fx.initialize(), "VU1 fixture should initialize");

            writeVuInstructionPair(fx.code, 0u, makeVuLowerSpecial(0x68u, 0u, 2u), kVuUpperNop); // XTOP vi2
            writeVuInstructionPair(fx.code, 8u, makeVuLowerSpecial(0x69u, 0u, 3u), kVuUpperNop); // XITOP vi3

            VU1Interpreter vu1;
            vu1.execute(fx.code, PS2_VU1_CODE_SIZE, fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem, 0u, 0x123u, 0x2ABu, 2u);

            t.Equals(vu1.state().vi[2], 0x123, "XTOP should move TOP into the target VI register");
            t.Equals(vu1.state().vi[3], 0x2AB, "XITOP should move ITOP into the target VI register");
        });

        tc.Run("lower branch commits after one delay-slot instruction", [](TestCase &t)
        {
            Vu1Fixture fx;
            t.IsTrue(fx.initialize(), "VU1 fixture should initialize");

            writeVuInstructionPair(fx.code, 0u, makeVuBranch(2), kVuUpperNop);              // target pc = 24
            writeVuInstructionPair(fx.code, 8u, makeVuIaddiu(1u, 0u, 1), kVuUpperNop);      // delay slot
            writeVuInstructionPair(fx.code, 16u, makeVuIaddiu(2u, 0u, 99), kVuUpperNop);    // skipped
            writeVuInstructionPair(fx.code, 24u, makeVuIaddiu(3u, 0u, 7), kVuUpperNop);     // branch target

            VU1Interpreter vu1;
            vu1.execute(fx.code, PS2_VU1_CODE_SIZE, fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem, 0u, 0u, 0u, 3u);

            t.Equals(vu1.state().vi[1], 1, "branch delay slot should execute");
            t.Equals(vu1.state().vi[2], 0, "instruction between delay slot and target should be skipped");
            t.Equals(vu1.state().vi[3], 7, "branch target should execute after the delay slot");
        });

        tc.Run("lower side sees old VF value when upper writes the same register", [](TestCase &t)
        {
            Vu1Fixture fx;
            t.IsTrue(fx.initialize(), "VU1 fixture should initialize");

            writeVuInstructionPair(fx.code,
                                   0u,
                                   makeVuSq(0xFu, 1u, 1u, 0),                 // SQ.xyzw vf1, 0(vi1)
                                   makeVuUpper(0x28u, 0xFu, 3u, 2u, 1u));     // ADD.xyzw vf1, vf2, vf3

            VU1Interpreter vu1;
            vu1.state().vi[1] = 6;
            vu1.state().vf[1][0] = 1.0f;
            vu1.state().vf[1][1] = 2.0f;
            vu1.state().vf[1][2] = 3.0f;
            vu1.state().vf[1][3] = 4.0f;
            vu1.state().vf[2][0] = 10.0f;
            vu1.state().vf[2][1] = 20.0f;
            vu1.state().vf[2][2] = 30.0f;
            vu1.state().vf[2][3] = 40.0f;
            vu1.state().vf[3][0] = 100.0f;
            vu1.state().vf[3][1] = 200.0f;
            vu1.state().vf[3][2] = 300.0f;
            vu1.state().vf[3][3] = 400.0f;

            vu1.execute(fx.code, PS2_VU1_CODE_SIZE, fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem, 0u, 0u, 0u, 2u);

            float stored[4] = {};
            readVuQword(fx.data, 6u, stored);
            t.Equals(stored[0], 1.0f, "SQ should observe old VF value for x");
            t.Equals(stored[1], 2.0f, "SQ should observe old VF value for y");
            t.Equals(stored[2], 3.0f, "SQ should observe old VF value for z");
            t.Equals(stored[3], 4.0f, "SQ should observe old VF value for w");
            vu1.resume(fx.code, PS2_VU1_CODE_SIZE, fx.data, PS2_VU1_DATA_SIZE,
                       fx.gs, &fx.mem, 0u, 0u, 2u);
            t.Equals(vu1.state().vf[1][0], 110.0f, "upper ADD should write x after lower read");
            t.Equals(vu1.state().vf[1][1], 220.0f, "upper ADD should write y after lower read");
            t.Equals(vu1.state().vf[1][2], 330.0f, "upper ADD should write z after lower read");
            t.Equals(vu1.state().vf[1][3], 440.0f, "upper ADD should write w after lower read");
        });

        tc.Run("upper suppresses only the colliding lower VF write", [](TestCase &t)
        {
            Vu1Fixture fx;
            t.IsTrue(fx.initialize(), "VU1 fixture should initialize");

            const float loaded[4] = {90.0f, 91.0f, 92.0f, 93.0f};
            writeVuQword(fx.data, 5u, loaded);
            writeVuInstructionPair(
                fx.code,
                0u,
                makeVuLowerSpecial(0x34u, 1u, 1u, 0u, 0xFu),
                makeVuUpper(0x28u, 0x8u, 3u, 2u, 1u));

            VU1Interpreter vu1;
            vu1.state().vi[1] = 5;
            vu1.state().vf[1][0] = 1.0f;
            vu1.state().vf[1][1] = 2.0f;
            vu1.state().vf[1][2] = 3.0f;
            vu1.state().vf[1][3] = 4.0f;
            vu1.state().vf[2][0] = 10.0f;
            vu1.state().vf[3][0] = 100.0f;

            vu1.execute(fx.code, PS2_VU1_CODE_SIZE,
                        fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem,
                        0u, 0u, 0u, 1u);

            t.Equals(vu1.state().vf[1][0], 1.0f,
                     "upper write should remain pending until the FMAC writeback cycle");
            t.Equals(vu1.state().vi[1], 6,
                     "LQI post-increment should commit after one cycle");
            vu1.resume(fx.code, PS2_VU1_CODE_SIZE,
                       fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem,
                       0u, 0u, 3u);
            t.Equals(vu1.state().vf[1][0], 110.0f,
                     "upper x should be written");
            t.Equals(vu1.state().vf[1][1], 2.0f,
                     "discarded lower must not leak y into the upper result");
            t.Equals(vu1.state().vf[1][2], 3.0f,
                     "discarded lower must not leak z into the upper result");
            t.Equals(vu1.state().vf[1][3], 4.0f,
                      "discarded lower must not leak w into the upper result");
            t.Equals(vu1.state().vi[1], 6,
                     "LQI post-increment must survive suppression of its colliding VF write");
        });

        tc.Run("upper and lower both read the pre-pair VF state", [](TestCase &t)
        {
            Vu1Fixture fx;
            t.IsTrue(fx.initialize(), "VU1 fixture should initialize");

            writeVuInstructionPair(
                fx.code, 0u,
                makeVuLowerSpecial(0x30u, 1u, 2u, 0u, 0x8u),
                makeVuUpper(0x28u, 0x8u, 3u, 2u, 1u));

            VU1Interpreter vu1;
            vu1.state().vf[1][0] = 10.0f;
            vu1.state().vf[2][0] = 20.0f;
            vu1.state().vf[3][0] = 1.0f;
            vu1.execute(fx.code, PS2_VU1_CODE_SIZE,
                        fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem,
                        0u, 0u, 0u, 1u);

            vu1.resume(fx.code, PS2_VU1_CODE_SIZE,
                       fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem,
                       0u, 0u, 3u);
            t.Equals(vu1.state().vf[1][0], 21.0f,
                     "upper must read vf2 before lower MOVE overwrites it");
            t.Equals(vu1.state().vf[2][0], 10.0f,
                     "lower must read vf1 before upper ADD overwrites it");
        });

        tc.Run("FMAC dependency stalls only the lanes that are read", [](TestCase &t)
        {
            Vu1Fixture fx;
            t.IsTrue(fx.initialize(), "VU1 fixture should initialize");

            writeVuInstructionPair(
                fx.code, 0u, 0u,
                makeVuUpper(0x28u, 0x8u, 2u, 1u, 3u));
            writeVuInstructionPair(
                fx.code, 8u, 0u,
                makeVuUpper(0x28u, 0x8u, 2u, 3u, 4u));

            VU1Interpreter vu1;
            vu1.state().vf[1][0] = 1.0f;
            vu1.state().vf[2][0] = 2.0f;
            vu1.execute(fx.code, PS2_VU1_CODE_SIZE,
                        fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem,
                        0u, 0u, 0u, 5u);

            t.Equals(vu1.state().vf[4][0], 0.0f,
                     "the dependent result should remain pending until its own writeback");
            vu1.resume(fx.code, PS2_VU1_CODE_SIZE,
                       fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem,
                       0u, 0u, 3u);
            t.Equals(vu1.state().vf[4][0], 5.0f,
                     "dependent ADD should consume the completed x lane");
            t.Equals(vu1.state().cycles, static_cast<uint64_t>(8u),
                     "dependency stall and the dependent FMAC writeback must both consume cycles");
        });

        tc.Run("ACC forwarding feeds the next upper instruction without a dependency stall", [](TestCase &t)
        {
            Vu1Fixture fx;
            t.IsTrue(fx.initialize(), "VU1 fixture should initialize");

            writeVuInstructionPair(
                fx.code, 0u, 0u,
                makeVuUpperSpecial(0x28u, 0x8u, 2u, 1u)); // ADDA.x acc, vf1, vf2
            writeVuInstructionPair(
                fx.code, 8u, 0u,
                makeVuUpper(0x29u, 0x8u, 4u, 3u, 5u)); // MADD.x vf5, vf3, vf4

            VU1Interpreter vu1;
            vu1.state().vf[1][0] = 1.0f;
            vu1.state().vf[2][0] = 10.0f;
            vu1.state().vf[3][0] = 2.0f;
            vu1.state().vf[4][0] = 3.0f;
            vu1.execute(fx.code, PS2_VU1_CODE_SIZE,
                        fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem,
                        0u, 0u, 0u, 5u);

            t.Equals(vu1.state().acc[0], 11.0f,
                     "ADDA should forward ACC to the following upper instruction");
            t.Equals(vu1.state().vf[5][0], 17.0f,
                     "MADD should consume the forwarded ACC value without stalling");
            t.Equals(vu1.state().cycles, static_cast<uint64_t>(5u),
                     "ACC forwarding must not introduce a four-cycle dependency stall");
        });

        tc.Run("two VF writes per cycle retire across queue wraps and one-cycle resumes", [](TestCase &t)
        {
            Vu1Fixture fx;
            t.IsTrue(fx.initialize(), "VU1 fixture should initialize");
            VU1Interpreter vu;
            for (uint32_t i = 0; i < 32u; ++i)
            {
                const float values[4] = {float(i + 1u), float(i + 2u), float(i + 3u), float(i + 4u)};
                std::memcpy(fx.data + i * 16u, values, sizeof(values));
                writeVuInstructionPair(fx.code, i * 8u,
                    makeVuLq(0xFu, 16u + i % 8u, 0u, i),
                    makeVuUpper(0x28u, 0xFu, 0u, 1u + i / 8u, 8u + i % 8u));
            }
            for (uint32_t i = 32u; i < 36u; ++i)
                writeVuInstructionPair(fx.code, i * 8u, 0u, kVuUpperNop);
            for (uint32_t reg = 1u; reg <= 4u; ++reg)
                for (float &component : vu.state().vf[reg])
                    component = float(reg * 10u);

            for (uint32_t cycle = 1u; cycle <= 35u; ++cycle)
            {
                if (cycle == 1u)
                    vu.execute(fx.code, PS2_VU1_CODE_SIZE, fx.data, PS2_VU1_DATA_SIZE,
                               fx.gs, &fx.mem, 0u, 0u, 0u, 1u);
                else
                    vu.resume(fx.code, PS2_VU1_CODE_SIZE, fx.data, PS2_VU1_DATA_SIZE,
                              fx.gs, &fx.mem, 0u, 0u, 1u);
                for (uint32_t reg = 0u; reg < 8u; ++reg)
                {
                    int32_t last = -1;
                    for (uint32_t issued = reg; issued < 32u; issued += 8u)
                        if (issued + 4u <= cycle) last = issued;
                    const float upper = last < 0 ? 0.0f : float((last / 8 + 1) * 10);
                    const float lower = last < 0 ? 0.0f : float(last + 1);
                    t.Equals(vu.state().vf[8u + reg][0], upper, "upper write must retire at its exact cycle");
                    t.Equals(vu.state().vf[16u + reg][0], lower, "lower write must retire at its exact cycle");
                }
            }
        });

        tc.Run("ILW and IALU can retire together after repeated queue wraps", [](TestCase &t)
        {
            Vu1Fixture fx;
            t.IsTrue(fx.initialize(), "VU1 fixture should initialize");
            for (uint32_t group = 0; group < 8u; ++group)
            {
                const uint32_t base = group * 6u;
                for (uint32_t i = 0; i < 3u; ++i)
                {
                    const uint32_t value = group * 10u + i + 1u;
                    std::memcpy(fx.data + (base + i) * 16u, &value, sizeof(value));
                    writeVuInstructionPair(fx.code, (base + i) * 8u,
                        makeVuIlw(0x8u, i + 1u, 0u, base + i), kVuUpperNop);
                    writeVuInstructionPair(fx.code, (base + i + 3u) * 8u,
                        makeVuIaddiu(i + 4u, 0u, value + 100u), kVuUpperNop);
                }
            }
            VU1Interpreter vu;
            for (uint32_t cycle = 1u; cycle <= 48u; ++cycle)
            {
                if (cycle == 1u)
                    vu.execute(fx.code, PS2_VU1_CODE_SIZE, fx.data, PS2_VU1_DATA_SIZE,
                               fx.gs, &fx.mem, 0u, 0u, 0u, 1u);
                else
                    vu.resume(fx.code, PS2_VU1_CODE_SIZE, fx.data, PS2_VU1_DATA_SIZE,
                              fx.gs, &fx.mem, 0u, 0u, 1u);
                for (uint32_t i = 0; i < 3u; ++i)
                {
                    int32_t expected = 0;
                    for (uint32_t group = 0; group < 8u; ++group)
                        if (group * 6u + i + 4u <= cycle) expected = group * 10u + i + 1u;
                    t.Equals(vu.state().vi[i + 1u], expected, "delayed ILW must commit on the shared cycle");
                    t.Equals(vu.state().vi[i + 4u], expected ? expected + 100 : 0,
                             "one-cycle IALU must commit alongside ILW");
                }
            }
        });

        tc.Run("ILW result becomes visible after four cycles before IALU consumes it", [](TestCase &t)
        {
            Vu1Fixture fx;
            t.IsTrue(fx.initialize(), "VU1 fixture should initialize");

            const uint32_t source[4] = {0x1234u, 0u, 0u, 0u};
            std::memcpy(fx.data + 2u * 16u, source, sizeof(source));
            writeVuInstructionPair(
                fx.code, 0u, makeVuIlw(0x8u, 2u, 1u, 0), kVuUpperNop);
            writeVuInstructionPair(
                fx.code, 8u, makeVuIaddiu(3u, 2u, 1), kVuUpperNop);

            VU1Interpreter vu1;
            vu1.state().vi[1] = 2;
            vu1.execute(fx.code, PS2_VU1_CODE_SIZE,
                        fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem,
                        0u, 0u, 0u, 5u);

            t.Equals(vu1.state().vi[2], 0x1234,
                     "ILW should commit the selected word after four cycles");
            t.Equals(vu1.state().vi[3], 0x1235,
                     "IADDIU should wait for and consume the ILW result");
            t.Equals(vu1.state().cycles, static_cast<uint64_t>(5u),
                     "ILW-to-IALU dependency should account for all stalled cycles");
        });

        tc.Run("DIV and SQRT update the Q register from selected vector components", [](TestCase &t)
        {
            Vu1Fixture fx;
            t.IsTrue(fx.initialize(), "VU1 fixture should initialize");

            writeVuInstructionPair(fx.code, 0u, makeVuDiv(1u, 2u, 1u, 2u), kVuUpperNop);              // Q = vf1.y / vf2.z
            writeVuInstructionPair(fx.code, 8u, makeVuLowerSpecial(0x3Bu, 0u), kVuUpperNop);          // WAITQ
            writeVuInstructionPair(fx.code, 16u, makeVuSqrt(3u, 3u), kVuUpperNop);                    // Q = sqrt(abs(vf3.w))
            writeVuInstructionPair(fx.code, 24u, makeVuLowerSpecial(0x3Bu, 0u), kVuUpperNop);         // WAITQ

            VU1Interpreter vu1;
            vu1.state().vf[1][1] = 18.0f;
            vu1.state().vf[2][2] = 3.0f;
            vu1.state().vf[3][3] = 25.0f;

            vu1.execute(fx.code, PS2_VU1_CODE_SIZE, fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem, 0u, 0u, 0u, 8u);
            t.Equals(vu1.state().q, 6.0f, "WAITQ should expose DIV after its seven-cycle latency");
            t.Equals(vu1.state().cycles, static_cast<uint64_t>(8u),
                     "maxCycles should count FDIV stalls as elapsed VU cycles");

            vu1.resume(fx.code, PS2_VU1_CODE_SIZE, fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem, 0u, 0u, 8u);
            t.Equals(vu1.state().q, 5.0f, "WAITQ should expose SQRT after its seven-cycle latency");
        });

        tc.Run("FDIV resource serializes back-to-back scalar operations", [](TestCase &t)
        {
            Vu1Fixture fx;
            t.IsTrue(fx.initialize(), "VU1 fixture should initialize");

            writeVuInstructionPair(
                fx.code, 0u,
                makeVuDiv(1u, 2u, 0u, 0u),
                kVuUpperNop); // Q = vf1.x / vf2.x
            writeVuInstructionPair(
                fx.code, 8u,
                makeVuSqrt(3u, 0u),
                kVuUpperNop); // Must wait for the shared FDIV unit.
            writeVuInstructionPair(
                fx.code, 16u,
                makeVuLowerSpecial(0x3Bu, 0u),
                kVuUpperNop); // WAITQ

            VU1Interpreter vu1;
            vu1.state().vf[1][0] = 18.0f;
            vu1.state().vf[2][0] = 3.0f;
            vu1.state().vf[3][0] = 25.0f;
            vu1.execute(fx.code, PS2_VU1_CODE_SIZE,
                        fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem,
                        0u, 0u, 0u, 15u);

            t.Equals(vu1.state().q, 5.0f,
                     "the second FDIV operation should commit the final Q value");
            t.Equals(vu1.state().cycles, static_cast<uint64_t>(15u),
                     "back-to-back FDIV operations should include the resource stall");
        });

        tc.Run("FDIV commits current and sticky divide-invalid status", [](TestCase &t)
        {
            Vu1Fixture fx;
            t.IsTrue(fx.initialize(), "VU1 fixture should initialize");

            writeVuInstructionPair(
                fx.code, 0u, makeVuDiv(1u, 2u, 0u, 0u),
                kVuUpperNop);
            writeVuInstructionPair(
                fx.code, 8u, makeVuLowerSpecial(0x3Bu, 0u),
                kVuUpperNop);

            VU1Interpreter vu1;
            vu1.state().vf[1][0] = 0.0f;
            vu1.state().vf[2][0] = 0.0f;
            vu1.execute(fx.code, PS2_VU1_CODE_SIZE,
                        fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem,
                        0u, 0u, 0u, 8u);
            t.Equals(vu1.state().status, 0x410u,
                     "zero divided by zero should set current and sticky I");

            vu1.reset();
            vu1.state().vf[1][0] = 1.0f;
            vu1.state().vf[2][0] = 0.0f;
            vu1.execute(fx.code, PS2_VU1_CODE_SIZE,
                        fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem,
                        0u, 0u, 0u, 8u);
            t.Equals(vu1.state().status, 0x820u,
                     "a nonzero numerator divided by zero should set current and sticky D");
        });

        tc.Run("EFU WAITP and RNG execute with architectural latency and state", [](TestCase &t)
        {
            Vu1Fixture fx;
            t.IsTrue(fx.initialize(), "VU1 fixture should initialize");

            writeVuInstructionPair(
                fx.code, 0u,
                makeVuLowerSpecial(0x70u, 1u),
                kVuUpperNop); // ESADD P, vf1
            writeVuInstructionPair(
                fx.code, 8u,
                makeVuLowerSpecial(0x7Bu, 0u),
                kVuUpperNop); // WAITP
            writeVuInstructionPair(
                fx.code, 16u,
                makeVuLowerSpecial(0x42u, 2u, 0u, 0u, 0x8u),
                kVuUpperNop); // RINIT R, vf2.x
            writeVuInstructionPair(
                fx.code, 24u,
                makeVuLowerSpecial(0x40u, 0u, 3u, 0u, 0x8u),
                kVuUpperNop); // RNEXT.x vf3

            VU1Interpreter vu1;
            vu1.state().vf[1][0] = 1.0f;
            vu1.state().vf[1][1] = 2.0f;
            vu1.state().vf[1][2] = 3.0f;
            vu1.state().vf[2][0] = 1.5f;
            vu1.execute(fx.code, PS2_VU1_CODE_SIZE,
                        fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem,
                        0u, 0u, 0u, 14u);

            t.Equals(vu1.state().p, 14.0f,
                     "WAITP should expose ESADD after eleven cycles");
            t.Equals(vu1.state().vf[3][0], 0.0f,
                     "RNEXT vector result should respect FMAC writeback latency");
            vu1.resume(fx.code, PS2_VU1_CODE_SIZE,
                       fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem,
                       0u, 0u, 3u);
            t.IsTrue(vu1.state().vf[3][0] >= 1.0f &&
                         vu1.state().vf[3][0] < 2.0f &&
                         vu1.state().vf[3][0] != 1.5f,
                      "RNEXT should advance the 23-bit R LFSR and write a 1.x value");
        });

        tc.Run("EFU resource observes throughput separately from P visibility", [](TestCase &t)
        {
            Vu1Fixture fx;
            t.IsTrue(fx.initialize(), "VU1 fixture should initialize");

            writeVuInstructionPair(
                fx.code, 0u,
                makeVuLowerSpecial(0x70u, 1u),
                kVuUpperNop); // ESADD: result at cycle 11, resource free at 10.
            writeVuInstructionPair(
                fx.code, 8u,
                makeVuLowerSpecial(0x72u, 1u),
                kVuUpperNop); // ELENG: must issue at cycle 10.
            writeVuInstructionPair(
                fx.code, 16u,
                makeVuLowerSpecial(0x7Bu, 0u),
                kVuUpperNop); // WAITP waits for ELENG at cycle 28.

            VU1Interpreter vu1;
            vu1.state().vf[1][0] = 1.0f;
            vu1.state().vf[1][1] = 2.0f;
            vu1.state().vf[1][2] = 3.0f;
            vu1.execute(fx.code, PS2_VU1_CODE_SIZE,
                        fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem,
                        0u, 0u, 0u, 29u);

            t.IsTrue(vu1.state().p > 3.7f && vu1.state().p < 3.8f,
                     "WAITP should expose the second EFU result");
            t.Equals(vu1.state().cycles, static_cast<uint64_t>(29u),
                     "EFU scheduling should use opcode throughput and result latency");
        });

        tc.Run("all EFU opcodes produce P at their architectural latency", [](TestCase &t)
        {
            Vu1Fixture fx;
            t.IsTrue(fx.initialize(), "VU1 fixture should initialize");

            struct EfuCase
            {
                uint8_t opcode;
                uint32_t latency;
            };
            constexpr EfuCase cases[] = {
                {0x70u, 11u}, {0x71u, 18u}, {0x72u, 18u}, {0x73u, 24u},
                {0x74u, 54u}, {0x75u, 54u}, {0x76u, 12u}, {0x77u, 18u},
                {0x78u, 12u}, {0x79u, 29u}, {0x7Au, 12u}, {0x7Cu, 54u},
                {0x7Du, 44u}};

            for (const EfuCase &efu : cases)
            {
                std::memset(fx.code, 0, PS2_VU1_CODE_SIZE);
                writeVuInstructionPair(
                    fx.code, 0u,
                    makeVuLowerSpecial(efu.opcode, 1u, 0u, 0u, 0u),
                    kVuUpperNop);
                writeVuInstructionPair(
                    fx.code, 8u,
                    makeVuLowerSpecial(0x7Bu, 0u),
                    kVuUpperNop);

                VU1Interpreter vu1;
                vu1.state().vf[1][0] = 0.25f;
                vu1.state().vf[1][1] = 0.5f;
                vu1.state().vf[1][2] = 0.75f;
                vu1.state().vf[1][3] = 1.0f;
                vu1.execute(fx.code, PS2_VU1_CODE_SIZE,
                            fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem,
                            0u, 0u, 0u, efu.latency + 1u);

                t.Equals(vu1.state().cycles,
                         static_cast<uint64_t>(efu.latency + 1u),
                         "WAITP should count every EFU stall as an elapsed VU cycle");
                t.IsTrue(std::isfinite(vu1.state().p),
                         "architected EFU opcode should commit a finite P result");
            }
        });

        tc.Run("E and enabled D/T stop with their architectural delay rules", [](TestCase &t)
        {
            Vu1Fixture fx;
            t.IsTrue(fx.initialize(), "VU1 fixture should initialize");

            writeVuInstructionPair(
                fx.code, 0u, 0u,
                kVuUpperNop | 0x40000000u);
            writeVuInstructionPair(
                fx.code, 8u, makeVuIaddiu(1u, 0u, 7),
                kVuUpperNop);
            writeVuInstructionPair(
                fx.code, 16u, makeVuIaddiu(2u, 0u, 9),
                kVuUpperNop);

            VU1Interpreter vu1;
            vu1.execute(fx.code, PS2_VU1_CODE_SIZE,
                        fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem,
                        0u, 0u, 0u, 32u);
            t.Equals(vu1.state().vi[1], 7,
                     "E should execute exactly one sequential delay slot");
            t.Equals(vu1.state().vi[2], 0,
                     "E should stop before the instruction after its delay slot");

            vu1.reset();
            writeTrackedVuInstructionPair(
                fx, 0u, makeVuIaddiu(1u, 0u, 3),
                kVuUpperNop | 0x08000000u);
            writeTrackedVuInstructionPair(
                fx, 8u, makeVuIaddiu(2u, 0u, 5),
                kVuUpperNop);
            vu1.execute(fx.code, PS2_VU1_CODE_SIZE,
                        fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem,
                        0u, 0u, 0u, 2u);
            t.Equals(vu1.state().vi[2], 5,
                     "T must be ignored while TE is disabled");

            vu1.reset();
            vu1.state().tBitEnabled = true;
            vu1.execute(fx.code, PS2_VU1_CODE_SIZE,
                        fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem,
                        0u, 0u, 0u, 32u);
            t.Equals(vu1.state().vi[1], 3,
                     "T instruction itself should complete");
            t.Equals(vu1.state().vi[2], 0,
                     "enabled T should stop without an ordinary delay slot");
            t.IsTrue(vu1.state().stoppedByT,
                     "the stop reason should identify T");

            vu1.reset();
            vu1.state().dBitEnabled = true;
            writeTrackedVuInstructionPair(
                fx, 0u, makeVuIaddiu(1u, 0u, 4),
                kVuUpperNop | 0x10000000u);
            writeTrackedVuInstructionPair(
                fx, 8u, makeVuIaddiu(2u, 0u, 6),
                kVuUpperNop);
            vu1.execute(fx.code, PS2_VU1_CODE_SIZE,
                        fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem,
                        0u, 0u, 0u, 32u);
            t.Equals(vu1.state().vi[1], 4,
                     "D instruction itself should complete");
            t.Equals(vu1.state().vi[2], 0,
                     "enabled D should stop without an ordinary delay slot");
            t.IsTrue(vu1.state().stoppedByD,
                     "the stop reason should identify D");

            vu1.resume(fx.code, PS2_VU1_CODE_SIZE,
                       fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem,
                       0u, 0u, 1u);
            t.Equals(vu1.state().vi[2], 6,
                     "MSCNT-style resume should continue at the stopped TPC");
            t.IsTrue(!vu1.state().stoppedByD && !vu1.state().stoppedByT,
                     "resuming should clear the previous D/T stop reason");

            vu1.reset();
            vu1.state().tBitEnabled = true;
            writeTrackedVuInstructionPair(
                fx, 0u, makeVuBranch(1),
                kVuUpperNop | 0x08000000u);
            writeTrackedVuInstructionPair(
                fx, 8u, makeVuIaddiu(2u, 0u, 11),
                kVuUpperNop);
            writeTrackedVuInstructionPair(
                fx, 16u, makeVuIaddiu(3u, 0u, 13),
                kVuUpperNop);
            vu1.execute(fx.code, PS2_VU1_CODE_SIZE,
                        fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem,
                        0u, 0u, 0u, 32u);
            t.Equals(vu1.state().vi[2], 11,
                     "T on a branch should still execute its branch delay slot");
            t.Equals(vu1.state().vi[3], 0,
                     "T on a branch should stop before executing the branch target");
            t.Equals(vu1.state().pc, 16u,
                     "the stopped TPC should be the branch destination");
        });

        tc.Run("conditional branch sees the previous VI value for one instruction", [](TestCase &t)
        {
            Vu1Fixture fx;
            t.IsTrue(fx.initialize(), "VU1 fixture should initialize");

            writeVuInstructionPair(
                fx.code, 0u, makeVuIaddiu(1u, 0u, 1),
                kVuUpperNop);
            writeVuInstructionPair(
                fx.code, 8u, makeVuIbne(1u, 0u, 2),
                kVuUpperNop);
            writeVuInstructionPair(
                fx.code, 16u, makeVuIaddiu(2u, 0u, 2),
                kVuUpperNop);
            writeVuInstructionPair(
                fx.code, 24u, makeVuIaddiu(3u, 0u, 3),
                kVuUpperNop);
            writeVuInstructionPair(
                fx.code, 32u, makeVuIaddiu(4u, 0u, 4),
                kVuUpperNop);

            VU1Interpreter vu1;
            vu1.execute(fx.code, PS2_VU1_CODE_SIZE,
                        fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem,
                        0u, 0u, 0u, 4u);

            t.Equals(vu1.state().vi[2], 2,
                     "the instruction after a conditional branch remains its delay slot");
            t.Equals(vu1.state().vi[3], 3,
                     "an immediately following branch should see the pre-write VI value");

            vu1.reset();
            writeTrackedVuInstructionPair(fx, 8u, 0u, kVuUpperNop);
            writeTrackedVuInstructionPair(
                fx, 16u, makeVuIbne(1u, 0u, 2),
                kVuUpperNop);
            writeTrackedVuInstructionPair(
                fx, 40u, makeVuIaddiu(5u, 0u, 5),
                kVuUpperNop);
            vu1.execute(fx.code, PS2_VU1_CODE_SIZE,
                        fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem,
                        0u, 0u, 0u, 5u);
            t.Equals(vu1.state().vi[3], 3,
                     "taken branch should still execute its delay slot");
            t.Equals(vu1.state().vi[4], 0,
                     "after one intervening instruction the branch should observe and branch on the new VI value");
            t.Equals(vu1.state().vi[5], 5,
                     "taken branch should arrive at its target after the delay slot");

            vu1.reset();
            writeTrackedVuInstructionPair(
                fx, 0u, makeVuIaddiu(1u, 0u, 1),
                makeVuUpper(0x28u, 0x8u, 2u, 1u, 3u));
            writeTrackedVuInstructionPair(
                fx, 8u, makeVuIbne(1u, 0u, 2),
                makeVuUpper(0x28u, 0x8u, 0u, 3u, 4u));
            writeTrackedVuInstructionPair(
                fx, 16u, makeVuIaddiu(2u, 0u, 2),
                kVuUpperNop);
            writeTrackedVuInstructionPair(
                fx, 24u, makeVuIaddiu(3u, 0u, 3),
                kVuUpperNop);
            writeTrackedVuInstructionPair(
                fx, 32u, makeVuIaddiu(4u, 0u, 4),
                kVuUpperNop);
            vu1.execute(fx.code, PS2_VU1_CODE_SIZE,
                        fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem,
                        0u, 0u, 0u, 7u);
            t.Equals(vu1.state().vi[2], 2,
                     "a stalled conditional branch should retain its delay slot");
            t.Equals(vu1.state().vi[3], 3,
                     "the VI bypass must survive VF hazard stalls before the next pair issues");
            t.Equals(vu1.state().vi[4], 0,
                     "elapsed stall cycles must not expire the one-instruction VI bypass");
        });

        tc.Run("flag checks are visible to an immediately following branch", [](TestCase &t)
        {
            Vu1Fixture fx;
            t.IsTrue(fx.initialize(), "VU1 fixture should initialize");

            writeVuInstructionPair(
                fx.code, 0u,
                makeVuFlagImmediate(0x16u, 1u, 0x001u),
                kVuUpperNop);
            writeVuInstructionPair(
                fx.code, 8u, makeVuIbne(1u, 0u, 2),
                kVuUpperNop);
            writeVuInstructionPair(
                fx.code, 16u, makeVuIaddiu(2u, 0u, 2),
                kVuUpperNop);
            writeVuInstructionPair(
                fx.code, 24u, makeVuIaddiu(3u, 0u, 3),
                kVuUpperNop);
            writeVuInstructionPair(
                fx.code, 32u, makeVuIaddiu(4u, 0u, 4),
                kVuUpperNop);

            VU1Interpreter vu1;
            vu1.state().status = 0x001u;
            vu1.execute(fx.code, PS2_VU1_CODE_SIZE,
                        fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem,
                        0u, 0u, 0u, 4u);

            t.Equals(vu1.state().vi[1], 1,
                     "FSAND should publish its result after one cycle");
            t.Equals(vu1.state().vi[2], 2,
                     "the taken branch should still execute its delay slot");
            t.Equals(vu1.state().vi[3], 0,
                     "the immediately following branch must consume the flag-check result");
            t.Equals(vu1.state().vi[4], 4,
                     "the flag-driven branch should arrive at its target");
        });

        tc.Run("JR reads the latest VI value after an integer write", [](TestCase &t)
        {
            Vu1Fixture fx;
            t.IsTrue(fx.initialize(), "VU1 fixture should initialize");

            writeVuInstructionPair(
                fx.code, 0u, makeVuIaddiu(1u, 0u, 4),
                kVuUpperNop);
            writeVuInstructionPair(
                fx.code, 8u, makeVuJr(1u),
                kVuUpperNop);
            writeVuInstructionPair(
                fx.code, 16u, makeVuIaddiu(2u, 0u, 2),
                kVuUpperNop);
            writeVuInstructionPair(
                fx.code, 24u, makeVuIaddiu(3u, 0u, 3),
                kVuUpperNop);
            writeVuInstructionPair(
                fx.code, 32u, makeVuIaddiu(4u, 0u, 4),
                kVuUpperNop);

            VU1Interpreter vu1;
            vu1.state().vi[1] = 3;
            vu1.execute(fx.code, PS2_VU1_CODE_SIZE,
                        fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem,
                        0u, 0u, 0u, 4u);

            t.Equals(vu1.state().vi[1], 4,
                     "the pending IALU write should still commit normally");
            t.Equals(vu1.state().vi[2], 2,
                     "JR should execute exactly one delay-slot pair");
            t.Equals(vu1.state().vi[3], 0,
                     "JR should skip the sequential instruction after its delay slot");
            t.Equals(vu1.state().vi[4], 4,
                     "JR must use the updated target, unlike a conditional branch");
        });

        tc.Run("JALR uses the latest target before writing its link register", [](TestCase &t)
        {
            for (uint8_t link : {uint8_t{15}, uint8_t{1}})
            {
                Vu1Fixture fx;
                t.IsTrue(fx.initialize(), "VU1 fixture should initialize");
                writeVuInstructionPair(fx.code, 0u, makeVuIaddiu(1u, 0u, 4), kVuUpperNop);
                const uint32_t jalr = (0x25u << 25u) | (static_cast<uint32_t>(link) << 16u) | (1u << 11u);
                writeVuInstructionPair(fx.code, 8u, jalr, kVuUpperNop);
                writeVuInstructionPair(fx.code, 16u, makeVuIaddiu(2u, 0u, 2), kVuUpperNop);
                writeVuInstructionPair(fx.code, 24u, makeVuIaddiu(3u, 0u, 3), kVuUpperNop);
                writeVuInstructionPair(fx.code, 32u, makeVuIaddiu(4u, 0u, 4), kVuUpperNop);
                VU1Interpreter vu1;
                vu1.state().vi[1] = 3;
                vu1.execute(fx.code, PS2_VU1_CODE_SIZE, fx.data, PS2_VU1_DATA_SIZE,
                            fx.gs, &fx.mem, 0u, 0u, 0u, 4u);
                t.Equals(vu1.state().vi[link], 3, "JALR links to the instruction after its delay slot");
                t.Equals(vu1.state().vi[2], 2, "JALR must execute its delay slot");
                t.Equals(vu1.state().vi[3], 0, "JALR must not use the stale target or newly written link");
                t.Equals(vu1.state().vi[4], 4, "JALR must reach the updated target");
            }
        });

        tc.Run("MPG upload invalidates cached VU1 decode before MSCAL", [](TestCase &t)
        {
            Vu1Fixture fx;
            t.IsTrue(fx.initialize(), "VU1 fixture should initialize");

            VU1Interpreter vu1;
            fx.mem.setVu1MscalCallback([&](uint32_t startPC, uint32_t top, uint32_t itop)
            {
                vu1.execute(fx.code,
                            PS2_VU1_CODE_SIZE,
                            fx.data,
                            PS2_VU1_DATA_SIZE,
                            fx.gs,
                            &fx.mem,
                            startPC,
                            top,
                            itop,
                            1u);
            });

            uploadVu1Mpg(fx.mem, 0u, makeVuIaddiu(1u, 0u, 1), kVuUpperNop);
            const uint32_t firstMscal = makeVifCmd(0x14u, 0u, 0u);
            fx.mem.processVIF1Data(reinterpret_cast<const uint8_t *>(&firstMscal), sizeof(firstMscal));
            t.Equals(vu1.state().vi[1], 1, "first MSCAL should execute the first uploaded program");

            uploadVu1Mpg(fx.mem, 0u, makeVuIaddiu(1u, 0u, 2), kVuUpperNop);
            const uint32_t secondMscal = makeVifCmd(0x14u, 0u, 0u);
            fx.mem.processVIF1Data(reinterpret_cast<const uint8_t *>(&secondMscal), sizeof(secondMscal));
            t.Equals(vu1.state().vi[1], 2, "second MSCAL should see the MPG-updated instruction");
        });

        tc.Run("late code edits invalidate every overlapping native region", [](TestCase &t)
        {
            for (unsigned length : {4u, 8u, 12u, 16u})
                for (unsigned offset = 0u; offset < length; ++offset)
                    for (bool atCodeEnd : {false, true})
                    {
                        Vu1Fixture nativeMemory, oracleMemory;
                        t.IsTrue(nativeMemory.initialize() && oracleMemory.initialize(), "VU1 fixtures should initialize");
                        const uint32_t start = atCodeEnd ? PS2_VU1_CODE_SIZE - length * 8u : 0u;
                        for (auto *fx : {&nativeMemory, &oracleMemory})
                        {
                            for (unsigned pc = 0u; pc < PS2_VU1_CODE_SIZE; pc += 8u)
                                writeVuInstructionPair(fx->code, pc, 0x8000033cu, kVuUpperNop);
                            for (unsigned index = 0u; index < length; ++index)
                                fx->mem.write64(PS2_VU1_CODE_BASE + start + index * 8u,
                                    packVuInstructionPair(makeVuIaddiu(1u, 0u, index + 1u), kVuUpperNop));
                        }
                        VU1Interpreter native, oracle;
                        oracle.setCompiledExecutionEnabled(false);
                        const auto accepted = [&] { return ps2_vu_detail::regionCounters.acceptedByLength[length][1]; };
                        const uint64_t warm = accepted();
                        native.execute(nativeMemory.code, PS2_VU1_CODE_SIZE, nativeMemory.data, PS2_VU1_DATA_SIZE,
                            nativeMemory.gs, &nativeMemory.mem, start, 0u, 0u, length);
                        oracle.execute(oracleMemory.code, PS2_VU1_CODE_SIZE, oracleMemory.data, PS2_VU1_DATA_SIZE,
                            oracleMemory.gs, &oracleMemory.mem, start, 0u, 0u, length);
                        if (std::getenv("PS2_VU_REQUIRE_REGIONS"))
                            t.Equals(accepted() - warm, uint64_t{length}, "the complete original region must be cached and executed");
                        for (bool restore : {false, true})
                        {
                            const uint32_t lower = restore ? makeVuIaddiu(1u, 0u, offset + 1u) : makeVuIaddiu(2u, 0u, 777u);
                            for (auto *fx : {&nativeMemory, &oracleMemory})
                                fx->mem.write64(PS2_VU1_CODE_BASE + start + offset * 8u,
                                    packVuInstructionPair(lower, kVuUpperNop));
                            native.state().pc = oracle.state().pc = start;
                            const uint64_t before = accepted();
                            native.resume(nativeMemory.code, PS2_VU1_CODE_SIZE, nativeMemory.data, PS2_VU1_DATA_SIZE,
                                nativeMemory.gs, &nativeMemory.mem, 0u, 0u, length);
                            oracle.resume(oracleMemory.code, PS2_VU1_CODE_SIZE, oracleMemory.data, PS2_VU1_DATA_SIZE,
                                oracleMemory.gs, &oracleMemory.mem, 0u, 0u, length);
                            t.IsTrue(std::memcmp(&native.state(), &oracle.state(), sizeof(VU1State)) == 0,
                                "late edits and restored long regions must match raw interpretation across resume");
                            t.Equals(native.state().vi[2], 777, "the replacement instruction must execute");
                            if (std::getenv("PS2_VU_REQUIRE_REGIONS"))
                                t.Equals(accepted() - before, restore ? uint64_t{length} : uint64_t{0},
                                    "a late edit must replace a cached long match; restoration must select it again");
                        }
                        native.setCompiledExecutionEnabled(false);
                        for (unsigned tail = 0u; tail < 8u; ++tail)
                        {
                            native.resume(nativeMemory.code, PS2_VU1_CODE_SIZE, nativeMemory.data, PS2_VU1_DATA_SIZE,
                                nativeMemory.gs, &nativeMemory.mem, 0u, 0u, 1u);
                            oracle.resume(oracleMemory.code, PS2_VU1_CODE_SIZE, oracleMemory.data, PS2_VU1_DATA_SIZE,
                                oracleMemory.gs, &oracleMemory.mem, 0u, 0u, 1u);
                            t.IsTrue(std::memcmp(&native.state(), &oracle.state(), sizeof(VU1State)) == 0,
                                "replacement state must survive one-cycle raw continuation");
                        }
                    }
        });
        tc.Run("direct VU1 code writes invalidate cached decode", [](TestCase &t)
        {
            Vu1Fixture fx;
            t.IsTrue(fx.initialize(), "VU1 fixture should initialize");

            VU1Interpreter vu1;
            fx.mem.write64(PS2_VU1_CODE_BASE, packVuInstructionPair(makeVuIaddiu(1u, 0u, 1), kVuUpperNop));
            vu1.execute(fx.code, PS2_VU1_CODE_SIZE, fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem, 0u, 0u, 0u, 1u);
            t.Equals(vu1.state().vi[1], 1, "first execution should use the original direct write");

            fx.mem.write64(PS2_VU1_CODE_BASE, packVuInstructionPair(makeVuIaddiu(1u, 0u, 2), kVuUpperNop));
            vu1.execute(fx.code, PS2_VU1_CODE_SIZE, fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem, 0u, 0u, 0u, 1u);
            t.Equals(vu1.state().vi[1], 2, "second execution should rebuild decode after the direct write");
        });

        tc.Run("XGKICK sends a VU memory GIF packet through PATH1", [](TestCase &t)
        {
            PS2Memory mem;
            t.IsTrue(mem.initialize(), "PS2Memory initialize should succeed");

            std::vector<std::vector<uint8_t>> captured;
            mem.setGifPacketCallback([&](const uint8_t *data, uint32_t sizeBytes)
            {
                captured.emplace_back(data, data + sizeBytes);
            });

            std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE, 0u);
            GS gs;
            gs.init(vram.data(), static_cast<uint32_t>(vram.size()), nullptr);

            uint8_t *vuCode = mem.getVU1Code();
            uint8_t *vuData = mem.getVU1Data();
            std::memset(vuCode, 0, PS2_VU1_CODE_SIZE);
            std::memset(vuData, 0, PS2_VU1_DATA_SIZE);

            constexpr uint32_t kLastQw = (PS2_VU1_DATA_SIZE / 16u) - 1u;
            const uint32_t tagOffset = kLastQw * 16u;

            const uint64_t imageTag = makeGifTag(1u, GIF_FMT_IMAGE, 0u, true);
            std::memcpy(vuData + tagOffset, &imageTag, sizeof(imageTag));

            for (uint32_t i = 0; i < 16u; ++i)
            {
                vuData[i] = static_cast<uint8_t>(0xC0u + i);
            }

            const uint32_t lower = makeVuLowerSpecial(0x6Cu, 1u);
            std::memcpy(vuCode + 0u, &lower, sizeof(lower));
            const uint32_t upper = 0u;
            std::memcpy(vuCode + 4u, &upper, sizeof(upper));

            VU1Interpreter vu1;
            vu1.state().vi[1] = static_cast<int32_t>(kLastQw);
            vu1.execute(vuCode,
                        PS2_VU1_CODE_SIZE,
                        vuData,
                        PS2_VU1_DATA_SIZE,
                        gs,
                        &mem,
                        0u,
                        0u,
                        0u,
                        3u);

            t.Equals(captured.size(), static_cast<size_t>(1u), "XGKICK should emit one wrapped GIF packet");
            if (!captured.empty())
            {
                t.Equals(captured[0].size(), static_cast<size_t>(32u), "wrapped packet should include tag plus one qword payload");
                bool payloadOk = true;
                for (uint32_t i = 0; i < 16u; ++i)
                {
                    if (captured[0].size() < 32u || captured[0][16u + i] != static_cast<uint8_t>(0xC0u + i))
                    {
                        payloadOk = false;
                        break;
                    }
                }
                t.IsTrue(payloadOk, "wrapped payload should be copied from start of VU1 memory");
            }
        });

        tc.Run("XGKICK wraps within a qword for an external unaligned data size", [](TestCase &t)
        {
            Vu1Fixture fx;
            t.IsTrue(fx.initialize(), "VU1 fixture should initialize");
            std::vector<uint8_t> data(31u, 0u);
            uint8_t expected[16]{};
            const uint64_t tag = makeGifTag(0u, GIF_FMT_PACKED, 1u, true);
            std::memcpy(expected, &tag, sizeof(tag));
            expected[15] = 0x5au;
            for (uint32_t i = 0; i < 16u; ++i)
                data[(16u + i) % data.size()] = expected[i];
            std::vector<uint8_t> captured;
            fx.mem.setGifPacketCallback([&](const uint8_t *packet, uint32_t size) {
                captured.assign(packet, packet + size);
            });
            writeVuInstructionPair(fx.code, 0u, makeVuLowerSpecial(0x6Cu, 1u), kVuUpperNop);
            VU1Interpreter vu;
            vu.state().vi[1] = 1;
            vu.execute(fx.code, PS2_VU1_CODE_SIZE, data.data(), data.size(),
                       fx.gs, &fx.mem, 0u, 0u, 0u, 1u);
            t.IsTrue(captured.size() == sizeof(expected) &&
                         std::memcmp(captured.data(), expected, sizeof(expected)) == 0,
                     "the final byte of the qword must wrap to the data buffer start");
        });

        tc.Run("XGKICK observes stores committed before a future PATH1 qword", [](TestCase &t)
        {
            PS2Memory mem;
            t.IsTrue(mem.initialize(), "PS2Memory initialize should succeed");

            std::vector<std::vector<uint8_t>> captured;
            mem.setGifPacketCallback([&](const uint8_t *packet, uint32_t sizeBytes)
            {
                captured.emplace_back(packet, packet + sizeBytes);
            });

            std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE, 0u);
            GS gs;
            gs.init(vram.data(), static_cast<uint32_t>(vram.size()), nullptr);

            uint8_t *code = mem.getVU1Code();
            uint8_t *data = mem.getVU1Data();
            std::memset(code, 0, PS2_VU1_CODE_SIZE);
            std::memset(data, 0xFF, PS2_VU1_DATA_SIZE);

            const uint64_t imageTag = makeGifTag(1u, GIF_FMT_IMAGE, 0u, true);
            std::memset(data, 0, 16u);
            std::memcpy(data, &imageTag, sizeof(imageTag));
            writeVuInstructionPair(
                code, 0u,
                makeVuLowerSpecial(0x6Cu, 1u),
                kVuUpperNop);
            writeVuInstructionPair(
                code, 8u,
                makeVuSq(0xFu, 4u, 2u, 0),
                kVuUpperNop);
            writeVuInstructionPair(code, 16u, 0u, kVuUpperNop);

            VU1Interpreter vu1;
            vu1.state().vi[1] = 0;
            vu1.state().vi[2] = 1;
            const float replacement[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            std::memcpy(vu1.state().vf[4], replacement, sizeof(replacement));
            vu1.execute(code, PS2_VU1_CODE_SIZE,
                        data, PS2_VU1_DATA_SIZE, gs, &mem,
                        0u, 0u, 0u, 3u);

            t.Equals(captured.size(), static_cast<size_t>(1u),
                     "PATH1 should finish after consuming the updated payload");
            if (!captured.empty())
            {
                t.IsTrue(captured[0].size() >= 32u &&
                             std::memcmp(captured[0].data() + 16u,
                                         replacement,
                                         sizeof(replacement)) == 0,
                         "XGKICK must read each qword in its transfer cycle");
            }
        });

        tc.Run("a second XGKICK stalls until the active PATH1 transfer completes", [](TestCase &t)
        {
            PS2Memory mem;
            t.IsTrue(mem.initialize(), "PS2Memory initialize should succeed");

            std::vector<std::vector<uint8_t>> captured;
            mem.setGifPacketCallback([&](const uint8_t *packet, uint32_t sizeBytes)
            {
                captured.emplace_back(packet, packet + sizeBytes);
            });

            std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE, 0u);
            GS gs;
            gs.init(vram.data(), static_cast<uint32_t>(vram.size()), nullptr);
            uint8_t *code = mem.getVU1Code();
            uint8_t *data = mem.getVU1Data();
            std::memset(code, 0, PS2_VU1_CODE_SIZE);
            std::memset(data, 0, PS2_VU1_DATA_SIZE);

            const uint64_t imageTag = makeGifTag(1u, GIF_FMT_IMAGE, 0u, true);
            std::memcpy(data + 0u, &imageTag, sizeof(imageTag));
            std::memcpy(data + 32u, &imageTag, sizeof(imageTag));
            std::memset(data + 16u, 0x11, 16u);
            std::memset(data + 48u, 0x22, 16u);
            writeVuInstructionPair(
                code, 0u,
                makeVuLowerSpecial(0x6Cu, 1u),
                kVuUpperNop);
            writeVuInstructionPair(
                code, 8u,
                makeVuLowerSpecial(0x6Cu, 2u),
                kVuUpperNop);
            writeVuInstructionPair(code, 16u, 0u, kVuUpperNop);
            writeVuInstructionPair(code, 24u, 0u, kVuUpperNop);

            VU1Interpreter vu1;
            vu1.state().vi[1] = 0;
            vu1.state().vi[2] = 2;
            vu1.execute(code, PS2_VU1_CODE_SIZE,
                        data, PS2_VU1_DATA_SIZE, gs, &mem,
                        0u, 0u, 0u, 6u);

            t.Equals(captured.size(), static_cast<size_t>(2u),
                     "both PATH1 transfers should complete in issue order");
            if (captured.size() == 2u)
            {
                t.IsTrue(captured[0].size() >= 32u && captured[0][16u] == 0x11u,
                         "the first XGKICK payload should be delivered first");
                t.IsTrue(captured[1].size() >= 32u && captured[1][16u] == 0x22u,
                         "the stalled XGKICK should retain its own source address");
            }
            t.Equals(vu1.state().cycles, static_cast<uint64_t>(6u),
                     "XGKICK resource stalls should consume VU cycles");
        });

        tc.Run("synthetic Code Veronica text packet preserves black-frame PATH1 data", [](TestCase &t)
        {
            PS2Memory mem;
            t.IsTrue(mem.initialize(), "PS2Memory initialize should succeed");

            std::vector<std::vector<uint8_t>> captured;
            mem.setGifPacketCallback([&](const uint8_t *data, uint32_t sizeBytes)
            {
                captured.emplace_back(data, data + sizeBytes);
            });

            std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE, 0u);
            GS gs;
            gs.init(vram.data(), static_cast<uint32_t>(vram.size()), nullptr);

            uint8_t *code = mem.getVU1Code();
            uint8_t *data = mem.getVU1Data();
            std::memset(code, 0, PS2_VU1_CODE_SIZE);
            std::memset(data, 0, PS2_VU1_DATA_SIZE);

            const uint64_t imageTag = makeGifTag(1u, GIF_FMT_IMAGE, 0u, true);
            std::memcpy(data, &imageTag, sizeof(imageTag));
            writeVuInstructionPair(
                code, 0u, 0u,
                makeVuUpper(0x28u, 0xFu, 3u, 2u, 4u));
            writeVuInstructionPair(
                code, 8u,
                makeVuSq(0xFu, 4u, 2u, 0),
                kVuUpperNop);
            writeVuInstructionPair(
                code, 16u,
                makeVuLowerSpecial(0x6Cu, 1u),
                kVuUpperNop);

            VU1Interpreter vu1;
            vu1.state().vi[1] = 0;
            vu1.state().vi[2] = 1;
            const float a[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            const float b[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            std::memcpy(vu1.state().vf[2], a, sizeof(a));
            std::memcpy(vu1.state().vf[3], b, sizeof(b));

            vu1.execute(code, PS2_VU1_CODE_SIZE,
                        data, PS2_VU1_DATA_SIZE, gs, &mem,
                        0u, 0u, 0u, 10u);

            t.Equals(captured.size(), static_cast<size_t>(1u),
                     "the synthetic scene should emit exactly one PATH1 packet");
            if (!captured.empty())
            {
                const float expected[4] = {0.0f, 0.0f, 0.0f, 1.0f};
                t.Equals(captured[0].size(), static_cast<size_t>(32u),
                         "text regression packet should contain one image qword");
                t.IsTrue(captured[0].size() >= 32u &&
                             std::memcmp(captured[0].data() + 16u,
                                         expected,
                                         sizeof(expected)) == 0,
                         "PATH1 must observe the post-FMAC store, not stale blue/magenta data");
            }
        });

        tc.Run("MSCAL can start a VU1 XGKICK program and update GS VRAM", [](TestCase &t)
        {
            PS2Memory mem;
            t.IsTrue(mem.initialize(), "PS2Memory initialize should succeed");

            GS gs;
            gs.init(mem.getGSVRAM(), static_cast<uint32_t>(PS2_GS_VRAM_SIZE), &mem.gs());
            GifArbiter arbiter([&](const uint8_t *data, uint32_t sizeBytes)
            {
                gs.processGIFPacket(data, sizeBytes);
            });
            mem.setGifArbiter(&arbiter);

            const uint64_t bitblt =
                (static_cast<uint64_t>(0u) << 0) |
                (static_cast<uint64_t>(1u) << 16) |
                (static_cast<uint64_t>(0u) << 24) |
                (static_cast<uint64_t>(0u) << 32) |
                (static_cast<uint64_t>(1u) << 48) |
                (static_cast<uint64_t>(0u) << 56);
            gs.writeRegister(GS_REG_BITBLTBUF, bitblt);
            gs.writeRegister(GS_REG_TRXPOS, 0ull);
            gs.writeRegister(GS_REG_TRXREG, (4ull << 0) | (1ull << 32));
            gs.writeRegister(GS_REG_TRXDIR, 0ull);

            uint8_t *vuCode = mem.getVU1Code();
            uint8_t *vuData = mem.getVU1Data();
            std::memset(vuCode, 0, PS2_VU1_CODE_SIZE);
            std::memset(vuData, 0, PS2_VU1_DATA_SIZE);

            const uint32_t lower = makeVuLowerSpecial(0x6Cu, 0u);
            std::memcpy(vuCode + 0u, &lower, sizeof(lower));
            const uint32_t upper = 0u;
            std::memcpy(vuCode + 4u, &upper, sizeof(upper));

            const uint64_t gifTag = makeGifTag(1u, GIF_FMT_IMAGE, 0u, true);
            std::memcpy(vuData + 0u, &gifTag, sizeof(gifTag));
            const uint64_t tagHi = 0u;
            std::memcpy(vuData + 8u, &tagHi, sizeof(tagHi));
            for (uint32_t i = 0; i < 16u; ++i)
            {
                vuData[16u + i] = static_cast<uint8_t>(0x90u + i);
            }

            VU1Interpreter vu1;
            mem.setVu1MscalCallback([&](uint32_t startPC, uint32_t top, uint32_t itop)
            {
                vu1.execute(vuCode,
                            PS2_VU1_CODE_SIZE,
                            vuData,
                            PS2_VU1_DATA_SIZE,
                            gs,
                            &mem,
                            startPC,
                            top,
                            itop,
                            3u);
            });

            const uint32_t mscalCmd = makeVifCmd(0x14u, 0u, 0u);
            mem.processVIF1Data(reinterpret_cast<const uint8_t *>(&mscalCmd), sizeof(mscalCmd));

            const uint8_t *vramOut = mem.getGSVRAM();
            bool imageOk = true;
            for (uint32_t x = 0; x < 4u && imageOk; ++x)
            {
                const uint32_t off = GSPSMCT32::addrPSMCT32(0u, 1u, x, 0u);
                for (uint32_t c = 0; c < 4u; ++c)
                {
                    if (vramOut[off + c] != static_cast<uint8_t>(0x90u + x * 4u + c))
                    {
                        imageOk = false;
                        break;
                    }
                }
            }
            t.IsTrue(imageOk, "MSCAL-triggered XGKICK should route PATH1 packet into GS VRAM");
        });

        tc.Run("standalone VU1 code honors the nullable PS2Memory API", [](TestCase &t)
        {
            std::vector<uint8_t> code(8u, 0u);
            std::vector<uint8_t> data(16u, 0u);
            std::vector<uint8_t> vram(PS2_GS_VRAM_SIZE, 0u);
            GS gs;
            gs.init(vram.data(), static_cast<uint32_t>(vram.size()), nullptr);

            VU1Interpreter vu1;
            vu1.execute(code.data(), static_cast<uint32_t>(code.size()),
                        data.data(), static_cast<uint32_t>(data.size()),
                        gs, nullptr, 0u, 0u, 0u, 1u);

            t.Equals(vu1.state().pc, 0u,
                     "external code should execute and wrap without dereferencing a null memory tracker");
        });

        tc.Run("VU1 status-immediate ops decode IMM12 and their target VI", [](TestCase &t)
        {
            Vu1Fixture fx;
            t.IsTrue(fx.initialize(), "VU1 fixture should initialize");

            writeVuInstructionPair(fx.code, 0u,
                                   makeVuFlagImmediate(0x14u, 5u, 0x812u),
                                   kVuUpperNop);
            writeVuInstructionPair(fx.code, 8u,
                                   makeVuFlagImmediate(0x16u, 6u, 0x810u),
                                   kVuUpperNop);
            writeVuInstructionPair(fx.code, 16u,
                                   makeVuFlagImmediate(0x17u, 7u, 0x040u),
                                   kVuUpperNop);

            VU1Interpreter vu1;
            vu1.state().status = 0x812u;
            vu1.execute(fx.code, PS2_VU1_CODE_SIZE,
                        fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem,
                        0u, 0u, 0u, 3u);

            t.Equals(vu1.state().vi[5], 1,
                     "FSEQ should compare all 12 immediate bits and write IT");
            t.Equals(vu1.state().vi[6], 0x810,
                     "FSAND should return the masked 12-bit status in IT");
            t.Equals(vu1.state().vi[7], 0x852,
                     "FSOR should return the 12-bit OR value rather than a boolean");
            t.Equals(vu1.state().vi[1], 0,
                     "status-immediate ops should not hardcode VI1");
        });

        tc.Run("VU1 FSSET enters the four-cycle flag pipeline", [](TestCase &t)
        {
            Vu1Fixture fx;
            t.IsTrue(fx.initialize(), "VU1 fixture should initialize");

            writeVuInstructionPair(fx.code, 0u,
                                   makeVuFlagImmediate(0x15u, 0u, 0xA80u),
                                   kVuUpperNop);

            VU1Interpreter vu1;
            vu1.state().status = 0x015u;
            vu1.execute(fx.code, PS2_VU1_CODE_SIZE,
                        fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem,
                        0u, 0u, 0u, 3u);
            t.Equals(vu1.state().status, 0x015u,
                     "FSSET should not be visible before four cycles elapse");

            vu1.resume(fx.code, PS2_VU1_CODE_SIZE,
                       fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem,
                       0u, 0u, 1u);
            t.Equals(vu1.state().status, 0xA95u,
                     "FSSET should replace sticky bits while preserving current and D/I bits");
        });

        tc.Run("VU1 has one flag pipeline and FSSET wins a same-pair conflict", [](TestCase &t)
        {
            Vu1Fixture fx;
            t.IsTrue(fx.initialize(), "VU1 fixture should initialize");

            writeVuInstructionPair(fx.code, 0u,
                                   makeVuFlagImmediate(0x15u, 0u, 0xA80u),
                                   makeVuUpper(0x28u, 0xAu, 2u, 1u, 3u));
            for (uint32_t pc = 8u; pc <= 32u; pc += 8u)
                writeVuInstructionPair(fx.code, pc, 0u, kVuUpperNop);

            VU1Interpreter vu1;
            vu1.state().vf[1][0] = 1.0f;
            vu1.state().vf[1][2] = -3.0f;
            vu1.state().vf[2][0] = -1.0f;
            vu1.state().vf[2][2] = 1.0f;

            vu1.execute(fx.code, PS2_VU1_CODE_SIZE,
                        fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem,
                        0u, 0u, 0u, 5u);

            t.Equals(vu1.state().mac, 0x28u,
                     "same-pair FSSET should suppress only STATUS, not the upper MAC result");
            t.Equals(vu1.state().status, 0xA80u,
                     "the single runtime pipeline should commit FSSET sticky bits");
        });

        tc.Run("VU1 FMAC flags respect destination lanes and become visible after four cycles", [](TestCase &t)
        {
            Vu1Fixture fx;
            t.IsTrue(fx.initialize(), "VU1 fixture should initialize");

            writeVuInstructionPair(fx.code, 0u, 0u,
                                   makeVuUpper(0x28u, 0xAu, 2u, 1u, 3u));
            writeVuInstructionPair(fx.code, 8u,
                                   makeVuFlagRegister(0x18u, 6u, 7u),
                                   kVuUpperNop);
            writeVuInstructionPair(fx.code, 16u,
                                   0u,
                                   kVuUpperNop);
            writeVuInstructionPair(fx.code, 24u,
                                   0u,
                                   kVuUpperNop);
            writeVuInstructionPair(fx.code, 32u,
                                   makeVuFlagRegister(0x1Au, 4u, 5u),
                                   kVuUpperNop);

            VU1Interpreter vu1;
            vu1.state().vf[1][0] = 1.0f;
            vu1.state().vf[1][2] = -3.0f;
            vu1.state().vf[2][0] = -1.0f;
            vu1.state().vf[2][2] = 1.0f;
            vu1.state().vi[5] = 0xFFFF;
            vu1.state().vi[7] = 0;

            vu1.execute(fx.code, PS2_VU1_CODE_SIZE,
                        fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem,
                        0u, 0u, 0u, 3u);

            t.Equals(vu1.state().mac, 0u,
                     "FMAC flags should remain hidden before four cycles elapse");
            t.Equals(vu1.state().vi[6], 1,
                     "FMEQ before the commit cycle should observe the old MAC flags");

            vu1.resume(fx.code, PS2_VU1_CODE_SIZE,
                       fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem,
                       0u, 0u, 2u);

            t.Equals(vu1.state().mac, 0x28u,
                     "ADD.xz should report zero on x and sign on z only");
            t.Equals(vu1.state().status, 0xC3u,
                     "FMAC commit should update current Z/S and accumulate their sticky bits");
            t.Equals(vu1.state().vi[4], 0x28,
                      "FMAND on the commit cycle should observe the new MAC flags");
        });

        tc.Run("VU1 CLIP and FCGET share the four-cycle flag timeline", [](TestCase &t)
        {
            Vu1Fixture fx;
            t.IsTrue(fx.initialize(), "VU1 fixture should initialize");

            writeVuInstructionPair(
                fx.code, 0u, 0u,
                makeVuUpperSpecial(0x1Fu, 0u, 2u, 1u));
            writeVuInstructionPair(
                fx.code, 8u,
                makeVuFlagRegister(0x1Cu, 3u, 0u),
                kVuUpperNop);
            writeVuInstructionPair(fx.code, 16u, 0u, kVuUpperNop);
            writeVuInstructionPair(fx.code, 24u, 0u, kVuUpperNop);
            writeVuInstructionPair(
                fx.code, 32u,
                makeVuFlagRegister(0x1Cu, 4u, 0u),
                kVuUpperNop);

            VU1Interpreter vu1;
            vu1.state().vf[1][0] = 2.0f;
            vu1.state().vf[2][3] = 1.0f;
            vu1.execute(fx.code, PS2_VU1_CODE_SIZE,
                        fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem,
                        0u, 0u, 0u, 5u);

            t.Equals(vu1.state().vi[3], 0,
                     "FCGET before cycle four should observe the previous CLIP value");
            t.Equals(vu1.state().vi[4], 1,
                     "FCGET on cycle four should observe the committed +X CLIP bit");
            t.Equals(vu1.state().clip, 1u,
                     "CLIP should shift and commit the six new comparison bits");
        });

        tc.Run("VU1 FMAC normalizes overflow and underflow before writing results", [](TestCase &t)
        {
            Vu1Fixture fx;
            t.IsTrue(fx.initialize(), "VU1 fixture should initialize");

            writeVuInstructionPair(fx.code, 0u, 0u,
                                   makeVuUpper(0x2Au, 0xFu, 2u, 1u, 3u));

            VU1Interpreter vu1;
            vu1.state().vf[1][0] = std::numeric_limits<float>::max();
            vu1.state().vf[1][1] = std::numeric_limits<float>::min();
            vu1.state().vf[1][2] = -2.0f;
            vu1.state().vf[1][3] = 0.0f;
            vu1.state().vf[2][0] = 2.0f;
            vu1.state().vf[2][1] = 0.5f;
            vu1.state().vf[2][2] = 1.0f;
            vu1.state().vf[2][3] = 1.0f;

            vu1.execute(fx.code, PS2_VU1_CODE_SIZE,
                        fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem,
                        0u, 0u, 0u, 5u);

            t.Equals(vu1.state().mac, 0x8425u,
                     "MUL should report x overflow, y underflow+zero, z sign and w zero");
            t.Equals(vu1.state().status, 0x3CFu,
                     "current and sticky status should summarize Z/S/U/O");
            t.Equals(vu1.state().vf[3][0], std::numeric_limits<float>::max(),
                     "overflow should clamp to the largest finite VU value");
            t.Equals(vu1.state().vf[3][1], 0.0f,
                     "underflow should flush to signed zero before writeback");
        });

        tc.Run("FMAC normal results and finite overflow keep distinct flags", [](TestCase &t)
        {
            Vu1Fixture fx;
            t.IsTrue(fx.initialize(), "VU1 fixture should initialize");
            struct Case { uint8_t op; float left, right, result; uint32_t flags; };
            const float maximum = std::numeric_limits<float>::max();
            const float minimum = std::numeric_limits<float>::min();
            const Case cases[] = {
                {0x28u, 1.25f, 2.5f, 3.75f, 0u},
                {0x2cu, 1.25f, 2.5f, -1.25f, 2u},
                {0x2au, -1.25f, 2.5f, -3.125f, 2u},
                {0x2au, minimum, 1.0f, minimum, 0u},
                {0x2au, maximum, 1.0f, maximum, 0u},
                {0x28u, maximum, maximum, maximum, 8u},
                {0x2cu, -maximum, maximum, -maximum, 10u},
                {0x2au, -minimum, 0.5f, -0.0f, 7u},
            };
            for (const auto &entry : cases)
            {
                writeVuInstructionPair(fx.code, 0u, 0u,
                                       makeVuUpper(entry.op, 0x8u, 2u, 1u, 3u));
                VU1Interpreter vu1;
                vu1.state().vf[1][0] = entry.left;
                vu1.state().vf[2][0] = entry.right;
                vu1.execute(fx.code, PS2_VU1_CODE_SIZE,
                            fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem,
                            0u, 0u, 0u, 5u);
                t.Equals(vu1.state().vf[3][0], entry.result,
                         "arithmetic must retain toward-zero saturation and signed underflow");
                t.Equals(std::signbit(vu1.state().vf[3][0]), std::signbit(entry.result),
                         "the sign of an underflowed result must survive");
                t.Equals(vu1.state().status, entry.flags | (entry.flags << 6u),
                         "normal finite values and overflow saturated to finite values differ");
            }
        });

        tc.Run("FMAC product contributes Z/S/U/O sticky flags", [](TestCase &t)
        {
            Vu1Fixture fx;
            t.IsTrue(fx.initialize(), "VU1 fixture should initialize");

            writeVuInstructionPair(
                fx.code, 0u, 0u,
                makeVuUpper(0x29u, 0x8u, 2u, 1u, 3u)); // MADD.x

            VU1Interpreter vu1;
            vu1.state().acc[0] = 1.0f;
            vu1.state().vf[1][0] = std::numeric_limits<float>::min();
            vu1.state().vf[2][0] = 0.5f;
            vu1.execute(fx.code, PS2_VU1_CODE_SIZE,
                        fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem,
                        0u, 0u, 0u, 5u);

            t.Equals(vu1.state().vf[3][0], 1.0f,
                     "the accumulated FMAC result should remain normal");
            t.Equals(vu1.state().mac, 0u,
                     "MAC flags should describe the final accumulated value");
            t.Equals(vu1.state().status, 0x140u,
                     "the underflowing product should set sticky Z and U");
        });

        tc.Run("FMAC product sticky flags retain sign and range boundaries", [](TestCase &t)
        {
            Vu1Fixture fx;
            t.IsTrue(fx.initialize(), "VU1 fixture should initialize");
            writeVuInstructionPair(fx.code, 0u, 0u, makeVuUpper(0x29u, 0x8u, 2u, 1u, 3u));
            struct Input { float left, right, acc; uint32_t status; };
            const float minimum = std::numeric_limits<float>::min();
            const float maximum = std::numeric_limits<float>::max();
            const Input inputs[] = {
                {2.0f, 3.0f, 1.0f, 0u},
                {-2.0f, 3.0f, 10.0f, 0x80u},
                {0.0f, 3.0f, 1.0f, 0x40u},
                {-0.0f, 3.0f, 1.0f, 0xc0u},
                {minimum, 0.5f, 1.0f, 0x140u},
                {maximum, 2.0f, -maximum, 0x200u},
            };
            for (const auto &input : inputs)
            {
                VU1Interpreter vu1;
                vu1.state().vf[1][0] = input.left;
                vu1.state().vf[2][0] = input.right;
                vu1.state().acc[0] = input.acc;
                vu1.execute(fx.code, PS2_VU1_CODE_SIZE, fx.data, PS2_VU1_DATA_SIZE,
                            fx.gs, &fx.mem, 0u, 0u, 0u, 5u);
                t.Equals(vu1.state().status, input.status,
                         "a normal accumulated result must retain exceptional product conditions");
            }
        });

        tc.Run("FMAC product-sum fast flags require a certified normal result", [](TestCase &t)
        {
            const float minimum = std::numeric_limits<float>::min();
            const float maximum = std::numeric_limits<float>::max();
            t.IsTrue(ps2_vu_detail::normalProductSumFlags(7.0f, 2.0f, 3.0f),
                     "ordinary matrix arithmetic should use the fast flags");
            t.IsTrue(!ps2_vu_detail::normalProductSumFlags(minimum, minimum, 0.5f),
                     "the minimum normal exponent requires exact underflow checks");
            t.IsTrue(!ps2_vu_detail::normalProductSumFlags(maximum, maximum, 1.0f),
                     "the maximum exponent requires exact overflow checks");
            t.IsTrue(!ps2_vu_detail::normalProductSumFlags(maximum / 2.0f, maximum, 2.0f),
                     "a clamped product followed by cancellation is not certified");
            t.IsTrue(!ps2_vu_detail::normalProductSumFlags(0x1p-22f, 1.0f, 1.0f),
                     "near cancellation must keep exact checks");
            t.IsTrue(ps2_vu_detail::normalProductSumFlags(0x1p106f, 0x1p63f, 0x1p62f),
                     "product exponent sum 379 and result exponent plus 146 are included");
            t.IsTrue(!ps2_vu_detail::normalProductSumFlags(0x1p107f, 0x1p63f, 0x1p63f),
                     "product exponent sum 380 retains the exact fallback");
            t.IsTrue(!ps2_vu_detail::normalProductSumFlags(0x1p105f, 0x1p63f, 0x1p62f),
                     "product exponent sum at result exponent plus 147 is excluded");
            t.IsTrue(ps2_vu_detail::normalProductSumFlags(7.0f, 0.0f, maximum) &&
                     ps2_vu_detail::normalProductSumFlags(-7.0f, maximum, -0.0f),
                     "both signed-zero multiplicands leave a normal accumulator unchanged");
        });

        tc.Run("FMAC product-sum fast flags match exact fused and separate arithmetic", [](TestCase &t)
        {
            struct RestoreRounding {
                int mode = std::fegetround();
                ~RestoreRounding() { std::fesetround(mode); }
            } rounding;
            std::fesetround(FE_TOWARDZERO);
            uint64_t randomState = 0x8467410553247782ull;
            const auto random = [&]() {
                randomState ^= randomState << 13u;
                randomState ^= randomState >> 7u;
                randomState ^= randomState << 17u;
                return static_cast<uint32_t>(randomState);
            };
            const auto normalized = [](uint32_t bits) {
                const uint32_t exponent = bits & 0x7f800000u;
                if (exponent == 0u)
                    bits &= 0x80000000u;
                else if (exponent == 0x7f800000u)
                    bits = (bits & 0x80000000u) | 0x7f7fffffu;
                return std::bit_cast<float>(bits);
            };
            uint32_t accepted = 0u;
            bool matches = true;
            const auto check = [&](float left, float right, float acc) {
                volatile float product = left * right;
                const float results[] = {product + acc, std::fma(left, right, acc)};
                const long double exact = static_cast<long double>(left) * right + acc;
                for (const float result : results)
                {
                    if (!ps2_vu_detail::normalProductSumFlags(result, left, right))
                        continue;
                    ++accepted;
                    matches &= std::signbit(exact) == std::signbit(result) &&
                        std::fabs(exact) >= std::numeric_limits<float>::min() &&
                        std::fabs(exact) <= std::numeric_limits<float>::max();
                }
            };
            for (uint32_t iteration = 0u; iteration < 100000u; ++iteration)
            {
                const float left = normalized(random()), right = normalized(random());
                check(left, right, normalized(random()));
                const uint32_t oppositeProduct = std::bit_cast<uint32_t>(left * right) ^ 0x80000000u;
                for (const int32_t offset : {-2, -1, 0, 1, 2})
                    check(left, right, normalized(oppositeProduct + offset));
            }
            t.IsTrue(matches, "every accepted result must have exact normal/sign flags");
            t.IsTrue(accepted > 100000u, "the randomized test must exercise the certified path");
        });

#if defined(__aarch64__)
        tc.Run("SIMD product-sum values and flags match independent scalar arithmetic", [](TestCase &t)
        {
            struct RestoreRounding {
                int mode = std::fegetround();
                ~RestoreRounding() { std::fesetround(mode); }
            } rounding;
            std::fesetround(FE_TOWARDZERO);
            uint64_t randomState = 0x8754617084462351ull;
            const auto random = [&]() {
                randomState ^= randomState << 13u;
                randomState ^= randomState >> 7u;
                randomState ^= randomState << 17u;
                return static_cast<uint32_t>(randomState);
            };
            const auto normalized = [](uint32_t bits) {
                const uint32_t exponent = bits & 0x7f800000u;
                if (exponent == 0u)
                    bits &= 0x80000000u;
                else if (exponent == 0x7f800000u)
                    bits = (bits & 0x80000000u) | 0x7f7fffffu;
                return std::bit_cast<float>(bits);
            };
            const auto flags = [](long double exact) {
                const auto magnitude = std::fabs(exact);
                return (std::signbit(exact) ? 2u : 0u) |
                    (magnitude == 0.0L ? 1u : magnitude < std::numeric_limits<float>::min() ? 5u :
                     magnitude > std::numeric_limits<float>::max() ? 8u : 0u);
            };
            uint32_t accepted = 0u;
            bool matches = true;
            const auto check = [&]<bool subtract>(const float left[4], const float right[4],
                                                  const float acc[4], uint8_t destination) {
                float result[4] = {123.0f, 123.0f, 123.0f, 123.0f};
                uint8_t laneFlags[4] = {0xff, 0xff, 0xff, 0xff};
                uint32_t sticky = 0xfeedbeef;
                if (!ps2_vu_detail::tryProductSumVector<subtract>(left, right, acc, destination, result, laneFlags, sticky))
                {
                    for (unsigned lane = 0u; lane < 4u; ++lane)
                        matches &= result[lane] == 123.0f && laneFlags[lane] == 0xff && sticky == 0xfeedbeef;
                    return;
                }
                ++accepted;
                uint32_t expectedSticky = 0u;
                for (unsigned lane = 0u; lane < 4u; ++lane)
                {
                    const float l = normalized(std::bit_cast<uint32_t>(left[lane]));
                    const float r = normalized(std::bit_cast<uint32_t>(right[lane]));
                    const float a = normalized(std::bit_cast<uint32_t>(acc[lane]));
                    const float expected = std::fma(subtract ? -l : l, r, a);
                    const long double product = static_cast<long double>(l) * r;
                    const long double exact = subtract ? static_cast<long double>(a) - product : static_cast<long double>(a) + product;
                    const bool active = (destination & (8u >> lane)) != 0u;
                    matches &= std::bit_cast<uint32_t>(result[lane]) == std::bit_cast<uint32_t>(expected) &&
                        laneFlags[lane] == (active ? flags(exact) : 0u);
                    if (active)
                        expectedSticky |= flags(product);
                }
                matches &= sticky == expectedSticky;
            };
            const auto checkBoth = [&](const float left[4], const float right[4], const float acc[4], uint8_t mask) {
                check.template operator()<false>(left, right, acc, mask);
                check.template operator()<true>(left, right, acc, mask);
            };
            struct GuardBoundary { float left, right, acc; bool accepted; };
            const GuardBoundary boundaries[] = {
                {0x1p63f, 0x1p62f, -(0x1p125f - 0x1p106f), true},
                {0x1p63f, 0x1p62f, -(0x1p125f - 0x1p105f), false},
                {0x1p63f, 0x1p63f, -0x1p125f, false},
                {0.0f, std::numeric_limits<float>::max(), 7.0f, true},
                {-0.0f, std::numeric_limits<float>::max(), -7.0f, true},
            };
            for (const auto &boundary : boundaries)
            {
                float left[4], right[4], acc[4];
                std::fill_n(left, 4u, boundary.left);
                std::fill_n(right, 4u, boundary.right);
                std::fill_n(acc, 4u, boundary.acc);
                uint32_t previous = accepted;
                check.template operator()<false>(left, right, acc, 0xfu);
                matches &= accepted == previous + boundary.accepted;
                std::fill_n(acc, 4u, -boundary.acc);
                previous = accepted;
                check.template operator()<true>(left, right, acc, 0xfu);
                matches &= accepted == previous + boundary.accepted;
            }
            for (unsigned signs = 0u; signs < 8u; ++signs)
                for (uint8_t mask = 0u; mask < 16u; ++mask)
                {
                    float left[4], right[4], acc[4];
                    for (unsigned lane = 0u; lane < 4u; ++lane)
                    {
                        left[lane] = std::bit_cast<float>((signs & 1u) << 31u);
                        right[lane] = std::bit_cast<float>((signs & 2u) << 30u);
                        acc[lane] = std::bit_cast<float>((signs & 4u) << 29u);
                    }
                    checkBoth(left, right, acc, mask);
                }
            for (uint32_t iteration = 0u; iteration < 100000u; ++iteration)
            {
                float left[4], right[4], acc[4];
                for (unsigned lane = 0u; lane < 4u; ++lane)
                {
                    uint32_t l = random(), r = random(), a = random();
                    if (iteration % 3u == 0u)
                    {
                        l = (l & 0x807fffffu) | ((115u + l % 24u) << 23u);
                        r = (r & 0x807fffffu) | ((115u + r % 24u) << 23u);
                        a = (a & 0x807fffffu) | ((115u + a % 24u) << 23u);
                    }
                    left[lane] = std::bit_cast<float>(l);
                    right[lane] = std::bit_cast<float>(r);
                    acc[lane] = std::bit_cast<float>(a);
                    if (iteration % 3u == 2u)
                    {
                        const float product = normalized(l) * normalized(r);
                        acc[lane] = normalized((std::bit_cast<uint32_t>(product) ^ 0x80000000u) + iteration % 5u - 2u);
                    }
                }
                checkBoth(left, right, acc, static_cast<uint8_t>(random() & 15u));
            }
            t.IsTrue(matches, "accepted vectors must match exact flags and scalar fused values; fallback must leave outputs unchanged");
            t.IsTrue(accepted > 100000u, "the test must exercise the vector path across destination masks");
        });
#endif

        tc.Run("reserved opcodes stop before executing or corrupting state", [](TestCase &t)
        {
            Vu1Fixture fx;
            t.IsTrue(fx.initialize(), "VU1 fixture should initialize");

            writeVuInstructionPair(fx.code, 0u, 0u, 0x30u);
            writeVuInstructionPair(
                fx.code, 8u, makeVuIaddiu(1u, 0u, 7),
                kVuUpperNop);

            VU1Interpreter vu1;
            vu1.execute(fx.code, PS2_VU1_CODE_SIZE,
                        fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem,
                        0u, 0u, 0u, 8u);

            t.Equals(vu1.state().cycles, static_cast<uint64_t>(0u),
                     "reserved opcode should stop before consuming its issue cycle");
            t.Equals(vu1.state().pc, 0u,
                     "reserved opcode should retain the diagnostic PC");
            t.Equals(vu1.state().vi[1], 0,
                     "instruction following a reserved opcode must not execute");
        });
    });
}
