#include "ps2_runtime.h"
#include "ps2_syscalls.h"
#include "ps2_runtime_macros.h"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <cstring>
#include <atomic>
#include <thread>
#include <unordered_map>
#include "raylib.h"

// From ps2_syscalls.cpp to help keep the scheduler semaphore sane.
extern std::atomic<int> g_schedulerSemaId;

#define ELF_MAGIC 0x464C457F // "\x7FELF" in little endian
#define ET_EXEC 2            // Executable file

#define EM_MIPS 8 // MIPS architecture

struct ElfHeader
{
    uint32_t magic;
    uint8_t elf_class;
    uint8_t endianness;
    uint8_t version;
    uint8_t os_abi;
    uint8_t abi_version;
    uint8_t padding[7];
    uint16_t type;
    uint16_t machine;
    uint32_t version2;
    uint32_t entry;
    uint32_t phoff;
    uint32_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
};

struct ProgramHeader
{
    uint32_t type;
    uint32_t offset;
    uint32_t vaddr;
    uint32_t paddr;
    uint32_t filesz;
    uint32_t memsz;
    uint32_t flags;
    uint32_t align;
};

#define PT_LOAD 1 // Loadable segment

static constexpr int FB_WIDTH = 640;
static constexpr int FB_HEIGHT = 448;
static constexpr uint32_t DEFAULT_FB_ADDR = 0x00100000; // location in RDRAM the guest will draw to
static constexpr uint32_t DEFAULT_FB_SIZE = FB_WIDTH * FB_HEIGHT * 4;

static void UploadFrame(Texture2D &tex, PS2Runtime *rt)
{
    // Try to use GS dispfb/display registers to locate the visible buffer.
    const GSRegisters &gs = rt->memory().gs();

    // DISPFBUF1 fields: FBP (bits 0-8) * 2048 bytes, FBW (bits 10-15) blocks of 64 pixels, PSM (bits 16-20)
    uint32_t dispfb = static_cast<uint32_t>(gs.dispfb1 & 0xFFFFFFFFULL);
    uint32_t fbp = dispfb & 0x1FF;
    uint32_t fbw = (dispfb >> 10) & 0x3F;
    uint32_t psm = (dispfb >> 16) & 0x1F;

    // DISPLAY1 fields: DX,DY not used here; DW,DH are width/height minus 1 (11 bits each)
    uint64_t display64 = gs.display1;
    uint32_t dw = static_cast<uint32_t>((display64 >> 23) & 0x7FF);
    uint32_t dh = static_cast<uint32_t>((display64 >> 34) & 0x7FF);

    // Default to 640x448 if regs look strange.
    uint32_t width = (dw + 1);
    uint32_t height = (dh + 1);
    if (dw == 0)
        width = FB_WIDTH;
    if (dh == 0)
        height = FB_HEIGHT;
    if (width > FB_WIDTH)
        width = FB_WIDTH;
    if (height > FB_HEIGHT)
        height = FB_HEIGHT;

    static uint64_t prev_dispfb = ~0ull;
    static uint64_t prev_display = ~0ull;
    static bool vramLogged = false;
    if (gs.dispfb1 != prev_dispfb || gs.display1 != prev_display)
    {
        std::cout << "[GS] dispfb1=0x" << std::hex << gs.dispfb1
                  << " display1=0x" << gs.display1 << std::dec << std::endl;
        prev_dispfb = gs.dispfb1;
        prev_display = gs.display1;
        // Allow VRAM peek to re-log when the buffer changes.
        vramLogged = false;
    }

    // Only handle PSMCT32 (0) in this minimal blitter.
    if (psm != 0)
    {
        uint8_t *src = rt->memory().getRDRAM() + (DEFAULT_FB_ADDR & 0x1FFFFFFF);
        UpdateTexture(tex, src);
        return;
    }

    constexpr uint32_t DEFAULT_FB_ADDR = 0x00100000;
    uint32_t baseBytes = fbp * 2048;
    if (fbp == 0)
    {
        baseBytes = DEFAULT_FB_ADDR;
    }
    uint32_t strideBytes = (fbw ? fbw : (FB_WIDTH / 64)) * 64 * 4;
    uint8_t *rdram = rt->memory().getRDRAM();
    uint8_t *gsvram = rt->memory().getGSVRAM();
    std::vector<uint8_t> scratch(FB_WIDTH * FB_HEIGHT * 4, 0);

    for (uint32_t y = 0; y < height; ++y)
    {
        uint32_t srcOff = baseBytes + y * strideBytes;
        uint32_t dstOff = y * FB_WIDTH * 4;
        uint32_t copyW = width * 4;
        uint32_t srcIdx = srcOff;
        if (!vramLogged)
        {
            uint32_t sum = 0;
            for (int i = 0; i < 32 && (srcIdx + i) < PS2_GS_VRAM_SIZE; ++i)
            {
                sum += gsvram[srcIdx + i];
            }
            std::cout << "[VRAM peek] sum first32=0x" << std::hex << sum << std::dec << std::endl;
            vramLogged = true;
        }
        if (srcIdx + copyW <= PS2_GS_VRAM_SIZE && gsvram)
        {
            std::memcpy(&scratch[dstOff], gsvram + srcIdx, copyW);
        }
        else
        {
            uint32_t rdramIdx = srcOff & PS2_RAM_MASK;
            if (rdramIdx + copyW > PS2_RAM_SIZE)
                copyW = PS2_RAM_SIZE - rdramIdx;
            std::memcpy(&scratch[dstOff], rdram + rdramIdx, copyW);
        }
    }

    // Peek first few bytes to see if anything is drawn.
    uint32_t peekOff = 0;
    uint32_t sum = 0;
    for (int i = 0; i < 32; ++i)
    {
        sum += scratch[peekOff + i];
    }
    static int peekCount = 0;
    if (peekCount < 4)
    {
        std::cout << "[FB peek] sum first32=0x" << std::hex << sum << std::dec
                  << " w=" << width << " h=" << height << std::endl;
        ++peekCount;
    }

    UpdateTexture(tex, scratch.data());
}

static void DumpFramebufferSample(PS2Memory &mem)
{
    uint32_t base = DEFAULT_FB_ADDR & 0x1FFFFFFF;
    uint8_t *ptr = mem.getRDRAM() + base;
    uint32_t sum = 0;
    for (int i = 0; i < 32; ++i)
    {
        sum += ptr[i];
    }
    std::cout << "[FB] addr=0x" << std::hex << DEFAULT_FB_ADDR << " first32 sum=0x" << sum << std::dec
              << " bytes:";
    for (int i = 0; i < 16; ++i)
    {
        std::cout << " " << (int)ptr[i];
    }
    std::cout << std::dec << std::endl;

    // Also dump the thread param block used in InitThread (0x363610 area)
    uint32_t tparam = 0x363600 & PS2_RAM_MASK;
    uint32_t *tp = reinterpret_cast<uint32_t *>(mem.getRDRAM() + tparam);
    std::cout << "[InitThread params] @0x363600: "
              << std::hex << tp[0] << " " << tp[1] << " " << tp[2] << " " << tp[3]
              << " " << tp[4] << " " << tp[5] << " " << tp[6] << std::dec << std::endl;
}

// Trace hook for a suspected thread entry to see what it does.
static PS2Runtime::RecompiledFunction g_entry_10c920 = nullptr;
static void entry_10c920_traced(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    std::cout << "[trace] enter 0x10c920 sp=0x" << std::hex << ctx->r[29].m128i_u32[0]
              << " gp=0x" << ctx->r[28].m128i_u32[0]
              << " ra=0x" << ctx->r[31].m128i_u32[0] << std::dec << std::endl;
    if (g_entry_10c920)
    {
        g_entry_10c920(rdram, ctx, runtime);
    }
    std::cout << "[trace] exit 0x10c920 pc=0x" << std::hex << ctx->pc
              << " ra=0x" << ctx->r[31].m128i_u32[0] << std::dec << std::endl;
}

// Hook for sceGsPutDispEnv to capture display buffer setup.
static PS2Runtime::RecompiledFunction g_putDispEnv = nullptr;
static void sceGsPutDispEnv_hook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    uint32_t envPtr = GPR_U32(ctx, 4);
    const uint8_t *base = runtime->memory().getRDRAM();
    constexpr uint32_t MASK = PS2_RAM_SIZE - 1;
    const uint64_t *env = reinterpret_cast<const uint64_t *>(base + (envPtr & MASK));
    if (env)
    {
        auto &gs = runtime->memory().gs();
        gs.dispfb1 = env[0];
        gs.display1 = env[1];
        std::cout << "[hook] sceGsPutDispEnv @0x" << std::hex << envPtr
                  << " dispfb1=0x" << env[0] << " display1=0x" << env[1] << std::dec << std::endl;
    }
    if (g_putDispEnv)
    {
        g_putDispEnv(rdram, ctx, runtime);
    }
}

// Hook for sceSifCallRpc to keep IOP RPC loops from stalling the main thread.
static PS2Runtime::RecompiledFunction g_sceSifCallRpc = nullptr;
static void sceSifCallRpc_stub(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    static int logCount = 0;
    if (logCount++ < 5)
    {
        std::cout << "[stub] sceSifCallRpc fno=0x" << std::hex << GPR_U32(ctx, 5)
                  << " mode=0x" << GPR_U32(ctx, 6)
                  << " send=0x" << GPR_U32(ctx, 7)
                  << " recv=0x" << GPR_U32(ctx, 8) << std::dec << std::endl;
    }
    SET_GPR_S32(ctx, 2, 0);
}

PS2Runtime::PS2Runtime()
{
    std::memset(&m_cpuContext, 0, sizeof(m_cpuContext));

    // R0 is always zero in MIPS
    m_cpuContext.r[0] = _mm_set1_epi32(0);

    // Stack pointer (SP) and global pointer (GP) will be set by the loaded ELF

    m_functionTable.clear();

    m_loadedModules.clear();
}

PS2Runtime::~PS2Runtime()
{
    m_loadedModules.clear();

    m_functionTable.clear();
}

bool PS2Runtime::initialize(const char *title)
{
    if (!m_memory.initialize())
    {
        std::cerr << "Failed to initialize PS2 memory" << std::endl;
        return false;
    }

    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(FB_WIDTH, FB_HEIGHT, title);
    SetTargetFPS(60);

    return true;
}

bool PS2Runtime::loadELF(const std::string &elfPath)
{
    std::ifstream file(elfPath, std::ios::binary);
    if (!file)
    {
        std::cerr << "Failed to open ELF file: " << elfPath << std::endl;
        return false;
    }

    ElfHeader header;
    file.read(reinterpret_cast<char *>(&header), sizeof(header));

    if (header.magic != ELF_MAGIC)
    {
        std::cerr << "Invalid ELF magic number" << std::endl;
        return false;
    }

    if (header.machine != EM_MIPS || header.type != ET_EXEC)
    {
        std::cerr << "Not a MIPS executable ELF file" << std::endl;
        return false;
    }

    m_cpuContext.pc = header.entry;

    for (uint16_t i = 0; i < header.phnum; i++)
    {
        ProgramHeader ph;
        file.seekg(header.phoff + i * header.phentsize);
        file.read(reinterpret_cast<char *>(&ph), sizeof(ph));

        if (ph.type == PT_LOAD && ph.filesz > 0)
        {
            std::cout << "Loading segment: 0x" << std::hex << ph.vaddr
                      << " - 0x" << (ph.vaddr + ph.memsz)
                      << " (size: 0x" << ph.memsz << ")" << std::dec << std::endl;

            // Allocate temporary buffer for the segment
            std::vector<uint8_t> buffer(ph.filesz);

            // Read segment data
            file.seekg(ph.offset);
            file.read(reinterpret_cast<char *>(buffer.data()), ph.filesz);

            // Copy to memory
            uint32_t physAddr = m_memory.translateAddress(ph.vaddr);
            uint8_t *dest = nullptr;
            if (ph.vaddr >= PS2_SCRATCHPAD_BASE && ph.vaddr < PS2_SCRATCHPAD_BASE + PS2_SCRATCHPAD_SIZE)
            {
                dest = m_memory.getScratchpad() + physAddr;
            }
            else
            {
                dest = m_memory.getRDRAM() + physAddr;
            }
            std::memcpy(dest, buffer.data(), ph.filesz);

            if (ph.memsz > ph.filesz)
            {
                std::memset(dest + ph.filesz, 0, ph.memsz - ph.filesz);
            }

            // Track executable regions for self-modifying code invalidation
            if (ph.flags & 0x1) // PF_X
            {
                m_memory.registerCodeRegion(ph.vaddr, ph.vaddr + ph.memsz);
            }
        }
    }

    LoadedModule module;
    module.name = elfPath.substr(elfPath.find_last_of("/\\") + 1);
    module.baseAddress = 0x00100000; // Typical base address for PS2 executables
    module.size = 0;                 // Would need to calculate from segments
    module.active = true;

    m_loadedModules.push_back(module);

    // acccordind to GPT some titles expect cmd_sem_init to see -1 sentinels before creating semaphores.
    const uint32_t semaInitAddrs[] = {0x00302c90u, 0x00302c94u, 0x00302c98u, 0x00302c9cu};
    bool seeded = false;
    for (uint32_t addr : semaInitAddrs)
    {
        uint32_t physAddr = m_memory.translateAddress(addr);
        uint32_t *p = reinterpret_cast<uint32_t *>(m_memory.getRDRAM() + physAddr);
        if (*p == 0)
        {
            *p = 0xFFFFFFFFu;
            seeded = true;
        }
    }
    if (seeded)
    {
        std::cout << "[init] Seeded cmd_sem_init sema IDs to -1" << std::endl;
    }

    // Debug: peek at some early globals to verify init state
    uint32_t dbg_addr = 0x00300000 + 11240;
    uint8_t *dbg_base = m_memory.getRDRAM();
    uint32_t dbg_val = *reinterpret_cast<uint32_t *>(dbg_base + (dbg_addr & PS2_RAM_MASK));
    std::cout << "Debug: [0x" << std::hex << dbg_addr << "] = 0x" << dbg_val << std::dec << std::endl;

    std::cout << "ELF file loaded successfully. Entry point: 0x" << std::hex << m_cpuContext.pc << std::dec << std::endl;
    return true;
}

void PS2Runtime::registerFunction(uint32_t address, RecompiledFunction func)
{
    m_functionTable[address] = func;
}

bool PS2Runtime::hasFunction(uint32_t address) const
{
    return m_functionTable.find(address) != m_functionTable.end();
}

PS2Runtime::RecompiledFunction PS2Runtime::lookupFunction(uint32_t address)
{
    auto it = m_functionTable.find(address);
    if (it != m_functionTable.end())
    {
        return it->second;
    }

    std::cerr << "Warning: Function at address 0x" << std::hex << address << std::dec << " not found" << std::endl;

    static RecompiledFunction defaultFunction = [](uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        std::cerr << "Error: Called unimplemented function at address 0x" << std::hex << ctx->pc << std::dec << std::endl;
    };

    return defaultFunction;
}

void PS2Runtime::SignalException(R5900Context *ctx, PS2Exception exception)
{
    if (exception == EXCEPTION_INTEGER_OVERFLOW)
    {
        // PS2 behavior: jump to exception handler
        HandleIntegerOverflow(ctx);
    }
}

static PS2Runtime::RecompiledFunction g_entry_10cb00 = nullptr;
static void entry_10cb00_hook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    static int logCount = 0;
    if (logCount < 16)
    {
        std::cout << "[cmdq] enqueue cmd=0 tid=" << GPR_U32(ctx, 16)
                  << " a0=0x" << std::hex << GPR_U32(ctx, 4) << std::dec << std::endl;
        ++logCount;
    }
    if (g_entry_10cb00)
    {
        g_entry_10cb00(rdram, ctx, runtime);
    }
}

static PS2Runtime::RecompiledFunction g_entry_10cb98 = nullptr;
static void entry_10cb98_hook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    static int logCount = 0;
    if (logCount < 16)
    {
        std::cout << "[cmdq] enqueue cmd=1 tid=" << GPR_U32(ctx, 16)
                  << " a0=0x" << std::hex << GPR_U32(ctx, 4) << std::dec << std::endl;
        ++logCount;
    }
    if (g_entry_10cb98)
    {
        g_entry_10cb98(rdram, ctx, runtime);
    }
}

static PS2Runtime::RecompiledFunction g_entry_10cc34 = nullptr;
static void entry_10cc34_hook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    static int logCount = 0;
    if (logCount < 16)
    {
        std::cout << "[cmdq] enqueue cmd=2 tid=" << GPR_U32(ctx, 16)
                  << " a0=0x" << std::hex << GPR_U32(ctx, 4) << std::dec << std::endl;
        ++logCount;
    }
    if (g_entry_10cc34)
    {
        g_entry_10cc34(rdram, ctx, runtime);
    }
}

void PS2Runtime::executeVU0Microprogram(uint8_t *rdram, R5900Context *ctx, uint32_t address)
{
    static std::unordered_map<uint32_t, int> seen;
    int &count = seen[address];
    if (count < 3)
    {
        std::cout << "[VU0] microprogram @0x" << std::hex << address
                  << " pc=0x" << ctx->pc
                  << " ra=0x" << ctx->r[31].m128i_u32[0]
                  << std::dec << std::endl;
    }
    ++count;

    // Clear/seed status so dependent code sees "success".
    ctx->vu0_clip_flags = 0;
    ctx->vu0_clip_flags2 = 0;
    ctx->vu0_mac_flags = 0;
    ctx->vu0_status = 0;
    ctx->vu0_q = 1.0f;

    // TODO: Implement a real interpreter. For now, no register mutations beyond defaults.
}

void PS2Runtime::vu0StartMicroProgram(uint8_t *rdram, R5900Context *ctx, uint32_t address)
{
    // VCALLMS/VCALLMSR paths both end up here; reuse the same minimal stub.
    executeVU0Microprogram(rdram, ctx, address);
}

void PS2Runtime::handleSyscall(uint8_t *rdram, R5900Context *ctx)
{
    std::cout << "Syscall encountered at PC: 0x" << std::hex << ctx->pc << std::dec << std::endl;
}

void PS2Runtime::handleBreak(uint8_t *rdram, R5900Context *ctx)
{
    std::cout << "Break encountered at PC: 0x" << std::hex << ctx->pc << std::dec << std::endl;
}

void PS2Runtime::handleTrap(uint8_t *rdram, R5900Context *ctx)
{
    std::cout << "Trap encountered at PC: 0x" << std::hex << ctx->pc << std::dec << std::endl;
}

void PS2Runtime::handleTLBR(uint8_t *rdram, R5900Context *ctx)
{
    std::cout << "TLBR (TLB Read) at PC: 0x" << std::hex << ctx->pc << std::dec << std::endl;
}

void PS2Runtime::handleTLBWI(uint8_t *rdram, R5900Context *ctx)
{
    std::cout << "TLBWI (TLB Write Indexed) at PC: 0x" << std::hex << ctx->pc << std::dec << std::endl;
}

void PS2Runtime::handleTLBWR(uint8_t *rdram, R5900Context *ctx)
{
    std::cout << "TLBWR (TLB Write Random) at PC: 0x" << std::hex << ctx->pc << std::dec << std::endl;
}

void PS2Runtime::handleTLBP(uint8_t *rdram, R5900Context *ctx)
{
    std::cout << "TLBP (TLB Probe) at PC: 0x" << std::hex << ctx->pc << std::dec << std::endl;
}

void PS2Runtime::clearLLBit(R5900Context *ctx)
{
    ctx->cop0_status &= ~0x00000002; // LL bit is bit 1 in the status register
    std::cout << "LL bit cleared at PC: 0x" << std::hex << ctx->pc << std::dec << std::endl;
}

void PS2Runtime::HandleIntegerOverflow(R5900Context *ctx)
{
    std::cerr << "Integer overflow exception at PC: 0x" << std::hex << ctx->pc << std::dec << std::endl;

    // Set the EPC (Exception Program Counter) to the current PC
    m_cpuContext.cop0_epc = ctx->pc;

    // Set the cause register to indicate an integer overflow
    m_cpuContext.cop0_cause |= (EXCEPTION_INTEGER_OVERFLOW << 2);

    // Jump to the exception handler (usually at 0x80000000)
    m_cpuContext.pc = 0x80000000; // Default PS2 exception handler address
}

void PS2Runtime::run()
{
    RecompiledFunction entryPoint = lookupFunction(m_cpuContext.pc);

    m_cpuContext.r[4] = _mm_set1_epi32(0);           // A0 = 0 (argc)
    m_cpuContext.r[5] = _mm_set1_epi32(0);           // A1 = 0 (argv)
    m_cpuContext.r[29] = _mm_set1_epi32(0x02000000); // SP = top of RAM

    std::cout << "Starting execution at address 0x" << std::hex << m_cpuContext.pc << std::dec << std::endl;

    // A blank image to use as a framebuffer
    Image blank = GenImageColor(FB_WIDTH, FB_HEIGHT, BLANK);
    Texture2D frameTex = LoadTextureFromImage(blank);
    UnloadImage(blank);

    if (hasFunction(0x10c920))
    {
        g_entry_10c920 = lookupFunction(0x10c920);
        registerFunction(0x10c920, entry_10c920_traced);
        std::cout << "[trace] hooked entry 0x10c920 for logging" << std::endl;
    }
    else
    {
        std::cout << "[trace] entry 0x10c920 not registered" << std::endl;
    }
    if (hasFunction(0x1004b8))
    {
        g_putDispEnv = lookupFunction(0x1004b8);
        registerFunction(0x1004b8, sceGsPutDispEnv_hook);
        std::cout << "[hook] wrapped sceGsPutDispEnv at 0x1004b8" << std::endl;
    }
    if (hasFunction(0x10ed80))
    {
        g_sceSifCallRpc = lookupFunction(0x10ed80);
        registerFunction(0x10ed80, sceSifCallRpc_stub);
        std::cout << "[hook] wrapped sceSifCallRpc at 0x10ed80" << std::endl;
    }
    if (hasFunction(0x10cb00))
    {
        g_entry_10cb00 = lookupFunction(0x10cb00);
        registerFunction(0x10cb00, entry_10cb00_hook);
        std::cout << "[hook] wrapped cmd queue (cmd=0) at 0x10cb00" << std::endl;
    }
    if (hasFunction(0x10cb98))
    {
        g_entry_10cb98 = lookupFunction(0x10cb98);
        registerFunction(0x10cb98, entry_10cb98_hook);
        std::cout << "[hook] wrapped cmd queue (cmd=1) at 0x10cb98" << std::endl;
    }
    if (hasFunction(0x10cc34))
    {
        g_entry_10cc34 = lookupFunction(0x10cc34);
        registerFunction(0x10cc34, entry_10cc34_hook);
        std::cout << "[hook] wrapped cmd queue (cmd=2) at 0x10cc34" << std::endl;
    }

    g_activeThreads.store(1, std::memory_order_relaxed);

    // for now if the scheduler sema hasn't been created yet, force InitThread to set it up.
    if (hasFunction(0x10c9f8))
    {
        uint32_t *sched = reinterpret_cast<uint32_t *>(m_memory.getRDRAM() + (0x363a10 & PS2_RAM_MASK));
        if (!sched || sched[0] == 0)
        {
            RecompiledFunction initThread = lookupFunction(0x10c9f8);
            R5900Context initCtx{};
            std::memset(&initCtx, 0, sizeof(initCtx));
            initCtx.r[0] = _mm_set1_epi32(0);
            initCtx.r[29] = _mm_set1_epi32(0x02000000);
            initCtx.r[28] = _mm_set1_epi32(0x36a7f0);
            initCtx.pc = 0x10c9f8;
            std::cout << "[autorun] running InitThread pc=0x10c9f8" << std::endl;
            initThread(m_memory.getRDRAM(), &initCtx, this);
        }
    }

    // Fallback: if the game's main entry (ps2_main at 0x12b0a0) is registered, start it on a separate host thread.
    // The normal bootstrap thread seems to stall before spawning it, so we kick it off manually.
    if (hasFunction(0x12b0a0))
    {
        RecompiledFunction ps2Main = lookupFunction(0x12b0a0);
        g_activeThreads.fetch_add(1, std::memory_order_relaxed);
        std::thread([=]() mutable
                    {
            R5900Context localCtx{};
            std::memset(&localCtx, 0, sizeof(localCtx));
            // Set baseline registers similar to the primary thread.
            localCtx.r[0] = _mm_set1_epi32(0);
            localCtx.r[29] = _mm_set1_epi32(0x02000000); // SP top of RAM
            localCtx.r[28] = _mm_set1_epi32(0x36a7f0);   // GP from ELF bootstrap
            localCtx.pc = 0x12b0a0;

            std::cout << "[autorun] starting ps2_main fallback pc=0x12b0a0 sp=0x02000000 gp=0x36a7f0" << std::endl;
            try
            {
                ps2Main(m_memory.getRDRAM(), &localCtx, this);
                std::cout << "[autorun] ps2_main returned pc=0x" << std::hex << localCtx.pc
                          << " ra=0x" << localCtx.r[31].m128i_u32[0] << std::dec << std::endl;
            }
            catch (const std::exception &e)
            {
                std::cerr << "[autorun] ps2_main exception: " << e.what() << std::endl;
            }
            g_activeThreads.fetch_sub(1, std::memory_order_relaxed); })
            .detach();
    }
    else
    {
        std::cout << "[autorun] ps2_main not registered; skipping fallback launch" << std::endl;
    }

    // Dump a small sample
    {
        uint32_t base = DEFAULT_FB_ADDR & 0x1FFFFFFF;
        uint8_t *ptr = m_memory.getRDRAM() + base;
        uint32_t sum = 0;
        for (int i = 0; i < 32; ++i)
            sum += ptr[i];
        std::cout << "[FB] addr=0x" << std::hex << DEFAULT_FB_ADDR
                  << " first32 sum=0x" << sum << " bytes:";
        for (int i = 0; i < 16; ++i)
        {
            std::cout << " " << (int)ptr[i];
        }
        std::cout << std::dec << std::endl;
    }

    std::thread gameThread([&, entryPoint]()
                           {
        try
        {
            entryPoint(m_memory.getRDRAM(), &m_cpuContext, this);
            std::cout << "Game thread returned. PC=0x" << std::hex << m_cpuContext.pc
                      << " RA=0x" << m_cpuContext.r[31].m128i_u32[0] << std::dec << std::endl;
        }
        catch (const std::exception &e)
        {
            std::cerr << "Error during program execution: " << e.what() << std::endl;
        }
        g_activeThreads.fetch_sub(1, std::memory_order_relaxed); });

    static uint32_t lastSchedId = 0;
    uint64_t tick = 0;
    while (g_activeThreads.load(std::memory_order_relaxed) > 0)
    {
        {
            uint32_t *sched = reinterpret_cast<uint32_t *>(m_memory.getRDRAM() + (0x363a10 & PS2_RAM_MASK));
            int known = g_schedulerSemaId.load(std::memory_order_relaxed);
            if (sched)
            {
                if ((sched[0] == 0 || sched[0] > 1000) && known > 0)
                    sched[0] = static_cast<uint32_t>(known);
                // head/tail indices
                if (sched[2] > 511)
                    sched[2] = 0;
                if (sched[3] > 511)
                    sched[3] = 0;
            }
        }

        {
            constexpr uint32_t kSchedSpan = 0x420;
            static std::vector<uint8_t> schedSnapshot;
            static int schedDeltaLogs = 0;

            uint8_t *rdram = m_memory.getRDRAM();
            uint32_t base = 0x363a10 & PS2_RAM_MASK;

            if (schedSnapshot.empty())
            {
                schedSnapshot.resize(kSchedSpan);
                std::memcpy(schedSnapshot.data(), rdram + base, kSchedSpan);
            }
            else
            {
                int diffCount = 0;
                int detailCount = 0;
                for (uint32_t i = 0; i < kSchedSpan; ++i)
                {
                    uint8_t cur = rdram[(base + i) & PS2_RAM_MASK];
                    uint8_t prev = schedSnapshot[i];
                    if (cur != prev)
                    {
                        schedSnapshot[i] = cur;
                        ++diffCount;
                        if (schedDeltaLogs < 32 && detailCount < 8)
                        {
                            std::cout << "[sched delta] off=0x" << std::hex << i
                                      << " " << (int)prev << "->" << (int)cur << std::dec << std::endl;
                            ++detailCount;
                        }
                    }
                }
                if (diffCount > 0 && schedDeltaLogs < 32)
                {
                    std::cout << "[sched delta] changed=" << diffCount << std::endl;
                    ++schedDeltaLogs;
                }
            }
        }

        if ((tick++ % 120) == 0)
        {
            std::cout << "[run] activeThreads=" << g_activeThreads.load(std::memory_order_relaxed);
            std::cout << " pc=0x" << std::hex << m_cpuContext.pc
                      << " ra=0x" << m_cpuContext.r[31].m128i_u32[0]
                      << " sp=0x" << m_cpuContext.r[29].m128i_u32[0]
                      << " gp=0x" << m_cpuContext.r[28].m128i_u32[0] << std::dec << std::endl;
        }
        if ((tick % 600) == 0)
        {
            static int schedLog = 0;
            if (schedLog < 5)
            {
                uint8_t *rdram = m_memory.getRDRAM();
                uint32_t base = 0x363a10 & PS2_RAM_MASK;
                uint32_t *p = reinterpret_cast<uint32_t *>(rdram + base);
                uint32_t argPtr = 0x363a18 & PS2_RAM_MASK;
                uint32_t idx = *reinterpret_cast<uint32_t *>(rdram + argPtr) & 0x1FF;
                uint32_t cmdBase = (argPtr + 8) & PS2_RAM_MASK;
                uint32_t tidBase = (argPtr + 9) & PS2_RAM_MASK;
                uint8_t cmd0 = rdram[cmdBase & PS2_RAM_MASK];
                uint8_t tid0 = rdram[tidBase & PS2_RAM_MASK];
                uint8_t cmdIdx = rdram[(cmdBase + (idx << 1)) & PS2_RAM_MASK];
                uint8_t tidIdx = rdram[(tidBase + (idx << 1)) & PS2_RAM_MASK];
                std::cout << "[sched] sema=" << p[0] << " q1=" << p[1] << " head=" << p[2] << " tail=" << p[3]
                          << " cmd0=" << (int)cmd0 << "/" << (int)tid0
                          << " cmd[idx=" << idx << "]=" << (int)cmdIdx << "/" << (int)tidIdx
                          << " lastId=" << lastSchedId << std::endl;
                ++schedLog;
            }

            static uint64_t lastDma = 0, lastGif = 0, lastGs = 0, lastVif = 0;
            uint64_t curDma = m_memory.dmaStartCount();
            uint64_t curGif = m_memory.gifCopyCount();
            uint64_t curGs = m_memory.gsWriteCount();
            uint64_t curVif = m_memory.vifWriteCount();
            if (curDma != lastDma || curGif != lastGif || curGs != lastGs || curVif != lastVif)
            {
                std::cout << "[hw] dma_starts=" << curDma
                          << " gif_copies=" << curGif
                          << " gs_writes=" << curGs
                          << " vif_writes=" << curVif << std::endl;
                lastDma = curDma;
                lastGif = curGif;
                lastGs = curGs;
                lastVif = curVif;
            }
        }
        // Kick the scheduler semaphore (stored at 0x363a10) to simulate VBlank-style ticks.
        {
            uint32_t schedId = *reinterpret_cast<uint32_t *>(m_memory.getRDRAM() + (0x363a10 & PS2_RAM_MASK));
            if (schedId && schedId < 0x1000)
            {
                lastSchedId = schedId;
            }
            if (schedId == 0 && lastSchedId != 0)
            {
                schedId = lastSchedId; // fall back to the last seen non-zero id
            }
            else if (schedId >= 0x1000 && lastSchedId != 0)
            {
                // Ignore obviously bogus ids that are likely other data scribbling over the struct.
                schedId = lastSchedId;
            }
            if (schedId)
            {
                R5900Context semaCtx{};
                R5900Context *semaCtxPtr = &semaCtx;
                SET_GPR_U32(semaCtxPtr, 4, schedId);
                ps2_syscalls::SignalSema(m_memory.getRDRAM(), semaCtxPtr, this);
            }
        }
        UploadFrame(frameTex, this);

        BeginDrawing();
        ClearBackground(BLACK);
        DrawTexture(frameTex, 0, 0, WHITE);
        EndDrawing();

        if (WindowShouldClose())
        {
            std::cout << "[run] window close requested, breaking out of loop" << std::endl;
            break;
        }
    }

    if (g_activeThreads.load(std::memory_order_relaxed) == 0)
    {
        if (gameThread.joinable())
        {
            gameThread.join();
        }
    }
    else
    {

        if (gameThread.joinable())
        {
            gameThread.detach();
        }
    }

    UnloadTexture(frameTex);
    CloseWindow();

    std::cout << "[run] exiting loop, activeThreads=" << g_activeThreads.load(std::memory_order_relaxed) << std::endl;
}
