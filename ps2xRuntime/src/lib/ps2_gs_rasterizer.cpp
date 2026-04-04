#include "runtime/ps2_gs_rasterizer.h"
#include "runtime/ps2_gs_gpu.h"
#include "runtime/ps2_gs_common.h"
#include "runtime/ps2_gs_psmct16.h"
#include "runtime/ps2_gs_psmct32.h"
#include "runtime/ps2_gs_psmt4.h"
#include "runtime/ps2_gs_psmt8.h"
#include "ps2_log.h"
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace GSInternal;

namespace
{
    float fabsQ(float q)
    {
        return (std::fabs(q) > 1.0e-8f) ? q : 1.0f;
    }

    uint32_t decodePSMCT16(uint16_t pixel)
    {
        const uint32_t r = ((pixel >> 0) & 0x1Fu) << 3;
        const uint32_t g = ((pixel >> 5) & 0x1Fu) << 3;
        const uint32_t b = ((pixel >> 10) & 0x1Fu) << 3;
        const uint32_t a = (pixel & 0x8000u) ? 0x80u : 0u;
        return r | (g << 8) | (b << 16) | (a << 24);
    }

    uint32_t applyTexa(const GSTexaReg &texa, uint8_t psm, uint32_t texel)
    {
        if (psm == GS_PSM_CT32)
            return texel;

        const uint8_t r = static_cast<uint8_t>(texel & 0xFFu);
        const uint8_t g = static_cast<uint8_t>((texel >> 8) & 0xFFu);
        const uint8_t b = static_cast<uint8_t>((texel >> 16) & 0xFFu);
        const bool rgbZero = r == 0u && g == 0u && b == 0u;
        uint8_t a = static_cast<uint8_t>((texel >> 24) & 0xFFu);

        switch (psm)
        {
        case GS_PSM_CT24:
            a = (texa.aem && rgbZero) ? 0u : texa.ta0;
            break;
        case GS_PSM_CT16:
        case GS_PSM_CT16S:
            if ((a & 0x80u) != 0u)
                a = texa.ta1;
            else
                a = (texa.aem && rgbZero) ? 0u : texa.ta0;
            break;
        default:
            break;
        }

        return (texel & 0x00FFFFFFu) | (static_cast<uint32_t>(a) << 24);
    }

    uint16_t encodePSMCT16(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        return static_cast<uint16_t>(((r >> 3) & 0x1Fu) |
                                     (((g >> 3) & 0x1Fu) << 5) |
                                     (((b >> 3) & 0x1Fu) << 10) |
                                     ((a >= 0x40u) ? 0x8000u : 0u));
    }

    uint32_t addrPSMCT16Family(uint32_t basePtr, uint32_t width, uint8_t psm, uint32_t x, uint32_t y)
    {
        switch (psm)
        {
        case GS_PSM_CT16:
            return GSPSMCT16::addrPSMCT16(basePtr, width, x, y);
        case GS_PSM_CT16S:
            return GSPSMCT16::addrPSMCT16S(basePtr, width, x, y);
        case GS_PSM_Z16:
            return GSPSMCT16::addrPSMZ16(basePtr, width, x, y);
        case GS_PSM_Z16S:
            return GSPSMCT16::addrPSMZ16S(basePtr, width, x, y);
        default:
            return 0u;
        }
    }

    std::atomic<uint32_t> s_debugPrimitiveCount{0};
    std::atomic<uint32_t> s_debugPixelCount{0};
    std::atomic<uint32_t> s_debugContext1PrimitiveCount{0};
    std::atomic<uint32_t> s_debugFbp150PixelCount{0};
    bool passesAlphaTest(uint64_t testReg, uint8_t alpha)
    {
        if ((testReg & 0x1u) == 0u)
            return true;

        const uint8_t atst = static_cast<uint8_t>((testReg >> 1) & 0x7u);
        const uint8_t aref = static_cast<uint8_t>((testReg >> 4) & 0xFFu);

        switch (atst)
        {
        case 0:
            return false;
        case 1:
            return true;
        case 2:
            return alpha < aref;
        case 3:
            return alpha <= aref;
        case 4:
            return alpha == aref;
        case 5:
            return alpha >= aref;
        case 6:
            return alpha > aref;
        case 7:
            return alpha != aref;
        default:
            return true;
        }
    }

    struct AlphaTestResult
    {
        bool writeFramebuffer;
        bool preserveDestinationAlpha;
    };

    AlphaTestResult classifyAlphaTest(uint64_t testReg, uint8_t alpha)
    {
        const bool pass = passesAlphaTest(testReg, alpha);
        if (pass)
            return {true, false};

        // TEST.AFAIL controls what happens when the alpha comparison fails.
        switch (static_cast<uint8_t>((testReg >> 12) & 0x3u))
        {
        case 1: // FB_ONLY
            return {true, false};
        case 3: // RGB_ONLY
            return {true, true};
        case 0: // KEEP
        case 2: // ZB_ONLY
        default:
            return {false, false};
        }
    }

    struct TextureCombineResult
    {
        uint8_t r;
        uint8_t g;
        uint8_t b;
        uint8_t a;
    };

    TextureCombineResult combineTexture(const GSTex0Reg &tex,
                                        uint8_t vr,
                                        uint8_t vg,
                                        uint8_t vb,
                                        uint8_t va,
                                        uint8_t tr,
                                        uint8_t tg,
                                        uint8_t tb,
                                        uint8_t ta)
    {
        const bool textureHasAlpha = tex.tcc != 0u;
        TextureCombineResult out{tr, tg, tb, textureHasAlpha ? ta : va};

        switch (tex.tfx)
        {
        case 0: // MODULATE
            out.r = clampU8((tr * vr) >> 7);
            out.g = clampU8((tg * vg) >> 7);
            out.b = clampU8((tb * vb) >> 7);
            out.a = textureHasAlpha ? clampU8((ta * va) >> 7) : va;
            break;
        case 1: // DECAL
            out.r = tr;
            out.g = tg;
            out.b = tb;
            out.a = textureHasAlpha ? ta : va;
            break;
        case 2: // HIGHLIGHT
            out.r = clampU8(((tr * vr) >> 7) + va);
            out.g = clampU8(((tg * vg) >> 7) + va);
            out.b = clampU8(((tb * vb) >> 7) + va);
            out.a = textureHasAlpha ? clampU8(ta + va) : va;
            break;
        case 3: // HIGHLIGHT2
            out.r = clampU8(((tr * vr) >> 7) + va);
            out.g = clampU8(((tg * vg) >> 7) + va);
            out.b = clampU8(((tb * vb) >> 7) + va);
            out.a = textureHasAlpha ? ta : va;
            break;
        default:
            out.r = tr;
            out.g = tg;
            out.b = tb;
            out.a = textureHasAlpha ? ta : va;
            break;
        }

        return out;
    }

    uint32_t swizzleClutIndexCSM1(uint32_t index)
    {
        return (index & 0xE7u) | ((index & 0x08u) << 1u) | ((index & 0x10u) >> 1u);
    }

    uint32_t resolveClutIndex(uint8_t index, uint8_t csm, uint8_t csa, uint8_t sourcePsm)
    {
        uint32_t clutIndex = static_cast<uint32_t>(index);

        if (sourcePsm == GS_PSM_T4)
        {
            clutIndex = (static_cast<uint32_t>(csa) << 4u) | (clutIndex & 0x0Fu);
            if (csm == 0u)
                clutIndex = swizzleClutIndexCSM1(clutIndex);
        }
        else if (sourcePsm == GS_PSM_T8 && csm == 0u)
        {
            clutIndex = swizzleClutIndexCSM1(clutIndex);
        }

        return clutIndex;
    }

    bool tex1UsesLinearFilter(uint64_t tex1)
    {
        const uint8_t mmag = static_cast<uint8_t>((tex1 >> 5) & 0x1u);
        const uint8_t mmin = static_cast<uint8_t>((tex1 >> 6) & 0x7u);
        return mmag != 0u || mmin == 1u || (mmin & 0x4u) != 0u;
    }

    uint8_t lerpChannel(uint8_t c00, uint8_t c10, uint8_t c01, uint8_t c11, float fx, float fy)
    {
        const float top = static_cast<float>(c00) + (static_cast<float>(c10) - static_cast<float>(c00)) * fx;
        const float bottom = static_cast<float>(c01) + (static_cast<float>(c11) - static_cast<float>(c01)) * fx;
        return clampU8(static_cast<int>(std::lround(top + (bottom - top) * fy)));
    }
}

void GSRasterizer::drawPrimitive(GS *gs)
{
    const auto &ctx = gs->activeContext();
    PS2_IF_AGRESSIVE_LOGS({
        const uint32_t primitiveIndex = s_debugPrimitiveCount.fetch_add(1u, std::memory_order_relaxed);
        if (primitiveIndex < 64u)
        {
            std::cout << "[gs:prim] idx=" << primitiveIndex
                      << " type=" << static_cast<uint32_t>(gs->m_prim.type)
                      << " tme=" << static_cast<uint32_t>(gs->m_prim.tme)
                      << " abe=" << static_cast<uint32_t>(gs->m_prim.abe)
                      << " fst=" << static_cast<uint32_t>(gs->m_prim.fst)
                      << " ctxt=" << static_cast<uint32_t>(gs->m_prim.ctxt)
                      << " fbp=" << ctx.frame.fbp
                      << " fbw=" << ctx.frame.fbw
                      << " psm=0x" << std::hex << static_cast<uint32_t>(ctx.frame.psm) << std::dec
                      << " tex0=("
                      << "tbp0=" << ctx.tex0.tbp0
                      << " tbw=" << static_cast<uint32_t>(ctx.tex0.tbw)
                      << " psm=0x" << std::hex << static_cast<uint32_t>(ctx.tex0.psm) << std::dec
                      << " tw=" << static_cast<uint32_t>(ctx.tex0.tw)
                      << " th=" << static_cast<uint32_t>(ctx.tex0.th)
                      << " tcc=" << static_cast<uint32_t>(ctx.tex0.tcc)
                      << " tfx=" << static_cast<uint32_t>(ctx.tex0.tfx)
                      << " cbp=" << ctx.tex0.cbp
                      << " cpsm=0x" << std::hex << static_cast<uint32_t>(ctx.tex0.cpsm) << std::dec
                      << " csm=" << static_cast<uint32_t>(ctx.tex0.csm)
                      << " csa=" << static_cast<uint32_t>(ctx.tex0.csa)
                      << ")"
                      << " texclut=("
                      << "cbw=" << static_cast<uint32_t>(gs->m_texclut.cbw)
                      << " cou=" << static_cast<uint32_t>(gs->m_texclut.cou)
                      << " cov=" << gs->m_texclut.cov
                      << ")"
                      << " ofx=" << (ctx.xyoffset.ofx >> 4)
                      << " ofy=" << (ctx.xyoffset.ofy >> 4)
                      << " scissor=(" << ctx.scissor.x0
                      << "," << ctx.scissor.y0
                      << ")-(" << ctx.scissor.x1
                      << "," << ctx.scissor.y1 << ")"
                      << " test=0x" << std::hex << ctx.test
                      << " alpha=0x" << ctx.alpha
                      << std::dec
                      << " v0=(" << gs->m_vtxQueue[0].x << "," << gs->m_vtxQueue[0].y << ")"
                      << " uv0=(" << (gs->m_vtxQueue[0].u >> 4) << "," << (gs->m_vtxQueue[0].v >> 4) << ")"
                      << " stq0=(" << gs->m_vtxQueue[0].s << "," << gs->m_vtxQueue[0].t << "," << gs->m_vtxQueue[0].q << ")"
                      << " v1=(" << gs->m_vtxQueue[1].x << "," << gs->m_vtxQueue[1].y << ")"
                      << " uv1=(" << (gs->m_vtxQueue[1].u >> 4) << "," << (gs->m_vtxQueue[1].v >> 4) << ")"
                      << " stq1=(" << gs->m_vtxQueue[1].s << "," << gs->m_vtxQueue[1].t << "," << gs->m_vtxQueue[1].q << ")"
                      << " v2=(" << gs->m_vtxQueue[2].x << "," << gs->m_vtxQueue[2].y << ")"
                      << " uv2=(" << (gs->m_vtxQueue[2].u >> 4) << "," << (gs->m_vtxQueue[2].v >> 4) << ")"
                      << " stq2=(" << gs->m_vtxQueue[2].s << "," << gs->m_vtxQueue[2].t << "," << gs->m_vtxQueue[2].q << ")"
                      << " rgba0=(" << static_cast<uint32_t>(gs->m_vtxQueue[0].r) << ","
                      << static_cast<uint32_t>(gs->m_vtxQueue[0].g) << ","
                      << static_cast<uint32_t>(gs->m_vtxQueue[0].b) << ","
                      << static_cast<uint32_t>(gs->m_vtxQueue[0].a) << ")"
                      << " rgba1=(" << static_cast<uint32_t>(gs->m_vtxQueue[1].r) << ","
                      << static_cast<uint32_t>(gs->m_vtxQueue[1].g) << ","
                      << static_cast<uint32_t>(gs->m_vtxQueue[1].b) << ","
                      << static_cast<uint32_t>(gs->m_vtxQueue[1].a) << ")"
                      << " rgba2=(" << static_cast<uint32_t>(gs->m_vtxQueue[2].r) << ","
                      << static_cast<uint32_t>(gs->m_vtxQueue[2].g) << ","
                      << static_cast<uint32_t>(gs->m_vtxQueue[2].b) << ","
                      << static_cast<uint32_t>(gs->m_vtxQueue[2].a) << ")"
                      << std::endl;
        }
    });

    PS2_IF_AGRESSIVE_LOGS({
        if ((gs->m_prim.ctxt != 0u || ctx.frame.fbp == 150u) &&
            s_debugContext1PrimitiveCount.fetch_add(1u, std::memory_order_relaxed) < 32u)
        {
            std::cout << "[gs:copy-prim]"
                      << " type=" << static_cast<uint32_t>(gs->m_prim.type)
                      << " tme=" << static_cast<uint32_t>(gs->m_prim.tme)
                      << " abe=" << static_cast<uint32_t>(gs->m_prim.abe)
                      << " fst=" << static_cast<uint32_t>(gs->m_prim.fst)
                      << " ctxt=" << static_cast<uint32_t>(gs->m_prim.ctxt)
                      << " fbp=" << ctx.frame.fbp
                      << " fbw=" << ctx.frame.fbw
                      << " psm=0x" << std::hex << static_cast<uint32_t>(ctx.frame.psm) << std::dec
                      << " tex0=("
                      << "tbp0=" << ctx.tex0.tbp0
                      << " tbw=" << static_cast<uint32_t>(ctx.tex0.tbw)
                      << " psm=0x" << std::hex << static_cast<uint32_t>(ctx.tex0.psm) << std::dec
                      << " tcc=" << static_cast<uint32_t>(ctx.tex0.tcc)
                      << " tfx=" << static_cast<uint32_t>(ctx.tex0.tfx)
                      << " cbp=" << ctx.tex0.cbp
                      << " cpsm=0x" << std::hex << static_cast<uint32_t>(ctx.tex0.cpsm) << std::dec
                      << " csm=" << static_cast<uint32_t>(ctx.tex0.csm)
                      << " csa=" << static_cast<uint32_t>(ctx.tex0.csa)
                      << ")"
                      << " texclut=("
                      << "cbw=" << static_cast<uint32_t>(gs->m_texclut.cbw)
                      << " cou=" << static_cast<uint32_t>(gs->m_texclut.cou)
                      << " cov=" << gs->m_texclut.cov
                      << ")"
                      << " ofx=" << (ctx.xyoffset.ofx >> 4)
                      << " ofy=" << (ctx.xyoffset.ofy >> 4)
                      << " scissor=(" << ctx.scissor.x0
                      << "," << ctx.scissor.y0
                      << ")-(" << ctx.scissor.x1
                      << "," << ctx.scissor.y1 << ")"
                      << " test=0x" << std::hex << ctx.test
                      << " alpha=0x" << ctx.alpha
                      << std::dec << std::endl;
        }
    });

    if (gs->m_hasPreferredDisplaySource && ctx.frame.fbp == gs->m_preferredDisplayDestFbp)
    {
        gs->m_hasPreferredDisplaySource = false;
    }

    switch (gs->m_prim.type)
    {
    case GS_PRIM_SPRITE:
        drawSprite(gs);
        break;
    case GS_PRIM_TRIANGLE:
    case GS_PRIM_TRISTRIP:
    case GS_PRIM_TRIFAN:
        drawTriangle(gs);
        break;
    case GS_PRIM_LINE:
    case GS_PRIM_LINESTRIP:
        drawLine(gs);
        break;
    case GS_PRIM_POINT:
    {
        const GSVertex &v = gs->m_vtxQueue[0];
        const auto &ctx = gs->activeContext();
        int px = static_cast<int>(v.x) - (ctx.xyoffset.ofx >> 4);
        int py = static_cast<int>(v.y) - (ctx.xyoffset.ofy >> 4);
        writePixel(gs, px, py, v.r, v.g, v.b, v.a);
        break;
    }
    default:
        break;
    }
}

void GSRasterizer::writePixel(GS *gs, int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    const auto &ctx = gs->activeContext();
    if (x < ctx.scissor.x0 || x > ctx.scissor.x1 ||
        y < ctx.scissor.y0 || y > ctx.scissor.y1)
        return;

    const AlphaTestResult alphaTest = classifyAlphaTest(ctx.test, a);
    if (!alphaTest.writeFramebuffer)
        return;

    const uint32_t widthBlocks = (ctx.frame.fbw != 0u) ? ctx.frame.fbw : 1u;
    const uint32_t bytesPerPixel =
        (ctx.frame.psm == GS_PSM_CT16 || ctx.frame.psm == GS_PSM_CT16S) ? 2u : 4u;

    uint32_t off = 0u;
    if (ctx.frame.psm == GS_PSM_CT32 || ctx.frame.psm == GS_PSM_CT24)
    {
        off = GSPSMCT32::addrPSMCT32(GSInternal::framePageBaseToBlock(ctx.frame.fbp),
                                     widthBlocks,
                                     static_cast<uint32_t>(x),
                                     static_cast<uint32_t>(y));
    }
    else
    {
        off = addrPSMCT16Family(GSInternal::framePageBaseToBlock(ctx.frame.fbp),
                                widthBlocks,
                                ctx.frame.psm,
                                static_cast<uint32_t>(x),
                                static_cast<uint32_t>(y));
    }

    if (off + bytesPerPixel > gs->m_vramSize)
        return;

    PS2_IF_AGRESSIVE_LOGS({
        const uint32_t pixelIndex = s_debugPixelCount.fetch_add(1, std::memory_order_relaxed);
        if (pixelIndex < 32u)
        {
            std::cout << "[gs:pixel] idx=" << pixelIndex
                      << " xy=(" << x << "," << y << ")"
                      << " rgba=(" << static_cast<uint32_t>(r) << ","
                      << static_cast<uint32_t>(g) << ","
                      << static_cast<uint32_t>(b) << ","
                      << static_cast<uint32_t>(a) << ")"
                      << " fbp=" << ctx.frame.fbp
                      << " fbw=" << ctx.frame.fbw
                      << " psm=0x" << std::hex << static_cast<uint32_t>(ctx.frame.psm) << std::dec
                      << " off=0x" << std::hex << off << std::dec
                      << std::endl;
        }
    });

    PS2_IF_AGRESSIVE_LOGS({
        if (ctx.frame.fbp == 150u &&
            s_debugFbp150PixelCount.fetch_add(1u, std::memory_order_relaxed) < 32u)
        {
            std::cout << "[gs:fbp150-pixel]"
                      << " xy=(" << x << "," << y << ")"
                      << " rgba=(" << static_cast<uint32_t>(r) << ","
                      << static_cast<uint32_t>(g) << ","
                      << static_cast<uint32_t>(b) << ","
                      << static_cast<uint32_t>(a) << ")"
                      << " scissor=(" << ctx.scissor.x0
                      << "," << ctx.scissor.y0
                      << ")-(" << ctx.scissor.x1
                      << "," << ctx.scissor.y1 << ")"
                      << " off=0x" << std::hex << off << std::dec << std::endl;
        }
    });

    const uint8_t srcR = r;
    const uint8_t srcG = g;
    const uint8_t srcB = b;

    if (gs->m_prim.abe)
    {
        uint32_t existing = 0u;
        if (bytesPerPixel == 2u)
        {
            uint16_t packed = 0u;
            std::memcpy(&packed, gs->m_vram + off, 2);
            existing = decodePSMCT16(packed);
        }
        else
        {
            std::memcpy(&existing, gs->m_vram + off, 4);
        }
        uint8_t dr = existing & 0xFF;
        uint8_t dg = (existing >> 8) & 0xFF;
        uint8_t db = (existing >> 16) & 0xFF;
        uint8_t da = (existing >> 24) & 0xFF;

        // PABE disables alpha blending when the source alpha MSB is clear.
        if (!(gs->m_pabe && (a & 0x80u) == 0u))
        {
            uint64_t alphaReg = ctx.alpha;
            uint8_t asel = alphaReg & 3;
            uint8_t bsel = (alphaReg >> 2) & 3;
            uint8_t csel = (alphaReg >> 4) & 3;
            uint8_t dsel = (alphaReg >> 6) & 3;
            uint8_t fix = static_cast<uint8_t>((alphaReg >> 32) & 0xFF);

            auto pickRGB = [&](uint8_t sel, int cs, int cd) -> int
            {
                if (sel == 0)
                    return cs;
                if (sel == 1)
                    return cd;
                return 0;
            };
            int cAlpha = (csel == 0) ? a : (csel == 1) ? da
                                                       : fix;

            r = clampU8(((pickRGB(asel, r, dr) - pickRGB(bsel, r, dr)) * cAlpha >> 7) + pickRGB(dsel, r, dr));
            g = clampU8(((pickRGB(asel, g, dg) - pickRGB(bsel, g, dg)) * cAlpha >> 7) + pickRGB(dsel, g, dg));
            b = clampU8(((pickRGB(asel, b, db) - pickRGB(bsel, b, db)) * cAlpha >> 7) + pickRGB(dsel, b, db));
        }
        else
        {
            r = srcR;
            g = srcG;
            b = srcB;
        }
    }

    uint32_t mask = ctx.frame.fbmsk;
    if (!alphaTest.preserveDestinationAlpha &&
        (ctx.fba & 0x1ull) != 0ull &&
        ctx.frame.psm != GS_PSM_CT24)
    {
        a = static_cast<uint8_t>(a | 0x80u);
    }

    if (bytesPerPixel == 2u)
    {
        uint16_t pixel = encodePSMCT16(r, g, b, a);
        if ((mask & 0xFFFFu) != 0u)
        {
            uint16_t existing = 0u;
            std::memcpy(&existing, gs->m_vram + off, 2);
            pixel = static_cast<uint16_t>((pixel & ~mask) | (existing & mask));
        }
        std::memcpy(gs->m_vram + off, &pixel, 2);
        return;
    }

    uint32_t pixel = static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8) | (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(a) << 24);

    if (mask != 0)
    {
        uint32_t existing;
        std::memcpy(&existing, gs->m_vram + off, 4);
        pixel = (pixel & ~mask) | (existing & mask);
    }

    if (alphaTest.preserveDestinationAlpha)
    {
        uint32_t existing = 0u;
        std::memcpy(&existing, gs->m_vram + off, 4);
        pixel = (pixel & 0x00FFFFFFu) | (existing & 0xFF000000u);
    }

    std::memcpy(gs->m_vram + off, &pixel, 4);
}

uint32_t GSRasterizer::readTexelPSMCT32(GS *gs, uint32_t tbp0, uint32_t tbw, int texU, int texV)
{
    if (tbw == 0)
        tbw = 1;
    uint32_t off = GSPSMCT32::addrPSMCT32(tbp0, tbw, static_cast<uint32_t>(texU), static_cast<uint32_t>(texV));
    if (off + 4 > gs->m_vramSize)
        return 0xFFFF00FFu;
    uint32_t texel;
    std::memcpy(&texel, gs->m_vram + off, 4);
    return texel;
}

uint32_t GSRasterizer::readTexelPSMCT16(GS *gs, uint32_t tbp0, uint32_t tbw, int texU, int texV)
{
    if (tbw == 0)
        tbw = 1;
    const uint8_t psm = gs->activeContext().tex0.psm;
    uint32_t off = addrPSMCT16Family(tbp0, tbw, psm, static_cast<uint32_t>(texU), static_cast<uint32_t>(texV));
    if (off + 2 > gs->m_vramSize)
        return 0xFFFF00FFu;
    uint16_t texel;
    std::memcpy(&texel, gs->m_vram + off, 2);
    return decodePSMCT16(texel);
}

uint32_t GSRasterizer::readTexelPSMT4(GS *gs, uint32_t tbp0, uint32_t tbw, int texU, int texV)
{
    if (tbw == 0)
        tbw = 1;
    uint32_t nibbleAddr = GSPSMT4::addrPSMT4(tbp0, tbw, static_cast<uint32_t>(texU), static_cast<uint32_t>(texV));
    uint32_t byteOff = nibbleAddr >> 1;
    if (byteOff >= gs->m_vramSize)
        return 0;
    uint8_t packed = gs->m_vram[byteOff];
    uint32_t shift = (nibbleAddr & 1u) << 2;
    uint32_t idx = (packed >> shift) & 0xFu;
    return idx;
}

uint32_t GSRasterizer::lookupCLUT(GS *gs,
                                  uint8_t index,
                                  uint32_t cbp,
                                  uint8_t cpsm,
                                  uint8_t csm,
                                  uint8_t csa,
                                  uint8_t sourcePsm)
{
    const uint32_t clutIndex = resolveClutIndex(index, csm, csa, sourcePsm);
    const uint32_t clutWidth = (gs->m_texclut.cbw != 0u) ? static_cast<uint32_t>(gs->m_texclut.cbw) : 1u;
    const uint32_t clutX = static_cast<uint32_t>(gs->m_texclut.cou) + (clutIndex & 0x0Fu);
    const uint32_t clutY = static_cast<uint32_t>(gs->m_texclut.cov) + (clutIndex >> 4);

    if (cpsm == GS_PSM_CT32 || cpsm == GS_PSM_CT24)
    {
        const uint32_t off = GSPSMCT32::addrPSMCT32(cbp, clutWidth, clutX, clutY);
        if (off + 4 > gs->m_vramSize)
            return 0xFFFF00FFu;
        uint32_t color;
        std::memcpy(&color, gs->m_vram + off, 4);
        return applyTexa(gs->m_texa, cpsm, color);
    }

    if (cpsm == GS_PSM_CT16 || cpsm == GS_PSM_CT16S)
    {
        uint32_t off = addrPSMCT16Family(cbp, clutWidth, cpsm, clutX, clutY);
        if (off + 2 > gs->m_vramSize)
            return 0xFFFF00FFu;
        uint16_t c16;
        std::memcpy(&c16, gs->m_vram + off, 2);
        return applyTexa(gs->m_texa, cpsm, decodePSMCT16(c16));
    }

    return 0xFFFF00FFu;
}

uint32_t GSRasterizer::sampleTexture(GS *gs, float s, float t, float q, uint16_t u, uint16_t v)
{
    const auto &ctx = gs->activeContext();
    const auto &tex = ctx.tex0;

    int texW = 1 << tex.tw;
    int texH = 1 << tex.th;

    float texUf, texVf;
    if (gs->m_prim.fst)
    {
        texUf = static_cast<float>(u) / 16.0f;
        texVf = static_cast<float>(v) / 16.0f;
    }
    else
    {
        const float invQ = 1.0f / fabsQ(q);
        texUf = s * invQ * static_cast<float>(texW);
        texVf = t * invQ * static_cast<float>(texH);
    }

    auto samplePoint = [&](int sampleU, int sampleV) -> uint32_t
    {
        sampleU = clampInt(sampleU, 0, texW - 1);
        sampleV = clampInt(sampleV, 0, texH - 1);

        if (tex.psm == GS_PSM_CT32 || tex.psm == GS_PSM_CT24)
            return applyTexa(gs->m_texa, tex.psm, readTexelPSMCT32(gs, tex.tbp0, tex.tbw, sampleU, sampleV));

        if (tex.psm == GS_PSM_CT16 || tex.psm == GS_PSM_CT16S)
            return applyTexa(gs->m_texa, tex.psm, readTexelPSMCT16(gs, tex.tbp0, tex.tbw, sampleU, sampleV));

        if (tex.psm == GS_PSM_T4)
        {
            uint32_t idx = readTexelPSMT4(gs, tex.tbp0, tex.tbw, sampleU, sampleV);
            return lookupCLUT(gs, static_cast<uint8_t>(idx), tex.cbp, tex.cpsm, tex.csm, tex.csa, tex.psm);
        }

        if (tex.psm == GS_PSM_T8)
        {
            if (tex.tbw == 0)
                return 0xFFFF00FFu;
            uint32_t off = GSPSMT8::addrPSMT8(tex.tbp0, tex.tbw, static_cast<uint32_t>(sampleU), static_cast<uint32_t>(sampleV));
            if (off >= gs->m_vramSize)
                return 0xFFFF00FFu;
            uint8_t idx = gs->m_vram[off];
            return lookupCLUT(gs, idx, tex.cbp, tex.cpsm, tex.csm, tex.csa, tex.psm);
        }

        return 0xFFFF00FFu;
    };

    if (!tex1UsesLinearFilter(ctx.tex1))
    {
        return samplePoint(static_cast<int>(texUf), static_cast<int>(texVf));
    }

    const float sampleU = texUf - 0.5f;
    const float sampleV = texVf - 0.5f;
    const int u0 = static_cast<int>(std::floor(sampleU));
    const int v0 = static_cast<int>(std::floor(sampleV));
    const int u1 = u0 + 1;
    const int v1 = v0 + 1;
    const float fx = sampleU - static_cast<float>(u0);
    const float fy = sampleV - static_cast<float>(v0);

    const uint32_t c00 = samplePoint(u0, v0);
    const uint32_t c10 = samplePoint(u1, v0);
    const uint32_t c01 = samplePoint(u0, v1);
    const uint32_t c11 = samplePoint(u1, v1);

    const uint8_t r = lerpChannel(static_cast<uint8_t>(c00 & 0xFFu),
                                  static_cast<uint8_t>(c10 & 0xFFu),
                                  static_cast<uint8_t>(c01 & 0xFFu),
                                  static_cast<uint8_t>(c11 & 0xFFu),
                                  fx, fy);
    const uint8_t g = lerpChannel(static_cast<uint8_t>((c00 >> 8) & 0xFFu),
                                  static_cast<uint8_t>((c10 >> 8) & 0xFFu),
                                  static_cast<uint8_t>((c01 >> 8) & 0xFFu),
                                  static_cast<uint8_t>((c11 >> 8) & 0xFFu),
                                  fx, fy);
    const uint8_t b = lerpChannel(static_cast<uint8_t>((c00 >> 16) & 0xFFu),
                                  static_cast<uint8_t>((c10 >> 16) & 0xFFu),
                                  static_cast<uint8_t>((c01 >> 16) & 0xFFu),
                                  static_cast<uint8_t>((c11 >> 16) & 0xFFu),
                                  fx, fy);
    const uint8_t a = lerpChannel(static_cast<uint8_t>((c00 >> 24) & 0xFFu),
                                  static_cast<uint8_t>((c10 >> 24) & 0xFFu),
                                  static_cast<uint8_t>((c01 >> 24) & 0xFFu),
                                  static_cast<uint8_t>((c11 >> 24) & 0xFFu),
                                  fx, fy);

    return static_cast<uint32_t>(r) |
           (static_cast<uint32_t>(g) << 8) |
           (static_cast<uint32_t>(b) << 16) |
           (static_cast<uint32_t>(a) << 24);
}

void GSRasterizer::drawSprite(GS *gs)
{
    const GSVertex &v0 = gs->m_vtxQueue[0];
    const GSVertex &v1 = gs->m_vtxQueue[1];
    const auto &ctx = gs->activeContext();

    int ofx = ctx.xyoffset.ofx >> 4;
    int ofy = ctx.xyoffset.ofy >> 4;

    int x0 = static_cast<int>(v0.x) - ofx;
    int y0 = static_cast<int>(v0.y) - ofy;
    int x1 = static_cast<int>(v1.x) - ofx;
    int y1 = static_cast<int>(v1.y) - ofy;

    if (x0 > x1)
        std::swap(x0, x1);
    if (y0 > y1)
        std::swap(y0, y1);

    const int unclippedX0 = x0;
    const int unclippedY0 = y0;
    const int spanX = std::max(1, x1 - x0);
    const int spanY = std::max(1, y1 - y0);
    const int unclippedX1 = unclippedX0 + spanX - 1;
    const int unclippedY1 = unclippedY0 + spanY - 1;

    // If the sprite rectangle is fully outside scissor, nothing should render.
    if (unclippedX1 < ctx.scissor.x0 || unclippedX0 > ctx.scissor.x1 ||
        unclippedY1 < ctx.scissor.y0 || unclippedY0 > ctx.scissor.y1)
    {
        // maybe a log here idk ?
        return;
    }

    const int drawX0 = clampInt(unclippedX0, ctx.scissor.x0, ctx.scissor.x1);
    const int drawY0 = clampInt(unclippedY0, ctx.scissor.y0, ctx.scissor.y1);
    const int drawX1 = clampInt(unclippedX1, ctx.scissor.x0, ctx.scissor.x1);
    const int drawY1 = clampInt(unclippedY1, ctx.scissor.y0, ctx.scissor.y1);

    const uint64_t alphaReg = ctx.alpha;
    const uint8_t alphaMode = static_cast<uint8_t>(alphaReg & 0xFFu);
    const uint8_t alphaFix = static_cast<uint8_t>((alphaReg >> 32) & 0xFFu);
    const bool looksLikeDisplayCopy =
        gs->m_prim.tme &&
        gs->m_prim.abe &&
        gs->m_prim.fst &&
        gs->m_prim.ctxt &&
        ctx.frame.fbp != ctx.tex0.tbp0 &&
        alphaMode == 0x64u &&
        (alphaFix == 0x60u || alphaFix == 0x80u) &&
        unclippedX0 <= 0 &&
        unclippedY0 <= 0 &&
        unclippedX1 >= 639 &&
        unclippedY1 >= 447;
    if (looksLikeDisplayCopy)
    {
        gs->m_preferredDisplaySourceFrame = {ctx.tex0.tbp0, ctx.tex0.tbw, ctx.tex0.psm, 0u};
        gs->m_preferredDisplayDestFbp = ctx.frame.fbp;
        gs->m_hasPreferredDisplaySource = true;
    }

    uint8_t r = v1.r, g = v1.g, b = v1.b, a = v1.a;

    if (gs->m_prim.tme)
    {
        const auto &tex = ctx.tex0;
        int texW = 1 << tex.tw;
        int texH = 1 << tex.th;
        if (texW == 0)
            texW = 1;
        if (texH == 0)
            texH = 1;

        float u0f, v0f, u1f, v1f;
        if (gs->m_prim.fst)
        {
            u0f = static_cast<float>(v0.u >> 4);
            v0f = static_cast<float>(v0.v >> 4);
            u1f = static_cast<float>(v1.u >> 4);
            v1f = static_cast<float>(v1.v >> 4);
        }
        else
        {
            const float q0 = fabsQ(v0.q);
            const float q1 = fabsQ(v1.q);
            u0f = (v0.s / q0) * static_cast<float>(texW);
            v0f = (v0.t / q0) * static_cast<float>(texH);
            u1f = (v1.s / q1) * static_cast<float>(texW);
            v1f = (v1.t / q1) * static_cast<float>(texH);
        }

        float spriteW = static_cast<float>(spanX);
        float spriteH = static_cast<float>(spanY);
        if (spriteW < 1.0f)
            spriteW = 1.0f;
        if (spriteH < 1.0f)
            spriteH = 1.0f;

        for (int y = drawY0; y <= drawY1; ++y)
        {
            float ty = (static_cast<float>(y - unclippedY0) + 0.5f) / spriteH;
            float texVf = v0f + (v1f - v0f) * ty;

            for (int x = drawX0; x <= drawX1; ++x)
            {
                float tx = (static_cast<float>(x - unclippedX0) + 0.5f) / spriteW;
                float texUf = u0f + (u1f - u0f) * tx;
                uint32_t texel = 0xFFFF00FFu;
                if (gs->m_prim.fst)
                {
                    const uint16_t sampleU = static_cast<uint16_t>(clampInt(static_cast<int>(std::lround(texUf * 16.0f)), 0, 0xFFFF));
                    const uint16_t sampleV = static_cast<uint16_t>(clampInt(static_cast<int>(std::lround(texVf * 16.0f)), 0, 0xFFFF));
                    texel = sampleTexture(gs, 0.0f, 0.0f, 1.0f, sampleU, sampleV);
                }
                else
                {
                    texel = sampleTexture(gs,
                                          texUf / static_cast<float>(texW),
                                          texVf / static_cast<float>(texH),
                                          1.0f, 0u, 0u);
                }

                uint8_t tr = static_cast<uint8_t>(texel & 0xFF);
                uint8_t tg = static_cast<uint8_t>((texel >> 8) & 0xFF);
                uint8_t tb = static_cast<uint8_t>((texel >> 16) & 0xFF);
                uint8_t ta = static_cast<uint8_t>((texel >> 24) & 0xFF);

                const TextureCombineResult color = combineTexture(tex, r, g, b, a, tr, tg, tb, ta);
                writePixel(gs, x, y, color.r, color.g, color.b, color.a);
            }
        }
    }
    else
    {
        for (int y = drawY0; y <= drawY1; ++y)
            for (int x = drawX0; x <= drawX1; ++x)
                writePixel(gs, x, y, r, g, b, a);
    }
}

void GSRasterizer::drawTriangle(GS *gs)
{
    const GSVertex &v0 = gs->m_vtxQueue[0];
    const GSVertex &v1 = gs->m_vtxQueue[1];
    const GSVertex &v2 = gs->m_vtxQueue[2];
    const auto &ctx = gs->activeContext();

    int ofx = ctx.xyoffset.ofx >> 4;
    int ofy = ctx.xyoffset.ofy >> 4;

    float fx0 = v0.x - static_cast<float>(ofx);
    float fy0 = v0.y - static_cast<float>(ofy);
    float fx1 = v1.x - static_cast<float>(ofx);
    float fy1 = v1.y - static_cast<float>(ofy);
    float fx2 = v2.x - static_cast<float>(ofx);
    float fy2 = v2.y - static_cast<float>(ofy);

    int minX = static_cast<int>(std::floor(std::min({fx0, fx1, fx2})));
    int maxX = static_cast<int>(std::ceil(std::max({fx0, fx1, fx2})));
    int minY = static_cast<int>(std::floor(std::min({fy0, fy1, fy2})));
    int maxY = static_cast<int>(std::ceil(std::max({fy0, fy1, fy2})));

    minX = clampInt(minX, ctx.scissor.x0, ctx.scissor.x1);
    maxX = clampInt(maxX, ctx.scissor.x0, ctx.scissor.x1);
    minY = clampInt(minY, ctx.scissor.y0, ctx.scissor.y1);
    maxY = clampInt(maxY, ctx.scissor.y0, ctx.scissor.y1);

    float denom = (fy1 - fy2) * (fx0 - fx2) + (fx2 - fx1) * (fy0 - fy2);
    if (std::fabs(denom) < 0.001f)
        return;

    const float winding = (denom < 0.0f) ? -1.0f : 1.0f;
    const float invAbsDenom = 1.0f / std::fabs(denom);
    constexpr float kEdgeEpsilon = 1.0e-4f;

    for (int y = minY; y <= maxY; ++y)
    {
        float py = static_cast<float>(y) + 0.5f;
        for (int x = minX; x <= maxX; ++x)
        {
            float px = static_cast<float>(x) + 0.5f;

            float w0 = (((fy1 - fy2) * (px - fx2) + (fx2 - fx1) * (py - fy2)) * winding) * invAbsDenom;
            float w1 = (((fy2 - fy0) * (px - fx2) + (fx0 - fx2) * (py - fy2)) * winding) * invAbsDenom;
            float w2 = 1.0f - w0 - w1;

            if (w0 < -kEdgeEpsilon || w1 < -kEdgeEpsilon || w2 < -kEdgeEpsilon)
                continue;

            uint8_t r, g, b, a;
            if (gs->m_prim.iip)
            {
                r = clampU8(static_cast<int>(v0.r * w0 + v1.r * w1 + v2.r * w2));
                g = clampU8(static_cast<int>(v0.g * w0 + v1.g * w1 + v2.g * w2));
                b = clampU8(static_cast<int>(v0.b * w0 + v1.b * w1 + v2.b * w2));
                a = clampU8(static_cast<int>(v0.a * w0 + v1.a * w1 + v2.a * w2));
            }
            else
            {
                r = v2.r;
                g = v2.g;
                b = v2.b;
                a = v2.a;
            }

            if (gs->m_prim.tme)
            {
                float is, it, iq;
                uint16_t iu, iv;
                if (gs->m_prim.fst)
                {
                    iu = static_cast<uint16_t>(v0.u * w0 + v1.u * w1 + v2.u * w2);
                    iv = static_cast<uint16_t>(v0.v * w0 + v1.v * w1 + v2.v * w2);
                    is = 0.0f;
                    it = 0.0f;
                    iq = 1.0f;
                }
                else
                {
                    const float invQ0 = 1.0f / fabsQ(v0.q);
                    const float invQ1 = 1.0f / fabsQ(v1.q);
                    const float invQ2 = 1.0f / fabsQ(v2.q);
                    const float sOverQ = (v0.s * invQ0) * w0 + (v1.s * invQ1) * w1 + (v2.s * invQ2) * w2;
                    const float tOverQ = (v0.t * invQ0) * w0 + (v1.t * invQ1) * w1 + (v2.t * invQ2) * w2;
                    const float invQ = invQ0 * w0 + invQ1 * w1 + invQ2 * w2;
                    iq = (std::fabs(invQ) > 1.0e-8f) ? (1.0f / invQ) : 1.0f;
                    is = sOverQ * iq;
                    it = tOverQ * iq;
                    iu = 0;
                    iv = 0;
                }

                uint32_t texel = sampleTexture(gs, is, it, iq, iu, iv);

                uint8_t tr = static_cast<uint8_t>(texel & 0xFF);
                uint8_t tg = static_cast<uint8_t>((texel >> 8) & 0xFF);
                uint8_t tb = static_cast<uint8_t>((texel >> 16) & 0xFF);
                uint8_t ta = static_cast<uint8_t>((texel >> 24) & 0xFF);

                const auto &tex = ctx.tex0;
                const uint8_t shadeR = r;
                const uint8_t shadeG = g;
                const uint8_t shadeB = b;
                const uint8_t shadeA = a;
                const TextureCombineResult color = combineTexture(tex, shadeR, shadeG, shadeB, shadeA, tr, tg, tb, ta);

                r = color.r;
                g = color.g;
                b = color.b;
                a = color.a;
            }

            writePixel(gs, x, y, r, g, b, a);
        }
    }
}

void GSRasterizer::drawLine(GS *gs)
{
    const GSVertex &v0 = gs->m_vtxQueue[0];
    const GSVertex &v1 = gs->m_vtxQueue[1];
    const auto &ctx = gs->activeContext();

    int ofx = ctx.xyoffset.ofx >> 4;
    int ofy = ctx.xyoffset.ofy >> 4;

    int x0 = static_cast<int>(v0.x) - ofx;
    int y0 = static_cast<int>(v0.y) - ofy;
    int x1 = static_cast<int>(v1.x) - ofx;
    int y1 = static_cast<int>(v1.y) - ofy;

    int dx = std::abs(x1 - x0);
    int dy = -std::abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;

    int totalSteps = std::max(std::abs(x1 - x0), std::abs(y1 - y0));
    if (totalSteps == 0)
        totalSteps = 1;
    int step = 0;

    for (;;)
    {
        float t = static_cast<float>(step) / static_cast<float>(totalSteps);
        uint8_t r, g, b, a;
        if (gs->m_prim.iip)
        {
            r = clampU8(static_cast<int>(v0.r + (v1.r - v0.r) * t));
            g = clampU8(static_cast<int>(v0.g + (v1.g - v0.g) * t));
            b = clampU8(static_cast<int>(v0.b + (v1.b - v0.b) * t));
            a = clampU8(static_cast<int>(v0.a + (v1.a - v0.a) * t));
        }
        else
        {
            r = v1.r;
            g = v1.g;
            b = v1.b;
            a = v1.a;
        }

        writePixel(gs, x0, y0, r, g, b, a);

        if (x0 == x1 && y0 == y1)
            break;

        int e2 = 2 * err;
        if (e2 >= dy)
        {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx)
        {
            err += dx;
            y0 += sy;
        }
        ++step;
    }
}
