#include "ps2_gs_rasterizer.h"
#include "ps2_gs_gpu.h"
#include "ps2_gs_common.h"
#include "ps2_gs_psmt4.h"
#include "ps2_gs_psmt8.h"
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
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

    uint16_t encodePSMCT16(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        return static_cast<uint16_t>(((r >> 3) & 0x1Fu) |
                                     (((g >> 3) & 0x1Fu) << 5) |
                                     (((b >> 3) & 0x1Fu) << 10) |
                                     ((a >= 0x40u) ? 0x8000u : 0u));
    }

    std::atomic<uint32_t> s_debugPrimitiveCount{0};
    std::atomic<uint32_t> s_debugPixelCount{0};
    std::atomic<uint32_t> s_debugContext1PrimitiveCount{0};
    std::atomic<uint32_t> s_debugFbp150PixelCount{0};
    std::atomic<uint32_t> s_debugCvFontPrimitiveCount{0};
    std::atomic<uint32_t> s_debugCvFontSampleCount{0};
    std::atomic<bool> s_debugCvFontClutDumped{false};
    std::atomic<uint32_t> s_debugCvFontTrianglePrimitiveCount{0};
    std::atomic<uint32_t> s_debugCvFontTriangleSampleCount{0};

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
        const bool useTextureRgb = tex.tcc != 0u;
        TextureCombineResult out{vr, vg, vb, ta};

        switch (tex.tfx)
        {
        case 0:
            if (useTextureRgb)
            {
                out.r = clampU8((tr * vr) >> 7);
                out.g = clampU8((tg * vg) >> 7);
                out.b = clampU8((tb * vb) >> 7);
            }
            out.a = clampU8((ta * va) >> 7);
            break;
        case 1:
            if (useTextureRgb)
            {
                out.r = tr;
                out.g = tg;
                out.b = tb;
            }
            out.a = ta;
            break;
        case 2:
        case 3:
            if (useTextureRgb)
            {
                out.r = clampU8((tr * vr) >> 7);
                out.g = clampU8((tg * vg) >> 7);
                out.b = clampU8((tb * vb) >> 7);
            }
            out.a = ta;
            break;
        default:
            if (useTextureRgb)
            {
                out.r = tr;
                out.g = tg;
                out.b = tb;
            }
            out.a = ta;
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

    bool isVeronicaWarningFont(const GSTex0Reg &tex)
    {
        return tex.tbp0 == 12160u &&
               tex.tbw == 8u &&
               tex.psm == GS_PSM_T4 &&
               tex.cbp == 16368u &&
               tex.cpsm == GS_PSM_CT32 &&
               tex.csm == 0u &&
               tex.csa == 0u;
    }

    void dumpFontClut(const uint8_t *vram, uint32_t vramSize, uint32_t cbp)
    {
        //this is for code veronica only but maybe can help in other games
        bool expected = false;
        if (!s_debugCvFontClutDumped.compare_exchange_strong(expected, true, std::memory_order_relaxed))
            return;

        {
            std::cout << "[cvfont:clut] cbp=" << cbp << std::endl;
        }
        for (uint32_t i = 0; i < 24u; ++i)
        {
            const uint32_t off = cbp * 256u + i * 4u;
            if (off + 4u > vramSize)
                break;

            uint32_t color = 0u;
            std::memcpy(&color, vram + off, sizeof(color));
            std::cout << "  [" << i << "] rgba=("
                      << static_cast<uint32_t>(color & 0xFFu) << ","
                      << static_cast<uint32_t>((color >> 8) & 0xFFu) << ","
                      << static_cast<uint32_t>((color >> 16) & 0xFFu) << ","
                      << static_cast<uint32_t>((color >> 24) & 0xFFu) << ")" << std::endl;
        }
    }

}

void GSRasterizer::drawPrimitive(GS *gs)
{
    const uint32_t primitiveIndex = s_debugPrimitiveCount.fetch_add(1, std::memory_order_relaxed);
    if (primitiveIndex < 64u)
    {
        const auto &ctx = gs->activeContext();
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
                  << " tcc=" << static_cast<uint32_t>(ctx.tex0.tcc)
                  << " tfx=" << static_cast<uint32_t>(ctx.tex0.tfx)
                  << " cbp=" << ctx.tex0.cbp
                  << " cpsm=0x" << std::hex << static_cast<uint32_t>(ctx.tex0.cpsm) << std::dec
                  << " csm=" << static_cast<uint32_t>(ctx.tex0.csm)
                  << " csa=" << static_cast<uint32_t>(ctx.tex0.csa)
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

    const auto &ctx = gs->activeContext();
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

    uint32_t fbBase = ctx.frame.fbp * 8192u;
    uint32_t stride = fbStride(ctx.frame.fbw, ctx.frame.psm);
    if (stride == 0)
        return;

    const uint32_t bytesPerPixel = std::max<uint32_t>(1u, bitsPerPixel(ctx.frame.psm) / 8u);
    uint32_t off = fbBase + static_cast<uint32_t>(y) * stride + static_cast<uint32_t>(x) * bytesPerPixel;
    if (off + bytesPerPixel > gs->m_vramSize)
        return;

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

    uint32_t mask = ctx.frame.fbmsk;
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
    uint32_t base = tbp0 * 256u;
    uint32_t stride = tbw * 64u * 4u;
    uint32_t off = base + static_cast<uint32_t>(texV) * stride + static_cast<uint32_t>(texU) * 4u;
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
    uint32_t base = tbp0 * 256u;
    uint32_t stride = tbw * 64u * 2u;
    uint32_t off = base + static_cast<uint32_t>(texV) * stride + static_cast<uint32_t>(texU) * 2u;
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
    uint32_t clutBase = cbp * 256u;
    const uint32_t clutIndex = resolveClutIndex(index, csm, csa, sourcePsm);

    if (cpsm == GS_PSM_CT32 || cpsm == GS_PSM_CT24)
    {
        uint32_t off = clutBase + clutIndex * 4u;
        if (off + 4 > gs->m_vramSize)
            return 0xFFFF00FFu;
        uint32_t color;
        std::memcpy(&color, gs->m_vram + off, 4);
        return color;
    }

    if (cpsm == GS_PSM_CT16 || cpsm == GS_PSM_CT16S)
    {
        uint32_t off = clutBase + clutIndex * 2u;
        if (off + 2 > gs->m_vramSize)
            return 0xFFFF00FFu;
        uint16_t c16;
        std::memcpy(&c16, gs->m_vram + off, 2);
        uint32_t r = ((c16 >> 0) & 0x1Fu) << 3;
        uint32_t g = ((c16 >> 5) & 0x1Fu) << 3;
        uint32_t b = ((c16 >> 10) & 0x1Fu) << 3;
        uint32_t a = (c16 & 0x8000u) ? 0x80u : 0u;
        return r | (g << 8) | (b << 16) | (a << 24);
    }

    return 0xFFFF00FFu;
}

uint32_t GSRasterizer::sampleTexture(GS *gs, float s, float t, float q, uint16_t u, uint16_t v)
{
    const auto &ctx = gs->activeContext();
    const auto &tex = ctx.tex0;

    int texW = 1 << tex.tw;
    int texH = 1 << tex.th;

    int texU, texV;
    if (gs->m_prim.fst)
    {
        texU = static_cast<int>(u >> 4);
        texV = static_cast<int>(v >> 4);
    }
    else
    {
        const float invQ = 1.0f / fabsQ(q);
        texU = static_cast<int>(s * invQ * static_cast<float>(texW));
        texV = static_cast<int>(t * invQ * static_cast<float>(texH));
    }

    texU = clampInt(texU, 0, texW - 1);
    texV = clampInt(texV, 0, texH - 1);

    if (tex.psm == GS_PSM_CT32 || tex.psm == GS_PSM_CT24)
        return readTexelPSMCT32(gs, tex.tbp0, tex.tbw, texU, texV);

    if (tex.psm == GS_PSM_CT16 || tex.psm == GS_PSM_CT16S)
        return readTexelPSMCT16(gs, tex.tbp0, tex.tbw, texU, texV);

    if (tex.psm == GS_PSM_T4)
    {
        uint32_t idx = readTexelPSMT4(gs, tex.tbp0, tex.tbw, texU, texV);
        return lookupCLUT(gs, static_cast<uint8_t>(idx), tex.cbp, tex.cpsm, tex.csm, tex.csa, tex.psm);
    }

    if (tex.psm == GS_PSM_T8)
    {
        if (tex.tbw == 0)
            return 0xFFFF00FFu;
        uint32_t off = GSPSMT8::addrPSMT8(tex.tbp0, tex.tbw, static_cast<uint32_t>(texU), static_cast<uint32_t>(texV));
        if (off >= gs->m_vramSize)
            return 0xFFFF00FFu;
        uint8_t idx = gs->m_vram[off];
        return lookupCLUT(gs, idx, tex.cbp, tex.cpsm, tex.csm, tex.csa, tex.psm);
    }

    return 0xFFFF00FFu;
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

    // If the sprite rectangle is fully outside scissor, nothing should render.
    if (x1 < ctx.scissor.x0 || x0 > ctx.scissor.x1 ||
        y1 < ctx.scissor.y0 || y0 > ctx.scissor.y1)
    {
        // maybe a log here idk ?
        return;
    }

    x0 = clampInt(x0, ctx.scissor.x0, ctx.scissor.x1);
    y0 = clampInt(y0, ctx.scissor.y0, ctx.scissor.y1);
    x1 = clampInt(x1, ctx.scissor.x0, ctx.scissor.x1);
    y1 = clampInt(y1, ctx.scissor.y0, ctx.scissor.y1);

    uint8_t r = v1.r, g = v1.g, b = v1.b, a = v1.a;

    if (gs->m_prim.tme)
    {
        const auto &tex = ctx.tex0;
        const bool debugCvFont =
            (tex.tbp0 != 0u &&
             s_debugCvFontPrimitiveCount.load(std::memory_order_relaxed) < 32u);
        int texW = 1 << tex.tw;
        int texH = 1 << tex.th;
        if (texW == 0)
            texW = 1;
        if (texH == 0)
            texH = 1;

        if (debugCvFont)
        {
            const uint32_t primIndex = s_debugCvFontPrimitiveCount.fetch_add(1u, std::memory_order_relaxed);
            if (primIndex < 16u)
            {
                if (tex.psm == GS_PSM_T4 || tex.psm == GS_PSM_T8)
                {
                    dumpFontClut(gs->m_vram, gs->m_vramSize, tex.cbp);
                }
                std::cout << "[cvfont:prim] idx=" << primIndex
                          << " rect=(" << x0 << "," << y0 << ")-(" << x1 << "," << y1 << ")"
                          << " uv0=(" << (v0.u >> 4) << "," << (v0.v >> 4) << ")"
                          << " uv1=(" << (v1.u >> 4) << "," << (v1.v >> 4) << ")"
                          << " tex=("
                          << "tbp0=" << tex.tbp0
                          << " tbw=" << static_cast<uint32_t>(tex.tbw)
                          << " psm=0x" << std::hex << static_cast<uint32_t>(tex.psm) << std::dec
                          << " cbp=" << tex.cbp
                          << " cpsm=0x" << std::hex << static_cast<uint32_t>(tex.cpsm) << std::dec
                          << " csm=" << static_cast<uint32_t>(tex.csm)
                          << " csa=" << static_cast<uint32_t>(tex.csa)
                          << ")"
                          << " rgba=(" << static_cast<uint32_t>(r) << ","
                          << static_cast<uint32_t>(g) << ","
                          << static_cast<uint32_t>(b) << ","
                          << static_cast<uint32_t>(a) << ")"
                          << " test=0x" << std::hex << ctx.test
                          << " alpha=0x" << ctx.alpha
                          << std::dec << std::endl;
            }
        }

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

        float spriteW = static_cast<float>(x1 - x0);
        float spriteH = static_cast<float>(y1 - y0);
        if (spriteW < 1.0f)
            spriteW = 1.0f;
        if (spriteH < 1.0f)
            spriteH = 1.0f;

        for (int y = y0; y <= y1; ++y)
        {
            float ty = (static_cast<float>(y - y0) + 0.5f) / spriteH;
            float texVf = v0f + (v1f - v0f) * ty;
            int tv = clampInt(static_cast<int>(texVf), 0, texH - 1);

            for (int x = x0; x <= x1; ++x)
            {
                float tx = (static_cast<float>(x - x0) + 0.5f) / spriteW;
                float texUf = u0f + (u1f - u0f) * tx;
                int tu = clampInt(static_cast<int>(texUf), 0, texW - 1);

                uint32_t texel;
                uint32_t sampleIndexValue = 0u;
                if (tex.psm == GS_PSM_CT32 || tex.psm == GS_PSM_CT24)
                    texel = readTexelPSMCT32(gs, tex.tbp0, tex.tbw, tu, tv);
                else if (tex.psm == GS_PSM_CT16 || tex.psm == GS_PSM_CT16S)
                    texel = readTexelPSMCT16(gs, tex.tbp0, tex.tbw, tu, tv);
                else if (tex.psm == GS_PSM_T4)
                {
                    sampleIndexValue = readTexelPSMT4(gs, tex.tbp0, tex.tbw, tu, tv);
                    texel = lookupCLUT(gs, static_cast<uint8_t>(sampleIndexValue), tex.cbp, tex.cpsm, tex.csm, tex.csa, tex.psm);
                }
                else if (tex.psm == GS_PSM_T8)
                {
                    uint32_t off = GSPSMT8::addrPSMT8(tex.tbp0, tex.tbw ? tex.tbw : 1u,
                                                      static_cast<uint32_t>(tu), static_cast<uint32_t>(tv));
                    sampleIndexValue = (off < gs->m_vramSize) ? gs->m_vram[off] : 0u;
                    texel = lookupCLUT(gs, static_cast<uint8_t>(sampleIndexValue), tex.cbp, tex.cpsm, tex.csm, tex.csa, tex.psm);
                }
                else
                    texel = 0xFFFF00FFu;

                uint8_t tr = static_cast<uint8_t>(texel & 0xFF);
                uint8_t tg = static_cast<uint8_t>((texel >> 8) & 0xFF);
                uint8_t tb = static_cast<uint8_t>((texel >> 16) & 0xFF);
                uint8_t ta = static_cast<uint8_t>((texel >> 24) & 0xFF);

                const TextureCombineResult color = combineTexture(tex, r, g, b, a, tr, tg, tb, ta);
                if (debugCvFont)
                {
                    const uint32_t debugSample = s_debugCvFontSampleCount.fetch_add(1u, std::memory_order_relaxed);
                    if (debugSample < 48u)
                    {
                        std::cout << "[cvfont:sample] idx=" << debugSample
                                  << " xy=(" << x << "," << y << ")"
                                  << " uv=(" << tu << "," << tv << ")"
                                  << " clutIdx=" << sampleIndexValue
                                  << " texel=(" << static_cast<uint32_t>(tr) << ","
                                  << static_cast<uint32_t>(tg) << ","
                                  << static_cast<uint32_t>(tb) << ","
                                  << static_cast<uint32_t>(ta) << ")"
                                  << " out=(" << static_cast<uint32_t>(color.r) << ","
                                  << static_cast<uint32_t>(color.g) << ","
                                  << static_cast<uint32_t>(color.b) << ","
                                  << static_cast<uint32_t>(color.a) << ")"
                                  << " passA=" << static_cast<uint32_t>(passesAlphaTest(ctx.test, color.a) ? 1u : 0u) << std::endl;
                    }
                }
                writePixel(gs, x, y, color.r, color.g, color.b, color.a);
            }
        }
    }
    else
    {
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
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
    const bool debugCvFontTriangle = gs->m_prim.tme && isVeronicaWarningFont(ctx.tex0);

    if (debugCvFontTriangle)
    {
        const uint32_t primIndex = s_debugCvFontTrianglePrimitiveCount.fetch_add(1u, std::memory_order_relaxed);
        if (primIndex < 24u)
        {
            dumpFontClut(gs->m_vram, gs->m_vramSize, ctx.tex0.cbp);
            std::cout << "[cvfont:tri-prim] idx=" << primIndex
                      << " box=(" << minX << "," << minY << ")-(" << maxX << "," << maxY << ")"
                      << " tex=("
                      << "tbp0=" << ctx.tex0.tbp0
                      << " tbw=" << static_cast<uint32_t>(ctx.tex0.tbw)
                      << " psm=0x" << std::hex << static_cast<uint32_t>(ctx.tex0.psm) << std::dec
                      << " cbp=" << ctx.tex0.cbp
                      << " cpsm=0x" << std::hex << static_cast<uint32_t>(ctx.tex0.cpsm) << std::dec
                      << " csm=" << static_cast<uint32_t>(ctx.tex0.csm)
                      << " csa=" << static_cast<uint32_t>(ctx.tex0.csa)
                      << ")"
                      << " v0=(" << fx0 << "," << fy0 << ")"
                      << " stq0=(" << v0.s << "," << v0.t << "," << v0.q << ")"
                      << " rgba0=(" << static_cast<uint32_t>(v0.r) << ","
                      << static_cast<uint32_t>(v0.g) << ","
                      << static_cast<uint32_t>(v0.b) << ","
                      << static_cast<uint32_t>(v0.a) << ")"
                      << " v1=(" << fx1 << "," << fy1 << ")"
                      << " stq1=(" << v1.s << "," << v1.t << "," << v1.q << ")"
                      << " rgba1=(" << static_cast<uint32_t>(v1.r) << ","
                      << static_cast<uint32_t>(v1.g) << ","
                      << static_cast<uint32_t>(v1.b) << ","
                      << static_cast<uint32_t>(v1.a) << ")"
                      << " v2=(" << fx2 << "," << fy2 << ")"
                      << " stq2=(" << v2.s << "," << v2.t << "," << v2.q << ")"
                      << " rgba2=(" << static_cast<uint32_t>(v2.r) << ","
                      << static_cast<uint32_t>(v2.g) << ","
                      << static_cast<uint32_t>(v2.b) << ","
                      << static_cast<uint32_t>(v2.a) << ")"
                      << " test=0x" << std::hex << ctx.test
                      << " alpha=0x" << ctx.alpha
                      << std::dec << std::endl;
        }
    }

    for (int y = minY; y <= maxY; ++y)
    {
        float py = static_cast<float>(y) + 0.5f;
        for (int x = minX; x <= maxX; ++x)
        {
            float px = static_cast<float>(x) + 0.5f;

            float w0 = (((fy1 - fy2) * (px - fx2) + (fx2 - fx1) * (py - fy2)) * winding) * invAbsDenom;
            float w1 = (((fy2 - fy0) * (px - fx2) + (fx0 - fx2) * (py - fy2)) * winding) * invAbsDenom;
            float w2 = 1.0f - w0 - w1;

            if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f)
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
                int debugTexU = -1;
                int debugTexV = -1;
                uint32_t debugRawIndex = 0u;
                uint32_t debugPhysicalClutIndex = 0u;
                uint32_t debugTexel = 0u;
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

                uint32_t texel = 0u;
                if (debugCvFontTriangle)
                {
                    const auto &tex = ctx.tex0;
                    int texW = 1 << tex.tw;
                    int texH = 1 << tex.th;
                    if (texW == 0)
                        texW = 1;
                    if (texH == 0)
                        texH = 1;

                    if (gs->m_prim.fst)
                    {
                        debugTexU = clampInt(static_cast<int>(iu >> 4), 0, texW - 1);
                        debugTexV = clampInt(static_cast<int>(iv >> 4), 0, texH - 1);
                    }
                    else
                    {
                        const float invQ = 1.0f / fabsQ(iq);
                        debugTexU = clampInt(static_cast<int>(is * invQ * static_cast<float>(texW)), 0, texW - 1);
                        debugTexV = clampInt(static_cast<int>(it * invQ * static_cast<float>(texH)), 0, texH - 1);
                    }

                    if (tex.psm == GS_PSM_T4)
                    {
                        const uint32_t debugNibbleAddr = GSPSMT4::addrPSMT4(tex.tbp0,
                                                                            tex.tbw ? tex.tbw : 1u,
                                                                            static_cast<uint32_t>(debugTexU),
                                                                            static_cast<uint32_t>(debugTexV));
                        const uint32_t debugByteOff = debugNibbleAddr >> 1;
                        const uint8_t debugPackedByte =
                            (debugByteOff < gs->m_vramSize) ? gs->m_vram[debugByteOff] : 0u;
                        const uint32_t debugShift = (debugNibbleAddr & 1u) << 2;
                        const uint32_t debugAltShift = ((debugNibbleAddr ^ 1u) & 1u) << 2;
                        const uint32_t debugAltRawIndex = (debugPackedByte >> debugAltShift) & 0xFu;

                        debugRawIndex = (debugPackedByte >> debugShift) & 0xFu;
                        debugPhysicalClutIndex = resolveClutIndex(static_cast<uint8_t>(debugRawIndex),
                                                                  tex.csm,
                                                                  tex.csa,
                                                                  tex.psm);
                        const uint32_t debugAltPhysicalClutIndex =
                            resolveClutIndex(static_cast<uint8_t>(debugAltRawIndex),
                                             tex.csm,
                                             tex.csa,
                                             tex.psm);
                        debugTexel = lookupCLUT(gs,
                                                static_cast<uint8_t>(debugRawIndex),
                                                tex.cbp,
                                                tex.cpsm,
                                                tex.csm,
                                                tex.csa,
                                                tex.psm);
                        const uint32_t debugAltTexel = lookupCLUT(gs,
                                                                  static_cast<uint8_t>(debugAltRawIndex),
                                                                  tex.cbp,
                                                                  tex.cpsm,
                                                                  tex.csm,
                                                                  tex.csa,
                                                                  tex.psm);

                        if (s_debugCvFontTriangleSampleCount.load(std::memory_order_relaxed) < 96u)
                        {
                            std::cout << "[cvfont:tri-fetch]"
                                      << " uv=(" << debugTexU << "," << debugTexV << ")"
                                      << " nibbleAddr=" << debugNibbleAddr
                                      << " byteOff=0x" << std::hex << debugByteOff << std::dec
                                      << " packed=0x" << std::hex << static_cast<uint32_t>(debugPackedByte) << std::dec
                                      << " nibbleSel=" << (debugNibbleAddr & 1u)
                                      << " rawIdx=" << debugRawIndex
                                      << " altIdx=" << debugAltRawIndex
                                      << " physClut=" << debugPhysicalClutIndex
                                      << " altPhysClut=" << debugAltPhysicalClutIndex
                                      << " texelA=(" << static_cast<uint32_t>(debugTexel & 0xFFu) << ","
                                      << static_cast<uint32_t>((debugTexel >> 8) & 0xFFu) << ","
                                      << static_cast<uint32_t>((debugTexel >> 16) & 0xFFu) << ","
                                      << static_cast<uint32_t>((debugTexel >> 24) & 0xFFu) << ")"
                                      << " texelB=(" << static_cast<uint32_t>(debugAltTexel & 0xFFu) << ","
                                      << static_cast<uint32_t>((debugAltTexel >> 8) & 0xFFu) << ","
                                      << static_cast<uint32_t>((debugAltTexel >> 16) & 0xFFu) << ","
                                      << static_cast<uint32_t>((debugAltTexel >> 24) & 0xFFu) << ")" << std::endl;
                        }
                    }
                    else if (tex.psm == GS_PSM_T8)
                    {
                        const uint32_t off = GSPSMT8::addrPSMT8(tex.tbp0,
                                                                tex.tbw ? tex.tbw : 1u,
                                                                static_cast<uint32_t>(debugTexU),
                                                                static_cast<uint32_t>(debugTexV));
                        debugRawIndex = (off < gs->m_vramSize) ? gs->m_vram[off] : 0u;
                        debugPhysicalClutIndex = resolveClutIndex(static_cast<uint8_t>(debugRawIndex),
                                                                  tex.csm,
                                                                  tex.csa,
                                                                  tex.psm);
                        debugTexel = lookupCLUT(gs,
                                                static_cast<uint8_t>(debugRawIndex),
                                                tex.cbp,
                                                tex.cpsm,
                                                tex.csm,
                                                tex.csa,
                                                tex.psm);
                    }
                    else
                    {
                        debugTexel = sampleTexture(gs, is, it, iq, iu, iv);
                    }

                    texel = debugTexel;
                }
                else
                {
                    texel = sampleTexture(gs, is, it, iq, iu, iv);
                }

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

                if (debugCvFontTriangle)
                {
                    const uint32_t debugSample = s_debugCvFontTriangleSampleCount.fetch_add(1u, std::memory_order_relaxed);
                    if (debugSample < 96u)
                    {
                        uint32_t dstBefore = 0u;
                        if (ctx.frame.psm == GS_PSM_CT16 || ctx.frame.psm == GS_PSM_CT16S)
                        {
                            const uint32_t base = ctx.frame.fbp * 8192u;
                            const uint32_t stride = fbStride(ctx.frame.fbw, ctx.frame.psm);
                            const uint32_t off = base + static_cast<uint32_t>(y) * stride + static_cast<uint32_t>(x) * 2u;
                            if (off + 2u <= gs->m_vramSize)
                            {
                                uint16_t packed = 0u;
                                std::memcpy(&packed, gs->m_vram + off, sizeof(packed));
                                dstBefore = decodePSMCT16(packed);
                            }
                        }
                        else
                        {
                            const uint32_t base = ctx.frame.fbp * 8192u;
                            const uint32_t stride = fbStride(ctx.frame.fbw, ctx.frame.psm);
                            const uint32_t off = base + static_cast<uint32_t>(y) * stride + static_cast<uint32_t>(x) * 4u;
                            if (off + 4u <= gs->m_vramSize)
                                std::memcpy(&dstBefore, gs->m_vram + off, sizeof(dstBefore));
                        }

                        const AlphaTestResult alphaTest = classifyAlphaTest(ctx.test, color.a);
                        std::cout << "[cvfont:tri-sample] idx=" << debugSample
                                  << " xy=(" << x << "," << y << ")"
                                  << " stq=(" << is << "," << it << "," << iq << ")"
                                  << " texUV=(" << debugTexU << "," << debugTexV << ")"
                                  << " rawIdx=" << debugRawIndex
                                  << " physClut=" << debugPhysicalClutIndex
                                  << " shade=(" << static_cast<uint32_t>(shadeR) << ","
                                  << static_cast<uint32_t>(shadeG) << ","
                                  << static_cast<uint32_t>(shadeB) << ","
                                  << static_cast<uint32_t>(shadeA) << ")"
                                  << " texel=(" << static_cast<uint32_t>(tr) << ","
                                  << static_cast<uint32_t>(tg) << ","
                                  << static_cast<uint32_t>(tb) << ","
                                  << static_cast<uint32_t>(ta) << ")"
                                  << " out=(" << static_cast<uint32_t>(color.r) << ","
                                  << static_cast<uint32_t>(color.g) << ","
                                  << static_cast<uint32_t>(color.b) << ","
                                  << static_cast<uint32_t>(color.a) << ")"
                                  << " dstBefore=(" << static_cast<uint32_t>(dstBefore & 0xFFu) << ","
                                  << static_cast<uint32_t>((dstBefore >> 8) & 0xFFu) << ","
                                  << static_cast<uint32_t>((dstBefore >> 16) & 0xFFu) << ","
                                  << static_cast<uint32_t>((dstBefore >> 24) & 0xFFu) << ")"
                                  << " alphaWrite=" << static_cast<uint32_t>(alphaTest.writeFramebuffer ? 1u : 0u)
                                  << " keepDstA=" << static_cast<uint32_t>(alphaTest.preserveDestinationAlpha ? 1u : 0u) << std::endl;
                    }
                }

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
