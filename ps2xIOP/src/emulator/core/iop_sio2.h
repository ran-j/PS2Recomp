#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <fstream>
#include <string>
#include <vector>

namespace ps2x::iop::detail
{
    // PS2 memory card as seen over SIO2: 528-byte pages (512 data + 16 ECC), 16-page erase blocks.
    // The image layout matches PCSX2's .ps2 files, so cards can move between the two.
    class IopMemoryCard
    {
    public:
        static constexpr uint32_t kPageSize = 512u;
        static constexpr uint32_t kSpareSize = 16u;
        static constexpr uint32_t kRawPageSize = kPageSize + kSpareSize;
        static constexpr uint32_t kPagesPerBlock = 16u;
        static constexpr uint32_t kPageCount = 0x4000u; // 8 MB

        // The image is opened on first use, so hosts that never touch SIO2 never create a file.
        void attach(const std::string &path);
        bool ensureOpen();
        bool open(const std::string &path);
        void close();
        [[nodiscard]] bool present() const noexcept { return !m_image.empty(); }
        [[nodiscard]] const std::string &path() const noexcept { return m_path; }

        void setSector(uint32_t page) noexcept;
        void read(uint8_t *destination, size_t size);
        // Flash programming can only clear bits; erase is what sets them again.
        void write(const uint8_t *source, size_t size);
        void eraseBlock();

        uint8_t terminator = 0x55;

    private:
        void flush(size_t offset, size_t size);

        std::string m_path;
        std::string m_attachedPath;
        bool m_openFailed = false;
        std::vector<uint8_t> m_image;
        std::fstream m_file;
        size_t m_transferAddress = 0;
    };

    // A DualShock 2 at rest. Real input reaches the game through the EE-side pad library, but IOP drivers
    // such as sio2d judge the bus by the controller in port 1, as on a console where one is always plugged in.
    class IopDualShock2
    {
    public:
        void reset() noexcept;
        // Answers one packet (command byte first, after the 0x01 address byte) with a reply of equal length.
        void transfer(const std::deque<uint8_t> &command, std::deque<uint8_t> &reply);

    private:
        [[nodiscard]] uint8_t modeId() const noexcept;

        bool m_analog = false;
        bool m_config = false;
        bool m_pressure = false;
        std::array<uint8_t, 6> m_vibrationMap{0x00, 0x01, 0xFF, 0xFF, 0xFF, 0xFF};
    };

    // SIO2 serial controller (0x1F808200-0x1F808280) with DMA channels 11 (in) and 12 (out).
    // Port 0 holds a DualShock 2 and slot 1 a memory card; the second pad port stays empty.
    class IopSio2
    {
    public:
        static constexpr uint32_t kBase = 0x1F808200u;
        static constexpr uint32_t kEnd = 0x1F808284u;
        static constexpr uint32_t kDataIn = 0x1F808260u;
        static constexpr uint32_t kDataOut = 0x1F808264u;
        static constexpr int kIrq = 17;

        void reset();
        IopMemoryCard &card(size_t port) noexcept { return m_cards[port & 1u]; }

        [[nodiscard]] static bool contains(uint32_t address) noexcept { return address >= kBase && address < kEnd; }
        uint32_t read32(uint32_t address);
        void write32(uint32_t address, uint32_t value);
        uint8_t readData();
        void writeData(uint8_t value);

        // DMA11 delivers command bytes in blocks; replies are padded to the same block size for DMA12.
        void dmaIn(const uint8_t *data, size_t size, size_t blockSize);
        void dmaOut(uint8_t *data, size_t size);

        [[nodiscard]] bool takeInterrupt() noexcept;

    private:
        void softReset();
        void process();
        void memoryCard();
        void pad();

        std::array<uint32_t, 16> m_send3{};
        std::array<uint32_t, 4> m_send1{};
        std::array<uint32_t, 4> m_send2{};
        uint32_t m_ctrl = 0x3BCu;
        uint32_t m_recv1 = 0x1D100u;
        uint32_t m_recv2 = 0xFu;
        uint32_t m_recv3 = 0u;
        uint32_t m_unknown278 = 0u;
        uint32_t m_unknown27c = 0u;
        uint32_t m_istat = 0u;
        bool m_interruptPending = false;

        std::deque<uint8_t> m_fifoIn;
        std::deque<uint8_t> m_fifoOut;
        size_t m_queuePosition = 0;
        size_t m_commandLength = 0;
        size_t m_dmaBlockSize = 0;
        uint32_t m_port = 0;
        bool m_queueRead = false;
        bool m_queueComplete = false;

        std::array<IopMemoryCard, 2> m_cards;
        IopDualShock2 m_pad;
    };
}
