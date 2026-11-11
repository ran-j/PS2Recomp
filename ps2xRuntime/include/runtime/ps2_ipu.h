#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
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

    // Pulls up to N quadwords from the toIPU DMA channel into a buffer, returning how many were moved.
    // The channel only advances as the FIFO drains, which is what sceIpuStopDMA/RestartDMA rely on.
    using InputSource = std::function<size_t(uint8_t *destination, size_t maxQuadwords)>;
    void setInputSource(InputSource source);
    // Called when the source may have data again (a toIPU DMA was started).
    void inputAvailable();
    // Queues bytes as if already transferred by DMA; used where no live channel exists, such as tests.
    void feedDmaInput(const uint8_t *data, size_t size);
    size_t takeOutput(uint8_t *destination, size_t maxBytes);
    // Decoded data becomes visible after a hardware-like latency; games re-arm fromIPU expecting the IPU to lag.
    size_t outputBytes() const noexcept { return std::min(m_output.size(), m_releasedBytes); }
    void advance(uint64_t eeCycles);
    // Latency before output becomes visible: games re-arm fromIPU right after starting a command and
    // expect no data yet. Keep it short; movie players busy-wait on the drain.
    static constexpr uint64_t kEeCyclesPerMacroblock = 64u;

    // A command is only finished once its output fits in the 8-quadword OUT FIFO; until fromIPU drains
    // the rest it stays BUSY, which is what callers wait on before using the decoded data.
    static constexpr size_t kOutputFifoBytes = 8u * 16u;
    bool busy() const noexcept { return m_busy || m_output.size() > kOutputFifoBytes; }

private:
    struct Snapshot
    {
        uint32_t bitPos = 0;
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
    void discardConsumed();
    size_t liveFifoBytes() const noexcept;
    bool ensureBits(uint32_t count);
    uint32_t peekBits(uint32_t count);
    uint32_t getBits(uint32_t count);
    void skipBits(uint32_t count);

    void commandBclr();
    void commandIdec();
    void commandBdec();
    void commandVdec();
    void commandFdec();
    void commandSetiq();
    void commandSetvq();
    void commandSetth();
    void commandCsc();

    int decodeMacroblockAddressIncrement();
    bool decodeIntraBlock(int component, int16_t *block);
    bool decodeNonIntraBlock(int16_t *block);
    uint32_t decodeCodedBlockPattern();
    void inverseDct(const int16_t *coefficients, uint8_t *destination, int stride) const;
    void inverseDctSigned(const int16_t *coefficients, int16_t *destination, int stride, bool intra) const;
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
    InputSource m_inputSource;
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
