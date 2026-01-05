#include "ps2_runtime.h"
#include <iostream>
#include <cstring>
#include <stdexcept>
#include <unordered_map>

namespace
{
    inline bool isGsPrivReg(uint32_t addr)
    {
        return addr >= PS2_GS_PRIV_REG_BASE && addr < PS2_GS_PRIV_REG_BASE + PS2_GS_PRIV_REG_SIZE;
    }

    inline uint64_t *gsRegPtr(GSRegisters &gs, uint32_t addr)
    {
        uint32_t off = addr - PS2_GS_PRIV_REG_BASE;
        switch (off)
        {
        case 0x0000:
            return &gs.pmode;
        case 0x0010:
            return &gs.smode1;
        case 0x0020:
            return &gs.smode2;
        case 0x0030:
            return &gs.srfsh;
        case 0x0040:
            return &gs.synch1;
        case 0x0050:
            return &gs.synch2;
        case 0x0060:
            return &gs.syncv;
        case 0x0070:
            return &gs.dispfb1;
        case 0x0080:
            return &gs.display1;
        case 0x0090:
            return &gs.dispfb2;
        case 0x00A0:
            return &gs.display2;
        case 0x00B0:
            return &gs.extbuf;
        case 0x00C0:
            return &gs.extdata;
        case 0x00D0:
            return &gs.extwrite;
        case 0x00E0:
            return &gs.bgcolor;
        case 0x1000:
            return &gs.csr;
        case 0x1010:
            return &gs.imr;
        case 0x1040:
            return &gs.busdir;
        case 0x1080:
            return &gs.siglblid;
        default:
            return nullptr;
        }
    }

    inline void logGsWrite(uint32_t addr, uint64_t value)
    {
        static std::unordered_map<uint32_t, int> logCount;
        int &count = logCount[addr];
        if (count < 10)
        {
            std::cout << "[GS] write 0x" << std::hex << addr << " = 0x" << value << std::dec << std::endl;
        }
        ++count;
    }

    constexpr uint32_t kSchedulerBase = 0x00363a10;
    constexpr uint32_t kSchedulerSpan = 0x00000420;
    static int g_schedWriteLogCount = 0;

    inline void logSchedulerWrite(uint32_t physAddr, uint32_t size, uint64_t value)
    {
        if (physAddr < kSchedulerBase || physAddr >= kSchedulerBase + kSchedulerSpan)
        {
            return;
        }
        if (g_schedWriteLogCount >= 64)
        {
            return;
        }
        std::cout << "[sched write" << size << "] addr=0x" << std::hex << physAddr
                  << " val=0x" << value << std::dec << std::endl;
        ++g_schedWriteLogCount;
    }
}

// Helpers for GS VRAM addressing (PSMCT32 only in this minimal path).
static inline uint32_t gs_vram_offset(uint32_t basePage, uint32_t x, uint32_t y, uint32_t fbw)
{
    // basePage is in 2048-byte units; fbw is in blocks of 64 pixels.
    uint32_t strideBytes = fbw * 64 * 4;
    return basePage * 2048 + y * strideBytes + x * 4;
}

PS2Memory::PS2Memory()
    : m_rdram(nullptr), m_scratchpad(nullptr), m_gsVRAM(nullptr), m_seenGifCopy(false)
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

    if (m_gsVRAM)
    {
        delete[] m_gsVRAM;
        m_gsVRAM = nullptr;
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

        // Initialize TLB entries
        m_tlbEntries.clear();

        // Allocate IOP RAM
        iop_ram = new uint8_t[2 * 1024 * 1024]; // 2MB
        if (!iop_ram)
        {
            delete[] m_rdram;
            delete[] m_scratchpad;
            m_rdram = nullptr;
            m_scratchpad = nullptr;
            return false;
        }

        // Initialize IOP RAM with zeros
        std::memset(iop_ram, 0, 2 * 1024 * 1024);

        // Initialize I/O registers
        m_ioRegisters.clear();

        // Initialize GS registers
        memset(&gs_regs, 0, sizeof(gs_regs));

        // Allocate GS VRAM (4MB)
        m_gsVRAM = new uint8_t[PS2_GS_VRAM_SIZE];
        if (!m_gsVRAM)
        {
            delete[] m_rdram;
            delete[] m_scratchpad;
            delete[] iop_ram;
            m_rdram = nullptr;
            m_scratchpad = nullptr;
            iop_ram = nullptr;
            return false;
        }
        std::memset(m_gsVRAM, 0, PS2_GS_VRAM_SIZE);

        // Initialize VIF registers
        memset(&vif0_regs, 0, sizeof(vif0_regs));
        memset(&vif1_regs, 0, sizeof(vif1_regs));

        // Initialize DMA registers
        memset(dma_regs, 0, sizeof(dma_regs));

        return true;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error initializing PS2 memory: " << e.what() << std::endl;
        return false;
    }
}

bool PS2Memory::isScratchpad(uint32_t address) const
{
    return address >= PS2_SCRATCHPAD_BASE &&
           address < PS2_SCRATCHPAD_BASE + PS2_SCRATCHPAD_SIZE;
}

uint32_t PS2Memory::translateAddress(uint32_t virtualAddress)
{
    if (isScratchpad(virtualAddress))
    {
        return virtualAddress - PS2_SCRATCHPAD_BASE;
    }

    if (virtualAddress < PS2_RAM_SIZE ||
        (virtualAddress >= 0x80000000 && virtualAddress < 0x80000000 + PS2_RAM_SIZE))
    {
        return virtualAddress & 0x1FFFFFFF;
    }

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
        throw std::runtime_error("TLB miss for address: 0x" + std::to_string(virtualAddress));
    }

    return virtualAddress & 0x1FFFFFFF;
}

uint8_t PS2Memory::read8(uint32_t address)
{
    const bool scratch = isScratchpad(address);
    uint32_t physAddr = translateAddress(address);

    if (scratch)
    {
        return m_scratchpad[physAddr];
    }
    if (physAddr < PS2_RAM_SIZE)
    {
        return m_rdram[physAddr];
    }
    else if (physAddr >= PS2_IO_BASE && physAddr < PS2_IO_BASE + PS2_IO_SIZE)
    {
        uint32_t regAddr = physAddr & ~0x3;
        if (m_ioRegisters.find(regAddr) != m_ioRegisters.end())
        {
            uint32_t value = m_ioRegisters[regAddr];
            uint32_t shift = (physAddr & 3) * 8;
            return (value >> shift) & 0xFF;
        }
        return 0;
    }

    // TODO: Handle other memory regions
    return 0;
}

uint16_t PS2Memory::read16(uint32_t address)
{
    if (address & 1)
    {
        throw std::runtime_error("Unaligned 16-bit read at address: 0x" + std::to_string(address));
    }

    const bool scratch = isScratchpad(address);
    uint32_t physAddr = translateAddress(address);

    if (scratch)
    {
        return *reinterpret_cast<uint16_t *>(&m_scratchpad[physAddr]);
    }
    if (physAddr < PS2_RAM_SIZE)
    {
        return *reinterpret_cast<uint16_t *>(&m_rdram[physAddr]);
    }
    else if (physAddr >= PS2_IO_BASE && physAddr < PS2_IO_BASE + PS2_IO_SIZE)
    {
        uint32_t regAddr = physAddr & ~0x3;
        if (m_ioRegisters.find(regAddr) != m_ioRegisters.end())
        {
            uint32_t value = m_ioRegisters[regAddr];
            uint32_t shift = (physAddr & 2) * 8;
            return (value >> shift) & 0xFFFF;
        }
        return 0;
    }

    return 0;
}

uint32_t PS2Memory::read32(uint32_t address)
{
    if (address & 3)
    {
        throw std::runtime_error("Unaligned 32-bit read at address: 0x" + std::to_string(address));
    }

    if (isGsPrivReg(address))
    {
        uint64_t *reg = gsRegPtr(gs_regs, address);
        uint32_t off = address & 7;
        uint64_t val = reg ? *reg : 0;
        return (uint32_t)(val >> (off * 8));
    }

    const bool scratch = isScratchpad(address);
    uint32_t physAddr = translateAddress(address);

    if (scratch)
    {
        return *reinterpret_cast<uint32_t *>(&m_scratchpad[physAddr]);
    }
    if (physAddr < PS2_RAM_SIZE)
    {
        return *reinterpret_cast<uint32_t *>(&m_rdram[physAddr]);
    }
    else if (physAddr >= PS2_IO_BASE && physAddr < PS2_IO_BASE + PS2_IO_SIZE)
    {
        if (m_ioRegisters.find(physAddr) != m_ioRegisters.end())
        {
            return m_ioRegisters[physAddr];
        }
        return 0;
    }

    return 0;
}

uint64_t PS2Memory::read64(uint32_t address)
{
    if (address & 7)
    {
        throw std::runtime_error("Unaligned 64-bit read at address: 0x" + std::to_string(address));
    }

    if (isGsPrivReg(address))
    {
        uint64_t *reg = gsRegPtr(gs_regs, address);
        return reg ? *reg : 0;
    }

    const bool scratch = isScratchpad(address);
    uint32_t physAddr = translateAddress(address);

    if (scratch)
    {
        return *reinterpret_cast<uint64_t *>(&m_scratchpad[physAddr]);
    }
    if (physAddr < PS2_RAM_SIZE)
    {
        return *reinterpret_cast<uint64_t *>(&m_rdram[physAddr]);
    }

    // 64-bit IO operations are not common, but who knows
    return (uint64_t)read32(address) | ((uint64_t)read32(address + 4) << 32);
}

__m128i PS2Memory::read128(uint32_t address)
{
    if (address & 15)
    {
        throw std::runtime_error("Unaligned 128-bit read at address: 0x" + std::to_string(address));
    }

    const bool scratch = isScratchpad(address);
    uint32_t physAddr = translateAddress(address);

    if (scratch)
    {
        return _mm_loadu_si128(reinterpret_cast<__m128i *>(&m_scratchpad[physAddr]));
    }
    if (physAddr < PS2_RAM_SIZE)
    {
        return _mm_loadu_si128(reinterpret_cast<__m128i *>(&m_rdram[physAddr]));
    }

    // 128-bit reads are primarily for quad-word loads in the EE, which are only valid for RAM areas
    // Return zeroes for unsupported areas
    return _mm_setzero_si128();
}

void PS2Memory::write8(uint32_t address, uint8_t value)
{
    const bool scratch = isScratchpad(address);
    uint32_t physAddr = translateAddress(address);

    if (scratch)
    {
        m_scratchpad[physAddr] = value;
    }
    else if (physAddr < PS2_RAM_SIZE)
    {
        m_rdram[physAddr] = value;
        logSchedulerWrite(physAddr, 8, value);
    }
    else if (physAddr >= PS2_IO_BASE && physAddr < PS2_IO_BASE + PS2_IO_SIZE)
    {
        // IO registers - handle byte writes by modifying the appropriate byte in the word
        uint32_t regAddr = physAddr & ~0x3;
        uint32_t shift = (physAddr & 3) * 8;
        uint32_t mask = ~(0xFF << shift);
        uint32_t newValue = (m_ioRegisters[regAddr] & mask) | ((uint32_t)value << shift);
        m_ioRegisters[regAddr] = newValue;

        // TODO: Handle potential side effects of IO register writes
    }
}

void PS2Memory::write16(uint32_t address, uint16_t value)
{
    if (address & 1)
    {
        throw std::runtime_error("Unaligned 16-bit write at address: 0x" + std::to_string(address));
    }

    const bool scratch = isScratchpad(address);
    uint32_t physAddr = translateAddress(address);

    if (scratch)
    {
        *reinterpret_cast<uint16_t *>(&m_scratchpad[physAddr]) = value;
    }
    else if (physAddr < PS2_RAM_SIZE)
    {
        *reinterpret_cast<uint16_t *>(&m_rdram[physAddr]) = value;
        logSchedulerWrite(physAddr, 16, value);
    }
    else if (physAddr >= PS2_IO_BASE && physAddr < PS2_IO_BASE + PS2_IO_SIZE)
    {
        uint32_t regAddr = physAddr & ~0x3;
        uint32_t shift = (physAddr & 2) * 8;
        uint32_t mask = ~(0xFFFF << shift);
        uint32_t newValue = (m_ioRegisters[regAddr] & mask) | ((uint32_t)value << shift);
        m_ioRegisters[regAddr] = newValue;

        // TODO: Handle potential side effects of IO register writes
    }
}

void PS2Memory::write32(uint32_t address, uint32_t value)
{
    if (address & 3)
    {
        throw std::runtime_error("Unaligned 32-bit write at address: 0x" + std::to_string(address));
    }

    if (isGsPrivReg(address))
    {
        uint64_t *reg = gsRegPtr(gs_regs, address);
        if (reg)
        {
            uint32_t off = address & 7;
            uint64_t mask = 0xFFFFFFFFULL << (off * 8);
            uint64_t newVal = (*reg & ~mask) | ((uint64_t)value << (off * 8));
            *reg = newVal;
            logGsWrite(address, newVal);
        }
        return;
    }

    const bool scratch = isScratchpad(address);
    uint32_t physAddr = translateAddress(address);

    if (scratch)
    {
        *reinterpret_cast<uint32_t *>(&m_scratchpad[physAddr]) = value;
    }
    else if (physAddr < PS2_RAM_SIZE)
    {
        // Check if this might be code modification
        markModified(address, 4);

        *reinterpret_cast<uint32_t *>(&m_rdram[physAddr]) = value;
        logSchedulerWrite(physAddr, 32, value);
    }
    else if (physAddr >= PS2_IO_BASE && physAddr < PS2_IO_BASE + PS2_IO_SIZE)
    {
        static int ioLogCount = 0;
        if (ioLogCount < 64)
        {
            std::cout << "[IO write32] addr=0x" << std::hex << physAddr << " val=0x" << value << std::dec << std::endl;
            ++ioLogCount;
        }
        // Handle IO register writes with potential side effects
        writeIORegister(physAddr, value);
    }
}

void PS2Memory::write64(uint32_t address, uint64_t value)
{
    if (address & 7)
    {
        throw std::runtime_error("Unaligned 64-bit write at address: 0x" + std::to_string(address));
    }

    if (isGsPrivReg(address))
    {
        uint64_t *reg = gsRegPtr(gs_regs, address);
        if (reg)
        {
            *reg = value;
            logGsWrite(address, value);
        }
        return;
    }

    const bool scratch = isScratchpad(address);
    uint32_t physAddr = translateAddress(address);

    if (scratch)
    {
        *reinterpret_cast<uint64_t *>(&m_scratchpad[physAddr]) = value;
    }
    else if (physAddr < PS2_RAM_SIZE)
    {
        *reinterpret_cast<uint64_t *>(&m_rdram[physAddr]) = value;
        logSchedulerWrite(physAddr, 64, value);
    }
    else
    {
        write32(address, (uint32_t)value);
        write32(address + 4, (uint32_t)(value >> 32));
    }
}

void PS2Memory::write128(uint32_t address, __m128i value)
{
    if (address & 15)
    {
        throw std::runtime_error("Unaligned 128-bit write at address: 0x" + std::to_string(address));
    }

    const bool scratch = isScratchpad(address);
    uint32_t physAddr = translateAddress(address);

    if (scratch)
    {
        _mm_storeu_si128(reinterpret_cast<__m128i *>(&m_scratchpad[physAddr]), value);
    }
    else if (physAddr < PS2_RAM_SIZE)
    {
        _mm_storeu_si128(reinterpret_cast<__m128i *>(&m_rdram[physAddr]), value);
    }
    else if (physAddr < PS2_GS_VRAM_SIZE)
    {
        _mm_storeu_si128(reinterpret_cast<__m128i *>(&m_gsVRAM[physAddr]), value);
    }
    else
    {
        uint64_t lo = _mm_extract_epi64(value, 0);
        uint64_t hi = _mm_extract_epi64(value, 1);

        write64(address, lo);
        write64(address + 8, hi);
    }
}

bool PS2Memory::writeIORegister(uint32_t address, uint32_t value)
{
    if (address >= 0x10008000 && address < 0x1000F000)
    {
        static int dmaLogCount = 0;
        if (dmaLogCount < 100)
        {
            uint32_t channelBase = address & 0xFFFFFF00;
            uint32_t offset = address & 0xFF;
            std::cout << "[DMA reg] ch=0x" << std::hex << channelBase
                      << " off=0x" << offset << " = 0x" << value << std::dec << std::endl;
            dmaLogCount++;
            if (offset == 0x00 && (value & 0x100))
            {
                uint32_t madr = m_ioRegisters[channelBase + 0x10];
                uint32_t qwc = m_ioRegisters[channelBase + 0x20];
                uint32_t tadr = m_ioRegisters[channelBase + 0x30];
                std::cout << "[DMA start] ch=0x" << std::hex << channelBase
                          << " madr=0x" << madr << " qwc=0x" << qwc
                          << " tadr=0x" << tadr << std::dec << std::endl;
                m_dmaStartCount.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
    m_ioRegisters[address] = value;

    if (address >= 0x10000000 && address < 0x10010000)
    {
        // Timer/counter registers
        if (address >= 0x10000000 && address < 0x10000100)
        {
            std::cout << "Timer register write: " << std::hex << address << " = " << value << std::dec << std::endl;
            return true;
        }

        // VIF0/VIF1 registers
        if (address >= 0x10003800 && address < 0x10003A00)
        {
            static int vif0Log = 0;
            if (vif0Log < 50)
            {
                std::cout << "[VIF0] write 0x" << std::hex << address << " = 0x" << value << std::dec << std::endl;
                ++vif0Log;
            }
            m_vifWriteCount.fetch_add(1, std::memory_order_relaxed);
        }
        if (address >= 0x10003C00 && address < 0x10003E00)
        {
            static int vif1Log = 0;
            if (vif1Log < 50)
            {
                std::cout << "[VIF1] write 0x" << std::hex << address << " = 0x" << value << std::dec << std::endl;
                ++vif1Log;
            }
            m_vifWriteCount.fetch_add(1, std::memory_order_relaxed);
        }

        // DMA registers
        if (address >= 0x10008000 && address < 0x1000F000)
        {
            std::cout << "DMA register write: " << std::hex << address << " = " << value << std::dec << std::endl;

            // Dump current DMA regs for all channels
            static bool dumpedDma = false;
            if (!dumpedDma)
            {
                for (int ch = 0; ch < 10; ++ch)
                {
                    uint32_t base = 0x10008000 + ch * 0x100;
                    uint32_t chcr_v = m_ioRegisters[base + 0x00];
                    uint32_t madr_v = m_ioRegisters[base + 0x10];
                    uint32_t qwc_v = m_ioRegisters[base + 0x20];
                    uint32_t tadr_v = m_ioRegisters[base + 0x30];
                    std::cout << "[DMA dump] ch" << ch
                              << " chcr=0x" << std::hex << chcr_v
                              << " madr=0x" << madr_v
                              << " qwc=0x" << qwc_v
                              << " tadr=0x" << tadr_v << std::dec << std::endl;
                }
                dumpedDma = true;
            }

            if ((address & 0xFF) == 0x00)
            { // CHCR registers
                if (value & 0x100)
                {
                    uint32_t channelBase = address & 0xFFFFFF00;
                    uint32_t madr = m_ioRegisters[channelBase + 0x10]; // Memory address
                    uint32_t qwc = m_ioRegisters[channelBase + 0x20];  // Quadword count

                    std::cout << "Starting DMA transfer on channel " << ((address >> 8) & 0xF)
                              << ", MADR: " << std::hex << madr
                              << ", QWC: " << qwc << std::dec << std::endl;

                    // Minimal GIF (channel 2) and VIF1 (channel 1) image transfer: copy from EE memory to GS VRAM.
                    // Only handles simple linear IMAGE transfers; treats destination as current DISPFBUF1 FBP.
                    if ((channelBase == 0x1000A000 || channelBase == 0x10009000) && m_gsVRAM)
                    {
                        auto doCopy = [&](uint32_t srcAddr, uint32_t qwCount)
                        {
                            uint32_t bytes = qwCount * 16;
                            uint32_t src = translateAddress(srcAddr);
                            uint32_t basePage = static_cast<uint32_t>(gs_regs.dispfb1 & 0x1FF);
                            uint32_t dest = basePage * 2048;
                            std::cout << "[GIF] ch=" << ((channelBase == 0x1000A000) ? 2 : 1)
                                      << " IMAGE copy bytes=" << bytes
                                      << " src=0x" << std::hex << srcAddr
                                      << " (phys 0x" << src << ")"
                                      << " dest=0x" << dest << std::dec << std::endl;
                            if (dest + bytes > PS2_GS_VRAM_SIZE)
                            {
                                bytes = std::min<uint32_t>(bytes, PS2_GS_VRAM_SIZE - dest);
                            }
                            if (src + bytes > PS2_RAM_SIZE)
                            {
                                bytes = std::min<uint32_t>(bytes, PS2_RAM_SIZE - src);
                            }
                            std::memcpy(m_gsVRAM + dest, m_rdram + src, bytes);
                            m_seenGifCopy = true;
                            m_gifCopyCount.fetch_add(1, std::memory_order_relaxed);
                        };

                        // Dump GIF tag/header
                        uint32_t phys = translateAddress(madr);
                        if (phys + 16 <= PS2_RAM_SIZE)
                        {
                            const uint8_t *p = m_rdram + phys;
                            uint64_t tag0 = *reinterpret_cast<const uint64_t *>(p + 0);
                            uint64_t tag1 = *reinterpret_cast<const uint64_t *>(p + 8);
                            std::cout << "[GIF] tag0=0x" << std::hex << tag0 << " tag1=0x" << tag1 << std::dec << std::endl;
                        }

                        if (qwc > 0)
                        {
                            doCopy(madr, qwc);
                        }
                        else
                        {
                            // Simple DMA chain walker for one tag from TADR (REF/NEXT).
                            uint32_t tadr = m_ioRegisters[channelBase + 0x30];
                            uint32_t physTag = translateAddress(tadr);
                            if (physTag + 16 <= PS2_RAM_SIZE)
                            {
                                const uint8_t *tp = m_rdram + physTag;
                                uint64_t tag = *reinterpret_cast<const uint64_t *>(tp);
                                uint16_t tagQwc = static_cast<uint16_t>(tag & 0xFFFF);
                                uint32_t id = static_cast<uint32_t>((tag >> 28) & 0x7);
                                uint32_t addr = static_cast<uint32_t>((tag >> 32) & 0x7FFFFFF);
                                std::cout << "[DMA chain] ch=" << ((channelBase == 0x1000A000) ? 2 : 1)
                                          << " tag id=0x" << std::hex << id
                                          << " qwc=" << tagQwc
                                          << " addr=0x" << addr
                                          << " raw=0x" << tag << std::dec << std::endl;
                                if (id == 0 || id == 1 || id == 2)
                                {
                                    doCopy(addr, tagQwc);
                                }
                            }
                        }
                        m_ioRegisters[address] &= ~0x100;
                    }
                }
            }
            return true;
        }

        if (address >= 0x10000200 && address < 0x10000300)
        {
            std::cout << "Interrupt register write: " << std::hex << address << " = " << value << std::dec << std::endl;
            return true;
        }
    }
    else if (address >= 0x12000000 && address < 0x12001000)
    {
        // GS registers
        std::cout << "GS register write: " << std::hex << address << " = " << value << std::dec << std::endl;
        m_gsWriteCount.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    return false;
}

uint32_t PS2Memory::readIORegister(uint32_t address)
{
    auto it = m_ioRegisters.find(address);
    if (it != m_ioRegisters.end())
    {
        return it->second;
    }

    if (address >= 0x10000000 && address < 0x10010000)
    {
        // Timer registers
        if (address >= 0x10000000 && address < 0x10000100)
        {
            if ((address & 0xF) == 0x00)
            {                            // COUNT registers
                uint32_t timerCount = 0; // Should calculate based on elapsed time
                std::cout << "Timer COUNT read: " << std::hex << address << " = " << timerCount << std::dec << std::endl;
                return timerCount;
            }
        }

        // DMA status registers
        if (address >= 0x10008000 && address < 0x1000F000)
        {
            if ((address & 0xFF) == 0x00)
            {                                                             // CHCR registers
                uint32_t channelStatus = m_ioRegisters[address] & ~0x100; // Clear busy bit
                std::cout << "DMA status read: " << std::hex << address << " = " << channelStatus << std::dec << std::endl;
                return channelStatus;
            }
        }

        // Interrupt status registers
        if (address >= 0x10000200 && address < 0x10000300)
        {
            std::cout << "Interrupt status read: " << std::hex << address << std::dec << std::endl;
            // Should calculate based on pending interrupts
            return 0;
        }
    }

    return 0;
}

void PS2Memory::registerCodeRegion(uint32_t start, uint32_t end)
{
    CodeRegion region;
    region.start = start;
    region.end = end;

    size_t sizeInWords = (end - start) / 4;
    region.modified.resize(sizeInWords, false);

    m_codeRegions.push_back(region);
    std::cout << "Registered code region: " << std::hex << start << " - " << end << std::dec << std::endl;
}

bool PS2Memory::isAddressInRegion(uint32_t address, const CodeRegion &region)
{
    return (address >= region.start && address < region.end);
}

void PS2Memory::markModified(uint32_t address, uint32_t size)
{
    for (auto &region : m_codeRegions)
    {
        if (address + size <= region.start || address >= region.end)
        {
            continue;
        }

        uint32_t overlapStart = std::max(address, region.start);
        uint32_t overlapEnd = std::min(address + size, region.end);

        for (uint32_t addr = overlapStart; addr < overlapEnd; addr += 4)
        {
            size_t bitIndex = (addr - region.start) / 4;
            if (bitIndex < region.modified.size())
            {
                region.modified[bitIndex] = true;
                std::cout << "Marked code at " << std::hex << addr << std::dec << " as modified" << std::endl;
            }
        }
    }
}

bool PS2Memory::isCodeModified(uint32_t address, uint32_t size)
{
    for (const auto &region : m_codeRegions)
    {
        if (address + size <= region.start || address >= region.end)
        {
            continue;
        }

        uint32_t overlapStart = std::max(address, region.start);
        uint32_t overlapEnd = std::min(address + size, region.end);

        for (uint32_t addr = overlapStart; addr < overlapEnd; addr += 4)
        {
            size_t bitIndex = (addr - region.start) / 4;
            if (bitIndex < region.modified.size() && region.modified[bitIndex])
            {
                return true; // Found modified code
            }
        }
    }

    return false; // No modifications found
}

void PS2Memory::clearModifiedFlag(uint32_t address, uint32_t size)
{
    for (auto &region : m_codeRegions)
    {
        if (address + size <= region.start || address >= region.end)
        {
            continue;
        }

        uint32_t overlapStart = std::max(address, region.start);
        uint32_t overlapEnd = std::min(address + size, region.end);

        for (uint32_t addr = overlapStart; addr < overlapEnd; addr += 4)
        {
            size_t bitIndex = (addr - region.start) / 4;
            if (bitIndex < region.modified.size())
            {
                region.modified[bitIndex] = false;
            }
        }
    }
}
