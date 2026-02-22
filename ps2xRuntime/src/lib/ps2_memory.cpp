#include "ps2_memory.h"
#include <iostream>
#include <cstring>
#include <stdexcept>
#include <algorithm>
#include <string>

namespace
{
    inline void inRange(uint32_t offset, size_t bytes, size_t regionSize, const char *op, uint32_t address)
    {
        if (static_cast<uint64_t>(offset) + static_cast<uint64_t>(bytes) > static_cast<uint64_t>(regionSize))
        {
            throw std::runtime_error(std::string(op) + " out-of-bounds at address: 0x" + std::to_string(address));
        }
    }

    template <typename T>
    inline T loadScalar(const uint8_t *base, uint32_t offset, size_t regionSize, const char *op, uint32_t address)
    {
        inRange(offset, sizeof(T), regionSize, op, address);
        T value{};
        std::memcpy(&value, base + offset, sizeof(T));
        return value;
    }

    template <typename T>
    inline void storeScalar(uint8_t *base, uint32_t offset, size_t regionSize, T value, const char *op, uint32_t address)
    {
        inRange(offset, sizeof(T), regionSize, op, address);
        std::memcpy(base + offset, &value, sizeof(T));
    }

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

}

// Helpers for GS VRAM addressing (PSMCT32 path).
static inline uint32_t gs_vram_offset(uint32_t basePage, uint32_t x, uint32_t y, uint32_t fbw)
{
    // basePage is in 2048-byte units; fbw is in blocks of 64 pixels.
    uint32_t strideBytes = fbw * 64 * 4;
    return basePage * 2048 + y * strideBytes + x * 4;
}

PS2Memory::PS2Memory()
    : m_rdram(nullptr), m_scratchpad(nullptr), iop_ram(nullptr), m_seenGifCopy(false), m_gsVRAM(nullptr)
{
    ps2SetScratchpadHostPtr(nullptr);
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
        ps2SetScratchpadHostPtr(nullptr);
        delete[] m_scratchpad;
        m_scratchpad = nullptr;
    }

    if (m_gsVRAM)
    {
        delete[] m_gsVRAM;
        m_gsVRAM = nullptr;
    }

    if (iop_ram)
    {
        delete[] iop_ram;
        iop_ram = nullptr;
    }
}

bool PS2Memory::initialize(size_t ramSize)
{
    auto cleanup = [this]()
    {
        delete[] m_rdram;
        delete[] m_scratchpad;
        delete[] iop_ram;
        delete[] m_gsVRAM;
        m_rdram = nullptr;
        m_scratchpad = nullptr;
        ps2SetScratchpadHostPtr(nullptr);
        iop_ram = nullptr;
        m_gsVRAM = nullptr;
    };

    cleanup();
    m_seenGifCopy = false;
    m_dmaStartCount.store(0, std::memory_order_relaxed);
    m_gifCopyCount.store(0, std::memory_order_relaxed);
    m_gsWriteCount.store(0, std::memory_order_relaxed);
    m_vifWriteCount.store(0, std::memory_order_relaxed);
    m_codeRegions.clear();

    try
    {
        // Allocate main RAM
        m_rdram = new uint8_t[ramSize];
        std::memset(m_rdram, 0, ramSize);

        // Allocate scratchpad
        m_scratchpad = new uint8_t[PS2_SCRATCHPAD_SIZE];
        std::memset(m_scratchpad, 0, PS2_SCRATCHPAD_SIZE);
        ps2SetScratchpadHostPtr(m_scratchpad);

        // Initialize EE TLB entries (R5900 has 48 entries).
        m_tlbEntries.assign(48, TLBEntry{0, 0, 0, false});

        // Allocate IOP RAM
        iop_ram = new uint8_t[2 * 1024 * 1024]; // 2MB

        // Initialize IOP RAM with zeros
        std::memset(iop_ram, 0, 2 * 1024 * 1024);

        // Initialize I/O registers
        m_ioRegisters.clear();

        // Initialize GS registers
        memset(&gs_regs, 0, sizeof(gs_regs));
        m_gsDrawCtx = GSDrawContext{};

        // Allocate GS VRAM (4MB)
        m_gsVRAM = new uint8_t[PS2_GS_VRAM_SIZE];
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
        cleanup();
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

    // KSEG0/KSEG1 direct-mapped window.
    if (virtualAddress >= 0x80000000 && virtualAddress < 0xC0000000)
    {
        return virtualAddress & 0x1FFFFFFF;
    }

    // In this runtime, low segments are treated as physical-style addresses already.
    if (virtualAddress < 0x80000000)
    {
        return virtualAddress;
    }

    // KSEG2/KSEG3 are TLB mapped.
    if (virtualAddress >= 0xC0000000)
    {
        for (const auto &entry : m_tlbEntries)
        {
            if (entry.valid)
            {
                // PageMask uses bits [24:13]. Build an address-level mask (plus 4KB base page bits).
                const uint32_t mask = entry.mask & 0x01FFE000u;
                const uint32_t compareMask = ~(mask | 0xFFFu);
                if ((virtualAddress & compareMask) == (entry.vpn & compareMask))
                {
                    // TLB hit
                    const uint32_t pageOffsetMask = mask | 0xFFFu;
                    const uint32_t physBase = entry.pfn << 12;
                    return physBase | (virtualAddress & pageOffsetMask);
                }
            }
        }
        throw std::runtime_error("TLB miss for address: 0x" + std::to_string(virtualAddress));
    }

    return virtualAddress;
}

bool PS2Memory::tlbRead(uint32_t index, uint32_t &vpn, uint32_t &pfn, uint32_t &mask, bool &valid) const
{
    if (index >= m_tlbEntries.size())
    {
        return false;
    }

    const TLBEntry &entry = m_tlbEntries[index];
    vpn = entry.vpn;
    pfn = entry.pfn;
    mask = entry.mask;
    valid = entry.valid;
    return true;
}

bool PS2Memory::tlbWrite(uint32_t index, uint32_t vpn, uint32_t pfn, uint32_t mask, bool valid)
{
    if (index >= m_tlbEntries.size())
    {
        return false;
    }

    TLBEntry &entry = m_tlbEntries[index];
    entry.vpn = vpn & 0xFFFFF000u;
    entry.pfn = pfn & 0x000FFFFFu;
    entry.mask = mask & 0x01FFE000u;
    entry.valid = valid;
    return true;
}

int32_t PS2Memory::tlbProbe(uint32_t vpn) const
{
    const uint32_t normalizedVpn = vpn & 0xFFFFF000u;
    for (uint32_t i = 0; i < static_cast<uint32_t>(m_tlbEntries.size()); ++i)
    {
        const TLBEntry &entry = m_tlbEntries[i];
        if (!entry.valid)
        {
            continue;
        }

        const uint32_t mask = entry.mask & 0x01FFE000u;
        const uint32_t compareMask = ~(mask | 0xFFFu);
        if ((normalizedVpn & compareMask) == (entry.vpn & compareMask))
        {
            return static_cast<int32_t>(i);
        }
    }

    return -1;
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
        uint32_t value = readIORegister(regAddr);
        uint32_t shift = (physAddr & 3) * 8;
        return static_cast<uint8_t>((value >> shift) & 0xFF);
    }

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
        return loadScalar<uint16_t>(m_scratchpad, physAddr, PS2_SCRATCHPAD_SIZE, "read16 scratchpad", address);
    }
    if (physAddr < PS2_RAM_SIZE)
    {
        return loadScalar<uint16_t>(m_rdram, physAddr, PS2_RAM_SIZE, "read16 rdram", address);
    }
    else if (physAddr >= PS2_IO_BASE && physAddr < PS2_IO_BASE + PS2_IO_SIZE)
    {
        uint32_t regAddr = physAddr & ~0x3;
        uint32_t value = readIORegister(regAddr);
        uint32_t shift = (physAddr & 2) * 8;
        return static_cast<uint16_t>((value >> shift) & 0xFFFF);
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
        if (!reg)
            return 0;
        uint32_t off = address & 7;
        uint64_t val = *reg;
        return (uint32_t)(val >> (off * 8));
    }

    const bool scratch = isScratchpad(address);
    uint32_t physAddr = translateAddress(address);

    if (scratch)
    {
        return loadScalar<uint32_t>(m_scratchpad, physAddr, PS2_SCRATCHPAD_SIZE, "read32 scratchpad", address);
    }
    if (physAddr < PS2_RAM_SIZE)
    {
        return loadScalar<uint32_t>(m_rdram, physAddr, PS2_RAM_SIZE, "read32 rdram", address);
    }
    else if (physAddr >= PS2_IO_BASE && physAddr < PS2_IO_BASE + PS2_IO_SIZE)
    {
        return readIORegister(physAddr);
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
        return loadScalar<uint64_t>(m_scratchpad, physAddr, PS2_SCRATCHPAD_SIZE, "read64 scratchpad", address);
    }
    if (physAddr < PS2_RAM_SIZE)
    {
        return loadScalar<uint64_t>(m_rdram, physAddr, PS2_RAM_SIZE, "read64 rdram", address);
    }

    // 64-bit IO read: compose from the two adjacent 32-bit IO register slots
    // to avoid any side-effects from read32 handlers.
    if (address >= PS2_IO_BASE && address < (PS2_IO_BASE + PS2_IO_SIZE))
    {
        uint32_t lo = m_ioRegisters.count(address) ? m_ioRegisters[address] : 0u;
        uint32_t hi = m_ioRegisters.count(address + 4) ? m_ioRegisters[address + 4] : 0u;
        return static_cast<uint64_t>(lo) | (static_cast<uint64_t>(hi) << 32);
    }
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
        inRange(physAddr, sizeof(__m128i), PS2_SCRATCHPAD_SIZE, "read128 scratchpad", address);
        return _mm_loadu_si128(reinterpret_cast<__m128i *>(&m_scratchpad[physAddr]));
    }
    if (physAddr < PS2_RAM_SIZE)
    {
        inRange(physAddr, sizeof(__m128i), PS2_RAM_SIZE, "read128 rdram", address);
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
    }
    else if (physAddr >= PS2_IO_BASE && physAddr < PS2_IO_BASE + PS2_IO_SIZE)
    {
        // IO registers - handle byte writes by modifying the appropriate byte in the word
        uint32_t regAddr = physAddr & ~0x3;
        uint32_t shift = (physAddr & 3) * 8;
        uint32_t mask = ~(0xFF << shift);
        uint32_t newValue = (m_ioRegisters[regAddr] & mask) | ((uint32_t)value << shift);
        writeIORegister(regAddr, newValue);
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
        storeScalar<uint16_t>(m_scratchpad, physAddr, PS2_SCRATCHPAD_SIZE, value, "write16 scratchpad", address);
    }
    else if (physAddr < PS2_RAM_SIZE)
    {
        storeScalar<uint16_t>(m_rdram, physAddr, PS2_RAM_SIZE, value, "write16 rdram", address);
    }
    else if (physAddr >= PS2_IO_BASE && physAddr < PS2_IO_BASE + PS2_IO_SIZE)
    {
        uint32_t regAddr = physAddr & ~0x3;
        uint32_t shift = (physAddr & 2) * 8;
        uint32_t mask = ~(0xFFFF << shift);
        uint32_t newValue = (m_ioRegisters[regAddr] & mask) | ((uint32_t)value << shift);
        writeIORegister(regAddr, newValue);
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
        }
        return;
    }

    const bool scratch = isScratchpad(address);
    uint32_t physAddr = translateAddress(address);

    if (scratch)
    {
        storeScalar<uint32_t>(m_scratchpad, physAddr, PS2_SCRATCHPAD_SIZE, value, "write32 scratchpad", address);
    }
    else if (physAddr < PS2_RAM_SIZE)
    {
        // Check if this might be code modification
        markModified(address, 4);

        storeScalar<uint32_t>(m_rdram, physAddr, PS2_RAM_SIZE, value, "write32 rdram", address);
    }
    else if (physAddr >= PS2_IO_BASE && physAddr < PS2_IO_BASE + PS2_IO_SIZE)
    {
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
        }
        return;
    }

    const bool scratch = isScratchpad(address);
    uint32_t physAddr = translateAddress(address);

    if (scratch)
    {
        storeScalar<uint64_t>(m_scratchpad, physAddr, PS2_SCRATCHPAD_SIZE, value, "write64 scratchpad", address);
    }
    else if (physAddr < PS2_RAM_SIZE)
    {
        markModified(address, 8);
        storeScalar<uint64_t>(m_rdram, physAddr, PS2_RAM_SIZE, value, "write64 rdram", address);
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
        inRange(physAddr, sizeof(__m128i), PS2_SCRATCHPAD_SIZE, "write128 scratchpad", address);
        _mm_storeu_si128(reinterpret_cast<__m128i *>(&m_scratchpad[physAddr]), value);
    }
    else if (physAddr < PS2_RAM_SIZE)
    {
        markModified(address, 16);
        inRange(physAddr, sizeof(__m128i), PS2_RAM_SIZE, "write128 rdram", address);
        _mm_storeu_si128(reinterpret_cast<__m128i *>(&m_rdram[physAddr]), value);
    }
    else
    {
        // Non-RAM 128-bit stores are modeled as two 64-bit stores.
        uint64_t lo = _mm_extract_epi64(value, 0);
        uint64_t hi = _mm_extract_epi64(value, 1);

        write64(address, lo);
        write64(address + 8, hi);
    }
}

bool PS2Memory::writeIORegister(uint32_t address, uint32_t value)
{
    // ── IPU registers (0x10002000-0x10002030) ──────────────────
    // On real PS2, IPU_CTRL bit 31 (BUSY) is READ-ONLY — set by hardware.
    // We must NOT store the raw value for IPU_CTRL because the game
    // might write 0x40000000 (RST) and we'd return 0 with no BUSY,
    // but if any stale value had bit 31, the polling loop would hang.
    if (address >= 0x10002000 && address <= 0x10002030)
    {
        static int ipuWriteLog = 0;
        if (ipuWriteLog < 30)
        {
            std::cerr << "[IPU] write addr=0x" << std::hex << address
                      << " val=0x" << value << std::dec << std::endl;
            ++ipuWriteLog;
        }
        if (address == 0x10002010)
        {
            // IPU_CTRL write: bit 30 = RST (reset). After reset,
            // all status bits clear. Never store BUSY (bit 31).
            if (value & (1u << 30))
            {
                // Reset IPU — clear all IPU registers
                m_ioRegisters[0x10002000] = 0;
                m_ioRegisters[0x10002010] = 0;
                m_ioRegisters[0x10002020] = 0;
                m_ioRegisters[0x10002030] = 0;
            }
            else
            {
                // Store without BUSY bit
                m_ioRegisters[address] = value & ~(1u << 31);
            }
        }
        else
        {
            // IPU_CMD (0x10002000) — store command, don't set busy
            m_ioRegisters[address] = value;
        }
        return true;
    }

    m_ioRegisters[address] = value;

    {
        static int io_total_log = 0;
        if (io_total_log < 100)
        {
            std::cerr << "[IO_WRITE] addr=0x" << std::hex << address << " val=0x" << value << std::dec << std::endl;
            ++io_total_log;
        }
    }

    if (address >= 0x10008000 && address < 0x1000F000)
    {
        static int dma_io_log = 0;
        if (dma_io_log < 200)
        {
            uint32_t ch = (address >> 8) & 0xFF;
            uint32_t off = address & 0xFF;
            std::cerr << "[DMA_IO] ch=0x" << std::hex << (address & 0xFFFFFF00)
                      << " off=0x" << off << " val=0x" << value << std::dec << std::endl;
            ++dma_io_log;
        }
    }

    if (address >= 0x10008000 && address < 0x1000F000)
    {
        if ((address & 0xFF) == 0x00 && (value & 0x100))
        {
            const uint32_t channelBase = address & 0xFFFFFF00;
            const uint32_t madr = m_ioRegisters[channelBase + 0x10];
            const uint32_t qwc = m_ioRegisters[channelBase + 0x20];
            m_dmaStartCount.fetch_add(1, std::memory_order_relaxed);

            if ((channelBase == 0x1000A000 || channelBase == 0x10009000) && m_gsVRAM)
            {
                auto dispatchTransfer = [&](uint32_t srcAddr, uint32_t qwCount)
                {
                    if (qwCount == 0)
                    {
                        return;
                    }

                    uint32_t srcPhys = 0;
                    try
                    {
                        srcPhys = translateAddress(srcAddr);
                    }
                    catch (const std::exception &)
                    {
                        return;
                    }

                    if (srcPhys >= PS2_RAM_SIZE)
                    {
                        return;
                    }

                    if (channelBase == 0x1000A000)
                    {
                        processGIFPacket(srcPhys, qwCount);
                        return;
                    }

                    const uint64_t bytes64 = static_cast<uint64_t>(qwCount) * 16ull;
                    uint32_t bytes = bytes64 > static_cast<uint64_t>(PS2_RAM_SIZE)
                                         ? PS2_RAM_SIZE
                                         : static_cast<uint32_t>(bytes64);
                    if (srcPhys + bytes > PS2_RAM_SIZE)
                    {
                        bytes = PS2_RAM_SIZE - srcPhys;
                    }
                    processVIF1Data(srcPhys, bytes);
                };

                auto walkChain = [&](uint32_t startTadr)
                {
                    uint32_t curTadr = startTadr;
                    constexpr int kMaxTags = 4096;
                    for (int i = 0; i < kMaxTags; ++i)
                    {
                        uint32_t physTag = 0;
                        try
                        {
                            physTag = translateAddress(curTadr);
                        }
                        catch (const std::exception &)
                        {
                            break;
                        }

                        if (physTag + 16 > PS2_RAM_SIZE)
                        {
                            break;
                        }

                        const uint64_t tag = loadScalar<uint64_t>(m_rdram, physTag, PS2_RAM_SIZE, "dma chain tag", curTadr);
                        const uint16_t tagQwc = static_cast<uint16_t>(tag & 0xFFFFu);
                        const uint32_t id = static_cast<uint32_t>((tag >> 28) & 0x7u);
                        const uint32_t addr = static_cast<uint32_t>((tag >> 32) & 0x7FFFFFF0u);
                        const bool irq = ((tag >> 31) & 0x1u) != 0;

                        uint32_t dataAddr = 0;
                        uint32_t nextTag = 0;
                        bool endChain = false;

                        switch (id)
                        {
                        case 0: // REFE
                            dataAddr = addr;
                            endChain = true;
                            break;
                        case 1: // CNT
                            dataAddr = curTadr + 16u;
                            nextTag = curTadr + 16u + static_cast<uint32_t>(tagQwc) * 16u;
                            break;
                        case 2: // NEXT
                            dataAddr = curTadr + 16u;
                            nextTag = addr;
                            break;
                        case 3: // REF
                        case 4: // REFS
                            dataAddr = addr;
                            nextTag = curTadr + 16u;
                            break;
                        case 7: // END
                            dataAddr = curTadr + 16u;
                            endChain = true;
                            break;
                        default:
                            endChain = true;
                            break;
                        }

                        if (tagQwc > 0 && dataAddr != 0)
                        {
                            dispatchTransfer(dataAddr, tagQwc);
                        }

                        if (endChain || irq)
                        {
                            break;
                        }
                        curTadr = nextTag;
                    }
                };

                if (qwc > 0)
                {
                    dispatchTransfer(madr, qwc);
                }
                else
                {
                    const uint32_t tadr = m_ioRegisters[channelBase + 0x30];
                    walkChain(tadr);
                }

                m_ioRegisters[address] &= ~0x100;
            }
        }
        return true;
    }

    if (address >= 0x10000000 && address < 0x10010000)
    {
        if (address >= 0x10003800 && address < 0x10003A00)
        {
            m_vifWriteCount.fetch_add(1, std::memory_order_relaxed);
        }
        if (address >= 0x10003C00 && address < 0x10003E00)
        {
            m_vifWriteCount.fetch_add(1, std::memory_order_relaxed);
        }
        if (address >= 0x10000200 && address < 0x10000300)
        {
            return true;
        }
        if (address >= 0x10000000 && address < 0x10000100)
        {
            return true;
        }
    }

    if (address >= 0x12000000 && address < 0x12001000)
    {
        m_gsWriteCount.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    return false;
}

// ============================================================================
// pollDmaRegisters: Workaround for KSEG1 fast-path bypass
// When libsles.a is compiled with old headers, isSpecialAddress() doesn't
// recognize KSEG0/KSEG1 addresses (0x8xxx/0xBxxx). Game writes to e.g.
// 0xB000A000 (GIF DMA CHCR via KSEG1) go through Ps2FastWrite32 which
// stores to rdram[addr & 0x01FFFFFF] = rdram[0x1000A000], bypassing
// writeIORegister entirely. This function polls those shadow locations
// and triggers DMA processing when CHCR.STR (bit 8) is set.
//
// NOTE: DISABLED — sho_runner writes DMA regs via physical addresses which
// go through writeIORegister correctly. This function was reading garbage
// from rdram shadow (ELF code area) and triggering bogus DMA transfers.
// ============================================================================
int PS2Memory::pollDmaRegisters()
{
    // Disabled — DMA writes go through writeIORegister, not KSEG1 shadow
    return 0;
}

uint32_t PS2Memory::readIORegister(uint32_t address)
{
    // ── IPU registers (0x10002000-0x10002030) ──────────────────
    // IPU_CMD  0x10002000: command result / FIFO output
    // IPU_CTRL 0x10002010: status — bit 31=BUSY (always 0: we don't decode)
    // IPU_BP   0x10002020: bitstream pointer
    // IPU_TOP  0x10002030: top 32 bits of FIFO
    if (address >= 0x10002000 && address <= 0x10002030)
    {
        static int ipuReadLog = 0;
        uint32_t val = 0;
        switch (address)
        {
        case 0x10002000: // IPU_CMD — command result
            val = m_ioRegisters[address];
            break;
        case 0x10002010:                                // IPU_CTRL — always NOT busy, ECD=0
            val = m_ioRegisters[address] & ~(1u << 31); // clear BUSY
            break;
        case 0x10002020: // IPU_BP
        case 0x10002030: // IPU_TOP
            val = m_ioRegisters[address];
            break;
        default:
            val = 0;
            break;
        }
        if (ipuReadLog < 30)
        {
            std::cerr << "[IPU] read addr=0x" << std::hex << address
                      << " val=0x" << val << std::dec << std::endl;
            ++ipuReadLog;
        }
        return val;
    }
    if (address >= 0x10000000 && address < 0x10010000)
    {
        if (address >= 0x10000000 && address < 0x10000100)
        {
            if ((address & 0xF) == 0x00)
            {
                return 0;
            }
        }

        if (address >= 0x10008000 && address < 0x1000F000)
        {
            if ((address & 0xFF) == 0x00)
            {
                // Return CHCR as-is. STR (bit 8) is cleared after DMA
                // completion in writeIORegister, not on read.
                return m_ioRegisters[address];
            }
        }

        if (address >= 0x10000200 && address < 0x10000300)
        {
            return 0;
        }

        // SIF hardware registers — HLE: pretend IOP is always ready
        // 0x1000F200: SIF_SMCOM — IOP communication status
        // 0x1000F210: SIF_MSCOM — EE→IOP command
        // 0x1000F220: SIF_MSFLG — Main→Sub flags
        // 0x1000F230: SIF_SMFLG — Sub→Main flags (IOP ready bits)
        // 0x1000F240: SIF_CTRL  — SIF control
        if (address >= 0x1000F200 && address <= 0x1000F260)
        {
            static std::atomic<uint64_t> sifReads{0};
            uint64_t n = sifReads.fetch_add(1);
            if (n < 5 || (n % 100000) == 0)
            {
                std::cerr << "[SIF-HW] read 0x" << std::hex << address
                          << " #" << std::dec << n << std::endl;
            }
            if (address == 0x1000F230)
            {
                return 0x60000;
            }
            if (address == 0x1000F240)
            {
                return 0xF0000002;
            }
            return 0;
        }
    }

    auto it = m_ioRegisters.find(address);
    if (it != m_ioRegisters.end())
    {
        return it->second;
    }

    return 0;
}

void PS2Memory::registerCodeRegion(uint32_t start, uint32_t end)
{
    if (end <= start)
    {
        std::cerr << "Ignoring invalid code region: start=0x" << std::hex << start
                  << " end=0x" << end << std::dec << std::endl;
        return;
    }

    if ((end - start) > PS2_RAM_SIZE)
    {
        std::cerr << "Ignoring oversized code region: start=0x" << std::hex << start
                  << " end=0x" << end << std::dec << std::endl;
        return;
    }

    for (const auto &existing : m_codeRegions)
    {
        if (existing.start == start && existing.end == end)
        {
            return;
        }
    }

    CodeRegion region;
    region.start = start;
    region.end = end;

    size_t sizeInWords = (end - start + 3u) / 4u;
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
    if (size == 0)
    {
        return;
    }

    const uint64_t writeEnd = static_cast<uint64_t>(address) + static_cast<uint64_t>(size);
    for (auto &region : m_codeRegions)
    {
        const uint64_t regionStart = region.start;
        const uint64_t regionEnd = region.end;
        if (writeEnd <= regionStart || static_cast<uint64_t>(address) >= regionEnd)
        {
            continue;
        }

        uint32_t overlapStart = static_cast<uint32_t>(std::max<uint64_t>(address, regionStart));
        uint32_t overlapEnd = static_cast<uint32_t>(std::min<uint64_t>(writeEnd, regionEnd));

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
    if (size == 0)
    {
        return false;
    }

    const uint64_t writeEnd = static_cast<uint64_t>(address) + static_cast<uint64_t>(size);
    for (const auto &region : m_codeRegions)
    {
        const uint64_t regionStart = region.start;
        const uint64_t regionEnd = region.end;
        if (writeEnd <= regionStart || static_cast<uint64_t>(address) >= regionEnd)
        {
            continue;
        }

        uint32_t overlapStart = static_cast<uint32_t>(std::max<uint64_t>(address, regionStart));
        uint32_t overlapEnd = static_cast<uint32_t>(std::min<uint64_t>(writeEnd, regionEnd));

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
    if (size == 0)
    {
        return;
    }

    const uint64_t writeEnd = static_cast<uint64_t>(address) + static_cast<uint64_t>(size);
    for (auto &region : m_codeRegions)
    {
        const uint64_t regionStart = region.start;
        const uint64_t regionEnd = region.end;
        if (writeEnd <= regionStart || static_cast<uint64_t>(address) >= regionEnd)
        {
            continue;
        }

        uint32_t overlapStart = static_cast<uint32_t>(std::max<uint64_t>(address, regionStart));
        uint32_t overlapEnd = static_cast<uint32_t>(std::min<uint64_t>(writeEnd, regionEnd));

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
