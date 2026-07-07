#include "MiniTest.h"
#include "runtime/ps2_gif_arbiter.h"
#include "runtime/ps2_gs_gpu.h"
#include "runtime/ps2_gs_psmct32.h"
#include "runtime/ps2_memory.h"
#include "runtime/ps2_vu1.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace
{
    constexpr uint32_t kVuUpperNop = 0u;

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

    void writeVuQword(uint8_t *data, uint32_t qwordIndex, const float values[4])
    {
        std::memcpy(data + qwordIndex * 16u, values, sizeof(float) * 4u);
    }

    void readVuQword(const uint8_t *data, uint32_t qwordIndex, float values[4])
    {
        std::memcpy(values, data + qwordIndex * 16u, sizeof(float) * 4u);
    }
}

void register_ps2_vu1_tests()
{
    MiniTest::Case("PS2VU1", [](TestCase &tc)
    {
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

            t.Equals(vu1.state().vf[2][0], 3.0f, "ADDi should use old I for x");
            t.Equals(vu1.state().vf[2][1], 4.0f, "ADDi should use old I for y");
            t.Equals(vu1.state().vf[2][2], 5.0f, "ADDi should use old I for z");
            t.Equals(vu1.state().vf[2][3], 6.0f, "ADDi should use old I for w");
            t.Equals(vu1.state().i, 7.0f, "LOI should commit lower immediate into I after upper execution");
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

            vu1.execute(fx.code, PS2_VU1_CODE_SIZE, fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem, 0u, 0u, 0u, 2u);

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

            vu1.execute(fx.code, PS2_VU1_CODE_SIZE, fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem, 0u, 0u, 0u, 1u);

            float stored[4] = {};
            readVuQword(fx.data, 6u, stored);
            t.Equals(stored[0], 1.0f, "SQ should observe old VF value for x");
            t.Equals(stored[1], 2.0f, "SQ should observe old VF value for y");
            t.Equals(stored[2], 3.0f, "SQ should observe old VF value for z");
            t.Equals(stored[3], 4.0f, "SQ should observe old VF value for w");
            t.Equals(vu1.state().vf[1][0], 110.0f, "upper ADD should write x after lower read");
            t.Equals(vu1.state().vf[1][1], 220.0f, "upper ADD should write y after lower read");
            t.Equals(vu1.state().vf[1][2], 330.0f, "upper ADD should write z after lower read");
            t.Equals(vu1.state().vf[1][3], 440.0f, "upper ADD should write w after lower read");
        });

        tc.Run("DIV and SQRT update the Q register from selected vector components", [](TestCase &t)
        {
            Vu1Fixture fx;
            t.IsTrue(fx.initialize(), "VU1 fixture should initialize");

            writeVuInstructionPair(fx.code, 0u, makeVuDiv(1u, 2u, 1u, 2u), kVuUpperNop);  // Q = vf1.y / vf2.z
            writeVuInstructionPair(fx.code, 8u, makeVuSqrt(3u, 3u), kVuUpperNop);         // Q = sqrt(abs(vf3.w))

            VU1Interpreter vu1;
            vu1.state().vf[1][1] = 18.0f;
            vu1.state().vf[2][2] = 3.0f;
            vu1.state().vf[3][3] = 25.0f;

            vu1.execute(fx.code, PS2_VU1_CODE_SIZE, fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem, 0u, 0u, 0u, 1u);
            t.Equals(vu1.state().q, 6.0f, "DIV should divide selected FS and FT components into Q");

            vu1.resume(fx.code, PS2_VU1_CODE_SIZE, fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem, 0u, 0u, 1u);
            t.Equals(vu1.state().q, 5.0f, "SQRT should write square root of selected FT component into Q");
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
                        1u);

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
                            1u);
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
    });
}
