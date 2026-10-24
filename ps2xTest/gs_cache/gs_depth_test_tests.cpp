#include "gs_test_support.h"

using namespace GSTest;

namespace
{
    constexpr uint32_t kDepthPage = 300u;
    constexpr uint32_t kStoredZ = 100u;

    GSPrimitiveBatch solidSprite(uint64_t test, uint32_t z, bool maskDepth = false)
    {
        auto batch = sprite(texture(), 0, 0);
        batch.state.prim.tme = false;
        batch.state.context.test = test;
        batch.state.context.zbuf.zbp = kDepthPage;
        batch.state.context.zbuf.psm = GS_PSM_Z32;
        batch.state.context.zbuf.zmask = maskDepth;
        for (auto& vertex : batch.vertices)
        {
            vertex.r = 248;
            vertex.g = vertex.b = 0;
            vertex.a = 128;
            vertex.z = static_cast<float>(z);
        }
        return batch;
    }

    void checkDraw(uint64_t test, uint32_t z, bool maskDepth,
                   bool writesColor, bool writesDepth)
    {
        BackendFixture f;
        f.backend.WriteVram(GS_PSM_CT32, kOutputPage * 32u, 1, 0, 0, kGreen);
        f.backend.WriteVram(GS_PSM_Z32, kDepthPage * 32u, 1, 0, 0, kStoredZ);
        f.backend.Submit(solidSprite(test, z, maskDepth));
        expectEqual(f.backend.ReadVram(GS_PSM_CT32, kOutputPage * 32u, 1, 0, 0),
                    writesColor ? kRed : kGreen, "framebuffer after depth test");
        expectEqual(f.backend.ReadVram(GS_PSM_Z32, kDepthPage * 32u, 1, 0, 0),
                    writesDepth ? z : kStoredZ, "depth buffer after depth test");
    }

    void movieSprite()
    {
        BackendFixture f;
        auto tex = texture(GS_PSM_CT32, 4480);
        tex.tbw = 10;
        tex.tw = tex.th = 10;
        tex.tcc = 0;
        tex.tfx = 1; // DECAL: RGB comes from the texture even with vertex RGB=0.
        const uint32_t colors[] = {kRed, kGreen, kBlue};
        for (uint32_t y = 0; y < 448; ++y)
            for (uint32_t x = 0; x < 640; ++x)
                f.backend.WriteVram(GS_PSM_CT32, tex.tbp0, tex.tbw, x, y,
                                    colors[x < 200 ? 0 : (x < 400 ? 1 : 2)]);

        // Register/vertex state observed in the retail opening movie. Both
        // field sprites draw into one 640x448 framebuffer; pixels are synthetic.
        auto batch = sprite(tex, 0, 0, true);
        batch.state.context.frame.fbp = 0;
        batch.state.context.frame.fbw = 10;
        batch.state.context.scissor = {0, 639, 0, 447};
        batch.state.context.test = 0;
        batch.state.textureWidth = batch.state.textureHeight = 1024;
        for (auto& vertex : batch.vertices)
            vertex.r = vertex.g = vertex.b = vertex.a = 0;
        batch.vertices[0].u = 8;
        batch.vertices[0].v = 24;
        batch.vertices[1].u = 10248;
        batch.vertices[1].v = 7192;
        batch.vertices[1].x = 640;
        batch.vertices[1].y = 224;
        f.backend.Submit(batch);
        batch.vertices[0].y = 224;
        batch.vertices[1].y = 448;
        batch.vertices[0].v = 8;
        batch.vertices[1].v = 7176;
        f.backend.Submit(batch);

        // Sample within the image: the captured half-pixel UV offsets and
        // linear filter can blend the final edge with the unused texture area.
        for (uint32_t y : {4u, 111u, 219u, 228u, 335u, 443u})
            for (uint32_t x : {10u, 100u, 300u, 500u, 630u})
            {
                const uint32_t expected = colors[x < 200 ? 0 : (x < 400 ? 1 : 2)] & 0x00FFFFFFu;
                expectEqual(f.backend.ReadVram(GS_PSM_CT32, 0, 10, x, y), expected,
                            "TEST=0 movie field must render textured RGB");
            }
    }

    void disabledMethods()
    {
        for (uint32_t method = 0; method < 4; ++method)
        {
            // A stale NEVER/GEQUAL/GREATER selection cannot reject color or
            // write depth while ZTE=0, even when ZBUF.ZMSK is clear.
            checkDraw(uint64_t(method) << 17, 99, false, true, false);
            checkDraw(uint64_t(method) << 17, 101, true, true, false);
        }
    }

    void enabledNeverAlways()
    {
        for (uint32_t z : {99u, 100u, 101u})
        {
            checkDraw(1ull << 16, z, false, false, false);
            checkDraw((1ull << 16) | (1ull << 17), z, false, true, true);
        }
    }

    void enabledComparisons()
    {
        for (uint32_t z : {99u, 100u, 101u})
        {
            checkDraw((1ull << 16) | (2ull << 17), z, false, z >= kStoredZ, z >= kStoredZ);
            checkDraw((1ull << 16) | (3ull << 17), z, false, z > kStoredZ, z > kStoredZ);
        }
    }

    void depthWriteMask()
    {
        for (uint32_t method = 0; method < 4; ++method)
            checkDraw((1ull << 16) | (uint64_t(method) << 17), 101, true,
                      method != 0, false);
    }

    void alphaZonly()
    {
        // ATE=1, ATST=NEVER, AFAIL=ZB_ONLY preserves the framebuffer. It
        // cannot revive writes when depth testing is disabled or rejects Z.
        constexpr uint64_t alphaFailZonly = 1ull | (2ull << 12);
        for (uint32_t method = 0; method < 4; ++method)
        {
            checkDraw(alphaFailZonly | (uint64_t(method) << 17), 101, false, false, false);
            checkDraw(alphaFailZonly | (1ull << 16) | (uint64_t(method) << 17),
                      101, false, false, method != 0);
        }
        checkDraw(alphaFailZonly | (1ull << 16) | (1ull << 17), 101, true, false, false);
        checkDraw(alphaFailZonly | (1ull << 16) | (3ull << 17), 99, false, false, false);
    }
}

int main(int argc, char** argv)
{
    return run(argc, argv, {
        {"movie_sprite", movieSprite}, {"disabled_methods", disabledMethods},
        {"enabled_never_always", enabledNeverAlways}, {"enabled_comparisons", enabledComparisons},
        {"depth_write_mask", depthWriteMask}, {"alpha_zonly", alphaZonly}
    });
}
