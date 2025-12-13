#include "ps2_runtime.h"
#include <iostream>
#include <cstring>
#include <stdexcept>
#include <fstream>

// GS register logging - output to file to avoid console spam
static std::ofstream gs_log_file;
static bool gs_log_initialized = false;
static uint64_t gs_write_count = 0;

static void initGSLog() {
    if (!gs_log_initialized) {
        gs_log_file.open("gs_trace.log", std::ios::out | std::ios::trunc);
        gs_log_initialized = true;
        if (gs_log_file.is_open()) {
            gs_log_file << "=== GS Register Trace ===" << std::endl;
        }
    }
}

// Helper: Get GS privileged register name (0x12000000 range)
static const char* getGSPrivRegName(uint32_t offset) {
    switch (offset) {
        case 0x00: return "PMODE";      // PCRTC mode
        case 0x10: return "SMODE1";     // Sync mode 1
        case 0x20: return "SMODE2";     // Sync mode 2 (interlace)
        case 0x30: return "SRFSH";      // DRAM refresh
        case 0x40: return "SYNCH1";     // H-sync
        case 0x50: return "SYNCH2";     // H-sync
        case 0x60: return "SYNCV";      // V-sync
        case 0x70: return "DISPFB1";    // Display buffer 1
        case 0x80: return "DISPLAY1";   // Display area 1
        case 0x90: return "DISPFB2";    // Display buffer 2
        case 0xA0: return "DISPLAY2";   // Display area 2
        case 0xB0: return "EXTBUF";     // Feedback buffer
        case 0xC0: return "EXTDATA";    // Feedback data
        case 0xD0: return "EXTWRITE";   // Feedback write
        case 0xE0: return "BGCOLOR";    // Background color
        case 0x1000: return "CSR";      // System status
        case 0x1010: return "IMR";      // Interrupt mask
        case 0x1040: return "BUSDIR";   // Bus direction
        case 0x1080: return "SIGLBLID"; // Signal/Label ID
        default: return "UNKNOWN";
    }
}

// Helper: Get GS general register name (GIF packet registers)
static const char* getGSGenRegName(uint8_t reg) {
    switch (reg) {
        case 0x00: return "PRIM";       // Primitive type
        case 0x01: return "RGBAQ";      // Vertex color
        case 0x02: return "ST";         // Texture coords
        case 0x03: return "UV";         // Texture coords (integer)
        case 0x04: return "XYZF2";      // Vertex + fog (no draw)
        case 0x05: return "XYZ2";       // Vertex (no draw)
        case 0x06: return "TEX0_1";     // Texture info ctx1
        case 0x07: return "TEX0_2";     // Texture info ctx2
        case 0x08: return "CLAMP_1";    // Texture clamp ctx1
        case 0x09: return "CLAMP_2";    // Texture clamp ctx2
        case 0x0A: return "FOG";        // Fog value
        case 0x0C: return "XYZF3";      // Vertex + fog (draw)
        case 0x0D: return "XYZ3";       // Vertex (draw)
        case 0x14: return "TEX1_1";     // Texture info ctx1
        case 0x15: return "TEX1_2";     // Texture info ctx2
        case 0x16: return "TEX2_1";     // Texture CLUT ctx1
        case 0x17: return "TEX2_2";     // Texture CLUT ctx2
        case 0x18: return "XYOFFSET_1"; // Drawing offset ctx1
        case 0x19: return "XYOFFSET_2"; // Drawing offset ctx2
        case 0x1A: return "PRMODECONT"; // Primitive mode continue
        case 0x1B: return "PRMODE";     // Primitive mode
        case 0x1C: return "TEXCLUT";    // CLUT position
        case 0x22: return "SCANMSK";    // Scanline mask
        case 0x34: return "MIPTBP1_1"; // MIPMAP addr ctx1
        case 0x35: return "MIPTBP1_2"; // MIPMAP addr ctx2
        case 0x36: return "MIPTBP2_1"; // MIPMAP addr ctx1
        case 0x37: return "MIPTBP2_2"; // MIPMAP addr ctx2
        case 0x3B: return "TEXA";       // Texture alpha
        case 0x3D: return "FOGCOL";     // Fog color
        case 0x3F: return "TEXFLUSH";   // Texture flush
        case 0x40: return "SCISSOR_1"; // Scissor ctx1
        case 0x41: return "SCISSOR_2"; // Scissor ctx2
        case 0x42: return "ALPHA_1";   // Alpha blend ctx1
        case 0x43: return "ALPHA_2";   // Alpha blend ctx2
        case 0x44: return "DIMX";      // Dither matrix
        case 0x45: return "DTHE";      // Dither enable
        case 0x46: return "COLCLAMP"; // Color clamp
        case 0x47: return "TEST_1";   // Pixel test ctx1
        case 0x48: return "TEST_2";   // Pixel test ctx2
        case 0x49: return "PABE";     // Per-pixel alpha
        case 0x4A: return "FBA_1";    // Alpha correction ctx1
        case 0x4B: return "FBA_2";    // Alpha correction ctx2
        case 0x4C: return "FRAME_1";  // Framebuffer ctx1
        case 0x4D: return "FRAME_2";  // Framebuffer ctx2
        case 0x4E: return "ZBUF_1";   // Z-buffer ctx1
        case 0x4F: return "ZBUF_2";   // Z-buffer ctx2
        case 0x50: return "BITBLTBUF"; // Transmission area
        case 0x51: return "TRXPOS";   // Transmission position
        case 0x52: return "TRXREG";   // Transmission size
        case 0x53: return "TRXDIR";   // Transmission direction
        case 0x54: return "HWREG";    // Host data write
        case 0x60: return "SIGNAL";   // Signal event
        case 0x61: return "FINISH";   // Drawing finish
        case 0x62: return "LABEL";    // Label event
        default: return "UNKNOWN";
    }
}

// Helper: Get DMA channel name
static const char* getDMAChannelName(int channel) {
    switch (channel) {
        case 0: return "VIF0";
        case 1: return "VIF1";
        case 2: return "GIF";      // <-- This is the important one for graphics!
        case 3: return "IPU_FROM";
        case 4: return "IPU_TO";
        case 5: return "SIF0";
        case 6: return "SIF1";
        case 7: return "SIF2";
        case 8: return "SPR_FROM";
        case 9: return "SPR_TO";
        default: return "UNKNOWN";
    }
}

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
    // Handle special memory regions
    if (isScratchpad(virtualAddress))
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
    // Check alignment
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

    const bool scratch = isScratchpad(address);
    uint32_t physAddr = translateAddress(address);

    if (scratch)
    {
        *reinterpret_cast<uint16_t *>(&m_scratchpad[physAddr]) = value;
    }
    else if (physAddr < PS2_RAM_SIZE)
    {
        *reinterpret_cast<uint16_t *>(&m_rdram[physAddr]) = value;
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
    }
    else if (physAddr >= PS2_IO_BASE && physAddr < PS2_IO_BASE + PS2_IO_SIZE)
    {
        // Handle IO register writes with potential side effects
        writeIORegister(physAddr, value);
    }
}

void PS2Memory::write64(uint32_t address, uint64_t value)
{
    // Check alignment
    if (address & 7)
    {
        throw std::runtime_error("Unaligned 64-bit write at address: 0x" + std::to_string(address));
    }

    // LOG GS PRIVILEGED REGISTER WRITES (64-bit)
    if (address >= 0x12000000 && address < 0x12002000)
    {
        initGSLog();
        uint32_t offset = address - 0x12000000;
        const char* regName = getGSPrivRegName(offset);

        gs_write_count++;
        if (gs_log_file.is_open()) {
            gs_log_file << "[GS #" << gs_write_count << "] "
                       << regName << " (0x" << std::hex << address << ") = 0x"
                       << value << std::dec << std::endl;
        }

        // Also print first 100 to console so user sees something happening
        if (gs_write_count <= 100) {
            std::cout << "[GS] " << regName << " = 0x" << std::hex << value << std::dec << std::endl;
        } else if (gs_write_count == 101) {
            std::cout << "[GS] ... (see gs_trace.log for full output)" << std::endl;
        }
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

bool PS2Memory::writeIORegister(uint32_t address, uint32_t value)
{
    m_ioRegisters[address] = value;

    // Now check if this is a special hardware register
    if (address >= 0x10000000 && address < 0x10010000)
    {
        // Timer/counter registers
        if (address >= 0x10000000 && address < 0x10000100)
        {
            std::cout << "Timer register write: " << std::hex << address << " = " << value << std::dec << std::endl;
            return true;
        }

        // DMA registers
        if (address >= 0x10008000 && address < 0x1000F000)
        {
            // Calculate channel number from address
            // DMA channels are at: 0x10008000 (VIF0), 0x10009000 (VIF1), 0x1000A000 (GIF), etc.
            int channel = (address >> 12) & 0xF;
            if (channel >= 8) channel = channel - 8 + 8; // Adjust for higher channels
            else channel = channel - 8;

            const char* channelName = getDMAChannelName(channel);
            uint32_t regOffset = address & 0xFF;

            const char* regName = "UNKNOWN";
            switch (regOffset) {
                case 0x00: regName = "CHCR"; break;  // Channel control
                case 0x10: regName = "MADR"; break;  // Memory address
                case 0x20: regName = "QWC"; break;   // Quadword count
                case 0x30: regName = "TADR"; break;  // Tag address
                case 0x40: regName = "ASR0"; break;  // Address stack 0
                case 0x50: regName = "ASR1"; break;  // Address stack 1
                case 0x80: regName = "SADR"; break;  // Stall address
            }

            initGSLog();
            if (gs_log_file.is_open()) {
                gs_log_file << "[DMA] " << channelName << "." << regName
                           << " (0x" << std::hex << address << ") = 0x" << value << std::dec << std::endl;
            }

            // Check if we need to start a DMA transfer
            if (regOffset == 0x00 && (value & 0x100))
            { // CHCR register with STR bit set
                uint32_t channelBase = address & 0xFFFFFF00;
                uint32_t madr = m_ioRegisters[channelBase + 0x10]; // Memory address
                uint32_t qwc = m_ioRegisters[channelBase + 0x20];  // Quadword count
                uint32_t tadr = m_ioRegisters[channelBase + 0x30]; // Tag address

                std::cout << "\n=== DMA TRANSFER START ===" << std::endl;
                std::cout << "Channel: " << channelName << " (" << channel << ")" << std::endl;
                std::cout << "MADR: 0x" << std::hex << madr << " (source memory)" << std::endl;
                std::cout << "QWC: " << std::dec << qwc << " quadwords (" << (qwc * 16) << " bytes)" << std::endl;
                std::cout << "TADR: 0x" << std::hex << tadr << std::dec << std::endl;
                std::cout << "CHCR: 0x" << std::hex << value << std::dec << std::endl;

                // Decode CHCR bits
                int dir = value & 0x1;          // 0=to memory, 1=from memory
                int mod = (value >> 2) & 0x3;   // Transfer mode
                int asp = (value >> 4) & 0x3;   // Address stack pointer
                int tte = (value >> 6) & 0x1;   // Tag transfer enable
                int tie = (value >> 7) & 0x1;   // Tag interrupt enable
                int str = (value >> 8) & 0x1;   // Start
                int tag = (value >> 16) & 0xFFFF; // DMAtag

                std::cout << "  DIR=" << (dir ? "FromMem" : "ToMem");
                std::cout << " MOD=" << mod;
                std::cout << " TTE=" << tte;
                std::cout << " TAG=0x" << std::hex << tag << std::dec << std::endl;

                // If this is GIF channel, log more details
                if (channel == 2) {
                    std::cout << ">>> GIF TRANSFER: " << (qwc * 16) << " bytes from 0x"
                              << std::hex << madr << std::dec << std::endl;

                    // Log first few quadwords of the GIF packet
                    if (madr < PS2_RAM_SIZE && qwc > 0) {
                        std::cout << "GIF Data Preview (first 4 QW):" << std::endl;
                        for (int i = 0; i < std::min((int)qwc, 4); i++) {
                            uint32_t addr = madr + i * 16;
                            if (addr + 16 <= PS2_RAM_SIZE) {
                                uint64_t lo = *reinterpret_cast<uint64_t*>(m_rdram + addr);
                                uint64_t hi = *reinterpret_cast<uint64_t*>(m_rdram + addr + 8);
                                std::cout << "  QW[" << i << "]: " << std::hex
                                         << hi << "_" << lo << std::dec << std::endl;
                            }
                        }
                    }
                }
                std::cout << "==========================\n" << std::endl;

                if (gs_log_file.is_open()) {
                    gs_log_file << "\n=== DMA " << channelName << " TRANSFER ===" << std::endl;
                    gs_log_file << "MADR=0x" << std::hex << madr << " QWC=" << std::dec << qwc << std::endl;
                }
            }
            return true;
        }

        // Interrupt control registers
        if (address >= 0x10000200 && address < 0x10000300)
        {
            std::cout << "Interrupt register write: " << std::hex << address << " = " << value << std::dec << std::endl;
            // Handle interrupt register side effects
            return true;
        }
    }
    else if (address >= 0x12000000 && address < 0x12001000)
    {
        // GS registers
        std::cout << "GS register write: " << std::hex << address << " = " << value << std::dec << std::endl;
        // Handle GS register side effects
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

    // Special cases for reads from hardware registers that have side effects
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

    // Initialize the modified bitmap (one bit per 4-byte word)
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

        // Mark each 4-byte word in the overlap as modified
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

        // Calculate overlap
        uint32_t overlapStart = std::max(address, region.start);
        uint32_t overlapEnd = std::min(address + size, region.end);

        // Check each 4-byte word in the overlap
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

        // Calculate overlap
        uint32_t overlapStart = std::max(address, region.start);
        uint32_t overlapEnd = std::min(address + size, region.end);

        // Clear flags for each 4-byte word in the overlap
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

// ============================================
// HARDWARE LOGGING FUNCTIONS (called from macros)
// ============================================

static uint64_t hw_write32_count = 0;
static uint64_t hw_write64_count = 0;

void logHardwareWrite32(uint32_t addr, uint32_t val) {
    initGSLog();
    hw_write32_count++;

    // Categorize the write
    const char* category = "UNKNOWN";
    const char* regName = "";

    if (addr >= 0x10000000 && addr < 0x10001000) {
        category = "TIMER";
    } else if (addr >= 0x10002000 && addr < 0x10003000) {
        category = "IPU";
    } else if (addr >= 0x10003000 && addr < 0x10004000) {
        category = "GIF";
        uint32_t offset = addr & 0xFF;
        switch(offset) {
            case 0x00: regName = "CTRL"; break;
            case 0x10: regName = "MODE"; break;
            case 0x20: regName = "STAT"; break;
            case 0x40: regName = "TAG0"; break;
            case 0x50: regName = "TAG1"; break;
            case 0x60: regName = "TAG2"; break;
            case 0x70: regName = "TAG3"; break;
            case 0x80: regName = "CNT"; break;
            case 0x90: regName = "P3CNT"; break;
            case 0xA0: regName = "P3TAG"; break;
        }
    } else if (addr >= 0x10008000 && addr < 0x1000F000) {
        category = "DMA";
        int channel = ((addr >> 12) & 0xF) - 8;
        regName = getDMAChannelName(channel);
    } else if (addr >= 0x1000F000 && addr < 0x10010000) {
        category = "INTC";
    } else if (addr >= 0x12000000 && addr < 0x12002000) {
        category = "GS";
        uint32_t offset = addr - 0x12000000;
        regName = getGSPrivRegName(offset);
    }

    // Log to file
    if (gs_log_file.is_open()) {
        gs_log_file << "[HW32 #" << hw_write32_count << "] "
                   << category;
        if (regName[0] != '\0') {
            gs_log_file << "." << regName;
        }
        gs_log_file << " (0x" << std::hex << addr << ") = 0x" << val << std::dec << std::endl;
    }

    // Print first 50 to console
    if (hw_write32_count <= 50) {
        std::cout << "[HW32] " << category;
        if (regName[0] != '\0') std::cout << "." << regName;
        std::cout << " = 0x" << std::hex << val << std::dec << std::endl;
    } else if (hw_write32_count == 51) {
        std::cout << "[HW32] ... (see gs_trace.log)" << std::endl;
    }
}

void logHardwareWrite64(uint32_t addr, uint64_t val) {
    initGSLog();
    hw_write64_count++;

    // Categorize
    const char* category = "UNKNOWN";
    const char* regName = "";

    if (addr >= 0x12000000 && addr < 0x12002000) {
        category = "GS";
        uint32_t offset = addr - 0x12000000;
        regName = getGSPrivRegName(offset);
    } else if (addr >= 0x10003000 && addr < 0x10004000) {
        category = "GIF";
    }

    // Log to file
    if (gs_log_file.is_open()) {
        gs_log_file << "[HW64 #" << hw_write64_count << "] "
                   << category;
        if (regName[0] != '\0') {
            gs_log_file << "." << regName;
        }
        gs_log_file << " (0x" << std::hex << addr << ") = 0x" << val << std::dec << std::endl;
    }

    // Print first 50 to console
    if (hw_write64_count <= 50) {
        std::cout << "[HW64] " << category;
        if (regName[0] != '\0') std::cout << "." << regName;
        std::cout << " = 0x" << std::hex << val << std::dec << std::endl;
    } else if (hw_write64_count == 51) {
        std::cout << "[HW64] ... (see gs_trace.log)" << std::endl;
    }
}
