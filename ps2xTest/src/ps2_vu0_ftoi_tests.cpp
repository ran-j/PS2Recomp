#include "MiniTest.h"
#include "ps2recomp/code_generator.h"
#include "ps2recomp/instructions.h"
#include "ps2recomp/r5900_decoder.h"
#include "ps2recomp/types.h"
#include "ps2_runtime_macros.h"

#include <bit>
#include <cstdint>
#include <cstdio>
#include <string>

// VFTOIn truncates toward zero and saturates by sign at 2^31 (PCSX2 VUops.cpp floatToInt).
// _mm_cvttps_epi32 alone gives 0x80000000 for every out-of-range value, both signs.

using namespace ps2recomp;

namespace
{
    void expectWords(TestCase &t, __m128i value, const uint32_t (&expected)[4], const std::string &what)
    {
        alignas(16) uint32_t got[4];
        _mm_store_si128(reinterpret_cast<__m128i *>(got), value);
        const bool same = got[0] == expected[0] && got[1] == expected[1] && got[2] == expected[2] && got[3] == expected[3];
        std::string text = what + ": expected";
        char word[16];
        for (int i = 0; i < 4; ++i)
        {
            std::snprintf(word, sizeof(word), " %08X", expected[i]);
            text += word;
        }
        text += ", got";
        for (int i = 0; i < 4; ++i)
        {
            std::snprintf(word, sizeof(word), " %08X", got[i]);
            text += word;
        }
        t.IsTrue(same, text);
    }

    float fromBits(uint32_t value)
    {
        return std::bit_cast<float>(value);
    }

    __m128 floats(float x, float y, float z, float w)
    {
        return _mm_set_ps(w, z, y, x);
    }

    // COP2 Special2 macro instruction: dest, ft, fs, and the 7-bit Special2 opcode.
    uint32_t cop2Special2(uint32_t dest, uint32_t ft, uint32_t fs, uint32_t op)
    {
        return (OPCODE_COP2 << 26) | (1u << 25) | ((dest & 0xFu) << 21) | ((ft & 31u) << 16) | ((fs & 31u) << 11) |
               (((op >> 2) & 31u) << 6) | 0x3Cu | (op & 3u);
    }

    std::string translate(uint32_t address, uint32_t raw)
    {
        R5900Decoder decoder;
        CodeGenerator gen({}, {});
        return gen.translateInstruction(decoder.decodeInstruction(address, raw));
    }

    bool contains(const std::string &text, const std::string &part)
    {
        return text.find(part) != std::string::npos;
    }
}

void register_ps2_vu0_ftoi_tests()
{
    MiniTest::Case("PS2Vu0Ftoi", [](TestCase &tc)
    {
        tc.Run("FTOI truncates and saturates by sign", [](TestCase &t)
        {
            expectWords(t, ps2_vu_ftoi(floats(1.75f, -1.75f, 3.0e9f, -3.0e9f), 1.0f),
                        {1u, 0xFFFFFFFFu, 0x7FFFFFFFu, 0x80000000u}, "FTOI0 (1.75, -1.75, 3e9, -3e9)");
            expectWords(t, ps2_vu_ftoi(floats(1.0f, 134217728.0f, -134217728.0f, fromBits(0x7F800001u)), 16.0f),
                        {16u, 0x7FFFFFFFu, 0x80000000u, 0x7FFFFFFFu}, "FTOI4 (1, 2^27, -2^27, exponent 255)");
            expectWords(t, ps2_vu_ftoi(floats(0.5f, -0.25f, 131072.0f, fromBits(0xFF800000u)), 32768.0f),
                        {16384u, 0xFFFFE000u, 0x7FFFFFFFu, 0x80000000u}, "FTOI15 (0.5, -0.25, 2^17, -exponent 255)");
            expectWords(t, ps2_vu_ftoi(floats(fromBits(0x00000001u), 0.5f, -2147483648.0f, 2147483520.0f), 1.0f),
                        {0u, 0u, 0x80000000u, 0x7FFFFF80u}, "FTOI0 (denormal, 0.5, -2^31, largest float below 2^31)");
        });

        tc.Run("FTOI emitters scale and saturate through ps2_vu_ftoi", [](TestCase &t)
        {
            // vftoi4.xyzw $vf3, $vf7  (fs = vf7, ft = vf3)
            const std::string code = translate(0x00200000u, cop2Special2(0xF, 3, 7, VU0_S2_VFTOI4));
            t.IsTrue(contains(code, "ps2_vu_ftoi(ctx->vu0_vf[7], 16") && contains(code, "ctx->vu0_vf[3] ="),
                     "vftoi4 $vf3, $vf7 should be vf3 = ftoi(vf7 * 16) but got: " + code);
            t.IsTrue(!contains(code, "_mm_cvttps_epi32"), "vftoi4 must not truncate without saturation but got: " + code);
        });
    });
}
