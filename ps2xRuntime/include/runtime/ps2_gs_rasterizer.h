#ifndef PS2_GS_RASTERIZER_H
#define PS2_GS_RASTERIZER_H

#include <cstdint>

class GS;

class GSRasterizer
{
public:
    void drawPrimitive(GS *gs);
    void writePixel(GS *gs, int x, int y, int z, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
    uint32_t sampleTexture(GS *gs, float s, float t, float q, uint16_t u, uint16_t v);
    uint32_t lookupCLUT(GS *gs, uint8_t index, uint32_t cbp, uint8_t cpsm, uint8_t csm, uint8_t csa, uint8_t sourcePsm);

private:
    void drawSprite(GS *gs);
    void drawTriangle(GS *gs);
    void drawLine(GS *gs);

    // Diagnostics-only helpers (see ps2_gs_rasterizer.cpp / runtime/ps2_diag.h).
    // Kept as members (rather than free functions) purely so they retain
    // GSRasterizer's friend access to GS's private VRAM/CLUT state.
    static uint32_t readClutRgba(GS *gs, uint32_t cbp, uint8_t cpsm, uint32_t clutWidth, uint32_t x, uint32_t y);
    static void dumpClutIfNew(GS *gs, uint32_t cbp, uint8_t cpsm, uint8_t csm, uint8_t csa, uint8_t sourcePsm);
};

#endif
