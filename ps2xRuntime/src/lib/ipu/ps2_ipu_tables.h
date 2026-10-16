#pragma once
// MPEG-1/MPEG-2 intra-decode tables used by the EE IPU's IDEC command.
//
// Source: ISO/IEC 13818-2 Annex B (B.1 macroblock_address_increment,
// B.12/B.13 dct_dc_size, B.14/B.15 DCT coefficient tables), 7.3 scan
// orders, table 7-6 non-linear quantiser_scale. Transcribed from (and
// cross-checked against) FFmpeg libavcodec/mpeg12data.c and
// mpegvideodata.c. Codes exclude the trailing sign bit.
#include <cstdint>

namespace ps2ipu
{
    struct Vlc
    {
        uint16_t code;
        uint8_t len;
    };

    // Table B.1: index i -> macroblock_address_increment i + 1.
    inline constexpr Vlc kMbaIncrement[33] = {
        {0x1, 1}, {0x3, 3}, {0x2, 3}, {0x3, 4}, {0x2, 4}, {0x3, 5}, {0x2, 5},
        {0x7, 7}, {0x6, 7}, {0xb, 8}, {0xa, 8}, {0x9, 8}, {0x8, 8}, {0x7, 8},
        {0x6, 8}, {0x17, 10}, {0x16, 10}, {0x15, 10}, {0x14, 10}, {0x13, 10},
        {0x12, 10}, {0x23, 11}, {0x22, 11}, {0x21, 11}, {0x20, 11}, {0x1f, 11},
        {0x1e, 11}, {0x1d, 11}, {0x1c, 11}, {0x1b, 11}, {0x1a, 11}, {0x19, 11},
        {0x18, 11},
    };
    inline constexpr Vlc kMbaEscape{0x8, 11};   // +33
    inline constexpr Vlc kMbaStuffing{0xf, 11}; // MPEG-1 macroblock_stuffing

    // Tables B.12 / B.13: index = dct_dc_size.
    inline constexpr Vlc kDcSizeLuma[12] = {
        {0x4, 3}, {0x0, 2}, {0x1, 2}, {0x5, 3}, {0x6, 3}, {0xe, 4},
        {0x1e, 5}, {0x3e, 6}, {0x7e, 7}, {0xfe, 8}, {0x1fe, 9}, {0x1ff, 9},
    };
    inline constexpr Vlc kDcSizeChroma[12] = {
        {0x0, 2}, {0x1, 2}, {0x2, 2}, {0x6, 3}, {0xe, 4}, {0x1e, 5},
        {0x3e, 6}, {0x7e, 7}, {0xfe, 8}, {0x1fe, 9}, {0x3fe, 10}, {0x3ff, 10},
    };

    // Tables B.14 ("table zero") and B.15 ("table one"); entry i codes
    // (kRunLevelRun[i], kRunLevelLevel[i]). The B.14 (0,1) entry is the
    // "not first coefficient" form 11s, which is what intra AC uses.
    inline constexpr int kRunLevelCount = 111;
    inline constexpr Vlc kDctTableZero[kRunLevelCount] = {
        {0x3, 2}, {0x4, 4}, {0x5, 5}, {0x6, 7}, {0x26, 8}, {0x21, 8}, {0xa, 10}, {0x1d, 12},
        {0x18, 12}, {0x13, 12}, {0x10, 12}, {0x1a, 13}, {0x19, 13}, {0x18, 13}, {0x17, 13}, {0x1f, 14},
        {0x1e, 14}, {0x1d, 14}, {0x1c, 14}, {0x1b, 14}, {0x1a, 14}, {0x19, 14}, {0x18, 14}, {0x17, 14},
        {0x16, 14}, {0x15, 14}, {0x14, 14}, {0x13, 14}, {0x12, 14}, {0x11, 14}, {0x10, 14}, {0x18, 15},
        {0x17, 15}, {0x16, 15}, {0x15, 15}, {0x14, 15}, {0x13, 15}, {0x12, 15}, {0x11, 15}, {0x10, 15},
        {0x3, 3}, {0x6, 6}, {0x25, 8}, {0xc, 10}, {0x1b, 12}, {0x16, 13}, {0x15, 13}, {0x1f, 15},
        {0x1e, 15}, {0x1d, 15}, {0x1c, 15}, {0x1b, 15}, {0x1a, 15}, {0x19, 15}, {0x13, 16}, {0x12, 16},
        {0x11, 16}, {0x10, 16}, {0x5, 4}, {0x4, 7}, {0xb, 10}, {0x14, 12}, {0x14, 13}, {0x7, 5},
        {0x24, 8}, {0x1c, 12}, {0x13, 13}, {0x6, 5}, {0xf, 10}, {0x12, 12}, {0x7, 6}, {0x9, 10},
        {0x12, 13}, {0x5, 6}, {0x1e, 12}, {0x14, 16}, {0x4, 6}, {0x15, 12}, {0x7, 7}, {0x11, 12},
        {0x5, 7}, {0x11, 13}, {0x27, 8}, {0x10, 13}, {0x23, 8}, {0x1a, 16}, {0x22, 8}, {0x19, 16},
        {0x20, 8}, {0x18, 16}, {0xe, 10}, {0x17, 16}, {0xd, 10}, {0x16, 16}, {0x8, 10}, {0x15, 16},
        {0x1f, 12}, {0x1a, 12}, {0x19, 12}, {0x17, 12}, {0x16, 12}, {0x1f, 13}, {0x1e, 13}, {0x1d, 13},
        {0x1c, 13}, {0x1b, 13}, {0x1f, 16}, {0x1e, 16}, {0x1d, 16}, {0x1c, 16}, {0x1b, 16},
    };
    inline constexpr Vlc kDctTableOne[kRunLevelCount] = {
        {0x02, 2}, {0x06, 3}, {0x07, 4}, {0x1c, 5}, {0x1d, 5}, {0x05, 6}, {0x04, 6}, {0x7b, 7},
        {0x7c, 7}, {0x23, 8}, {0x22, 8}, {0xfa, 8}, {0xfb, 8}, {0xfe, 8}, {0xff, 8}, {0x1f, 14},
        {0x1e, 14}, {0x1d, 14}, {0x1c, 14}, {0x1b, 14}, {0x1a, 14}, {0x19, 14}, {0x18, 14}, {0x17, 14},
        {0x16, 14}, {0x15, 14}, {0x14, 14}, {0x13, 14}, {0x12, 14}, {0x11, 14}, {0x10, 14}, {0x18, 15},
        {0x17, 15}, {0x16, 15}, {0x15, 15}, {0x14, 15}, {0x13, 15}, {0x12, 15}, {0x11, 15}, {0x10, 15},
        {0x02, 3}, {0x06, 5}, {0x79, 7}, {0x27, 8}, {0x20, 8}, {0x16, 13}, {0x15, 13}, {0x1f, 15},
        {0x1e, 15}, {0x1d, 15}, {0x1c, 15}, {0x1b, 15}, {0x1a, 15}, {0x19, 15}, {0x13, 16}, {0x12, 16},
        {0x11, 16}, {0x10, 16}, {0x05, 5}, {0x07, 7}, {0xfc, 8}, {0x0c, 10}, {0x14, 13}, {0x07, 5},
        {0x26, 8}, {0x1c, 12}, {0x13, 13}, {0x06, 6}, {0xfd, 8}, {0x12, 12}, {0x07, 6}, {0x04, 9},
        {0x12, 13}, {0x06, 7}, {0x1e, 12}, {0x14, 16}, {0x04, 7}, {0x15, 12}, {0x05, 7}, {0x11, 12},
        {0x78, 7}, {0x11, 13}, {0x7a, 7}, {0x10, 13}, {0x21, 8}, {0x1a, 16}, {0x25, 8}, {0x19, 16},
        {0x24, 8}, {0x18, 16}, {0x05, 9}, {0x17, 16}, {0x07, 9}, {0x16, 16}, {0x0d, 10}, {0x15, 16},
        {0x1f, 12}, {0x1a, 12}, {0x19, 12}, {0x17, 12}, {0x16, 12}, {0x1f, 13}, {0x1e, 13}, {0x1d, 13},
        {0x1c, 13}, {0x1b, 13}, {0x1f, 16}, {0x1e, 16}, {0x1d, 16}, {0x1c, 16}, {0x1b, 16},
    };
    inline constexpr uint8_t kRunLevelLevel[kRunLevelCount] = {
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
        17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32,
        33, 34, 35, 36, 37, 38, 39, 40, 1, 2, 3, 4, 5, 6, 7, 8,
        9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 1, 2, 3, 4, 5, 1,
        2, 3, 4, 1, 2, 3, 1, 2, 3, 1, 2, 3, 1, 2, 1, 2,
        1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    };
    inline constexpr uint8_t kRunLevelRun[kRunLevelCount] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 3,
        3, 3, 3, 4, 4, 4, 5, 5, 5, 6, 6, 6, 7, 7, 8, 8,
        9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 15, 16, 16,
        17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
    };
    inline constexpr Vlc kDctEscape{0x1, 6};
    inline constexpr Vlc kDctEobTableZero{0x2, 2};
    inline constexpr Vlc kDctEobTableOne{0x6, 4};

    // 7.3: scan index -> raster position.
    inline constexpr uint8_t kZigzagScan[64] = {
        0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5,
        12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
        35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
        58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
    };
    inline constexpr uint8_t kAlternateScan[64] = {
        0, 8, 16, 24, 1, 9, 2, 10, 17, 25, 32, 40, 48, 56, 57, 49,
        41, 33, 26, 18, 3, 11, 4, 12, 19, 27, 34, 42, 50, 58, 35, 43,
        51, 59, 20, 28, 5, 13, 6, 14, 21, 29, 36, 44, 52, 60, 37, 45,
        53, 61, 22, 30, 7, 15, 23, 31, 38, 46, 54, 62, 39, 47, 55, 63,
    };

    // Table 7-6: quantiser_scale_code -> quantiser_scale (q_scale_type 1).
    inline constexpr uint8_t kNonLinearQuantiserScale[32] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 14, 16, 18, 20, 22,
        24, 28, 32, 36, 40, 44, 48, 52, 56, 64, 72, 80, 88, 96, 104, 112,
    };
} // namespace ps2ipu
