#include "runtime/gs/gs_frontend.h"
#include "runtime/gs/ps2_gif_arbiter.h"
#include "runtime/ps2_memory.h"
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace
{
    void check(bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }
    struct RawBackend final : GSRasterBackend
    {
        std::vector<GifArbiterPacket> packets;
        std::vector<std::pair<uint8_t, uint64_t>> registers;
        std::vector<uint8_t> image;
        unsigned batches = 0, cluts = 0, transfers = 0, snapshots = 0;

        void Initialize(uint8_t *, uint32_t) override
        {
        }

        void Reset() override
        {
        }

        bool UsesRawCommands() const override
        {
            return true;
        }

        void ProcessGIF(uint32_t path, const uint8_t *data, uint32_t size) override
        {
            packets.push_back({GifPathId(path), false, false, {data, data + size}});
        }

        void WriteRegisterRaw(uint8_t addr, uint64_t value) override
        {
            registers.emplace_back(addr, value);
        }

        void ReadRegisterState(GSDebugSnapshot &s) const override
        {
            s.ctx[0].frame.fbp = 37;
        }

        void Submit(const GSPrimitiveBatch &) override
        {
            ++batches;
        }

        void LoadClut(const GSTex0Reg &, const GSTexClutReg &) override
        {
            ++cluts;
        }

        void BeginTransfer(const GSTransferCommand &) override
        {
            ++transfers;
        }

        void UploadImage(const uint8_t *data, uint32_t size) override
        {
            image.insert(image.end(), data, data + size);
        }

        void Flush() override
        {
        }

        void TextureFlush() override
        {
        }

        void Sync(GSSyncReason) override
        {
        }

        PresentationFrame Present(const GSPresentationRequest &) override
        {
            return {};
        }

        bool ClearFramebuffer(const GSContext &, uint32_t) override
        {
            return true;
        }

        uint32_t ConsumeLocalToHostBytes(uint8_t *, uint32_t) override
        {

            return 0;
        }
        uint32_t ReadVram(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) const override
        {
            return 0;
        }

        void WriteVram(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) override
        {
        }

        void SnapshotVram(std::vector<uint8_t> &) const override
        {
            throw std::runtime_error("unexpected raw VRAM snapshot");
        }

        GSTransferSnapshot GetTransferSnapshot() const override
        {
            return {};
        }
    };
}
int main()
{
    try
    {
        std::vector<uint8_t> vram(4 * 1024 * 1024);
        GSRegisters priv{};
        GS gs;
        gs.init(vram.data(), uint32_t(vram.size()), &priv);
        auto backend = std::make_unique<RawBackend>();
        auto &raw = *backend;
        gs.setRasterBackend(std::move(backend));
        const uint64_t packed[] = {1ull | (1ull << 15) | (1ull << 60), 14, 0xff112233, GS_REG_RGBAQ};
        auto *packet = reinterpret_cast<const uint8_t *>(packed);
        GifArbiter arbiter([&](GifPathId path, const uint8_t *data, uint32_t size)
                           { gs.processGIFPacket(data, size, uint32_t(path)); });
        for (auto path : {GifPathId::Path3, GifPathId::Path1, GifPathId::Path2})
            arbiter.submit(path, packet, sizeof(packed));
        arbiter.drain();
        arbiter.drain();
        check(raw.packets.size() == 3, "arbiter duplicated or lost a packet");
        for (unsigned i = 0; i < 3; ++i)
        {
            check(uint32_t(raw.packets[i].pathId) == i + 1, "arbiter lost PATH identity");
            check(raw.packets[i].data == std::vector<uint8_t>(packet, packet + sizeof(packed)), "arbiter changed GIF bytes");
        }
        check(gs.processNativePackedGIFPacket(packet, sizeof(packed)), "native packed rejected");
        check(raw.packets.size() == 4 && raw.packets.back().pathId == GifPathId::Path3, "native packed route incorrect");
        check(raw.registers.empty(), "GIF was also decoded by CPU frontend");
        gs.processGIFPacket(packet, 16, 1);
        gs.processGIFPacket(packet + 16, 16, 1);
        check(raw.packets.size() == 6 && raw.packets[4].data.size() == 16, "split GIF was discarded");
        const uint8_t pixels[] = {1, 2, 3, 4, 5};
        gs.uploadImageNative(1, 2, 3, 0, pixels, sizeof(pixels));
        check(raw.registers == std::vector<std::pair<uint8_t, uint64_t>>{{GS_REG_BITBLTBUF, 1}, {GS_REG_TRXPOS, 2}, {GS_REG_TRXREG, 3}, {GS_REG_TRXDIR, 0}}, "native upload duplicated setup");
        check(raw.image == std::vector<uint8_t>(pixels, pixels + sizeof(pixels)), "native upload changed bytes");
        gs.writeRegister(GS_REG_SIGNAL, ~0ull);
        gs.writeRegister(GS_REG_FINISH, 0);
        gs.writeRegister(GS_REG_LABEL, ~0ull);
        check(priv.csr == 0 && priv.siglblid == 0, "frontend executed raw backend IRQ effects twice");
        gs.writeRegister(GS_REG_TEX0_1, 0);
        gs.writeRegister(GS_REG_XYZ2, 0);
        check(raw.batches == 0 && raw.cluts == 0 && raw.transfers == 0, "CPU work leaked into raw route");
        check(gs.getDebugSnapshot().ctx[0].frame.fbp == 37 && gs.getContextFrame(0).fbp == 37, "diagnostics used stale CPU state");
        gs.latchHostPresentationFrame();
        gs.shutdownBackend();
        std::cout << "Raw GIF routing, PATH, native shortcuts and side-effect ownership passed\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
