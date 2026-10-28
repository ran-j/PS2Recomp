#include "iop_compat_test_support.h"

#include "emulator/core/iop_memory.h"

namespace
{
    using namespace iop_test;
    using namespace ps2x::iop::detail;

    constexpr uint32_t kSpu0Bcr = 0x1F8010C4u;
    constexpr uint32_t kSpu0Chcr = 0x1F8010C8u;
    constexpr uint32_t kSpu0Admas = 0x1F9001B0u;
    constexpr uint32_t kChcrStartToSpu = 0x01000201u;

    void spu2DmaCompletesAtBusSpeed()
    {
        IopMemory memory;
        memory.write32(kSpu0Bcr, (1u << 16u) | 16u); // one block of 16 words
        memory.write32(kSpu0Chcr, kChcrStartToSpu);
        const auto start = memory.takeDmaStart();
        require(start.has_value(), "SPU2 DMA did not start");
        require(start->delayCycles == 64u, "a plain SPU2 DMA should complete at bus speed");
    }

    void spu2AutoDmaCompletesAtPlaybackSpeed()
    {
        // Auto-DMA feeds the SPU2 at its playback rate: 48 kHz stereo 16-bit
        // is 192000 bytes/s, 192 IOP cycles per byte.
        IopMemory memory;
        memory.write16(kSpu0Admas, 1u);
        memory.write32(kSpu0Bcr, (1u << 16u) | 16u);
        memory.write32(kSpu0Chcr, kChcrStartToSpu);
        const auto start = memory.takeDmaStart();
        require(start.has_value(), "SPU2 auto-DMA did not start");
        require(start->delayCycles == 16u * 4u * 192u, "SPU2 auto-DMA should complete at playback speed");
    }
}

int main()
{
    const Test tests[] = {
        {"SPU2 DMA completes at bus speed", spu2DmaCompletesAtBusSpeed},
        {"SPU2 auto-DMA completes at playback speed", spu2AutoDmaCompletesAtPlaybackSpeed},
    };
    return run(tests);
}
