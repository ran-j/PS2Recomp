#include "emulator/core/iop_memory.h"
#include "emulator/core/iop_spu2.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

namespace
{
    using namespace ps2x::iop::detail;

    constexpr uint32_t kSpu = 0x1F900000u;
    constexpr uint32_t kCore1 = 0x400u;

    bool expect(bool condition, std::string_view message)
    {
        if (condition)
            return true;
        std::cerr << "FAIL: " << message << '\n';
        return false;
    }

    // Builds one ADPCM block; nibbles are given in playback order.
    std::vector<uint16_t> adpcmBlock(uint8_t shift, uint8_t filter, uint8_t flags, const std::vector<uint8_t> &nibbles)
    {
        std::vector<uint16_t> block(8u, 0u);
        block[0] = static_cast<uint16_t>((shift & 0x0Fu) | ((filter & 0x07u) << 4u) | (flags << 8u));
        for (size_t i = 0; i < 28u && i < nibbles.size(); ++i)
            block[1u + i / 4u] |= static_cast<uint16_t>((nibbles[i] & 0x0Fu) << ((i & 3u) * 4u));
        return block;
    }

    void writeSpuRam(IopMemory &memory, uint32_t core, uint32_t address, const std::vector<uint16_t> &words)
    {
        const uint32_t base = kSpu + core * kCore1;
        memory.write16(base + 0x1A8u, static_cast<uint16_t>(address >> 16u));
        memory.write16(base + 0x1AAu, static_cast<uint16_t>(address));
        for (const uint16_t word : words)
            memory.write16(base + 0x1ACu, word);
    }

    void setVolume(IopMemory &memory, uint32_t reg, uint16_t value)
    {
        memory.write16(kSpu + reg, value);
        memory.write16(kSpu + reg + 2u, value);
    }

    bool testAdpcmDecode()
    {
        // Shift 12 leaves the raw signed nibble; filter 1 then adds 60/64 of the previous sample.
        const std::vector<uint16_t> raw = adpcmBlock(12u, 0u, 0u, {1u, 7u, 8u, 0xFu});
        int16_t out[28];
        AdpcmHistory history;
        decodeAdpcmBlock(raw.data(), out, history);
        if (!expect(out[0] == 1 && out[1] == 7 && out[2] == -8 && out[3] == -1 && out[4] == 0,
                    "shift 12 / filter 0 should decode signed nibbles unchanged"))
            return false;

        const std::vector<uint16_t> filtered = adpcmBlock(12u, 1u, 0u, {7u, 0u, 0u});
        history = {};
        decodeAdpcmBlock(filtered.data(), out, history);
        if (!expect(out[0] == 7 && out[1] == 7 && out[2] == 7, "filter 1 should carry (prev * 60 + 32) >> 6"))
            return false;

        // Shift 0 puts the nibble in the top bits; the result saturates instead of wrapping.
        const std::vector<uint16_t> loud = adpcmBlock(0u, 4u, 0u, {7u, 7u, 7u});
        history = {};
        decodeAdpcmBlock(loud.data(), out, history);
        return expect(out[0] == 0x7000 && out[1] == 0x7FFF && out[2] == 0x7FFF,
                      "filter 4 on loud input should clamp to 16 bits");
    }

    // Core 1 voice 0 plays a two-block one-shot sample at 1:1 pitch with a near-instant attack.
    void setupOneShotVoice(IopMemory &memory, uint32_t address)
    {
        std::vector<uint16_t> data = adpcmBlock(0u, 0u, 0u, std::vector<uint8_t>(28u, 7u));
        const std::vector<uint16_t> last = adpcmBlock(0u, 0u, 0x01u, std::vector<uint8_t>(28u, 7u));
        data.insert(data.end(), last.begin(), last.end());
        writeSpuRam(memory, 1u, address, data);

        const uint32_t voice = kSpu + kCore1;
        memory.write16(voice + 0x00u, 0x3FFFu); // VOLL
        memory.write16(voice + 0x02u, 0x3FFFu); // VOLR
        memory.write16(voice + 0x04u, 0x1000u); // PITCH 1.0
        memory.write16(voice + 0x06u, 0x000Fu); // ADSR1: fastest linear attack, full sustain level
        memory.write16(voice + 0x08u, 0x1FC0u); // ADSR2: sustain held
        memory.write16(voice + 0x1C0u, static_cast<uint16_t>(address >> 16u));
        memory.write16(voice + 0x1C2u, static_cast<uint16_t>(address));
        memory.write16(voice + 0x188u, 1u); // VMIXL
        memory.write16(voice + 0x190u, 1u); // VMIXR
        memory.write16(voice + 0x198u, 0x0C00u); // MMIX: voices to dry out
        setVolume(memory, 0x788u, 0x3FFFu); // core 1 MVOL
    }

    bool testVoicePlaysAndEnds()
    {
        IopMemory memory;
        setupOneShotVoice(memory, 0x5000u);
        memory.write16(kSpu + kCore1 + 0x1A0u, 1u); // KON voice 0

        std::vector<int16_t> out;
        memory.mixSpu2(40u, out);
        const int16_t peak = *std::max_element(out.begin(), out.end());
        if (!expect(peak > 0x3000, "a keyed-on voice should reach the DAC at close to its sample level") ||
            !expect(memory.read16(kSpu + kCore1 + 0x0Au) == 0x7FFFu, "ENVX should report the finished attack") ||
            !expect(memory.read16(kSpu + kCore1 + 0x340u) == 0u, "ENDX must stay clear mid-sample"))
            return false;

        out.clear();
        memory.mixSpu2(40u, out);
        if (!expect((memory.read16(kSpu + kCore1 + 0x340u) & 1u) != 0u, "ENDX should flag the voice after the loop-end block") ||
            !expect(memory.read16(kSpu + kCore1 + 0x0Au) == 0u, "a loop end without repeat silences the voice"))
            return false;
        out.clear();
        memory.mixSpu2(16u, out);
        if (!expect(std::all_of(out.begin(), out.end(), [](int16_t sample)
                                { return sample == 0; }),
                    "a stopped voice must not keep sounding"))
            return false;

        // Core 0 voices reach the DAC only through core 1's external input.
        memory.write16(kSpu + kCore1 + 0x198u, 0x0000u);
        out.clear();
        memory.write16(kSpu + kCore1 + 0x1A0u, 1u);
        memory.mixSpu2(20u, out);
        return expect(std::all_of(out.begin(), out.end(), [](int16_t sample)
                                  { return sample == 0; }),
                      "MMIX gates the voice sum");
    }

    bool testVoiceIrqAndKeyOff()
    {
        IopMemory memory;
        setupOneShotVoice(memory, 0x6000u);
        memory.write16(kSpu + kCore1 + 0x19Cu, 0u);
        memory.write16(kSpu + kCore1 + 0x19Eu, 0x6008u); // IRQA at the second block
        memory.write16(kSpu + kCore1 + 0x19Au, 0x8040u); // ATTR: core on, IRQ enabled
        memory.write16(kSpu + kCore1 + 0x1A0u, 1u);

        std::vector<int16_t> out;
        memory.mixSpu2(20u, out);
        if (!expect(!memory.takeDmaStart().has_value(), "the IRQ must wait for the voice to reach IRQA"))
            return false;
        memory.mixSpu2(20u, out);
        const auto irq = memory.takeDmaStart();
        if (!expect(irq && irq->irq == IopSpu2::kIrq, "reading the IRQA block should raise the SPU2 interrupt") ||
            !expect(memory.read16(kSpu + 0x7C2u) == 8u, "IRQINFO should name core 1") ||
            !expect(memory.read16(kSpu + 0x7C2u) == 0u, "IRQINFO clears once read"))
            return false;

        // Key off releases instead of cutting.
        memory.write16(kSpu + kCore1 + 0x08u, 0x1FC0u | 0x000Au);
        memory.write16(kSpu + kCore1 + 0x1A0u, 1u);
        memory.mixSpu2(10u, out);
        memory.write16(kSpu + kCore1 + 0x1A4u, 1u);
        memory.mixSpu2(1u, out);
        const uint16_t released = memory.read16(kSpu + kCore1 + 0x0Au);
        return expect(released > 0u && released < 0x7FFFu, "KOFF should start a gradual release");
    }

    bool testDmaWritesSoundRam()
    {
        IopMemory memory;
        std::vector<uint8_t> payload(256u);
        for (size_t i = 0; i < payload.size(); ++i)
            payload[i] = static_cast<uint8_t>(i * 3u);
        if (!expect(memory.writeRam(0x40000u, payload.data(), payload.size()), "test payload should fit in IOP RAM"))
            return false;

        memory.write16(kSpu + 0x1A8u, 0x0001u);
        memory.write16(kSpu + 0x1AAu, 0x2345u); // TSA 0x12345
        memory.write32(0x1F8010C0u, 0x40000u); // ch4 MADR
        memory.write32(0x1F8010C4u, (4u << 16u) | 16u); // 4 blocks of 16 words
        memory.write32(0x1F8010C8u, 0x01000201u); // to SPU, start

        const auto irq = memory.takeDmaStart();
        const std::span<uint16_t> ram = memory.spu2().ram();
        return expect(irq && irq->irq == 0x24, "a finished voice transfer should raise the channel 4 IRQ") &&
               expect(ram[0x12345u] == (payload[0] | (payload[1] << 8u)), "DMA should land at TSA") &&
               expect(ram[0x12345u + 127u] == (payload[254] | (payload[255] << 8u)), "DMA should copy the whole BCR size") &&
               expect(memory.read16(kSpu + 0x1AAu) == 0x23C5u, "TSA should advance past the transfer");
    }

    bool testAutoDmaStreamsPcm()
    {
        IopMemory memory;
        // Two 256-frame blocks: left at +1000, right at -1000.
        std::vector<int16_t> pcm(1024u);
        for (size_t block = 0; block < 2u; ++block)
            for (size_t i = 0; i < 256u; ++i)
            {
                pcm[block * 512u + i] = 1000;
                pcm[block * 512u + 256u + i] = -1000;
            }
        if (!expect(memory.writeRam(0x50000u, pcm.data(), pcm.size() * 2u), "PCM should fit in IOP RAM"))
            return false;

        memory.write16(kSpu + 0x1B0u, 1u); // core 0 ADMAS
        memory.write16(kSpu + 0x198u, 0x00C0u); // core 0 MMIX: sound data input to dry out
        memory.write16(kSpu + kCore1 + 0x198u, 0x000Cu); // core 1 MMIX: core 0 to dry out
        setVolume(memory, 0x760u, 0x3FFFu); // core 0 MVOL
        setVolume(memory, 0x76Cu, 0x7FFFu); // core 0 BVOL, plain signed 16-bit unlike MVOL
        setVolume(memory, 0x788u, 0x3FFFu); // core 1 MVOL
        setVolume(memory, 0x790u, 0x7FFFu); // core 1 AVOL

        memory.write32(0x1F8010C0u, 0x50000u);
        memory.write32(0x1F8010C4u, (32u << 16u) | 16u); // 2048 bytes
        memory.write32(0x1F8010C8u, 0x01000201u);
        if (!expect(!memory.takeDmaStart().has_value(), "AutoDMA must not complete before playback drains it") ||
            !expect((memory.read32(0x1F8010C8u) & 0x01000000u) != 0u, "CHCR should stay busy while streaming"))
            return false;

        std::vector<int16_t> out;
        memory.mixSpu2(256u, out);
        if (!expect(out[0] > 900 && out[1] < -900 && out[510] > 900 && out[511] < -900,
                    "streamed PCM should reach both channels through core 1") ||
            !expect(memory.read32(0x1F8010C0u) == 0x50400u, "MADR should advance one block per 256 frames") ||
            !expect(!memory.takeDmaStart().has_value(), "the transfer still has a block left"))
            return false;

        out.clear();
        memory.mixSpu2(256u, out);
        const auto irq = memory.takeDmaStart();
        if (!expect(irq && irq->irq == 0x24, "draining the last block should complete channel 4") ||
            !expect((memory.read32(0x1F8010C8u) & 0x01000000u) == 0u, "CHCR should go idle after the last block"))
            return false;

        out.clear();
        memory.mixSpu2(256u, out);
        return expect(std::all_of(out.begin(), out.end(), [](int16_t sample)
                                  { return sample == 0; }),
                      "an idle AutoDMA input plays silence");
    }
}

int main()
{
    if (!testAdpcmDecode() || !testVoicePlaysAndEnds() || !testVoiceIrqAndKeyOff() || !testDmaWritesSoundRam() ||
        !testAutoDmaStreamsPcm())
        return EXIT_FAILURE;
    std::cout << "ps2xIOP SPU2 tests passed\n";
    return EXIT_SUCCESS;
}
