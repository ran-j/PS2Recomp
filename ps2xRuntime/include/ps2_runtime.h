#ifndef PS2_RUNTIME_H
#define PS2_RUNTIME_H

#include <cstdint>
#include <vector>
#include <unordered_map>
#include <string>
#include <functional>
#include <immintrin.h> // For SSE/AVX instructions

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

    // VU0 registers (when used in macro mode)
    __m128 vu0_vf[32];
    float vu0_acc[4];    // VU0 ACC (accumulator)
    float vu0_q;         // VU0 Q register (quotient)
    float vu0_p;         // VU0 P register
    uint16_t vu0_status; // VU0 status/flags
 
    // COP0 System control registers (some critical ones)
    uint32_t cop0_registers[32];
    uint32_t cop0_status;    // Status register
    uint32_t cop0_cause;     // Cause register
    uint32_t cop0_epc;       // Exception PC

    // FPU registers (COP1)
    float f[32];

    // FPU control registers
    uint32_t fcr0;  // Implementation/revision register
    uint32_t fcr31; // Control/status register
};

class PS2Memory
{
public:
    PS2Memory();
    ~PS2Memory();

    // Initialize memory
    bool initialize(size_t ramSize = 32 * 1024 * 1024);

    // Memory access methods
    uint8_t *getRDRAM() { return m_rdram; }
    uint8_t *getScratchpad() { return m_scratchpad; }

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

private:
    // Main RAM (32MB)
    uint8_t *m_rdram;

    // Scratchpad (16KB)
    uint8_t *m_scratchpad;

    // I/O registers
    std::unordered_map<uint32_t, uint32_t> m_ioRegisters;

    // TLB entries
    struct TLBEntry
    {
        uint32_t vpn;
        uint32_t pfn;
        uint32_t mask;
        bool valid;
    };

    std::vector<TLBEntry> m_tlbEntries;
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