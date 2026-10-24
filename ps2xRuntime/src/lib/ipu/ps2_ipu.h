#pragma once
// EE Image Processing Unit (IPU): registers, bitstream FIFO, command state
// machine and the intra decode path (IDEC). PS2Memory owns one and routes the
// IPU registers, the FIFO ports and DMA ch.3/ch.4 to it.
//
// Registers (PS2Tek "IPU I/O Registers"; PCSX2 pcsx2/IPU/IPU.cpp):
//   0x10002000 IPU_CMD  w: bits 28-31 command, 0-27 option
//                       r: bits 0-31 DATA (FDEC/VDEC result, else the next
//                          32 bitstream bits), bit 63 BUSY
//   0x10002010 IPU_CTRL bits 0-3 IFC, 4-7 OFC, 8-13 CBP, 14 ECD, 15 SCD,
//                       16-17 IDP, 20 AS, 21 IVF, 22 QST, 23 MP1,
//                       24-26 PCT, 30 RST (write only), 31 BUSY
//   0x10002020 IPU_BP   bits 0-6 BP, 8-11 IFC, 16-17 FP
//   0x10002030 IPU_TOP  bits 0-31 next 32 bitstream bits, bit 63 set while
//                       fewer than 32 bits are queued
//   0x10007000 output FIFO port (128-bit read), 0x10007010 input FIFO port
//   (128-bit write); DMA ch.3 IPU_FROM drains the output, ch.4 IPU_TO feeds
//   the input.
// Commands: 0 BCLR, 1 IDEC, 2 BDEC, 3 VDEC, 4 FDEC, 5 SETIQ, 6 SETVQ, 7 CSC,
// 8 PACK, 9 SETTH. BDEC, VDEC, CSC, PACK and IDEC's RGB16 output are not
// implemented: they set ECD and log "[IPU:unimplemented]" instead of
// pretending to succeed.
//
// Timing model: a command runs to completion as soon as the input FIFO holds
// all the bits it needs; until then BUSY stays set and the command is retried
// whenever input arrives. IDEC additionally holds BUSY until its macroblocks
// have been drained from the output FIFO.
#include <cstddef>
#include <cstdint>
#include <vector>

class Ps2Ipu
{
public:
    static constexpr size_t kMacroblockRgb32Bytes = 16u * 16u * 4u;

    Ps2Ipu() = default;
    Ps2Ipu(const Ps2Ipu &) = delete;
    Ps2Ipu &operator=(const Ps2Ipu &) = delete;

    void reset();

    void writeCommand(uint32_t value);
    void writeControl(uint32_t value);
    uint64_t readCommand();
    uint32_t readControl() const;
    uint32_t readBp() const;
    uint64_t readTop() const;

    // Input FIFO. pushInput only queues bytes; runPending retries a command
    // that is waiting for input. feedInput does both.
    void pushInput(const uint8_t *data, size_t size);
    void runPending();
    void feedInput(const uint8_t *data, size_t size)
    {
        pushInput(data, size);
        runPending();
    }

    size_t outputBytes() const { return m_out.size() - m_outHead; }
    size_t drainOutput(uint8_t *dst, size_t maxBytes);

    bool busy() const;

private:
    struct Cursor;

    size_t availableBits() const;
    uint32_t peekBits(size_t bitPos, unsigned count) const;
    void copyBits(size_t bitPos, uint8_t *dst, size_t bytes) const;
    bool runIdec();
    void softReset();
    void compactInput();

    std::vector<uint8_t> m_in; // input FIFO + internal buffer, QW aligned
    size_t m_bitPos = 0;       // absolute bit position of the decoder in m_in
    std::vector<uint8_t> m_out; // output FIFO
    size_t m_outHead = 0;

    uint32_t m_ctrlConfig = 0; // writable CTRL bits (IDP/AS/IVF/QST/MP1/PCT)
    uint32_t m_cbp = 0;
    bool m_ecd = false;
    bool m_scd = false;
    uint32_t m_cmdData = 0;
    uint32_t m_top = 0;
    uint32_t m_lastCode = 0xFFFFFFFFu;
    bool m_pending = false;
    uint32_t m_pendingCmd = 0;
    bool m_busyUntilDrained = false;

    uint8_t m_intraQuant[64]{}; // SETIQ order (zigzag), as transmitted
    uint8_t m_nonIntraQuant[64]{};
    uint8_t m_vqClut[32]{};
    uint16_t m_th0 = 0;
    uint16_t m_th1 = 0;

    // Log budgets, so a title that hammers one of these cannot flood stderr.
    uint32_t m_busyWritesReported = 0;
    uint32_t m_unimplementedReported = 0;
    uint32_t m_idecErrorsReported = 0;
};
