#include "MiniTest.h"
#include "ps2recomp/code_generator.h"
#include "ps2recomp/instructions.h"
#include "ps2recomp/types.h"
#include "ps2_runtime_macros.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>

using namespace ps2recomp;

namespace
{
    constexpr uint32_t kPlusMax = 0x7F7FFFFFu;
    constexpr uint32_t kMinusMax = 0xFF7FFFFFu;

    uint32_t bitsOf(float f)
    {
        uint32_t b;
        std::memcpy(&b, &f, sizeof(b));
        return b;
    }

    float floatOf(uint32_t b)
    {
        float f;
        std::memcpy(&f, &b, sizeof(f));
        return f;
    }

    Instruction makeCop1S(uint32_t function, uint8_t fd, uint8_t fs, uint8_t ft)
    {
        Instruction inst{};
        inst.opcode = OPCODE_COP1;
        inst.rs = COP1_S;
        inst.rt = ft;
        inst.rd = fs;
        inst.sa = fd;
        inst.function = function;
        return inst;
    }

    uint32_t laneBits(__m128 v, int lane)
    {
        float lanes[4];
        _mm_storeu_ps(lanes, v);
        return bitsOf(lanes[lane]);
    }
}

void register_ps2_float_semantics_tests()
{
    MiniTest::Case("PS2FloatSemantics", [](TestCase &tc)
    {
        tc.Run("ps2_fclamp turns Inf and NaN into +-max and leaves finite values alone", [](TestCase &t)
        {
            t.Equals(bitsOf(ps2_fclamp(std::numeric_limits<float>::infinity())), kPlusMax, "+Inf should become +max");
            t.Equals(bitsOf(ps2_fclamp(-std::numeric_limits<float>::infinity())), kMinusMax, "-Inf should become -max");
            t.Equals(bitsOf(ps2_fclamp(floatOf(0x7FC00000u))), kPlusMax, "positive NaN should become +max");
            t.Equals(bitsOf(ps2_fclamp(floatOf(0xFFC00000u))), kMinusMax, "negative NaN should become -max");
            t.Equals(bitsOf(ps2_fclamp(1.5f)), bitsOf(1.5f), "finite values should be unchanged");
            t.Equals(bitsOf(ps2_fclamp(floatOf(kPlusMax))), kPlusMax, "max should be unchanged");
            t.Equals(bitsOf(ps2_fclamp(-0.0f)), bitsOf(-0.0f), "negative zero should be unchanged");
        });

        tc.Run("FPU ADD/SUB/MUL saturate instead of overflowing to Inf", [](TestCase &t)
        {
            const float maxf = floatOf(kPlusMax);
            const float minf = floatOf(kMinusMax);
            t.Equals(bitsOf(FPU_ADD_S(maxf, maxf)), kPlusMax, "max + max should saturate to +max");
            t.Equals(bitsOf(FPU_ADD_S(minf, minf)), kMinusMax, "-max + -max should saturate to -max");
            t.Equals(bitsOf(FPU_SUB_S(maxf, minf)), kPlusMax, "max - (-max) should saturate to +max");
            t.Equals(bitsOf(FPU_MUL_S(maxf, 2.0f)), kPlusMax, "max * 2 should saturate to +max");
            t.Equals(bitsOf(FPU_MUL_S(maxf, -2.0f)), kMinusMax, "max * -2 should saturate to -max");
            t.Equals(bitsOf(FPU_ADD_S(1.0f, 2.0f)), bitsOf(3.0f), "ordinary addition should be unchanged");
            t.Equals(bitsOf(FPU_MUL_S(1.5f, 4.0f)), bitsOf(6.0f), "ordinary multiplication should be unchanged");
        });

        tc.Run("FPU ADD/SUB/MUL never produce NaN from Inf or NaN operands", [](TestCase &t)
        {
            const float inf = std::numeric_limits<float>::infinity();
            const float nanf = floatOf(0x7FC00000u);
            t.IsFalse(std::isnan(FPU_SUB_S(inf, inf)), "Inf - Inf should not be NaN");
            t.IsFalse(std::isnan(FPU_MUL_S(inf, 0.0f)), "Inf * 0 should not be NaN");
            t.Equals(bitsOf(FPU_MUL_S(inf, 0.0f)), bitsOf(0.0f), "Inf * 0 should be 0");
            t.IsFalse(std::isnan(FPU_ADD_S(nanf, 1.0f)), "NaN input should not produce NaN");
            t.IsFalse(std::isinf(FPU_ADD_S(nanf, 1.0f)), "NaN input should not leave an Inf");
        });

        tc.Run("FPU DIV.S by zero gives +-max with the sign of fs xor ft", [](TestCase &t)
        {
            t.Equals(bitsOf(FPU_DIV_S(1.0f, 0.0f)), kPlusMax, "1/+0 should be +max");
            t.Equals(bitsOf(FPU_DIV_S(-1.0f, 0.0f)), kMinusMax, "-1/+0 should be -max");
            t.Equals(bitsOf(FPU_DIV_S(1.0f, -0.0f)), kMinusMax, "1/-0 should be -max");
            t.Equals(bitsOf(FPU_DIV_S(-1.0f, -0.0f)), kPlusMax, "-1/-0 should be +max");
            t.Equals(bitsOf(FPU_DIV_S(0.0f, 0.0f)), kPlusMax, "0/0 should be +max, not NaN");
            t.Equals(bitsOf(FPU_DIV_S(6.0f, 3.0f)), bitsOf(2.0f), "ordinary division should be unchanged");
            t.Equals(bitsOf(FPU_DIV_S(floatOf(kPlusMax), 0.5f)), kPlusMax, "overflowing quotient should saturate");
        });

        tc.Run("FPU RSQRT.S computes fs / sqrt(ft)", [](TestCase &t)
        {
            t.Equals(bitsOf(FPU_RSQRT_S(4.0f, 16.0f)), bitsOf(1.0f), "4 / sqrt(16) should be 1");
            t.Equals(bitsOf(FPU_RSQRT_S(1.0f, 4.0f)), bitsOf(0.5f), "1 / sqrt(4) should be 0.5");
            t.Equals(bitsOf(FPU_RSQRT_S(1.0f, -4.0f)), bitsOf(0.5f), "the sign of the radicand should be ignored");
            t.Equals(bitsOf(FPU_RSQRT_S(2.0f, 0.0f)), kPlusMax, "x / sqrt(0) should be +max");
            t.Equals(bitsOf(FPU_RSQRT_S(-2.0f, 0.0f)), kMinusMax, "-x / sqrt(0) should be -max");
        });

        tc.Run("FPU SQRT.S takes the square root of the magnitude", [](TestCase &t)
        {
            t.Equals(bitsOf(FPU_SQRT_S(16.0f)), bitsOf(4.0f), "sqrt(16) should be 4");
            t.Equals(bitsOf(FPU_SQRT_S(-16.0f)), bitsOf(4.0f), "sqrt(-16) should be 4, not NaN");
        });

        tc.Run("VU vector ADD/SUB/MUL clamp every lane", [](TestCase &t)
        {
            // _mm_set_ps lists lanes 3..0
            const __m128 a = _mm_set_ps(1.0f, std::numeric_limits<float>::infinity(), floatOf(kMinusMax), floatOf(kPlusMax));
            const __m128 b = _mm_set_ps(2.0f, 1.0f, floatOf(kMinusMax), floatOf(kPlusMax));

            const __m128 sum = PS2_VADD(a, b);
            t.Equals(laneBits(sum, 0), kPlusMax, "lane 0: max + max should saturate to +max");
            t.Equals(laneBits(sum, 1), kMinusMax, "lane 1: -max + -max should saturate to -max");
            t.Equals(laneBits(sum, 2), kPlusMax, "lane 2: +Inf + 1 should be +max");
            t.Equals(laneBits(sum, 3), bitsOf(3.0f), "lane 3: 1 + 2 should be 3");

            const __m128 diff = PS2_VSUB(a, b);
            t.Equals(laneBits(diff, 0), bitsOf(0.0f), "lane 0: max - max should be 0");
            t.Equals(laneBits(diff, 3), bitsOf(-1.0f), "lane 3: 1 - 2 should be -1");

            const __m128 prod = PS2_VMUL(a, b);
            t.Equals(laneBits(prod, 0), kPlusMax, "lane 0: max * max should saturate to +max");
            t.Equals(laneBits(prod, 1), kPlusMax, "lane 1: -max * -max should saturate to +max");
            t.Equals(laneBits(prod, 2), kPlusMax, "lane 2: +Inf * 1 should be +max");
            t.Equals(laneBits(prod, 3), bitsOf(2.0f), "lane 3: 1 * 2 should be 2");
        });

        tc.Run("DIV.S and RSQRT.S translate to the PS2 semantics helpers", [](TestCase &t)
        {
            CodeGenerator gen({}, {});

            // fd = f2, fs = f3, ft = f4 (all distinct so a wrong operand shows up)
            const std::string div = gen.translateInstruction(makeCop1S(COP1_S_DIV, 2, 3, 4));
            t.IsTrue(div.find("ctx->f[2] = FPU_DIV_S(ctx->f[3], ctx->f[4])") != std::string::npos,
                     "DIV.S should write fs / ft to fd but got: " + div);
            t.IsTrue(div.find("INFINITY") == std::string::npos, "DIV.S must not produce IEEE infinity but got: " + div);

            const std::string rsqrt = gen.translateInstruction(makeCop1S(COP1_S_RSQRT, 2, 3, 4));
            t.IsTrue(rsqrt.find("ctx->f[2] = FPU_RSQRT_S(ctx->f[3], ctx->f[4])") != std::string::npos,
                     "RSQRT.S should compute fs / sqrt(ft) but got: " + rsqrt);
        });
    });
}
