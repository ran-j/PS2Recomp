#include "gs_test_support.h"
#include "runtime/gs/ps2_gs_memory.h"

using namespace GSTest;

namespace
{
    template<uint8_t Psm>
    void addressCoverage()
    {
        BackendFixture f;
        GSMem::TexturePageCache cache;
        uint32_t random = 0x51375A9Du;
        for (auto& byte : f.vram)
        {
            random ^= random << 13;
            random ^= random >> 17;
            random ^= random << 5;
            byte = static_cast<uint8_t>(random);
        }
        constexpr auto mode = static_cast<GSMem::PixelStorageMode>(Psm);
        constexpr auto extent = GSMem::PixelStorageTraits<mode>::PageExtent();
        uint32_t checked = 0;
        const auto check = [&](uint32_t base, uint32_t bw, uint32_t x, uint32_t y)
        {
            const auto direct = f.backend.ReadVram(Psm, base, bw, x, y);
            const auto cached = GSMem::ReadTexture(cache, f.vram.data(), Psm, base, bw, x, y);
            if (cached != direct)
            {
                std::ostringstream error;
                error << "PSM=" << unsigned(Psm) << " BP=" << base << " BW=" << bw << " XY=" << x << ',' << y;
                expectEqual(cached, direct, error.str());
            }
            ++checked;
        };
        // Every local texel, for every 256-byte base offset inside an 8 KiB page.
        for (uint32_t offset = 0; offset < 32; ++offset)
            for (uint32_t y = 0; y < extent.y; ++y)
                for (uint32_t x = 0; x < extent.x; ++x)
                    check(32 + offset, 2, x, y);

        for (uint32_t base : {0u, 31u, 32u, 12160u, 16256u, 16383u})
            for (uint32_t bw : {0u, 1u, 2u, 3u, 7u, 8u, 10u, 63u})
                for (uint32_t y : {0u, 1u, 7u, 8u, 15u, 16u, 31u, 32u, 63u, 64u, 127u, 128u, 255u, 256u, 511u, 512u, 1023u, 2047u})
                    for (uint32_t x : {0u, 1u, 7u, 8u, 15u, 16u, 31u, 32u, 63u, 64u, 127u, 128u, 255u, 256u, 511u, 512u, 1023u, 2047u})
                        check(base, bw, x, y);
        std::cout << checked << " cached/direct comparisons\n";
    }

    void aliasLanes()
    {
        BackendFixture f;
        GSMem::TexturePageCache cache;
        constexpr uint32_t base = 31;
        const auto read = [&](uint32_t psm)
        {
            return GSMem::ReadTexture(cache, f.vram.data(), psm, base, 2, 8, 0);
        };
        f.backend.WriteVram(GS_PSM_CT32, base, 2, 8, 0, 0xAB123456);
        expectEqual(read(GS_PSM_T8H), 0xAB, "8H lane on a crossing page");
        f.backend.WriteVram(GS_PSM_CT24, base, 2, 8, 0, 0x654321);
        expectEqual(read(GS_PSM_CT32), 0xAB123456, "changing PSM does not refresh the physical page");
        cache.Invalidate();
        expectEqual(read(GS_PSM_CT32), 0xAB654321, "CT24 upload preserves alpha");
        f.backend.WriteVram(GS_PSM_T4HL, base, 2, 8, 0, 5);
        expectEqual(read(GS_PSM_T4HL), 11, "low nibble remains cached before flush");
        cache.Invalidate();
        expectEqual(read(GS_PSM_T4HL), 5, "low nibble after flush");
        expectEqual(read(GS_PSM_T4HH), 10, "high nibble preserved");
        expectEqual(read(GS_PSM_CT24), 0x654321, "RGB plane preserved");
    }

    void physicalTagAliases()
    {
        BackendFixture f;
        GSMem::TexturePageCache cache;
        f.backend.WriteVram(GS_PSM_CT32, 32, 2, 0, 0, kRed);
        expectEqual(GSMem::ReadTexture(cache, f.vram.data(), GS_PSM_CT32, 32, 2, 0, 0), kRed, "prime aligned view");
        f.backend.WriteVram(GS_PSM_CT32, 32, 2, 0, 0, kGreen);
        // Both descriptors resolve to the very same physical byte, so this is a hit.
        expectEqual(GSMem::ReadTexture(cache, f.vram.data(), GS_PSM_CT32, 31, 2, 8, 0), kRed, "alias descriptor keeps the same cached bytes");
        cache.Invalidate();
        expectEqual(GSMem::ReadTexture(cache, f.vram.data(), GS_PSM_CT32, 31, 2, 8, 0), kGreen, "alias after flush");
    }
}

int main(int argc, char** argv)
{
    return run(argc, argv, {
        {"ct32", addressCoverage<GS_PSM_CT32>}, {"ct24", addressCoverage<GS_PSM_CT24>},
        {"ct16", addressCoverage<GS_PSM_CT16>}, {"ct16s", addressCoverage<GS_PSM_CT16S>},
        {"t8", addressCoverage<GS_PSM_T8>}, {"t4", addressCoverage<GS_PSM_T4>},
        {"t8h", addressCoverage<GS_PSM_T8H>}, {"t4hl", addressCoverage<GS_PSM_T4HL>},
        {"t4hh", addressCoverage<GS_PSM_T4HH>}, {"z32", addressCoverage<GS_PSM_Z32>},
        {"z24", addressCoverage<GS_PSM_Z24>}, {"z16", addressCoverage<GS_PSM_Z16>},
        {"z16s", addressCoverage<GS_PSM_Z16S>}, {"alias_lanes", aliasLanes},
        {"physical_tag_aliases", physicalTagAliases}
    });
}
