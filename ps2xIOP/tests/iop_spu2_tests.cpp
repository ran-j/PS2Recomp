#include "emulator/core/iop_spu2.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <vector>

namespace
{
    using ps2x::iop::detail::IopSpu2;

    bool expect(bool value, const char *message)
    {
        if (!value)
            std::cerr << "FAIL: " << message << '\n';
        return value;
    }

    constexpr uint32_t kBlockAddress = 0x2800u; // halfword address in sound RAM

    // One ADPCM block (shift 0, filter 0, every nibble 7 -> sample 0x7000) with the
    // "end, no loop" flag, written through core 0's data port, played by voice 0 at pitch 1.0
    // into core 0's dry output, which core 1 mixes in as its external input.
    void setUpVoice(IopSpu2 &spu, uint32_t extraAttr = 0u)
    {
        spu.write16(0x1A8u, static_cast<uint16_t>(kBlockAddress >> 16u)); // TSA
        spu.write16(0x1AAu, static_cast<uint16_t>(kBlockAddress));
        spu.write16(0x1ACu, 0x0100u); // header: flags = end
        for (int i = 0; i < 7; ++i)
            spu.write16(0x1ACu, 0x7777u);

        spu.write16(0x1C0u, static_cast<uint16_t>(kBlockAddress >> 16u)); // voice 0 SSA
        spu.write16(0x1C2u, static_cast<uint16_t>(kBlockAddress));
        spu.write16(0x000u, 0x3FFFu); // VOLL
        spu.write16(0x002u, 0x3FFFu); // VOLR
        spu.write16(0x004u, 0x1000u); // PITCH = 1.0
        spu.write16(0x006u, 0x000Fu); // ADSR1: fastest attack, sustain level max
        spu.write16(0x008u, 0x0000u); // ADSR2
        spu.write16(0x188u, 0x0001u); // VMIXL voice 0
        spu.write16(0x190u, 0x0001u); // VMIXR voice 0
        spu.write16(0x198u, 0x0C00u); // core 0 MMIX: voices to dry
        spu.write16(0x400u + 0x198u, 0x000Cu); // core 1 MMIX: external input (core 0) to dry
        spu.write16(0x19Au, static_cast<uint16_t>(0x8000u | extraAttr)); // ATTR: core enable
    }
}

int main()
{
    {
        IopSpu2 spu;
        setUpVoice(spu);
        spu.write16(0x1A0u, 0x0001u); // KON voice 0

        std::vector<int16_t> out(64u * 2u);
        spu.generate(out.data(), 64u);

        int32_t peak = 0;
        for (int16_t sample : out)
            peak = std::max<int32_t>(peak, sample < 0 ? -sample : sample);
        if (!expect(peak > 0x1000, "a keyed-on voice should reach the output")) return 1;
        if (!expect((spu.read16(0x340u) & 1u) != 0u, "ENDX should be set once the end block has played")) return 1;
        if (!expect(spu.read16(0x00Au) == 0u, "a voice that ends without looping should be released")) return 1;
        if (!expect(!spu.takeIrq(), "no IRQ should be raised while IRQ is disabled")) return 1;
    }

    {
        IopSpu2 spu;
        setUpVoice(spu, 0x0040u); // ATTR.IRQ enable
        spu.write16(0x19Cu, static_cast<uint16_t>(kBlockAddress >> 16u)); // IRQA
        spu.write16(0x19Eu, static_cast<uint16_t>(kBlockAddress));
        spu.write16(0x1A0u, 0x0001u);

        std::vector<int16_t> out(8u * 2u);
        spu.generate(out.data(), 8u);
        if (!expect(spu.takeIrq(), "a voice reading the IRQA address should raise the SPU2 interrupt")) return 1;
        if (!expect((spu.read16(0x7C2u) & 0x4u) != 0u, "SPDIF_IRQINFO should report core 0")) return 1;
    }

    {
        IopSpu2 spu;
        std::vector<uint8_t> source(64u);
        for (size_t i = 0; i < source.size(); ++i)
            source[i] = static_cast<uint8_t>(i);
        spu.write16(0x400u + 0x1A8u, 0u); // core 1 TSA = 0x100
        spu.write16(0x400u + 0x1AAu, 0x100u);
        spu.dmaWrite(1, source.data(), static_cast<uint32_t>(source.size()));

        std::vector<uint8_t> back(64u, 0u);
        spu.write16(0x400u + 0x1AAu, 0x100u);
        spu.dmaRead(1, back.data(), static_cast<uint32_t>(back.size()));
        if (!expect(back == source, "DMA into sound RAM should read back unchanged")) return 1;
    }

    std::cout << "ps2xIOP SPU2 tests passed\n";
    return 0;
}
