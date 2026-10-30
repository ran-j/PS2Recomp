#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

// Image Processing Unit: MPEG-1/2 intra decoding and colour conversion fed through the
// IPU FIFOs (0x10007000/0x10007010) or the toIPU/fromIPU DMA channels.
// Commands run synchronously; one that runs out of input is rolled back and retried
// when more data arrives, so callers never observe a half-executed command.
class Ps2Ipu
{
public:
    static constexpr uint32_t kRegCmd = 0x10002000u;
    static constexpr uint32_t kRegCtrl = 0x10002010u;
    static constexpr uint32_t kRegBp = 0x10002020u;
    static constexpr uint32_t kRegTop = 0x10002030u;

    Ps2Ipu();

    void reset();

    // 32-bit register access in 0x10002000-0x1000203F; the high words carry BUSY.
    uint32_t readRegister(uint32_t address) const;
    void writeRegister(uint32_t address, uint32_t value);

    // IN_FIFO write / OUT_FIFO read, one quadword each.
    void writeInputFifo(const uint8_t *quadword);
    bool readOutputFifo(uint8_t *quadword);

    // Data delivered by the toIPU DMA; it enters the 8-quadword input FIFO as commands consume it.
    void feedDmaInput(const uint8_t *data, size_t size);
    size_t takeOutput(uint8_t *destination, size_t maxBytes);
    // Decoded data becomes visible at a hardware-like rate; games re-arm fromIPU expecting the IPU to lag.
    size_t outputBytes() const noexcept { return std::min(m_output.size(), m_releasedBytes); }
    void advance(uint64_t eeCycles);
    static constexpr uint64_t kEeCyclesPerMacroblock = 1024u;

    bool busy() const noexcept { return m_busy; }

private:
    struct Snapshot
    {
        std::vector<uint8_t> fifo;
        uint32_t bitPos = 0;
        size_t dmaCursor = 0;
        std::array<int32_t, 3> dcPred{};
        int32_t quantizerScale = 0;
        size_t outputSize = 0;
    };

    struct NeedInput
    {
    };

    void runCommand();
    void executeCommand();
    Snapshot snapshot() const;
    void restore(const Snapshot &state);

    // Bitstream access over the input FIFO, refilled from pending DMA input.
    void refill();
    bool ensureBits(uint32_t count);
    uint32_t peekBits(uint32_t count);
    uint32_t getBits(uint32_t count);
    void skipBits(uint32_t count);

    void commandBclr();
    void commandIdec();
    void commandFdec();
    void commandSetiq();
    void commandSetvq();
    void commandSetth();
    void commandCsc();

    int decodeMacroblockAddressIncrement();
    bool decodeIntraBlock(int component, int16_t *block);
    void inverseDct(const int16_t *coefficients, uint8_t *destination, int stride) const;
    void emitMacroblock(const uint8_t *luma, const uint8_t *cb, const uint8_t *cr, bool rgb16, bool dither);

    uint32_t m_cmd = 0;
    uint32_t m_cmdData = 0;
    uint32_t m_ctrl = 0;
    bool m_busy = false;
    bool m_pending = false;
    bool m_commandError = false;
    bool m_startCodeSeen = false;

    std::vector<uint8_t> m_fifo;
    uint32_t m_bitPos = 0;
    std::vector<uint8_t> m_dmaInput;
    size_t m_dmaCursor = 0;
    std::deque<uint8_t> m_output;
    size_t m_releasedBytes = 0;
    uint64_t m_releaseCycles = 0;

    std::array<uint8_t, 64> m_intraMatrix{};
    std::array<uint8_t, 64> m_nonIntraMatrix{};
    std::array<uint16_t, 16> m_vqClut{};
    uint32_t m_threshold0 = 0;
    uint32_t m_threshold1 = 0;
    std::array<int32_t, 3> m_dcPred{};
    int32_t m_quantizerScale = 0;
};
