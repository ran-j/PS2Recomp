#include "MiniTest.h"
#include "../../ps2xIOP/src/lle/iop.h"
#include "../../ps2xIOP/src/lle/spu2.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

using ps2x::iop::lle::Iop;
using ps2x::iop::lle::Spu2;

namespace
{
    // Hands the SPU2 queued AutoDMA blocks, each one value for 256 left and
    // 256 right samples, so the output says which block is playing.
    struct BlockSource final : ps2x::iop::lle::Spu2Host
    {
        std::deque<std::array<uint16_t, 512>> blocks;

        void queue(int16_t left)
        {
            std::array<uint16_t, 512> block;
            std::fill(block.begin(), block.begin() + 256, static_cast<uint16_t>(left));
            std::fill(block.begin() + 256, block.end(), static_cast<uint16_t>(-left));
            blocks.push_back(block);
        }

        bool fetchAutoDma(int, uint16_t *block) override
        {
            if (blocks.empty())
                return false;
            std::memcpy(block, blocks.front().data(), 1024u);
            blocks.pop_front();
            return true;
        }

        void raiseSpuInterrupt() override {}
    };

    struct NoEe final : ps2x::iop::lle::EeLink
    {
        bool readEe(uint32_t, void *, uint32_t) override { return false; }
        bool writeEe(uint32_t, const void *, uint32_t) override { return false; }
        bool eeServer(uint32_t) override { return false; }
        void log(const std::string &) override {}
    };

    // SPU2 register offsets: core 0's window, then the per-core volumes.
    constexpr uint32_t kCore1 = 0x400u;
    constexpr uint32_t kMmix = 0x198u;
    constexpr uint32_t kAdmas = 0x1B0u;
    constexpr uint32_t kTsaH = 0x1A8u;
    constexpr uint32_t kTsaL = 0x1AAu;
    constexpr uint32_t kVolumes[2] = {0x760u, 0x788u};
    constexpr uint32_t kMvol = 0x00u;
    constexpr uint32_t kAvol = 0x08u;
    constexpr uint32_t kBvol = 0x0Cu;

    void setVolume(Spu2 &spu, int core, uint32_t which, uint16_t value)
    {
        spu.write(kVolumes[core] + which, value);
        spu.write(kVolumes[core] + which + 2u, value);
    }

    // Core 0's AutoDMA input straight to the output, through core 1's
    // external input, at close to unity gain throughout.
    void routeCore0Input(Spu2 &spu, uint16_t inputVolume, uint16_t externalVolume)
    {
        spu.write(kMmix, 0x0C0u);          // input dry L/R
        spu.write(kCore1 + kMmix, 0x00Cu); // external dry L/R
        setVolume(spu, 0, kMvol, 0x3FFFu);
        setVolume(spu, 1, kMvol, 0x3FFFu);
        setVolume(spu, 1, kAvol, 0x7FFFu);
        setVolume(spu, 0, kBvol, inputVolume);
        setVolume(spu, 0, kAvol, externalVolume);
    }

    // Plays count samples and reports whether all of them carry value, give
    // or take the rounding of four volume stages.
    bool plays(Spu2 &spu, uint32_t count, int32_t value)
    {
        for (uint32_t index = 0u; index < count; ++index)
        {
            int16_t left = 0, right = 0;
            spu.tick(left, right);
            if (std::abs(left - value) > 8 || std::abs(right + value) > 8)
                return false;
        }
        return true;
    }
}

void register_ps2_native_iop_tests()
{
    MiniTest::Case("PS2NativeIop", [](TestCase &tc)
    {
        tc.Run("AutoDMA input plays its blocks in order across DMA restarts", [](TestCase &t)
        {
            BlockSource source;
            Spu2 spu(source);
            routeCore0Input(spu, 0x7FFFu, 0u);
            for (int16_t block = 1; block <= 6; ++block)
                source.queue(static_cast<int16_t>(block * 3000));
            spu.write(kAdmas, 1u);
            t.IsTrue(plays(spu, 256u, 3000), "the first block plays once both halves are primed");
            t.IsTrue(plays(spu, 44u, 6000), "then the second block");
            // Drivers restart the channel for every half they refill.
            spu.dmaStarted(0);
            t.IsTrue(plays(spu, 212u, 6000), "a DMA restart does not move the input position");
            t.IsTrue(plays(spu, 256u, 9000), "and the third block follows");
        });

        tc.Run("AutoDMA starts over from a fresh stream after it was stopped", [](TestCase &t)
        {
            BlockSource source;
            Spu2 spu(source);
            routeCore0Input(spu, 0x7FFFu, 0u);
            for (int16_t block = 1; block <= 4; ++block)
                source.queue(static_cast<int16_t>(block * 3000));
            spu.write(kAdmas, 1u);
            t.IsTrue(plays(spu, 256u, 3000) && plays(spu, 44u, 6000), "the first stream is playing");
            spu.write(kAdmas, 0u);
            t.IsTrue(plays(spu, 64u, 0), "a stopped core has no input");
            source.blocks.clear();
            source.queue(-20000);
            source.queue(-24000);
            spu.write(kAdmas, 1u);
            t.IsTrue(plays(spu, 256u, -20000), "the new stream plays from its first block");
            t.IsTrue(plays(spu, 256u, -24000), "and its second");
        });

        tc.Run("BVOL scales a core's own input and AVOL does not", [](TestCase &t)
        {
            for (bool input : {false, true})
            {
                BlockSource source;
                Spu2 spu(source);
                routeCore0Input(spu, input ? 0x7FFFu : 0u, input ? 0u : 0x7FFFu);
                source.queue(12000);
                source.queue(12000);
                spu.write(kAdmas, 1u);
                t.IsTrue(plays(spu, 256u, input ? 12000 : 0),
                         input ? "BVOL lets the input through" : "AVOL alone leaves the input silent");
            }
        });

        tc.Run("DMA registers take the halfword writes LIBSD makes", [](TestCase &t)
        {
            NoEe ee;
            Iop iop(ee);
            for (uint32_t base : {0x1F8010C0u, 0x1F801500u})
            {
                iop.ioWrite(base + 4u, 0x0010u, 2u);
                iop.ioWrite(base + 6u, 0x0004u, 2u);
                t.Equals(iop.ioRead(base + 4u, 4u), 0x00040010u, "block size and count combine in BCR");
                t.Equals(iop.ioRead(base + 6u, 2u), 0x0004u, "and read back by halves");
            }
        });

        tc.Run("a manual DMA moves data into sound memory and back", [](TestCase &t)
        {
            NoEe ee;
            Iop iop(ee);
            std::vector<uint8_t> sent(64u), received(64u, 0u);
            for (size_t index = 0; index < sent.size(); ++index)
                sent[index] = static_cast<uint8_t>(index * 7u + 3u);
            iop.writeMemory(0x20000u, sent.data(), 64u);
            const auto transfer = [&](uint32_t address, uint32_t chcr)
            {
                iop.ioWrite(0x1F900000u + kTsaH, 0u, 2u);
                iop.ioWrite(0x1F900000u + kTsaL, 0x3000u, 2u);
                iop.ioWrite(0x1F8010C0u, address, 4u);
                iop.ioWrite(0x1F8010C4u, 16u, 2u);
                iop.ioWrite(0x1F8010C6u, 1u, 2u);
                iop.ioWrite(0x1F8010C8u, chcr, 4u);
            };
            transfer(0x20000u, 0x01000201u);
            transfer(0x30000u, 0x01000200u);
            iop.readMemory(0x30000u, received.data(), 64u);
            t.IsTrue(received == sent, "64 bytes written at TSA come back unchanged");
            t.Equals(iop.ioRead(0x1F8010C8u, 4u) & 0x01000000u, 0u, "the channel is idle again");
        });
    });
}
