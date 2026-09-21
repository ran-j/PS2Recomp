#ifndef PS2_VU1_FMAC_FLAGS_H
#define PS2_VU1_FMAC_FLAGS_H

#include "ps2_vu1_detail.h"
#include <bit>
#include <cstdint>
#include <cstring>

namespace ps2_vu_detail {

struct FmacFlagBits {
    uint32_t mac;
    uint32_t status;
};

PS2_VU_FORCE_INLINE FmacFlagBits packFmacFlags(const uint8_t laneFlags[4], uint8_t destination)
{
    const uint32_t lanes = destination & 15u;
    if (std::has_single_bit(lanes))
    {
        const uint32_t flags = laneFlags[3u - std::countr_zero(lanes)];
        const uint32_t spread = (flags & 1u) | ((flags & 2u) << 3u) |
                                ((flags & 4u) << 6u) | ((flags & 8u) << 9u);
        return {spread * lanes, flags};
    }
    uint32_t bytes;
    std::memcpy(&bytes, laneFlags, sizeof(bytes));
    if constexpr (std::endian::native == std::endian::big)
        bytes = (bytes >> 24u) | ((bytes >> 8u) & 0xff00u) |
                ((bytes << 8u) & 0xff0000u) | (bytes << 24u);
    const uint32_t active = ((destination & 8u) ? 0x000000ffu : 0u) |
                            ((destination & 4u) ? 0x0000ff00u : 0u) |
                            ((destination & 2u) ? 0x00ff0000u : 0u) |
                            ((destination & 1u) ? 0xff000000u : 0u);
    bytes &= active;
    uint32_t mac = ((bytes & 0x0000000fu) << 12u) | (bytes & 0x00000f00u) |
                   ((bytes & 0x000f0000u) >> 12u) | ((bytes >> 24u) & 0x0fu);
    // Transpose four lane nibbles into the four architectural flag groups.
    uint32_t swap = (mac ^ (mac >> 3u)) & 0x0a0au;
    mac ^= swap ^ (swap << 3u);
    swap = (mac ^ (mac >> 6u)) & 0x00ccu;
    mac ^= swap ^ (swap << 6u);
    const uint32_t folded = bytes | (bytes >> 16u);
    return {mac, (folded | (folded >> 8u)) & 0xffu};
}

}
#endif
