#ifndef PS2_RUNTIME_H
#define PS2_RUNTIME_H

#include <cstdint>
#include <vector>
#include <unordered_map>
#include <map>
#include <string>
#include <functional>
#include <immintrin.h> // For SSE/AVX instructions
#include <filesystem>
#include <iostream>
#include <iomanip>

// Cross-platform __m128i element access
// Note: These are also defined in ps2_runtime_macros.h for generated code
// Check if already defined to avoid redefinition
#ifndef M128I_U32
#ifdef _MSC_VER
    #define M128I_U32(v, i) ((v).m128i_u32[i])
    #define M128I_U64(v, i) ((v).m128i_u64[i])
    #define M128I_I32(v, i) ((v).m128i_i32[i])
    #define M128I_I64(v, i) ((v).m128i_i64[i])
#else
    // GCC/Clang - use union or intrinsics
    #ifndef M128I_UNION_DEFINED
    #define M128I_UNION_DEFINED
    union m128i_union_rt {
        __m128i v;
        uint32_t u32[4];
        int32_t i32[4];
        uint64_t u64[2];
        int64_t i64[2];
    };
    inline uint32_t m128i_get_u32_rt(__m128i v, int i) {
        m128i_union_rt u; u.v = v;
        return u.u32[i];
    }
    inline int32_t m128i_get_i32_rt(__m128i v, int i) {
        m128i_union_rt u; u.v = v;
        return u.i32[i];
    }
    inline uint64_t m128i_get_u64_rt(__m128i v, int i) {
        m128i_union_rt u; u.v = v;
        return u.u64[i];
    }
    inline int64_t m128i_get_i64_rt(__m128i v, int i) {
        m128i_union_rt u; u.v = v;
        return u.i64[i];
    }
    #endif
    #define M128I_U32(v, i) m128i_get_u32_rt(v, i)
    #define M128I_U64(v, i) m128i_get_u64_rt(v, i)
    #define M128I_I32(v, i) m128i_get_i32_rt(v, i)
    #define M128I_I64(v, i) m128i_get_i64_rt(v, i)
#endif
#endif

constexpr uint32_t PS2_RAM_SIZE = 32 * 1024 * 1024; // 32MB
constexpr uint32_t PS2_RAM_MASK = 0x1FFFFFF;        // Mask for 32MB alignment
constexpr uint32_t PS2_RAM_BASE = 0x00000000;       // Physical base of RDRAM
constexpr uint32_t PS2_SCRATCHPAD_BASE = 0x70000000;
constexpr uint32_t PS2_SCRATCHPAD_SIZE = 16 * 1024; // 16KB
constexpr uint32_t PS2_IO_BASE = 0x10000000;        // Base for many I/O regs (Timers, DMAC, INTC)
constexpr uint32_t PS2_IO_SIZE = 0x10000;           // 64KB
constexpr uint32_t PS2_BIOS_BASE = 0x1FC00000;      // Or BFC00000 depending on KSEG
constexpr uint32_t PS2_BIOS_SIZE = 4 * 1024 * 1024; // 4MB

constexpr uint32_t PS2_VU0_CODE_BASE = 0x11000000; // Base address as seen from EE
constexpr uint32_t PS2_VU0_DATA_BASE = 0x11004000;
constexpr uint32_t PS2_VU0_CODE_SIZE = 4 * 1024; // 4KB Micro Memory
constexpr uint32_t PS2_VU0_DATA_SIZE = 4 * 1024; // 4KB Data Memory (VU Mem)

constexpr uint32_t PS2_VU1_MEM_BASE = 0x11008000; // Base address as seen from EE
constexpr uint32_t PS2_VU1_CODE_SIZE = 16 * 1024; // 16KB Micro Memory
constexpr uint32_t PS2_VU1_DATA_SIZE = 16 * 1024; // 16KB Data Memory (VU Mem)
constexpr uint32_t PS2_VU1_CODE_BASE = 0x11008000;
constexpr uint32_t PS2_VU1_DATA_BASE = 0x1100C000;

constexpr uint32_t PS2_GS_BASE = 0x12000000;
constexpr uint32_t PS2_GS_PRIV_REG_BASE = 0x12000000; // GS Privileged Registers
constexpr uint32_t PS2_GS_PRIV_REG_SIZE = 0x2000;

#define PS2_FIO_O_RDONLY 0x0001
#define PS2_FIO_O_WRONLY 0x0002
#define PS2_FIO_O_RDWR 0x0003
#define PS2_FIO_O_APPEND 0x0100
#define PS2_FIO_O_CREAT 0x0200
#define PS2_FIO_O_TRUNC 0x0400
#define PS2_FIO_O_EXCL 0x0800

#define PS2_FIO_SEEK_SET 0
#define PS2_FIO_SEEK_CUR 1
#define PS2_FIO_SEEK_END 2

#define PS2_FIO_S_IFDIR 0x1000
#define PS2_FIO_S_IFREG 0x2000

enum PS2Exception
{
    EXCEPTION_INTEGER_OVERFLOW = 0x0C, // From MIPS spec
};

// PS2 CPU context (R5900)
struct alignas(16) R5900Context
{
    // General Purpose Registers (128-bit)
    __m128i r[32]; // Main registers

    // Control registers
    uint32_t pc;         // Program counter
    uint64_t insn_count; // Instruction counter
    uint64_t hi, lo;     // HI/LO registers for mult/div results
    uint64_t hi1, lo1;   // Secondary HI/LO registers for MULT1/DIV1
    uint32_t sa;         // Shift amount register

    // VU0 registers (when used in macro mode)
    __m128 vu0_vf[32];        // VU0 vector float registers
    uint16_t vi[16];          // VU0 vector integer registers
    float vu0_q;              // VU0 Q register (quotient)
    float vu0_p;              // VU0 P register (EFU result)
    float vu0_i;              // VU0 I register (integer value)
    __m128 vu0_r;             // VU0 R register
    __m128 vu0_acc;           // VU0 ACC accumulator register
    uint16_t vu0_status;      // VU0 status register
    uint32_t vu0_mac_flags;   // VU0 MAC flags
    uint32_t vu0_clip_flags;  // VU0 clipping flags
    uint32_t vu0_clip_flags2; // VU0 clipping flags
    uint32_t vu0_cmsar0;      // VU0 microprogram start address
    uint32_t vu0_cmsar1;      // VU0 microprogram start address
    uint32_t vu0_cmsar2;      // VU0 microprogram start address
    uint32_t vu0_cmsar3;      // VU0 microprogram start address
    uint32_t vu0_vpu_stat;
    uint32_t vu0_vpu_stat2; // extra VPU status (used by CR_VPU_STAT2)
    uint32_t vu0_vpu_stat3; // extra VPU status 3
    uint32_t vu0_vpu_stat4; // extra VPU status 4
    uint32_t vu0_tpc;       // TPC (VU0 PC)
    uint32_t vu0_tpc2;      // second TPC
    uint32_t vu0_fbrst;     // VIF/VU reset register
    uint32_t vu0_fbrst2;    // FBRST2
    uint32_t vu0_fbrst3;    // FBRST3
    uint32_t vu0_fbrst4;    // FBRST4
    uint32_t vu0_itop;
    uint32_t vu0_info;

    float vu0_cf[4]; // VU0 FMAC control floating-point registers

    // COP0 System control registers
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
    uint32_t cop0_status;
    uint32_t cop0_cause;
    uint32_t cop0_epc;
    uint32_t cop0_prid;
    uint32_t cop0_config;
    uint32_t cop0_badpaddr;
    uint32_t cop0_debug;
    uint32_t cop0_perf;
    uint32_t cop0_taglo;
    uint32_t cop0_taghi;
    uint32_t cop0_errorepc;

    // COP2 control registers (VU0 integer + control)
    uint32_t cop2_ccr[32];

    // FPU registers (COP1)
    float f[32];
    uint32_t fcr31; // Control/status register

    R5900Context()
    {
        for (int i = 0; i < 32; i++)
        {
            r[i] = _mm_setzero_si128();
            f[i] = 0.0f;
            vu0_vf[i] = _mm_setzero_ps();
        }

        for (int i = 0; i < 4; i++)
        {
            vu0_cf[i] = 0.0f;
        }

        for (int i = 0; i < 16; ++i)
        {
            vi[i] = 0;
        }

        pc = 0;
        insn_count = 0;
        lo = hi = lo1 = hi1 = 0;
        sa = 0;

        // Initialize VU0 registers
        vu0_q = 1.0f; // Q register usually initialized to 1.0
        vu0_p = 0.0f;
        vu0_i = 0.0f;
        vu0_r = _mm_setzero_ps();
        vu0_acc = _mm_setzero_ps();
        vu0_status = 0;
        vu0_mac_flags = 0;
        vu0_clip_flags = 0;
        vu0_cmsar0 = 0;
        vu0_fbrst = 0;
        vu0_fbrst2 = 0;
        vu0_fbrst3 = 0;
        vu0_fbrst4 = 0;

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

        vu0_tpc = 0;
        vu0_vpu_stat2 = 0;
        vu0_tpc2 = 0;
        vu0_cmsar1 = 0;
        vu0_vpu_stat3 = 0;
        vu0_cmsar2 = 0;
        vu0_vpu_stat4 = 0;
        vu0_itop = 0;
        vu0_info = 0;

        // Reset COP1 state
        fcr31 = 0;
    }

    void dump() const
    {
        std::ios_base::fmtflags flags = std::cout.flags();
        std::cout << std::hex << std::setfill('0');
        std::cout << "--- R5900 Context Dump ---\n";
        std::cout << "PC: 0x" << std::setw(8) << pc << "\n";
        std::cout << "HI: 0x" << std::setw(8) << hi << " LO: 0x" << std::setw(8) << lo << "\n";
        std::cout << "HI1:0x" << std::setw(8) << hi1 << " LO1:0x" << std::setw(8) << lo1 << "\n";
        std::cout << "SA: 0x" << std::setw(8) << sa << "\n";
        for (int i = 0; i < 32; ++i)
        {
            std::cout << "R" << std::setw(2) << std::dec << i << ": 0x" << std::hex
                      << std::setw(8) << M128I_U32(r[i], 3) << std::setw(8) << M128I_U32(r[i], 2) << "_"
                      << std::setw(8) << M128I_U32(r[i], 1) << std::setw(8) << M128I_U32(r[i], 0) << "\n";
        }
        std::cout << "Status: 0x" << std::setw(8) << cop0_status
                  << " Cause: 0x" << std::setw(8) << cop0_cause
                  << " EPC: 0x" << std::setw(8) << cop0_epc << "\n";
        std::cout << "--- End Context Dump ---\n";
        std::cout.flags(flags); // Restore format flags
    }

    ~R5900Context() = default;
};

inline uint32_t getRegU32(const R5900Context *ctx, int reg)
{
    // Check if reg is valid (0-31)
    if (reg < 0 || reg > 31)
        return 0;
    return M128I_U32(ctx->r[reg], 0);
}

inline void setReturnU32(R5900Context *ctx, uint32_t value)
{
    ctx->r[2] = _mm_set_epi32(0, 0, 0, value); // $v0
}

inline void setReturnS32(R5900Context *ctx, int32_t value)
{
    ctx->r[2] = _mm_set_epi32(0, 0, 0, value); // $v0 Sign extension handled by cast? TODO Check MIPS ABI.
}

inline void setReturnU64(R5900Context *ctx, uint64_t value)
{
    // 64-bit returns use $v0/$v1 (r2/r3)
    ctx->r[2] = _mm_set_epi32(0, 0, 0, static_cast<uint32_t>(value));
    ctx->r[3] = _mm_set_epi32(0, 0, 0, static_cast<uint32_t>(value >> 32));
}

inline uint8_t *getMemPtr(uint8_t *rdram, uint32_t addr)
{
    constexpr uint32_t PS2_RAM_MASK = PS2_RAM_SIZE - 1;
    return rdram + (addr & PS2_RAM_MASK);
}

inline const uint8_t *getConstMemPtr(uint8_t *rdram, uint32_t addr)
{
    constexpr uint32_t PS2_RAM_MASK = PS2_RAM_SIZE - 1;
    return rdram + (addr & PS2_RAM_MASK);
}

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
    bool isScratchpad(uint32_t address) const;
};

class PS2Runtime
{
public:
    PS2Runtime();
    ~PS2Runtime();

    bool initialize(const char *title = "PS2 Game");
    bool loadELF(const std::string &elfPath);
    void run();

    using RecompiledFunction = void (*)(uint8_t *, R5900Context *, PS2Runtime *);

    void registerFunction(uint32_t address, RecompiledFunction func);
    RecompiledFunction lookupFunction(uint32_t address);

    void SignalException(R5900Context *ctx, PS2Exception exception);

    void executeVU0Microprogram(uint8_t *rdram, R5900Context *ctx, uint32_t address);
    void vu0StartMicroProgram(uint8_t *rdram, R5900Context *ctx, uint32_t address);

public:
    void handleSyscall(uint8_t *rdram, R5900Context *ctx);
    void handleBreak(uint8_t *rdram, R5900Context *ctx);

    void handleTrap(uint8_t *rdram, R5900Context *ctx);
    void handleTLBR(uint8_t *rdram, R5900Context *ctx);
    void handleTLBWI(uint8_t *rdram, R5900Context *ctx);
    void handleTLBWR(uint8_t *rdram, R5900Context *ctx);
    void handleTLBP(uint8_t *rdram, R5900Context *ctx);
    void clearLLBit(R5900Context *ctx);

public:
    inline R5900Context &cpu() { return m_cpuContext; }
    inline const R5900Context &cpu() const { return m_cpuContext; }

    inline PS2Memory &memory() { return m_memory; }
    inline const PS2Memory &memory() const { return m_memory; }

public:
    bool check_overflow = false;

private:
    void HandleIntegerOverflow(R5900Context *ctx);

private:
    PS2Memory m_memory;
    R5900Context m_cpuContext;

    std::unordered_map<uint32_t, RecompiledFunction> m_functionTable;
    std::map<uint32_t, RecompiledFunction> m_sortedFunctionTable; // For range lookup

    // Custom syscall handlers registered via SetSyscall
    std::unordered_map<uint32_t, uint32_t> m_customSyscalls;

    // Interrupt handlers registered via AddIntcHandler
    // Key: INTC cause (0=GS, 2=VBLANK_S, 3=VBLANK_E, etc.)
    // Value: handler address
    std::unordered_map<uint32_t, uint32_t> m_intcHandlers;

    // Track enabled interrupts
    uint32_t m_intcMask = 0;

public:
    // Trigger a VBlank interrupt (call from render loop)
    void triggerVBlank();

    // Register an interrupt handler
    int addIntcHandler(uint32_t cause, uint32_t handler);

private:
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
