#ifndef PS2_RUNTIME_H
#define PS2_RUNTIME_H

#include <cstdint>
#include <vector>
#include <unordered_map>
#include <string>
#include <functional>
#include <immintrin.h> // For SSE/AVX instructions

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

// PS2 CPU context (R5900)
struct R5900Context
{
    // General Purpose Registers (128-bit)
    __m128i r[32];

    // Program Counter Hi/Lo registers (64-bit)
    uint32_t pc;
    uint64_t insn_count;
    uint32_t hi; // High result register
    uint32_t lo; // Low result register
    uint32_t sa; // Shift amount register

    uint32_t lo1, hi1; // For MULT1/DIV1 instructions

    // 128-bit registers for SIMD operations
    __m128i r128[32];

    // VU0 registers (when used in macro mode)
    __m128 vu0_vf[32];
    uint16_t vu0_status; // VU0 status/flags
    float vu0_acc[4];    // VU0 ACC (accumulator)
    float vu0_q;         // VU0 Q register (quotient)
    float vu0_p;         // VU0 P register
    float vu0_i;
    __m128 vu0_r;            // VU0 R register (special purpose register)
    uint32_t vu0_mac_flags;  // VU0 MAC flags
    uint32_t vu0_clip_flags; // VU0 clipping flags
    uint32_t vu0_cmsar0;     // VU0 microprogram start address
    uint32_t vu0_fbrst;      // VIF/VU reset register
    float vu0_cf[4];         // VU0 FMAC control floating-point registers

    // COP0 System control registers (some critical ones)
    uint32_t cop0_registers[32];
    uint32_t cop0_status; // Status register
    uint32_t cop0_cause;  // Cause register
    uint32_t cop0_epc;    // Exception PC
    uint32_t cop0_prid;
    uint32_t cop0_index;
    uint32_t cop0_random;
    uint32_t cop0_entrylo0;
    uint32_t cop0_entrylo1;
    uint32_t cop0_context;
    uint32_t cop0_pagemask;
    uint32_t cop0_wired;
    uint32_t cop0_badvaddr;
    uint32_t cop0_count;
    uint32_t cop0_entryhi;
    uint32_t cop0_compare;
    uint32_t cop0_config;
    uint32_t cop0_badpaddr;
    uint32_t cop0_debug;
    uint32_t cop0_perf;
    uint32_t cop0_taglo;
    uint32_t cop0_taghi;
    uint32_t cop0_errorepc;

    // FPU registers (COP1)
    float f[32];
    uint32_t fcr0;  // Implementation/revision register
    uint32_t fcr31; // Control/status register

    R5900Context()
    {
        // Zero all registers
        for (int i = 0; i < 32; i++)
        {
            r[i] = _mm_setzero_si128();
            f[i] = 0.0f;
            vu0_vf[i] = _mm_setzero_ps();
        }

        pc = 0;
        insn_count = 0;
        lo = hi = lo1 = hi1 = 0;
        sa = 0;

        // Reset COP0 registers
        cop0_index = 0;
        cop0_random = 47; // Start at maximum value
        cop0_entrylo0 = 0;
        cop0_entrylo1 = 0;
        cop0_context = 0;
        cop0_pagemask = 0;
        cop0_wired = 0;
        cop0_badvaddr = 0;
        cop0_count = 0;
        cop0_entryhi = 0;
        cop0_compare = 0;
        cop0_status = 0x400000; // BEV set, ERL clear, kernel mode
        cop0_cause = 0;
        cop0_epc = 0;
        cop0_prid = 0x00002e20; // CPU ID for R5900
        cop0_config = 0;
        cop0_badpaddr = 0;
        cop0_debug = 0;
        cop0_perf = 0;
        cop0_taglo = 0;
        cop0_taghi = 0;
        cop0_errorepc = 0;

        // Reset COP1/VU0 state
        fcr31 = 0;
        vu0_q = 0.0f;
        vu0_i = 0.0f;
        vu0_status = 0;

        // Set the FPU registers to predictable but non-zero values
        for (int i = 0; i < 32; i++)
        {
            f[i] = static_cast<float>(i);
        }
    }
};

// PS2 GS (Graphics Synthesizer) registers
struct GSRegisters
{
    uint64_t pmode;    // Pixel mode
    uint64_t smode1;   // Sync mode 1
    uint64_t smode2;   // Sync mode 2
    uint64_t srfsh;    // Refresh control
    uint64_t synch1;   // Synchronization control 1
    uint64_t synch2;   // Synchronization control 2
    uint64_t syncv;    // Synchronization control V
    uint64_t dispfb1;  // Display buffer 1
    uint64_t display1; // Display area 1
    uint64_t dispfb2;  // Display buffer 2
    uint64_t display2; // Display area 2
    uint64_t extbuf;   // External buffer
    uint64_t extdata;  // External data
    uint64_t extwrite; // External write
    uint64_t bgcolor;  // Background color
    uint64_t csr;      // Status
    uint64_t imr;      // Interrupt mask
    uint64_t busdir;   // Bus direction
    uint64_t siglblid; // Signal label ID
};

// PS2 VIF (VPU Interface) registers
struct VIFRegisters
{
    uint32_t stat;   // Status
    uint32_t fbrst;  // VIF Force Break
    uint32_t err;    // Error status
    uint32_t mark;   // Interrupt control
    uint32_t cycle;  // Transfer mode
    uint32_t mode;   // Mode control
    uint32_t num;    // Data amount counter
    uint32_t mask;   // Data mask
    uint32_t code;   // VIFcode
    uint32_t itops;  // ITOP save
    uint32_t base;   // Base address
    uint32_t ofst;   // Offset
    uint32_t tops;   // TOPS
    uint32_t itop;   // ITOP
    uint32_t top;    // TOP
    uint32_t row[4]; // Transfer row data
    uint32_t col[4]; // Transfer column data
};

// PS2 DMA registers
struct DMARegisters
{
    uint32_t chcr; // Channel control
    uint32_t madr; // Memory address
    uint32_t qwc;  // Quadword count
    uint32_t tadr; // Tag address
    uint32_t asr0; // Address stack 0
    uint32_t asr1; // Address stack 1
    uint32_t sadr; // Source address
};

struct JumpTable
{
    uint32_t address;              // Base address of the jump table
    uint32_t baseRegister;         // Register used for index
    std::vector<uint32_t> targets; // Jump targets
};

class PS2Memory
{
public:
    PS2Memory();
    ~PS2Memory();

    // Initialize memory
    bool initialize(size_t ramSize = PS2_RAM_SIZE);

    // Memory access methods
    uint8_t *getRDRAM() { return m_rdram; }
    uint8_t *getScratchpad() { return m_scratchpad; }
    uint8_t *getIOPRAM() { return iop_ram; }

    // Read/write memory
    uint8_t read8(uint32_t address);
    uint16_t read16(uint32_t address);
    uint32_t read32(uint32_t address);
    uint64_t read64(uint32_t address);
    __m128i read128(uint32_t address);

    void write8(uint32_t address, uint8_t value);
    void write16(uint32_t address, uint16_t value);
    void write32(uint32_t address, uint32_t value);
    void write64(uint32_t address, uint64_t value);
    void write128(uint32_t address, __m128i value);

    // TLB handling
    uint32_t translateAddress(uint32_t virtualAddress);

    // Hardware register interface
    bool writeIORegister(uint32_t address, uint32_t value);
    uint32_t readIORegister(uint32_t address);

    // Track code modifications for self-modifying code
    void registerCodeRegion(uint32_t start, uint32_t end);
    bool isCodeModified(uint32_t address, uint32_t size);
    void clearModifiedFlag(uint32_t address, uint32_t size);

private:
    // Main RAM (32MB)
    uint8_t *m_rdram;

    // Scratchpad memory (16KB)
    uint8_t *m_scratchpad;

    // IOP RAM (2MB)
    uint8_t *iop_ram;

    // I/O registers
    std::unordered_map<uint32_t, uint32_t> m_ioRegisters;

    // Registers
    GSRegisters gs_regs;
    VIFRegisters vif0_regs;
    VIFRegisters vif1_regs;
    DMARegisters dma_regs[10]; // 10 DMA channels

    // TLB entries
    struct TLBEntry
    {
        uint32_t vpn;
        uint32_t pfn;
        uint32_t mask;
        bool valid;
    };

    std::vector<TLBEntry> m_tlbEntries;

    struct CodeRegion
    {
        uint32_t start;
        uint32_t end;
        std::vector<bool> modified; // Bitmap of modified 4-byte blocks
    };
    std::vector<CodeRegion> m_codeRegions;

    bool isAddressInRegion(uint32_t address, const CodeRegion &region);
    void markModified(uint32_t address, uint32_t size);
};

class PS2Runtime
{
public:
    PS2Runtime();
    ~PS2Runtime();

    bool initialize();
    bool loadELF(const std::string &elfPath);
    void run();

    using RecompiledFunction = void (*)(uint8_t *, R5900Context *);
    void registerFunction(uint32_t address, RecompiledFunction func);
    RecompiledFunction lookupFunction(uint32_t address);

    void registerBuiltinStubs();

private:
    PS2Memory m_memory;
    R5900Context m_cpuContext;

    // Function table for recompiled code
    std::unordered_map<uint32_t, RecompiledFunction> m_functionTable;

    // Currently loaded modules
    struct LoadedModule
    {
        std::string name;
        uint32_t baseAddress;
        size_t size;
        bool active;
    };

    std::vector<LoadedModule> m_loadedModules;
};

#endif // PS2_RUNTIME_H