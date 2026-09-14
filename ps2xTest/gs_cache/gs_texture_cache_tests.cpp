#include "gs_test_support.h"

#include <cstring>

using namespace GSTest;

namespace
{
    void unalignedTexture()
    {
        BackendFixture f;
        auto tex = texture(GS_PSM_CT32, 31);
        // TBP=31, CT32(8,0): block 31 + swizzled block 1 = physical page 1.
        std::memcpy(f.vram.data() + 8192u, &kRed, sizeof(kRed));
        expectEqual(f.sample(tex, 8), kRed, "non-page-aligned texture base");
    }

    void unalignedWrap()
    {
        BackendFixture f;
        auto tex = texture(GS_PSM_CT32, 16383);
        std::memcpy(f.vram.data(), &kGreen, sizeof(kGreen));
        expectEqual(f.sample(tex, 8), kGreen, "swizzle carry wraps through the 4 MiB boundary");
    }

    void staleMirror()
    {
        BackendFixture f;
        auto tex = texture();
        f.backend.WriteVram(tex.psm, tex.tbp0, tex.tbw, 0, 0, kRed);
        expectEqual(f.sample(tex), kRed, "prime the following physical page");
        f.backend.WriteVram(tex.psm, tex.tbp0, tex.tbw, 0, 0, kGreen);
        f.backend.TextureFlush();
        tex.tbp0 = 31;
        expectEqual(f.sample(tex, 8), kGreen, "TEXFLUSH must not expose stale bytes in the old mirror");
    }

    void pageAlternation()
    {
        BackendFixture f;
        auto tex = texture(GS_PSM_CT32, 31);
        f.backend.WriteVram(tex.psm, tex.tbp0, tex.tbw, 0, 0, kRed);
        f.backend.WriteVram(tex.psm, tex.tbp0, tex.tbw, 8, 0, kGreen);
        for (unsigned i = 0; i < 8; ++i)
        {
            expectEqual(f.sample(tex), kRed, "first physical page");
            expectEqual(f.sample(tex, 8), kGreen, "second physical page in the same logical page");
        }
    }

    void flushVisibility()
    {
        BackendFixture f;
        auto tex = texture();
        f.backend.WriteVram(tex.psm, tex.tbp0, tex.tbw, 0, 0, kRed);
        expectEqual(f.sample(tex), kRed, "initial cache fill");
        f.backend.WriteVram(tex.psm, tex.tbp0, tex.tbw, 0, 0, kGreen);
        expectEqual(f.backend.ReadVram(tex.psm, tex.tbp0, tex.tbw, 0, 0), kGreen, "canonical VRAM changes immediately");
        f.backend.Flush();
        f.backend.Sync(GSSyncReason::Finish);
        expectEqual(f.sample(tex), kRed, "ordinary flush and FINISH do not invalidate texels");
        f.backend.TextureFlush();
        expectEqual(f.sample(tex), kGreen, "TEXFLUSH exposes the updated texels");
    }

    void uploadVisibility()
    {
        BackendFixture f;
        auto tex = texture();
        f.backend.WriteVram(tex.psm, tex.tbp0, tex.tbw, 0, 0, kRed);
        expectEqual(f.sample(tex), kRed, "prime destination");
        GSTransferCommand transfer{};
        transfer.direction = 0;
        transfer.bitbltbuf.dbp = tex.tbp0;
        transfer.bitbltbuf.dbw = tex.tbw;
        transfer.bitbltbuf.dpsm = tex.psm;
        transfer.trxreg.rrw = transfer.trxreg.rrh = 1;
        f.backend.BeginTransfer(transfer);
        f.backend.UploadImage(reinterpret_cast<const uint8_t*>(&kGreen), sizeof(kGreen));
        expectEqual(f.sample(tex), kRed, "host upload does not implicitly flush texels");
        f.backend.TextureFlush();
        expectEqual(f.sample(tex), kGreen, "host upload visible after TEXFLUSH");
    }

    void localCopyVisibility()
    {
        BackendFixture f;
        auto tex = texture();
        f.backend.WriteVram(tex.psm, tex.tbp0, tex.tbw, 0, 0, kRed);
        f.backend.WriteVram(tex.psm, 96, tex.tbw, 0, 0, kGreen);
        expectEqual(f.sample(tex), kRed, "prime destination");
        GSTransferCommand transfer{};
        transfer.direction = 2;
        transfer.bitbltbuf.sbp = 96;
        transfer.bitbltbuf.sbw = transfer.bitbltbuf.dbw = tex.tbw;
        transfer.bitbltbuf.spsm = transfer.bitbltbuf.dpsm = tex.psm;
        transfer.bitbltbuf.dbp = tex.tbp0;
        transfer.trxreg.rrw = transfer.trxreg.rrh = 1;
        f.backend.BeginTransfer(transfer);
        expectEqual(f.sample(tex), kRed, "local copy does not implicitly flush texels");
        f.backend.TextureFlush();
        expectEqual(f.sample(tex), kGreen, "local copy visible after TEXFLUSH");
    }

    void rasterVisibility()
    {
        BackendFixture f;
        auto tex = texture();
        f.backend.WriteVram(tex.psm, tex.tbp0, tex.tbw, 0, 0, kRed);
        expectEqual(f.sample(tex), kRed, "prime render target as texture");
        auto batch = sprite(tex, 0, 0);
        batch.state.prim.tme = false;
        batch.state.context.frame.fbp = tex.tbp0 / 32;
        for (auto& vertex : batch.vertices)
        {
            vertex.r = 0;
            vertex.g = 248;
            vertex.b = 0;
        }
        f.backend.Submit(batch);
        expectEqual(f.sample(tex), kRed, "raster writes do not implicitly flush texels");
        f.backend.TextureFlush();
        expectEqual(f.sample(tex), kGreen, "render-to-texture visible after TEXFLUSH");
    }

    void resetAndRebind()
    {
        BackendFixture f;
        auto tex = texture();
        f.backend.WriteVram(tex.psm, tex.tbp0, tex.tbw, 0, 0, kRed);
        expectEqual(f.sample(tex), kRed, "prime cache");
        f.backend.WriteVram(tex.psm, tex.tbp0, tex.tbw, 0, 0, kGreen);
        f.backend.Reset();
        expectEqual(f.sample(tex), kGreen, "reset invalidates without clearing VRAM");
        std::vector<uint8_t> other(kVramSize);
        std::memcpy(other.data() + 8192u, &kBlue, sizeof(kBlue));
        f.backend.Initialize(other.data(), static_cast<uint32_t>(other.size()));
        expectEqual(f.sample(tex), kBlue, "initialize invalidates the previous VRAM allocation");
    }

    void invalidVramSize()
    {
        BackendFixture f;
        std::vector<uint8_t> shortVram(8192);
        bool rejected = false;
        try { f.backend.Initialize(shortVram.data(), static_cast<uint32_t>(shortVram.size())); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "undersized VRAM must be rejected before masked accesses can escape it");
        f.backend.WriteVram(GS_PSM_CT32, 32, 2, 0, 0, kGreen);
        expectEqual(f.sample(texture()), kGreen, "failed initialize preserves the existing backend binding");
        f.backend.Initialize(nullptr, 0);
        expectEqual(f.backend.ReadVram(GS_PSM_CT32, 32, 2, 0, 0), 0, "null binding is safe");
    }

    void reservedPsm()
    {
        BackendFixture f;
        auto tex = texture(0x3F);
        f.backend.WriteVram(GS_PSM_CT32, 32, 2, 0, 0, kGreen);
        f.backend.WriteVram(0x3F, 32, 2, 0, 0, kRed);
        expectEqual(f.backend.ReadVram(GS_PSM_CT32, 32, 2, 0, 0), kGreen, "reserved writes are no-op");
        expectEqual(f.backend.ReadVram(0x3F, 32, 2, 0, 0), 0, "reserved raw reads use null semantics");
        expectEqual(f.sample(tex), 0xFFFF00FFu, "reserved sampling preserves the existing magenta diagnostic");
    }
}

int main(int argc, char** argv)
{
    return run(argc, argv, {
        {"unaligned_texture", unalignedTexture}, {"unaligned_wrap", unalignedWrap},
        {"stale_mirror", staleMirror}, {"page_alternation", pageAlternation},
        {"flush_visibility", flushVisibility}, {"upload_visibility", uploadVisibility},
        {"local_copy_visibility", localCopyVisibility}, {"raster_visibility", rasterVisibility},
        {"reset_and_rebind", resetAndRebind}, {"invalid_vram_size", invalidVramSize},
        {"reserved_psm", reservedPsm}
    });
}
