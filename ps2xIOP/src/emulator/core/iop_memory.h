#pragma once

#include "iop_sio2.h"
#include "iop_spu2.h"

#include <deque>
#include <string>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace ps2x::iop::detail
{
    class IopMemory
    {
    public:
        static constexpr uint32_t RamSize = 2u * 1024u * 1024u;
        static constexpr uint32_t PhysicalMask = 0x1FFFFFFFu;
        static constexpr uint32_t ScratchBase = 0x1F800000u;
        static constexpr uint32_t ScratchSize = 0x400u;
        static constexpr uint32_t HardwareBase = 0x1F801000u;
        static constexpr uint32_t HardwareEnd = 0x1F900000u;
        static constexpr uint32_t Spu2Base = 0x1F900000u;
        static constexpr uint32_t Spu2End = 0x1FA00000u;
        static constexpr uint32_t SifBase = 0x1D000000u;
        static constexpr uint32_t SifEnd = 0x1D001000u;
        // Modules and heap blocks share one arena, as under sysmem; a fixed split starved games that load
        // few modules but allocate large IOP buffers (CRI PSJ asks for 0x41040 bytes after 13 IRX).
        static constexpr uint32_t HeapBase = 0x00010000u;
        static constexpr uint32_t HeapLimit = 0x001F0000u;

        struct Allocation
        {
            uint32_t address = 0;
            uint32_t size = 0;
        };

        struct DmaStart
        {
            int irq = 0;
            uint64_t delayCycles = 0;
        };

        IopMemory();

        void reset();

        // RAM accesses are inlined: the interpreter makes several per instruction and the call overhead dominated.
        [[nodiscard]] uint8_t read8(uint32_t address) const
        {
            const uint32_t phys = address & PhysicalMask;
            return phys < RamSize ? m_ram[phys] : read8Slow(address);
        }
        [[nodiscard]] uint16_t read16(uint32_t address) const
        {
            const uint32_t phys = address & PhysicalMask;
            if (phys + 1u < RamSize)
            {
                uint16_t value;
                std::memcpy(&value, m_ram.data() + phys, sizeof(value));
                return value;
            }
            return read16Slow(address);
        }
        [[nodiscard]] uint32_t read32(uint32_t address) const
        {
            const uint32_t phys = address & PhysicalMask;
            if ((phys & 3u) == 0u && phys + 3u < RamSize)
            {
                uint32_t value;
                std::memcpy(&value, m_ram.data() + phys, sizeof(value));
                return value;
            }
            return read32Slow(address);
        }
        void write8(uint32_t address, uint8_t value)
        {
            const uint32_t phys = address & PhysicalMask;
            if (phys < RamSize)
            {
                m_ram[phys] = value;
                m_owned[phys] = 1u;
                return;
            }
            write8Slow(address, value);
        }
        void write16(uint32_t address, uint16_t value)
        {
            const uint32_t phys = address & PhysicalMask;
            if (phys + 1u < RamSize)
            {
                std::memcpy(m_ram.data() + phys, &value, sizeof(value));
                m_owned[phys] = 1u;
                m_owned[phys + 1u] = 1u;
                return;
            }
            write16Slow(address, value);
        }
        void write32(uint32_t address, uint32_t value)
        {
            const uint32_t phys = address & PhysicalMask;
            if ((phys & 3u) == 0u && phys + 3u < RamSize)
            {
                std::memcpy(m_ram.data() + phys, &value, sizeof(value));
                std::memset(m_owned.data() + phys, 1, sizeof(value));
                return;
            }
            write32Slow(address, value);
        }

        [[nodiscard]] bool readRam(uint32_t address, void *destination, size_t size) const;
        [[nodiscard]] bool writeRam(uint32_t address, const void *source, size_t size);
        [[nodiscard]] bool zeroRam(uint32_t address, size_t size);
        [[nodiscard]] bool ownsRamRange(uint32_t address, size_t size) const;
        [[nodiscard]] bool isHardwareAddress(uint32_t address) const;
        [[nodiscard]] std::string readString(uint32_t address, size_t limit = 1024u) const;

        [[nodiscard]] uint32_t allocate(uint32_t size, uint32_t alignment = 16u, std::optional<uint32_t> fixed = std::nullopt);
        [[nodiscard]] bool freeAllocation(uint32_t address);
        [[nodiscard]] uint32_t maxFreeMemory() const;
        [[nodiscard]] std::optional<Allocation> allocationContaining(uint32_t address) const;

        [[nodiscard]] uint32_t interruptStatus() const noexcept { return m_interruptStatus; }
        [[nodiscard]] uint32_t interruptMask() const noexcept { return m_interruptMask; }
        [[nodiscard]] uint32_t interruptControl() const noexcept { return m_interruptControl; }
        void setInterruptStatus(uint32_t value) noexcept { m_interruptStatus = value; }
        void setInterruptMask(uint32_t value) noexcept { m_interruptMask = value; }
        void setInterruptControl(uint32_t value) noexcept { m_interruptControl = value & 1u; }

        [[nodiscard]] std::optional<DmaStart> takeDmaStart() noexcept;
        [[nodiscard]] bool hasDmaStart() const noexcept { return !m_dmaStarts.empty(); }
        // Mixes SPU2 output in emulated time and queues the IRQs and AutoDMA completions it caused.
        void mixSpu2(uint32_t frames, std::vector<int16_t> &out);
        [[nodiscard]] uint32_t spu2FramesUntilBlock() const noexcept { return m_spu2.framesUntilBlock(); }
        [[nodiscard]] IopSpu2 &spu2() noexcept { return m_spu2; }
        // Attaches a raw memory card image to a SIO2 port; a missing file is created blank on first access.
        bool attachMemoryCard(size_t port, const std::string &path);
        [[nodiscard]] std::span<const uint8_t> ram() const noexcept { return m_ram; }

        [[nodiscard]] static uint32_t physicalAddress(uint32_t address) noexcept;

    private:
        [[nodiscard]] uint8_t read8Slow(uint32_t address) const;
        [[nodiscard]] uint16_t read16Slow(uint32_t address) const;
        [[nodiscard]] uint32_t read32Slow(uint32_t address) const;
        void write8Slow(uint32_t address, uint8_t value);
        void write16Slow(uint32_t address, uint16_t value);
        void write32Slow(uint32_t address, uint32_t value);
        [[nodiscard]] uint32_t readHardware32(uint32_t address) const;
        void writeHardware32(uint32_t address, uint32_t value);
        void markOwned(uint32_t address, size_t size);
        void writeSio2(uint32_t address, uint32_t value);
        void runSio2Dma(uint32_t chcrAddress, uint32_t chcr);
        void queueInterrupt(int irq, uint64_t delayCycles);

        std::vector<uint8_t> m_ram;
        std::vector<uint8_t> m_owned;
        std::vector<uint8_t> m_scratch;
        std::unordered_map<uint32_t, uint32_t> m_hardware;
        std::vector<Allocation> m_allocations; // sorted by address
        uint32_t m_interruptStatus = 0;
        uint32_t m_interruptMask = 0;
        uint32_t m_interruptControl = 1;
        std::deque<DmaStart> m_dmaStarts;
        // SIO2 data reads pop its FIFO, so the controller changes even on const reads.
        mutable IopSio2 m_sio2;
        // IRQINFO and the STD data port change state on read.
        mutable IopSpu2 m_spu2;
    };
}
