// Based on Blackline Interactive implementation
#include "ps2_memory.h"
#include "ps2_gs_gpu.h"
#include <cstring>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <mutex>

enum GSReg : uint8_t
{
    GS_PRIM = 0x00,
    GS_RGBAQ = 0x01,
    GS_ST = 0x02,
    GS_UV = 0x03,
    GS_XYZF2 = 0x04,
    GS_XYZ2 = 0x05,
    GS_TEX0_1 = 0x06,
    GS_TEX0_2 = 0x07,
    GS_CLAMP_1 = 0x08,
    GS_CLAMP_2 = 0x09,
    GS_FOG = 0x0A,
    GS_XYZF3 = 0x0C,
    GS_XYZ3 = 0x0D,
    GS_TEX1_1 = 0x14,
    GS_TEX1_2 = 0x15,
    GS_TEX2_1 = 0x16,
    GS_TEX2_2 = 0x17,
    GS_XYOFFSET_1 = 0x18,
    GS_XYOFFSET_2 = 0x19,
    GS_PRMODECONT = 0x1A,
    GS_PRMODE = 0x1B,
    GS_TEXCLUT = 0x1C,
    GS_SCANMSK = 0x22,
    GS_MIPTBP1_1 = 0x34,
    GS_MIPTBP1_2 = 0x35,
    GS_MIPTBP2_1 = 0x36,
    GS_MIPTBP2_2 = 0x37,
    GS_TEXA = 0x3B,
    GS_FOGCOL = 0x3D,
    GS_TEXFLUSH = 0x3F,
    GS_SCISSOR_1 = 0x40,
    GS_SCISSOR_2 = 0x41,
    GS_ALPHA_1 = 0x42,
    GS_ALPHA_2 = 0x43,
    GS_DIMX = 0x44,
    GS_DTHE = 0x45,
    GS_COLCLAMP = 0x46,
    GS_TEST_1 = 0x47,
    GS_TEST_2 = 0x48,
    GS_PABE = 0x49,
    GS_FBA_1 = 0x4A,
    GS_FBA_2 = 0x4B,
    GS_FRAME_1 = 0x4C,
    GS_FRAME_2 = 0x4D,
    GS_ZBUF_1 = 0x4E,
    GS_ZBUF_2 = 0x4F,
    GS_BITBLTBUF = 0x50,
    GS_TRXPOS = 0x51,
    GS_TRXREG = 0x52,
    GS_TRXDIR = 0x53,
    GS_HWREG = 0x54,
    GS_SIGNAL = 0x60,
    GS_FINISH = 0x61,
    GS_LABEL = 0x62,
};

namespace
{

    struct GSVertex
    {
        float x, y, z;      // screen coords (after 12.4 fixed -> float)
        uint8_t r, g, b, a; // vertex color
        float s, t, q;      // texture coords
        uint16_t u, v;      // UV coords (14.0 fixed)
    };

    struct GSInternalRegs
    {
        // Current primitive
        uint32_t prim = 0;
        // Color
        uint8_t r = 128, g = 128, b = 128, a = 128;
        float q = 1.0f;
        // Texture coords
        float s = 0, t = 0;
        uint16_t u = 0, v = 0;
        // Frame buffer
        uint32_t fbp = 0;   // frame buffer base pointer (in pages)
        uint32_t fbw = 10;  // frame buffer width (64-pixel units)
        uint32_t psm = 0;   // pixel storage mode
        uint32_t fbmsk = 0; // frame buffer write mask
        // Scissor
        uint32_t scax0 = 0, scax1 = 639, scay0 = 0, scay1 = 447;
        // XY offset (12.4 fixed point)
        uint32_t ofx = 0, ofy = 0;
        // Texture
        uint64_t tex0 = 0;
        uint32_t tbp0 = 0;       // texture base pointer
        uint32_t tbw = 0;        // texture buffer width
        uint32_t tpsm = 0;       // texture pixel storage mode
        uint32_t tw = 0, th = 0; // texture width/height (log2)
        // Alpha
        uint64_t alpha = 0;
        // Test
        uint64_t test = 0;
        // BITBLTBUF / TRXPOS / TRXREG / TRXDIR for image transfers
        uint64_t bitbltbuf = 0;
        uint64_t trxpos = 0;
        uint64_t trxreg = 0;
        uint64_t trxdir = 0;
        // Image transfer state
        bool xferActive = false;
        uint32_t xferX = 0, xferY = 0;
        uint32_t xferW = 0, xferH = 0;
        uint32_t xferDBP = 0, xferDBW = 0, xferDPSM = 0;
        uint32_t xferDstX = 0, xferDstY = 0;
        uint32_t xferPixelsWritten = 0;
        // Vertex queue for primitive assembly
        GSVertex vtxQueue[3];
        int vtxCount = 0;
        int vtxKick = 0; // vertices needed for current prim
        // Stats
        uint32_t gifTagsProcessed = 0;
        uint32_t adWrites = 0;
        uint32_t imageQWs = 0;
        uint32_t primsDrawn = 0;
        // PRMODECONT / PRMODE
        uint32_t prmodecont = 1; // 1 = use PRIM bits, 0 = use PRMODE bits
        uint32_t prmode = 0;
    };

    static GSInternalRegs g_gsRegs;
    static std::mutex g_gsRegsMutex; // protects g_gsRegs from game/render thread races
    static int g_gsLogCount = 0;

    // PSMCT24 bit accumulator for HWREG transfers.
    // Each 64-bit write delivers 64 bits; each pixel is 24 bits.
    // We accumulate leftover bits across writes.
    static uint64_t g_psmct24_accBits = 0;
    static int g_psmct24_accCount = 0; // bits currently in accumulator

    // PSMCT32: 4 bytes per pixel, standard layout per block
    // For FRAME register: FBP is in words/2048 => base = fbp * 2048 * 4 bytes
    inline uint32_t gsVramOffset32_Frame(uint32_t fbp, uint32_t fbw, uint32_t x, uint32_t y)
    {
        const uint32_t baseBytes = fbp * 2048u * 4u;
        const uint32_t stride = (fbw ? fbw : 10u) * 64u * 4u; // bytes per row
        return baseBytes + y * stride + x * 4u;
    }

    // For BITBLTBUF/TEX0: BP is in words/64 => base = bp * 64 * 4 bytes
    inline uint32_t gsVramOffset32_BP64(uint32_t bp, uint32_t bw, uint32_t x, uint32_t y)
    {
        const uint32_t baseBytes = bp * 64u * 4u;
        const uint32_t stride = (bw ? bw : 1u) * 64u * 4u;
        return baseBytes + y * stride + x * 4u;
    }

    inline void writePixel32(uint8_t *vram, uint32_t bp, uint32_t bw,
                             uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                             uint32_t vramSize, bool isBP64 = false)
    {
        uint32_t off = isBP64 ? gsVramOffset32_BP64(bp, bw, x, y)
                              : gsVramOffset32_Frame(bp, bw, x, y);
        if (off + 3 < vramSize)
        {
            vram[off + 0] = r;
            vram[off + 1] = g;
            vram[off + 2] = b;
            vram[off + 3] = a;
        }
    }

    inline void readPixel32(const uint8_t *vram, uint32_t bp, uint32_t bw,
                            uint32_t x, uint32_t y, uint8_t &r, uint8_t &g, uint8_t &b, uint8_t &a,
                            uint32_t vramSize, bool isBP64 = false)
    {
        uint32_t off = isBP64 ? gsVramOffset32_BP64(bp, bw, x, y)
                              : gsVramOffset32_Frame(bp, bw, x, y);
        if (off + 3 < vramSize)
        {
            r = vram[off + 0];
            g = vram[off + 1];
            b = vram[off + 2];
            a = vram[off + 3];
        }
        else
        {
            r = g = b = a = 0;
        }
    }

    inline GsGpuVertex toGpuVertex(const GSVertex &v)
    {
        GsGpuVertex gv{};
        gv.x = v.x;
        gv.y = v.y;
        gv.z = v.z;
        gv.r = v.r;
        gv.g = v.g;
        gv.b = v.b;
        gv.a = v.a;
        gv.u = v.s; // TODO: proper tex coord mapping
        gv.v = v.t;
        return gv;
    }

    // Emit a QUAD (axis-aligned rectangle from SPRITE primitive)
    void drawSprite(uint8_t * /*vram*/, uint32_t /*vramSize*/)
    {
        auto &gs = g_gsRegs;
        if (gs.vtxCount < 2)
            return;

        GSVertex &v0 = gs.vtxQueue[0];
        GSVertex &v1 = gs.vtxQueue[1];

        float x0 = std::max(v0.x, (float)gs.scax0);
        float y0 = std::max(v0.y, (float)gs.scay0);
        float x1 = std::min(v1.x, (float)(gs.scax1 + 1));
        float y1 = std::min(v1.y, (float)(gs.scay1 + 1));

        if (x0 >= x1 || y0 >= y1)
            return;

        // Use the second vertex color (PS2 SPRITE convention)
        uint8_t r = v1.r, g = v1.g, b = v1.b, a = v1.a;
        float z = v1.z;

        GsGpuPrimitive prim{};
        prim.type = GS_GPU_QUAD;
        prim.vertexCount = 4;
        // v0=Top-left, v1=Top-right, v2=Bottom-right, v3=Bottom-left
        prim.verts[0] = {x0, y0, z, r, g, b, a, 0.0f, 0.0f};
        prim.verts[1] = {x1, y0, z, r, g, b, a, 1.0f, 0.0f};
        prim.verts[2] = {x1, y1, z, r, g, b, a, 1.0f, 1.0f};
        prim.verts[3] = {x0, y1, z, r, g, b, a, 0.0f, 1.0f};

        gsGpuGetFrameData().pushPrimitive(prim);
        gs.primsDrawn++;
    }

    void drawTriangle(uint8_t * /*vram*/, uint32_t /*vramSize*/)
    {
        auto &gs = g_gsRegs;
        if (gs.vtxCount < 3)
            return;

        bool gouraud;
        {
            // Determine effective primitive attributes (PRMODECONT)
            uint32_t effectivePrim = (gs.prmodecont == 1) ? gs.prim : ((gs.prim & 0x7) | (gs.prmode & ~0x7));
            gouraud = (effectivePrim >> 3) & 1; // IIP bit
        }

        GsGpuPrimitive prim{};
        prim.type = GS_GPU_TRIANGLE;
        prim.vertexCount = 3;

        if (gouraud)
        {
            // Per-vertex colors
            prim.verts[0] = toGpuVertex(gs.vtxQueue[0]);
            prim.verts[1] = toGpuVertex(gs.vtxQueue[1]);
            prim.verts[2] = toGpuVertex(gs.vtxQueue[2]);
        }
        else
        {
            // Flat shading: use last vertex color for all
            prim.verts[0] = toGpuVertex(gs.vtxQueue[0]);
            prim.verts[1] = toGpuVertex(gs.vtxQueue[1]);
            prim.verts[2] = toGpuVertex(gs.vtxQueue[2]);
            // Override colors to match PS2 flat shading (last vertex)
            uint8_t r = gs.vtxQueue[2].r, g = gs.vtxQueue[2].g;
            uint8_t b = gs.vtxQueue[2].b, a = gs.vtxQueue[2].a;
            prim.verts[0].r = r;
            prim.verts[0].g = g;
            prim.verts[0].b = b;
            prim.verts[0].a = a;
            prim.verts[1].r = r;
            prim.verts[1].g = g;
            prim.verts[1].b = b;
            prim.verts[1].a = a;
        }

        gsGpuGetFrameData().pushPrimitive(prim);
        gs.primsDrawn++;
    }

    void drawLine(uint8_t * /*vram*/, uint32_t /*vramSize*/)
    {
        auto &gs = g_gsRegs;
        if (gs.vtxCount < 2)
            return;

        // Determine effective primitive attributes (PRMODECONT)
        uint32_t effectivePrim = (gs.prmodecont == 1) ? gs.prim : ((gs.prim & 0x7) | (gs.prmode & ~0x7));
        bool gouraud = (effectivePrim >> 3) & 1; // IIP bit

        GsGpuPrimitive prim{};
        prim.type = GS_GPU_LINE;
        prim.vertexCount = 2;
        prim.verts[0] = toGpuVertex(gs.vtxQueue[0]);
        prim.verts[1] = toGpuVertex(gs.vtxQueue[1]);

        if (!gouraud)
        {
            // Flat shading: use last vertex color
            prim.verts[0].r = prim.verts[1].r;
            prim.verts[0].g = prim.verts[1].g;
            prim.verts[0].b = prim.verts[1].b;
            prim.verts[0].a = prim.verts[1].a;
        }

        gsGpuGetFrameData().pushPrimitive(prim);
        gs.primsDrawn++;
    }

    // Submit vertex (XYZ2/XYZ3/XYZF2)
    void submitVertex(uint8_t *vram, uint32_t vramSize, bool drawing)
    {
        auto &gs = g_gsRegs;
        int primType = gs.prim & 0x7;

        switch (primType)
        {
        case 0: // POINT
            if (gs.vtxCount >= 1 && drawing)
            {
                GsGpuPrimitive prim{};
                prim.type = GS_GPU_POINT;
                prim.vertexCount = 1;
                prim.verts[0] = toGpuVertex(gs.vtxQueue[0]);
                gsGpuGetFrameData().pushPrimitive(prim);
                gs.primsDrawn++;
                gs.vtxCount = 0;
            }
            break;
        case 1: // LINE
            if (gs.vtxCount >= 2 && drawing)
            {
                drawLine(vram, vramSize);
                gs.vtxCount = 0;
            }
            break;
        case 2: // LINE_STRIP
            if (gs.vtxCount >= 2 && drawing)
            {
                drawLine(vram, vramSize);
                gs.vtxQueue[0] = gs.vtxQueue[1];
                gs.vtxCount = 1;
            }
            break;
        case 3: // TRIANGLE
            if (gs.vtxCount >= 3 && drawing)
            {
                drawTriangle(vram, vramSize);
                gs.vtxCount = 0;
            }
            break;
        case 4: // TRIANGLE_STRIP
            if (gs.vtxCount >= 3 && drawing)
            {
                drawTriangle(vram, vramSize);
                gs.vtxQueue[0] = gs.vtxQueue[1];
                gs.vtxQueue[1] = gs.vtxQueue[2];
                gs.vtxCount = 2;
            }
            break;
        case 5: // TRIANGLE_FAN
            if (gs.vtxCount >= 3 && drawing)
            {
                drawTriangle(vram, vramSize);
                gs.vtxQueue[1] = gs.vtxQueue[2];
                gs.vtxCount = 2;
            }
            break;
        case 6: // SPRITE
            if (gs.vtxCount >= 2 && drawing)
            {
                drawSprite(vram, vramSize);
                gs.vtxCount = 0;
            }
            break;
        default:
            gs.vtxCount = 0;
            break;
        }
    }

    void handleADWrite(uint64_t data, uint8_t reg, uint8_t *vram, uint32_t vramSize,
                       PS2Memory::GSDrawContext &drawCtx)
    {
        auto &gs = g_gsRegs;
        gs.adWrites++;

        switch (reg)
        {
        case GS_PRIM:
            gs.prim = (uint32_t)(data & 0x7FF);
            gs.vtxCount = 0; // reset vertex queue on new prim
            break;

        case GS_RGBAQ:
            gs.r = data & 0xFF;
            gs.g = (data >> 8) & 0xFF;
            gs.b = (data >> 16) & 0xFF;
            gs.a = (data >> 24) & 0xFF;
            // Q is in bits 32-63 as float
            {
                uint32_t qBits = (uint32_t)(data >> 32);
                memcpy(&gs.q, &qBits, 4);
                if (!std::isfinite(gs.q) || gs.q == 0.0f)
                    gs.q = 1.0f;
            }
            break;

        case GS_ST:
        {
            uint32_t sBits = (uint32_t)(data & 0xFFFFFFFF);
            uint32_t tBits = (uint32_t)(data >> 32);
            memcpy(&gs.s, &sBits, 4);
            memcpy(&gs.t, &tBits, 4);
        }
        break;

        case GS_UV:
            gs.u = (uint16_t)(data & 0x3FFF);
            gs.v = (uint16_t)((data >> 16) & 0x3FFF);
            break;

        case GS_XYZ2:
        case GS_XYZ3:
        {
            // XYZ2 triggers drawing kick, XYZ3 does not
            bool drawing = (reg == GS_XYZ2);

            // X,Y are 16-bit 12.4 fixed point
            uint32_t xFix = (uint32_t)(data & 0xFFFF);
            uint32_t yFix = (uint32_t)((data >> 16) & 0xFFFF);
            uint32_t z = (uint32_t)(data >> 32);

            float x = (float)((int32_t)xFix - (drawing ? (int32_t)(gs.ofx & 0xFFFF) : 0)) / 16.0f;
            float y = (float)((int32_t)yFix - (drawing ? (int32_t)(gs.ofy & 0xFFFF) : 0)) / 16.0f;
            float zf = (float)z / 4294967295.0f; // normalize 32-bit Z to [0,1]

            if (gs.vtxCount < 3)
            {
                GSVertex &v = gs.vtxQueue[gs.vtxCount];
                v.x = x;
                v.y = y;
                v.z = zf;
                v.r = gs.r;
                v.g = gs.g;
                v.b = gs.b;
                v.a = gs.a;
                v.s = gs.s;
                v.t = gs.t;
                v.q = gs.q;
                v.u = gs.u;
                v.v = gs.v;
                gs.vtxCount++;
            }

            submitVertex(vram, vramSize, drawing);
            break;
        }

        case GS_XYZF2:
        case GS_XYZF3:
        {
            bool drawing = (reg == GS_XYZF2);
            uint32_t xFix = (uint32_t)(data & 0xFFFF);
            uint32_t yFix = (uint32_t)((data >> 16) & 0xFFFF);
            // Z is bits 32-55, F is bits 56-63
            uint32_t z = (uint32_t)((data >> 32) & 0xFFFFFF);

            float x = (float)((int32_t)xFix - (drawing ? (int32_t)(gs.ofx & 0xFFFF) : 0)) / 16.0f;
            float y = (float)((int32_t)yFix - (drawing ? (int32_t)(gs.ofy & 0xFFFF) : 0)) / 16.0f;
            float zf = (float)z / 16777215.0f; // normalize 24-bit Z to [0,1]

            if (gs.vtxCount < 3)
            {
                GSVertex &v = gs.vtxQueue[gs.vtxCount];
                v.x = x;
                v.y = y;
                v.z = zf;
                v.r = gs.r;
                v.g = gs.g;
                v.b = gs.b;
                v.a = gs.a;
                v.s = gs.s;
                v.t = gs.t;
                v.q = gs.q;
                v.u = gs.u;
                v.v = gs.v;
                gs.vtxCount++;
            }

            submitVertex(vram, vramSize, drawing);
            break;
        }

        case GS_FRAME_1:
        case GS_FRAME_2: // Context 2 — alias to Context 1
            gs.fbp = (uint32_t)(data & 0x1FF);
            gs.fbw = (uint32_t)((data >> 16) & 0x3F);
            gs.psm = (uint32_t)((data >> 24) & 0x3F);
            gs.fbmsk = (uint32_t)(data >> 32);
            break;

        case GS_SCISSOR_1:
        case GS_SCISSOR_2: // Context 2 — alias to Context 1
            gs.scax0 = (uint32_t)(data & 0x7FF);
            gs.scax1 = (uint32_t)((data >> 16) & 0x7FF);
            gs.scay0 = (uint32_t)((data >> 32) & 0x7FF);
            gs.scay1 = (uint32_t)((data >> 48) & 0x7FF);
            break;

        case GS_XYOFFSET_1:
        case GS_XYOFFSET_2: // Context 2 — alias to Context 1
            gs.ofx = (uint32_t)(data & 0xFFFF);
            gs.ofy = (uint32_t)((data >> 32) & 0xFFFF);
            break;

        case GS_TEX0_1:
        case GS_TEX0_2: // Context 2 — alias to Context 1
            gs.tex0 = data;
            gs.tbp0 = (uint32_t)(data & 0x3FFF);
            gs.tbw = (uint32_t)((data >> 14) & 0x3F);
            gs.tpsm = (uint32_t)((data >> 20) & 0x3F);
            gs.tw = (uint32_t)((data >> 26) & 0xF);
            gs.th = (uint32_t)((data >> 30) & 0xF);
            break;

        case GS_ALPHA_1:
        case GS_ALPHA_2:
            gs.alpha = data;
            break;

        case GS_TEST_1:
        case GS_TEST_2:
            gs.test = data;
            break;

        case GS_PRMODECONT:
            gs.prmodecont = (uint32_t)(data & 1);
            break;

        case GS_PRMODE:
            gs.prmode = (uint32_t)(data & 0x7F8); // bits 3-10
            break;

        case GS_BITBLTBUF:
            gs.bitbltbuf = data;
            drawCtx.bitbltbuf = data;
            break;

        case GS_TRXPOS:
            gs.trxpos = data;
            drawCtx.trxpos = data;
            break;

        case GS_TRXREG:
            gs.trxreg = data;
            drawCtx.trxreg = data;
            break;

        case GS_TRXDIR:
        {
            gs.trxdir = data;
            drawCtx.trxdir = data;
            uint32_t dir = data & 3;
            if (dir == 0)
            {
                // Host -> Local (texture upload to GS VRAM)
                gs.xferActive = true;
                gs.xferDBP = (uint32_t)((gs.bitbltbuf >> 32) & 0x3FFF);
                gs.xferDBW = (uint32_t)((gs.bitbltbuf >> 46) & 0x3F);
                gs.xferDPSM = (uint32_t)((gs.bitbltbuf >> 56) & 0x3F);
                gs.xferDstX = (uint32_t)((gs.trxpos >> 32) & 0x7FF);
                gs.xferDstY = (uint32_t)((gs.trxpos >> 48) & 0x7FF);
                gs.xferW = (uint32_t)(gs.trxreg & 0xFFF);
                gs.xferH = (uint32_t)((gs.trxreg >> 32) & 0xFFF);
                gs.xferPixelsWritten = 0;
                gs.xferX = 0;
                gs.xferY = 0;
                // Reset PSMCT24 bit accumulator for new transfer.
                g_psmct24_accBits = 0;
                g_psmct24_accCount = 0;

                drawCtx.xferActive = true;
                drawCtx.xferDBP = gs.xferDBP;
                drawCtx.xferDBW = gs.xferDBW;
                drawCtx.xferDPSM = gs.xferDPSM;
                drawCtx.xferDestX = gs.xferDstX;
                drawCtx.xferDestY = gs.xferDstY;
                drawCtx.xferWidth = gs.xferW;
                drawCtx.xferHeight = gs.xferH;

                if (g_gsLogCount < 20)
                {
                    std::cerr << "[GS] IMAGE xfer start: DBP=" << gs.xferDBP
                              << " DBW=" << gs.xferDBW << " DPSM=" << gs.xferDPSM
                              << " dst=(" << gs.xferDstX << "," << gs.xferDstY << ")"
                              << " size=" << gs.xferW << "x" << gs.xferH << std::endl;
                    g_gsLogCount++;
                }
                drawCtx.imageTransfers++;
            }
            else if (dir == 1)
            {
                // Local -> Host (readback) - not needed for rendering
                gs.xferActive = false;
            }
            else if (dir == 2)
            {
                // Local -> Local (VRAM copy)
                uint32_t sbp = (uint32_t)(gs.bitbltbuf & 0x3FFF);
                uint32_t sbw = (uint32_t)((gs.bitbltbuf >> 14) & 0x3F);
                uint32_t spsm = (uint32_t)((gs.bitbltbuf >> 24) & 0x3F);
                uint32_t dbp = (uint32_t)((gs.bitbltbuf >> 32) & 0x3FFF);
                uint32_t dbw = (uint32_t)((gs.bitbltbuf >> 46) & 0x3F);
                uint32_t dpsm = (uint32_t)((gs.bitbltbuf >> 56) & 0x3F);
                uint32_t sx = (uint32_t)(gs.trxpos & 0x7FF);
                uint32_t sy = (uint32_t)((gs.trxpos >> 16) & 0x7FF);
                uint32_t dx = (uint32_t)((gs.trxpos >> 32) & 0x7FF);
                uint32_t dy = (uint32_t)((gs.trxpos >> 48) & 0x7FF);
                uint32_t w = (uint32_t)(gs.trxreg & 0xFFF);
                uint32_t h = (uint32_t)((gs.trxreg >> 32) & 0xFFF);

                if (spsm == 0 && dpsm == 0 && w > 0 && h > 0)
                {
                    // PSMCT32 copy — sbp/dbp are BP64 units (from BITBLTBUF)
                    for (uint32_t row = 0; row < h; row++)
                    {
                        for (uint32_t col = 0; col < w; col++)
                        {
                            uint8_t r, g, b, a;
                            readPixel32(vram, sbp, sbw, sx + col, sy + row, r, g, b, a, vramSize, true);
                            writePixel32(vram, dbp, dbw, dx + col, dy + row, r, g, b, a, vramSize, true);
                        }
                    }
                }
                gs.xferActive = false;
            }
            break;
        }

        case GS_HWREG:
        {
            // Image data for Host -> Local transfer
            if (!gs.xferActive)
                break;

            // Each HWREG write delivers 64 bits of pixel data
            // For PSMCT32: 2 pixels per write
            if (gs.xferDPSM == 0)
            {
                // PSMCT32: 2 pixels per 64-bit write
                for (int p = 0; p < 2 && gs.xferY < gs.xferH; p++)
                {
                    uint32_t pixel = (p == 0) ? (uint32_t)(data & 0xFFFFFFFF) : (uint32_t)(data >> 32);
                    uint32_t dx = gs.xferDstX + gs.xferX;
                    uint32_t dy = gs.xferDstY + gs.xferY;
                    writePixel32(vram, gs.xferDBP, gs.xferDBW, dx, dy,
                                 pixel & 0xFF, (pixel >> 8) & 0xFF,
                                 (pixel >> 16) & 0xFF, (pixel >> 24) & 0xFF,
                                 vramSize, true); // BP64 units for BITBLTBUF
                    gs.xferX++;
                    if (gs.xferX >= gs.xferW)
                    {
                        gs.xferX = 0;
                        gs.xferY++;
                    }
                    gs.xferPixelsWritten++;
                }
            }
            else if (gs.xferDPSM == 0x13)
            {
                // PSMT8: 8 pixels per 64-bit write
                for (int p = 0; p < 8 && gs.xferY < gs.xferH; p++)
                {
                    uint8_t idx = (uint8_t)(data >> (p * 8));
                    // For indexed textures, store index as grayscale
                    uint32_t dx = gs.xferDstX + gs.xferX;
                    uint32_t dy = gs.xferDstY + gs.xferY;
                    writePixel32(vram, gs.xferDBP, gs.xferDBW, dx, dy,
                                 idx, idx, idx, 255, vramSize, true);
                    gs.xferX++;
                    if (gs.xferX >= gs.xferW)
                    {
                        gs.xferX = 0;
                        gs.xferY++;
                    }
                    gs.xferPixelsWritten++;
                }
            }
            else if (gs.xferDPSM == 0x14)
            {
                // PSMT4: 16 pixels per 64-bit write
                for (int p = 0; p < 16 && gs.xferY < gs.xferH; p++)
                {
                    uint8_t idx = (uint8_t)((data >> (p * 4)) & 0xF);
                    uint32_t dx = gs.xferDstX + gs.xferX;
                    uint32_t dy = gs.xferDstY + gs.xferY;
                    writePixel32(vram, gs.xferDBP, gs.xferDBW, dx, dy,
                                 idx * 17, idx * 17, idx * 17, 255, vramSize, true);
                    gs.xferX++;
                    if (gs.xferX >= gs.xferW)
                    {
                        gs.xferX = 0;
                        gs.xferY++;
                    }
                    gs.xferPixelsWritten++;
                }
            }
            else if (gs.xferDPSM == 0x01)
            {
                // PSMCT24: 24-bit pixels. Use a bit accumulator to handle
                // the non-aligned boundary (64 bits / 24 bits = 2.67 pixels).
                g_psmct24_accBits |= (data << g_psmct24_accCount);
                g_psmct24_accCount += 64;

                while (g_psmct24_accCount >= 24 && gs.xferY < gs.xferH)
                {
                    uint32_t pixel = (uint32_t)(g_psmct24_accBits & 0xFFFFFF);
                    g_psmct24_accBits >>= 24;
                    g_psmct24_accCount -= 24;

                    uint32_t dx = gs.xferDstX + gs.xferX;
                    uint32_t dy = gs.xferDstY + gs.xferY;
                    writePixel32(vram, gs.xferDBP, gs.xferDBW, dx, dy,
                                 pixel & 0xFF, (pixel >> 8) & 0xFF,
                                 (pixel >> 16) & 0xFF, 0x80,
                                 vramSize, true);
                    gs.xferX++;
                    if (gs.xferX >= gs.xferW)
                    {
                        gs.xferX = 0;
                        gs.xferY++;
                    }
                    gs.xferPixelsWritten++;
                }
            }
            else if (gs.xferDPSM == 0x30 || gs.xferDPSM == 0x31)
            {
                // PSMZ32/PSMZ24: z-buffer - skip for now
            }
            else
            {
                // PSMCT16/PSMCT16S: 4 pixels per 64-bit write
                for (int p = 0; p < 4 && gs.xferY < gs.xferH; p++)
                {
                    uint16_t pixel16 = (uint16_t)((data >> (p * 16)) & 0xFFFF);
                    uint8_t r = (pixel16 & 0x1F) << 3;
                    uint8_t g = ((pixel16 >> 5) & 0x1F) << 3;
                    uint8_t b = ((pixel16 >> 10) & 0x1F) << 3;
                    uint8_t a = (pixel16 & 0x8000) ? 128 : 0;
                    uint32_t dx = gs.xferDstX + gs.xferX;
                    uint32_t dy = gs.xferDstY + gs.xferY;
                    writePixel32(vram, gs.xferDBP, gs.xferDBW, dx, dy, r, g, b, a, vramSize, true);
                    gs.xferX++;
                    if (gs.xferX >= gs.xferW)
                    {
                        gs.xferX = 0;
                        gs.xferY++;
                    }
                    gs.xferPixelsWritten++;
                }
            }

            if (gs.xferY >= gs.xferH)
            {
                gs.xferActive = false;
                drawCtx.xferActive = false;
                drawCtx.xferPixelsWritten = gs.xferPixelsWritten;
            }
            break;
        }

        case GS_TEXFLUSH:
            // Texture cache flush - no-op for software renderer
            break;

        default:
            // Ignore unknown registers, maybe we should log?
            break;
        }
    }

}

void PS2Memory::processGIFPacket(uint32_t srcAddr, uint32_t qwCount)
{
    if (!m_rdram || !m_gsVRAM || qwCount == 0)
        return;

    if (srcAddr >= PS2_RAM_SIZE)
        return;

    uint32_t pos = srcAddr;
    const uint64_t requestedEnd = static_cast<uint64_t>(srcAddr) + static_cast<uint64_t>(qwCount) * 16ull;
    uint32_t endAddr = requestedEnd > static_cast<uint64_t>(PS2_RAM_SIZE)
                           ? PS2_RAM_SIZE
                           : static_cast<uint32_t>(requestedEnd);

    while (pos + 16 <= endAddr)
    {
        // Read GIF tag (128 bits = 16 bytes)
        uint64_t lo, hi;
        memcpy(&lo, m_rdram + pos, 8);
        memcpy(&hi, m_rdram + pos + 8, 8);
        pos += 16;

        uint32_t nloop = (uint32_t)(lo & 0x7FFF);
        bool eop = (lo >> 15) & 1;
        // bool pre = (lo >> 46) & 1; // not used currently
        uint32_t prim = (uint32_t)((lo >> 47) & 0x7FF);
        uint32_t flg = (uint32_t)((lo >> 58) & 0x3);
        uint32_t nreg = (uint32_t)((lo >> 60) & 0xF);
        if (nreg == 0)
            nreg = 16;

        // PRE bit: if set, write PRIM register
        bool pre = (lo >> 46) & 1;
        if (pre && flg != 2)
        { // not IMAGE mode
            g_gsRegs.prim = prim;
            g_gsRegs.vtxCount = 0;
        }

        g_gsRegs.gifTagsProcessed++;
        m_gsDrawCtx.gifTagsProcessed = g_gsRegs.gifTagsProcessed;

        // GS Q register resets to 1.0f when a GIFtag is read (ps2tek spec)
        g_gsRegs.q = 1.0f;

        // If NLOOP==0, no processing — just check EOP
        if (nloop == 0)
        {
            if (eop)
                break;
            continue;
        }

        if (flg == 0)
        {
            // PACKED mode
            uint64_t regs = hi;
            for (uint32_t loop = 0; loop < nloop && pos + 16 <= endAddr; loop++)
            {
                for (uint32_t r = 0; r < nreg && pos + 16 <= endAddr; r++)
                {
                    uint8_t regId = (uint8_t)((regs >> (r * 4)) & 0xF);

                    uint64_t dataLo, dataHi;
                    memcpy(&dataLo, m_rdram + pos, 8);
                    memcpy(&dataHi, m_rdram + pos + 8, 8);
                    pos += 16;

                    // In PACKED mode, most regs use dataLo, except A+D which uses both
                    if (regId == 0x0E)
                    {
                        // A+D: dataLo = value, dataHi low byte = register address
                        uint8_t gsReg = (uint8_t)(dataHi & 0xFF);
                        handleADWrite(dataLo, gsReg, m_gsVRAM, PS2_GS_VRAM_SIZE, m_gsDrawCtx);
                    }
                    else if (regId == 0x0F)
                    {
                        // NOP
                    }
                    else
                    {
                        // Direct register write in PACKED format
                        // Convert PACKED register data to A+D equivalent
                        switch (regId)
                        {
                        case 0x00: // PRIM
                            handleADWrite(dataLo & 0x7FF, GS_PRIM, m_gsVRAM, PS2_GS_VRAM_SIZE, m_gsDrawCtx);
                            break;
                        case 0x01: // RGBA (PACKED writes RGBAQ, Q unchanged)
                        {
                            // PACKED RGBA: R=lo[7:0], G=lo[39:32], B=hi[7:0], A=hi[39:32]
                            g_gsRegs.r = (uint8_t)(dataLo & 0xFF);
                            g_gsRegs.g = (uint8_t)((dataLo >> 32) & 0xFF);
                            g_gsRegs.b = (uint8_t)(dataHi & 0xFF);
                            g_gsRegs.a = (uint8_t)((dataHi >> 32) & 0xFF);
                            // Do NOT touch g_gsRegs.q — PACKED RGBA leaves Q unchanged
                            break;
                        }
                        case 0x02: // ST
                        {
                            // PACKED ST: lo[31:0]=S, lo[63:32]=T, hi[31:0]=Q
                            uint32_t sVal = (uint32_t)(dataLo & 0xFFFFFFFF);
                            uint32_t tVal = (uint32_t)(dataLo >> 32);
                            uint32_t qVal = (uint32_t)(dataHi & 0xFFFFFFFF);
                            memcpy(&g_gsRegs.s, &sVal, 4);
                            memcpy(&g_gsRegs.t, &tVal, 4);
                            memcpy(&g_gsRegs.q, &qVal, 4);
                            if (!std::isfinite(g_gsRegs.q) || g_gsRegs.q == 0.0f)
                                g_gsRegs.q = 1.0f;
                            break;
                        }
                        case 0x03: // UV (PACKED: U=lo[13:0], V=lo[45:32])
                        {
                            uint16_t u = (uint16_t)(dataLo & 0x3FFF);
                            uint16_t v = (uint16_t)((dataLo >> 32) & 0x3FFF);
                            // Repack to A+D UV layout: U=bits[13:0], V=bits[29:16]
                            uint64_t uv = (uint64_t)u | ((uint64_t)v << 16);
                            handleADWrite(uv, GS_UV, m_gsVRAM, PS2_GS_VRAM_SIZE, m_gsDrawCtx);
                            break;
                        }
                        case 0x04: // XYZF2/XYZF3
                        {
                            // PACKED XYZF: X=lo[15:0], Y=lo[47:32]
                            // Z=hi[27:4] (24 bits), F=hi[43:36] (8 bits)
                            // ADC=hi[47] (bit 111 of 128-bit QW)
                            uint32_t x = (uint32_t)(dataLo & 0xFFFF);
                            uint32_t y = (uint32_t)((dataLo >> 32) & 0xFFFF);
                            uint32_t z = (uint32_t)((dataHi >> 4) & 0x00FFFFFF); // 24-bit Z
                            uint32_t f = (uint32_t)((dataHi >> 36) & 0xFF);      // fog coeff
                            bool adc = ((dataHi >> 47) & 1) != 0;                // bit 111

                            uint8_t gsReg = adc ? GS_XYZF3 : GS_XYZF2;
                            uint64_t xyzf = (uint64_t)x | ((uint64_t)y << 16) | ((uint64_t)z << 32) | ((uint64_t)f << 56);
                            handleADWrite(xyzf, gsReg, m_gsVRAM, PS2_GS_VRAM_SIZE, m_gsDrawCtx);
                            break;
                        }
                        case 0x05: // XYZ2/XYZ3
                        {
                            uint32_t x = (uint32_t)(dataLo & 0xFFFF);
                            uint32_t y = (uint32_t)((dataLo >> 32) & 0xFFFF);
                            uint32_t z = (uint32_t)(dataHi & 0xFFFFFFFF);
                            bool adc = ((dataHi >> 47) & 1) != 0; // bit 111
                            uint8_t gsReg = adc ? GS_XYZ3 : GS_XYZ2;
                            uint64_t xyz = (uint64_t)x | ((uint64_t)y << 16) | ((uint64_t)z << 32);
                            handleADWrite(xyz, gsReg, m_gsVRAM, PS2_GS_VRAM_SIZE, m_gsDrawCtx);
                            break;
                        }
                        case 0x0A: // FOG
                            // Ignore fog for now
                            break;
                        default:
                            // Other packed regs - pass through as A+D
                            handleADWrite(dataLo, regId, m_gsVRAM, PS2_GS_VRAM_SIZE, m_gsDrawCtx);
                            break;
                        }
                    }
                }
            }
        }
        else if (flg == 1)
        {
            // REGLIST mode: stream is DWs; each QW contains 2 DWs
            // A+D is NOT available in REGLIST (only regs 0x0..0xD)
            uint64_t regs = hi;

            uint64_t totalDw = (uint64_t)nloop * (uint64_t)nreg;
            uint64_t dwIndex = 0;

            while (dwIndex < totalDw && pos + 16 <= endAddr)
            {
                uint64_t qwLo, qwHi;
                memcpy(&qwLo, m_rdram + pos, 8);
                memcpy(&qwHi, m_rdram + pos + 8, 8);
                pos += 16;

                // DW0
                if (dwIndex < totalDw)
                {
                    uint32_t r = (uint32_t)(dwIndex % nreg);
                    uint8_t regId = (uint8_t)((regs >> (r * 4)) & 0xF);
                    if (regId != 0x0E && regId != 0x0F)
                    {
                        handleADWrite(qwLo, regId, m_gsVRAM, PS2_GS_VRAM_SIZE, m_gsDrawCtx);
                    }
                    dwIndex++;
                }

                // DW1
                if (dwIndex < totalDw)
                {
                    uint32_t r = (uint32_t)(dwIndex % nreg);
                    uint8_t regId = (uint8_t)((regs >> (r * 4)) & 0xF);
                    if (regId != 0x0E && regId != 0x0F)
                    {
                        handleADWrite(qwHi, regId, m_gsVRAM, PS2_GS_VRAM_SIZE, m_gsDrawCtx);
                    }
                    dwIndex++;
                }
            }
        }
        else if (flg == 2)
        {
            // IMAGE mode: raw pixel data for Host->Local transfer
            uint32_t imageBytes = nloop * 16;
            if (g_gsRegs.xferActive && m_gsVRAM)
            {
                for (uint32_t i = 0; i < nloop && pos + 16 <= endAddr; i++)
                {
                    uint64_t lo2, hi2;
                    memcpy(&lo2, m_rdram + pos, 8);
                    memcpy(&hi2, m_rdram + pos + 8, 8);

                    // Process as two HWREG writes (each 64 bits)
                    handleADWrite(lo2, GS_HWREG, m_gsVRAM, PS2_GS_VRAM_SIZE, m_gsDrawCtx);
                    handleADWrite(hi2, GS_HWREG, m_gsVRAM, PS2_GS_VRAM_SIZE, m_gsDrawCtx);

                    pos += 16;
                    g_gsRegs.imageQWs++;
                }
            }
            else
            {
                pos += imageBytes;
                if (pos > endAddr)
                    pos = endAddr;
            }
        }
        else
        {
            // flg == 3: disabled/reserved
            break;
        }

        if (eop)
            break;
    }

    m_gsDrawCtx.gifTagsProcessed = g_gsRegs.gifTagsProcessed;
    m_gsDrawCtx.adWrites = g_gsRegs.adWrites;
    m_gsDrawCtx.primitivesDrawn = g_gsRegs.primsDrawn;

    m_gsWriteCount.fetch_add(1, std::memory_order_relaxed);
    m_seenGifCopy = true;
}
