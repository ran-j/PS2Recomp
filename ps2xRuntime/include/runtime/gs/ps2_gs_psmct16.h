#ifndef PS2_GS_PSMCT16_H
#define PS2_GS_PSMCT16_H

#include <cstdint>

namespace GSPSMCT16
{

    static constexpr uint8_t blockTable16[8][4] = {
        {0, 2, 8, 10},
        {1, 3, 9, 11},
        {4, 6, 12, 14},
        {5, 7, 13, 15},
        {16, 18, 24, 26},
        {17, 19, 25, 27},
        {20, 22, 28, 30},
        {21, 23, 29, 31},
    };

    static constexpr uint8_t blockTable16S[8][4] = {
        {0, 2, 16, 18},
        {1, 3, 17, 19},
        {8, 10, 24, 26},
        {9, 11, 25, 27},
        {4, 6, 20, 22},
        {5, 7, 21, 23},
        {12, 14, 28, 30},
        {13, 15, 29, 31},
    };

    static constexpr uint8_t blockTableZ16[8][4] = {
        {24, 26, 16, 18},
        {25, 27, 17, 19},
        {28, 30, 20, 22},
        {29, 31, 21, 23},
        {8, 10, 0, 2},
        {9, 11, 1, 3},
        {12, 14, 4, 6},
        {13, 15, 5, 7},
    };

    static constexpr uint8_t blockTableZ16S[8][4] = {
        {24, 26, 8, 10},
        {25, 27, 9, 11},
        {16, 18, 0, 2},
        {17, 19, 1, 3},
        {28, 30, 12, 14},
        {29, 31, 13, 15},
        {20, 22, 4, 6},
        {21, 23, 5, 7},
    };

    static constexpr uint8_t columnTable16[2][16] = {
        {0, 2, 8, 10, 16, 18, 24, 26, 1, 3, 9, 11, 17, 19, 25, 27},
        {4, 6, 12, 14, 20, 22, 28, 30, 5, 7, 13, 15, 21, 23, 29, 31},
    };

    inline uint32_t addrPSMCT16Like(uint32_t block,
                                    uint32_t width,
                                    uint32_t x,
                                    uint32_t y,
                                    const uint8_t (&blockTable)[8][4])
    {
        const uint32_t pagesPerRow = (width != 0u) ? width : 1u;
        const uint32_t page = (block >> 5u) + (y >> 6u) * pagesPerRow + (x >> 6u);
        const uint32_t blockId = (block & 0x1Fu) + blockTable[(y >> 3u) & 0x7u][(x >> 4u) & 0x3u];
        const uint32_t pageOffset = (blockId >> 5u) << 13u;
        const uint32_t localBlock = blockId & 0x1Fu;
        const uint32_t columnOffset = ((y >> 1u) & 0x3u) * 64u;
        return (page << 13u) + pageOffset + localBlock * 256u + columnOffset +
               static_cast<uint32_t>(columnTable16[y & 0x1u][x & 0x0Fu]) * 2u;
    }

    inline uint32_t addrPSMCT16(uint32_t block, uint32_t width, uint32_t x, uint32_t y)
    {
        return addrPSMCT16Like(block, width, x, y, blockTable16);
    }

    inline uint32_t addrPSMCT16S(uint32_t block, uint32_t width, uint32_t x, uint32_t y)
    {
        return addrPSMCT16Like(block, width, x, y, blockTable16S);
    }

    inline uint32_t addrPSMZ16(uint32_t block, uint32_t width, uint32_t x, uint32_t y)
    {
        return addrPSMCT16Like(block, width, x, y, blockTableZ16);
    }

    inline uint32_t addrPSMZ16S(uint32_t block, uint32_t width, uint32_t x, uint32_t y)
    {
        return addrPSMCT16Like(block, width, x, y, blockTableZ16S);
    }

}

#endif
