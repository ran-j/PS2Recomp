#include "MiniTest.h"
#include "ps2recomp/code_generator.h"
#include "ps2recomp/instructions.h"
#include "ps2recomp/r5900_decoder.h"
#include "ps2recomp/types.h"
#include "ps2_runtime_macros.h"

#include <bit>
#include <cstdint>
#include <string>

// The VU0 macro-mode FDIV unit checked against PCSX2's interpreter (pcsx2/VUops.cpp _vuDIV,
// _vuSQRT, _vuRSQRT). The PS2 has no NaN: a zero divisor gives +-max, and SQRT/RSQRT take |ft|.

using namespace ps2recomp;

namespace
{
    uint32_t bits(float value)
    {
        return std::bit_cast<uint32_t>(value);
    }

    float fromBits(uint32_t value)
    {
        return std::bit_cast<float>(value);
    }

    // A float the compiler cannot constant-fold: the bits come from a volatile load.
    float runtimeBits(uint32_t value)
    {
        volatile uint32_t stored = value;
        return std::bit_cast<float>(static_cast<uint32_t>(stored));
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

void register_ps2_vu0_fdiv_tests()
{
    MiniTest::Case("PS2Vu0Fdiv", [](TestCase &tc)
    {
        tc.Run("RSQRT is fs / sqrt(|ft|)", [](TestCase &t)
        {
            t.Equals(bits(ps2_vu_rsqrt(1.0f, 4.0f)), bits(0.5f), "1 / sqrt(4)");
            t.Equals(bits(ps2_vu_rsqrt(3.0f, 4.0f)), bits(1.5f), "fs is the numerator: 3 / sqrt(4)");
            t.Equals(bits(ps2_vu_rsqrt(1.0f, -4.0f)), bits(0.5f), "the sign of ft is ignored: 1 / sqrt(|-4|)");
        });

        tc.Run("RSQRT of a zero ft gives +-max, or +-0 when fs is zero too", [](TestCase &t)
        {
            t.Equals(bits(ps2_vu_rsqrt(1.0f, 0.0f)), 0x7FFFFFFFu, "1 / sqrt(+0) = +max");
            t.Equals(bits(ps2_vu_rsqrt(1.0f, -0.0f)), 0xFFFFFFFFu, "1 / sqrt(-0) = -max (sign of fs xor ft)");
            t.Equals(bits(ps2_vu_rsqrt(-2.0f, 0.0f)), 0xFFFFFFFFu, "-2 / sqrt(+0) = -max");
            t.Equals(bits(ps2_vu_rsqrt(1.0f, fromBits(0x00000001u))), 0x7FFFFFFFu, "a denormal ft is zero");
            // The runtime and these tests build with /fp:fast, under which MSVC may materialise a
            // constant-folded -0.0f as +0.0f. Generated code always passes run-time values, so the
            // signed-zero cases load their operands from volatile bit patterns.
            t.Equals(bits(ps2_vu_rsqrt(runtimeBits(0x00000000u), runtimeBits(0x00000000u))), 0x00000000u, "0 / sqrt(+0) = +0");
            t.Equals(bits(ps2_vu_rsqrt(runtimeBits(0x00000000u), runtimeBits(0x80000000u))), 0x80000000u, "0 / sqrt(-0) = -0");
            t.Equals(bits(ps2_vu_rsqrt(runtimeBits(0x80000000u), runtimeBits(0x00000000u))), 0x80000000u, "-0 / sqrt(+0) = -0");
        });

        tc.Run("DIV by zero gives +-max and SQRT takes |ft|", [](TestCase &t)
        {
            t.Equals(bits(ps2_fdiv(1.0f, 4.0f)), bits(0.25f), "1 / 4");
            t.Equals(bits(ps2_fdiv(1.0f, 0.0f)), 0x7FFFFFFFu, "1 / +0 = +max");
            t.Equals(bits(ps2_fdiv(1.0f, -0.0f)), 0xFFFFFFFFu, "1 / -0 = -max");
            t.Equals(bits(ps2_fdiv(0.0f, 0.0f)), 0x7FFFFFFFu, "0 / 0 = +max");
            t.Equals(bits(ps2_fsqrt(-4.0f)), bits(2.0f), "sqrt(|-4|)");
        });

        tc.Run("VRSQRT and VDIV translate their fs and ft fields", [](TestCase &t)
        {
            // Stuntman (SLUS-20250) 0x0011BC1C: vrsqrt Q, $vf0w, $vf1x. All 63 VRSQRT sites in
            // the title read vf0.w, so an emitter that ignores fs computes 1/sqrt(ft) instead.
            const std::string rsqrt = translate(0x0011BC1Cu, 0x4A6103BEu);
            t.IsTrue(contains(rsqrt, "float fs = _mm_cvtss_f32(_mm_shuffle_ps(ctx->vu0_vf[0], ctx->vu0_vf[0], _MM_SHUFFLE(0,0,0,3)))") &&
                         contains(rsqrt, "float ft = _mm_cvtss_f32(_mm_shuffle_ps(ctx->vu0_vf[1], ctx->vu0_vf[1], _MM_SHUFFLE(0,0,0,0)))") &&
                         contains(rsqrt, "ctx->vu0_q = ps2_vu_rsqrt(fs, ft)"),
                     "0x4A6103BE should be Q = vf0.w / sqrt(|vf1.x|) but got: " + rsqrt);
            // Stuntman 0x001D85F4: vdiv Q, $vf0w, $vf1y
            const std::string div = translate(0x001D85F4u, 0x4AE103BCu);
            t.IsTrue(contains(div, "_MM_SHUFFLE(0,0,0,3)") && contains(div, "ctx->vu0_vf[1], ctx->vu0_vf[1], _MM_SHUFFLE(0,0,0,1)") &&
                         contains(div, "ctx->vu0_q = ps2_fdiv(fs, ft)"),
                     "0x4AE103BC should be Q = vf0.w / vf1.y through ps2_fdiv but got: " + div);
            // vsqrt Q, $vf2y
            const std::string sqrt = translate(0x00200000u, cop2Special2(1u << 2, 2, 0, VU0_S2_VSQRT));
            t.IsTrue(contains(sqrt, "ctx->vu0_vf[2], ctx->vu0_vf[2], _MM_SHUFFLE(0,0,0,1)") && contains(sqrt, "ctx->vu0_q = ps2_fsqrt(ft)"),
                     "vsqrt Q, $vf2y should be Q = sqrt(|vf2.y|) through ps2_fsqrt but got: " + sqrt);
        });
    });
}
