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

// PS2 floats have no NaN or Inf. Exponent 255 is the largest magnitude: 0x7FFFFFFF is what ps2_fdiv
// gives for x / 0 and what overflow saturates to. Exponent 0 is zero whatever the mantissa. The
// FPU's C.EQ/C.LT/C.LE, MIN.S and MAX.S and the VU's MAX/MINI order values that way (PCSX2 FPU.cpp
// fpuDouble, VUops.cpp fp_max/fp_min). Values are built from bit patterns at run time so the
// compiler cannot fold them.

using namespace ps2recomp;

namespace
{
    constexpr uint32_t kPlusMax = 0x7FFFFFFFu;
    constexpr uint32_t kMinusMax = 0xFFFFFFFFu;
    constexpr uint32_t kPlusE255 = 0x7F800001u;  // a smaller exponent-255 number
    constexpr uint32_t kDenormal = 0x00000001u;
    constexpr uint32_t kMinusDenormal = 0x80000001u;

    float f(uint32_t bits)
    {
        volatile uint32_t stored = bits;
        return std::bit_cast<float>(static_cast<uint32_t>(stored));
    }

    uint32_t b(float value)
    {
        return std::bit_cast<uint32_t>(value);
    }

    std::string hex(uint32_t value)
    {
        char text[12];
        std::snprintf(text, sizeof(text), "0x%08X", value);
        return text;
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

    __m128 lanes(uint32_t x, uint32_t y, uint32_t z, uint32_t w)
    {
        return _mm_castsi128_ps(_mm_set_epi32(static_cast<int>(w), static_cast<int>(z), static_cast<int>(y), static_cast<int>(x)));
    }

    void expectLanes(TestCase &t, __m128 value, uint32_t x, uint32_t y, uint32_t z, uint32_t w, const std::string &what)
    {
        alignas(16) uint32_t got[4];
        _mm_store_si128(reinterpret_cast<__m128i *>(got), _mm_castps_si128(value));
        t.IsTrue(got[0] == x && got[1] == y && got[2] == z && got[3] == w,
                 what + ": expected " + hex(x) + " " + hex(y) + " " + hex(z) + " " + hex(w) + ", got " + hex(got[0]) +
                     " " + hex(got[1]) + " " + hex(got[2]) + " " + hex(got[3]));
    }
}

void register_ps2_fpu_order_tests()
{
    MiniTest::Case("PS2FpuOrder", [](TestCase &tc)
    {
        // The R5900's C.LT.S and C.LE.S have the function codes MIPS gives C.OLT (0x34) and C.OLE
        // (0x36), so the title's 1673 c.lt.s and 234 c.le.s arrive as FPU_C_OLT_S and FPU_C_OLE_S.
        tc.Run("C.LT and C.LE treat exponent 255 as the largest numbers", [](TestCase &t)
        {
            t.IsTrue(FPU_C_OLT_S(f(0x00000000u), f(kPlusMax)), "0 < +max");
            t.IsTrue(!FPU_C_OLT_S(f(kPlusMax), f(b(1.0f))), "+max is not < 1");
            t.IsTrue(FPU_C_OLT_S(f(b(3.0e38f)), f(kPlusE255)), "3e38 < an exponent-255 number");
            t.IsTrue(FPU_C_OLT_S(f(kPlusE255), f(kPlusMax)), "exponent-255 numbers are ordered by mantissa");
            t.IsTrue(FPU_C_OLT_S(f(kMinusMax), f(b(-1.0f))), "-max < -1");
            t.IsTrue(FPU_C_OLE_S(f(kPlusMax), f(kPlusMax)), "+max <= +max");
            t.IsTrue(!FPU_C_OLE_S(f(kPlusMax), f(b(2.0f))), "+max is not <= 2");
        });

        tc.Run("C.EQ, C.LT and C.LE treat exponent 0 as zero", [](TestCase &t)
        {
            t.IsTrue(FPU_C_EQ_S(f(kDenormal), f(0x00000000u)), "a denormal equals 0");
            t.IsTrue(FPU_C_EQ_S(f(kMinusDenormal), f(0x80000000u)), "-denormal equals -0");
            t.IsTrue(FPU_C_EQ_S(f(0x80000000u), f(0x00000000u)), "-0 equals +0");
            t.IsTrue(!FPU_C_OLT_S(f(0x00000000u), f(kDenormal)), "0 is not < a denormal");
            t.IsTrue(FPU_C_OLE_S(f(kDenormal), f(0x80000000u)), "a denormal <= -0");
            t.IsTrue(FPU_C_EQ_S(f(kPlusMax), f(kPlusMax)), "+max equals itself");
        });

        tc.Run("MIN.S and MAX.S clamp a saturated value", [](TestCase &t)
        {
            t.Equals(b(FPU_MIN_S(f(kPlusMax), f(b(2.0f)))), b(2.0f), "min(+max, 2) = 2");
            t.Equals(b(FPU_MAX_S(f(b(1.0f)), f(kPlusMax))), kPlusMax, "max(1, +max) = +max");
            t.Equals(b(FPU_MAX_S(f(kMinusMax), f(b(-1.0f)))), b(-1.0f), "max(-max, -1) = -1");
            t.Equals(b(FPU_MIN_S(f(b(-1.0f)), f(kMinusMax))), kMinusMax, "min(-1, -max) = -max");
            t.Equals(b(FPU_MAX_S(f(kDenormal), f(b(-1.0f)))), 0x00000000u, "max(denormal, -1) = +0 (flushed)");
            t.Equals(b(FPU_MIN_S(f(0x00000000u), f(0x80000000u))), 0x00000000u, "a tie keeps the first operand");
        });

        tc.Run("Stuntman's launch clamp survives the car record's zero divisor", [](TestCase &t)
        {
            // sub_1A93C0, 0x001A94FC-0x001A9518 clamps x / c to [0, 1], where c is +0x2C of the car
            // record (0x0026C6D8), 0.0 for the first mission's car. x / 0 is +-max on the EE and the
            // clamp must turn it into 1 or 0. Host min/max passed the +-max through as a NaN, so the
            // clamp produced garbage that the driven-wheel code then integrated (speedometer 999).
            const float divisor = f(0x00000000u); // record +0x2C
            const float one = f(b(1.0f));         // $f5
            const float floor = f(0x00000000u);   // $f6
            const auto clamp = [&](float x)
            {
                return FPU_MAX_S(FPU_MIN_S(FPU_DIV_S(x, divisor), one), floor);
            };
            t.Equals(b(clamp(f(b(1.0f)))), b(1.0f), "a positive x / 0 clamps to 1");
            t.Equals(b(clamp(f(b(-19.0f)))), b(0.0f), "a negative x / 0 clamps to 0");
            // The title's words at 0x001A94FC, 0x001A9504 and 0x001A9508.
            const std::string div = translate(0x001A94FCu, 0x46090003u);
            t.IsTrue(contains(div, "ctx->f[0] = FPU_DIV_S(ctx->f[0], ctx->f[9])"), "div.s should use FPU_DIV_S but got: " + div);
            const std::string minCode = translate(0x001A9504u, 0x46050029u);
            t.IsTrue(contains(minCode, "ctx->f[0] = FPU_MIN_S(ctx->f[0], ctx->f[5])"), "min.s should use FPU_MIN_S but got: " + minCode);
            const std::string maxCode = translate(0x001A9508u, 0x46060028u);
            t.IsTrue(contains(maxCode, "ctx->f[0] = FPU_MAX_S(ctx->f[0], ctx->f[6])"), "max.s should use FPU_MAX_S but got: " + maxCode);
        });

        tc.Run("VU MAX and MINI order lanes as PS2 numbers", [](TestCase &t)
        {
            const __m128 a = lanes(b(-1.0f), b(-2.0f), kPlusMax, b(1.0f));
            const __m128 c = lanes(b(-2.0f), b(-1.0f), b(1.0f), kMinusMax);
            expectLanes(t, ps2_vu_max_ps(a, c), b(-1.0f), b(-1.0f), kPlusMax, b(1.0f), "MAX");
            // Operands swapped for MINI, so that neither SSE NaN rule (the second operand wins) can pass it.
            expectLanes(t, ps2_vu_min_ps(c, a), b(-2.0f), b(-2.0f), b(1.0f), kMinusMax, "MINI");
        });

        tc.Run("VU MAX and MINI emitters use the PS2 ordering", [](TestCase &t)
        {
            // vmax.xyzw $vf3, $vf4, $vf5 and vmini.xyzw $vf3, $vf4, $vf5
            const uint32_t vmax = (OPCODE_COP2 << 26) | (1u << 25) | (0xFu << 21) | (5u << 16) | (4u << 11) | (3u << 6) | 0x2Bu;
            const uint32_t vmini = (OPCODE_COP2 << 26) | (1u << 25) | (0xFu << 21) | (5u << 16) | (4u << 11) | (3u << 6) | 0x2Fu;
            const std::string maxCode = translate(0x00200000u, vmax);
            const std::string minCode = translate(0x00200000u, vmini);
            t.IsTrue(contains(maxCode, "ps2_vu_max_ps(ctx->vu0_vf[4], ctx->vu0_vf[5])") && !contains(maxCode, "_mm_max_ps"),
                     "vmax should use ps2_vu_max_ps but got: " + maxCode);
            t.IsTrue(contains(minCode, "ps2_vu_min_ps(ctx->vu0_vf[4], ctx->vu0_vf[5])") && !contains(minCode, "_mm_min_ps"),
                     "vmini should use ps2_vu_min_ps but got: " + minCode);
        });
    });
}
