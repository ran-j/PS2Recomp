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

    // kVuUpperNop (raw 0) decodes as ADDbc.x vf0, vf0, vf0 with dest=0: a real FMAC
    // instruction that writes no VF lanes but, once MAC/STATUS are modelled, legitimately
    // clears MAC/STATUS every time it runs (dest=0 means every lane's flags are cleared,
    // not skipped). Tests that check MAC/STATUS/CLIP need filler that is truly inert on the
    // upper pipe: the special-group NOP at specialOp 0x2F, which returns before touching any
    // VF/MAC/STATUS state. kVuUpperFmacNop is that encoding.
    constexpr uint32_t kVuUpperFmacNop = (0x0Bu << 6) | 0x3Fu; // specialOp 0x2F, dest=ft=fs=0

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

    // Encodes the lower-word primary-opcode space 0x10..0x1C: the flag-condition ops
    // (FCEQ/FCSET/FCAND/FCOR/FSEQ/FSSET/FSAND/FSOR, which use a bare 24/12-bit immediate)
    // and the flag-register ops (FMEQ/FMAND/FMOR/FCGET, which use it/is like an integer op).
    // Callers pass 0 for whichever field group their instruction does not have.
    uint32_t makeVuLowerOpHi(uint8_t opHi, uint8_t it, uint8_t is, uint32_t imm)
    {
        return (static_cast<uint32_t>(opHi & 0x7Fu) << 25) |
               (static_cast<uint32_t>(it & 0xFu) << 16) |
               (static_cast<uint32_t>(is & 0xFu) << 11) |
               (imm & 0xFFFFFFu);
    }

    // Encodes the upper-word special group (raw op 0x3C..0x3F). The real selector is
    // (instr & 0x3) | ((instr >> 4) & 0x7C); the FD field doubles as the top bits of that
    // selector, which is why CLIP/ITOF/FTOI/ABS cannot be built with makeVuUpper.
    uint32_t makeVuUpperSpecial(uint8_t specialOp, uint8_t dest, uint8_t ft, uint8_t fs)
    {
        const uint8_t op6 = static_cast<uint8_t>(0x3Cu | (specialOp & 0x3u));
        const uint8_t fdField = static_cast<uint8_t>((specialOp >> 2) & 0x1Fu);
        return (static_cast<uint32_t>(dest & 0xFu) << 21) |
               (static_cast<uint32_t>(ft & 0x1Fu) << 16) |
               (static_cast<uint32_t>(fs & 0x1Fu) << 11) |
               (static_cast<uint32_t>(fdField) << 6) |
               static_cast<uint32_t>(op6);
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

        tc.Run("FMAC computes MAC Z and S bits for zero and negative results", [](TestCase &t)
        {
            // Z case: SUB.xyzw vf3, vf1, vf1 -> all-lane zero result.
            {
                Vu1Fixture fx;
                t.IsTrue(fx.initialize(), "VU1 fixture should initialize");

                uint32_t pc = 0u;
                writeVuInstructionPair(fx.code, pc, 0u, makeVuUpper(0x2Cu, 0xFu, 1u, 1u, 3u)); // SUB.xyzw vf3, vf1, vf1
                pc += 8u;
                for (int i = 0; i < 4; ++i)
                {
                    writeVuInstructionPair(fx.code, pc, 0u, kVuUpperFmacNop);
                    pc += 8u;
                }
                writeVuInstructionPair(fx.code, pc, makeVuLowerOpHi(0x1Au, 1u, 2u, 0u), kVuUpperFmacNop); // FMAND vi1, vi2
                pc += 8u;

                VU1Interpreter vu1;
                vu1.state().vf[1][0] = 5.0f;
                vu1.state().vf[1][1] = 5.0f;
                vu1.state().vf[1][2] = 5.0f;
                vu1.state().vf[1][3] = 5.0f;
                vu1.state().vi[2] = 0xFFFF;

                vu1.execute(fx.code, PS2_VU1_CODE_SIZE, fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem, 0u, 0u, 0u, pc / 8u);

                t.Equals(vu1.state().vi[1], 0xF, "FMAND after a zero FMAC result should read back the four Z bits");
            }

            // S case: SUB.xyzw vf3, vf1, vf2 with vf1 < vf2 in every lane -> all-lane negative result.
            {
                Vu1Fixture fx;
                t.IsTrue(fx.initialize(), "VU1 fixture should initialize");

                uint32_t pc = 0u;
                writeVuInstructionPair(fx.code, pc, 0u, makeVuUpper(0x2Cu, 0xFu, 2u, 1u, 3u)); // SUB.xyzw vf3, vf1, vf2
                pc += 8u;
                for (int i = 0; i < 4; ++i)
                {
                    writeVuInstructionPair(fx.code, pc, 0u, kVuUpperFmacNop);
                    pc += 8u;
                }
                writeVuInstructionPair(fx.code, pc, makeVuLowerOpHi(0x1Au, 1u, 2u, 0u), kVuUpperFmacNop); // FMAND vi1, vi2
                pc += 8u;

                VU1Interpreter vu1;
                vu1.state().vf[1][0] = 1.0f;
                vu1.state().vf[1][1] = 1.0f;
                vu1.state().vf[1][2] = 1.0f;
                vu1.state().vf[1][3] = 1.0f;
                vu1.state().vf[2][0] = 5.0f;
                vu1.state().vf[2][1] = 5.0f;
                vu1.state().vf[2][2] = 5.0f;
                vu1.state().vf[2][3] = 5.0f;
                vu1.state().vi[2] = 0xFFFF;

                vu1.execute(fx.code, PS2_VU1_CODE_SIZE, fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem, 0u, 0u, 0u, pc / 8u);

                t.Equals(vu1.state().vi[1], 0xF0, "FMAND after a negative FMAC result should read back the four S bits");
            }
        });

        tc.Run("FMAC sets both MAC U and Z bits on a denormal (underflow) result", [](TestCase &t)
        {
            Vu1Fixture fx;
            t.IsTrue(fx.initialize(), "VU1 fixture should initialize");

            uint32_t pc = 0u;
            // MUL.xyzw vf3, vf1, vf2 with vf1=vf2=1e-20 -> ~1e-40 in every lane: a positive
            // denormal (exponent field 0, nonzero mantissa). PCSX2 flushes this underflow to
            // zero, raising both U and Z per lane.
            writeVuInstructionPair(fx.code, pc, 0u, makeVuUpper(0x2Au, 0xFu, 2u, 1u, 3u)); // MUL.xyzw vf3, vf1, vf2
            pc += 8u;
            for (int i = 0; i < 4; ++i)
            {
                writeVuInstructionPair(fx.code, pc, 0u, kVuUpperFmacNop);
                pc += 8u;
            }
            writeVuInstructionPair(fx.code, pc, makeVuLowerOpHi(0x1Au, 1u, 2u, 0u), kVuUpperFmacNop); // FMAND vi1, vi2
            pc += 8u;

            VU1Interpreter vu1;
            const float tiny = 1e-20f;
            vu1.state().vf[1][0] = tiny;
            vu1.state().vf[1][1] = tiny;
            vu1.state().vf[1][2] = tiny;
            vu1.state().vf[1][3] = tiny;
            vu1.state().vf[2][0] = tiny;
            vu1.state().vf[2][1] = tiny;
            vu1.state().vf[2][2] = tiny;
            vu1.state().vf[2][3] = tiny;
            vu1.state().vi[2] = 0xFFFF;

            vu1.execute(fx.code, PS2_VU1_CODE_SIZE, fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem, 0u, 0u, 0u, pc / 8u);

            // Positive denormal in all four lanes -> U nibble 0x0F00 and Z nibble 0x000F,
            // no S (positive), no O -> mac == 0x0F0F. A U-only denormal branch would read 0x0F00.
            t.Equals(vu1.state().vi[1], 0x0F0F, "FMAND after a denormal FMAC result should read back both the U and Z bits");
        });

        tc.Run("FMAC honors the DEST mask when computing MAC bits", [](TestCase &t)
        {
            Vu1Fixture fx;
            t.IsTrue(fx.initialize(), "VU1 fixture should initialize");

            uint32_t pc = 0u;
            writeVuInstructionPair(fx.code, pc, 0u, makeVuUpper(0x2Cu, 0x8u, 2u, 1u, 3u)); // SUB.x vf3, vf1, vf2 (dest=x only)
            pc += 8u;
            for (int i = 0; i < 4; ++i)
            {
                writeVuInstructionPair(fx.code, pc, 0u, kVuUpperFmacNop);
                pc += 8u;
            }
            writeVuInstructionPair(fx.code, pc, makeVuLowerOpHi(0x1Au, 1u, 2u, 0u), kVuUpperFmacNop); // FMAND vi1, vi2
            pc += 8u;

            VU1Interpreter vu1;
            // x lane produces zero (Z bit, 0x8). y/z/w produce a NEGATIVE result (S bits 0x40/0x20/0x10)
            // if wrongly included, which is a different bit pattern than the x-only Z bit, so a dest-mask
            // bug that computes all four lanes is distinguishable from the correct x-only result.
            vu1.state().vf[1][0] = 5.0f;
            vu1.state().vf[1][1] = 1.0f;
            vu1.state().vf[1][2] = 1.0f;
            vu1.state().vf[1][3] = 1.0f;
            vu1.state().vf[2][0] = 5.0f;
            vu1.state().vf[2][1] = 5.0f;
            vu1.state().vf[2][2] = 5.0f;
            vu1.state().vf[2][3] = 5.0f;
            vu1.state().vi[2] = 0xFFFF;

            vu1.execute(fx.code, PS2_VU1_CODE_SIZE, fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem, 0u, 0u, 0u, pc / 8u);

            t.Equals(vu1.state().vi[1], 0x8, "MAC should only reflect the x lane when dest masks out y/z/w");
        });

        tc.Run("MAX and MINI run in the FMAC pipeline but must not update MAC or STATUS", [](TestCase &t)
        {
            // MAX site (0x2B).
            {
                Vu1Fixture fx;
                t.IsTrue(fx.initialize(), "VU1 fixture should initialize");

                uint32_t pc = 0u;
                // MUL.xyzw vf3, vf1, vf2 with vf1=-1, vf2=0 -> -0.0 in every lane: Z and S
                // both set, mac == 0xFF. This is the pattern FMAND must still read back after
                // the MAX below, if MAX correctly leaves MAC untouched.
                writeVuInstructionPair(fx.code, pc, 0u, makeVuUpper(0x2Au, 0xFu, 2u, 1u, 3u));
                pc += 8u;
                // MAX.xyzw vf12, vf10, vf11 with vf10=5.0, vf11=6.0 -> 6.0 in every lane: a
                // positive nonzero result that raises no MAC bits at all. If MAX wrongly
                // computed flags it would clobber mac to 0x00, a different pattern than 0xFF,
                // so the two outcomes are distinguishable by the read below.
                writeVuInstructionPair(fx.code, pc, 0u, makeVuUpper(0x2Bu, 0xFu, 11u, 10u, 12u));
                pc += 8u;
                for (int i = 0; i < 4; ++i)
                {
                    writeVuInstructionPair(fx.code, pc, 0u, kVuUpperFmacNop);
                    pc += 8u;
                }
                writeVuInstructionPair(fx.code, pc, makeVuLowerOpHi(0x1Au, 1u, 2u, 0u), kVuUpperFmacNop); // FMAND vi1, vi2
                pc += 8u;

                VU1Interpreter vu1;
                vu1.state().vf[1][0] = -1.0f;
                vu1.state().vf[1][1] = -1.0f;
                vu1.state().vf[1][2] = -1.0f;
                vu1.state().vf[1][3] = -1.0f;
                vu1.state().vf[2][0] = 0.0f;
                vu1.state().vf[2][1] = 0.0f;
                vu1.state().vf[2][2] = 0.0f;
                vu1.state().vf[2][3] = 0.0f;
                vu1.state().vf[10][0] = 5.0f;
                vu1.state().vf[10][1] = 5.0f;
                vu1.state().vf[10][2] = 5.0f;
                vu1.state().vf[10][3] = 5.0f;
                vu1.state().vf[11][0] = 6.0f;
                vu1.state().vf[11][1] = 6.0f;
                vu1.state().vf[11][2] = 6.0f;
                vu1.state().vf[11][3] = 6.0f;
                vu1.state().vi[2] = 0xFFFF;

                vu1.execute(fx.code, PS2_VU1_CODE_SIZE, fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem, 0u, 0u, 0u, pc / 8u);

                t.Equals(vu1.state().vi[1], 0xFF, "MAX must not clobber MAC; FMAND should still read the producing FMAC's flags");
            }

            // MINI site (0x2F).
            {
                Vu1Fixture fx;
                t.IsTrue(fx.initialize(), "VU1 fixture should initialize");

                uint32_t pc = 0u;
                // Same -0.0 Z+S producer as the MAX block above, mac == 0xFF.
                writeVuInstructionPair(fx.code, pc, 0u, makeVuUpper(0x2Au, 0xFu, 2u, 1u, 3u));
                pc += 8u;
                // MINI.xyzw vf12, vf10, vf11 with vf10=5.0, vf11=6.0 -> 5.0 in every lane: a
                // positive nonzero result that raises no MAC bits, distinguishable from 0xFF
                // the same way as the MAX block.
                writeVuInstructionPair(fx.code, pc, 0u, makeVuUpper(0x2Fu, 0xFu, 11u, 10u, 12u));
                pc += 8u;
                for (int i = 0; i < 4; ++i)
                {
                    writeVuInstructionPair(fx.code, pc, 0u, kVuUpperFmacNop);
                    pc += 8u;
                }
                writeVuInstructionPair(fx.code, pc, makeVuLowerOpHi(0x1Au, 1u, 2u, 0u), kVuUpperFmacNop); // FMAND vi1, vi2
                pc += 8u;

                VU1Interpreter vu1;
                vu1.state().vf[1][0] = -1.0f;
                vu1.state().vf[1][1] = -1.0f;
                vu1.state().vf[1][2] = -1.0f;
                vu1.state().vf[1][3] = -1.0f;
                vu1.state().vf[2][0] = 0.0f;
                vu1.state().vf[2][1] = 0.0f;
                vu1.state().vf[2][2] = 0.0f;
                vu1.state().vf[2][3] = 0.0f;
                vu1.state().vf[10][0] = 5.0f;
                vu1.state().vf[10][1] = 5.0f;
                vu1.state().vf[10][2] = 5.0f;
                vu1.state().vf[10][3] = 5.0f;
                vu1.state().vf[11][0] = 6.0f;
                vu1.state().vf[11][1] = 6.0f;
                vu1.state().vf[11][2] = 6.0f;
                vu1.state().vf[11][3] = 6.0f;
                vu1.state().vi[2] = 0xFFFF;

                vu1.execute(fx.code, PS2_VU1_CODE_SIZE, fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem, 0u, 0u, 0u, pc / 8u);

                t.Equals(vu1.state().vi[1], 0xFF, "MINI must not clobber MAC; FMAND should still read the producing FMAC's flags");
            }
        });

        tc.Run("STATUS folds MAC into a live half that resets and a sticky half that accumulates", [](TestCase &t)
        {
            Vu1Fixture fx;
            t.IsTrue(fx.initialize(), "VU1 fixture should initialize");

            uint32_t pc = 0u;
            // MUL.xyzw vf3, vf1, vf2 with vf1=-1, vf2=0 -> -0.0 in every lane (Z and S both set).
            writeVuInstructionPair(fx.code, pc, 0u, makeVuUpper(0x2Au, 0xFu, 2u, 1u, 3u));
            pc += 8u;
            // ADD.xyzw vf4, vf5, vf6 with 5+5=10 -> positive nonzero, no flags.
            writeVuInstructionPair(fx.code, pc, 0u, makeVuUpper(0x28u, 0xFu, 6u, 5u, 4u));
            pc += 8u;
            for (int i = 0; i < 4; ++i)
            {
                writeVuInstructionPair(fx.code, pc, 0u, kVuUpperFmacNop);
                pc += 8u;
            }
            writeVuInstructionPair(fx.code, pc, makeVuLowerOpHi(0x16u, 1u, 0u, 0xFFFu), kVuUpperFmacNop); // FSAND vi1, 0xFFF
            pc += 8u;

            VU1Interpreter vu1;
            vu1.state().vf[1][0] = -1.0f;
            vu1.state().vf[1][1] = -1.0f;
            vu1.state().vf[1][2] = -1.0f;
            vu1.state().vf[1][3] = -1.0f;
            vu1.state().vf[2][0] = 0.0f;
            vu1.state().vf[2][1] = 0.0f;
            vu1.state().vf[2][2] = 0.0f;
            vu1.state().vf[2][3] = 0.0f;
            vu1.state().vf[5][0] = 5.0f;
            vu1.state().vf[5][1] = 5.0f;
            vu1.state().vf[5][2] = 5.0f;
            vu1.state().vf[5][3] = 5.0f;
            vu1.state().vf[6][0] = 5.0f;
            vu1.state().vf[6][1] = 5.0f;
            vu1.state().vf[6][2] = 5.0f;
            vu1.state().vf[6][3] = 5.0f;

            vu1.execute(fx.code, PS2_VU1_CODE_SIZE, fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem, 0u, 0u, 0u, pc / 8u);

            const int32_t status = vu1.state().vi[1];
            t.IsTrue((status & 0x40) != 0, "sticky Z should survive a later non-zero FMAC");
            t.IsTrue((status & 0x80) != 0, "sticky S should survive a later non-zero FMAC");
            t.IsTrue((status & 0x01) == 0, "live Z should not persist from the first FMAC once a later FMAC clears it");
        });

        tc.Run("FSSET preserves the live half and writes the sourced immediate into the sticky half", [](TestCase &t)
        {
            Vu1Fixture fx;
            t.IsTrue(fx.initialize(), "VU1 fixture should initialize");

            // Sourced from PCSX2 _vuFSSET: imm12 = ((instr>>21)&1)<<11 | (instr&0x7FF).
            // Target imm12 = 0xFFF, so instr bit21=1 and instr bits10:0=0x7FF -> imm24 = 0x2007FF.
            writeVuInstructionPair(fx.code, 0u, makeVuLowerOpHi(0x15u, 0u, 0u, 0x2007FFu), kVuUpperFmacNop); // FSSET

            VU1Interpreter vu1;
            vu1.state().status = 0x15u; // arbitrary live half, must survive FSSET untouched

            vu1.execute(fx.code, PS2_VU1_CODE_SIZE, fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem, 0u, 0u, 0u, 1u);

            t.Equals(vu1.state().status & 0x3Fu, 0x15u, "FSSET should preserve the live half");
            t.Equals(vu1.state().status & 0xFC0u, 0xFC0u, "FSSET should place the sourced immediate in the sticky half");
        });

        tc.Run("the lower flag-read table decodes FMEQ, FMAND, FMOR, and FCGET at their correct opcodes", [](TestCase &t)
        {
            // FMEQ @ 0x18
            {
                Vu1Fixture fx;
                t.IsTrue(fx.initialize(), "VU1 fixture should initialize");
                writeVuInstructionPair(fx.code, 0u, makeVuLowerOpHi(0x18u, 1u, 2u, 0u), kVuUpperFmacNop);
                VU1Interpreter vu1;
                vu1.state().mac = 0x1234u;
                vu1.state().vi[2] = 0x1234;
                vu1.execute(fx.code, PS2_VU1_CODE_SIZE, fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem, 0u, 0u, 0u, 1u);
                t.Equals(vu1.state().vi[1], 1, "0x18 should decode FMEQ and compare equal MAC/VI values");
            }
            // FMAND @ 0x1A
            {
                Vu1Fixture fx;
                t.IsTrue(fx.initialize(), "VU1 fixture should initialize");
                writeVuInstructionPair(fx.code, 0u, makeVuLowerOpHi(0x1Au, 1u, 2u, 0u), kVuUpperFmacNop);
                VU1Interpreter vu1;
                vu1.state().mac = 0x0F0Fu;
                vu1.state().vi[2] = 0x00FF;
                vu1.execute(fx.code, PS2_VU1_CODE_SIZE, fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem, 0u, 0u, 0u, 1u);
                t.Equals(vu1.state().vi[1], 0x0F, "0x1A should decode FMAND and AND the MAC and VI values");
            }
            // FMOR @ 0x1B
            {
                Vu1Fixture fx;
                t.IsTrue(fx.initialize(), "VU1 fixture should initialize");
                writeVuInstructionPair(fx.code, 0u, makeVuLowerOpHi(0x1Bu, 1u, 2u, 0u), kVuUpperFmacNop);
                VU1Interpreter vu1;
                vu1.state().mac = 0x0F00u;
                vu1.state().vi[2] = 0x000F;
                vu1.execute(fx.code, PS2_VU1_CODE_SIZE, fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem, 0u, 0u, 0u, 1u);
                t.Equals(vu1.state().vi[1], 0x0F0F, "0x1B should decode FMOR and OR the MAC and VI values");
            }
            // FCGET @ 0x1C
            {
                Vu1Fixture fx;
                t.IsTrue(fx.initialize(), "VU1 fixture should initialize");
                writeVuInstructionPair(fx.code, 0u, makeVuLowerOpHi(0x1Cu, 1u, 0u, 0u), kVuUpperFmacNop);
                VU1Interpreter vu1;
                vu1.state().clip = 0xABCDEFu;
                vu1.execute(fx.code, PS2_VU1_CODE_SIZE, fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem, 0u, 0u, 0u, 1u);
                t.Equals(vu1.state().vi[1], 0xDEF, "0x1C should decode FCGET and read the low 12 bits of CLIP");
            }
        });

        tc.Run("ITOF, FTOI, and ABS never feed the flag-computation path", [](TestCase &t)
        {
            Vu1Fixture fx;
            t.IsTrue(fx.initialize(), "VU1 fixture should initialize");

            uint32_t pc = 0u;
            // MUL.xyzw vf3, vf1, vf2 with vf1=-1, vf2=0 -> -0.0 in every lane (mac becomes 0xFF for dest=xyzw).
            writeVuInstructionPair(fx.code, pc, 0u, makeVuUpper(0x2Au, 0xFu, 2u, 1u, 3u));
            pc += 8u;
            writeVuInstructionPair(fx.code, pc, 0u, makeVuUpperSpecial(0x10u, 0xFu, 5u, 4u)); // ITOF0.xyzw vf5, vf4
            pc += 8u;
            writeVuInstructionPair(fx.code, pc, 0u, makeVuUpperSpecial(0x14u, 0xFu, 6u, 7u)); // FTOI0.xyzw vf6, vf7
            pc += 8u;
            writeVuInstructionPair(fx.code, pc, 0u, makeVuUpperSpecial(0x1Du, 0xFu, 8u, 9u)); // ABS.xyzw vf8, vf9
            pc += 8u;
            for (int i = 0; i < 4; ++i)
            {
                writeVuInstructionPair(fx.code, pc, 0u, kVuUpperFmacNop);
                pc += 8u;
            }
            writeVuInstructionPair(fx.code, pc, makeVuLowerOpHi(0x1Au, 1u, 2u, 0u), kVuUpperFmacNop); // FMAND vi1, vi2
            pc += 8u;

            VU1Interpreter vu1;
            vu1.state().vf[1][0] = -1.0f;
            vu1.state().vf[1][1] = -1.0f;
            vu1.state().vf[1][2] = -1.0f;
            vu1.state().vf[1][3] = -1.0f;
            vu1.state().vf[2][0] = 0.0f;
            vu1.state().vf[2][1] = 0.0f;
            vu1.state().vf[2][2] = 0.0f;
            vu1.state().vf[2][3] = 0.0f;

            uint32_t intBits = 1000u;
            float asFloat;
            std::memcpy(&asFloat, &intBits, 4);
            vu1.state().vf[4][0] = asFloat;
            vu1.state().vf[4][1] = asFloat;
            vu1.state().vf[4][2] = asFloat;
            vu1.state().vf[4][3] = asFloat;

            vu1.state().vf[7][0] = 2.5f;
            vu1.state().vf[7][1] = 2.5f;
            vu1.state().vf[7][2] = 2.5f;
            vu1.state().vf[7][3] = 2.5f;

            vu1.state().vf[9][0] = -3.0f;
            vu1.state().vf[9][1] = -3.0f;
            vu1.state().vf[9][2] = -3.0f;
            vu1.state().vf[9][3] = -3.0f;

            vu1.state().vi[2] = 0xFFFF;

            vu1.execute(fx.code, PS2_VU1_CODE_SIZE, fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem, 0u, 0u, 0u, pc / 8u);

            t.Equals(vu1.state().vi[1], 0xFF, "MAC must reflect only the producing FMAC, not the intervening ITOF/FTOI/ABS");
        });

        tc.Run("CLIP accumulation is masked to 24 bits so FCOR can go true again", [](TestCase &t)
        {
            Vu1Fixture fx;
            t.IsTrue(fx.initialize(), "VU1 fixture should initialize");

            uint32_t pc = 0u;
            for (int i = 0; i < 5; ++i)
            {
                writeVuInstructionPair(fx.code, pc, 0u, makeVuUpperSpecial(0x1Fu, 0u, 2u, 1u)); // CLIP vf1.xyz, vf2.w
                pc += 8u;
            }
            writeVuInstructionPair(fx.code, pc, makeVuLowerOpHi(0x13u, 0u, 0u, 0x555555u), kVuUpperFmacNop); // FCOR vi1, 0x555555
            pc += 8u;

            VU1Interpreter vu1;
            vu1.state().vf[1][0] = -2.0f;
            vu1.state().vf[1][1] = -2.0f;
            vu1.state().vf[1][2] = -2.0f;
            vu1.state().vf[2][3] = 1.0f;

            vu1.execute(fx.code, PS2_VU1_CODE_SIZE, fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem, 0u, 0u, 0u, pc / 8u);

            t.Equals(vu1.state().vi[1], 1, "FCOR should go true again once CLIP is masked to 24 bits after five accumulations");
        });

        tc.Run("FTOI4 saturates out-of-range floats instead of wrapping the plain int32 cast", [](TestCase &t)
        {
            Vu1Fixture fx;
            t.IsTrue(fx.initialize(), "VU1 fixture should initialize");

            uint32_t pc = 0u;
            for (uint8_t i = 0; i < 6u; ++i)
            {
                writeVuInstructionPair(fx.code, pc, 0u,
                                       makeVuUpperSpecial(0x15u, 0xFu, static_cast<uint8_t>(20u + i), static_cast<uint8_t>(10u + i)));
                pc += 8u;
            }

            VU1Interpreter vu1;
            vu1.state().vf[10][0] = 1e30f;
            vu1.state().vf[11][0] = -1e30f;
            vu1.state().vf[12][0] = 0.5f;
            vu1.state().vf[13][0] = -0.5f;
            vu1.state().vf[14][0] = 0.0f;
            const uint32_t nanBits = 0x7FC00000u; // positive quiet NaN
            std::memcpy(&vu1.state().vf[15][0], &nanBits, 4);

            vu1.execute(fx.code, PS2_VU1_CODE_SIZE, fx.data, PS2_VU1_DATA_SIZE, fx.gs, &fx.mem, 0u, 0u, 0u, 6u);

            auto bitsOf = [&](int reg) -> uint32_t
            {
                uint32_t v;
                std::memcpy(&v, &vu1.state().vf[reg][0], 4);
                return v;
            };

            t.Equals(bitsOf(20), 0x7FFFFFFFu, "FTOI4 should clamp large positive overflow to INT_MAX");
            t.Equals(bitsOf(21), 0x80000000u, "FTOI4 should clamp large negative overflow to INT_MIN");
            t.Equals(static_cast<int32_t>(bitsOf(22)), 8, "FTOI4 should convert +0.5 scaled by 16 to 8");
            t.Equals(static_cast<int32_t>(bitsOf(23)), -8, "FTOI4 should convert -0.5 scaled by 16 to -8");
            t.Equals(static_cast<int32_t>(bitsOf(24)), 0, "FTOI4 should convert 0.0 to 0");
            t.Equals(bitsOf(25), 0x7FFFFFFFu, "FTOI4 should clamp a positive-signed NaN to INT_MAX per the sourced convention");
        });
    });
}
