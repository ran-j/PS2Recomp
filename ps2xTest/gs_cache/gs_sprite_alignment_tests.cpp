// SPDX-License-Identifier: GPL-3.0-only
#include "runtime/gs/gs_cpu_backend.h"
#include "runtime/gs/ps2_gs_common.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
unsigned checks = 0, failures = 0;
constexpr unsigned width = 256, height = 64, bw = 4;
constexpr unsigned textureBlock = 2048;
constexpr uint32_t untouched = 0x80ff00ffu;

void expect(bool condition, const char *message)
{
    ++checks;
    if (!condition)
    {
        if (failures < 24)
            std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

uint32_t pattern(int x, int y)
{
    x = std::clamp(x, 0, static_cast<int>(width) - 1);
    y = std::clamp(y, 0, static_cast<int>(height) - 1);
    return 0x80000000u | static_cast<uint32_t>((x % 64) * 4) |
           (static_cast<uint32_t>((y % 32) * 6) << 8) |
           (static_cast<uint32_t>(((x + y * 3) % 64) * 4) << 16);
}

// Scalar oracle: UV keeps its 4 fractional bits, sprite interpolation uses
// integer GS pixel coordinates, and linear filtering subtracts half a texel.
uint32_t filtered(float u, float v, bool linear)
{
    if (!linear)
        return pattern(static_cast<int>(u), static_cast<int>(v));
    const float sx = u - 0.5f, sy = v - 0.5f;
    const int x0 = static_cast<int>(std::floor(sx));
    const int y0 = static_cast<int>(std::floor(sy));
    const float fx = sx - static_cast<float>(x0), fy = sy - static_cast<float>(y0);
    const uint32_t c[4] = {pattern(x0, y0), pattern(x0 + 1, y0),
                            pattern(x0, y0 + 1), pattern(x0 + 1, y0 + 1)};
    uint32_t result = 0;
    for (unsigned shift = 0; shift < 32; shift += 8)
    {
        const float top = static_cast<float>((c[0] >> shift) & 255u) * (1.0f - fx) +
                          static_cast<float>((c[1] >> shift) & 255u) * fx;
        const float bottom = static_cast<float>((c[2] >> shift) & 255u) * (1.0f - fx) +
                             static_cast<float>((c[3] >> shift) & 255u) * fx;
        const unsigned channel = static_cast<unsigned>(top * (1.0f - fy) + bottom * fy + 0.5f);
        result |= std::min(channel, 255u) << shift;
    }
    return result;
}

bool withinRounding(uint32_t actual, uint32_t expected, unsigned tolerance)
{
    for (unsigned shift = 0; shift < 32; shift += 8)
    {
        const int a = static_cast<int>((actual >> shift) & 255u);
        const int e = static_cast<int>((expected >> shift) & 255u);
        if (std::abs(a - e) > static_cast<int>(tolerance))
            return false;
    }
    return true;
}

GSPrimitiveBatch sprite(float left, float top, float right, float bottom,
                        float u0, float v0, float u1, float v1, unsigned reverse = 0)
{
    GSPrimitiveBatch batch{};
    batch.vertexCount = 2;
    batch.state.prim.type = GS_PRIM_SPRITE;
    batch.state.prim.tme = true;
    batch.state.prim.fst = true;
    batch.state.context.frame = {0, bw, GS_PSM_CT32, 0};
    batch.state.context.zbuf = {200, GS_PSM_Z32, true};
    // Isolate alignment from the separate disabled-ZTE bug: enable ALWAYS
    // while masking depth writes. This fixture does not require that fix.
    batch.state.context.test = (1ull << 16u) | (1ull << 17u);
    batch.state.context.scissor = {0, width - 1, 0, height - 1};
    batch.state.context.tex0 = {textureBlock, bw, GS_PSM_CT32, 8, 6, 1, 1};
    batch.state.context.clamp = 5; // Clamp U and V to the texture edges.
    batch.state.textureWidth = width;
    batch.state.textureHeight = height;
    batch.state.linearFilter = true;
    auto &a = batch.vertices[0];
    auto &b = batch.vertices[1];
    a.x = left; a.y = top; b.x = right; b.y = bottom;
    a.u = static_cast<uint16_t>(u0 * 16); a.v = static_cast<uint16_t>(v0 * 16);
    b.u = static_cast<uint16_t>(u1 * 16); b.v = static_cast<uint16_t>(v1 * 16);
    a.a = b.a = 128;
    if ((reverse & 1u) != 0)
    { std::swap(a.x, b.x); std::swap(a.u, b.u); }
    if ((reverse & 2u) != 0)
    { std::swap(a.y, b.y); std::swap(a.v, b.v); }
    return batch;
}

void initialize(GSCpuBackend &gs)
{
    gs.Reset();
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x)
        {
            gs.WriteVram(GS_PSM_CT32, 0, bw, x, y, untouched);
            gs.WriteVram(GS_PSM_CT32, textureBlock, bw, x, y, pattern(x, y));
        }
    gs.TextureFlush();
}

void checkSprite(GSCpuBackend &gs, const GSPrimitiveBatch &batch, const char *message)
{
    initialize(gs);
    gs.Submit(batch);
    gs.Sync(GSSyncReason::Presentation);
    const auto &a = batch.vertices[0], &b = batch.vertices[1];
    const float ofx = static_cast<float>(batch.state.context.xyoffset.ofx) / 16;
    const float ofy = static_cast<float>(batch.state.context.xyoffset.ofy) / 16;
    const float left = std::min(a.x, b.x) - ofx, right = std::max(a.x, b.x) - ofx;
    const float top = std::min(a.y, b.y) - ofy, bottom = std::max(a.y, b.y) - ofy;
    const float startU = static_cast<float>((a.x <= b.x) ? a.u : b.u) / 16;
    const float endU = static_cast<float>((a.x <= b.x) ? b.u : a.u) / 16;
    const float startV = static_cast<float>((a.y <= b.y) ? a.v : b.v) / 16;
    const float endV = static_cast<float>((a.y <= b.y) ? b.v : a.v) / 16;
    const auto &clip = batch.state.context.scissor;
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x)
        {
            uint32_t expected = untouched;
            const bool covered = static_cast<float>(x) >= std::ceil(left) && static_cast<float>(x) < std::ceil(right) &&
                                 static_cast<float>(y) >= std::ceil(top) && static_cast<float>(y) < std::ceil(bottom) &&
                                 x >= clip.x0 && x <= clip.x1 && y >= clip.y0 && y <= clip.y1;
            if (covered)
            {
                const float tx = (static_cast<float>(x) - left) / (right - left);
                const float ty = (static_cast<float>(y) - top) / (bottom - top);
                expected = filtered(startU + (endU - startU) * tx,
                                    startV + (endV - startV) * ty, batch.state.linearFilter);
            }
            // One unit permits different float operation order only for
            // bilinear channel rounding; coverage and untouched pixels are exact.
            const unsigned tolerance = covered && batch.state.linearFilter ? 1u : 0u;
            expect(withinRounding(gs.ReadVram(GS_PSM_CT32, 0, bw, x, y), expected, tolerance), message);
        }
}

void coordinateCases(GSCpuBackend &gs)
{
    for (unsigned reverse = 0; reverse < 4; ++reverse)
    {
        checkSprite(gs, sprite(0, 0, 192, 16, 0.5f, 0.5f, 192.5f, 16.5f, reverse),
                    "each reversed axis preserves its matching UV endpoint");
        checkSprite(gs, sprite(0, 1, 63.5f, 32.5f, 0, 0, 64, 32, reverse),
                    "63.5 endpoint covers column 63 without mirroring the strip");
        auto offset = sprite(0.5f, 0.5f, 64.5f, 16.5f, 0.5f, 0.5f, 64.5f, 16.5f, reverse);
        offset.state.context.xyoffset = {8, 8};
        checkSprite(gs, offset, "fractional XYOFFSET cancels fractional guest XY exactly");
    }
    for (unsigned fraction = 0; fraction < 16; ++fraction)
    {
        const float sub = static_cast<float>(fraction) / 16;
        for (bool linear : {false, true})
        {
            auto batch = sprite(0, 0, 64, 16, sub, sub, 64 + sub, 16 + sub);
            batch.state.linearFilter = linear;
            checkSprite(gs, batch, "all four UV fraction bits affect integer-position texture sampling");
        }
    }
    auto clipped = sprite(0, 0, 192, 32, 0.5f, 0.5f, 192.5f, 32.5f, 3);
    clipped.state.context.scissor = {61, 130, 7, 20};
    checkSprite(gs, clipped, "scissoring preserves interpolation relative to the original sprite");
    for (unsigned reverse = 0; reverse < 4; ++reverse)
    {
        auto stq = sprite(0, 0, 64, 16, 0.5f, 0.5f, 64.5f, 16.5f, reverse);
        stq.state.prim.fst = false;
        stq.vertices[0].q = 0.25f;
        stq.vertices[1].q = 2.0f;
        for (auto &vertex : stq.vertices)
        {
            vertex.s = (static_cast<float>(vertex.u) / 16.0f / width) * 2.0f;
            vertex.t = (static_cast<float>(vertex.v) / 16.0f / height) * 2.0f;
        }
        checkSprite(gs, stq, "STQ sprite uses the second vertex's flat Q for both endpoints");
    }
    checkSprite(gs, sprite(20.5f, 0, 20.5f, 8, 0, 0, 8, 8), "zero-width sprite draws no pixels");
    checkSprite(gs, sprite(0, 10.5f, 8, 10.5f, 0, 0, 8, 8), "zero-height sprite draws no pixels");
    auto plain = sprite(0.5f, 0.5f, 63.5f, 16.5f, 0, 0, 64, 16, 3);
    plain.state.prim.tme = false;
    plain.vertices[1].r = 20; plain.vertices[1].g = 40; plain.vertices[1].b = 60;
    initialize(gs);
    gs.Submit(plain); gs.Sync(GSSyncReason::Presentation);
    for (unsigned y = 0; y < 20; ++y)
        for (unsigned x = 0; x < 68; ++x)
            expect(gs.ReadVram(GS_PSM_CT32, 0, bw, x, y) ==
                   ((x >= 1 && x < 64 && y >= 1 && y < 17) ? 0x803c2814u : untouched),
                   "untextured sprite uses the same ceil-exclusive coverage");
}

void feedbackStrips(GSCpuBackend &gs)
{
    gs.Reset();
    // This identity pass intentionally aliases source/destination. It uses
    // the synchronous backend's actual single-page texture cache.
    // Matching fractional UV makes every sample equal to its existing pixel,
    // so the result is independent of texture-cache invalidation policy.
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x)
            gs.WriteVram(GS_PSM_CT32, 0, bw, x, y, 0x80400000u | x | ((255u - x) << 16));
    gs.TextureFlush();
    for (unsigned strip = 0; strip < 4; ++strip)
    {
        const float left = static_cast<float>(strip * 64);
        auto batch = sprite(left, 1, left + 63.5f, 32.5f,
                            left + 0.5f, 1.5f, left + 64, 33, 3);
        batch.state.context.tex0.tbp0 = 0;
        gs.Submit(batch);
        // Deliberately no TEXFLUSH between adjacent 64px CT32 pages.
    }
    gs.Sync(GSSyncReason::Presentation);
    unsigned changed = 0, seams = 0;
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x)
        {
            const uint32_t expected = 0x80400000u | x | ((255u - x) << 16);
            const uint32_t value = gs.ReadVram(GS_PSM_CT32, 0, bw, x, y);
            changed += value != expected;
            expect(value == expected, "reversed 64px feedback strips retain the original aligned pixels");
            if (y >= 1 && y < 33 && x > 0 && (x % 64 == 0 || x % 64 == 63))
            {
                const int previous = static_cast<int>(gs.ReadVram(GS_PSM_CT32, 0, bw, x - 1, y) & 255u);
                const bool aligned = static_cast<int>(value & 255u) - previous == 1;
                seams += !aligned;
                expect(aligned, "feedback page/strip boundaries have no extra brightness step");
            }
        }
    std::printf("Feedback strips: %u altered pixels, %u artificial boundary steps\n", changed, seams);
}
}

int main(int argc, char** argv)
{
    std::vector<uint8_t> vram(4u * 1024u * 1024u);
    GSCpuBackend gs;
    gs.Initialize(vram.data(), static_cast<uint32_t>(vram.size()));
    const std::string scenario = argc > 1 ? argv[1] : "all";
    if (scenario == "coordinates" || scenario == "all") coordinateCases(gs);
    if (scenario == "feedback" || scenario == "all") feedbackStrips(gs);
    if (scenario != "coordinates" && scenario != "feedback" && scenario != "all") return 2;
    std::printf("GS alignment regression: %u checks, %u failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
