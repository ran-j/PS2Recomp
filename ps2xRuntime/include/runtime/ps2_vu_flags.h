#pragma once

#include <cstdint>

namespace VUFlags
{
    struct Fmac
    {
        uint32_t mac;
        uint32_t status;
    };

    constexpr Fmac packFmac(const uint8_t *laneFlags, uint8_t dest)
    {
        uint32_t packed = 0u;
        uint32_t status = 0u;
        for (uint32_t component = 0u; component < 4u; ++component)
        {
            if ((dest & (8u >> component)) == 0u)
                continue;
            const uint32_t flags = laneFlags[component];
            packed |= (flags & 0xFu) << (12u - component * 4u);
            status |= flags;
        }
        // Transpose four lane nibbles into the four Z/S/U/O lane masks.
        uint32_t swap = (packed ^ (packed >> 3u)) & 0x0A0Au;
        packed ^= swap ^ (swap << 3u);
        swap = (packed ^ (packed >> 6u)) & 0x00CCu;
        packed ^= swap ^ (swap << 6u);
        return {packed, status};
    }
}
