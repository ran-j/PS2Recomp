#include "ps2_gs_rasterizer.h"
#include "ps2_gs_gpu.h"
#include "ps2_gs_common.h"
#include "ps2_gs_psmt4.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace GSInternal;

void GSRasterizer::drawPrimitive(GS *gs)
{
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

    uint32_t fbBase = ctx.frame.fbp * 8192u;
    uint32_t stride = fbStride(ctx.frame.fbw, ctx.frame.psm);
    if (stride == 0) return;

    uint32_t off = fbBase + static_cast<uint32_t>(y) * stride + static_cast<uint32_t>(x) * 4u;
    if (off + 4 > gs->m_vramSize)
        return;

    if (gs->m_prim.abe)
    {
        uint32_t existing;
        std::memcpy(&existing, gs->m_vram + off, 4);
        uint8_t dr = existing & 0xFF;
        uint8_t dg = (existing >> 8) & 0xFF;
        uint8_t db = (existing >> 16) & 0xFF;
        uint8_t da = (existing >> 24) & 0xFF;

        uint64_t alphaReg = ctx.alpha;
        uint8_t asel = alphaReg & 3;
        uint8_t bsel = (alphaReg >> 2) & 3;
        uint8_t csel = (alphaReg >> 4) & 3;
        uint8_t dsel = (alphaReg >> 6) & 3;
        uint8_t fix  = static_cast<uint8_t>((alphaReg >> 32) & 0xFF);

        auto pickRGB = [&](uint8_t sel, int cs, int cd) -> int {
            if (sel == 0) return cs;
            if (sel == 1) return cd;
            return 0;
        };
        int cAlpha = (csel == 0) ? a : (csel == 1) ? da : fix;

        r = clampU8(((pickRGB(asel, r, dr) - pickRGB(bsel, r, dr)) * cAlpha >> 7) + pickRGB(dsel, r, dr));
        g = clampU8(((pickRGB(asel, g, dg) - pickRGB(bsel, g, dg)) * cAlpha >> 7) + pickRGB(dsel, g, dg));
        b = clampU8(((pickRGB(asel, b, db) - pickRGB(bsel, b, db)) * cAlpha >> 7) + pickRGB(dsel, b, db));
    }

    uint32_t pixel = static_cast<uint32_t>(r)
                   | (static_cast<uint32_t>(g) << 8)
                   | (static_cast<uint32_t>(b) << 16)
                   | (static_cast<uint32_t>(a) << 24);

    uint32_t mask = ctx.frame.fbmsk;
    if (mask != 0)
    {
        uint32_t existing;
        std::memcpy(&existing, gs->m_vram + off, 4);
        pixel = (pixel & ~mask) | (existing & mask);
    }

    std::memcpy(gs->m_vram + off, &pixel, 4);
}

uint32_t GSRasterizer::readTexelPSMCT32(GS *gs, uint32_t tbp0, uint32_t tbw, int texU, int texV)
{
    if (tbw == 0) tbw = 1;
    uint32_t base = tbp0 * 256u;
    uint32_t stride = tbw * 64u * 4u;
    uint32_t off = base + static_cast<uint32_t>(texV) * stride + static_cast<uint32_t>(texU) * 4u;
    if (off + 4 > gs->m_vramSize)
        return 0xFFFF00FFu;
    uint32_t texel;
    std::memcpy(&texel, gs->m_vram + off, 4);
    return texel;
}

uint32_t GSRasterizer::readTexelPSMT4(GS *gs, uint32_t tbp0, uint32_t tbw, int texU, int texV)
{
    if (tbw == 0) tbw = 1;
    uint32_t nibbleAddr = GSPSMT4::addrPSMT4(tbp0, tbw, static_cast<uint32_t>(texU), static_cast<uint32_t>(texV));
    uint32_t byteOff = nibbleAddr >> 1;
    if (byteOff >= gs->m_vramSize)
        return 0;
    uint8_t packed = gs->m_vram[byteOff];
    uint32_t shift = (nibbleAddr & 1u) << 2;
    uint32_t idx = (packed >> shift) & 0xFu;
    return idx;
}

uint32_t GSRasterizer::lookupCLUT(GS *gs, uint8_t index, uint32_t cbp, uint8_t cpsm, uint8_t csa)
{
    uint32_t clutBase = cbp * 256u;

    if (cpsm == GS_PSM_CT32 || cpsm == GS_PSM_CT24)
    {
        uint32_t off = clutBase + (static_cast<uint32_t>(csa) * 16u + index) * 4u;
        if (off + 4 > gs->m_vramSize)
            return 0xFFFF00FFu;
        uint32_t color;
        std::memcpy(&color, gs->m_vram + off, 4);
        return color;
    }

    if (cpsm == GS_PSM_CT16 || cpsm == GS_PSM_CT16S)
    {
        uint32_t off = clutBase + (static_cast<uint32_t>(csa) * 16u + index) * 2u;
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

uint32_t GSRasterizer::sampleTexture(GS *gs, float s, float t, uint16_t u, uint16_t v)
{
    const auto &ctx = gs->activeContext();
    const auto &tex = ctx.tex0;

    int texW = 1 << tex.tw;
    int texH = 1 << tex.th;
    if (texW == 0) texW = 1;
    if (texH == 0) texH = 1;

    int texU, texV;
    if (gs->m_prim.fst)
    {
        texU = static_cast<int>(u >> 4);
        texV = static_cast<int>(v >> 4);
    }
    else
    {
        float invQ = (gs->m_curQ != 0.0f) ? (1.0f / gs->m_curQ) : 1.0f;
        texU = static_cast<int>(s * invQ * static_cast<float>(texW));
        texV = static_cast<int>(t * invQ * static_cast<float>(texH));
    }

    texU = clampInt(texU, 0, texW - 1);
    texV = clampInt(texV, 0, texH - 1);

    if (tex.psm == GS_PSM_CT32 || tex.psm == GS_PSM_CT24)
        return readTexelPSMCT32(gs, tex.tbp0, tex.tbw, texU, texV);

    if (tex.psm == GS_PSM_T4)
    {
        uint32_t idx = readTexelPSMT4(gs, tex.tbp0, tex.tbw, texU, texV);
        return lookupCLUT(gs, static_cast<uint8_t>(idx), tex.cbp, tex.cpsm, tex.csa);
    }

    if (tex.psm == GS_PSM_T8)
    {
        if (tex.tbw == 0) return 0xFFFF00FFu;
        uint32_t base = tex.tbp0 * 256u;
        uint32_t stride = static_cast<uint32_t>(tex.tbw) * 64u;
        uint32_t off = base + static_cast<uint32_t>(texV) * stride + static_cast<uint32_t>(texU);
        if (off >= gs->m_vramSize) return 0xFFFF00FFu;
        uint8_t idx = gs->m_vram[off];
        return lookupCLUT(gs, idx, tex.cbp, tex.cpsm, tex.csa);
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

    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);

    x0 = clampInt(x0, ctx.scissor.x0, ctx.scissor.x1);
    y0 = clampInt(y0, ctx.scissor.y0, ctx.scissor.y1);
    x1 = clampInt(x1, ctx.scissor.x0, ctx.scissor.x1);
    y1 = clampInt(y1, ctx.scissor.y0, ctx.scissor.y1);

    uint8_t r = v1.r, g = v1.g, b = v1.b, a = v1.a;

    if (gs->m_prim.tme)
    {
        const auto &tex = ctx.tex0;
        int texW = 1 << tex.tw;
        int texH = 1 << tex.th;
        if (texW == 0) texW = 1;
        if (texH == 0) texH = 1;

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
            u0f = v0.s * static_cast<float>(texW);
            v0f = v0.t * static_cast<float>(texH);
            u1f = v1.s * static_cast<float>(texW);
            v1f = v1.t * static_cast<float>(texH);
        }

        float spriteW = static_cast<float>(x1 - x0);
        float spriteH = static_cast<float>(y1 - y0);
        if (spriteW < 1.0f) spriteW = 1.0f;
        if (spriteH < 1.0f) spriteH = 1.0f;

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
                if (tex.psm == GS_PSM_CT32 || tex.psm == GS_PSM_CT24)
                    texel = readTexelPSMCT32(gs, tex.tbp0, tex.tbw, tu, tv);
                else if (tex.psm == GS_PSM_T4)
                {
                    uint32_t idx = readTexelPSMT4(gs, tex.tbp0, tex.tbw, tu, tv);
                    texel = lookupCLUT(gs, static_cast<uint8_t>(idx), tex.cbp, tex.cpsm, tex.csa);
                }
                else if (tex.psm == GS_PSM_T8)
                {
                    uint32_t base = tex.tbp0 * 256u;
                    uint32_t tbw = tex.tbw ? tex.tbw : 1u;
                    uint32_t stride = tbw * 64u;
                    uint32_t off = base + static_cast<uint32_t>(tv) * stride + static_cast<uint32_t>(tu);
                    uint8_t idx = (off < gs->m_vramSize) ? gs->m_vram[off] : 0;
                    texel = lookupCLUT(gs, idx, tex.cbp, tex.cpsm, tex.csa);
                }
                else
                    texel = 0xFFFF00FFu;

                uint8_t tr = static_cast<uint8_t>(texel & 0xFF);
                uint8_t tg = static_cast<uint8_t>((texel >> 8) & 0xFF);
                uint8_t tb = static_cast<uint8_t>((texel >> 16) & 0xFF);
                uint8_t ta = static_cast<uint8_t>((texel >> 24) & 0xFF);

                uint8_t fr, fg, fb, fa;
                if (tex.tfx == 0)
                {
                    fr = clampU8((tr * r) >> 7);
                    fg = clampU8((tg * g) >> 7);
                    fb = clampU8((tb * b) >> 7);
                    fa = ta;
                }
                else if (tex.tfx == 1)
                {
                    fr = tr; fg = tg; fb = tb; fa = ta;
                }
                else
                {
                    fr = clampU8((tr * r) >> 7);
                    fg = clampU8((tg * g) >> 7);
                    fb = clampU8((tb * b) >> 7);
                    fa = ta;
                }

                writePixel(gs, x, y, fr, fg, fb, fa);
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

    float invDenom = 1.0f / denom;

    for (int y = minY; y <= maxY; ++y)
    {
        float py = static_cast<float>(y) + 0.5f;
        for (int x = minX; x <= maxX; ++x)
        {
            float px = static_cast<float>(x) + 0.5f;

            float w0 = ((fy1 - fy2) * (px - fx2) + (fx2 - fx1) * (py - fy2)) * invDenom;
            float w1 = ((fy2 - fy0) * (px - fx2) + (fx0 - fx2) * (py - fy2)) * invDenom;
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
                r = v2.r; g = v2.g; b = v2.b; a = v2.a;
            }

            if (gs->m_prim.tme)
            {
                float is, it;
                uint16_t iu, iv;
                if (gs->m_prim.fst)
                {
                    iu = static_cast<uint16_t>(v0.u * w0 + v1.u * w1 + v2.u * w2);
                    iv = static_cast<uint16_t>(v0.v * w0 + v1.v * w1 + v2.v * w2);
                    is = 0; it = 0;
                }
                else
                {
                    is = v0.s * w0 + v1.s * w1 + v2.s * w2;
                    it = v0.t * w0 + v1.t * w1 + v2.t * w2;
                    iu = 0; iv = 0;
                }
                uint32_t texel = sampleTexture(gs, is, it, iu, iv);
                uint8_t tr = static_cast<uint8_t>(texel & 0xFF);
                uint8_t tg = static_cast<uint8_t>((texel >> 8) & 0xFF);
                uint8_t tb = static_cast<uint8_t>((texel >> 16) & 0xFF);
                uint8_t ta = static_cast<uint8_t>((texel >> 24) & 0xFF);

                const auto &tex = ctx.tex0;
                if (tex.tfx == 0)
                {
                    r = clampU8((tr * r) >> 7);
                    g = clampU8((tg * g) >> 7);
                    b = clampU8((tb * b) >> 7);
                    a = ta;
                }
                else if (tex.tfx == 1)
                {
                    r = tr; g = tg; b = tb; a = ta;
                }
                else
                {
                    r = clampU8((tr * r) >> 7);
                    g = clampU8((tg * g) >> 7);
                    b = clampU8((tb * b) >> 7);
                    a = ta;
                }
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
    if (totalSteps == 0) totalSteps = 1;
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
            r = v1.r; g = v1.g; b = v1.b; a = v1.a;
        }

        writePixel(gs, x0, y0, r, g, b, a);

        if (x0 == x1 && y0 == y1)
            break;

        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
        ++step;
    }
}
