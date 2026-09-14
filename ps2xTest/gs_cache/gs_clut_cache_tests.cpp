#include "gs_test_support.h"

using namespace GSTest;

namespace
{
    template<uint8_t Cpsm>
    void unalignedCsm1()
    {
        FrontendFixture f;
        auto tex = texture(GS_PSM_T8, 64);
        tex.cbp = 31;
        tex.cpsm = Cpsm;
        f.index(tex, 128);
        f.palette(tex, 128, Cpsm == GS_PSM_CT32 ? kRed : 0x801Fu);
        f.bind(tex);
        expectEqual(f.sample(), kRed, "CSM1 CLUT load crosses a physical page");
    }

    void wrappedClut()
    {
        FrontendFixture f;
        auto tex = texture(GS_PSM_T8, 64);
        tex.cbp = 16383;
        f.index(tex, 128);
        f.palette(tex, 128, kGreen);
        f.bind(tex);
        expectEqual(f.sample(), kGreen, "CLUT load wraps at the end of VRAM");
    }

    void unalignedCsm2()
    {
        FrontendFixture f;
        auto tex = texture(GS_PSM_T8, 64);
        tex.cbp = 31;
        tex.cpsm = GS_PSM_CT16;
        tex.csm = 1;
        constexpr uint32_t entry = 193;
        f.index(tex, entry);
        f.gs.writeRegister(GS_REG_TEXCLUT, 4ull | (3ull << 6) | (2ull << 12));
        f.gs.WriteVram(GS_PSM_CT16, tex.cbp, 4, 48 + entry, 2, 0x83E0);
        f.bind(tex);
        expectEqual(f.sample(), kGreen, "CSM2 CBW/COU/COV and swizzle carry");
    }

    void retainedPalette()
    {
        FrontendFixture f;
        auto tex = texture(GS_PSM_T4, 64);
        f.index(tex, 8);
        f.palette(tex, 8, kRed);
        f.bind(tex);
        expectEqual(f.sample(), kRed, "initial palette");
        f.palette(tex, 8, kGreen);
        f.flush();
        tex.cld = 0;
        f.bind(tex);
        expectEqual(f.sample(), kRed, "TEXFLUSH and CLD=0 preserve the CLUT temporary buffer");
        tex.cld = 1;
        f.bind(tex);
        expectEqual(f.sample(), kGreen, "CLD=1 reloads the palette");
    }

    void clutUsesPageCache()
    {
        FrontendFixture f;
        auto tex = texture(GS_PSM_T4, 64);
        f.index(tex, 8);
        f.palette(tex, 8, kRed);
        f.bind(tex);
        // No texture sampling between loads: the CLUT source page is still resident.
        f.palette(tex, 8, kGreen);
        f.bind(tex);
        expectEqual(f.sample(), kRed, "CLD=1 alone does not invalidate the texture page buffer");
        f.flush();
        f.bind(tex);
        expectEqual(f.sample(), kGreen, "identical TEX0 write still loads after TEXFLUSH");
    }

    template<unsigned Bank>
    void conditionalLoad()
    {
        FrontendFixture f;
        auto tex = texture(GS_PSM_T4, 64);
        tex.cld = 2 + Bank;
        f.index(tex, 0);
        f.palette(tex, 0, kRed);
        f.bind(tex);
        auto other = tex;
        other.cbp = 192;
        other.cld = 3 - Bank;
        f.palette(other, 0, kGreen);
        f.bind(other);
        tex.cld = 4 + Bank;
        f.bind(tex);
        expectEqual(f.sample(), kGreen, "matching CBP skips load, not switches palettes");
        tex.cbp = 160;
        f.palette(tex, 0, kBlue);
        f.flush();
        f.bind(tex);
        expectEqual(f.sample(), kBlue, "different CBP loads and updates comparison memory");
        f.palette(tex, 0, kRed);
        f.flush();
        f.bind(tex);
        expectEqual(f.sample(), kBlue, "repeated conditional CBP skips reload");
    }

    void reservedCld()
    {
        FrontendFixture f;
        auto tex = texture(GS_PSM_T4, 64);
        f.index(tex, 0);
        f.palette(tex, 0, kRed);
        f.bind(tex);
        tex.cbp = 192;
        f.palette(tex, 0, kGreen);
        for (uint8_t cld : {6, 7})
        {
            tex.cld = cld;
            f.flush();
            f.bind(tex);
            expectEqual(f.sample(), kRed, "reserved CLD leaves palette unchanged");
        }
    }

    void nonIndexedCld()
    {
        FrontendFixture f;
        auto tex = texture(GS_PSM_T4, 64);
        tex.cld = 2;
        f.index(tex, 0);
        f.palette(tex, 0, kRed);
        f.bind(tex);
        auto direct = tex;
        direct.psm = GS_PSM_CT32;
        direct.cbp = 192;
        f.bind(direct);
        f.palette(tex, 0, kGreen);
        f.flush();
        tex.cld = 4;
        f.bind(tex);
        expectEqual(f.sample(), kRed, "direct texture TEX0 must not modify CBP0");
    }

    void tex2Reload()
    {
        FrontendFixture f;
        auto tex = texture(GS_PSM_T8, 64);
        f.index(tex, 128);
        f.palette(tex, 128, kRed);
        f.bind(tex);
        expectEqual(f.sample(), kRed, "initial TEX0 palette");
        tex.cbp = 31;
        f.palette(tex, 128, kGreen);
        tex.tbp0 = 2048;
        tex.tbw = 8;
        tex.tw = tex.th = 9;
        f.flush();
        f.bind(tex, 0, true);
        expectEqual(f.sample(), kGreen, "TEX2 reloads from a crossing CLUT without changing texture layout");
        const auto state = f.gs.getDebugSnapshot();
        expectEqual(state.ctx[0].tex0.tbp0, 64, "TEX2 preserves TBP");
        expectEqual(state.ctx[0].tex0.tbw, 2, "TEX2 preserves TBW");
        expectEqual(state.ctx[0].tex0.tw, 8, "TEX2 preserves TW");
    }

    void sharedContexts()
    {
        FrontendFixture f;
        auto tex = texture(GS_PSM_T4, 64);
        f.index(tex, 0);
        f.palette(tex, 0, kRed);
        f.bind(tex, 0);
        tex.cbp = 192;
        f.palette(tex, 0, kGreen);
        f.bind(tex, 1);
        expectEqual(f.sample(0, 0, 0), kGreen, "both drawing contexts share one CLUT temporary buffer");
        tex.cbp = 256;
        f.palette(tex, 0, kBlue);
        f.bind(tex, 1, true);
        expectEqual(f.sample(0, 0, 0), kBlue, "context 1 TEX2 changes palette visible to context 0");
    }

    template<uint8_t Cpsm>
    void csaBanks()
    {
        FrontendFixture f;
        auto tex = texture(GS_PSM_T4, 64);
        tex.cpsm = Cpsm;
        f.index(tex, 15);
        f.palette(tex, 15, Cpsm == GS_PSM_CT32 ? kRed : 0x801Fu);
        f.bind(tex);
        auto other = tex;
        other.cbp = 192;
        other.csa = Cpsm == GS_PSM_CT32 ? 15 : 31;
        f.palette(other, 15, Cpsm == GS_PSM_CT32 ? kGreen : 0x83E0u);
        f.bind(other);
        expectEqual(f.sample(), kGreen, "highest CSA bank is readable");
        tex.cld = 0;
        tex.csa = Cpsm == GS_PSM_CT32 ? 16 : 0;
        f.bind(tex);
        expectEqual(f.sample(), kRed, "partial CLUT load retains unrelated banks and masks CSA per CPSM");
    }

    void texaWithoutReload()
    {
        FrontendFixture f;
        auto tex = texture(GS_PSM_T4, 64);
        tex.cpsm = GS_PSM_CT16;
        f.index(tex, 0);
        f.index(tex, 1, 1);
        f.index(tex, 2, 2);
        f.palette(tex, 0, 0x001F);
        f.palette(tex, 1, 0x8000);
        f.palette(tex, 2, 0x0000);
        f.bind(tex);
        f.gs.writeRegister(GS_REG_TEXA, 0x20ull | (0x40ull << 32));
        expectEqual(f.sample(), 0x200000F8, "TA0 applied at lookup");
        expectEqual(f.sample(1), 0x40000000, "TA1 applied to CLUT alpha bit");
        f.gs.writeRegister(GS_REG_TEXA, 0x70ull | (1ull << 15) | (0x60ull << 32));
        expectEqual(f.sample(), 0x700000F8, "TEXA changes without reloading raw palette");
        expectEqual(f.sample(1), 0x60000000, "AEM does not clear black with alpha bit set");
        expectEqual(f.sample(2), 0, "AEM clears zero color with alpha bit clear");
    }

    void paletteBeforeFiltering()
    {
        FrontendFixture f;
        auto tex = texture(GS_PSM_T4, 64);
        f.index(tex, 0, 0, 0);
        f.index(tex, 2, 1, 0);
        f.index(tex, 4, 0, 1);
        f.index(tex, 6, 1, 1);
        f.palette(tex, 0, kRed);
        f.palette(tex, 2, kGreen);
        f.palette(tex, 4, kBlue);
        f.palette(tex, 6, 0x80F8F8F8);
        f.palette(tex, 3, 0x80FF00FF);
        f.bind(tex);
        f.gs.writeRegister(GS_REG_TEX1_1, (1ull << 5) | (1ull << 6));
        expectEqual(f.sample(1, 1), 0x807C7C7C, "bilinear filtering blends four colors, never four indices");
    }

    void highPlanes()
    {
        FrontendFixture f;
        auto low = texture(GS_PSM_T4HL, 31);
        auto high = low;
        high.psm = GS_PSM_T4HH;
        high.cbp = 192;
        high.csa = 1;
        f.gs.WriteVram(GS_PSM_CT32, 31, 2, 8, 0, 0x00ABCDEF);
        f.index(low, 3, 8);
        f.index(high, 12, 8);
        f.palette(low, 3, kRed);
        f.palette(high, 12, kGreen);
        f.bind(low);
        f.bind(high);
        low.cld = high.cld = 0;
        f.bind(low);
        expectEqual(f.sample(8), kRed, "low nibble uses its own CSA bank");
        f.bind(high);
        expectEqual(f.sample(8), kGreen, "same cached physical bytes supply the high nibble");
        expectEqual(f.gs.ReadVram(GS_PSM_CT24, 31, 2, 8, 0), 0xABCDEF, "index writes preserve the RGB plane");
    }
}

int main(int argc, char** argv)
{
    return run(argc, argv, {
        {"unaligned_csm1_ct32", unalignedCsm1<GS_PSM_CT32>},
        {"unaligned_csm1_ct16", unalignedCsm1<GS_PSM_CT16>},
        {"unaligned_csm1_ct16s", unalignedCsm1<GS_PSM_CT16S>},
        {"wrapped_clut", wrappedClut}, {"unaligned_csm2", unalignedCsm2},
        {"retained_palette", retainedPalette}, {"clut_uses_page_cache", clutUsesPageCache},
        {"cbp0_conditional", conditionalLoad<0>}, {"cbp1_conditional", conditionalLoad<1>},
        {"reserved_cld", reservedCld}, {"nonindexed_cld", nonIndexedCld},
        {"tex2_reload", tex2Reload}, {"shared_contexts", sharedContexts},
        {"csa_ct32", csaBanks<GS_PSM_CT32>}, {"csa_ct16", csaBanks<GS_PSM_CT16>},
        {"csa_ct16s", csaBanks<GS_PSM_CT16S>}, {"texa_without_reload", texaWithoutReload},
        {"palette_before_filtering", paletteBeforeFiltering}, {"high_planes", highPlanes}
    });
}
