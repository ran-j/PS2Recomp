#include "iop_sio2.h"

#include <algorithm>
#include <filesystem>
#include <iostream>

namespace ps2x::iop::detail
{
    namespace
    {
        constexpr uint8_t kModePad = 0x01;
        constexpr uint8_t kModeMemoryCard = 0x81;

        constexpr uint32_t kRecvConnected = 0x1100u;
        constexpr uint32_t kRecvDisconnected = 0x1D100u;
        constexpr uint32_t kCtrlStart = 0x1u;
        constexpr uint32_t kCtrlResetFifos = 0xCu;
    }

    void IopMemoryCard::attach(const std::string &path)
    {
        if (path == m_attachedPath)
            return;
        close();
        m_attachedPath = path;
        m_openFailed = false;
    }

    bool IopMemoryCard::ensureOpen()
    {
        if (present())
            return true;
        if (m_attachedPath.empty() || m_openFailed)
            return false;
        m_openFailed = !open(m_attachedPath);
        if (m_openFailed)
            std::cerr << "[IOP] could not open or create memory card image " << m_attachedPath
                      << "; check the folder exists and is writable. Slot 1 reads as empty." << std::endl;
        return !m_openFailed;
    }

    bool IopMemoryCard::open(const std::string &path)
    {
        close();
        std::error_code ec;
        const size_t expected = static_cast<size_t>(kRawPageSize) * kPageCount;
        if (!std::filesystem::exists(path, ec))
        {
            // A new card is blank flash; the game formats it like a fresh retail card.
            std::ofstream create(path, std::ios::binary);
            if (!create)
                return false;
            const std::vector<uint8_t> blank(expected, 0xFFu);
            create.write(reinterpret_cast<const char *>(blank.data()), static_cast<std::streamsize>(blank.size()));
            if (!create)
                return false;
        }

        m_file.open(path, std::ios::binary | std::ios::in | std::ios::out);
        if (!m_file)
            return false;
        m_image.assign(expected, 0xFFu);
        m_file.read(reinterpret_cast<char *>(m_image.data()), static_cast<std::streamsize>(m_image.size()));
        m_file.clear();
        m_path = path;
        terminator = 0x55;
        m_transferAddress = 0;
        return true;
    }

    void IopMemoryCard::close()
    {
        if (m_file.is_open())
            m_file.close();
        m_image.clear();
        m_path.clear();
    }

    void IopMemoryCard::setSector(uint32_t page) noexcept
    {
        m_transferAddress = static_cast<size_t>(page) * kRawPageSize;
    }

    void IopMemoryCard::read(uint8_t *destination, size_t size)
    {
        for (size_t i = 0; i < size; ++i)
        {
            const size_t offset = m_transferAddress + i;
            destination[i] = offset < m_image.size() ? m_image[offset] : 0xFFu;
        }
        m_transferAddress += size;
    }

    void IopMemoryCard::write(const uint8_t *source, size_t size)
    {
        const size_t start = std::min(m_transferAddress, m_image.size());
        const size_t count = std::min(size, m_image.size() - start);
        for (size_t i = 0; i < count; ++i)
            m_image[start + i] &= source[i];
        flush(start, count);
        m_transferAddress += size;
    }

    void IopMemoryCard::eraseBlock()
    {
        const size_t blockBytes = static_cast<size_t>(kRawPageSize) * kPagesPerBlock;
        const size_t start = std::min(m_transferAddress, m_image.size());
        const size_t count = std::min(blockBytes, m_image.size() - start);
        std::fill_n(m_image.begin() + static_cast<std::ptrdiff_t>(start), count, uint8_t{0xFFu});
        flush(start, count);
    }

    void IopMemoryCard::flush(size_t offset, size_t size)
    {
        if (!m_file.is_open() || size == 0u)
            return;
        m_file.seekp(static_cast<std::streamoff>(offset));
        m_file.write(reinterpret_cast<const char *>(m_image.data() + offset), static_cast<std::streamsize>(size));
        m_file.flush();
    }

    void IopSio2::reset()
    {
        m_send3.fill(0u);
        m_send1.fill(0u);
        m_send2.fill(0u);
        m_ctrl = 0x3BCu;
        m_recv1 = kRecvDisconnected;
        m_recv2 = 0xFu;
        m_recv3 = 0u;
        m_unknown278 = 0u;
        m_unknown27c = 0u;
        m_istat = 0u;
        m_interruptPending = false;
        m_fifoOut.clear();
        m_port = 0;
        softReset();
    }

    void IopSio2::softReset()
    {
        m_queueRead = false;
        m_queuePosition = 0;
        m_commandLength = 0;
        m_dmaBlockSize = 0;
        m_queueComplete = false;
        m_fifoIn.clear();
        m_recv1 = 0u;
    }

    uint32_t IopSio2::read32(uint32_t address)
    {
        const uint32_t offset = address - kBase;
        if (offset < 0x40u)
            return m_send3[offset / 4u];
        if (offset < 0x60u)
            return (offset & 4u) != 0u ? m_send2[(offset - 0x40u) / 8u] : m_send1[(offset - 0x40u) / 8u];
        switch (address)
        {
        case kDataOut:
            return readData();
        case 0x1F808268u:
            return m_ctrl;
        case 0x1F80826Cu:
            return m_recv1;
        case 0x1F808270u:
            return m_recv2;
        case 0x1F808274u:
            return m_recv3;
        case 0x1F808278u:
            return m_unknown278;
        case 0x1F80827Cu:
            return m_unknown27c;
        case 0x1F808280u:
            return m_istat;
        default:
            return 0u;
        }
    }

    void IopSio2::write32(uint32_t address, uint32_t value)
    {
        const uint32_t offset = address - kBase;
        if (offset < 0x40u)
        {
            m_send3[offset / 4u] = value;
            // Writing the first queue entry begins a new packet.
            if (offset == 0u)
                softReset();
            return;
        }
        if (offset < 0x60u)
        {
            ((offset & 4u) != 0u ? m_send2 : m_send1)[(offset - 0x40u) / 8u] = value;
            return;
        }
        switch (address)
        {
        case kDataIn:
            writeData(static_cast<uint8_t>(value));
            break;
        case 0x1F808268u:
            m_ctrl = value;
            if ((value & kCtrlResetFifos) != 0u)
            {
                m_fifoIn.clear();
                m_fifoOut.clear();
            }
            if ((value & kCtrlStart) != 0u)
            {
                // Replies are produced as bytes arrive, so the transfer is complete once it starts.
                if (m_istat == 0u)
                    m_interruptPending = true;
                m_istat |= 1u;
            }
            break;
        case 0x1F80826Cu:
            m_recv1 = value;
            break;
        case 0x1F808270u:
            m_recv2 = value;
            break;
        case 0x1F808274u:
            m_recv3 = value;
            break;
        case 0x1F808278u:
            m_unknown278 = value;
            break;
        case 0x1F80827Cu:
            m_unknown27c = value;
            break;
        case 0x1F808280u:
            m_istat &= ~value;
            break;
        default:
            break;
        }
    }

    bool IopSio2::takeInterrupt() noexcept
    {
        const bool pending = m_interruptPending;
        m_interruptPending = false;
        return pending;
    }

    uint8_t IopSio2::readData()
    {
        if (m_fifoOut.empty())
            return 0xFFu;
        const uint8_t value = m_fifoOut.front();
        m_fifoOut.pop_front();
        return value;
    }

    void IopSio2::writeData(uint8_t value)
    {
        if (!m_queueRead)
        {
            if (m_queuePosition >= m_send3.size())
                return;
            const uint32_t command = m_send3[m_queuePosition];
            m_port = command & 1u;
            m_commandLength = (command >> 8u) & 0x3FFu;
            m_queueRead = true;
            if (m_commandLength == 0u)
                m_queueComplete = true;
            m_fifoIn.clear();
        }
        if (m_queueComplete)
            return;

        m_fifoIn.push_back(value);
        const bool fromDma = m_dmaBlockSize != 0u;
        if ((!fromDma && m_fifoIn.size() == m_commandLength) || (fromDma && m_fifoIn.size() == m_dmaBlockSize))
        {
            m_queueRead = false;
            ++m_queuePosition;
            process();
            if (fromDma)
            {
                while (m_fifoOut.size() % m_dmaBlockSize != 0u)
                    m_fifoOut.push_back(0x00u);
            }
        }
    }

    void IopSio2::dmaIn(const uint8_t *data, size_t size, size_t blockSize)
    {
        m_dmaBlockSize = blockSize;
        for (size_t i = 0; i < size; ++i)
            writeData(data[i]);
    }

    void IopSio2::dmaOut(uint8_t *data, size_t size)
    {
        for (size_t i = 0; i < size; ++i)
            data[i] = readData();
    }

    void IopSio2::process()
    {
        const uint8_t mode = m_fifoIn.front();
        m_fifoIn.pop_front();
        if (mode == kModeMemoryCard)
        {
            memoryCard();
            return;
        }

        // Pads, multitaps and infrared are not wired to SIO2 here: answer as an empty port.
        m_recv1 = mode == kModePad ? (m_recv1 | 0x1000u | (m_port == 0u ? 0x1D000u : 0x2D000u)) : kRecvDisconnected;
        m_fifoOut.push_back(0xFFu);
        while (!m_fifoIn.empty())
        {
            m_fifoIn.pop_front();
            m_fifoOut.push_back(0xFFu);
        }
    }

    void IopSio2::memoryCard()
    {
        IopMemoryCard &mc = m_cards[m_port];
        (void)mc.ensureOpen();
        m_recv1 = mc.present() ? kRecvConnected : kRecvDisconnected;
        const uint8_t command = m_fifoIn.front();
        m_fifoIn.pop_front();
        const uint8_t echo = mc.present() ? 0x00u : 0xFFu;
        // Reply lengths count from this command's first byte; earlier packets may still be queued.
        const size_t base = m_fifoOut.size();
        m_fifoOut.push_back(echo);
        m_fifoOut.push_back(echo);

        const auto pop = [this]() -> uint8_t
        {
            if (m_fifoIn.empty())
                return 0u;
            const uint8_t value = m_fifoIn.front();
            m_fifoIn.pop_front();
            return value;
        };
        // Most replies are zero padding ending in 0x2B and the terminator byte.
        const auto finish = [this, &mc, base](size_t length)
        {
            while (m_fifoOut.size() - base + 2u < length)
                m_fifoOut.push_back(0x00u);
            m_fifoOut.push_back(0x2Bu);
            m_fifoOut.push_back(mc.terminator);
        };

        if (!mc.present())
        {
            while (!m_fifoIn.empty())
            {
                m_fifoIn.pop_front();
                m_fifoOut.push_back(0xFFu);
            }
            return;
        }

        switch (command)
        {
        case 0x11: // probe
        case 0x12:
        case 0x81: // read/write end
            finish(4);
            break;
        case 0x21: // set erase sector
        case 0x22: // set write sector
        case 0x23: // set read sector
        {
            uint32_t page = 0;
            for (int i = 0; i < 4; ++i)
                page |= static_cast<uint32_t>(pop()) << (8 * i);
            (void)pop(); // XOR checksum of the address bytes
            mc.setSector(page);
            finish(9);
            break;
        }
        case 0x26: // get specs: page size, erase block size, page count, XOR
        {
            const uint8_t specs[8] = {
                static_cast<uint8_t>(IopMemoryCard::kPageSize), static_cast<uint8_t>(IopMemoryCard::kPageSize >> 8),
                static_cast<uint8_t>(IopMemoryCard::kPagesPerBlock), static_cast<uint8_t>(IopMemoryCard::kPagesPerBlock >> 8),
                static_cast<uint8_t>(IopMemoryCard::kPageCount), static_cast<uint8_t>(IopMemoryCard::kPageCount >> 8),
                static_cast<uint8_t>(IopMemoryCard::kPageCount >> 16), static_cast<uint8_t>(IopMemoryCard::kPageCount >> 24)};
            uint8_t checksum = 0;
            m_fifoOut.push_back(0x2Bu);
            for (const uint8_t value : specs)
            {
                checksum ^= value;
                m_fifoOut.push_back(value);
            }
            m_fifoOut.push_back(checksum);
            m_fifoOut.push_back(mc.terminator);
            break;
        }
        case 0x27: // set terminator
            mc.terminator = pop();
            m_fifoOut.push_back(0x00u);
            m_fifoOut.push_back(0x2Bu);
            m_fifoOut.push_back(mc.terminator);
            break;
        case 0x28: // get terminator
            m_fifoOut.push_back(0x2Bu);
            m_fifoOut.push_back(mc.terminator);
            m_fifoOut.push_back(mc.terminator);
            break;
        case 0x42: // write data
        {
            m_fifoOut.push_back(0x00u);
            m_fifoOut.push_back(0x2Bu);
            const uint8_t length = pop();
            std::vector<uint8_t> data(length);
            uint8_t checksum = 0;
            for (uint8_t &value : data)
            {
                value = pop();
                checksum ^= value;
                m_fifoOut.push_back(0x00u);
            }
            mc.write(data.data(), data.size());
            m_fifoOut.push_back(checksum);
            m_fifoOut.push_back(mc.terminator);
            break;
        }
        case 0x43: // read data
        {
            const uint8_t length = pop();
            m_fifoOut.push_back(0x00u);
            m_fifoOut.push_back(0x2Bu);
            std::vector<uint8_t> data(length);
            mc.read(data.data(), data.size());
            uint8_t checksum = 0;
            for (const uint8_t value : data)
            {
                checksum ^= value;
                m_fifoOut.push_back(value);
            }
            m_fifoOut.push_back(checksum);
            m_fifoOut.push_back(mc.terminator);
            break;
        }
        case 0x82: // erase block
            mc.eraseBlock();
            finish(4);
            break;
        case 0xBF:
            finish(5);
            break;
        case 0xF0: // MagicGate authentication; the card side only echoes or XORs
        {
            const uint8_t step = pop();
            switch (step)
            {
            case 0x01: case 0x02: case 0x04: case 0x0F: case 0x11: case 0x13:
            {
                m_fifoOut.push_back(0x00u);
                m_fifoOut.push_back(0x2Bu);
                uint8_t checksum = 0;
                for (int i = 0; i < 8; ++i)
                {
                    checksum ^= pop();
                    m_fifoOut.push_back(0x00u);
                }
                m_fifoOut.push_back(checksum);
                m_fifoOut.push_back(mc.terminator);
                break;
            }
            case 0x06: case 0x07: case 0x0B:
                finish(14);
                break;
            default:
                finish(5);
                break;
            }
            break;
        }
        case 0xF3: // reset authentication
            mc.terminator = 0x55;
            finish(5);
            break;
        case 0xF7:
            finish(5);
            break;
        default:
            break;
        }
    }
}
