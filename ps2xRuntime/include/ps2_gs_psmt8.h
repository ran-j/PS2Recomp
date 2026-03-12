#ifndef PS2_GS_PSMT8_H
#define PS2_GS_PSMT8_H

#include <array>
#include <cstdint>

namespace GSPSMT8
{

    static constexpr uint8_t blockTable8[4][8] = {
        {0, 1, 4, 5, 16, 17, 20, 21},
        {2, 3, 6, 7, 18, 19, 22, 23},
        {8, 9, 12, 13, 24, 25, 28, 29},
        {10, 11, 14, 15, 26, 27, 30, 31},
    };

    static constexpr uint8_t blockTable32[32] = {
        0,
        1,
        4,
        5,
        16,
        17,
        20,
        21,
        2,
        3,
        6,
        7,
        18,
        19,
        22,
        23,
        8,
        9,
        12,
        13,
        24,
        25,
        28,
        29,
        10,
        11,
        14,
        15,
        26,
        27,
        30,
        31,
    };

    inline const std::array<uint8_t, 32> &index32X()
    {
        static const std::array<uint8_t, 32> table = []
        {
            std::array<uint8_t, 32> result{};
            for (uint8_t i = 0; i < 4; ++i)
            {
                for (uint8_t j = 0; j < 8; ++j)
                {
                    const uint8_t index = blockTable32[i * 8u + j];
                    result[index] = j;
                }
            }
            return result;
        }();
        return table;
    }

    inline const std::array<uint8_t, 32> &index32Y()
    {
        static const std::array<uint8_t, 32> table = []
        {
            std::array<uint8_t, 32> result{};
            for (uint8_t i = 0; i < 4; ++i)
            {
                for (uint8_t j = 0; j < 8; ++j)
                {
                    const uint8_t index = blockTable32[i * 8u + j];
                    result[index] = i;
                }
            }
            return result;
        }();
        return table;
    }

    inline const std::array<uint8_t, 256> &columnTable8()
    {
        static const std::array<uint8_t, 256> table = []
        {
            std::array<uint8_t, 256> result{};
            static constexpr uint8_t lut[128] = {
                0,
                36,
                8,
                44,
                1,
                37,
                9,
                45,
                2,
                38,
                10,
                46,
                3,
                39,
                11,
                47,
                4,
                32,
                12,
                40,
                5,
                33,
                13,
                41,
                6,
                34,
                14,
                42,
                7,
                35,
                15,
                43,
                16,
                52,
                24,
                60,
                17,
                53,
                25,
                61,
                18,
                54,
                26,
                62,
                19,
                55,
                27,
                63,
                20,
                48,
                28,
                56,
                21,
                49,
                29,
                57,
                22,
                50,
                30,
                58,
                23,
                51,
                31,
                59,
                4,
                32,
                12,
                40,
                5,
                33,
                13,
                41,
                6,
                34,
                14,
                42,
                7,
                35,
                15,
                43,
                0,
                36,
                8,
                44,
                1,
                37,
                9,
                45,
                2,
                38,
                10,
                46,
                3,
                39,
                11,
                47,
                20,
                48,
                28,
                56,
                21,
                49,
                29,
                57,
                22,
                50,
                30,
                58,
                23,
                51,
                31,
                59,
                16,
                52,
                24,
                60,
                17,
                53,
                25,
                61,
                18,
                54,
                26,
                62,
                19,
                55,
                27,
                63,
            };

            uint32_t outputIndex = 0u;
            for (uint32_t k = 0; k < 4u; ++k)
            {
                uint32_t inputBase = (k % 2u) * 64u;
                for (uint32_t i = 0; i < 16u; ++i)
                {
                    for (uint32_t j = 0; j < 4u; ++j)
                    {
                        result[k * 64u + lut[inputBase++]] = static_cast<uint8_t>(outputIndex++);
                    }
                }
            }

            return result;
        }();
        return table;
    }

    inline uint32_t addrPSMT8(uint32_t block, uint32_t width, uint32_t x, uint32_t y)
    {
        const uint32_t page = (block >> 5) + (y >> 6) * (width >> 1) + (x >> 7);
        const uint32_t blockId = (block & 0x1Fu) + blockTable8[(y >> 4) & 3u][(x >> 4) & 7u];
        const uint32_t pageOffset = (blockId >> 5) << 13;
        const uint32_t localBlock = blockId & 0x1Fu;
        const uint32_t blockBase = static_cast<uint32_t>(index32Y()[localBlock]) * 2048u +
                                   static_cast<uint32_t>(index32X()[localBlock]) * 32u;
        const uint32_t column = columnTable8()[(y & 0xFu) * 16u + (x & 0xFu)];
        return (page << 13) + pageOffset + blockBase + column;
    }

}

#endif
