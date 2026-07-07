#ifndef PS2_RUNTIME_H
#define PS2_RUNTIME_H

#include <cstring>
#include <cstdint>
#include <vector>
#include <string>
#include <functional>
#if defined(_MSC_VER)
#include <intrin.h>
#elif defined(USE_SSE2NEON)
#include "sse2neon.h"
#else
#include <immintrin.h> // For SSE/AVX instructions
#include <smmintrin.h> // For SSE4.1 instructions
#endif
#include <atomic>
#include <array>
#include <mutex>
#include <condition_variable>
#include <filesystem>
#include <iostream>
#include <iomanip>

#include "ps2_log.h"
#include "ps2_scheduler.h"
#include "runtime/ps2_address.h"
#include "runtime/ps2_gif_arbiter.h"
#include "runtime/ps2_memory.h"
#include "runtime/ps2_gs_gpu.h"
#include "runtime/ps2_iop.h"
#include "runtime/ps2_vu1.h"
#include "runtime/ps2_audio.h"
#include "runtime/ps2_pad.h"

enum PS2Exception
{
    EXCEPTION_TLB_REFILL = 0x02,          // TLB refill/load exception
    EXCEPTION_ADDRESS_ERROR_LOAD = 0x04,  // Address error on load
    EXCEPTION_ADDRESS_ERROR_STORE = 0x05, // Address error on store
    EXCEPTION_SYSCALL = 0x08,             // SYSCALL instruction
    EXCEPTION_BREAKPOINT = 0x09,          // BREAK instruction
    EXCEPTION_RESERVED_INSTRUCTION = 0x0A,
    EXCEPTION_INTEGER_OVERFLOW = 0x0C, // From MIPS spec
    EXCEPTION_TRAP = 0x0D,             // Trap instruction condition met
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
    uint32_t vu0_top;
    uint32_t vu0_info;
    uint32_t vu0_xitop; // VU0 XITOP - input ITOP for VIF/VU sync
    uint32_t vu0_pc;

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

    // LL/SC reservation state (not part of COP0 Status bits).
    uint32_t llbit;
    uint32_t lladdr;

    // Delay slot state tracking
    bool in_delay_slot;
    uint32_t branch_pc;

    // COP2 control registers (VU0 integer + control)
    uint32_t cop2_ccr[32];

    // FPU registers (COP1)
    float f[32];
    float f_acc; // FPU accumulator
    uint32_t fcr31; // Control/status register

    R5900Context()
    {
        std::memset(this, 0, sizeof(*this));

        // Initialize VU0 registers
        vu0_q = 1.0f; // Q register usually initialized to 1.0

        // Reset COP0 registers
        cop0_random = 47; // Start at maximum value
        // cop0_status = 0x400000; // BEV set, ERL clear, kernel mode
        // 0x00400000 = BEV (Boot Exception Vectors).
        // 0x00000000 = Normal mode (after BIOS handoff).
        cop0_status = 0x00000000;
        cop0_prid = 0x00002e20; // CPU ID for R5900

        in_delay_slot = false;
        branch_pc = 0;
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
                      << std::setw(8) << static_cast<uint32_t>(_mm_extract_epi32(r[i], 3))
                      << std::setw(8) << static_cast<uint32_t>(_mm_extract_epi32(r[i], 2)) << "_"
                      << std::setw(8) << static_cast<uint32_t>(_mm_extract_epi32(r[i], 1))
                      << std::setw(8) << static_cast<uint32_t>(_mm_extract_epi32(r[i], 0)) << "\n";
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
    if (reg == 0)
        return 0;
    return static_cast<uint32_t>(_mm_extract_epi32(ctx->r[reg], 0));
}

inline void setReturnU32(R5900Context *ctx, uint32_t value)
{
    // R5900 sign-extends 32-bit results into 64-bit GPR, even for unsigned values.
    ctx->r[2] = _mm_set_epi64x(0, static_cast<int64_t>(static_cast<int32_t>(value))); // $v0
}

inline void setReturnS32(R5900Context *ctx, int32_t value)
{
    // Signed 32-bit return should be sign-extended when observed as 64-bit.
    ctx->r[2] = _mm_set_epi64x(0, static_cast<int64_t>(value)); // $v0
}

inline void setReturnU64(R5900Context *ctx, uint64_t value)
{
    // Keep both conventions: full 64-bit value in $v0 and high 32-bit in $v1.
    ctx->r[2] = _mm_set_epi64x(0, static_cast<int64_t>(value));
    ctx->r[3] = _mm_set_epi64x(0, static_cast<int64_t>(static_cast<uint32_t>(value >> 32)));
}

inline constexpr uint32_t PS2_PATH_WATCH_ADDR = 0x01EFFFA0u;
inline constexpr uint32_t PS2_PATH_WATCH_BYTES = 0x200u;

inline uint32_t ps2PathWatchPhysAddr()
{
    return PS2_PATH_WATCH_ADDR & PS2_RAM_MASK;
}

inline uint8_t ps2PathWatchExtractByteFromWrite(uint32_t writeAddr, uint32_t watchAddr, uint64_t valueLo, uint64_t valueHi)
{
    const uint32_t byteIndex = watchAddr - writeAddr;
    if (byteIndex < 8u)
    {
        return static_cast<uint8_t>((valueLo >> (byteIndex * 8u)) & 0xFFu);
    }
    return static_cast<uint8_t>((valueHi >> ((byteIndex - 8u) * 8u)) & 0xFFu);
}

inline void ps2TraceGuestWrite(uint8_t *rdram,
                               uint32_t guestAddr,
                               uint32_t size,
                               uint64_t valueLo,
                               uint64_t valueHi,
                               const char *op,
                               const R5900Context *ctx)
{
    (void)rdram;
    (void)guestAddr;
    (void)size;
    (void)valueLo;
    (void)valueHi;
    (void)op;
    (void)ctx;
    // TODO we dont need this anymore so on next release it will be deleted
}

inline void ps2TraceGuestRangeWrite(uint8_t *rdram,
                                    uint32_t guestAddr,
                                    uint32_t size,
                                    const char *op,
                                    const R5900Context *ctx)
{
    (void)rdram;
    (void)guestAddr;
    (void)size;
    (void)op;
    (void)ctx;
    // TODO we dont need this anymore so on next release it will be deleted
}

struct PS2SoundDriverCompatLayout
{
    uint32_t primarySeCheckAddr = 0;
    uint32_t primaryMidiCheckAddr = 0;
    uint32_t fallbackSeCheckAddr = 0;
    uint32_t fallbackMidiCheckAddr = 0;
    uint32_t busyFlagAddr = 0;
    std::array<uint32_t, 4> completionCallbacks{};
    std::array<uint32_t, 2> clearBusyCallbacks{};

    [[nodiscard]] bool hasChecksumTables() const
    {
        return primarySeCheckAddr != 0u || primaryMidiCheckAddr != 0u ||
               fallbackSeCheckAddr != 0u || fallbackMidiCheckAddr != 0u;
    }

    [[nodiscard]] bool matchesCompletionCallback(uint32_t addr) const
    {
        for (const uint32_t candidate : completionCallbacks)
        {
            if (candidate != 0u && candidate == addr)
            {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool matchesClearBusyCallback(uint32_t addr) const
    {
        for (const uint32_t candidate : clearBusyCallbacks)
        {
            if (candidate != 0u && candidate == addr)
            {
                return true;
            }
        }
        return false;
    }
};

struct PS2DtxCompatLayout
{
    uint32_t rpcSid = 0;
    uint32_t urpcObjBase = 0;
    uint32_t urpcObjLimit = 0;
    uint32_t urpcObjStride = 0x20u;
    uint32_t urpcFnTableBase = 0;
    uint32_t urpcObjTableBase = 0;
    uint32_t dispatcherFuncAddr = 0;

    [[nodiscard]] bool isConfigured() const
    {
        return rpcSid != 0u;
    }

    [[nodiscard]] bool hasUrpcObjectRange() const
    {
        return urpcObjBase != 0u && urpcObjLimit > urpcObjBase && urpcObjStride != 0u;
    }

    [[nodiscard]] bool hasUrpcTables() const
    {
        return urpcFnTableBase != 0u && urpcObjTableBase != 0u;
    }

    [[nodiscard]] bool isUrpcRpc(uint32_t sid, uint32_t rpcNum) const
    {
        return isConfigured() && sid == rpcSid && rpcNum >= 0x400u && rpcNum < 0x500u;
    }
};

// Async callback stack pool [floor, top): kernel-reserved guest memory. See
// the pool layout comment in ps2_runtime.cpp for why this range is disjoint
// from every other guest allocation.
constexpr uint32_t kAsyncCallbackStackFloor = 0x00080000u;
constexpr uint32_t kAsyncCallbackStackTop = 0x00100000u;

// $sp for a host-dispatched guest callback that could not get a stack out of
// the async-callback pool (pool exhausted or runtime unavailable). Top of the
// kernel-reserved pool, NOT PS2_RAM_SIZE-0x10 — that address is inside the
// guest's own main stack (games place it at top-of-RAM via SetupThread), and
// running a handler there would corrupt live guest frames.
constexpr uint32_t kAsyncCallbackFallbackSp = kAsyncCallbackStackTop - 0x10u;

// Encapsulates the async-callback stack pool's carve state. The floor is a
// compile-time constant (nothing ever moves it); `top` is the only runtime
// state — the downward-carving cursor — and is what loadELF() re-arms on
// each load via reset(). See the pool layout comment in ps2_runtime.cpp for
// the disjointness rationale.
struct KernelStackPool
{
    static constexpr uint32_t floor = kAsyncCallbackStackFloor;
    uint32_t top = kAsyncCallbackStackTop;

    void reset() { top = kAsyncCallbackStackTop; }

    // Carves an aligned [base, top) region of `size` bytes off the top of the
    // pool and returns the guest $sp (top - 0x10) for it, or 0 if the pool is
    // exhausted. `size` must already be alignGuestHeapValue()-rounded by the
    // caller; `align` is normalized here via PS2Runtime::normalizeGuestHeapAlignment.
    uint32_t carve(uint32_t size, uint32_t align);
};

class PS2Runtime
{
public:
    struct IoPaths
    {
        std::filesystem::path elfPath;
        std::filesystem::path elfDirectory;
        std::filesystem::path hostRoot;
        std::filesystem::path cdRoot;
        std::filesystem::path mcRoot;
        std::filesystem::path cdImage;
    };

    PS2Runtime();
    ~PS2Runtime();

    bool initialize(const char *title = "PS2 Game");
    bool syncCoreSubsystems();
    bool loadELF(const std::string &elfPath);
    void run();

    using DebugUiCallback = void (*)(PS2Runtime &runtime, void *userData);
    void setDebugUiCallbacks(DebugUiCallback initCallback,
                             DebugUiCallback drawCallback,
                             DebugUiCallback shutdownCallback,
                             void *userData);

    using RecompiledFunction = void (*)(uint8_t *, R5900Context *, PS2Runtime *);

    enum class GuestBranchKind
    {
        DirectJump,
        DirectCall,
        IndirectJump,
        IndirectCall,
        Return,
    };

    enum class MissingFunctionPolicy : uint32_t
    {
        // Strict mode for tests/CI: log the bad target and request the runtime to stop.
        Stop = 0,

        // Debug mode: log once, leave ctx->pc on the bad target, and let the caller unwind.
        ContinueToTarget = 1,

        // Debug mode: same as ContinueToTarget, but triggers a debugger break once on MSVC.
        BreakOnce = 2,

        // Escape hatch only: skip missing calls by returning to fallthrough (it can hide guest bugs)
        SkipCallDebug = 3,
    };

    // No-op RAII guards. Only one fiber ever executes guest code at a time
    // under the N=1 cooperative scheduler, and exclusion between the fiber
    // executor and borrowed host worker threads is provided by
    // ps2sched::async_guest_begin/async_guest_end (AsyncGuestScope). Kept as
    // no-ops only so code that still references them (MPEG/IPU decoder stubs)
    // compiles unchanged.
    class GuestExecutionScope
    {
    public:
        explicit GuestExecutionScope(PS2Runtime *) noexcept {}
        ~GuestExecutionScope() = default;

        GuestExecutionScope(const GuestExecutionScope &) = delete;
        GuestExecutionScope &operator=(const GuestExecutionScope &) = delete;
    };

    class GuestExecutionReleaseScope
    {
    public:
        explicit GuestExecutionReleaseScope(PS2Runtime *) noexcept {}
        ~GuestExecutionReleaseScope() = default;

        GuestExecutionReleaseScope(const GuestExecutionReleaseScope &) = delete;
        GuestExecutionReleaseScope &operator=(const GuestExecutionReleaseScope &) = delete;
    };

    bool replaceFunction(uint32_t address, RecompiledFunction func);
    // TODO remove this later need to update all tests
    bool registerFunction(uint32_t address, RecompiledFunction func);
    RecompiledFunction lookupFunction(uint32_t address);
    bool hasFunction(uint32_t address) const;
    bool dispatchGuestBranch(uint8_t *rdram,
                             R5900Context *ctx,
                             uint32_t targetPc,
                             uint32_t sourcePc,
                             uint32_t fallthroughPc,
                             GuestBranchKind kind,
                             const char *debugName);
    void reportMissingFunction(uint8_t *rdram,
                               R5900Context *ctx,
                               uint32_t targetPc,
                               uint32_t sourcePc,
                               GuestBranchKind kind,
                               const char *debugName);
    void setMissingFunctionPolicy(MissingFunctionPolicy policy);
    MissingFunctionPolicy missingFunctionPolicy() const;
    void resetMissingFunctionReportOnce();

    // Test/diagnostic seam: formats the dispatch-trace ring owned by whichever
    // fiber (or per-OS-thread fallback) is executing on the calling thread.
    // MUST only be called from inside a running fiber's step function — from
    // any other thread it returns the non-fiber fallback, not a fiber's trace.
    std::string debugCurrentDispatchTrace() const;

    static const IoPaths &getIoPaths();
    static void setIoPaths(const IoPaths &paths);
    static void configureIoPathsFromElf(const std::string &elfPath);

    void SignalException(R5900Context *ctx, PS2Exception exception);

    void executeVU0Microprogram(uint8_t *rdram, R5900Context *ctx, uint32_t address);
    void vu0StartMicroProgram(uint8_t *rdram, R5900Context *ctx, uint32_t address);

public:
    void handleSyscall(uint8_t *rdram, R5900Context *ctx);
    void handleSyscall(uint8_t *rdram, R5900Context *ctx, uint32_t encodedSyscallId);
    void handleBreak(uint8_t *rdram, R5900Context *ctx);

    void handleTrap(uint8_t *rdram, R5900Context *ctx);
    void handleTLBR(uint8_t *rdram, R5900Context *ctx);
    void handleTLBWI(uint8_t *rdram, R5900Context *ctx);
    void handleTLBWR(uint8_t *rdram, R5900Context *ctx);
    void handleTLBP(uint8_t *rdram, R5900Context *ctx);
    void clearLLBit(R5900Context *ctx);
    void configureGuestHeap(uint32_t guestBase, uint32_t guestLimit = PS2_RAM_SIZE);
    uint32_t guestMalloc(uint32_t size, uint32_t alignment = 16u);
    uint32_t guestCalloc(uint32_t count, uint32_t size, uint32_t alignment = 16u);
    uint32_t guestRealloc(uint32_t guestAddr, uint32_t newSize, uint32_t alignment = 16u);
    void guestFree(uint32_t guestAddr);
    uint32_t guestHeapBase() const;
    uint32_t guestHeapEnd() const;
    uint32_t guestHeapLimit() const;
    uint32_t reserveAsyncCallbackStack(uint32_t size, uint32_t alignment = 16u);
    void dispatchLoop(uint8_t *rdram, R5900Context *ctx);
    void drainCompletedDmacHandlers(uint8_t *rdram);
    bool shouldPreemptGuestExecution();
    void requestStop();
    void requestStopFlagOnly();
    bool isStopRequested() const;

    uint8_t Load8(uint8_t *rdram, R5900Context *ctx, uint32_t vaddr);
    uint16_t Load16(uint8_t *rdram, R5900Context *ctx, uint32_t vaddr);
    uint32_t Load32(uint8_t *rdram, R5900Context *ctx, uint32_t vaddr);
    uint64_t Load64(uint8_t *rdram, R5900Context *ctx, uint32_t vaddr);
    __m128i Load128(uint8_t *rdram, R5900Context *ctx, uint32_t vaddr);

    void Store8(uint8_t *rdram, R5900Context *ctx, uint32_t vaddr, uint8_t value);
    void Store16(uint8_t *rdram, R5900Context *ctx, uint32_t vaddr, uint16_t value);
    void Store32(uint8_t *rdram, R5900Context *ctx, uint32_t vaddr, uint32_t value);
    void Store64(uint8_t *rdram, R5900Context *ctx, uint32_t vaddr, uint64_t value);
    void Store128(uint8_t *rdram, R5900Context *ctx, uint32_t vaddr, __m128i value);
    void kickGifDmaChainFromMMIO(uint8_t *rdram,
                                 R5900Context *ctx,
                                 uint32_t dPcrValue,
                                 uint32_t dStatValue,
                                 uint32_t tadr,
                                 uint32_t chcr);

    static inline bool isSpecialAddress(uint32_t addr)
    {
        return Ps2IsSpecialAddress(addr);
    }

public:
    inline R5900Context &cpu() { return m_cpuContext; }
    inline const R5900Context &cpu() const { return m_cpuContext; }

    inline PS2Memory &memory() { return m_memory; }
    inline const PS2Memory &memory() const { return m_memory; }

    inline GS &gs() { return m_gs; }
    inline const GS &gs() const { return m_gs; }
    inline GifArbiter &gifArbiter() { return m_gifArbiter; }
    inline const GifArbiter &gifArbiter() const { return m_gifArbiter; }
    inline VU1Interpreter &vu0() { return m_vu0; }
    inline const VU1Interpreter &vu0() const { return m_vu0; }
    inline VU1Interpreter &vu1() { return m_vu1; }
    inline const VU1Interpreter &vu1() const { return m_vu1; }

    inline ps2_iop &iop() { return m_iop; }
    inline const ps2_iop &iop() const { return m_iop; }
    inline PS2AudioBackend &audioBackend() { return m_audioBackend; }
    inline const PS2AudioBackend &audioBackend() const { return m_audioBackend; }
    inline PSPadBackend &padBackend() { return m_padBackend; }
    inline const PSPadBackend &padBackend() const { return m_padBackend; }

private:
    struct GuestHeapBlock
    {
        uint32_t addr = 0;
        uint32_t size = 0;
        bool free = true;
    };

    static uint32_t alignGuestHeapValue(uint32_t value, uint32_t alignment);
    static bool isGuestHeapAlignmentValid(uint32_t alignment);
    static uint32_t normalizeGuestHeapAlignment(uint32_t alignment);
    uint32_t clampGuestHeapBase(uint32_t guestBase) const;
    uint32_t clampGuestHeapLimit(uint32_t guestLimit) const;
    void resetGuestHeapLocked(uint32_t guestBase, uint32_t guestLimit);
    void ensureGuestHeapInitializedLocked();
    int32_t findGuestHeapBlockIndexLocked(uint32_t guestAddr) const;
    uint32_t allocateGuestBlockLocked(uint32_t size, uint32_t alignment);
    void freeGuestBlockLocked(uint32_t guestAddr);
    void coalesceGuestHeapLocked();

    void HandleIntegerOverflow(R5900Context *ctx);

private:
    PS2Memory m_memory;
    GifArbiter m_gifArbiter;
    GS m_gs;
    ps2_iop m_iop;
    PS2AudioBackend m_audioBackend;
    PSPadBackend m_padBackend;
    VU1Interpreter m_vu0;
    VU1Interpreter m_vu1;
    R5900Context m_cpuContext;
    mutable std::mutex m_guestHeapMutex;
    mutable std::mutex m_asyncCallbackStackMutex;
    std::vector<GuestHeapBlock> m_guestHeapBlocks;
    uint32_t m_guestHeapBase = 0x00100000u;
    uint32_t m_guestHeapEnd = 0x00100000u;
    uint32_t m_guestHeapLimit = PS2_RAM_SIZE;
    uint32_t m_guestHeapSuggestedBase = 0x00100000u;
    bool m_guestHeapConfigured = false;
    // Async callback stack pool [floor, top): kernel-reserved guest memory,
    // carved downward. See the pool layout comment in ps2_runtime.cpp (near
    // kAsyncCallbackStackFloor's namespace-level block) for the full
    // disjointness rationale. loadELF() re-arms this on each load.
    KernelStackPool m_asyncCallbackStack;

    std::atomic<uint32_t> m_missingFunctionPolicy{static_cast<uint32_t>(MissingFunctionPolicy::ContinueToTarget)};
    std::atomic<bool> m_missingFunctionReported{false};
    std::atomic<bool> m_stopRequested{false};
    DebugUiCallback m_debugUiInitCallback = nullptr;
    DebugUiCallback m_debugUiDrawCallback = nullptr;
    DebugUiCallback m_debugUiShutdownCallback = nullptr;
    void *m_debugUiUserData = nullptr;
    bool m_debugUiInitialized = false;

public:
    // Live snapshot of the currently-dispatching guest thread's PC/RA/SP/GP,
    // updated from PS2Runtime::dispatchLoop() on every step. Read by the
    // debug UI (ps2_debug_panel.cpp); independent of the threading backend.
    std::atomic<uint32_t> m_debugPc{0};
    std::atomic<uint32_t> m_debugRa{0};
    std::atomic<uint32_t> m_debugSp{0};
    std::atomic<uint32_t> m_debugGp{0};

private:
    struct LoadedModule
    {
        std::string name;
        uint32_t baseAddress;
        size_t size;
        bool active;
    };

    std::vector<LoadedModule> m_loadedModules;
    uint8_t *m_boundRdram = nullptr;
    uint8_t *m_boundGSVram = nullptr;
};

// One-shot per-invocation guest scratch stack for running recompiled guest code
// inline on the calling fiber (RPC/override invoke, inline DMAC handlers, MPEG
// stream callbacks). Reserves a fresh guest-heap region on construction and
// releases it on destruction, so two fibers interleaving through the same path
// — or one fiber re-entering it — always run on DISJOINT stacks. RAII release
// also fires on the ThreadExitException unwind used for cooperative fiber
// teardown, so a killed fiber cannot leak.
class GuestScratchStack
{
public:
    GuestScratchStack(PS2Runtime *runtime, uint32_t size)
        : m_runtime(runtime)
    {
        if (m_runtime != nullptr && size != 0u)
        {
            m_base = m_runtime->guestMalloc(size, 16u);
            if (m_base != 0u)
            {
                m_top = (m_base + size) & ~0xFu;
            }
        }
    }
    ~GuestScratchStack()
    {
        if (m_runtime != nullptr && m_base != 0u)
        {
            m_runtime->guestFree(m_base);
        }
    }
    GuestScratchStack(const GuestScratchStack &)            = delete;
    GuestScratchStack &operator=(const GuestScratchStack &) = delete;

    // Aligned guest $sp for the reserved stack; 0 if the reservation failed.
    uint32_t top() const { return m_top; }
    bool     valid() const { return m_top != 0u; }
    // Reservation base; 0 if the reservation failed. Callers that need a flat
    // scratch BUFFER (not a stack) use this instead of top().
    uint32_t base() const { return m_base; }

private:
    PS2Runtime *m_runtime = nullptr;
    uint32_t    m_base    = 0u;
    uint32_t    m_top     = 0u;
};

// Generated by ps2xRecomp in ps2xRuntime/src/runner/register_functions.cpp.
extern const uint32_t g_ps2RecompiledFunctionTableBase;
extern const uint32_t g_ps2RecompiledFunctionTableEnd;
extern const uint32_t g_ps2RecompiledFunctionTableSlotCount;
extern PS2Runtime::RecompiledFunction g_ps2RecompiledFunctionTable[];

#endif // PS2_RUNTIME_H
