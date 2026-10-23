#include "MiniTest.h"
#include "ps2recomp/code_generator.h"
#include "ps2recomp/instructions.h"
#include "ps2recomp/r5900_decoder.h"
#include "ps2recomp/types.h"
#include "ps2_runtime.h"

#include <cstdint>
#include <cstdio>
#include <string>

// VF0 is hardwired to (0, 0, 0, 1) on the VU. Two paths broke that: every fresh R5900Context
// started with vf0 = 0 (so thread/interrupt/callback contexts had a zero w lane until something
// wrote it), and LQC2 into vf0 stored the loaded quadword into it.

using namespace ps2recomp;

namespace
{
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

void register_ps2_vu0_vf0_tests()
{
    MiniTest::Case("PS2Vu0Vf0", [](TestCase &tc)
    {
        tc.Run("a fresh guest context has vf0 = (0, 0, 0, 1)", [](TestCase &t)
        {
            // Thread and interrupt contexts start from R5900Context().
            const R5900Context fresh{};
            alignas(16) uint32_t vf0[4];
            _mm_store_si128(reinterpret_cast<__m128i *>(vf0), _mm_castps_si128(fresh.vu0_vf[0]));
            char got[64];
            std::snprintf(got, sizeof(got), "%08X %08X %08X %08X", vf0[0], vf0[1], vf0[2], vf0[3]);
            t.IsTrue(vf0[0] == 0u && vf0[1] == 0u && vf0[2] == 0u && vf0[3] == 0x3F800000u,
                     std::string("vf0 should be (0, 0, 0, 1.0), got ") + got);
        });

        tc.Run("LQC2 into vf0 changes nothing", [](TestCase &t)
        {
            // Stuntman (SLUS-20250) 0x002428DC (sub_2428B8 restores a VU0 context): lqc2 $vf0, 0($s0)
            const std::string vf0 = translate(0x002428DCu, 0xDA000000u);
            t.IsTrue(!contains(vf0, "vu0_vf[0]"), "lqc2 $vf0 must not write vf0 but got: " + vf0);
            t.IsTrue(contains(vf0, "(void)READ128(ADD32(GPR_U32(ctx, 16), 0))"), "lqc2 $vf0 must still perform the read but got: " + vf0);
            // Stuntman 0x002428E0: lqc2 $vf1, 0x10($s0)
            const std::string vf1 = translate(0x002428E0u, 0xDA010010u);
            t.IsTrue(contains(vf1, "ctx->vu0_vf[1] = _mm_castsi128_ps(READ128(ADD32(GPR_U32(ctx, 16), 16)))"),
                     "lqc2 $vf1 must load vf1 but got: " + vf1);
        });
    });
}
