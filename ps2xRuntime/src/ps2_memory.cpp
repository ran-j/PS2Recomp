#include "ps2_runtime.h"
#include <iostream>
#include <cstring>
#include <stdexcept>

constexpr uint32_t PS2_RAM_BASE = 0x00000000;
constexpr uint32_t PS2_RAM_SIZE = 32 * 1024 * 1024; // 32MB
constexpr uint32_t PS2_SCRATCHPAD_BASE = 0x70000000;
constexpr uint32_t PS2_SCRATCHPAD_SIZE = 16 * 1024; // 16KB
constexpr uint32_t PS2_IO_BASE = 0x10000000;
constexpr uint32_t PS2_IO_SIZE = 0x10000; // 64KB
constexpr uint32_t PS2_VU0_CODE_BASE = 0x11000000;
constexpr uint32_t PS2_VU0_DATA_BASE = 0x11004000;
constexpr uint32_t PS2_VU1_CODE_BASE = 0x11008000;
constexpr uint32_t PS2_VU1_DATA_BASE = 0x1100C000;
constexpr uint32_t PS2_GS_BASE = 0x12000000;

PS2Memory::PS2Memory()
    : m_rdram(nullptr), m_scratchpad(nullptr)
{
}

PS2Memory::~PS2Memory()
{
    if (m_rdram)
    {
        delete[] m_rdram;
        m_rdram = nullptr;
    }

    if (m_scratchpad)
    {
        delete[] m_scratchpad;
        m_scratchpad = nullptr;
    }
}

bool PS2Memory::initialize(size_t ramSize)
{
    try
    {
        // Allocate main RAM
        m_rdram = new uint8_t[ramSize];
        if (!m_rdram)
        {
            std::cerr << "Failed to allocate " << ramSize << " bytes for RDRAM" << std::endl;
            return false;
        }
        std::memset(m_rdram, 0, ramSize);

        // Allocate scratchpad
        m_scratchpad = new uint8_t[PS2_SCRATCHPAD_SIZE];
        if (!m_scratchpad)
        {
            std::cerr << "Failed to allocate " << PS2_SCRATCHPAD_SIZE << " bytes for scratchpad" << std::endl;
            delete[] m_rdram;
            m_rdram = nullptr;
            return false;
        }
        std::memset(m_scratchpad, 0, PS2_SCRATCHPAD_SIZE);

        // Initialize IO registers with default values
        m_ioRegisters.clear();

        // Initialize TLB entries
        m_tlbEntries.clear();

        return true;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error initializing PS2 memory: " << e.what() << std::endl;
        return false;
    }
}

uint32_t PS2Memory::translateAddress(uint32_t virtualAddress)
{
    // Handle special memory regions
    if (virtualAddress >= PS2_SCRATCHPAD_BASE && virtualAddress < PS2_SCRATCHPAD_BASE + PS2_SCRATCHPAD_SIZE)
    {
        // Scratchpad is directly mapped
        return virtualAddress - PS2_SCRATCHPAD_BASE;
    }

    // For RDRAM, mask the address to get the physical address
    if (virtualAddress < PS2_RAM_SIZE ||
        (virtualAddress >= 0x80000000 && virtualAddress < 0x80000000 + PS2_RAM_SIZE))
    {
        // KSEG0 is directly mapped, just mask out the high bits
        return virtualAddress & 0x1FFFFFFF;
    }

    // For addresses that need TLB lookup
    if (virtualAddress >= 0xC0000000)
    {
        for (const auto &entry : m_tlbEntries)
        {
            if (entry.valid)
            {
                uint32_t vpn_masked = (virtualAddress >> 12) & ~entry.mask;
                uint32_t entry_vpn_masked = entry.vpn & ~entry.mask;

                if (vpn_masked == entry_vpn_masked)
                {
                    // TLB hit
                    uint32_t offset = virtualAddress & 0xFFF; // Page offset
                    uint32_t page = entry.pfn | (virtualAddress & entry.mask);
                    return (page << 12) | offset;
                }
            }
        }
        // TLB miss
        throw std::runtime_error("TLB miss for address: 0x" + std::to_string(virtualAddress));
    }

    // Default to simple masking for other addresses
    return virtualAddress & 0x1FFFFFFF;
}

uint8_t PS2Memory::read8(uint32_t address)
{
    uint32_t physAddr = translateAddress(address);

    if (physAddr < PS2_RAM_SIZE)
    {
        return m_rdram[physAddr];
    }
    else if (physAddr >= PS2_SCRATCHPAD_BASE && physAddr < PS2_SCRATCHPAD_BASE + PS2_SCRATCHPAD_SIZE)
    {
        return m_scratchpad[physAddr - PS2_SCRATCHPAD_BASE];
    }
    else if (physAddr >= PS2_IO_BASE && physAddr < PS2_IO_BASE + PS2_IO_SIZE)
    {
        // IO registers - often not handled byte by byte
        uint32_t regAddr = physAddr & ~0x3; // Align to word boundary
        if (m_ioRegisters.find(regAddr) != m_ioRegisters.end())
        {
            uint32_t value = m_ioRegisters[regAddr];
            uint32_t shift = (physAddr & 3) * 8;
            return (value >> shift) & 0xFF;
        }
        return 0; // Unimplemented IO register
    }

    // Handle other memory regions ,for now return 0 for unimplemented regions
    return 0;
}

uint16_t PS2Memory::read16(uint32_t address)
{
    // Check alignment
    if (address & 1)
    {
        throw std::runtime_error("Unaligned 16-bit read at address: 0x" + std::to_string(address));
    }

    uint32_t physAddr = translateAddress(address);

    if (physAddr < PS2_RAM_SIZE)
    {
        return *reinterpret_cast<uint16_t *>(&m_rdram[physAddr]);
    }
    else if (physAddr >= PS2_SCRATCHPAD_BASE && physAddr < PS2_SCRATCHPAD_BASE + PS2_SCRATCHPAD_SIZE)
    {
        return *reinterpret_cast<uint16_t *>(&m_scratchpad[physAddr - PS2_SCRATCHPAD_BASE]);
    }
    else if (physAddr >= PS2_IO_BASE && physAddr < PS2_IO_BASE + PS2_IO_SIZE)
    {
        // IO registers - align to word boundary and extract relevant bits
        uint32_t regAddr = physAddr & ~0x3;
        if (m_ioRegisters.find(regAddr) != m_ioRegisters.end())
        {
            uint32_t value = m_ioRegisters[regAddr];
            uint32_t shift = (physAddr & 2) * 8;
            return (value >> shift) & 0xFFFF;
        }
        return 0; // Unimplemented IO register
    }

    return 0;
}

uint32_t PS2Memory::read32(uint32_t address)
{
    // Check alignment
    if (address & 3)
    {
        throw std::runtime_error("Unaligned 32-bit read at address: 0x" + std::to_string(address));
    }

    uint32_t physAddr = translateAddress(address);

    if (physAddr < PS2_RAM_SIZE)
    {
        return *reinterpret_cast<uint32_t *>(&m_rdram[physAddr]);
    }
    else if (physAddr >= PS2_SCRATCHPAD_BASE && physAddr < PS2_SCRATCHPAD_BASE + PS2_SCRATCHPAD_SIZE)
    {
        return *reinterpret_cast<uint32_t *>(&m_scratchpad[physAddr - PS2_SCRATCHPAD_BASE]);
    }
    else if (physAddr >= PS2_IO_BASE && physAddr < PS2_IO_BASE + PS2_IO_SIZE)
    {
        // IO registers
        if (m_ioRegisters.find(physAddr) != m_ioRegisters.end())
        {
            return m_ioRegisters[physAddr];
        }
        return 0; // Unimplemented IO register
    }

    return 0;
}

uint64_t PS2Memory::read64(uint32_t address)
{
    // Check alignment
    if (address & 7)
    {
        throw std::runtime_error("Unaligned 64-bit read at address: 0x" + std::to_string(address));
    }

    uint32_t physAddr = translateAddress(address);

    if (physAddr < PS2_RAM_SIZE)
    {
        return *reinterpret_cast<uint64_t *>(&m_rdram[physAddr]);
    }
    else if (physAddr >= PS2_SCRATCHPAD_BASE && physAddr < PS2_SCRATCHPAD_BASE + PS2_SCRATCHPAD_SIZE)
    {
        return *reinterpret_cast<uint64_t *>(&m_scratchpad[physAddr - PS2_SCRATCHPAD_BASE]);
    }

    // 64-bit IO operations are not common, but who knows
    return (uint64_t)read32(address) | ((uint64_t)read32(address + 4) << 32);
}

__m128i PS2Memory::read128(uint32_t address)
{
    // Check alignment
    if (address & 15)
    {
        throw std::runtime_error("Unaligned 128-bit read at address: 0x" + std::to_string(address));
    }

    uint32_t physAddr = translateAddress(address);

    if (physAddr < PS2_RAM_SIZE)
    {
        return _mm_loadu_si128(reinterpret_cast<__m128i *>(&m_rdram[physAddr]));
    }
    else if (physAddr >= PS2_SCRATCHPAD_BASE && physAddr < PS2_SCRATCHPAD_BASE + PS2_SCRATCHPAD_SIZE)
    {
        return _mm_loadu_si128(reinterpret_cast<__m128i *>(&m_scratchpad[physAddr - PS2_SCRATCHPAD_BASE]));
    }

    // 128-bit reads are primarily for quad-word loads in the EE, which are only valid for RAM areas
    // Return zeroes for unsupported areas
    return _mm_setzero_si128();
}
 
void PS2Memory::write8(uint32_t address, uint8_t value)
{
    uint32_t physAddr = translateAddress(address);

    if (physAddr < PS2_RAM_SIZE)
    {
        m_rdram[physAddr] = value;
    }
    else if (physAddr >= PS2_SCRATCHPAD_BASE && physAddr < PS2_SCRATCHPAD_BASE + PS2_SCRATCHPAD_SIZE)
    {
        m_scratchpad[physAddr - PS2_SCRATCHPAD_BASE] = value;
    }
    else if (physAddr >= PS2_IO_BASE && physAddr < PS2_IO_BASE + PS2_IO_SIZE)
    {
        // IO registers - handle byte writes by modifying the appropriate byte in the word
        uint32_t regAddr = physAddr & ~0x3;
        uint32_t shift = (physAddr & 3) * 8;
        uint32_t mask = ~(0xFF << shift);
        uint32_t newValue = (m_ioRegisters[regAddr] & mask) | ((uint32_t)value << shift);
        m_ioRegisters[regAddr] = newValue;

        // Handle potential side effects of IO register writes 
    }
}

void PS2Memory::write16(uint32_t address, uint16_t value)
{
    // Check alignment
    if (address & 1)
    {
        throw std::runtime_error("Unaligned 16-bit write at address: 0x" + std::to_string(address));
    }

    uint32_t physAddr = translateAddress(address);

    if (physAddr < PS2_RAM_SIZE)
    {
        *reinterpret_cast<uint16_t *>(&m_rdram[physAddr]) = value;
    }
    else if (physAddr >= PS2_SCRATCHPAD_BASE && physAddr < PS2_SCRATCHPAD_BASE + PS2_SCRATCHPAD_SIZE)
    {
        *reinterpret_cast<uint16_t *>(&m_scratchpad[physAddr - PS2_SCRATCHPAD_BASE]) = value;
    }
    else if (physAddr >= PS2_IO_BASE && physAddr < PS2_IO_BASE + PS2_IO_SIZE)
    {
        // IO registers - handle halfword writes
        uint32_t regAddr = physAddr & ~0x3;
        uint32_t shift = (physAddr & 2) * 8;
        uint32_t mask = ~(0xFFFF << shift);
        uint32_t newValue = (m_ioRegisters[regAddr] & mask) | ((uint32_t)value << shift);
        m_ioRegisters[regAddr] = newValue;

        // Handle potential side effects of IO register writes
    }
}

void PS2Memory::write32(uint32_t address, uint32_t value)
{
    // Check alignment
    if (address & 3)
    {
        throw std::runtime_error("Unaligned 32-bit write at address: 0x" + std::to_string(address));
    }

    uint32_t physAddr = translateAddress(address);

    if (physAddr < PS2_RAM_SIZE)
    {
        *reinterpret_cast<uint32_t *>(&m_rdram[physAddr]) = value;
    }
    else if (physAddr >= PS2_SCRATCHPAD_BASE && physAddr < PS2_SCRATCHPAD_BASE + PS2_SCRATCHPAD_SIZE)
    {
        *reinterpret_cast<uint32_t *>(&m_scratchpad[physAddr - PS2_SCRATCHPAD_BASE]) = value;
    }
    else if (physAddr >= PS2_IO_BASE && physAddr < PS2_IO_BASE + PS2_IO_SIZE)
    {
        // IO registers
        m_ioRegisters[physAddr] = value;

        // Handle potential side effects of IO register writes
        // This would be where we handle the various hardware effects
        // For example, writing to a DMA control register might trigger a transfer
    }
}

void PS2Memory::write64(uint32_t address, uint64_t value)
{
    // Check alignment
    if (address & 7)
    {
        throw std::runtime_error("Unaligned 64-bit write at address: 0x" + std::to_string(address));
    }

    uint32_t physAddr = translateAddress(address);

    if (physAddr < PS2_RAM_SIZE)
    {
        *reinterpret_cast<uint64_t *>(&m_rdram[physAddr]) = value;
    }
    else if (physAddr >= PS2_SCRATCHPAD_BASE && physAddr < PS2_SCRATCHPAD_BASE + PS2_SCRATCHPAD_SIZE)
    {
        *reinterpret_cast<uint64_t *>(&m_scratchpad[physAddr - PS2_SCRATCHPAD_BASE]) = value;
    }
    else
    {
        // Split into two 32-bit writes for other memory regions
        write32(address, (uint32_t)value);
        write32(address + 4, (uint32_t)(value >> 32));
    }
}

void PS2Memory::write128(uint32_t address, __m128i value)
{
    // Check alignment
    if (address & 15)
    {
        throw std::runtime_error("Unaligned 128-bit write at address: 0x" + std::to_string(address));
    }

    uint32_t physAddr = translateAddress(address);

    if (physAddr < PS2_RAM_SIZE)
    {
        _mm_storeu_si128(reinterpret_cast<__m128i *>(&m_rdram[physAddr]), value);
    }
    else if (physAddr >= PS2_SCRATCHPAD_BASE && physAddr < PS2_SCRATCHPAD_BASE + PS2_SCRATCHPAD_SIZE)
    {
        _mm_storeu_si128(reinterpret_cast<__m128i *>(&m_scratchpad[physAddr - PS2_SCRATCHPAD_BASE]), value);
    }
    else
    {
        // Split into smaller writes for other memory regions
        // Extract the data using SSE intrinsics
        uint64_t lo = _mm_extract_epi64(value, 0);
        uint64_t hi = _mm_extract_epi64(value, 1);

        write64(address, lo);
        write64(address + 8, hi);
    }
}