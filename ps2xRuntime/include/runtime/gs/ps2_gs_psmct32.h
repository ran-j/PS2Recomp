#ifndef PS2_GS_PSMCT32_H
#define PS2_GS_PSMCT32_H

#include <cstdint>

namespace GSPSMCT32
{

    static constexpr uint8_t blockTable32[4][8] = {
        {0, 1, 4, 5, 16, 17, 20, 21},
        {2, 3, 6, 7, 18, 19, 22, 23},
        {8, 9, 12, 13, 24, 25, 28, 29},
        {10, 11, 14, 15, 26, 27, 30, 31},
    };

    static constexpr uint8_t columnTable32[8][8] = {
        {0, 1, 4, 5, 8, 9, 12, 13},
        {2, 3, 6, 7, 10, 11, 14, 15},
        {16, 17, 20, 21, 24, 25, 28, 29},
        {18, 19, 22, 23, 26, 27, 30, 31},
        {32, 33, 36, 37, 40, 41, 44, 45},
        {34, 35, 38, 39, 42, 43, 46, 47},
        {48, 49, 52, 53, 56, 57, 60, 61},
        {50, 51, 54, 55, 58, 59, 62, 63},
    };

    inline uint32_t addrPSMCT32(uint32_t block, uint32_t width, uint32_t x, uint32_t y)
    {
        const uint32_t pagesPerRow = (width != 0u) ? width : 1u;
        const uint32_t page = (block >> 5u) + (y >> 5u) * pagesPerRow + (x >> 6u);
        const uint32_t blockId = (block & 0x1Fu) + blockTable32[(y >> 3u) & 3u][(x >> 3u) & 7u];
        const uint32_t pageOffset = (blockId >> 5u) << 13u;
        const uint32_t localBlock = blockId & 0x1Fu;
        return (page << 13u) + pageOffset + localBlock * 256u +
               static_cast<uint32_t>(columnTable32[y & 0x7u][x & 0x7u]) * 4u;
    }

}

#endif
