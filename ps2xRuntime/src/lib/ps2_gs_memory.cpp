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

    void WriteCT32(u8* data, u32 bp, u32 bw, u32 x, u32 y, u32 value)
    {
        PixelStorageTraits<C32>::Write(PageTableC32, data, bp, bw, x, y, value);
    }

    void WriteCT24(u8* data, u32 bp, u32 bw, u32 x, u32 y, u32 value)
    {
        PixelStorageTraits<C24>::Write(PageTableC32, data, bp, bw, x, y, value);
    }

    void WriteZ32(u8* data, u32 bp, u32 bw, u32 x, u32 y, u32 value)
    {
        PixelStorageTraits<Z32>::Write(PageTableZ32, data, bp, bw, x, y, value);
    }

    void WriteZ24(u8* data, u32 bp, u32 bw, u32 x, u32 y, u32 value)
    {
        PixelStorageTraits<Z24>::Write(PageTableZ32, data, bp, bw, x, y, value);
    }

    void WriteCT16(u8* data, u32 bp, u32 bw, u32 x, u32 y, u32 value)
    {
        PixelStorageTraits<C16>::Write(PageTableC16, data, bp, bw, x, y, value);
    }

    void WriteCT16S(u8* data, u32 bp, u32 bw, u32 x, u32 y, u32 value)
    {
        PixelStorageTraits<C16S>::Write(PageTableC16S, data, bp, bw, x, y, value);
    }

    void WriteZ16(u8* data, u32 bp, u32 bw, u32 x, u32 y, u32 value)
    {
        PixelStorageTraits<Z16>::Write(PageTableZ16, data, bp, bw, x, y, value);
    }

    void WriteZ16S(u8* data, u32 bp, u32 bw, u32 x, u32 y, u32 value)
    {
        PixelStorageTraits<Z16S>::Write(PageTableZ16S, data, bp, bw, x, y, value);
    }

    void WriteP8(u8* data, u32 bp, u32 bw, u32 x, u32 y, u32 value)
    {
        PixelStorageTraits<P8>::Write(PageTableP8, data, bp, bw, x, y, value);
    }

    void WriteP8H(u8* data, u32 bp, u32 bw, u32 x, u32 y, u32 value)
    {
        PixelStorageTraits<P8H>::Write(PageTableC32, data, bp, bw, x, y, value);
    }

    void WriteP4(u8* data, u32 bp, u32 bw, u32 x, u32 y, u32 value)
    {
        PixelStorageTraits<P4>::Write(PageTableP4, data, bp, bw, x, y, value);
    }

    void WriteP4HL(u8* data, u32 bp, u32 bw, u32 x, u32 y, u32 value)
    {
        PixelStorageTraits<P4HL>::Write(PageTableC32, data, bp, bw, x, y, value);
    }

    void WriteP4HH(u8* data, u32 bp, u32 bw, u32 x, u32 y, u32 value)
    {
        PixelStorageTraits<P4HH>::Write(PageTableC32, data, bp, bw, x, y, value);
    }

    void WriteNull(u8* data, u32 bp, u32 bw, u32 x, u32 y, u32 value)
    {
    }

    u32 ReadCT32(u8* data, u32 bp, u32 bw, u32 x, u32 y)
    {
        return PixelStorageTraits<C32>::Read(PageTableC32, data, bp, bw, x, y);
    }

    u32 ReadZ32(u8* data, u32 bp, u32 bw, u32 x, u32 y)
    {
        return PixelStorageTraits<Z32>::Read(PageTableZ32, data, bp, bw, x, y);
    }

    u32 ReadCT24(u8* data, u32 bp, u32 bw, u32 x, u32 y)
    {
        return PixelStorageTraits<C24>::Read(PageTableC32, data, bp, bw, x, y);
    }

    u32 ReadZ24(u8* data, u32 bp, u32 bw, u32 x, u32 y)
    {
        return PixelStorageTraits<Z24>::Read(PageTableZ32, data, bp, bw, x, y);
    }

    u32 ReadCT16(u8* data, u32 bp, u32 bw, u32 x, u32 y)
    {
        return PixelStorageTraits<C16>::Read(PageTableC16, data, bp, bw, x, y);
    }

    u32 ReadCT16S(u8* data, u32 bp, u32 bw, u32 x, u32 y)
    {
        return PixelStorageTraits<C16S>::Read(PageTableC16S, data, bp, bw, x, y);
    }

    u32 ReadZ16(u8* data, u32 bp, u32 bw, u32 x, u32 y)
    {
        return PixelStorageTraits<Z16>::Read(PageTableZ16, data, bp, bw, x, y);
    }

    u32 ReadZ16S(u8* data, u32 bp, u32 bw, u32 x, u32 y)
    {
        return PixelStorageTraits<Z16S>::Read(PageTableZ16S, data, bp, bw, x, y);
    }

    u32 ReadP8(u8* data, u32 bp, u32 bw, u32 x, u32 y)
    {
        return PixelStorageTraits<P8>::Read(PageTableP8, data, bp, bw, x, y);
    }

    u32 ReadP8H(u8* data, u32 bp, u32 bw, u32 x, u32 y)
    {
        return PixelStorageTraits<P8H>::Read(PageTableC32, data, bp, bw, x, y);
    }

    u32 ReadP4(u8* data, u32 bp, u32 bw, u32 x, u32 y)
    {
        return PixelStorageTraits<P4>::Read(PageTableP4, data, bp, bw, x, y);
    }

    u32 ReadP4HL(u8* data, u32 bp, u32 bw, u32 x, u32 y)
    {
        return PixelStorageTraits<P4HL>::Read(PageTableC32, data, bp, bw, x, y);
    }

    u32 ReadP4HH(u8* data, u32 bp, u32 bw, u32 x, u32 y)
    {
        return PixelStorageTraits<P4HH>::Read(PageTableC32, data, bp, bw, x, y);
    }

    u32 ReadNull(u8* data, u32 bp, u32 bw, u32 x, u32 y)
    {
        return 0;
    }
}


