#ifndef PS2_RUNTIME_H
#define PS2_RUNTIME_H

#include <cstdint>
#include <vector>
#include <unordered_map>
#include <string>
#include <functional>
#include <immintrin.h> // For SSE/AVX instructions

#define PS2_RAM_SIZE 0x2000000     // 32MB
#define PS2_SCRATCHPAD_SIZE 0x4000 // 16KB

// PS2 CPU context (R5900)
struct R5900Context
{
    // General Purpose Registers (128-bit)
    __m128i r[32];

    // Program Counter Hi/Lo registers (64-bit)
    uint32_t pc;
    uint32_t hi; // High result register
    uint32_t lo; // Low result register
    uint32_t sa; // Shift amount register

    // 128-bit registers for SIMD operations
    __m128i r128[32];

    // VU0 registers (when used in macro mode)
    __m128 vu0_vf[32];
    uint16_t vu0_status; // VU0 status/flags
    float vu0_acc[4];    // VU0 ACC (accumulator)
    float vu0_q;         // VU0 Q register (quotient)
    float vu0_p;         // VU0 P register
    float vu0_i;
    float vu0_mac;
    float vu0_clip;

    // COP0 System control registers (some critical ones)
    uint32_t cop0_registers[32];
    uint32_t cop0_status; // Status register
    uint32_t cop0_cause;  // Cause register
    uint32_t cop0_epc;    // Exception PC
    uint32_t cop0_prid;

    // FPU registers (COP1)
    float f[32];
    uint32_t fcr0;  // Implementation/revision register
    uint32_t fcr31; // Control/status register

    // Branch delay slot tracking
    bool in_delay_slot;
    uint32_t branch_target;

    // Function call stack for profiling and debugging
    uint32_t call_stack[16];
    int call_stack_depth;
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