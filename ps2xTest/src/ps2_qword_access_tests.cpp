#include "MiniTest.h"
#include "ps2recomp/code_generator.h"
#include "ps2recomp/instructions.h"
#include "ps2recomp/r5900_decoder.h"
#include "ps2recomp/types.h"
#include "ps2_runtime_macros.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// The EE ignores the low four address bits of LQ, SQ, LQC2 and SQC2 (the manuals define the
// effective address as a quadword address; PCSX2 masks it in interpreter and recompiler alike).
// A misaligned address must access the containing quadword, not straddle two.

using namespace ps2recomp;

namespace
{
    std::string lanes(const uint32_t *words, int count)
    {
        std::string text;
        for (int i = 0; i < count; ++i)
        {
            char word[12];
            std::snprintf(word, sizeof(word), "%s%08X", i ? " " : "", words[i]);
            text += word;
        }
        return text;
    }

    void expectWords(TestCase &t, __m128i value, const uint32_t (&expected)[4], const std::string &what)
    {
        alignas(16) uint32_t got[4];
        _mm_store_si128(reinterpret_cast<__m128i *>(got), value);
        const bool same = got[0] == expected[0] && got[1] == expected[1] && got[2] == expected[2] && got[3] == expected[3];
        t.IsTrue(same, what + ": expected " + lanes(expected, 4) + ", got " + lanes(got, 4));
    }

    __m128i words(uint32_t w0, uint32_t w1, uint32_t w2, uint32_t w3)
    {
        return _mm_set_epi32(static_cast<int>(w3), static_cast<int>(w2), static_cast<int>(w1), static_cast<int>(w0));
    }

    bool contains(const std::string &text, const std::string &part)
    {
        return text.find(part) != std::string::npos;
    }
}

void register_ps2_qword_access_tests()
{
    MiniTest::Case("PS2QuadwordAccess", [](TestCase &tc)
    {
        tc.Run("WRITE128 and READ128 ignore the low four address bits", [](TestCase &t)
        {
            std::vector<uint8_t> ram(PS2_RAM_SIZE, 0xEEu);
            uint8_t *rdram = ram.data();
            R5900Context context;
            R5900Context *ctx = &context;
            PS2Runtime *runtime = nullptr; // only touched for special (I/O) addresses
            (void)runtime;
            (void)ctx;

            const __m128i value = words(0x00112233u, 0x44556677u, 0x8899AABBu, 0xCCDDEEFFu);
            WRITE128(0x00100107u, value);
            uint32_t stored[4];
            std::memcpy(stored, rdram + 0x00100100u, sizeof(stored));
            t.IsTrue(stored[0] == 0x00112233u && stored[1] == 0x44556677u && stored[2] == 0x8899AABBu && stored[3] == 0xCCDDEEFFu,
                     "SQ at 0x00100107 must store the quadword at 0x00100100, got " + lanes(stored, 4));
            t.IsTrue(rdram[0x00100110u] == 0xEEu && rdram[0x001000FFu] == 0xEEu,
                     "SQ at 0x00100107 must not touch the neighbouring quadwords");

            expectWords(t, READ128(0x0010010Fu), {0x00112233u, 0x44556677u, 0x8899AABBu, 0xCCDDEEFFu},
                        "LQ at 0x0010010F must read the quadword at 0x00100100");
        });

        tc.Run("resolved-address and I/O quadword accesses are masked by the emitter", [](TestCase &t)
        {
            CodeGenerator gen({}, {});
            Instruction sq{};
            sq.opcode = OPCODE_SQ;
            sq.rs = 4;
            sq.rt = 5;
            sq.simmediate = 7;
            MemoryAccessHint hint;
            hint.hasAddress = true;
            hint.address = 0x00100107u;
            const std::string fast = gen.translateInstruction(sq, hint);
            t.IsTrue(contains(fast, "FAST_WRITE128(0x100100u") && !contains(fast, "0x100107"),
                     "a resolved SQ address must be masked to 0x100100 but got: " + fast);

            sq.isMmio = true;
            const std::string io = gen.translateInstruction(sq);
            t.IsTrue(contains(io, "runtime->Store128(rdram, ctx, PS2_QWORD_ADDR(ADD32(GPR_U32(ctx, 4), 7))"),
                     "an I/O SQ address must go through PS2_QWORD_ADDR but got: " + io);
        });
    });
}
