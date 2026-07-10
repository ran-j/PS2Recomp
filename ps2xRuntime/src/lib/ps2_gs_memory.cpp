#include <array>

#include "runtime/ps2_gs_memory.h"

namespace GSMem
{
    using enum PixelStorageMode;

    using C32Traits  = PixelStorageTraits<C32>;
    using Z32Traits  = PixelStorageTraits<Z32>;
    using C16Traits  = PixelStorageTraits<C16>;
    using C16STraits = PixelStorageTraits<C16S>;
    using Z16Traits  = PixelStorageTraits<Z16>;
    using Z16STraits = PixelStorageTraits<Z16S>;
    using P8Traits   = PixelStorageTraits<P8>;
    using P4Traits   = PixelStorageTraits<P4>;

    using C32PageLookupTable    = C32Traits::PageLookupTableT;
    using C32BlockLookupTableT  = C32Traits::BlockLookupTableT;
    using Z32PageLookupTableT = Z32Traits::PageLookupTableT;
    using Z32BlockLookupTableT = Z32Traits::BlockLookupTableT;

    // column table is shared
    using C32ColumnLookupTableT = C32Traits::ColumnLookupTableT;

    using C16PageLookupTable   = C16Traits::PageLookupTableT;
    using C16BlockLookupTable  = C16Traits::BlockLookupTableT;
    using Z16PageLookupTable   = Z16Traits::PageLookupTableT;
    using Z16BlockLookupTable  = Z16Traits::BlockLookupTableT;

    using C16SPageLookupTable  = C16STraits::PageLookupTableT;
    using C16SBlockLookupTable = C16STraits::BlockLookupTableT;
    using Z16SPageLookupTable  = Z16STraits::PageLookupTableT;
    using Z16SBlockLookupTable = Z16STraits::BlockLookupTableT;

    // column table is shared
    using C16ColumnLookupTable = C16Traits::ColumnLookupTableT;

    using P8PageLookupTable   = P8Traits::PageLookupTableT;
    using P8BlockLookupTable  = P8Traits::BlockLookupTableT;
    using P8ColumnLookupTable = P8Traits::ColumnLookupTableT;

    using P4PageLookupTable   = P4Traits::PageLookupTableT;
    using P4BlockLookupTable  = P4Traits::BlockLookupTableT;
    using P4ColumnLookupTable = P4Traits::ColumnLookupTableT;

    static constexpr C32BlockLookupTableT BlockTableC32
    {{
        {  0,  1,  4,  5, 16, 17, 20, 21 },
        {  2,  3,  6,  7, 18, 19, 22, 23 },
        {  8,  9, 12, 13, 24, 25, 28, 29 },
        { 10, 11, 14, 15, 26, 27, 30, 31 }
    }};

    static constexpr Z32BlockLookupTableT BlockTableZ32
    {{
        { 24, 25, 28, 29,  8,  9, 12, 13 },
        { 26, 27, 30, 31, 10, 11, 14, 15 },
        { 16, 17, 20, 21,  0,  1,  4,  5 },
        { 18, 19, 22, 23,  2,  3,  6,  7 }
    }};

    static constexpr C16BlockLookupTable BlockTableC16
    {{
        {  0,  2,  8, 10 },
        {  1,  3,  9, 11 },
        {  4,  6, 12, 14 },
        {  5,  7, 13, 15 },
        { 16, 18, 24, 26 },
        { 17, 19, 25, 27 },
        { 20, 22, 28, 30 },
        { 21, 23, 29, 31 }
    }};

    static constexpr C16SBlockLookupTable BlockTableC16S
    {{
        {  0,  2, 16, 18 },
        {  1,  3, 17, 19 },
        {  8, 10, 24, 26 },
        {  9, 11, 25, 27 },
        {  4,  6, 20, 22 },
        {  5,  7, 21, 23 },
        { 12, 14, 28, 30 },
        { 13, 15, 29, 31 }
    }};

    static constexpr Z16BlockLookupTable BlockTableZ16
    {{
        { 24, 26, 16, 18 },
        { 25, 27, 17, 19 },
        { 28, 30, 20, 22 },
        { 29, 31, 21, 23 },
        {  8, 10,  0,  2 },
        {  9, 11,  1,  3 },
        { 12, 14,  4,  6 },
        { 13, 15,  5,  7 }
    }};

    static constexpr Z16SBlockLookupTable BlockTableZ16S
    {{
        { 24, 26,  8, 10 },
        { 25, 27,  9, 11 },
        { 16, 18,  0,  2 },
        { 17, 19,  1,  3 },
        { 28, 30, 12, 14 },
        { 29, 31, 13, 15 },
        { 20, 22,  4,  6 },
        { 21, 23,  5,  7 }
    }};

    static constexpr P8BlockLookupTable BlockTableP8
    {{
        {  0,  1,  4,  5, 16, 17, 20, 21},
        {  2,  3,  6,  7, 18, 19, 22, 23},
        {  8,  9, 12, 13, 24, 25, 28, 29},
        { 10, 11, 14, 15, 26, 27, 30, 31}
    }};

    static constexpr P4BlockLookupTable BlockTableP4
    {{
        {  0,  2,  8, 10 },
        {  1,  3,  9, 11 },
        {  4,  6, 12, 14 },
        {  5,  7, 13, 15 },
        { 16, 18, 24, 26 },
        { 17, 19, 25, 27 },
        { 20, 22, 28, 30 },
        { 21, 23, 29, 31 }
    }};

    // column layout is shared between c32 and z32
    static constexpr C32ColumnLookupTableT ColumnTable32
    {{
        {  0,  1,  4,  5,  8,  9, 12, 13 },
        {  2,  3,  6,  7, 10, 11, 14, 15 },
        { 16, 17, 20, 21, 24, 25, 28, 29 },
        { 18, 19, 22, 23, 26, 27, 30, 31 },
        { 32, 33, 36, 37, 40, 41, 44, 45 },
        { 34, 35, 38, 39, 42, 43, 46, 47 },
        { 48, 49, 52, 53, 56, 57, 60, 61 },
        { 50, 51, 54, 55, 58, 59, 62, 63 },
    }};

    static constexpr C16ColumnLookupTable ColumnTable16
    {{
        {   0,   2,   8,  10,  16,  18,  24,  26,   1,   3,   9,  11,  17,  19,  25,  27 },
        {   4,   6,  12,  14,  20,  22,  28,  30,   5,   7,  13,  15,  21,  23,  29,  31 },
        {  32,  34,  40,  42,  48,  50,  56,  58,  33,  35,  41,  43,  49,  51,  57,  59 },
        {  36,  38,  44,  46,  52,  54,  60,  62,  37,  39,  45,  47,  53,  55,  61,  63 },
        {  64,  66,  72,  74,  80,  82,  88,  90,  65,  67,  73,  75,  81,  83,  89,  91 },
        {  68,  70,  76,  78,  84,  86,  92,  94,  69,  71,  77,  79,  85,  87,  93,  95 },
        {  96,  98, 104, 106, 112, 114, 120, 122,  97,  99, 105, 107, 113, 115, 121, 123 },
        { 100, 102, 108, 110, 116, 118, 124, 126, 101, 103, 109, 111, 117, 119, 125, 127 }
    }};

    static constexpr P8ColumnLookupTable ColumnTable8
    {{
        {   0,   4,  16,  20,  32,  36,  48,  52,   2,   6,  18,  22,  34,  38,  50,  54 },
        {   8,  12,  24,  28,  40,  44,  56,  60,  10,  14,  26,  30,  42,  46,  58,  62 },
        {  33,  37,  49,  53,   1,   5,  17,  21,  35,  39,  51,  55,   3,   7,  19,  23 },
        {  41,  45,  57,  61,   9,  13,  25,  29,  43,  47,  59,  63,  11,  15,  27,  31 },
        {  96, 100, 112, 116,  64,  68,  80,  84,  98, 102, 114, 118,  66,  70,  82,  86 },
        { 104, 108, 120, 124,  72,  76,  88,  92, 106, 110, 122, 126,  74,  78,  90,  94 },
        {  65,  69,  81,  85,  97, 101, 113, 117,  67,  71,  83,  87,  99, 103, 115, 119 },
        {  73,  77,  89,  93, 105, 109, 121, 125,  75,  79,  91,  95, 107, 111, 123, 127 },
        { 128, 132, 144, 148, 160, 164, 176, 180, 130, 134, 146, 150, 162, 166, 178, 182 },
        { 136, 140, 152, 156, 168, 172, 184, 188, 138, 142, 154, 158, 170, 174, 186, 190 },
        { 161, 165, 177, 181, 129, 133, 145, 149, 163, 167, 179, 183, 131, 135, 147, 151 },
        { 169, 173, 185, 189, 137, 141, 153, 157, 171, 175, 187, 191, 139, 143, 155, 159 },
        { 224, 228, 240, 244, 192, 196, 208, 212, 226, 230, 242, 246, 194, 198, 210, 214 },
        { 232, 236, 248, 252, 200, 204, 216, 220, 234, 238, 250, 254, 202, 206, 218, 222 },
        { 193, 197, 209, 213, 225, 229, 241, 245, 195, 199, 211, 215, 227, 231, 243, 247 },
        { 201, 205, 217, 221, 233, 237, 249, 253, 203, 207, 219, 223, 235, 239, 251, 255 }
    }};

    static constexpr P4ColumnLookupTable ColumnTable4
    {{
        {   0,   8,  32,  40,  64,  72,  96, 104,   2,  10,  34,  42,  66,  74,  98, 106,   4,  12,  36,  44,  68,  76, 100, 108,   6,  14,  38,  46,  70,  78, 102, 110 },
        {  16,  24,  48,  56,  80,  88, 112, 120,  18,  26,  50,  58,  82,  90, 114, 122,  20,  28,  52,  60,  84,  92, 116, 124,  22,  30,  54,  62,  86,  94, 118, 126 },
        {  65,  73,  97, 105,   1,   9,  33,  41,  67,  75,  99, 107,   3,  11,  35,  43,  69,  77, 101, 109,   5,  13,  37,  45,  71,  79, 103, 111,   7,  15,  39,  47 },
        {  81,  89, 113, 121,  17,  25,  49,  57,  83,  91, 115, 123,  19,  27,  51,  59,  85,  93, 117, 125,  21,  29,  53,  61,  87,  95, 119, 127,  23,  31,  55,  63 },
        { 192, 200, 224, 232, 128, 136, 160, 168, 194, 202, 226, 234, 130, 138, 162, 170, 196, 204, 228, 236, 132, 140, 164, 172, 198, 206, 230, 238, 134, 142, 166, 174 },
        { 208, 216, 240, 248, 144, 152, 176, 184, 210, 218, 242, 250, 146, 154, 178, 186, 212, 220, 244, 252, 148, 156, 180, 188, 214, 222, 246, 254, 150, 158, 182, 190 },
        { 129, 137, 161, 169, 193, 201, 225, 233, 131, 139, 163, 171, 195, 203, 227, 235, 133, 141, 165, 173, 197, 205, 229, 237, 135, 143, 167, 175, 199, 207, 231, 239 },
        { 145, 153, 177, 185, 209, 217, 241, 249, 147, 155, 179, 187, 211, 219, 243, 251, 149, 157, 181, 189, 213, 221, 245, 253, 151, 159, 183, 191, 215, 223, 247, 255 },
        { 256, 264, 288, 296, 320, 328, 352, 360, 258, 266, 290, 298, 322, 330, 354, 362, 260, 268, 292, 300, 324, 332, 356, 364, 262, 270, 294, 302, 326, 334, 358, 366 },
        { 272, 280, 304, 312, 336, 344, 368, 376, 274, 282, 306, 314, 338, 346, 370, 378, 276, 284, 308, 316, 340, 348, 372, 380, 278, 286, 310, 318, 342, 350, 374, 382 },
        { 321, 329, 353, 361, 257, 265, 289, 297, 323, 331, 355, 363, 259, 267, 291, 299, 325, 333, 357, 365, 261, 269, 293, 301, 327, 335, 359, 367, 263, 271, 295, 303 },
        { 337, 345, 369, 377, 273, 281, 305, 313, 339, 347, 371, 379, 275, 283, 307, 315, 341, 349, 373, 381, 277, 285, 309, 317, 343, 351, 375, 383, 279, 287, 311, 319 },
        { 448, 456, 480, 488, 384, 392, 416, 424, 450, 458, 482, 490, 386, 394, 418, 426, 452, 460, 484, 492, 388, 396, 420, 428, 454, 462, 486, 494, 390, 398, 422, 430 },
        { 464, 472, 496, 504, 400, 408, 432, 440, 466, 474, 498, 506, 402, 410, 434, 442, 468, 476, 500, 508, 404, 412, 436, 444, 470, 478, 502, 510, 406, 414, 438, 446 },
        { 385, 393, 417, 425, 449, 457, 481, 489, 387, 395, 419, 427, 451, 459, 483, 491, 389, 397, 421, 429, 453, 461, 485, 493, 391, 399, 423, 431, 455, 463, 487, 495 },
        { 401, 409, 433, 441, 465, 473, 497, 505, 403, 411, 435, 443, 467, 475, 499, 507, 405, 413, 437, 445, 469, 477, 501, 509, 407, 415, 439, 447, 471, 479, 503, 511 },
    }};

    // this is going to be massive (an entire page of addess lookups)
    static C32PageLookupTable  PageTableC32{ };
    static Z32PageLookupTableT PageTableZ32{ };
    static C16PageLookupTable  PageTableC16{ };
    static C16SPageLookupTable PageTableC16S{ };
    static Z16PageLookupTable  PageTableZ16{ };
    static Z16SPageLookupTable PageTableZ16S{ };
    static P8PageLookupTable   PageTableP8{ };
    static P4PageLookupTable   PageTableP4{ };

    void InitLookupTables()
    {
        // 32 bit
        PixelStorageTraits<C32>::InitPageLookupTable(PageTableC32, BlockTableC32, ColumnTable32);
        PixelStorageTraits<Z32>::InitPageLookupTable(PageTableZ32, BlockTableZ32, ColumnTable32);

        // 16 bit
        PixelStorageTraits<C16>::InitPageLookupTable(PageTableC16, BlockTableC16, ColumnTable16);
        PixelStorageTraits<C16S>::InitPageLookupTable(PageTableC16S, BlockTableC16S, ColumnTable16);
        PixelStorageTraits<Z16>::InitPageLookupTable(PageTableZ16, BlockTableZ16, ColumnTable16);
        PixelStorageTraits<Z16S>::InitPageLookupTable(PageTableZ16S, BlockTableZ16S, ColumnTable16);

        // 8 bit
        PixelStorageTraits<P8>::InitPageLookupTable(PageTableP8, BlockTableP8, ColumnTable8);

        // 4 bit
        PixelStorageTraits<P4>::InitPageLookupTable(PageTableP4, BlockTableP4, ColumnTable4);
    }

    u32 LookupPixelAddressCT32(u32 bp, u32 bw, u32 x, u32 y)
    {
        return PixelStorageTraits<C32>::Address(PageTableC32, bp, bw, x, y);
    }

    u32 LookupPixelAddressCT16(u32 bp, u32 bw, u32 x, u32 y)
    {
        return PixelStorageTraits<C16>::Address(PageTableC16, bp, bw, x, y);
    }

    u32 LookupPixelAddressCT16S(u32 bp, u32 bw, u32 x, u32 y)
    {
        return PixelStorageTraits<C16S>::Address(PageTableC16S, bp, bw, x, y);
    }

    u32 LookupPixelAddressZ32(u32 bp, u32 bw, u32 x, u32 y)
    {
        return PixelStorageTraits<Z32>::Address(PageTableZ32, bp, bw, x, y);
    }

    u32 LookupPixelAddressZ16(u32 bp, u32 bw, u32 x, u32 y)
    {
        return PixelStorageTraits<Z16>::Address(PageTableZ16, bp, bw, x, y);
    }

    u32 LookupPixelAddressZ16S(u32 bp, u32 bw, u32 x, u32 y)
    {
        return PixelStorageTraits<Z16S>::Address(PageTableZ16S, bp, bw, x, y);
    }

    u32 LookupPixelAddressP8(u32 bp, u32 bw, u32 x, u32 y)
    {
        return PixelStorageTraits<P8>::Address(PageTableP8, bp, bw, x, y);
    }

    u32 LookupPixelAddressP4(u32 bp, u32 bw, u32 x, u32 y)
    {
        return PixelStorageTraits<P4>::Address(PageTableP4, bp, bw, x, y);
    }

    u32 LookupPixelAddressNull(u32 bp, u32 bw, u32 x, u32 y)
    {
        return 0;
    }

    u32 ReadPixelAddressCT32(u8* data, u32 address)
    {
        return static_cast<u32>(PixelStorageTraits<C32>::Read(data, address));
    }

    u32 ReadPixelAddressCT24(u8* data, u32 address)
    {
        return static_cast<u32>(PixelStorageTraits<C24>::Read(data, address));
    }

    u32 ReadPixelAddressCT16(u8* data, u32 address)
    {
        return static_cast<u32>(PixelStorageTraits<C16>::Read(data, address));
    }

    u32 ReadPixelAddressCT16S(u8* data, u32 address)
    {
        return static_cast<u32>(PixelStorageTraits<C16S>::Read(data, address));
    }

    u32 ReadPixelAddressZ32(u8* data, u32 address)
    {
        return static_cast<u32>(PixelStorageTraits<Z32>::Read(data, address));
    }

    u32 ReadPixelAddressZ24(u8* data, u32 address)
    {
        return static_cast<u32>(PixelStorageTraits<Z24>::Read(data, address));
    }

    u32 ReadPixelAddressZ16(u8* data, u32 address)
    {
        return static_cast<u32>(PixelStorageTraits<Z16>::Read(data, address));
    }

    u32 ReadPixelAddressZ16S(u8* data, u32 address)
    {
        return static_cast<u32>(PixelStorageTraits<Z16S>::Read(data, address));
    }

    u32 ReadPixelAddressP8(u8* data, u32 address)
    {
        return static_cast<u32>(PixelStorageTraits<P8>::Read(data, address));
    }

    u32 ReadPixelAddressP8H(u8* data, u32 address)
    {
        return static_cast<u32>(PixelStorageTraits<P8H>::Read(data, address));
    }

    u32 ReadPixelAddressP4(u8* data, u32 address)
    {
        return static_cast<u32>(PixelStorageTraits<P4>::Read(data, address));
    }

    u32 ReadPixelAddressP4HH(u8* data, u32 address)
    {
        return static_cast<u32>(PixelStorageTraits<P4HH>::Read(data, address));
    }

    u32 ReadPixelAddressP4HL(u8* data, u32 address)
    {
        return static_cast<u32>(PixelStorageTraits<P4HL>::Read(data, address));
    }

    u32 ReadPixelAddressNull(u8* data, u32 address)
    {
        return static_cast<u32>(0);
    }

    void WritePixelAddressCT32(u8* data, u32 address, u32 value)
    {
        using Traits = PixelStorageTraits<C32>;

        Traits::Write(data, address, static_cast<Traits::PackedT>(value));
    }

    void WritePixelAddressCT24(u8* data, u32 address, u32 value)
    {
        using Traits = PixelStorageTraits<C24>;

        Traits::Write(data, address, static_cast<Traits::PackedT>(value));
    }

    void WritePixelAddressCT16(u8* data, u32 address, u32 value)
    {
        using Traits = PixelStorageTraits<C16>;

        Traits::Write(data, address, static_cast<Traits::PackedT>(value));
    }

    void WritePixelAddressCT16S(u8* data, u32 address, u32 value)
    {
        using Traits = PixelStorageTraits<C16S>;

        Traits::Write(data, address, static_cast<Traits::PackedT>(value));
    }

    void WritePixelAddressZ32(u8* data, u32 address, u32 value)
    {
        using Traits = PixelStorageTraits<Z32>;

        Traits::Write(data, address, static_cast<Traits::PackedT>(value));
    }

    void WritePixelAddressZ24(u8* data, u32 address, u32 value)
    {
        using Traits = PixelStorageTraits<Z24>;

        Traits::Write(data, address, static_cast<Traits::PackedT>(value));
    }

    void WritePixelAddressZ16(u8* data, u32 address, u32 value)
    {
        using Traits = PixelStorageTraits<Z16>;

        Traits::Write(data, address, static_cast<Traits::PackedT>(value));
    }

    void WritePixelAddressZ16S(u8* data, u32 address, u32 value)
    {
        using Traits = PixelStorageTraits<Z16S>;

        Traits::Write(data, address, static_cast<Traits::PackedT>(value));
    }

    void WritePixelAddressP8(u8* data, u32 address, u32 value)
    {
        using Traits = PixelStorageTraits<P8>;

        Traits::Write(data, address, static_cast<Traits::PackedT>(value));
    }

    void WritePixelAddressP8H(u8* data, u32 address, u32 value)
    {
        using Traits = PixelStorageTraits<P8H>;

        Traits::Write(data, address, static_cast<Traits::PackedT>(value));
    }

    void WritePixelAddressP4(u8* data, u32 address, u32 value)
    {
        using Traits = PixelStorageTraits<P4>;

        Traits::Write(data, address, static_cast<Traits::PackedT>(value));
    }

    void WritePixelAddressP4HH(u8* data, u32 address, u32 value)
    {
        using Traits = PixelStorageTraits<P4HH>;

        Traits::Write(data, address, static_cast<Traits::PackedT>(value));
    }

    void WritePixelAddressP4HL(u8* data, u32 address, u32 value)
    {
        using Traits = PixelStorageTraits<P4HL>;

        Traits::Write(data, address, static_cast<Traits::PackedT>(value));
    }

    void WritePixelAddressNull(u8* data, u32 address, u32 value)
    {

    }

    u32 ReadPixelCT32(u8* data, u32 bp, u32 bw, u32 x, u32 y)
    {
        return ReadPixelAddressCT32(data, LookupPixelAddressCT32(bp, bw, x, y));
    }

    u32 ReadPixelCT24(u8* data, u32 bp, u32 bw, u32 x, u32 y)
    {
        return ReadPixelAddressCT24(data, LookupPixelAddressCT32(bp, bw, x, y));
    }

    u32 ReadPixelCT16(u8* data, u32 bp, u32 bw, u32 x, u32 y)
    {
        return ReadPixelAddressCT16(data, LookupPixelAddressCT16(bp, bw, x, y));
    }

    u32 ReadPixelCT16S(u8* data, u32 bp, u32 bw, u32 x, u32 y)
    {
        return ReadPixelAddressCT16S(data, LookupPixelAddressCT16S(bp, bw, x, y));
    }

    u32 ReadPixelZ32(u8* data, u32 bp, u32 bw, u32 x, u32 y)
    {
        return ReadPixelAddressZ32(data, LookupPixelAddressZ32(bp, bw, x, y));
    }

    u32 ReadPixelZ24(u8* data, u32 bp, u32 bw, u32 x, u32 y)
    {
        return ReadPixelAddressZ24(data, LookupPixelAddressZ32(bp, bw, x, y));
    }

    u32 ReadPixelZ16(u8* data, u32 bp, u32 bw, u32 x, u32 y)
    {
        return ReadPixelAddressZ16(data, LookupPixelAddressZ16(bp, bw, x, y));
    }

    u32 ReadPixelZ16S(u8* data, u32 bp, u32 bw, u32 x, u32 y)
    {
        return ReadPixelAddressZ16S(data, LookupPixelAddressZ16S(bp, bw, x, y));
    }

    u32 ReadPixelP8(u8* data, u32 bp, u32 bw, u32 x, u32 y)
    {
        return ReadPixelAddressP8(data, LookupPixelAddressP8(bp, bw, x, y));
    }

    u32 ReadPixelP8H(u8* data, u32 bp, u32 bw, u32 x, u32 y)
    {
        return ReadPixelAddressP8H(data, LookupPixelAddressCT32(bp, bw, x, y));
    }

    u32 ReadPixelP4(u8* data, u32 bp, u32 bw, u32 x, u32 y)
    {
        return ReadPixelAddressP4(data, LookupPixelAddressP4(bp, bw, x, y));
    }

    u32 ReadPixelP4HL(u8* data, u32 bp, u32 bw, u32 x, u32 y)
    {
        return ReadPixelAddressP4HL(data, LookupPixelAddressCT32(bp, bw, x, y));
    }

    u32 ReadPixelP4HH(u8* data, u32 bp, u32 bw, u32 x, u32 y)
    {
        return ReadPixelAddressP4HH(data, LookupPixelAddressCT32(bp, bw, x, y));
    }

    u32 ReadPixelNull(u8* data, u32 bp, u32 bw, u32 x, u32 y)
    {
        return 0;
    }

    void WritePixelCT32(u8* data, u32 bp, u32 bw, u32 x, u32 y, u32 value)
    {
        WritePixelAddressCT32(data, LookupPixelAddressCT32(bp, bw, x, y), value);
    }

    void WritePixelCT24(u8* data, u32 bp, u32 bw, u32 x, u32 y, u32 value)
    {
        WritePixelAddressCT24(data, LookupPixelAddressCT32(bp, bw, x, y), value);
    }

    void WritePixelCT16(u8* data, u32 bp, u32 bw, u32 x, u32 y, u32 value)
    {
        WritePixelAddressCT16(data, LookupPixelAddressCT16(bp, bw, x, y), value);
    }

    void WritePixelCT16S(u8* data, u32 bp, u32 bw, u32 x, u32 y, u32 value)
    {
        WritePixelAddressCT16S(data, LookupPixelAddressCT16S(bp, bw, x, y), value);
    }

    void WritePixelZ32(u8* data, u32 bp, u32 bw, u32 x, u32 y, u32 value)
    {
        WritePixelAddressZ32(data, LookupPixelAddressZ32(bp, bw, x, y), value);
    }

    void WritePixelZ24(u8* data, u32 bp, u32 bw, u32 x, u32 y, u32 value)
    {
        WritePixelAddressZ24(data, LookupPixelAddressZ32(bp, bw, x, y), value);
    }

    void WritePixelZ16(u8* data, u32 bp, u32 bw, u32 x, u32 y, u32 value)
    {
        WritePixelAddressZ16(data, LookupPixelAddressZ16(bp, bw, x, y), value);
    }

    void WritePixelZ16S(u8* data, u32 bp, u32 bw, u32 x, u32 y, u32 value)
    {
        WritePixelAddressZ16S(data, LookupPixelAddressZ16S(bp, bw, x, y), value);
    }

    void WritePixelP8(u8* data, u32 bp, u32 bw, u32 x, u32 y, u32 value)
    {
        WritePixelAddressP8(data, LookupPixelAddressP8(bp, bw, x, y), value);
    }

    void WritePixelP8H(u8* data, u32 bp, u32 bw, u32 x, u32 y, u32 value)
    {
        WritePixelAddressP8H(data, LookupPixelAddressCT32(bp, bw, x, y), value);
    }

    void WritePixelP4(u8* data, u32 bp, u32 bw, u32 x, u32 y, u32 value)
    {
        WritePixelAddressP4(data, LookupPixelAddressP4(bp, bw, x, y), value);
    }

    void WritePixelP4HL(u8* data, u32 bp, u32 bw, u32 x, u32 y, u32 value)
    {
        WritePixelAddressP4HL(data, LookupPixelAddressCT32(bp, bw, x, y), value);
    }

    void WritePixelP4HH(u8* data, u32 bp, u32 bw, u32 x, u32 y, u32 value)
    {
        WritePixelAddressP4HH(data, LookupPixelAddressCT32(bp, bw, x, y), value);
    }

    void WritePixelNull(u8* data, u32 bp, u32 bw, u32 x, u32 y, u32 value)
    {

    }

    void ReadBlockToLinearBuffer32(u8* output, u32 pitch, const u8* data, u32 block_addr)
    {
        const u32* vram_ptr = reinterpret_cast<const u32*>(data);
        const u32 src_block_word_address = (block_addr & 0x3FFF) * 64;

        // <---------8--------->
        // ---------------------
        // |         0         |
        // ---------------------
        // |         1         |
        // ---------------------
        // |         2         |
        // ---------------------
        // |         3         |
        // ---------------------
        for (u32 column = 0; column < 4; ++column)
        {
            // <-------------------8------------------->
            // -----------------------------------------
            // | 0  | 1  | 4  | 5  | 8  | 9  | 12 | 13 |
            // -----------------------------------------
            // | 2  | 3  | 6  | 7  | 10 | 11 | 14 | 15 |
            // -----------------------------------------
            //
            // note: shows which sequential word load maps to what pixel

            const u32* src_col_ptr = &vram_ptr[src_block_word_address + 16 * column];
            const s128* src_row = reinterpret_cast<const s128*>(src_col_ptr);

            // --------------------------------
            // word   |  0  |  1  |  2  |  3  |
            // --------------------------------
            // w0123  | 0,0 | 1,0 | 0,1 | 1,1 |
            // --------------------------------
            // w4567  | 2,0 | 3,0 | 2,1 | 3,1 |
            // --------------------------------
            // w89AB  | 4,0 | 5,0 | 4,1 | 5,1 |
            //---------------------------------
            // wCDEF  | 6,0 | 7,0 | 6,1 | 7,1 |
            // --------------------------------
            const s128 w0123 = _mm_loadu_si128(&src_row[0]); // 0  1  2  3
            const s128 w4567 = _mm_loadu_si128(&src_row[1]); // 4  5  6  7
            const s128 w89AB = _mm_loadu_si128(&src_row[2]); // 8  9  10 11
            const s128 wCDEF = _mm_loadu_si128(&src_row[3]); // 12 13 14 15

            // --------------------------------
            // lane # |  0  |  1  |  2  |  3  |
            // --------------------------------
            // row0l  | 0,0 | 1,0 | 2,0 | 3,0 |
            // --------------------------------
            // row0r  | 4,0 | 5,0 | 6,0 | 7,0 |
            // --------------------------------
            // row1l  | 0,1 | 1,1 | 2,1 | 3,1 |
            // --------------------------------
            // row1r  | 4,1 | 5,1 | 6,1 | 7,1 |
            // --------------------------------
            const s128 row0l = _mm_unpacklo_epi64(w0123, w4567); // 0  1  4  5
            const s128 row0r = _mm_unpacklo_epi64(w89AB, wCDEF); // 8  9  12 13
            const s128 row1l = _mm_unpackhi_epi64(w0123, w4567); // 2  3  6  7
            const s128 row1r = _mm_unpackhi_epi64(w89AB, wCDEF); // 10 11 14 15

            u32 dest_row0_start = (2 * column + 0) * pitch;
            u32 dest_row1_start = (2 * column + 1) * pitch;

            s128* dest_row00 = reinterpret_cast<s128*>(&output[dest_row0_start + 0]);
            s128* dest_row04 = reinterpret_cast<s128*>(&output[dest_row0_start + 16]);
            s128* dest_row10 = reinterpret_cast<s128*>(&output[dest_row1_start + 0]);
            s128* dest_row14 = reinterpret_cast<s128*>(&output[dest_row1_start + 16]);

            // <---------------8-------------->
            // --------------------------------
            // |     row0l     |     row0r    |
            // --------------------------------
            // |     row1l     |     row1r    |
            // --------------------------------
            _mm_storeu_si128(dest_row00, row0l);
            _mm_storeu_si128(dest_row04, row0r);
            _mm_storeu_si128(dest_row10, row1l);
            _mm_storeu_si128(dest_row14, row1r);
        }
    }

    void ReadBlockToLinearBuffer16(u8* output, u32 pitch, const u8* data, u32 block_addr)
    {
        const u32* vram_ptr = reinterpret_cast<const u32*>(data);
        const u32 src_block_word_address = (block_addr & 0x3FFF) * 64;

        // mask to shuffle lower halfwords into the first half of the register and higher halfwords into the second
        //
        //      |<---0L-->|<--0H--->|<---1L-->|<--1H--->|<---2L-->|<--2H--->|<---3L-->|<--3H--->|
        //      ---------------------------------------------------------------------------------
        // src: | 00 | 11 | 22 | 33 | 44 | 55 | 66 | 77 | 88 | 99 | AA | BB | CC | DD | EE | FF |
        //      ---------------------------------------------------------------------------------
        // dst: | 00 | 11 | 44 | 55 | 88 | 99 | CC | DD | 22 | 33 | 66 | 77 | AA | BB | EE | FF |
        //      ---------------------------------------------------------------------------------
        //      |<---0L-->|<--1L--->|<---2L-->|<--3L--->|<---0H-->|<--1H--->|<---2H-->|<--3H--->|
        // note: bytes
        const s128 mask = _mm_setr_epi8(
            0,  1,  4,  5,  // 0
            8,  9,  12, 13, // 1
            2,  3,  6,  7,  // 2
            10, 11, 14, 15  // 3
        );

        // <---------16-------->
        // ---------------------
        // |         0         |
        // ---------------------
        // |         1         |
        // ---------------------
        // |         2         |
        // ---------------------
        // |         3         |
        // ---------------------
        for (u32 column = 0; column < 4; ++column)
        {
            // <--------------------------------------16--------------------------------------->
            // ---------------------------------------------------------------------------------
            // | 0  | 1  | 4  | 5  | 8  | 9  | 12 | 13 | 0  | 1  | 4  | 5  | 8  | 9  | 12 | 13 |
            // ---------------------------------------------------------------------------------
            // | 2  | 3  | 6  | 7  | 10 | 11 | 14 | 15 | 2  | 3  | 6  | 7  | 10 | 11 | 14 | 15 |
            // ---------------------------------------------------------------------------------
            // |<------------------L------------------>|<------------------H------------------>|
            //
            // note: shows which sequential word load maps to what pixel
            const u32* src_col_ptr = &vram_ptr[src_block_word_address + 16 * column];
            const s128* src_row = reinterpret_cast<const s128*>(src_col_ptr);

            // ----------------------------------------------------------------
            // word   |      0      |      1      |      2      |      3      |
            // ----------------------------------------------------------------
            // w0123  | 0,0  | 8,0  | 1,0  | 9,0  | 0,1  | 8,1  | 1,1  | 9,1  |
            // ----------------------------------------------------------------
            // w4567  | 2,0  | 10,0 | 3,0  | 11,0 | 2,1  | 10,1 | 3,1  | 11,1 |
            // ----------------------------------------------------------------
            // w89AB  | 4,0  | 12,0 | 5,0  | 13,0 | 4,1  | 12,1 | 5,1  | 13,1 |
            //-----------------------------------------------------------------
            // wCDEF  | 6,0  | 14,0 | 7,0  | 15,0 | 6,1  | 14,1 | 7,1  | 15,1 |
            // ----------------------------------------------------------------
            const s128 w0123 = _mm_loadu_si128(&src_row[0]); // 0HL  1HL  2HL  3HL
            const s128 w4567 = _mm_loadu_si128(&src_row[1]); // 4HL  5HL  6HL  7HL
            const s128 w89AB = _mm_loadu_si128(&src_row[2]); // 8HL  9HL  10HL 11HL
            const s128 wCDEF = _mm_loadu_si128(&src_row[3]); // 12HL 13HL 14HL 15HL

            // ----------------------------------------------------------------
            // word   |      0      |      1      |      2      |      3      |
            // ----------------------------------------------------------------
            // w0145  | 0,0  | 8,0  | 1,0  | 9,0  | 2,0  | 10,0 | 3,0  | 11,0 |
            // ----------------------------------------------------------------
            // w89CD  | 4,0  | 12,0 | 5,0  | 13,0 | 6,0  | 14,0 | 7,0  | 15,0 |
            // ----------------------------------------------------------------
            // w2367  | 0,1  | 8,1  | 1,1  | 9,1  | 2,1  | 10,1 | 3,1  | 11,1 |
            //-----------------------------------------------------------------
            // wABEF  | 4,1  | 12,1 | 5,1  | 13,1 | 6,1  | 14,1 | 7,1  | 15,1 |
            // ----------------------------------------------------------------
            const s128 w0145 = _mm_unpacklo_epi64(w0123, w4567); // 0HL  1HL  4HL  5HL
            const s128 w89CD = _mm_unpacklo_epi64(w89AB, wCDEF); // 8HL  9HL  12HL 13HL
            const s128 w2367 = _mm_unpackhi_epi64(w0123, w4567); // 2HL  3HL  6HL  7HL
            const s128 wABEF = _mm_unpackhi_epi64(w89AB, wCDEF); // 10HL 11HL 14HL 15HL

            // ----------------------------------------------------------------
            // word   |      0      |      1      |      2      |      3      |
            // ----------------------------------------------------------------
            // h0145  | 0,0  | 1,0  | 2,0  | 3,0  | 8,0  | 9,0  | 10,0 | 11,0 |
            // ----------------------------------------------------------------
            // h89CD  | 4,0  | 5,0  | 6,0  | 7,0  | 12,0 | 13,0 | 14,0 | 15,0 |
            // ----------------------------------------------------------------
            // h2367  | 0,1  | 1,1  | 2,1  | 3,1  | 8,1  | 9,1  | 10,1 | 11,1 |
            //-----------------------------------------------------------------
            // hABEF  | 4,1  | 5,1  | 6,1  | 7,1  | 12,1 | 13,1 | 14,1 | 15,1 |
            // ----------------------------------------------------------------
            const s128 h0145 = _mm_shuffle_epi8(w0145, mask); // 0L  1L  4L  5L  0H  1H  4H  5H
            const s128 h89CD = _mm_shuffle_epi8(w89CD, mask); // 8L  9L  12L 13L 8H  9H  12H 13H
            const s128 h2367 = _mm_shuffle_epi8(w2367, mask); // 2L  3L  6L  7L  2H  3H  6H  7H
            const s128 hABEF = _mm_shuffle_epi8(wABEF, mask); // 10L 11L 14L 15L 10H 11H 14H 15H

            // --------------------------------------------------------------------
            // word       |      0      |      1      |      2      |      3      |
            // --------------------------------------------------------------------
            // h014589CDL | 0,0  | 1,0  | 2,0  | 3,0  | 4,0  | 5,0  | 6,0  | 7,0  |
            // --------------------------------------------------------------------
            // h014589CDH | 8,0  | 9,0  | 10,0 | 11,0 | 12,0 | 13,0 | 14,0 | 15,0 |
            // --------------------------------------------------------------------
            // h2367ABEFL | 0,1  | 1,1  | 2,1  | 3,1  | 4,1  | 5,1  | 6,1  | 7,1  |
            //---------------------------------------------------------------------
            // h2367ABEFH | 8,1  | 9,1  | 10,1 | 11,1 | 12,1 | 13,1 | 14,1 | 15,1 |
            // --------------------------------------------------------------------
            const s128 h014589CDL = _mm_unpacklo_epi64(h0145, h89CD); // 0L  1L  4L  5L  8L  9L  12L 13L
            const s128 h014589CDH = _mm_unpackhi_epi64(h0145, h89CD); // 0H  1H  4H  5H  8H  9H  12H 13H
            const s128 h2367ABEFL = _mm_unpacklo_epi64(h2367, hABEF); // 2L  3L  6L  7L  10L 11L 14L 15L
            const s128 h2367ABEFH = _mm_unpackhi_epi64(h2367, hABEF); // 2H  3H  6H  7H  10H 11H 14H 15H

            u32 dest_row0_start = (2 * column + 0) * pitch;
            u32 dest_row1_start = (2 * column + 1) * pitch;

            s128* dest_row0l = reinterpret_cast<s128*>(&output[dest_row0_start + 0]);
            s128* dest_row0r = reinterpret_cast<s128*>(&output[dest_row0_start + 16]);
            s128* dest_row1l = reinterpret_cast<s128*>(&output[dest_row1_start + 0]);
            s128* dest_row1r = reinterpret_cast<s128*>(&output[dest_row1_start + 16]);

            // <---------------16------------->
            // --------------------------------
            // |     row0l     |     row0r    |
            // --------------------------------
            // |     row1l     |     row1r    |
            // --------------------------------
            _mm_storeu_si128(dest_row0l, h014589CDL);
            _mm_storeu_si128(dest_row0r, h014589CDH);
            _mm_storeu_si128(dest_row1l, h2367ABEFL);
            _mm_storeu_si128(dest_row1r, h2367ABEFH);
        }
    }

    void ReadBlockToLinearBuffer8(u8* output, u32 pitch, const u8* data, u32 block_addr)
    {
        const u32* vram_ptr = reinterpret_cast<const u32*>(data);
        const u32 src_block_word_address = (block_addr & 0x3FFF) * 64;

        // mask to shuffle the bytes so bits 0-7 are word 0, 16-23 are word 1, 8-15 are word 2, and 24-31 are word 3
        //
        //      |<-------0ABCD----->|<------1ABCD------>|<------2ABCD------>|<------3ABCD------>|
        //      ---------------------------------------------------------------------------------
        // src: | 00 | 11 | 22 | 33 | 44 | 55 | 66 | 77 | 88 | 99 | AA | BB | CC | DD | EE | FF |
        //      ---------------------------------------------------------------------------------
        // dst: | 00 | 44 | 88 | CC | 22 | 66 | AA | EE | 11 | 55 | 99 | DD | 33 | 77 | BB | FF |
        //      ---------------------------------------------------------------------------------
        //      | 0A | 1A | 2A | 3A | 0C | 1C | 2C | 3C | 0B | 1B | 2B | 3B | 0D | 1D | 2D | 3D |
        //
        // note: bytes
        const s128 mask = _mm_setr_epi8(
            0,  4,  8,  12, // 0
            2,  6,  10, 14, // 1
            1,  5,  9,  13, // 2
            3,  7,  11, 15  // 3
        );

        // <---------16-------->
        // ---------------------
        // |         0         |
        // ---------------------
        // |         1         |
        // ---------------------
        // |         2         |
        // ---------------------
        // |         3         |
        // ---------------------
        for (u32 column = 0; column < 4; ++column)
        {
            // even
            //      <--------------------------------------16--------------------------------------->
            //      ---------------------------------------------------------------------------------
            //      | 0  | 1  | 4  | 5  | 8  | 9  | 12 | 13 | 0  | 1  | 4  | 5  | 8  | 9  | 12 | 13 |
            // 0-7  --------------------------------------------------------------------------------- 16-23
            //      | 2  | 3  | 6  | 7  | 10 | 11 | 14 | 15 | 2  | 3  | 6  | 7  | 10 | 11 | 14 | 15 |
            // ---------------------------------------------------------------------------------------------
            //      | 8  | 9  | 12 | 13 | 0  | 1  | 4  | 5  | 8  | 9  | 12 | 13 | 0  | 1  | 4  | 5  |
            // 8-15 --------------------------------------------------------------------------------- 24-31
            //      | 10 | 11 | 14 | 15 | 2  | 3  | 6  | 7  | 10 | 11 | 14 | 15 | 2  | 3  | 6  | 7  |
            //      ---------------------------------------------------------------------------------
            //
            // odd
            //      <--------------------------------------16--------------------------------------->
            //      ---------------------------------------------------------------------------------
            //      | 8  | 9  | 12 | 13 | 0  | 1  | 4  | 5  | 8  | 9  | 12 | 13 | 0  | 1  | 4  | 5  |
            // 0-7  --------------------------------------------------------------------------------- 16-23
            //      | 10 | 11 | 14 | 15 | 2  | 3  | 6  | 7  | 10 | 11 | 14 | 15 | 2  | 3  | 6  | 7  |
            // --------------------------------------------------------------------------------------------
            //      | 0  | 1  | 4  | 5  | 8  | 9  | 12 | 13 | 0  | 1  | 4  | 5  | 8  | 9  | 12 | 13 |
            // 8-15 --------------------------------------------------------------------------------- 24-31
            //      | 2  | 3  | 6  | 7  | 10 | 11 | 14 | 15 | 2  | 3  | 6  | 7  | 10 | 11 | 14 | 15 |
            //      ---------------------------------------------------------------------------------
            //
            // note: shows which sequential word load maps to what pixel
            const u32* src_col_ptr = &vram_ptr[src_block_word_address + 16 * column];
            const s128* src_row = reinterpret_cast<const s128*>(src_col_ptr);

            // note: only the even column version is shown
            // ------------------------------------------------------------------------------------------------------------------------
            // word   |             0             |             1             |             2             |             3             |
            // ------------------------------------------------------------------------------------------------------------------------
            // w0123  | 0,0  | 4,2  | 8,0  | 12,2 | 1,0  | 5,2  | 9,0  | 13,2 | 0,1  | 4,3  | 8,1  | 12,3 | 1,1  | 5,3  | 9,1  | 13,3 |
            // ------------------------------------------------------------------------------------------------------------------------
            // w4567  | 2,0  | 6,2  | 10,0 | 14,2 | 3,0  | 7,2  | 11,0 | 15,2 | 2,1  | 6,3  | 10,1 | 14,3 | 3,1  | 7,3  | 11,1 | 15,3 |
            // ------------------------------------------------------------------------------------------------------------------------
            // w89AB  | 4,0  | 0,2  | 12,0 | 8,2  | 5,0  | 1,2  | 13,0 | 9,2  | 4,1  | 0,3  | 12,1 | 8,3  | 5,1  | 1,3  | 13,1 | 9,3  |
            // ------------------------------------------------------------------------------------------------------------------------
            // wCDEF  | 6,0  | 2,2  | 14,0 | 10,2 | 7,0  | 3,2  | 15,0 | 11,2 | 6,1  | 2,3  | 14,1 | 10,3 | 7,1  | 3,3  | 15,1 | 11,3 |
            // ------------------------------------------------------------------------------------------------------------------------
            const s128 w0123 = _mm_loadu_si128(&src_row[0]); // 0  1  2  3  0ABCD  1ABCD  2ABCD  3ABCD
            const s128 w4567 = _mm_loadu_si128(&src_row[1]); // 4  5  6  7  4ABCD  5ABCD  6ABCD  7ABCD
            const s128 w89AB = _mm_loadu_si128(&src_row[2]); // 8  9  10 11 8ABCD  9ABCD  10ABCD 11ABCD
            const s128 wCDEF = _mm_loadu_si128(&src_row[3]); // 12 13 14 15 12ABCD 13ABCD 14ABCD 15ABCD

            // note: only the even column version is shown
            // ------------------------------------------------------------------------------------------------------------------------
            // word   |             0             |             1             |             2             |             3             |
            // ------------------------------------------------------------------------------------------------------------------------
            // w0145  | 0,0  | 4,2  | 8,0  | 12,2 | 1,0  | 5,2  | 9,0  | 13,2 | 2,0  | 6,2  | 10,0 | 14,2 | 3,0  | 7,2  | 11,0 | 15,2 |
            // ------------------------------------------------------------------------------------------------------------------------
            // w89CD  | 4,0  | 0,2  | 12,0 | 8,2  | 5,0  | 1,2  | 13,0 | 9,2  | 6,0  | 2,2  | 14,0 | 10,2 | 7,0  | 3,2  | 15,0 | 11,2 |
            // ------------------------------------------------------------------------------------------------------------------------
            // w2367  | 0,1  | 4,3  | 8,1  | 12,3 | 1,1  | 5,3  | 9,1  | 13,3 | 2,1  | 6,3  | 10,1 | 14,3 | 3,1  | 7,3  | 11,1 | 15,3 |
            // ------------------------------------------------------------------------------------------------------------------------
            // wABEF  | 4,1  | 0,3  | 12,1 | 8,3  | 5,1  | 1,3  | 13,1 | 9,3  | 6,1  | 2,3  | 14,1 | 10,3 | 7,1  | 3,3  | 15,1 | 11,3 |
            // ------------------------------------------------------------------------------------------------------------------------
            const s128 w0145 = _mm_unpacklo_epi64(w0123, w4567); // 0  1  4  5  0ABCD  1ABCD  4ABCD  5ABCD
            const s128 w89CD = _mm_unpacklo_epi64(w89AB, wCDEF); // 8  9  12 13 8ABCD  9ABCD  12ABCD 13ABCD
            const s128 w2367 = _mm_unpackhi_epi64(w0123, w4567); // 2  3  6  7  2ABCD  3ABCD  6ABCD  7ABCD
            const s128 wABEF = _mm_unpackhi_epi64(w89AB, wCDEF); // 10 11 14 15 10ABCD 11ABCD 14ABCD 15ABCD

            // note: only the even column version is shown
            // ---------------------------------------------------------------------------------------------------------------------------
            // word      |             0             |             1             |             2             |             3             |
            // ---------------------------------------------------------------------------------------------------------------------------
            // b0145ACBD | 0,0  | 1,0  | 2,0  | 3,0  | 8,0  | 9,0  | 10,0 | 11,0 | 4,2  | 5,2  | 6,2  | 7,2  | 12,2 | 13,2 | 14,2 | 15,2 |
            // ---------------------------------------------------------------------------------------------------------------------------
            // b89CDACBD | 4,0  | 5,0  | 6,0  | 7,0  | 12,0 | 13,0 | 14,0 | 15,0 | 0,2  | 1,2  | 2,2  | 3,2  | 8,2  | 9,2  | 10,2 | 11,2 |
            // ---------------------------------------------------------------------------------------------------------------------------
            // b2367ACBD | 0,1  | 1,1  | 2,1  | 3,1  | 8,1  | 9,1  | 10,1 | 11,1 | 4,3  | 5,3  | 6,3  | 7,3  | 12,3 | 13,3 | 14,3 | 15,3 |
            // ---------------------------------------------------------------------------------------------------------------------------
            // bABEFACBD | 4,1  | 5,1  | 6,1  | 7,1  | 12,1 | 13,1 | 14,1 | 15,1 | 0,3  | 1,3  | 2,3  | 3,3  | 8,3  | 9,3  | 10,3 | 11,3 |
            // ---------------------------------------------------------------------------------------------------------------------------
            const s128 b0145ACBD = _mm_shuffle_epi8(w0145, mask); // 0A 1A 4A 5A | 0C 1C 4C 5C | 0B 1B 4B 5B | 0D 1D 4D 5D
            const s128 b89CDACBD = _mm_shuffle_epi8(w89CD, mask); // 8A 9A CA DA | 8C 9C CC DC | 8B 9B CB DB | 8D 9D CD DD
            const s128 b2367ACBD = _mm_shuffle_epi8(w2367, mask); // 2A 3A 6A 7A | 2C 3C 6C 7C | 2B 3B 6B 7B | 2D 3D 6D 7D
            const s128 bABEFACBD = _mm_shuffle_epi8(wABEF, mask); // AA BA EA FA | AC AC EC FC | AB BB EB FB | AD BD ED FD

            const bool even = (column & 1) == 0;

            s128 row0;
            s128 row1;
            s128 row2;
            s128 row3;

            // note: only the even column version is shown
            // ----------------------------------------------------------------------------------------------------------------------
            // word |             0             |             1             |             2             |             3             |
            // ----------------------------------------------------------------------------------------------------------------------
            // r0   | 0,0  | 1,0  | 2,0  | 3,0  | 4,0  | 5,0  | 6,0  | 7,0  | 8,0  | 9,0  | 10,0 | 11,0 | 12,0 | 13,0 | 14,0 | 15,0 |
            // ----------------------------------------------------------------------------------------------------------------------
            // r1   | 0,1  | 1,1  | 2,1  | 3,1  | 4,1  | 5,1  | 6,1  | 7,1  | 8,1  | 9,1  | 10,1 | 11,1 | 12,1 | 13,1 | 14,1 | 15,1 |
            // ----------------------------------------------------------------------------------------------------------------------
            // r2   | 0,2  | 1,2  | 2,2  | 3,2  | 4,2  | 5,2  | 6,2  | 7,2  | 8,2  | 9,2  | 10,2 | 11,2 | 12,2 | 13,2 | 14,2 | 15,2 |
            // ----------------------------------------------------------------------------------------------------------------------
            // r3   | 0,3  | 1,3  | 2,3  | 3,3  | 4,3  | 5,3  | 6,3  | 7,3  | 8,3  | 9,3  | 10,3 | 11,3 | 12,3 | 13,3 | 14,3 | 15,3 |
            // ----------------------------------------------------------------------------------------------------------------------
            if (even)
            {
                // bottom 2 row3 swapped
                row0 = _mm_unpacklo_epi32(b0145ACBD, b89CDACBD); // 0A 1A 4A 5A | 8A 9A CA DA | 0C 1C 2C 5C | 8C 9C CC DC
                row1 = _mm_unpacklo_epi32(b2367ACBD, bABEFACBD); // 2A 3A 6A 7A | AA BA EA FA | 2C 3C 6C 7C | AC AC EC FC
                row2 = _mm_unpackhi_epi32(b89CDACBD, b0145ACBD); // 8B 9B CB DB | 0B 1B 4B 5B | 8D 9D CD DD | 0D 1D 4D 5D
                row3 = _mm_unpackhi_epi32(bABEFACBD, b2367ACBD); // AB BB EB FB | 2B 3B 6B 7B | AD BD ED FD | 2D 3D 6D 7D
            }
            else
            {
                // top 2 rows swapped
                row0 = _mm_unpacklo_epi32(b89CDACBD, b0145ACBD); // 0C 1C 2C 5C | 8C 9C CC DC | 0A 1A 4A 5A | 8A 9A CA DA
                row1 = _mm_unpacklo_epi32(bABEFACBD, b2367ACBD); // 2C 3C 6C 7C | AC AC EC FC | 2A 3A 6A 7A | AA BA EA FA
                row2 = _mm_unpackhi_epi32(b0145ACBD, b89CDACBD); // 8D 9D CD DD | 0D 1D 4D 5D | 8B 9B CB DB | 0B 1B 4B 5B
                row3 = _mm_unpackhi_epi32(b2367ACBD, bABEFACBD); // AD BD ED FD | 2D 3D 6D 7D | AB BB EB FB | 2B 3B 6B 7B
            }

            u8* row = &output[(4 * column) * pitch];

            s128* dest_row0 = reinterpret_cast<s128*>(&row[0 * pitch]);
            s128* dest_row1 = reinterpret_cast<s128*>(&row[1 * pitch]);
            s128* dest_row2 = reinterpret_cast<s128*>(&row[2 * pitch]);
            s128* dest_row3 = reinterpret_cast<s128*>(&row[3 * pitch]);

            // <---------------16------------->
            // --------------------------------
            // |              row0            |
            // --------------------------------
            // |              row1            |
            // --------------------------------
            // |              row2            |
            // --------------------------------
            // |              row3            |
            // --------------------------------
            _mm_storeu_si128(dest_row0, row0);
            _mm_storeu_si128(dest_row1, row1);
            _mm_storeu_si128(dest_row2, row2);
            _mm_storeu_si128(dest_row3, row3);
        }
    }

    void ReadBlockToLinearBuffer4(u8* output, u32 pitch, const u8* data, u32 block_addr)
    {
        const u32* vram_ptr = reinterpret_cast<const u32*>(data);
        const u32 src_block_word_address = (block_addr & 0x3FFF) * 64;

        // mask to shuffle the bytes so bits 0-7 are word 0, 16-23 are word 1, 8-15 are word 2, and 24-31 are word 3
        //
        //      |<-------0ABCD----->|<------1ABCD------>|<------2ABCD------>|<------3ABCD------>|
        //      ---------------------------------------------------------------------------------
        // src: | 00 | 11 | 22 | 33 | 44 | 55 | 66 | 77 | 88 | 99 | AA | BB | CC | DD | EE | FF |
        //      ---------------------------------------------------------------------------------
        // dst: | 00 | 44 | 88 | CC | 11 | 55 | 99 | DD | 22 | 66 | AA | EE | 33 | 77 | BB | FF |
        //      ---------------------------------------------------------------------------------
        //      | 0A | 1A | 2A | 3A | 0B | 1B | 2B | 3B | 0C | 1C | 2C | 3C | 0D | 1D | 2D | 3D |
        //
        // note: bytes
        const s128 mask = _mm_setr_epi8(
            0,  4,  8,  12, // 0
            1,  5,  9,  13, // 1
            2,  6,  10, 14, // 2
            3,  7,  11, 15  // 3
        );

        const s128 nibble_mask = _mm_set1_epi8(0xF);

        // <---------32-------->
        // ---------------------
        // |         0         |
        // ---------------------
        // |         1         |
        // ---------------------
        // |         2         |
        // ---------------------
        // |         3         |
        // ---------------------
        for (u32 column = 0; column < 4; ++column)
        {
            // even
            //      <--------------------------------------16--------------------------------------->               <--------------------------------------16--------------------------------------->
            //      ---------------------------------------------------------------------------------       |       ---------------------------------------------------------------------------------
            //      | 0  | 1  | 4  | 5  | 8  | 9  | 12 | 13 | 0  | 1  | 4  | 5  | 8  | 9  | 12 | 13 |       |       | 0  | 1  | 4  | 5  | 8  | 9  | 12 | 13 | 0  | 1  | 4  | 5  | 8  | 9  | 12 | 13 |
            // 0-4  --------------------------------------------------------------------------------- 8-11  | 16-19 --------------------------------------------------------------------------------- 24-27
            //      | 2  | 3  | 6  | 7  | 10 | 11 | 14 | 15 | 2  | 3  | 6  | 7  | 10 | 11 | 14 | 15 |       |       | 2  | 3  | 6  | 7  | 10 | 11 | 14 | 15 | 2  | 3  | 6  | 7  | 10 | 11 | 14 | 15 |
            // ---------------------------------------------------------------------------------------------|----------------------------------------------------------------------------------------------
            //      | 8  | 9  | 12 | 13 | 0  | 1  | 4  | 5  | 8  | 9  | 12 | 13 | 0  | 1  | 4  | 5  |       |       | 8  | 9  | 12 | 13 | 0  | 1  | 4  | 5  | 8  | 9  | 12 | 13 | 0  | 1  | 4  | 5  |
            // 4-7  --------------------------------------------------------------------------------- 12-15 | 20-23 --------------------------------------------------------------------------------- 28-31
            //      | 10 | 11 | 14 | 15 | 2  | 3  | 6  | 7  | 10 | 11 | 14 | 15 | 2  | 3  | 6  | 7  |       |       | 10 | 11 | 14 | 15 | 2  | 3  | 6  | 7  | 10 | 11 | 14 | 15 | 2  | 3  | 6  | 7  |
            //      ---------------------------------------------------------------------------------       |       ---------------------------------------------------------------------------------
            //
            // odd
            //      <--------------------------------------16--------------------------------------->               <--------------------------------------16--------------------------------------->
            //      ---------------------------------------------------------------------------------       |       ---------------------------------------------------------------------------------
            //      | 8  | 9  | 12 | 13 | 0  | 1  | 4  | 5  | 8  | 9  | 12 | 13 | 0  | 1  | 4  | 5  |       |       | 8  | 9  | 12 | 13 | 0  | 1  | 4  | 5  | 8  | 9  | 12 | 13 | 0  | 1  | 4  | 5  |
            // 0-4  --------------------------------------------------------------------------------- 8-11  | 16-19 --------------------------------------------------------------------------------- 24-27
            //      | 10 | 11 | 14 | 15 | 2  | 3  | 6  | 7  | 10 | 11 | 14 | 15 | 2  | 3  | 6  | 7  |       |       | 10 | 11 | 14 | 15 | 2  | 3  | 6  | 7  | 10 | 11 | 14 | 15 | 2  | 3  | 6  | 7  |
            // ---------------------------------------------------------------------------------------------|----------------------------------------------------------------------------------------------
            //      | 0  | 1  | 4  | 5  | 8  | 9  | 12 | 13 | 0  | 1  | 4  | 5  | 8  | 9  | 12 | 13 |       |       | 0  | 1  | 4  | 5  | 8  | 9  | 12 | 13 | 0  | 1  | 4  | 5  | 8  | 9  | 12 | 13 |
            // 4-7  --------------------------------------------------------------------------------- 12-15 | 20-23 --------------------------------------------------------------------------------- 28-31
            //      | 2  | 3  | 6  | 7  | 10 | 11 | 14 | 15 | 2  | 3  | 6  | 7  | 10 | 11 | 14 | 15 |       |       | 2  | 3  | 6  | 7  | 10 | 11 | 14 | 15 | 2  | 3  | 6  | 7  | 10 | 11 | 14 | 15 |
            //      ---------------------------------------------------------------------------------       |       ---------------------------------------------------------------------------------
            //
            // note: shows which sequential word load maps to what pixel
            const u32* src_col_ptr = &vram_ptr[src_block_word_address + 16 * column];
            const s128* src_row = reinterpret_cast<const s128*>(src_col_ptr);

            // note: only the even column version is shown
            // ------------------------------------------------------------------------------------------------------------------------
            // word   |                           0                           |                           1                           |
            // ------------------------------------------------------------------------------------------------------------------------
            // w0123  | 0,0  | 4,2  | 8,0  | 12,2 | 16,0 | 20,2 | 24,0 | 28,2 | 1,0  | 5,2  | 9,0  | 13,2 | 17,0 | 21,2 | 25,0 | 29,2 |
            // ------------------------------------------------------------------------------------------------------------------------
            // w4567  | 2,0  | 6,2  | 10,0 | 14,2 | 18,0 | 22,2 | 26,0 | 30,2 | 3,0  | 7,2  | 11,0 | 15,2 | 19,0 | 23,2 | 27,0 | 31,2 |
            // ------------------------------------------------------------------------------------------------------------------------
            // w89AB  | 4,0  | 0,2  | 12,0 | 8,2  | 20,0 | 16,2 | 28,0 | 24,2 | 5,0  | 1,2  | 13,0 | 9,2  | 21,0 | 17,2 | 29,0 | 25,2 |
            // ------------------------------------------------------------------------------------------------------------------------
            // wCDEF  | 6,0  | 2,2  | 14,0 | 10,2 | 22,0 | 18,2 | 30,0 | 26,2 | 7,0  | 3,2  | 15,0 | 11,2 | 23,0 | 19,2 | 31,0 | 27,2 |
            // ------------------------------------------------------------------------------------------------------------------------
            //
            // ------------------------------------------------------------------------------------------------------------------------
            // word   |                           3                           |                           4                           |
            // ------------------------------------------------------------------------------------------------------------------------
            // w0123  | 0,1  | 4,3  | 8,1  | 12,3 | 16,1 | 20,3 | 24,1 | 28,3 | 1,1  | 5,3  | 9,1  | 13,3 | 17,1 | 21,3 | 25,1 | 29,3 |
            // ------------------------------------------------------------------------------------------------------------------------
            // w4567  | 2,1  | 6,3  | 10,1 | 14,3 | 18,1 | 22,3 | 26,1 | 30,3 | 3,1  | 7,3  | 11,1 | 15,3 | 19,1 | 23,3 | 27,1 | 31,3 |
            // ------------------------------------------------------------------------------------------------------------------------
            // w89AB  | 4,1  | 0,3  | 12,1 | 8,3  | 20,1 | 16,3 | 28,1 | 24,3 | 5,1  | 1,3  | 13,1 | 9,3  | 21,1 | 17,3 | 29,1 | 25,3 |
            // ------------------------------------------------------------------------------------------------------------------------
            // wCDEF  | 6,1  | 2,3  | 14,1 | 10,3 | 22,1 | 18,3 | 30,1 | 26,3 | 7,1  | 3,3  | 15,1 | 11,3 | 23,1 | 19,3 | 31,1 | 27,3 |
            // ------------------------------------------------------------------------------------------------------------------------
            const s128 w0123 = _mm_loadu_si128(&src_row[0]); // 0  1  2  3  0ABCD  1ABCD  2ABCD  3ABCD
            const s128 w4567 = _mm_loadu_si128(&src_row[1]); // 4  5  6  7  4ABCD  5ABCD  6ABCD  7ABCD
            const s128 w89AB = _mm_loadu_si128(&src_row[2]); // 8  9  10 11 8ABCD  9ABCD  10ABCD 11ABCD
            const s128 wCDEF = _mm_loadu_si128(&src_row[3]); // 12 13 14 15 12ABCD 13ABCD 14ABCD 15ABCD

            // note: only the even column version is shown
            // ------------------------------------------------------------------------------------------------------------------------
            // word   |                           0                           |                           1                           |
            // ------------------------------------------------------------------------------------------------------------------------
            // w0145  | 0,0  | 4,2  | 8,0  | 12,2 | 16,0 | 20,2 | 24,0 | 28,2 | 1,0  | 5,2  | 9,0  | 13,2 | 17,0 | 21,2 | 25,0 | 29,2 |
            // ------------------------------------------------------------------------------------------------------------------------
            // w89CD  | 4,0  | 0,2  | 12,0 | 8,2  | 20,0 | 16,2 | 28,0 | 24,2 | 5,0  | 1,2  | 13,0 | 9,2  | 21,0 | 17,2 | 29,0 | 25,2 |
            // ------------------------------------------------------------------------------------------------------------------------
            // w2367  | 0,1  | 4,3  | 8,1  | 12,3 | 16,1 | 20,3 | 24,1 | 28,3 | 1,1  | 5,3  | 9,1  | 13,3 | 17,1 | 21,3 | 25,1 | 29,3 |
            // ------------------------------------------------------------------------------------------------------------------------
            // wABEF  | 4,1  | 0,3  | 12,1 | 8,3  | 20,1 | 16,3 | 28,1 | 24,3 | 5,1  | 1,3  | 13,1 | 9,3  | 21,1 | 17,3 | 29,1 | 25,3 |
            // ------------------------------------------------------------------------------------------------------------------------
            //
            // ------------------------------------------------------------------------------------------------------------------------
            // word   |                           3                           |                           4                           |
            // ------------------------------------------------------------------------------------------------------------------------
            // w0145  | 2,0  | 6,2  | 10,0 | 14,2 | 18,0 | 22,2 | 26,0 | 30,2 | 3,0  | 7,2  | 11,0 | 15,2 | 19,0 | 23,2 | 27,0 | 31,2 |
            // ------------------------------------------------------------------------------------------------------------------------
            // w89CD  | 6,0  | 2,2  | 14,0 | 10,2 | 22,0 | 18,2 | 30,0 | 26,2 | 7,0  | 3,2  | 15,0 | 11,2 | 23,0 | 19,2 | 31,0 | 27,2 |
            // ------------------------------------------------------------------------------------------------------------------------
            // w2367  | 2,1  | 6,3  | 10,1 | 14,3 | 18,1 | 22,3 | 26,1 | 30,3 | 3,1  | 7,3  | 11,1 | 15,3 | 19,1 | 23,3 | 27,1 | 31,3 |
            // ------------------------------------------------------------------------------------------------------------------------
            // wABEF  | 6,1  | 2,3  | 14,1 | 10,3 | 22,1 | 18,3 | 30,1 | 26,3 | 7,1  | 3,3  | 15,1 | 11,3 | 23,1 | 19,3 | 31,1 | 27,3 |
            // ------------------------------------------------------------------------------------------------------------------------
            const s128 w0145 = _mm_unpacklo_epi64(w0123, w4567); // 0  1  4  5  0ABCD  1ABCD  4ABCD  5ABCD
            const s128 w89CD = _mm_unpacklo_epi64(w89AB, wCDEF); // 8  9  12 13 8ABCD  9ABCD  12ABCD 13ABCD
            const s128 w2367 = _mm_unpackhi_epi64(w0123, w4567); // 2  3  6  7  2ABCD  3ABCD  6ABCD  7ABCD
            const s128 wABEF = _mm_unpackhi_epi64(w89AB, wCDEF); // 10 11 14 15 10ABCD 11ABCD 14ABCD 15ABCD

            // note: only the even column version is shown
            // ----------------------------------------------------------------------------------------------------------------------------
            // word       |                           0                           |                           1                           |
            // ----------------------------------------------------------------------------------------------------------------------------
            // b0145ABCD  | 0,0  | 4,2  | 1,0  | 5,2  | 2,0  | 6,2  | 3,0  | 7,2  | 8,0  | 12,2 | 9,0  | 13,2 | 10,0 | 14,2 | 11,0 | 15,2 |
            // ----------------------------------------------------------------------------------------------------------------------------
            // b89CDABCD  | 4,0  | 0,2  | 5,0  | 1,2  | 6,0  | 2,2  | 7,0  | 3,2  | 12,0 | 8,2  | 13,0 | 9,2  | 14,0 | 10,2 | 15,0 | 11,2 |
            // ----------------------------------------------------------------------------------------------------------------------------
            // b2367ABCD  | 0,1  | 4,3  | 1,1  | 5,3  | 2,1  | 6,3  | 3,1  | 7,3  | 8,1  | 12,3 | 9,1  | 13,3 | 10,1 | 14,3 | 11,1 | 15,3 |
            // ----------------------------------------------------------------------------------------------------------------------------
            // bABEFABCD  | 4,1  | 0,3  | 5,1  | 1,3  | 6,1  | 2,3  | 7,1  | 3,3  | 12,1 | 8,3  | 13,1 | 9,3  | 14,1 | 10,3 | 15,1 | 11,3 |
            // ----------------------------------------------------------------------------------------------------------------------------
            //
            // ----------------------------------------------------------------------------------------------------------------------------
            // word       |                           3                           |                           4                           |
            // ----------------------------------------------------------------------------------------------------------------------------
            // b0145ABCD  | 16,0 | 20,2 | 17,0 | 21,2 | 18,0 | 22,2 | 19,0 | 23,2 | 24,0 | 28,2 | 25,0 | 29,2 | 26,0 | 30,2 | 27,0 | 31,2 |
            // ----------------------------------------------------------------------------------------------------------------------------
            // b89CDABCD  | 20,0 | 16,2 | 21,0 | 17,2 | 22,0 | 18,2 | 23,0 | 19,2 | 28,0 | 24,2 | 29,0 | 25,2 | 30,0 | 26,2 | 31,0 | 27,2 |
            // ----------------------------------------------------------------------------------------------------------------------------
            // b2367ABCD  | 16,1 | 20,3 | 17,1 | 21,3 | 18,1 | 22,3 | 19,1 | 23,3 | 24,1 | 28,3 | 25,1 | 29,3 | 26,1 | 30,3 | 27,1 | 31,3 |
            // ----------------------------------------------------------------------------------------------------------------------------
            // bABEFABCD  | 20,1 | 16,3 | 21,1 | 17,3 | 22,1 | 18,3 | 23,1 | 19,3 | 28,1 | 24,3 | 29,1 | 25,3 | 30,1 | 26,3 | 31,1 | 27,3 |
            // ----------------------------------------------------------------------------------------------------------------------------
            const s128 b0145ABCD = _mm_shuffle_epi8(w0145, mask); // 0A 1A 4A 5A | 0B 1B 4B 5B | 0C 1C 4C 5C | 0D 1D 4D 5D
            const s128 b89CDABCD = _mm_shuffle_epi8(w89CD, mask); // 8A 9A CA DA | 8B 9B CB DB | 8C 9C CC DC | 8D 9D CD DD
            const s128 b2367ABCD = _mm_shuffle_epi8(w2367, mask); // 2A 3A 6A 7A | 2B 3B 6B 7B | 2C 3C 6C 7C | 2D 3D 6D 7D
            const s128 bABEFABCD = _mm_shuffle_epi8(wABEF, mask); // AA BA EA FA | AB BB EB FB | AC BC EC FC | AD BD ED FD

            // note: only the even column version is shown
            // ------------------------------------------------------------------------------------------------------------------------------
            // word         |                           0                           |                           1                           |
            // ------------------------------------------------------------------------------------------------------------------------------
            // b014589CDAB  | 0,0  | 4,2  | 1,0  | 5,2  | 2,0  | 6,2  | 3,0  | 7,2  | 4,0  | 0,2  | 5,0  | 1,2  | 6,0  | 2,2  | 7,0  | 3,2  |
            // ------------------------------------------------------------------------------------------------------------------------------
            // b014589CDCD  | 16,0 | 20,2 | 17,0 | 21,2 | 18,0 | 22,2 | 19,0 | 23,2 | 20,0 | 16,2 | 21,0 | 17,2 | 22,0 | 18,2 | 23,0 | 19,2 |
            // ------------------------------------------------------------------------------------------------------------------------------
            // b2367ABEFAB  | 0,1  | 4,3  | 1,1  | 5,3  | 2,1  | 6,3  | 3,1  | 7,3  | 4,1  | 0,3  | 5,1  | 1,3  | 6,1  | 2,3  | 7,1  | 3,3  |
            // ------------------------------------------------------------------------------------------------------------------------------
            // b2367ABEFCD  | 6,1  | 20,3 | 17,1 | 21,3 | 18,1 | 22,3 | 19,1 | 23,3 | 20,1 | 16,3 | 21,1 | 17,3 | 22,1 | 18,3 | 23,1 | 19,3 |
            // ------------------------------------------------------------------------------------------------------------------------------
            //
            // ------------------------------------------------------------------------------------------------------------------------------
            // word         |                           3                           |                           4                           |
            // ------------------------------------------------------------------------------------------------------------------------------
            // b014589CDAB  | 8,0  | 12,2 | 9,0  | 13,2 | 10,0 | 14,2 | 11,0 | 15,2 | 12,0 | 8,2  | 13,0 | 9,2  | 14,0 | 10,2 | 15,0 | 11,2 |
            // ------------------------------------------------------------------------------------------------------------------------------
            // b014589CDCD  | 24,0 | 28,2 | 25,0 | 29,2 | 26,0 | 30,2 | 27,0 | 31,2 | 28,0 | 24,2 | 29,0 | 25,2 | 30,0 | 26,2 | 31,0 | 27,2 |
            // ------------------------------------------------------------------------------------------------------------------------------
            // b2367ABEFAB  | 8,1  | 12,3 | 9,1  | 13,3 | 10,1 | 14,3 | 11,1 | 15,3 | 12,1 | 8,3  | 13,1 | 9,3  | 14,1 | 10,3 | 15,1 | 11,3 |
            // ------------------------------------------------------------------------------------------------------------------------------
            // b2367ABEFCD  | 24,1 | 28,3 | 25,1 | 29,3 | 26,1 | 30,3 | 27,1 | 31,3 | 28,1 | 24,3 | 29,1 | 25,3 | 30,1 | 26,3 | 31,1 | 27,3 |
            // ------------------------------------------------------------------------------------------------------------------------------
            const s128 b014589CDAB = _mm_unpacklo_epi32(b0145ABCD, b89CDABCD); // 0A 1A 4A 5A | 0B 1B 4B 5B | 8A 9A CA DA | 8B 9B CB DB
            const s128 b014589CDCD = _mm_unpackhi_epi32(b0145ABCD, b89CDABCD); // 0C 1C 4C 5C | 0D 1D 4D 5D | 8C 9C CC DC | 8D 9D CD DD
            const s128 b2367ABEFAB = _mm_unpacklo_epi32(b2367ABCD, bABEFABCD); // 2A 3A 6A 7A | 2B 3B 6B 7B | AA BB EB FB | AC BC EC FC
            const s128 b2367ABEFCD = _mm_unpackhi_epi32(b2367ABCD, bABEFABCD); // 2C 3C 6C 7C | 2D 3D 6D 7D | AC BC EC FC | AD BD ED FD

            // note: only the even column version is shown
            // ------------------------------------------------------------------------------------------------------------------------------
            // word         |                           0                           |                           1                           |
            // ------------------------------------------------------------------------------------------------------------------------------
            // b89CD0145AB  | 4,0  | 0,2  | 5,0  | 1,2  | 6,0  | 2,2  | 7,0  | 3,2  | 0,0  | 4,2  | 1,0  | 5,2  | 2,0  | 6,2  | 3,0  | 7,2  |
            // ------------------------------------------------------------------------------------------------------------------------------
            // b89CD0145CD  | 20,0 | 16,2 | 21,0 | 17,2 | 22,0 | 18,2 | 23,0 | 19,2 | 6,0  | 20,2 | 17,0 | 21,2 | 18,0 | 22,2 | 19,0 | 23,2 |
            // ------------------------------------------------------------------------------------------------------------------------------
            // bABEF2367AB  | 4,1  | 0,3  | 5,1  | 1,3  | 6,1  | 2,3  | 7,1  | 3,3  | 0,1  | 4,3  | 1,1  | 5,3  | 2,1  | 6,3  | 3,1  | 7,3  |
            // ------------------------------------------------------------------------------------------------------------------------------
            // bABEF2367CD  | 20,1 | 16,3 | 21,1 | 17,3 | 22,1 | 18,3 | 23,1 | 19,3 | 6,1  | 20,3 | 17,1 | 21,3 | 18,1 | 22,3 | 19,1 | 23,3 |
            // ------------------------------------------------------------------------------------------------------------------------------
            //
            // ------------------------------------------------------------------------------------------------------------------------------
            // word         |                           3                           |                           4                           |
            // ------------------------------------------------------------------------------------------------------------------------------
            // b89CD0145AB  | 12,0 | 8,2  | 13,0 | 9,2  | 14,0 | 10,2 | 15,0 | 11,2 | 8,0  | 12,2 | 9,0  | 13,2 | 10,0 | 14,2 | 11,0 | 15,2 |
            // ------------------------------------------------------------------------------------------------------------------------------
            // b89CD0145CD  | 28,0 | 24,2 | 29,0 | 25,2 | 30,0 | 26,2 | 31,0 | 27,2 | 24,0 | 28,2 | 25,0 | 29,2 | 26,0 | 30,2 | 27,0 | 31,2 |
            // ------------------------------------------------------------------------------------------------------------------------------
            // bABEF2367AB  | 12,1 | 8,3  | 13,1 | 9,3  | 14,1 | 10,3 | 15,1 | 11,3 | 8,1  | 12,3 | 9,1  | 13,3 | 10,1 | 14,3 | 11,1 | 15,3 |
            // ------------------------------------------------------------------------------------------------------------------------------
            // bABEF2367CD  | 28,1 | 24,3 | 29,1 | 25,3 | 30,1 | 26,3 | 31,1 | 27,3 | 4,1 | 28,3 | 25,1 | 29,3 | 26,1 | 30,3 | 27,1 | 31,3  |
            // ------------------------------------------------------------------------------------------------------------------------------
            const s128 b89CD0145AB = _mm_unpacklo_epi32(b89CDABCD, b0145ABCD); // 8A 9A CA DA | 8B 9B CB DB | 0A 1A 4A 5A | 0B 1B 4B 5B
            const s128 b89CD0145CD = _mm_unpackhi_epi32(b89CDABCD, b0145ABCD); // 8C 9C CC DC | 8D 9D CD DD | 0C 1C 4C 5C | 0D 1D 4D 5D
            const s128 bABEF2367AB = _mm_unpacklo_epi32(bABEFABCD, b2367ABCD); // AA BB EB FB | AC BC EC FC | 2A 3A 6A 7A | 2B 3B 6B 7B
            const s128 bABEF2367CD = _mm_unpackhi_epi32(bABEFABCD, b2367ABCD); // AC BC EC FC | AD BD ED FD | 2C 3C 6C 7C | 2D 3D 6D 7D

            const bool even = (column & 1) == 0;

            s128 row0l;
            s128 row0r;
            s128 row1l;
            s128 row1r;
            s128 row2l;
            s128 row2r;
            s128 row3l;
            s128 row3r;

            // note: only the even column version is shown
            // -----------------------------------------------------------------------------------------------------------------------
            // word  |             0             |             1             |             2             |             3             |
            // -----------------------------------------------------------------------------------------------------------------------
            // r0l   | 0,0  | 1,0  | 2,0  | 3,0  | 4,0  | 5,0  | 6,0  | 7,0  | 8,0  | 9,0  | 10,0 | 11,0 | 12,0 | 13,0 | 14,0 | 15,0 |
            // -----------------------------------------------------------------------------------------------------------------------
            // r0r   | 16,0 | 17,0 | 18,0 | 19,0 | 20,0 | 21,0 | 22,0 | 23,0 | 24,0 | 25,0 | 26,0 | 27,0 | 28,0 | 29,0 | 30,0 | 31,0 |
            // -----------------------------------------------------------------------------------------------------------------------
            // r1l   | 0,1  | 1,1  | 2,1  | 3,1  | 4,1  | 5,1  | 6,1  | 7,1  | 8,1  | 9,1  | 10,1 | 11,1 | 12,1 | 13,1 | 14,1 | 15,1 |
            // -----------------------------------------------------------------------------------------------------------------------
            // r1r   | 16,1 | 17,1 | 18,1 | 19,1 | 20,1 | 21,1 | 22,1 | 23,1 | 24,1 | 25,1 | 26,1 | 27,1 | 28,1 | 29,1 | 30,1 | 31,1 |
            // -----------------------------------------------------------------------------------------------------------------------
            // r2l   | 0,2  | 1,2  | 2,2  | 3,2  | 4,2  | 5,2  | 6,2  | 7,2  | 8,2  | 9,2  | 10,2 | 11,2 | 12,2 | 13,2 | 14,2 | 15,2 |
            // -----------------------------------------------------------------------------------------------------------------------
            // r2r   | 16,2 | 17,2 | 18,2 | 19,2 | 20,2 | 21,2 | 22,2 | 23,2 | 24,2 | 25,2 | 26,2 | 27,2 | 28,2 | 29,2 | 30,2 | 31,2 |
            // -----------------------------------------------------------------------------------------------------------------------
            // r3l   | 0,3  | 1,3  | 2,3  | 3,3  | 4,3  | 5,3  | 6,3  | 7,3  | 8,3  | 9,3  | 10,3 | 11,3 | 12,3 | 13,3 | 14,3 | 15,3 |
            // -----------------------------------------------------------------------------------------------------------------------
            // r3r   | 16,3 | 17,3 | 18,3 | 19,3 | 20,3 | 21,3 | 22,3 | 23,3 | 24,3 | 25,3 | 26,3 | 27,3 | 28,3 | 29,3 | 30,3 | 31,3 |
            // -----------------------------------------------------------------------------------------------------------------------
            if (even)
            {
                // lo nibble
                row0l = _mm_and_si128(b014589CDAB, nibble_mask); // 0AL 1AL 4AL 5AL | 0BL 1BL 4BL 5BL | 8AL 9AL CAL DAL | 8BL 9BL CBL DBL
                row0r = _mm_and_si128(b014589CDCD, nibble_mask); // 0CL 1CL 4CL 5CL | 0DL 1DL 4DL 5DL | 8CL 9CL CCL DCL | 8DL 9DL CDL DDL
                row1l = _mm_and_si128(b2367ABEFAB, nibble_mask); // 2AL 3AL 6AL 7AL | 2BL 3BL 6BL 7BL | AAL BAL EAL FAL | ABL BBL EBL FBL
                row1r = _mm_and_si128(b2367ABEFCD, nibble_mask); // 2CL 3CL 6CL 7CL | 2DL 3DL 6DL 7DL | ACL BCL ECL FCL | ADL BDL EDL FDL

                // hi nibble
                row2l = _mm_and_si128(_mm_srli_epi16(b89CD0145AB, 4), nibble_mask); // 8AH 9AH CAH DAH | 8BH 9BH CBH DBH | 0AH 1AH 4AH 5AH | 0BH 1BH 4BH 5BH
                row2r = _mm_and_si128(_mm_srli_epi16(b89CD0145CD, 4), nibble_mask); // 8CH 9CH CCH DCH | 8DH 9DH CDH DDH | 0CH 1CH 4CH 5CH | 0DH 1DH 4DH 5DH
                row3l = _mm_and_si128(_mm_srli_epi16(bABEF2367AB, 4), nibble_mask); // AAH BAH EAH FAH | ABH BBH EBH FBH | 2AH 3AH 6AH 7AH | 2BH 3BH 6BH 7BH
                row3r = _mm_and_si128(_mm_srli_epi16(bABEF2367CD, 4), nibble_mask); // ACH BCH ECH FCH | ADH BDH EDH FDH | 2CH 3CH 6CH 7CH | 2DH 3DH 6DH 7DH
            }
            else
            {
                // lo nibble
                row0l = _mm_and_si128(b89CD0145AB, nibble_mask); // 8AL 9AL CAL DAL | 8BL 9BL CBL DBL | 0AL 1AL 4AL 5AL | 0BL 1BL 4BL 5BL
                row0r = _mm_and_si128(b89CD0145CD, nibble_mask); // 8CL 9CL CCL DCL | 8DL 9DL CDL DDL | 0CL 1CL 4CL 5CL | 0DL 1DL 4DL 5DL
                row1l = _mm_and_si128(bABEF2367AB, nibble_mask); // AAL BAL EAL FAL | ABL BBL EBL FBL | 2AL 3AL 6AL 7AL | 2BL 3BL 6BL 7BL
                row1r = _mm_and_si128(bABEF2367CD, nibble_mask); // ACL BCL ECL FCL | ADL BDL EDL FDL | 2CL 3CL 6CL 7CL | 2DL 3DL 6DL 7DL

                // hi nibble
                row2l = _mm_and_si128(_mm_srli_epi16(b014589CDAB, 4), nibble_mask); // 0AH 1AH 4AH 5AH | 0BH 1BH 4BH 5BH | 8AH 9AH CAH DAH | 8BH 9BH CBH DBH
                row2r = _mm_and_si128(_mm_srli_epi16(b014589CDCD, 4), nibble_mask); // 0CH 1CH 4CH 5CH | 0DH 1DH 4DH 5DH | 8CH 9CH CCH DCH | 8DH 9DH CDH DDH
                row3l = _mm_and_si128(_mm_srli_epi16(b2367ABEFAB, 4), nibble_mask); // 2AH 3AH 6AH 7AH | 2BH 3BH 6BH 7BH | AAH BAH EAH FAH | ABH BBH EBH FBH
                row3r = _mm_and_si128(_mm_srli_epi16(b2367ABEFCD, 4), nibble_mask); // 2CH 3CH 6CH 7CH | 2DH 3DH 6DH 7DH | ACH BCH ECH FCH | ADH BDH EDH FDH
            }

            u8* row = &output[(4 * column) * pitch];

            s128* dest_row00 = reinterpret_cast<s128*>(&row[0 * pitch + 0]);
            s128* dest_row01 = reinterpret_cast<s128*>(&row[0 * pitch + 16]);
            s128* dest_row10 = reinterpret_cast<s128*>(&row[1 * pitch + 0]);
            s128* dest_row11 = reinterpret_cast<s128*>(&row[1 * pitch + 16]);
            s128* dest_row20 = reinterpret_cast<s128*>(&row[2 * pitch + 0]);
            s128* dest_row21 = reinterpret_cast<s128*>(&row[2 * pitch + 16]);
            s128* dest_row30 = reinterpret_cast<s128*>(&row[3 * pitch + 0]);
            s128* dest_row31 = reinterpret_cast<s128*>(&row[3 * pitch + 16]);

            // <---------------32------------->
            // --------------------------------
            // |     row0l     |     row0r    |
            // --------------------------------
            // |     row1l     |     row1r    |
            // --------------------------------
            // |     row2l     |     row2r    |
            // --------------------------------
            // |     row3l     |     row3r    |
            // --------------------------------
            _mm_storeu_si128(dest_row00, row0l);
            _mm_storeu_si128(dest_row01, row0r);
            _mm_storeu_si128(dest_row10, row1l);
            _mm_storeu_si128(dest_row11, row1r);
            _mm_storeu_si128(dest_row20, row2l);
            _mm_storeu_si128(dest_row21, row2r);
            _mm_storeu_si128(dest_row30, row3l);
            _mm_storeu_si128(dest_row31, row3r);
        }
    }

    void ReadPageToLinearBufferCT32(u8* output, u32 pitch, const u8* data, u32 bp)
    {
        using Traits = PixelStorageTraits<C32>;

        constexpr auto block_extent = Traits::BlockExtent();
        constexpr auto column_extent = Traits::ColumnExtent();

        for (u32 block_row = 0; block_row < block_extent.y; ++block_row)
        for (u32 block_col = 0; block_col < block_extent.x; ++block_col)
        {
            const u32 block_id = BlockTableC32[block_row][block_col];
            const u32 dst_block_byte_address = (block_row * column_extent.y) * pitch + (block_col * column_extent.x) * 4;

            ReadBlockToLinearBuffer32(&output[dst_block_byte_address], pitch, data, bp + block_id);
        }
    }

    void ReadPageToLinearBufferCT16(u8* output, u32 pitch, const u8* data, u32 bp)
    {
        using Traits = PixelStorageTraits<C16>;

        constexpr auto block_extent = Traits::BlockExtent();
        constexpr auto column_extent = Traits::ColumnExtent();

        for (u32 block_row = 0; block_row < block_extent.y; ++block_row)
        for (u32 block_col = 0; block_col < block_extent.x; ++block_col)
        {
            const u32 block_id = BlockTableC16[block_row][block_col];
            const u32 dst_block_byte_address = (block_row * column_extent.y) * pitch + (block_col * column_extent.x) * 2;

            ReadBlockToLinearBuffer16(&output[dst_block_byte_address], pitch, data, bp + block_id);
        }
    }

    void ReadPageToLinearBufferCT16S(u8* output, u32 pitch, const u8* data, u32 bp)
    {
        using Traits = PixelStorageTraits<C16S>;

        constexpr auto block_extent = Traits::BlockExtent();
        constexpr auto column_extent = Traits::ColumnExtent();

        for (u32 block_row = 0; block_row < block_extent.y; ++block_row)
        for (u32 block_col = 0; block_col < block_extent.x; ++block_col)
        {
            const u32 block_id = BlockTableC16S[block_row][block_col];
            const u32 dst_block_byte_address = (block_row * column_extent.y) * pitch + (block_col * column_extent.x) * 2;

            ReadBlockToLinearBuffer16(&output[dst_block_byte_address], pitch, data, bp + block_id);
        }
    }

    void ReadPageToLinearBufferZ32(u8* output, u32 pitch, const u8* data, u32 bp)
    {
        using Traits = PixelStorageTraits<Z32>;

        constexpr auto block_extent = Traits::BlockExtent();
        constexpr auto column_extent = Traits::ColumnExtent();

        for (u32 block_row = 0; block_row < block_extent.y; ++block_row)
        for (u32 block_col = 0; block_col < block_extent.x; ++block_col)
        {
            const u32 block_id = BlockTableZ32[block_row][block_col];
            const u32 dst_block_byte_address = (block_row * column_extent.y) * pitch + (block_col * column_extent.x) * 4;

            ReadBlockToLinearBuffer32(&output[dst_block_byte_address], pitch, data, bp + block_id);
        }
    }

    void ReadPageToLinearBufferZ16(u8* output, u32 pitch, const u8* data, u32 bp)
    {
        using Traits = PixelStorageTraits<Z16>;

        constexpr auto block_extent = Traits::BlockExtent();
        constexpr auto column_extent = Traits::ColumnExtent();

        for (u32 block_row = 0; block_row < block_extent.y; ++block_row)
        for (u32 block_col = 0; block_col < block_extent.x; ++block_col)
        {
            const u32 block_id = BlockTableZ16[block_row][block_col];
            const u32 dst_block_byte_address = (block_row * column_extent.y) * pitch + (block_col * column_extent.x) * 2;

            ReadBlockToLinearBuffer16(&output[dst_block_byte_address], pitch, data, bp + block_id);
        }
    }

    void ReadPageToLinearBufferZ16S(u8* output, u32 pitch, const u8* data, u32 bp)
    {
        using Traits = PixelStorageTraits<Z16S>;

        constexpr auto block_extent = Traits::BlockExtent();
        constexpr auto column_extent = Traits::ColumnExtent();

        for (u32 block_row = 0; block_row < block_extent.y; ++block_row)
        for (u32 block_col = 0; block_col < block_extent.x; ++block_col)
        {
            const u32 block_id = BlockTableZ16S[block_row][block_col];
            const u32 dst_block_byte_address = (block_row * column_extent.y) * pitch + (block_col * column_extent.x) * 2;

            ReadBlockToLinearBuffer16(&output[dst_block_byte_address], pitch, data, bp + block_id);
        }
    }

    void ReadPageToLinearBufferP8(u8* output, u32 pitch, const u8* data, u32 bp)
    {
        using Traits = PixelStorageTraits<P8>;

        constexpr auto block_extent = Traits::BlockExtent();
        constexpr auto column_extent = Traits::ColumnExtent();

        for (u32 block_row = 0; block_row < block_extent.y; ++block_row)
        for (u32 block_col = 0; block_col < block_extent.x; ++block_col)
        {
            const u32 block_id = BlockTableP8[block_row][block_col];
            const u32 dst_block_byte_address = (block_row * column_extent.y) * pitch + (block_col * column_extent.x);

            ReadBlockToLinearBuffer8(&output[dst_block_byte_address], pitch, data, bp + block_id);
        }
    }

    void ReadPageToLinearBufferP4(u8* output, u32 pitch, const u8* data, u32 bp)
    {
        using Traits = PixelStorageTraits<P4>;

        constexpr auto block_extent = Traits::BlockExtent();
        constexpr auto column_extent = Traits::ColumnExtent();

        for (u32 block_row = 0; block_row < block_extent.y; ++block_row)
        for (u32 block_col = 0; block_col < block_extent.x; ++block_col)
        {
            const u32 block_id = BlockTableP4[block_row][block_col];
            const u32 dst_block_byte_address = (block_row * column_extent.y) * pitch + (block_col * column_extent.x);

            ReadBlockToLinearBuffer4(&output[dst_block_byte_address], pitch, data, bp + block_id);
        }
    }

    void ReadToLinearBufferCT32(u8* output, u32 pitch, const u8* data, u32 bp, u32 bw, const Extent2D extent)
    {
        const u32 pagesx = extent.x / 64;
        const u32 pagesy = extent.y / 32;

        for (u32 py = 0; py < pagesy; ++py)
        for (u32 px = 0; px < pagesx; ++px)
        {
            u32 base = bp + (py * bw + px) * 32;
            u8* dst = &output[(py * 32) * pitch + (px * 64) * 4];

            ReadPageToLinearBufferCT32(dst, pitch, data, base);
        }
    }
}


