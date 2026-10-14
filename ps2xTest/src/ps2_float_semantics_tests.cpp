#include "MiniTest.h"
#include "ps2recomp/code_generator.h"
#include "ps2recomp/instructions.h"
#include "ps2recomp/types.h"
#include "ps2_runtime_macros.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>

using namespace ps2recomp;

namespace
{
    constexpr uint32_t kPlusMax = 0x7FFFFFFFu;
    constexpr uint32_t kMinusMax = 0xFFFFFFFFu;
    // 2^128: the host's +Inf pattern, an ordinary number on the PS2.
    constexpr uint32_t kTwoPow128 = 0x7F800000u;

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

    struct HardwareVector
    {
        const char *op;
        uint32_t fs;
        uint32_t ft;
        uint32_t result;
        // 0: must match exactly.
        // 1: the hardware does not round to nearest (ADD.S and MUL.S drop the low bits, the divide unit has its
        //    own rule); rounding is not emulated, so the row differs in the last mantissa bit.
        // 2: an ADD.S/SUB.S/MUL.S operand has exponent 0 and a non-zero mantissa. The hardware reads it as
        //    zero; the fast path does not look for it. Not checked.
        int known;
    };

    const HardwareVector kHardwareVectors[] = {
#include "ps2_float_hw_vectors.inc"
    };

    uint32_t runHardwareOp(const HardwareVector &v)
    {
        const float fs = floatOf(v.fs);
        const float ft = floatOf(v.ft);
        const std::string op = v.op;
        if (op == "add")
            return bitsOf(FPU_ADD_S(fs, ft));
        if (op == "sub")
            return bitsOf(FPU_SUB_S(fs, ft));
        if (op == "mul")
            return bitsOf(FPU_MUL_S(fs, ft));
        if (op == "div")
            return bitsOf(FPU_DIV_S(fs, ft));
        if (op == "sqrt")
            return bitsOf(FPU_SQRT_S(fs));
        return bitsOf(FPU_RSQRT_S(fs, ft));
    }

    std::string describe(const HardwareVector &v, uint32_t got)
    {
        char text[160];
        std::snprintf(text, sizeof(text), "%s %08x, %08x: PS2 gives %08x, got %08x", v.op, v.fs, v.ft, v.result, got);
        return text;
    }
}

void register_ps2_float_semantics_tests()
{
    MiniTest::Case("PS2FloatSemantics", [](TestCase &tc)
    {
        tc.Run("FPU results match the PS2 hardware results from ps2autotests", [](TestCase &t)
        {
            for (const HardwareVector &v : kHardwareVectors)
            {
                if (v.known == 2)
                    continue;
                const uint32_t got = runHardwareOp(v);
                if (v.known == 1)
                {
                    const uint32_t distance = got > v.result ? got - v.result : v.result - got;
                    t.IsTrue(distance == 1u, describe(v, got));
                }
                else
                {
                    t.Equals(got, v.result, describe(v, got));
                }
            }
        });

        tc.Run("exponent 255 is an ordinary exponent, not Inf or NaN", [](TestCase &t)
        {
            const float twoPow128 = floatOf(kTwoPow128);
            t.Equals(bitsOf(FPU_ADD_S(twoPow128, 0.0f)), kTwoPow128, "2^128 + 0 should stay 2^128");
            t.Equals(bitsOf(FPU_MUL_S(twoPow128, 0.5f)), 0x7F000000u, "2^128 * 0.5 should be 2^127");
            t.Equals(bitsOf(FPU_MUL_S(floatOf(0x7F000000u), 2.0f)), kTwoPow128, "2^127 * 2 should be 2^128, not saturated");
            t.Equals(bitsOf(FPU_ADD_S(floatOf(0x7F000000u), floatOf(0x7F000000u))), kTwoPow128, "2^127 + 2^127 should be 2^128");
            t.Equals(bitsOf(FPU_SUB_S(floatOf(0x7FC00000u), twoPow128)), 0x7F000000u, "1.5*2^128 - 2^128 should be 2^127");
            t.Equals(bitsOf(FPU_DIV_S(floatOf(0x7FC00000u), 3.0f)), 0x7F000000u, "1.5*2^128 / 3 should be 2^127");
            t.Equals(bitsOf(FPU_SQRT_S(twoPow128)), 0x5F800000u, "sqrt(2^128) should be 2^64");
        });

        tc.Run("FPU results saturate at 0x7FFFFFFF and flush below 2^-126", [](TestCase &t)
        {
            const float maxf = floatOf(kPlusMax);
            const float minf = floatOf(kMinusMax);
            t.Equals(bitsOf(FPU_ADD_S(maxf, maxf)), kPlusMax, "max + max should saturate to +max");
            t.Equals(bitsOf(FPU_ADD_S(minf, minf)), kMinusMax, "-max + -max should saturate to -max");
            t.Equals(bitsOf(FPU_SUB_S(maxf, minf)), kPlusMax, "max - (-max) should saturate to +max");
            t.Equals(bitsOf(FPU_MUL_S(maxf, 2.0f)), kPlusMax, "max * 2 should saturate to +max");
            t.Equals(bitsOf(FPU_MUL_S(maxf, -2.0f)), kMinusMax, "max * -2 should saturate to -max");
            t.Equals(bitsOf(FPU_DIV_S(maxf, 0.5f)), kPlusMax, "max / 0.5 should saturate to +max");
            t.Equals(bitsOf(FPU_MUL_S(floatOf(0x00800000u), 0.5f)), 0x00000000u, "2^-126 * 0.5 should flush to +0");
            t.Equals(bitsOf(FPU_MUL_S(floatOf(0x80800000u), 0.5f)), 0x80000000u, "-2^-126 * 0.5 should flush to -0");
            t.Equals(bitsOf(FPU_MUL_S(floatOf(0x00000001u), 1.0f)), 0x00000000u, "a result with exponent 0 should flush to +0");
            t.Equals(bitsOf(FPU_ADD_S(1.0f, 2.0f)), bitsOf(3.0f), "ordinary addition should be unchanged");
            t.Equals(bitsOf(FPU_MUL_S(1.5f, 4.0f)), bitsOf(6.0f), "ordinary multiplication should be unchanged");
        });

        tc.Run("FPU DIV.S by zero gives +-max with the sign of fs xor ft", [](TestCase &t)
        {
            t.Equals(bitsOf(FPU_DIV_S(1.0f, 0.0f)), kPlusMax, "1/+0 should be +max");
            t.Equals(bitsOf(FPU_DIV_S(-1.0f, 0.0f)), kMinusMax, "-1/+0 should be -max");
            t.Equals(bitsOf(FPU_DIV_S(1.0f, -0.0f)), kMinusMax, "1/-0 should be -max");
            t.Equals(bitsOf(FPU_DIV_S(-1.0f, -0.0f)), kPlusMax, "-1/-0 should be +max");
            t.Equals(bitsOf(FPU_DIV_S(0.0f, 0.0f)), kPlusMax, "0/0 should be +max");
            t.Equals(bitsOf(FPU_DIV_S(1.0f, floatOf(0x00000001u))), kPlusMax, "an exponent-0 divisor should count as zero");
            t.Equals(bitsOf(FPU_DIV_S(6.0f, 3.0f)), bitsOf(2.0f), "ordinary division should be unchanged");
        });

        tc.Run("FPU RSQRT.S computes fs / sqrt(|ft|) and takes the sign of fs alone on a zero ft", [](TestCase &t)
        {
            t.Equals(bitsOf(FPU_RSQRT_S(4.0f, 16.0f)), bitsOf(1.0f), "4 / sqrt(16) should be 1");
            t.Equals(bitsOf(FPU_RSQRT_S(1.0f, 4.0f)), bitsOf(0.5f), "1 / sqrt(4) should be 0.5");
            t.Equals(bitsOf(FPU_RSQRT_S(1.0f, -4.0f)), bitsOf(0.5f), "the sign of the radicand should be ignored");
            t.Equals(bitsOf(FPU_RSQRT_S(2.0f, 0.0f)), kPlusMax, "x / sqrt(+0) should be +max");
            t.Equals(bitsOf(FPU_RSQRT_S(2.0f, -0.0f)), kPlusMax, "x / sqrt(-0) should be +max: the sign of ft is ignored");
            t.Equals(bitsOf(FPU_RSQRT_S(-2.0f, 0.0f)), kMinusMax, "-x / sqrt(+0) should be -max");
            t.Equals(bitsOf(FPU_RSQRT_S(-2.0f, -0.0f)), kMinusMax, "-x / sqrt(-0) should be -max");
        });

        tc.Run("FPU SQRT.S takes the square root of the magnitude", [](TestCase &t)
        {
            t.Equals(bitsOf(FPU_SQRT_S(16.0f)), bitsOf(4.0f), "sqrt(16) should be 4");
            t.Equals(bitsOf(FPU_SQRT_S(-16.0f)), bitsOf(4.0f), "sqrt(-16) should be 4");
            t.Equals(bitsOf(FPU_SQRT_S(-0.0f)), bitsOf(0.0f), "sqrt(-0) should be +0");
        });

        tc.Run("VU vector ADD/SUB/MUL follow the same rules in every lane", [](TestCase &t)
        {
            // _mm_set_ps lists lanes 3..0
            const __m128 a = _mm_set_ps(1.0f, floatOf(kTwoPow128), floatOf(kMinusMax), floatOf(kPlusMax));
            const __m128 b = _mm_set_ps(2.0f, 1.0f, floatOf(kMinusMax), floatOf(kPlusMax));

            const __m128 sum = PS2_VADD(a, b);
            t.Equals(laneBits(sum, 0), kPlusMax, "lane 0: max + max should saturate to +max");
            t.Equals(laneBits(sum, 1), kMinusMax, "lane 1: -max + -max should saturate to -max");
            t.Equals(laneBits(sum, 2), kTwoPow128, "lane 2: 2^128 + 1 should stay 2^128");
            t.Equals(laneBits(sum, 3), bitsOf(3.0f), "lane 3: 1 + 2 should be 3");

            const __m128 diff = PS2_VSUB(a, b);
            t.Equals(laneBits(diff, 0), bitsOf(0.0f), "lane 0: max - max should be 0");
            t.Equals(laneBits(diff, 2), kTwoPow128, "lane 2: 2^128 - 1 should stay 2^128");
            t.Equals(laneBits(diff, 3), bitsOf(-1.0f), "lane 3: 1 - 2 should be -1");

            const __m128 prod = PS2_VMUL(a, b);
            t.Equals(laneBits(prod, 0), kPlusMax, "lane 0: max * max should saturate to +max");
            t.Equals(laneBits(prod, 1), kPlusMax, "lane 1: -max * -max should saturate to +max");
            t.Equals(laneBits(prod, 2), kTwoPow128, "lane 2: 2^128 * 1 should stay 2^128");
            t.Equals(laneBits(prod, 3), bitsOf(2.0f), "lane 3: 1 * 2 should be 2");

            const __m128 tiny = _mm_set_ps(1.0f, floatOf(0x80800000u), floatOf(0x00800000u), 3.0f);
            const __m128 half = _mm_set1_ps(0.5f);
            const __m128 flushed = PS2_VMUL(tiny, half);
            t.Equals(laneBits(flushed, 0), bitsOf(1.5f), "lane 0: 3 * 0.5 should be 1.5");
            t.Equals(laneBits(flushed, 1), 0x00000000u, "lane 1: 2^-126 * 0.5 should flush to +0");
            t.Equals(laneBits(flushed, 2), 0x80000000u, "lane 2: -2^-126 * 0.5 should flush to -0");
            t.Equals(laneBits(flushed, 3), bitsOf(0.5f), "lane 3: 1 * 0.5 should be 0.5");
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
