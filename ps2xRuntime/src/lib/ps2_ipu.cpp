#include "runtime/ps2_ipu.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>

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
    constexpr uint32_t kCtrlWritable = (3u << 16) | (1u << 20) | (1u << 21) | (1u << 22) | (1u << 23) | (7u << 24) | kCtrlRst;
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
    const uint32_t outQw = static_cast<uint32_t>(std::min<size_t>(m_output.size() / 16u, 8u));
    switch (address & ~3u)
    {
    case kRegCmd:
        return m_cmdData;
    case kRegCmd + 4u:
        return m_busy ? kCtrlBusy : 0u;
    case kRegCtrl:
        return (m_ctrl & kCtrlWritable) | (m_commandError ? kCtrlEcd : 0u) | (m_startCodeSeen ? kCtrlScd : 0u) |
               (std::min(fifoQw, 8u) & 0xFu) | ((outQw & 0xFu) << 4u) | (m_busy ? kCtrlBusy : 0u);
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
        return m_busy ? kCtrlBusy : 0u;
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
    m_releaseCycles += eeCycles;
    const uint64_t macroblocks = m_releaseCycles / kEeCyclesPerMacroblock;
    m_releaseCycles %= kEeCyclesPerMacroblock;
    m_releasedBytes = static_cast<size_t>(std::min<uint64_t>(m_output.size(), m_releasedBytes + macroblocks * 1024u));
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
    return Snapshot{m_fifo, m_bitPos, m_dmaCursor, m_dcPred, m_quantizerScale, m_output.size()};
}

void Ps2Ipu::restore(const Snapshot &state)
{
    m_fifo = state.fifo;
    m_bitPos = state.bitPos;
    m_dmaCursor = state.dmaCursor;
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

void Ps2Ipu::refill()
{
    const size_t consumed = (m_bitPos / 128u) * 16u;
    if (consumed != 0u)
    {
        m_fifo.erase(m_fifo.begin(), m_fifo.begin() + static_cast<std::ptrdiff_t>(std::min(consumed, m_fifo.size())));
        m_bitPos -= static_cast<uint32_t>(consumed * 8u);
    }
    while (m_fifo.size() < kFifoBytes && m_dmaCursor + 16u <= m_dmaInput.size())
    {
        m_fifo.insert(m_fifo.end(), m_dmaInput.begin() + static_cast<std::ptrdiff_t>(m_dmaCursor),
                      m_dmaInput.begin() + static_cast<std::ptrdiff_t>(m_dmaCursor + 16u));
        m_dmaCursor += 16u;
    }
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
        uint8_t luma[256];
        uint8_t cb[64];
        uint8_t cr[64];
        for (uint8_t &value : luma)
            value = static_cast<uint8_t>(getBits(8));
        for (uint8_t &value : cb)
            value = static_cast<uint8_t>(getBits(8));
        for (uint8_t &value : cr)
            value = static_cast<uint8_t>(getBits(8));
        emitMacroblock(luma, cb, cr, rgb16, dither);
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

void Ps2Ipu::inverseDct(const int16_t *coefficients, uint8_t *destination, int stride) const
{
    const auto &c = tables().cosine;
    double rows[8][8];
    for (int y = 0; y < 8; ++y)
    {
        for (int x = 0; x < 8; ++x)
        {
            double value = 0.0;
            for (int u = 0; u < 8; ++u)
                value += c[x][u] * coefficients[y * 8 + u];
            rows[y][x] = value;
        }
    }
    for (int x = 0; x < 8; ++x)
    {
        for (int y = 0; y < 8; ++y)
        {
            double value = 0.0;
            for (int v = 0; v < 8; ++v)
                value += c[y][v] * rows[v][x];
            destination[y * stride + x] = clampByte(static_cast<int>(std::floor(value / 4.0 + 0.5)));
        }
    }
}

void Ps2Ipu::emitMacroblock(const uint8_t *luma, const uint8_t *cb, const uint8_t *cr, bool rgb16, bool dither)
{
    static constexpr int kDither[4][4] = {{-4, 0, -3, 1}, {2, -2, 3, -1}, {-3, 1, -4, 0}, {3, -1, 2, -2}};
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
                m_output.push_back(static_cast<uint8_t>(r));
                m_output.push_back(static_cast<uint8_t>(g));
                m_output.push_back(static_cast<uint8_t>(b));
                m_output.push_back(alpha);
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
            m_output.push_back(static_cast<uint8_t>(packed & 0xFFu));
            m_output.push_back(static_cast<uint8_t>(packed >> 8u));
        }
    }
}
