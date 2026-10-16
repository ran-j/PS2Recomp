#include "MiniTest.h"
#include "ps2recomp/code_generator.h"
#include "ps2recomp/instructions.h"
#include "ps2recomp/types.h"
#include "ps2_runtime_macros.h"

#include <cstdint>
#include <string>

using namespace ps2recomp;

namespace
{
    __m128i vec4(uint32_t w0, uint32_t w1, uint32_t w2, uint32_t w3)
    {
        return _mm_set_epi32(static_cast<int32_t>(w3), static_cast<int32_t>(w2), static_cast<int32_t>(w1), static_cast<int32_t>(w0));
    }

    void setReg(R5900Context &ctx, int reg, uint32_t w0, uint32_t w1, uint32_t w2, uint32_t w3)
    {
        ctx.r[reg] = vec4(w0, w1, w2, w3);
    }

    uint64_t regLo64(const R5900Context &ctx, int reg)
    {
        return static_cast<uint64_t>(_mm_cvtsi128_si64(ctx.r[reg]));
    }

    uint64_t regHi64(const R5900Context &ctx, int reg)
    {
        return static_cast<uint64_t>(_mm_extract_epi64(ctx.r[reg], 1));
    }

    // Distinct value in every 32-bit word of HI/LO so lane mix-ups show.
    void presetHiLo(R5900Context &ctx)
    {
        ctx.hi = 0x1122334455667788ULL;
        ctx.lo = 0x99AABBCCDDEEFF00ULL;
        ctx.hi1 = 0x0123456789ABCDEFULL;
        ctx.lo1 = 0xFEDCBA9876543210ULL;
    }

    void checkHiLo(TestCase &t, const R5900Context &ctx, uint64_t hi, uint64_t lo, uint64_t hi1, uint64_t lo1, const char *name)
    {
        t.Equals(ctx.hi, hi, std::string(name) + " hi");
        t.Equals(ctx.lo, lo, std::string(name) + " lo");
        t.Equals(ctx.hi1, hi1, std::string(name) + " hi1");
        t.Equals(ctx.lo1, lo1, std::string(name) + " lo1");
    }

    void checkRd(TestCase &t, const R5900Context &ctx, int rd, uint64_t lo64, uint64_t hi64, const char *name)
    {
        t.Equals(regLo64(ctx, rd), lo64, std::string(name) + " rd low64");
        t.Equals(regHi64(ctx, rd), hi64, std::string(name) + " rd high64");
    }

    void setSentinelRd(R5900Context &ctx, int rd)
    {
        setReg(ctx, rd, 0xDEADBEEFu, 0xCAFEBABEu, 0x0BADF00Du, 0xFEEDFACEu);
    }

    void checkSentinelRd(TestCase &t, const R5900Context &ctx, int rd, const char *name)
    {
        checkRd(t, ctx, rd, 0xCAFEBABEDEADBEEFULL, 0xFEEDFACE0BADF00DULL, name);
    }

    Instruction makeMmi(uint32_t function, uint32_t sa, uint32_t rd, uint32_t rs, uint32_t rt)
    {
        Instruction inst{};
        inst.isMMI = true;
        inst.opcode = OPCODE_MMI;
        inst.function = function;
        inst.sa = sa;
        inst.rd = rd;
        inst.rs = rs;
        inst.rt = rt;
        return inst;
    }

    void checkEmit(TestCase &t, CodeGenerator &gen, const Instruction &inst, const std::string &expected, const char *name)
    {
        const std::string out = gen.translateInstruction(inst);
        t.IsTrue(out.find(expected) != std::string::npos, std::string(name) + " should emit " + expected + ", got: " + out);
    }

    // Halfword test vectors: rs halfwords [3,2,5,-2,8,7,9,-5], rt [6,4,-3,10,12,11,13,-10],
    // products p = [18,8,-15,-20,96,77,117,50].
    void setHalfwordOperands(R5900Context &ctx)
    {
        setReg(ctx, 4, 0x00020003u, 0xFFFE0005u, 0x00070008u, 0xFFFB0009u);
        setReg(ctx, 5, 0x00040006u, 0x000AFFFDu, 0x000B000Cu, 0xFFF6000Du);
    }
}

void register_ps2_mmi_hilo_tests()
{
    MiniTest::Case("PS2MmiHiLo", [](TestCase &tc)
    {
        // ---- PMFHI / PMFLO ----
        tc.Run("PMFHI known answer", [](TestCase &t)
        {
            // Catches old emitter: SET_GPR_U64(ctx, rd, ctx->hi) dropped hi1 (upper 64 of rd).
            R5900Context ctx;
            presetHiLo(ctx);
            ps2_mmi_pmfhi(&ctx, 3);
            checkRd(t, ctx, 3, 0x1122334455667788ULL, 0x0123456789ABCDEFULL, "PMFHI");
            checkHiLo(t, ctx, 0x1122334455667788ULL, 0x99AABBCCDDEEFF00ULL, 0x0123456789ABCDEFULL, 0xFEDCBA9876543210ULL, "PMFHI");
        });
        tc.Run("PMFHI emitter", [](TestCase &t)
        {
            CodeGenerator gen({}, {});
            checkEmit(t, gen, makeMmi(MMI_MMI2, MMI2_PMFHI, 3, 4, 5), "ps2_mmi_pmfhi(ctx, 3)", "PMFHI");
        });
        tc.Run("PMFLO known answer", [](TestCase &t)
        {
            // Catches old emitter: SET_GPR_U64(ctx, rd, ctx->lo) dropped lo1 (upper 64 of rd).
            R5900Context ctx;
            presetHiLo(ctx);
            ps2_mmi_pmflo(&ctx, 3);
            checkRd(t, ctx, 3, 0x99AABBCCDDEEFF00ULL, 0xFEDCBA9876543210ULL, "PMFLO");
            checkHiLo(t, ctx, 0x1122334455667788ULL, 0x99AABBCCDDEEFF00ULL, 0x0123456789ABCDEFULL, 0xFEDCBA9876543210ULL, "PMFLO");
        });
        tc.Run("PMFLO emitter", [](TestCase &t)
        {
            CodeGenerator gen({}, {});
            checkEmit(t, gen, makeMmi(MMI_MMI2, MMI2_PMFLO, 3, 4, 5), "ps2_mmi_pmflo(ctx, 3)", "PMFLO");
        });

        // ---- PMTHI / PMTLO ----
        tc.Run("PMTHI known answer", [](TestCase &t)
        {
            // Catches old emitter: ctx->hi = GPR_U32(rs) kept only 32 bits and never wrote hi1.
            R5900Context ctx;
            presetHiLo(ctx);
            setReg(ctx, 4, 0x11111111u, 0x22222222u, 0x33333333u, 0x44444444u);
            ps2_mmi_pmthi(&ctx, ctx.r[4]);
            checkHiLo(t, ctx, 0x2222222211111111ULL, 0x99AABBCCDDEEFF00ULL, 0x4444444433333333ULL, 0xFEDCBA9876543210ULL, "PMTHI");
        });
        tc.Run("PMTHI emitter", [](TestCase &t)
        {
            CodeGenerator gen({}, {});
            checkEmit(t, gen, makeMmi(MMI_MMI3, MMI3_PMTHI, 3, 4, 5), "ps2_mmi_pmthi(ctx, GPR_VEC(ctx, 4))", "PMTHI");
        });
        tc.Run("PMTLO known answer", [](TestCase &t)
        {
            // Catches old emitter: ctx->lo = GPR_U32(rs) kept only 32 bits and never wrote lo1.
            R5900Context ctx;
            presetHiLo(ctx);
            setReg(ctx, 4, 0x11111111u, 0x22222222u, 0x33333333u, 0x44444444u);
            ps2_mmi_pmtlo(&ctx, ctx.r[4]);
            checkHiLo(t, ctx, 0x1122334455667788ULL, 0x2222222211111111ULL, 0x0123456789ABCDEFULL, 0x4444444433333333ULL, "PMTLO");
        });
        tc.Run("PMTLO emitter", [](TestCase &t)
        {
            CodeGenerator gen({}, {});
            checkEmit(t, gen, makeMmi(MMI_MMI3, MMI3_PMTLO, 3, 4, 5), "ps2_mmi_pmtlo(ctx, GPR_VEC(ctx, 4))", "PMTLO");
        });

        // ---- PMFHL forms ----
        tc.Run("PMFHL LW known answer", [](TestCase &t)
        {
            // Catches old PS2_PMFHL_LW macro: replicated 64-bit hi/lo, ignoring hi1/lo1.
            R5900Context ctx;
            presetHiLo(ctx);
            ps2_mmi_pmfhl(&ctx, 3, PMFHL_LW);
            checkRd(t, ctx, 3, 0x55667788DDEEFF00ULL, 0x89ABCDEF76543210ULL, "PMFHL LW");
        });
        tc.Run("PMFHL UW known answer", [](TestCase &t)
        {
            // Catches old PS2_PMFHL_UW macro: replicated 64-bit hi/lo, ignoring hi1/lo1.
            R5900Context ctx;
            presetHiLo(ctx);
            ps2_mmi_pmfhl(&ctx, 3, PMFHL_UW);
            checkRd(t, ctx, 3, 0x1122334499AABBCCULL, 0x01234567FEDCBA98ULL, "PMFHL UW");
        });
        tc.Run("PMFHL SLW known answer", [](TestCase &t)
        {
            // Catches old PS2_PMFHL_SLW macro: _mm_packs_epi32 saturation is not PCSX2's pair clamp.
            R5900Context ctx;
            presetHiLo(ctx);
            ps2_mmi_pmfhl(&ctx, 3, PMFHL_SLW);
            checkRd(t, ctx, 3, 0x000000007FFFFFFFULL, 0xFFFFFFFF80000000ULL, "PMFHL SLW");
        });
        tc.Run("PMFHL LH known answer", [](TestCase &t)
        {
            // Catches old PS2_PMFHL_LH macro: packs/shuffle of 64-bit hi/lo only.
            R5900Context ctx;
            presetHiLo(ctx);
            ps2_mmi_pmfhl(&ctx, 3, PMFHL_LH);
            checkRd(t, ctx, 3, 0x33447788BBCCFF00ULL, 0x4567CDEFBA983210ULL, "PMFHL LH");
        });
        tc.Run("PMFHL SH known answer", [](TestCase &t)
        {
            // Catches old PS2_PMFHL_SH macro: no per-word int16 clamp of all eight words.
            R5900Context ctx;
            ctx.lo = 0xFFFF800000001234ULL;
            ctx.hi = 0xFFFF7FFF0000FFFFULL;
            ctx.lo1 = 0xFFFFFFFE00008000ULL;
            ctx.hi1 = 0xFFFFFFFF00000001ULL;
            ps2_mmi_pmfhl(&ctx, 3, PMFHL_SH);
            checkRd(t, ctx, 3, 0x80007FFF80001234ULL, 0xFFFF0001FFFE7FFFULL, "PMFHL SH");
        });
        tc.Run("PMFHL emitter", [](TestCase &t)
        {
            CodeGenerator gen({}, {});
            const uint32_t forms[5] = {PMFHL_LW, PMFHL_UW, PMFHL_SLW, PMFHL_LH, PMFHL_SH};
            for (uint32_t form : forms)
            {
                checkEmit(t, gen, makeMmi(MMI_PMFHL, form, 3, 4, 5),
                          "ps2_mmi_pmfhl(ctx, 3, " + std::to_string(form) + ")", "PMFHL");
            }
        });

        // ---- PMTHL ----
        tc.Run("PMTHL LW known answer", [](TestCase &t)
        {
            // Catches old emitter: zero-extended word0/word1 into lo/hi and never wrote lo1/hi1
            // (PCSX2 preserves the upper 32 bits of each 64-bit half).
            R5900Context ctx;
            presetHiLo(ctx);
            setReg(ctx, 4, 0xA0A0A0A0u, 0xB1B1B1B1u, 0xC2C2C2C2u, 0xD3D3D3D3u);
            ps2_mmi_pmthl(&ctx, ctx.r[4], PMFHL_LW);
            checkHiLo(t, ctx, 0x11223344B1B1B1B1ULL, 0x99AABBCCA0A0A0A0ULL, 0x01234567D3D3D3D3ULL, 0xFEDCBA98C2C2C2C2ULL, "PMTHL LW");
        });
        tc.Run("PMTHL nonzero sa is a no-op", [](TestCase &t)
        {
            // Catches old emitter: returned an unhandled-instruction stub for sa != LW.
            R5900Context ctx;
            presetHiLo(ctx);
            setReg(ctx, 4, 0xA0A0A0A0u, 0xB1B1B1B1u, 0xC2C2C2C2u, 0xD3D3D3D3u);
            ps2_mmi_pmthl(&ctx, ctx.r[4], PMFHL_UW);
            checkHiLo(t, ctx, 0x1122334455667788ULL, 0x99AABBCCDDEEFF00ULL, 0x0123456789ABCDEFULL, 0xFEDCBA9876543210ULL, "PMTHL UW noop");
        });
        tc.Run("PMTHL emitter", [](TestCase &t)
        {
            CodeGenerator gen({}, {});
            checkEmit(t, gen, makeMmi(MMI_PMTHL, PMFHL_LW, 3, 4, 5), "ps2_mmi_pmthl(ctx, GPR_VEC(ctx, 4), 0)", "PMTHL");
        });

        // ---- PMULTW ----
        tc.Run("PMULTW known answer", [](TestCase &t)
        {
            // Catches old emitter: unsigned multiply, both lanes summed into one 64-bit acc.
            R5900Context ctx;
            presetHiLo(ctx);
            setReg(ctx, 4, 0x00020000u, 0x11111111u, 0x00010000u, 0x22222222u);
            setReg(ctx, 5, 0xFFFFFFFDu, 0x33333333u, 0x00010000u, 0x44444444u);
            ps2_mmi_pmultw(&ctx, 3, ctx.r[4], ctx.r[5]);
            checkHiLo(t, ctx, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFA0000ULL, 0x0000000000000001ULL, 0x0000000000000000ULL, "PMULTW");
            checkRd(t, ctx, 3, 0xFFFFFFFFFFFA0000ULL, 0x0000000100000000ULL, "PMULTW");
        });
        tc.Run("PMULTW emitter", [](TestCase &t)
        {
            CodeGenerator gen({}, {});
            checkEmit(t, gen, makeMmi(MMI_MMI2, MMI2_PMULTW, 3, 4, 5), "ps2_mmi_pmultw(ctx, 3, GPR_VEC(ctx, 4), GPR_VEC(ctx, 5))", "PMULTW");
        });
        tc.Run("PMULTW rd zero keeps r0 zero", [](TestCase &t)
        {
            // Catches old emitter: HI/LO collapsed to one acc even though r0 write was suppressed.
            R5900Context ctx;
            presetHiLo(ctx);
            setReg(ctx, 4, 0x00020000u, 0x11111111u, 0x00010000u, 0x22222222u);
            setReg(ctx, 5, 0xFFFFFFFDu, 0x33333333u, 0x00010000u, 0x44444444u);
            ps2_mmi_pmultw(&ctx, 0, ctx.r[4], ctx.r[5]);
            checkHiLo(t, ctx, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFA0000ULL, 0x0000000000000001ULL, 0x0000000000000000ULL, "PMULTW rd0");
            checkRd(t, ctx, 0, 0ULL, 0ULL, "PMULTW rd0");
        });

        // ---- PMADDW ----
        tc.Run("PMADDW known answer", [](TestCase &t)
        {
            // Catches old emitter: unsigned products summed into one acc, no voodoo, no /4294967295.
            // Lane 0 exercises the voodoo: without +0x70000000 the high word would be 0, not 1.
            R5900Context ctx;
            ctx.hi = 0x1122334400000000ULL;
            ctx.lo = 0x99AABBCC00000100ULL;
            ctx.hi1 = 0x5566778800000002ULL;
            ctx.lo1 = 0xDDEEFF0000000200ULL;
            setReg(ctx, 4, 0x00000002u, 0x11111111u, 0x00000004u, 0x22222222u);
            setReg(ctx, 5, 0x7FFFFFFFu, 0x33333333u, 0x00000006u, 0x44444444u);
            ps2_mmi_pmaddw(&ctx, 3, ctx.r[4], ctx.r[5]);
            checkHiLo(t, ctx, 0x0000000000000001ULL, 0x00000000000000FEULL, 0x0000000000000002ULL, 0x0000000000000218ULL, "PMADDW");
            checkRd(t, ctx, 3, 0x00000001000000FEULL, 0x0000000200000218ULL, "PMADDW");
        });
        tc.Run("PMADDW emitter", [](TestCase &t)
        {
            CodeGenerator gen({}, {});
            checkEmit(t, gen, makeMmi(MMI_MMI2, MMI2_PMADDW, 3, 4, 5), "ps2_mmi_pmaddw(ctx, 3, GPR_VEC(ctx, 4), GPR_VEC(ctx, 5))", "PMADDW");
        });

        // ---- PMSUBW ----
        tc.Run("PMSUBW known answer", [](TestCase &t)
        {
            // Catches old emitter: unsigned products subtracted from one acc, no /4294967295.
            R5900Context ctx;
            ctx.hi = 0x1122334400000002ULL;
            ctx.lo = 0x99AABBCC00000500ULL;
            ctx.hi1 = 0x5566778800000000ULL;
            ctx.lo1 = 0xDDEEFF0000000900ULL;
            setReg(ctx, 4, 0x0000000Au, 0x11111111u, 0x00000014u, 0x22222222u);
            setReg(ctx, 5, 0x00000003u, 0x33333333u, 0x00000004u, 0x44444444u);
            ps2_mmi_pmsubw(&ctx, 3, ctx.r[4], ctx.r[5]);
            checkHiLo(t, ctx, 0x0000000000000001ULL, 0x00000000000004E2ULL, 0x0000000000000000ULL, 0x00000000000008B0ULL, "PMSUBW");
            checkRd(t, ctx, 3, 0x00000001000004E2ULL, 0x00000000000008B0ULL, "PMSUBW");
        });
        tc.Run("PMSUBW emitter", [](TestCase &t)
        {
            CodeGenerator gen({}, {});
            checkEmit(t, gen, makeMmi(MMI_MMI2, MMI2_PMSUBW, 3, 4, 5), "ps2_mmi_pmsubw(ctx, 3, GPR_VEC(ctx, 4), GPR_VEC(ctx, 5))", "PMSUBW");
        });

        // ---- PMADDUW ----
        tc.Run("PMADDUW known answer", [](TestCase &t)
        {
            // Catches old emitter: lanes summed into one acc instead of per-pipeline 64-bit tempu.
            R5900Context ctx;
            ctx.hi = 0x1122334400000001ULL;
            ctx.lo = 0x99AABBCC00000010ULL;
            ctx.hi1 = 0x5566778800000000ULL;
            ctx.lo1 = 0xDDEEFF0000000020ULL;
            setReg(ctx, 4, 0xFFFFFFFFu, 0x11111111u, 0x00000003u, 0x22222222u);
            setReg(ctx, 5, 0x00000002u, 0x33333333u, 0x80000000u, 0x44444444u);
            ps2_mmi_pmadduw(&ctx, 3, ctx.r[4], ctx.r[5]);
            checkHiLo(t, ctx, 0x0000000000000003ULL, 0x000000000000000EULL, 0x0000000000000001ULL, 0xFFFFFFFF80000020ULL, "PMADDUW");
            checkRd(t, ctx, 3, 0x000000030000000EULL, 0x0000000180000020ULL, "PMADDUW");
        });
        tc.Run("PMADDUW emitter", [](TestCase &t)
        {
            CodeGenerator gen({}, {});
            checkEmit(t, gen, makeMmi(MMI_MMI3, MMI3_PMADDUW, 3, 4, 5), "ps2_mmi_pmadduw(ctx, 3, GPR_VEC(ctx, 4), GPR_VEC(ctx, 5))", "PMADDUW");
        });

        // ---- PDIVW ----
        tc.Run("PDIVW known answer", [](TestCase &t)
        {
            // Catches old emitter: only lane 0 divided and rd was overwritten with the quotient.
            R5900Context ctx;
            presetHiLo(ctx);
            setSentinelRd(ctx, 3);
            setReg(ctx, 4, 0x00000064u, 0x11111111u, 0xFFFFFFFBu, 0x22222222u);
            setReg(ctx, 5, 0x00000007u, 0x33333333u, 0x00000000u, 0x44444444u);
            ps2_mmi_pdivw(&ctx, 3, ctx.r[4], ctx.r[5]);
            checkHiLo(t, ctx, 0x0000000000000002ULL, 0x000000000000000EULL, 0xFFFFFFFFFFFFFFFBULL, 0x0000000000000001ULL, "PDIVW");
            checkSentinelRd(t, ctx, 3, "PDIVW");
        });
        tc.Run("PDIVW overflow known answer", [](TestCase &t)
        {
            // Catches old emitter: 0x80000000 / -1 handled only for lane 0, rd overwritten.
            R5900Context ctx;
            presetHiLo(ctx);
            setSentinelRd(ctx, 3);
            setReg(ctx, 4, 0x80000000u, 0x11111111u, 0x80000000u, 0x22222222u);
            setReg(ctx, 5, 0xFFFFFFFFu, 0x33333333u, 0xFFFFFFFFu, 0x44444444u);
            ps2_mmi_pdivw(&ctx, 3, ctx.r[4], ctx.r[5]);
            checkHiLo(t, ctx, 0x0000000000000000ULL, 0xFFFFFFFF80000000ULL, 0x0000000000000000ULL, 0xFFFFFFFF80000000ULL, "PDIVW ovf");
            checkSentinelRd(t, ctx, 3, "PDIVW ovf");
        });
        tc.Run("PDIVW emitter", [](TestCase &t)
        {
            CodeGenerator gen({}, {});
            checkEmit(t, gen, makeMmi(MMI_MMI2, MMI2_PDIVW, 3, 4, 5), "ps2_mmi_pdivw(ctx, 3, GPR_VEC(ctx, 4), GPR_VEC(ctx, 5))", "PDIVW");
        });

        // ---- PMULTH ----
        tc.Run("PMULTH known answer", [](TestCase &t)
        {
            // Catches old emitter: _mm_madd_epi16 pair sums collapsed into one scalar acc.
            R5900Context ctx;
            presetHiLo(ctx);
            setHalfwordOperands(ctx);
            ps2_mmi_pmulth(&ctx, 3, ctx.r[4], ctx.r[5]);
            checkHiLo(t, ctx, 0xFFFFFFECFFFFFFF1ULL, 0x0000000800000012ULL, 0x0000003200000075ULL, 0x0000004D00000060ULL, "PMULTH");
            checkRd(t, ctx, 3, 0xFFFFFFF100000012ULL, 0x0000007500000060ULL, "PMULTH");
        });
        tc.Run("PMULTH emitter", [](TestCase &t)
        {
            CodeGenerator gen({}, {});
            checkEmit(t, gen, makeMmi(MMI_MMI2, MMI2_PMULTH, 3, 4, 5), "ps2_mmi_pmulth(ctx, 3, GPR_VEC(ctx, 4), GPR_VEC(ctx, 5))", "PMULTH");
        });

        // ---- PMADDH ----
        tc.Run("PMADDH known answer", [](TestCase &t)
        {
            // Catches old emitter: pair sums accumulated into one scalar acc, upper words lost.
            R5900Context ctx;
            ctx.lo = 0x0000020000000100ULL;
            ctx.hi = 0x0000060000000500ULL;
            ctx.lo1 = 0x0000040000000300ULL;
            ctx.hi1 = 0x0000080000000700ULL;
            setHalfwordOperands(ctx);
            ps2_mmi_pmaddh(&ctx, 3, ctx.r[4], ctx.r[5]);
            checkHiLo(t, ctx, 0x000005EC000004F1ULL, 0x0000020800000112ULL, 0x0000083200000775ULL, 0x0000044D00000360ULL, "PMADDH");
            checkRd(t, ctx, 3, 0x000004F100000112ULL, 0x0000077500000360ULL, "PMADDH");
        });
        tc.Run("PMADDH emitter", [](TestCase &t)
        {
            CodeGenerator gen({}, {});
            checkEmit(t, gen, makeMmi(MMI_MMI2, MMI2_PMADDH, 3, 4, 5), "ps2_mmi_pmaddh(ctx, 3, GPR_VEC(ctx, 4), GPR_VEC(ctx, 5))", "PMADDH");
        });

        // ---- PMSUBH ----
        tc.Run("PMSUBH known answer", [](TestCase &t)
        {
            // Catches old emitter: pair sums subtracted from one scalar acc, upper words lost.
            R5900Context ctx;
            ctx.lo = 0x0000020000000100ULL;
            ctx.hi = 0x0000060000000500ULL;
            ctx.lo1 = 0x0000040000000300ULL;
            ctx.hi1 = 0x0000080000000700ULL;
            setHalfwordOperands(ctx);
            ps2_mmi_pmsubh(&ctx, 3, ctx.r[4], ctx.r[5]);
            checkHiLo(t, ctx, 0x000006140000050FULL, 0x000001F8000000EEULL, 0x000007CE0000068BULL, 0x000003B3000002A0ULL, "PMSUBH");
            checkRd(t, ctx, 3, 0x0000050F000000EEULL, 0x0000068B000002A0ULL, "PMSUBH");
        });
        tc.Run("PMSUBH emitter", [](TestCase &t)
        {
            CodeGenerator gen({}, {});
            checkEmit(t, gen, makeMmi(MMI_MMI2, MMI2_PMSUBH, 3, 4, 5), "ps2_mmi_pmsubh(ctx, 3, GPR_VEC(ctx, 4), GPR_VEC(ctx, 5))", "PMSUBH");
        });

        // ---- PHMADH ----
        tc.Run("PHMADH known answer", [](TestCase &t)
        {
            // Catches old emitter: horizontal sums accumulated into one scalar acc.
            R5900Context ctx;
            presetHiLo(ctx);
            setHalfwordOperands(ctx);
            ps2_mmi_phmadh(&ctx, 3, ctx.r[4], ctx.r[5]);
            checkHiLo(t, ctx, 0xFFFFFFECFFFFFFDDULL, 0x000000080000001AULL, 0x00000032000000A7ULL, 0x0000004D000000ADULL, "PHMADH");
            checkRd(t, ctx, 3, 0xFFFFFFDD0000001AULL, 0x000000A7000000ADULL, "PHMADH");
        });
        tc.Run("PHMADH emitter", [](TestCase &t)
        {
            CodeGenerator gen({}, {});
            checkEmit(t, gen, makeMmi(MMI_MMI2, MMI2_PHMADH, 3, 4, 5), "ps2_mmi_phmadh(ctx, 3, GPR_VEC(ctx, 4), GPR_VEC(ctx, 5))", "PHMADH");
        });

        // ---- PHMSBH ----
        tc.Run("PHMSBH known answer", [](TestCase &t)
        {
            // Catches old emitter: no ~firsttemp upper words, results summed into one acc.
            R5900Context ctx;
            presetHiLo(ctx);
            setHalfwordOperands(ctx);
            ps2_mmi_phmsbh(&ctx, 3, ctx.r[4], ctx.r[5]);
            checkHiLo(t, ctx, 0x00000013FFFFFFFBULL, 0xFFFFFFF7FFFFFFF6ULL, 0xFFFFFFCDFFFFFFBDULL, 0xFFFFFFB2FFFFFFEDULL, "PHMSBH");
            checkRd(t, ctx, 3, 0xFFFFFFFBFFFFFFF6ULL, 0xFFFFFFBDFFFFFFEDULL, "PHMSBH");
        });
        tc.Run("PHMSBH emitter", [](TestCase &t)
        {
            CodeGenerator gen({}, {});
            checkEmit(t, gen, makeMmi(MMI_MMI2, MMI2_PHMSBH, 3, 4, 5), "ps2_mmi_phmsbh(ctx, 3, GPR_VEC(ctx, 4), GPR_VEC(ctx, 5))", "PHMSBH");
        });

        // ---- PDIVBW ----
        tc.Run("PDIVBW known answer", [](TestCase &t)
        {
            // Catches old emitter: divided by rt word 0 (32-bit) and wrote quotients to rd;
            // PCSX2 divides every word by rt halfword 0 and writes no rd.
            R5900Context ctx;
            presetHiLo(ctx);
            setSentinelRd(ctx, 3);
            setReg(ctx, 4, 0x00000064u, 0xFFFFFF9Cu, 0x000002BCu, 0xFFFFFD43u);
            setReg(ctx, 5, 0x12340007u, 0x11111111u, 0x22222222u, 0x33333333u);
            ps2_mmi_pdivbw(&ctx, 3, ctx.r[4], ctx.r[5]);
            checkHiLo(t, ctx, 0xFFFFFFFE00000002ULL, 0xFFFFFFF20000000EULL, 0xFFFFFFFF00000000ULL, 0xFFFFFF9C00000064ULL, "PDIVBW");
            checkSentinelRd(t, ctx, 3, "PDIVBW");
        });
        tc.Run("PDIVBW emitter", [](TestCase &t)
        {
            CodeGenerator gen({}, {});
            checkEmit(t, gen, makeMmi(MMI_MMI2, MMI2_PDIVBW, 3, 4, 5), "ps2_mmi_pdivbw(ctx, 3, GPR_VEC(ctx, 4), GPR_VEC(ctx, 5))", "PDIVBW");
        });

        // ---- PMULTUW ----
        tc.Run("PMULTUW known answer", [](TestCase &t)
        {
            // Catches old emitter: HI/LO taken from the first product only, zero-extended.
            R5900Context ctx;
            presetHiLo(ctx);
            setReg(ctx, 4, 0xFFFFFFFFu, 0x11111111u, 0x00010000u, 0x22222222u);
            setReg(ctx, 5, 0x00000002u, 0x33333333u, 0x00010000u, 0x44444444u);
            ps2_mmi_pmultuw(&ctx, 3, ctx.r[4], ctx.r[5]);
            checkHiLo(t, ctx, 0x0000000000000001ULL, 0xFFFFFFFFFFFFFFFEULL, 0x0000000000000001ULL, 0x0000000000000000ULL, "PMULTUW");
            checkRd(t, ctx, 3, 0x00000001FFFFFFFEULL, 0x0000000100000000ULL, "PMULTUW");
        });
        tc.Run("PMULTUW emitter", [](TestCase &t)
        {
            CodeGenerator gen({}, {});
            checkEmit(t, gen, makeMmi(MMI_MMI3, MMI3_PMULTUW, 3, 4, 5), "ps2_mmi_pmultuw(ctx, 3, GPR_VEC(ctx, 4), GPR_VEC(ctx, 5))", "PMULTUW");
        });

        // ---- PDIVUW ----
        tc.Run("PDIVUW known answer", [](TestCase &t)
        {
            // Catches old emitter: only lane 0 divided and rd was overwritten with the quotient.
            R5900Context ctx;
            presetHiLo(ctx);
            setSentinelRd(ctx, 3);
            setReg(ctx, 4, 0x00000064u, 0x11111111u, 0x80000000u, 0x22222222u);
            setReg(ctx, 5, 0x00000007u, 0x33333333u, 0x00000000u, 0x44444444u);
            ps2_mmi_pdivuw(&ctx, 3, ctx.r[4], ctx.r[5]);
            checkHiLo(t, ctx, 0x0000000000000002ULL, 0x000000000000000EULL, 0xFFFFFFFF80000000ULL, 0xFFFFFFFFFFFFFFFFULL, "PDIVUW");
            checkSentinelRd(t, ctx, 3, "PDIVUW");
        });
        tc.Run("PDIVUW emitter", [](TestCase &t)
        {
            CodeGenerator gen({}, {});
            checkEmit(t, gen, makeMmi(MMI_MMI3, MMI3_PDIVUW, 3, 4, 5), "ps2_mmi_pdivuw(ctx, 3, GPR_VEC(ctx, 4), GPR_VEC(ctx, 5))", "PDIVUW");
        });
    });
}
