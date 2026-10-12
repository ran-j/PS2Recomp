#include "MiniTest.h"

#include <cfenv>
#include <cstdint>
#include <cstring>

#include "ps2_runtime_macros.h"

// R5900 CVT.W.S reference behavior (matches the PCSX2 interpreter's CVT_W):
// truncate toward zero, saturate to 0x7FFFFFFF / 0x80000000 on overflow.
// The EE FPU ignores the host/guest rounding mode for this conversion.

void register_ps2_fpu_macros_tests()
{
    MiniTest::Case("FpuMacros", [](TestCase &tc)
                   {
    tc.Run("CVT_W_S truncates toward zero", [](TestCase &t) {
        t.Equals((uint32_t)FPU_CVT_W_S(2.7f), 2u, "2.7 must truncate to 2");
        t.Equals((uint32_t)FPU_CVT_W_S(3.5f), 3u, "3.5 must truncate to 3 (not round to 4)");
        t.Equals((uint32_t)FPU_CVT_W_S(0.99f), 0u, "0.99 must truncate to 0");
        t.Equals((uint32_t)FPU_CVT_W_S(-2.7f), 0xFFFFFFFEu, "-2.7 must truncate to -2");
    });

    tc.Run("CVT_W_S ignores host rounding mode", [](TestCase &t) {
        const int previous = std::fegetround();
        std::fesetround(FE_TONEAREST);
        t.Equals((uint32_t)FPU_CVT_W_S(2.7f), 2u, "must truncate even under FE_TONEAREST");
        std::fesetround(FE_UPWARD);
        t.Equals((uint32_t)FPU_CVT_W_S(2.1f), 2u, "must truncate even under FE_UPWARD");
        std::fesetround(previous);
    });

    tc.Run("CVT_W_S saturates on overflow", [](TestCase &t) {
        t.Equals((uint32_t)FPU_CVT_W_S(3e9f), 0x7FFFFFFFu, "positive overflow -> 0x7FFFFFFF");
        t.Equals((uint32_t)FPU_CVT_W_S(-3e9f), 0x80000000u, "negative overflow -> 0x80000000");
        t.Equals((uint32_t)FPU_CVT_W_S(2147483648.0f), 0x7FFFFFFFu, "2^31 -> 0x7FFFFFFF");
        t.Equals((uint32_t)FPU_CVT_W_S(-2147483648.0f), 0x80000000u, "-2^31 -> 0x80000000");
        t.Equals((uint32_t)FPU_CVT_W_S(2147483520.0f), 0x7FFFFF80u,
                 "largest float below 2^31 converts exactly");
    }); });
}
