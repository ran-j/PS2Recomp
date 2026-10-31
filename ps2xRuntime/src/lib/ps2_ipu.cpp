#include "runtime/ps2_ipu.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <iterator>

namespace
{
    enum : uint32_t
    {
        kCmdBclr = 0x0,
        kCmdIdec = 0x1,
        kCmdBdec = 0x2,
        kCmdVdec = 0x3,
        kCmdFdec = 0x4,
        kCmdSetiq = 0x5,
        kCmdSetvq = 0x6,
        kCmdCsc = 0x7,
        kCmdPack = 0x8,
        kCmdSetth = 0x9,
    };

    constexpr uint32_t kCtrlEcd = 1u << 14;
    constexpr uint32_t kCtrlScd = 1u << 15;
    constexpr uint32_t kCtrlRst = 1u << 30;
    constexpr uint32_t kCtrlBusy = 1u << 31;
    // IDP, AS, IVF, QST, MP1, PCT and the reset request are software-writable.
    // RST is a write-only strobe: libmpeg read-modify-writes CTRL, so echoing RST would reset the IPU every time.
    constexpr uint32_t kCtrlWritable = (3u << 16) | (1u << 20) | (1u << 21) | (1u << 22) | (1u << 23) | (7u << 24);
    constexpr size_t kFifoBytes = 8u * 16u;

    constexpr uint8_t kZigzag[64] = {
        0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5,
        12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
        35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
        58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

    constexpr uint8_t kAlternateScan[64] = {
        0, 8, 16, 24, 1, 9, 2, 10, 17, 25, 32, 40, 48, 56, 57, 49,
        41, 33, 26, 18, 3, 11, 4, 12, 19, 27, 34, 42, 50, 58, 35, 43,
        51, 59, 20, 28, 5, 13, 6, 14, 21, 29, 36, 44, 52, 60, 37, 45,
        53, 61, 22, 30, 7, 15, 23, 31, 38, 46, 54, 62, 39, 47, 55, 63};

    constexpr uint8_t kNonLinearQuantizer[32] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 14, 16, 18, 20, 22,
        24, 28, 32, 36, 40, 44, 48, 52, 56, 64, 72, 80, 88, 96, 104, 112};

    // ISO/IEC 13818-2 tables B.12-B.15 (codes, lengths, run/level) as published in FFmpeg's mpeg12data.c.
    constexpr uint16_t kDcLumaCode[12] = {0x4, 0x0, 0x1, 0x5, 0x6, 0xe, 0x1e, 0x3e, 0x7e, 0xfe, 0x1fe, 0x1ff};
    constexpr uint8_t kDcLumaBits[12] = {3, 2, 2, 3, 3, 4, 5, 6, 7, 8, 9, 9};
    constexpr uint16_t kDcChromaCode[12] = {0x0, 0x1, 0x2, 0x6, 0xe, 0x1e, 0x3e, 0x7e, 0xfe, 0x1fe, 0x3fe, 0x3ff};
    constexpr uint8_t kDcChromaBits[12] = {2, 2, 2, 3, 4, 5, 6, 7, 8, 9, 10, 10};

    constexpr int kRunLevelCount = 111;
    constexpr uint16_t kTableZero[kRunLevelCount + 2][2] = {
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
        {0x1, 6}, // escape
        {0x2, 2}, // end of block
    };
    constexpr uint16_t kTableOne[kRunLevelCount + 2][2] = {
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
        {0x01, 6}, // escape
        {0x06, 4}, // end of block
    };
    constexpr int8_t kLevel[kRunLevelCount] = {
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
        17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32,
        33, 34, 35, 36, 37, 38, 39, 40, 1, 2, 3, 4, 5, 6, 7, 8,
        9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 1, 2, 3, 4, 5, 1,
        2, 3, 4, 1, 2, 3, 1, 2, 3, 1, 2, 3, 1, 2, 1, 2,
        1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    constexpr int8_t kRun[kRunLevelCount] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 3,
        3, 3, 3, 4, 4, 4, 5, 5, 5, 6, 6, 6, 7, 7, 8, 8,
        9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 15, 16, 16,
        17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31};
    // macroblock_address_increment 1..33, then escape (+33) and stuffing.
    constexpr uint8_t kMbaCode[35][2] = {
        {0x1, 1}, {0x3, 3}, {0x2, 3}, {0x3, 4}, {0x2, 4}, {0x3, 5}, {0x2, 5}, {0x7, 7},
        {0x6, 7}, {0xb, 8}, {0xa, 8}, {0x9, 8}, {0x8, 8}, {0x7, 8}, {0x6, 8}, {0x17, 10},
        {0x16, 10}, {0x15, 10}, {0x14, 10}, {0x13, 10}, {0x12, 10}, {0x23, 11}, {0x22, 11}, {0x21, 11},
        {0x20, 11}, {0x1f, 11}, {0x1e, 11}, {0x1d, 11}, {0x1c, 11}, {0x1b, 11}, {0x1a, 11}, {0x19, 11},
        {0x18, 11}, {0x8, 11}, {0xf, 11}};

    // coded_block_pattern (table B.9), indexed by pattern value, as published in FFmpeg's mpeg12data.c.
    constexpr uint8_t kCbpCode[64][2] = {
        {0x1, 9}, {0xb, 5}, {0x9, 5}, {0xd, 6}, {0xd, 4}, {0x17, 7}, {0x13, 7}, {0x1f, 8},
        {0xc, 4}, {0x16, 7}, {0x12, 7}, {0x1e, 8}, {0x13, 5}, {0x1b, 8}, {0x17, 8}, {0x13, 8},
        {0xb, 4}, {0x15, 7}, {0x11, 7}, {0x1d, 8}, {0x11, 5}, {0x19, 8}, {0x15, 8}, {0x11, 8},
        {0xf, 6}, {0xf, 8}, {0xd, 8}, {0x3, 9}, {0xf, 5}, {0xb, 8}, {0x7, 8}, {0x7, 9},
        {0xa, 4}, {0x14, 7}, {0x10, 7}, {0x1c, 8}, {0xe, 6}, {0xe, 8}, {0xc, 8}, {0x2, 9},
        {0x10, 5}, {0x18, 8}, {0x14, 8}, {0x10, 8}, {0xe, 5}, {0xa, 8}, {0x6, 8}, {0x6, 9},
        {0x12, 5}, {0x1a, 8}, {0x16, 8}, {0x12, 8}, {0xd, 5}, {0x9, 8}, {0x5, 8}, {0x5, 9},
        {0xc, 5}, {0x8, 8}, {0x4, 8}, {0x4, 9}, {0x7, 3}, {0xa, 5}, {0x8, 5}, {0xc, 6}};
    // motion_code magnitudes 1..16 (table B.10); 0 is the single bit '1'. A sign bit follows non-zero codes.
    constexpr uint8_t kMotionCode[17][2] = {
        {0x1, 1}, {0x1, 2}, {0x1, 3}, {0x1, 4}, {0x3, 6}, {0x5, 7}, {0x4, 7}, {0x3, 7}, {0xb, 9},
        {0xa, 9}, {0x9, 9}, {0x11, 10}, {0x10, 10}, {0xf, 10}, {0xe, 10}, {0xd, 10}, {0xc, 10}};
    // macroblock_type flags as VDEC reports them: quant 0x10, forward 0x08, backward 0x04, pattern 0x02, intra 0x01.
    struct MacroblockTypeCode
    {
        uint8_t code;
        uint8_t length;
        uint8_t flags;
    };
    constexpr MacroblockTypeCode kMbTypeI[] = {{0x1, 1, 0x01}, {0x1, 2, 0x11}};
    constexpr MacroblockTypeCode kMbTypeP[] = {{0x1, 1, 0x0A}, {0x1, 2, 0x02}, {0x1, 3, 0x08}, {0x3, 5, 0x01},
                                               {0x2, 5, 0x1A}, {0x1, 5, 0x12}, {0x1, 6, 0x11}};
    constexpr MacroblockTypeCode kMbTypeB[] = {{0x2, 2, 0x0C}, {0x3, 2, 0x0E}, {0x2, 3, 0x04}, {0x3, 3, 0x06},
                                               {0x2, 4, 0x08}, {0x3, 4, 0x0A}, {0x3, 5, 0x01}, {0x2, 5, 0x1E},
                                               {0x3, 6, 0x1A}, {0x2, 6, 0x16}, {0x1, 6, 0x11}};
    constexpr MacroblockTypeCode kMbTypeD[] = {{0x1, 1, 0x01}};

    enum class DctKind : uint8_t
    {
        Invalid,
        RunLevel,
        Escape,
        EndOfBlock
    };

    struct DctEntry
    {
        DctKind kind = DctKind::Invalid;
        uint8_t length = 0;
        uint8_t run = 0;
        uint8_t level = 0;
    };

    struct ShortEntry
    {
        uint8_t length = 0;
        uint8_t value = 0;
    };

    struct Tables
    {
        std::vector<DctEntry> dct[2];
        std::array<ShortEntry, 1024> dcLuma{};
        std::array<ShortEntry, 1024> dcChroma{};
        std::array<ShortEntry, 2048> mba{};
        std::array<ShortEntry, 512> cbp{};
        std::array<ShortEntry, 1024> motion{};
        std::array<ShortEntry, 64> mbType[4]{};
        double cosine[8][8]{};

        Tables()
        {
            for (int table = 0; table < 2; ++table)
            {
                const auto &codes = table == 0 ? kTableZero : kTableOne;
                dct[table].assign(1u << 16, DctEntry{});
                for (int i = 0; i < kRunLevelCount + 2; ++i)
                {
                    DctEntry entry;
                    entry.length = static_cast<uint8_t>(codes[i][1]);
                    if (i < kRunLevelCount)
                    {
                        entry.kind = DctKind::RunLevel;
                        entry.run = static_cast<uint8_t>(kRun[i]);
                        entry.level = static_cast<uint8_t>(kLevel[i]);
                    }
                    else
                    {
                        entry.kind = i == kRunLevelCount ? DctKind::Escape : DctKind::EndOfBlock;
                    }
                    fill(dct[table], codes[i][0], entry.length, 16, entry);
                }
            }
            for (uint8_t size = 0; size < 12; ++size)
            {
                fill(dcLuma, kDcLumaCode[size], kDcLumaBits[size], 10, ShortEntry{kDcLumaBits[size], size});
                fill(dcChroma, kDcChromaCode[size], kDcChromaBits[size], 10, ShortEntry{kDcChromaBits[size], size});
            }
            for (uint8_t i = 0; i < 35; ++i)
            {
                fill(mba, kMbaCode[i][0], kMbaCode[i][1], 11, ShortEntry{kMbaCode[i][1], static_cast<uint8_t>(i + 1u)});
            }
            for (uint8_t i = 0; i < 64; ++i)
                fill(cbp, kCbpCode[i][0], kCbpCode[i][1], 9, ShortEntry{kCbpCode[i][1], i});
            for (uint8_t i = 1; i < 17; ++i)
                fill(motion, kMotionCode[i][0], kMotionCode[i][1], 10, ShortEntry{kMotionCode[i][1], i});
            const auto fillTypes = [this](int pictureType, const MacroblockTypeCode *codes, size_t count)
            {
                for (size_t i = 0; i < count; ++i)
                    fill(mbType[pictureType], codes[i].code, codes[i].length, 6, ShortEntry{codes[i].length, codes[i].flags});
            };
            fillTypes(0, kMbTypeI, std::size(kMbTypeI));
            fillTypes(1, kMbTypeP, std::size(kMbTypeP));
            fillTypes(2, kMbTypeB, std::size(kMbTypeB));
            fillTypes(3, kMbTypeD, std::size(kMbTypeD));
            for (int x = 0; x < 8; ++x)
            {
                for (int u = 0; u < 8; ++u)
                {
                    const double scale = u == 0 ? std::sqrt(0.5) : 1.0;
                    cosine[x][u] = scale * std::cos((2.0 * x + 1.0) * u * 3.14159265358979323846 / 16.0);
                }
            }
        }

        template <typename Table, typename Entry>
        static void fill(Table &table, uint32_t code, uint32_t length, uint32_t width, const Entry &entry)
        {
            const uint32_t shift = width - length;
            const uint32_t first = code << shift;
            for (uint32_t i = 0; i < (1u << shift); ++i)
                table[first + i] = entry;
        }
    };

    const Tables &tables()
    {
        static const Tables instance;
        return instance;
    }

    uint8_t clampByte(int value)
    {
        return static_cast<uint8_t>(std::clamp(value, 0, 255));
    }
}

Ps2Ipu::Ps2Ipu()
{
    reset();
}

void Ps2Ipu::reset()
{
    m_cmd = 0;
    m_cmdData = 0;
    m_ctrl = 0;
    m_busy = false;
    m_pending = false;
    m_commandError = false;
    m_startCodeSeen = false;
    m_fifo.clear();
    m_bitPos = 0;
    m_dmaInput.clear();
    m_dmaCursor = 0;
    m_output.clear();
    m_releasedBytes = 0;
    m_releaseCycles = 0;
    m_intraMatrix.fill(16u);
    m_nonIntraMatrix.fill(16u);
    m_vqClut.fill(0u);
    m_threshold0 = 0;
    m_threshold1 = 0;
    m_dcPred.fill(0);
    m_quantizerScale = 0;
}

uint32_t Ps2Ipu::readRegister(uint32_t address) const
{
    const uint32_t fifoQw = static_cast<uint32_t>((m_fifo.size() - std::min<size_t>(m_fifo.size(), (m_bitPos / 128u) * 16u)) / 16u);
    // OFC must count only readable data: games poll it before reading OUT_FIFO with lq.
    const uint32_t outQw = static_cast<uint32_t>(std::min<size_t>(outputBytes() / 16u, 8u));
    switch (address & ~3u)
    {
    case kRegCmd:
        return m_cmdData;
    case kRegCmd + 4u:
        return busy() ? kCtrlBusy : 0u;
    case kRegCtrl:
        return (m_ctrl & kCtrlWritable) | (m_commandError ? kCtrlEcd : 0u) | (m_startCodeSeen ? kCtrlScd : 0u) |
               (std::min(fifoQw, 8u) & 0xFu) | ((outQw & 0xFu) << 4u) | (busy() ? kCtrlBusy : 0u);
    case kRegBp:
        return (m_bitPos % 128u) | ((std::min(fifoQw, 8u) & 0xFu) << 8u);
    case kRegTop:
    {
        // BSTOP shows the next 32 bits without consuming them; reads must not disturb the stream.
        Ps2Ipu &self = const_cast<Ps2Ipu &>(*this);
        const Snapshot state = self.snapshot();
        uint32_t value = 0;
        try
        {
            value = self.peekBits(32);
        }
        catch (const NeedInput &)
        {
        }
        self.restore(state);
        return value;
    }
    case kRegTop + 4u:
        return busy() ? kCtrlBusy : 0u;
    default:
        return 0u;
    }
}

void Ps2Ipu::writeRegister(uint32_t address, uint32_t value)
{
    switch (address & ~3u)
    {
    case kRegCmd:
        m_cmd = value;
        m_busy = true;
        m_pending = true;
        runCommand();
        break;
    case kRegCtrl:
        if ((value & kCtrlRst) != 0u)
        {
            const std::array<uint8_t, 64> intra = m_intraMatrix;
            const std::array<uint8_t, 64> nonIntra = m_nonIntraMatrix;
            reset();
            // Reset clears the FIFOs and decoder state; the quantiser matrices are left as loaded.
            m_intraMatrix = intra;
            m_nonIntraMatrix = nonIntra;
        }
        m_ctrl = value & kCtrlWritable;
        break;
    default:
        break;
    }
}

void Ps2Ipu::writeInputFifo(const uint8_t *quadword)
{
    m_fifo.insert(m_fifo.end(), quadword, quadword + 16u);
    if (m_pending)
        runCommand();
}

void Ps2Ipu::advance(uint64_t eeCycles)
{
    if (m_releasedBytes >= m_output.size())
    {
        m_releaseCycles = 0;
        return;
    }
    // Output appears after a short latency and then flows freely: the lag is what games that re-arm
    // fromIPU rely on, while throttling throughput starves movie players that busy-wait on the drain.
    m_releaseCycles += eeCycles;
    if (m_releaseCycles < kEeCyclesPerMacroblock)
        return;
    m_releaseCycles = 0;
    m_releasedBytes = m_output.size();
}

bool Ps2Ipu::readOutputFifo(uint8_t *quadword)
{
    if (outputBytes() < 16u)
    {
        std::memset(quadword, 0, 16u);
        return false;
    }
    takeOutput(quadword, 16u);
    return true;
}

void Ps2Ipu::feedDmaInput(const uint8_t *data, size_t size)
{
    if (m_dmaCursor >= m_dmaInput.size())
    {
        m_dmaInput.clear();
        m_dmaCursor = 0;
    }
    m_dmaInput.insert(m_dmaInput.end(), data, data + size);
    inputAvailable();
}

void Ps2Ipu::setInputSource(InputSource source)
{
    m_inputSource = std::move(source);
}

void Ps2Ipu::inputAvailable()
{
    refill();
    if (m_pending)
        runCommand();
}

size_t Ps2Ipu::takeOutput(uint8_t *destination, size_t maxBytes)
{
    const size_t count = std::min(maxBytes, outputBytes());
    std::copy_n(m_output.begin(), count, destination);
    m_output.erase(m_output.begin(), m_output.begin() + static_cast<std::ptrdiff_t>(count));
    m_releasedBytes -= count;
    return count;
}

Ps2Ipu::Snapshot Ps2Ipu::snapshot() const
{
    return Snapshot{m_bitPos, m_dcPred, m_quantizerScale, m_output.size()};
}

void Ps2Ipu::restore(const Snapshot &state)
{
    // FIFO bytes stay: whatever was pulled from DMA meanwhile is still owed to the replay.
    m_bitPos = state.bitPos;
    m_dcPred = state.dcPred;
    m_quantizerScale = state.quantizerScale;
    m_output.resize(state.outputSize);
    m_releasedBytes = std::min(m_releasedBytes, m_output.size());
}

void Ps2Ipu::runCommand()
{
    const Snapshot state = snapshot();
    try
    {
        executeCommand();
        m_pending = false;
        m_busy = false;
        discardConsumed();
        refill();
    }
    catch (const NeedInput &)
    {
        // Keep BUSY set and replay the whole command once more input has been queued.
        restore(state);
    }
}

void Ps2Ipu::executeCommand()
{
    switch (m_cmd >> 28)
    {
    case kCmdBclr:
        commandBclr();
        break;
    case kCmdIdec:
        commandIdec();
        break;
    case kCmdBdec:
        commandBdec();
        break;
    case kCmdVdec:
        commandVdec();
        break;
    case kCmdFdec:
        commandFdec();
        break;
    case kCmdSetiq:
        commandSetiq();
        break;
    case kCmdSetvq:
        commandSetvq();
        break;
    case kCmdCsc:
        commandCsc();
        break;
    case kCmdSetth:
        commandSetth();
        break;
    default:
    {
        static bool warned[16] = {};
        const uint32_t code = m_cmd >> 28;
        if (!warned[code])
        {
            warned[code] = true;
            std::cerr << "[IPU] command 0x" << std::hex << code << std::dec << " is not emulated; completing it with ECD set" << std::endl;
        }
        m_commandError = true;
        break;
    }
    }
}

void Ps2Ipu::discardConsumed()
{
    const size_t consumed = std::min<size_t>((m_bitPos / 128u) * 16u, m_fifo.size() / 16u * 16u);
    if (consumed == 0u)
        return;
    m_fifo.erase(m_fifo.begin(), m_fifo.begin() + static_cast<std::ptrdiff_t>(consumed));
    m_bitPos -= static_cast<uint32_t>(consumed * 8u);
}

size_t Ps2Ipu::liveFifoBytes() const noexcept
{
    const size_t consumed = (m_bitPos / 128u) * 16u;
    return m_fifo.size() > consumed ? m_fifo.size() - consumed : 0u;
}

void Ps2Ipu::refill()
{
    // Like the hardware FIFO, accept input only while fewer than 8 unread quadwords are queued.
    // Consumed bytes are kept until the command completes so a rolled-back command can replay them.
    while (liveFifoBytes() < kFifoBytes && m_dmaCursor + 16u <= m_dmaInput.size())
    {
        m_fifo.insert(m_fifo.end(), m_dmaInput.begin() + static_cast<std::ptrdiff_t>(m_dmaCursor),
                      m_dmaInput.begin() + static_cast<std::ptrdiff_t>(m_dmaCursor + 16u));
        m_dmaCursor += 16u;
    }
    if (!m_inputSource || liveFifoBytes() >= kFifoBytes)
        return;
    const size_t wantQuadwords = (kFifoBytes - liveFifoBytes()) / 16u;
    uint8_t buffer[kFifoBytes];
    const size_t got = m_inputSource(buffer, wantQuadwords);
    m_fifo.insert(m_fifo.end(), buffer, buffer + got * 16u);
}

bool Ps2Ipu::ensureBits(uint32_t count)
{
    if (m_bitPos + count > m_fifo.size() * 8u)
        refill();
    return m_bitPos + count <= m_fifo.size() * 8u;
}

uint32_t Ps2Ipu::peekBits(uint32_t count)
{
    if (count == 0u)
        return 0u;
    if (!ensureBits(count))
        throw NeedInput{};
    uint64_t window = 0;
    const uint32_t firstByte = m_bitPos / 8u;
    for (uint32_t i = 0; i < 5u; ++i)
    {
        const size_t index = firstByte + i;
        window = (window << 8u) | (index < m_fifo.size() ? m_fifo[index] : 0u);
    }
    const uint32_t shift = 40u - (m_bitPos % 8u) - count;
    return static_cast<uint32_t>((window >> shift) & ((1ull << count) - 1ull));
}

uint32_t Ps2Ipu::getBits(uint32_t count)
{
    const uint32_t value = peekBits(count);
    skipBits(count);
    return value;
}

void Ps2Ipu::skipBits(uint32_t count)
{
    if (!ensureBits(count))
        throw NeedInput{};
    m_bitPos += count;
}

void Ps2Ipu::commandBclr()
{
    // BCLR empties only the FIFO; DMA data not yet transferred into it is kept.
    m_fifo.clear();
    m_bitPos = m_cmd & 0x7Fu;
    m_commandError = false;
    m_startCodeSeen = false;
}

void Ps2Ipu::commandFdec()
{
    skipBits(m_cmd & 0x3Fu);
    m_cmdData = peekBits(32);
}

void Ps2Ipu::commandSetiq()
{
    skipBits(m_cmd & 0x3Fu);
    // Matrices arrive in zigzag order like the MPEG quant_matrix syntax; keep them in raster order.
    std::array<uint8_t, 64> matrix{};
    for (int i = 0; i < 64; ++i)
        matrix[kZigzag[i]] = static_cast<uint8_t>(getBits(8));
    if ((m_cmd & (1u << 27)) != 0u)
        m_nonIntraMatrix = matrix;
    else
        m_intraMatrix = matrix;
}

void Ps2Ipu::commandSetvq()
{
    skipBits(m_cmd & 0x3Fu);
    for (auto &entry : m_vqClut)
    {
        const uint32_t high = getBits(8);
        entry = static_cast<uint16_t>((getBits(8) << 8u) | high);
    }
}

void Ps2Ipu::commandSetth()
{
    m_threshold0 = m_cmd & 0x1FFu;
    m_threshold1 = (m_cmd >> 16u) & 0x1FFu;
}

void Ps2Ipu::commandCsc()
{
    const uint32_t count = m_cmd & 0x7FFu;
    const bool rgb16 = (m_cmd & (1u << 27)) != 0u;
    const bool dither = (m_cmd & (1u << 26)) != 0u;
    for (uint32_t mb = 0; mb < count; ++mb)
    {
        // RAW8 input is byte-aligned: copy what the FIFO already holds instead of 384 getBits calls.
        uint8_t raw[384];
        size_t filled = 0;
        while (filled < sizeof(raw))
        {
            if ((m_bitPos & 7u) == 0u && m_bitPos / 8u < m_fifo.size())
            {
                const size_t available = std::min(sizeof(raw) - filled, m_fifo.size() - m_bitPos / 8u);
                std::memcpy(raw + filled, m_fifo.data() + m_bitPos / 8u, available);
                filled += available;
                m_bitPos += static_cast<uint32_t>(available * 8u);
                continue;
            }
            raw[filled++] = static_cast<uint8_t>(getBits(8));
        }
        emitMacroblock(raw, raw + 256, raw + 320, rgb16, dither);
    }
}

void Ps2Ipu::commandVdec()
{
    skipBits(m_cmd & 0x3Fu);
    const Tables &t = tables();
    const uint32_t start = m_bitPos;
    int32_t value = 0;
    bool valid = true;
    switch ((m_cmd >> 26u) & 3u)
    {
    case 0: // macroblock_address_increment; escape and stuffing come back as 0x23 and 0x22
    {
        const ShortEntry entry = t.mba[peekBits(11)];
        const bool mpeg1 = (m_ctrl & (1u << 23)) != 0u;
        valid = entry.length != 0u && (entry.value != 35u || mpeg1);
        if (valid)
        {
            skipBits(entry.length);
            value = entry.value == 34u ? 0x23 : (entry.value == 35u ? 0x22 : entry.value);
        }
        break;
    }
    case 1: // macroblock_type for the picture type in CTRL.PCT
    {
        const uint32_t pictureType = (m_ctrl >> 24u) & 7u;
        const int table = pictureType >= 1u && pictureType <= 4u ? static_cast<int>(pictureType) - 1 : 0;
        const ShortEntry entry = t.mbType[table][peekBits(6)];
        valid = entry.length != 0u;
        if (valid)
        {
            skipBits(entry.length);
            value = entry.value;
        }
        break;
    }
    case 2: // motion_code
        if (getBits(1) != 0u)
            break;
        {
            // The leading '0' was consumed above, so the next nine bits complete the 10-bit index.
            const ShortEntry entry = t.motion[peekBits(9)];
            valid = entry.length > 1u;
            if (valid)
            {
                skipBits(entry.length - 1u);
                value = getBits(1) != 0u ? -static_cast<int32_t>(entry.value) : entry.value;
            }
        }
        break;
    default: // dmvector: '0' 0, '10' +1, '11' -1
        if (getBits(1) != 0u)
            value = getBits(1) != 0u ? -1 : 1;
        break;
    }
    // DATA holds the decoded symbol in bits 0-15 and the code length in bits 16-31.
    const uint32_t length = m_bitPos - start;
    m_cmdData = valid ? ((static_cast<uint32_t>(value) & 0xFFFFu) | (length << 16u)) : 0u;
    m_commandError = !valid;
}

void Ps2Ipu::commandBdec()
{
    skipBits(m_cmd & 0x3Fu);
    m_commandError = false;
    m_startCodeSeen = false;

    const uint32_t idp = (m_ctrl >> 16u) & 3u;
    const bool intra = (m_cmd & (1u << 27)) != 0u;
    const bool dcReset = (m_cmd & (1u << 26)) != 0u;
    const bool fieldDct = (m_cmd & (1u << 25)) != 0u;
    const bool nonLinear = (m_ctrl & (1u << 22)) != 0u;
    const uint32_t code = (m_cmd >> 16u) & 0x1Fu;
    m_quantizerScale = nonLinear ? static_cast<int32_t>(kNonLinearQuantizer[code]) : static_cast<int32_t>(code << 1u);
    if (dcReset)
        m_dcPred.fill(1 << (7 + idp));

    // RAW16 macroblock: 16x16 luma, then 8x8 Cb and Cr, as signed halfwords.
    int16_t luma[256] = {};
    int16_t cb[64] = {};
    int16_t cr[64] = {};
    const uint32_t pattern = intra ? 0x3Fu : decodeCodedBlockPattern();
    for (int i = 0; i < 6; ++i)
    {
        if ((pattern & (0x20u >> i)) == 0u)
            continue;
        int16_t block[64];
        const bool ok = intra ? decodeIntraBlock(i < 4 ? 0 : i - 3, block) : decodeNonIntraBlock(block);
        if (!ok)
        {
            m_commandError = true;
            return;
        }
        int16_t *destination = i == 4 ? cb : (i == 5 ? cr : nullptr);
        int stride = 8;
        if (destination == nullptr)
        {
            const int x = (i & 1) * 8;
            destination = fieldDct ? luma + (i >> 1) * 16 + x : luma + (i >> 1) * 128 + x;
            stride = fieldDct ? 32 : 16;
        }
        inverseDctSigned(block, destination, stride, intra);
    }
    // Halfwords go out little-endian, which is the host layout on every supported target.
    for (const int16_t *plane : {static_cast<const int16_t *>(luma), static_cast<const int16_t *>(cb), static_cast<const int16_t *>(cr)})
    {
        const auto *bytes = reinterpret_cast<const uint8_t *>(plane);
        m_output.insert(m_output.end(), bytes, bytes + (plane == luma ? sizeof(luma) : sizeof(cb)));
    }

    // Zero bits after a macroblock mean the slice ended: stop on the start code (SCD) or flag garbage (ECD).
    if (peekBits(8) == 0u)
    {
        skipBits((8u - (m_bitPos % 8u)) % 8u);
        for (;;)
        {
            const uint32_t next = peekBits(24);
            if (next != 0u)
            {
                if (next == 1u)
                    m_startCodeSeen = true;
                else
                    m_commandError = true;
                break;
            }
            skipBits(8);
        }
    }
}

uint32_t Ps2Ipu::decodeCodedBlockPattern()
{
    const ShortEntry entry = tables().cbp[peekBits(9)];
    if (entry.length == 0u)
        return 0u;
    skipBits(entry.length);
    return entry.value;
}

bool Ps2Ipu::decodeNonIntraBlock(int16_t *block)
{
    const Tables &t = tables();
    std::fill(block, block + 64, int16_t{0});

    const bool mpeg1 = (m_ctrl & (1u << 23)) != 0u;
    const bool alternateScan = (m_ctrl & (1u << 20)) != 0u;
    const uint8_t *scan = alternateScan ? kAlternateScan : kZigzag;

    int32_t sum = 0;
    int index = -1;
    for (;;)
    {
        int run = 0;
        int level = 0;
        if (index < 0 && peekBits(1) != 0u)
        {
            // First coefficient of a non-intra block: '1s' is run 0, level 1 (there is no end of block yet).
            skipBits(1);
            level = getBits(1) != 0u ? -1 : 1;
        }
        else
        {
            const DctEntry entry = t.dct[0][peekBits(16)];
            switch (entry.kind)
            {
            case DctKind::EndOfBlock:
                skipBits(entry.length);
                if (!mpeg1 && (sum & 1) == 0)
                    block[63] ^= 1;
                return true;
            case DctKind::RunLevel:
                skipBits(entry.length);
                run = entry.run;
                level = getBits(1) != 0u ? -static_cast<int>(entry.level) : entry.level;
                break;
            case DctKind::Escape:
                skipBits(entry.length);
                run = static_cast<int>(getBits(6));
                if (mpeg1)
                {
                    level = static_cast<int8_t>(getBits(8));
                    if (level == 0)
                        level = static_cast<int>(getBits(8));
                    else if (level == -128)
                        level = static_cast<int>(getBits(8)) - 256;
                }
                else
                {
                    level = static_cast<int>(getBits(12));
                    if (level & 0x800)
                        level -= 0x1000;
                }
                break;
            default:
                return false;
            }
        }

        index += run + 1;
        if (index > 63)
            return false;
        const int position = scan[index];
        int32_t magnitude = ((2 * std::abs(level) + 1) * m_quantizerScale * m_nonIntraMatrix[position]) >> 5;
        if (mpeg1 && magnitude != 0 && (magnitude & 1) == 0)
            magnitude -= 1; // MPEG-1 oddification
        int32_t value = level < 0 ? -magnitude : magnitude;
        value = std::clamp(value, -2048, 2047);
        block[position] = static_cast<int16_t>(value);
        sum += value;
    }
}

int Ps2Ipu::decodeMacroblockAddressIncrement()
{
    int increment = 0;
    for (;;)
    {
        const ShortEntry entry = tables().mba[peekBits(11)];
        if (entry.length == 0u)
            return -1;
        skipBits(entry.length);
        if (entry.value == 34u) // macroblock_escape
        {
            increment += 33;
            continue;
        }
        if (entry.value == 35u) // macroblock_stuffing
            continue;
        return increment + entry.value;
    }
}

void Ps2Ipu::commandIdec()
{
    skipBits(m_cmd & 0x3Fu);
    m_commandError = false;
    m_startCodeSeen = false;

    const uint32_t idp = (m_ctrl >> 16u) & 3u;
    const bool decodeDctType = (m_cmd & (1u << 24)) != 0u;
    const bool rgb16 = (m_cmd & (1u << 27)) != 0u;
    const bool dither = (m_cmd & (1u << 26)) != 0u;
    const bool nonLinear = (m_ctrl & (1u << 22)) != 0u;
    auto scaleFor = [&](uint32_t code)
    { return nonLinear ? static_cast<int32_t>(kNonLinearQuantizer[code & 31u]) : static_cast<int32_t>((code & 31u) << 1u); };

    m_quantizerScale = scaleFor((m_cmd >> 16u) & 0x1Fu);
    m_dcPred.fill(1 << (7 + idp));

    for (;;)
    {
        // macroblock_type for I pictures: '1' intra, '01' intra with quantiser_scale_code.
        bool quant = false;
        if (getBits(1) == 0u)
        {
            if (getBits(1) == 0u)
            {
                m_commandError = true;
                return;
            }
            quant = true;
        }
        const bool fieldDct = decodeDctType && getBits(1) != 0u;
        if (quant)
            m_quantizerScale = scaleFor(getBits(5));

        int16_t blocks[6][64];
        for (int i = 0; i < 6; ++i)
        {
            if (!decodeIntraBlock(i < 4 ? 0 : i - 3, blocks[i]))
            {
                m_commandError = true;
                return;
            }
        }

        uint8_t luma[256];
        uint8_t cb[64];
        uint8_t cr[64];
        for (int i = 0; i < 4; ++i)
        {
            // Field DCT interleaves the two upper blocks' rows with the lower ones.
            const int x = (i & 1) * 8;
            uint8_t *destination = fieldDct ? luma + (i >> 1) * 16 + x : luma + (i >> 1) * 128 + x;
            inverseDct(blocks[i], destination, fieldDct ? 32 : 16);
        }
        inverseDct(blocks[4], cb, 8);
        inverseDct(blocks[5], cr, 8);
        emitMacroblock(luma, cb, cr, rgb16, dither);

        // A start code ends the picture; anything but an increment of one is a decode error.
        if (peekBits(23) == 0u)
        {
            // Leave the stream on the byte-aligned start code so FDEC 32 steps over exactly that code.
            skipBits((8u - (m_bitPos % 8u)) % 8u);
            while (peekBits(24) != 0x000001u)
                skipBits(8);
            m_startCodeSeen = true;
            return;
        }
        const int increment = decodeMacroblockAddressIncrement();
        if (increment != 1)
        {
            m_commandError = true;
            return;
        }
    }
}

bool Ps2Ipu::decodeIntraBlock(int component, int16_t *block)
{
    const Tables &t = tables();
    std::fill(block, block + 64, int16_t{0});

    const uint32_t idp = (m_ctrl >> 16u) & 3u;
    const bool mpeg1 = (m_ctrl & (1u << 23)) != 0u;
    const bool alternateScan = (m_ctrl & (1u << 20)) != 0u;
    const bool tableOne = (m_ctrl & (1u << 21)) != 0u && !mpeg1;
    const uint8_t *scan = alternateScan ? kAlternateScan : kZigzag;

    const ShortEntry dc = component == 0 ? t.dcLuma[peekBits(10)] : t.dcChroma[peekBits(10)];
    if (dc.length == 0u)
        return false;
    skipBits(dc.length);
    int32_t diff = 0;
    if (dc.value != 0u)
    {
        diff = static_cast<int32_t>(getBits(dc.value));
        if (diff < (1 << (dc.value - 1)))
            diff -= (1 << dc.value) - 1;
    }
    m_dcPred[component] += diff;
    int32_t sum = m_dcPred[component] << (3 - idp);
    block[0] = static_cast<int16_t>(sum);

    const std::vector<DctEntry> &table = t.dct[tableOne ? 1 : 0];
    int index = 0;
    for (;;)
    {
        const DctEntry entry = table[peekBits(16)];
        int run = 0;
        int level = 0;
        switch (entry.kind)
        {
        case DctKind::EndOfBlock:
            skipBits(entry.length);
            if (!mpeg1 && (sum & 1) == 0)
                block[63] ^= 1; // MPEG-2 mismatch control
            return true;
        case DctKind::RunLevel:
            skipBits(entry.length);
            run = entry.run;
            level = getBits(1) != 0u ? -static_cast<int>(entry.level) : entry.level;
            break;
        case DctKind::Escape:
            skipBits(entry.length);
            run = static_cast<int>(getBits(6));
            if (mpeg1)
            {
                level = static_cast<int8_t>(getBits(8));
                if (level == 0)
                    level = static_cast<int>(getBits(8));
                else if (level == -128)
                    level = static_cast<int>(getBits(8)) - 256;
            }
            else
            {
                level = static_cast<int>(getBits(12));
                if (level & 0x800)
                    level -= 0x1000;
            }
            break;
        default:
            return false;
        }

        index += run + 1;
        if (index > 63)
            return false;
        const int position = scan[index];
        int32_t magnitude = (std::abs(level) * m_quantizerScale * m_intraMatrix[position]) >> 4;
        if (mpeg1 && magnitude != 0 && (magnitude & 1) == 0)
            magnitude -= 1; // MPEG-1 oddification
        int32_t value = level < 0 ? -magnitude : magnitude;
        value = std::clamp(value, -2048, 2047);
        block[position] = static_cast<int16_t>(value);
        sum += value;
    }
}

static int inverseDctRows(const int16_t *coefficients, double (&rows)[8][8], int (&live)[8])
{
    // Most coded rows are all zero and contribute exactly nothing, so only the others are transformed and summed.
    const auto &c = tables().cosine;
    int count = 0;
    for (int y = 0; y < 8; ++y)
    {
        const int16_t *row = coefficients + y * 8;
        if ((row[0] | row[1] | row[2] | row[3] | row[4] | row[5] | row[6] | row[7]) == 0)
            continue;
        live[count++] = y;
        for (int x = 0; x < 8; ++x)
        {
            double value = 0.0;
            for (int u = 0; u < 8; ++u)
                value += c[x][u] * row[u];
            rows[y][x] = value;
        }
    }
    return count;
}

void Ps2Ipu::inverseDctSigned(const int16_t *coefficients, int16_t *destination, int stride, bool intra) const
{
    // Intra blocks are pixels (0..255); residuals saturate to the 9-bit range motion compensation adds.
    uint8_t pixels[64];
    if (intra)
    {
        inverseDct(coefficients, pixels, 8);
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
                destination[y * stride + x] = pixels[y * 8 + x];
        return;
    }
    double rows[8][8];
    int live[8];
    const int liveRows = inverseDctRows(coefficients, rows, live);
    const auto &c = tables().cosine;
    for (int x = 0; x < 8; ++x)
        for (int y = 0; y < 8; ++y)
        {
            double value = 0.0;
            for (int i = 0; i < liveRows; ++i)
                value += c[y][live[i]] * rows[live[i]][x];
            destination[y * stride + x] = static_cast<int16_t>(std::clamp(static_cast<int>(std::floor(value / 4.0 + 0.5)), -256, 255));
        }
}


void Ps2Ipu::inverseDct(const int16_t *coefficients, uint8_t *destination, int stride) const
{
    double rows[8][8];
    int live[8];
    const int liveRows = inverseDctRows(coefficients, rows, live);
    const auto &c = tables().cosine;
    for (int x = 0; x < 8; ++x)
    {
        for (int y = 0; y < 8; ++y)
        {
            double value = 0.0;
            for (int i = 0; i < liveRows; ++i)
                value += c[y][live[i]] * rows[live[i]][x];
            destination[y * stride + x] = clampByte(static_cast<int>(std::floor(value / 4.0 + 0.5)));
        }
    }
}

void Ps2Ipu::emitMacroblock(const uint8_t *luma, const uint8_t *cb, const uint8_t *cr, bool rgb16, bool dither)
{
    static constexpr int kDither[4][4] = {{-4, 0, -3, 1}, {2, -2, 3, -1}, {-3, 1, -4, 0}, {3, -1, 2, -2}};
    uint8_t pixels[1024];
    uint8_t *out = pixels;
    for (int y = 0; y < 16; ++y)
    {
        for (int x = 0; x < 16; ++x)
        {
            // ITU-R BT.601 studio range, in the IPU's 1/128 fixed-point coefficients.
            const int lum = 0x95 * (luma[y * 16 + x] - 16);
            const int u = cb[(y / 2) * 8 + x / 2] - 128;
            const int v = cr[(y / 2) * 8 + x / 2] - 128;
            int r = (lum + 0xCC * v) >> 7;
            int g = (lum - 0x68 * v - 0x32 * u) >> 7;
            int b = (lum + 0x102 * u) >> 7;
            r = std::clamp(r, 0, 255);
            g = std::clamp(g, 0, 255);
            b = std::clamp(b, 0, 255);

            uint8_t alpha = 0x80;
            if (static_cast<uint32_t>(r) < m_threshold0 && static_cast<uint32_t>(g) < m_threshold0 && static_cast<uint32_t>(b) < m_threshold0)
                alpha = 0;
            else if (static_cast<uint32_t>(r) < m_threshold1 && static_cast<uint32_t>(g) < m_threshold1 && static_cast<uint32_t>(b) < m_threshold1)
                alpha = 0x40;

            if (!rgb16)
            {
                *out++ = static_cast<uint8_t>(r);
                *out++ = static_cast<uint8_t>(g);
                *out++ = static_cast<uint8_t>(b);
                *out++ = alpha;
                continue;
            }
            if (dither)
            {
                const int offset = kDither[y & 3][x & 3];
                r = std::clamp(r + offset, 0, 255);
                g = std::clamp(g + offset, 0, 255);
                b = std::clamp(b + offset, 0, 255);
            }
            const uint16_t packed = static_cast<uint16_t>((r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10) | (alpha == 0x40 ? 0x8000 : 0));
            *out++ = static_cast<uint8_t>(packed & 0xFFu);
            *out++ = static_cast<uint8_t>(packed >> 8u);
        }
    }
    m_output.insert(m_output.end(), pixels, out);
}
