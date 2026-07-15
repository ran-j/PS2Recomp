#include "ps2_runtime.h"
#include "ps2_dispatch_history.h"
#include "ps2_scheduler.h"
#include "ps2_log.h"
#include "ps2_stubs.h"
#include "ps2_syscalls.h"
#include "game_overrides.h"
#include "ps2_runtime_macros.h"
#include "runtime/ps2_gs_gpu.h"
#include "ThreadNaming.h"
#include "Kernel/Stubs/Audio.h"
#include "Kernel/Stubs/GS.h"
#include "Kernel/Stubs/MPEG.h"
#include "ps2_host_backend.h"
#include "ps2_iop_host.h"
#include "ps2x/iop/iop_subsystem.h"

#include <iostream>
#include <fstream>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <limits>
#include <chrono>
#include <atomic>
#include <thread>
#include <sstream>

namespace ps2_stubs
{
    void resetSifState();
}

#define ELF_MAGIC 0x464C457F // "\x7FELF" in little endian
#define ET_EXEC 2            // Executable file
#define EM_MIPS 8            // MIPS architecture
#define PT_LOAD 1            // Loadable segment

static constexpr int FB_WIDTH = 640;
static constexpr int FB_HEIGHT = 512;
static constexpr int DEFAULT_DISPLAY_HEIGHT = 448;
static constexpr uint32_t DEFAULT_FB_SIZE = FB_WIDTH * FB_HEIGHT * 4;
static constexpr uint32_t DEFAULT_FB_ADDR = (PS2_RAM_SIZE - DEFAULT_FB_SIZE - 0x10000u);
#if defined(PLATFORM_VITA)
static constexpr int HOST_WINDOW_WIDTH = 960;
static constexpr int HOST_WINDOW_HEIGHT = 544;
#else
static constexpr int HOST_WINDOW_WIDTH = FB_WIDTH;
static constexpr int HOST_WINDOW_HEIGHT = DEFAULT_DISPLAY_HEIGHT;
#endif
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

namespace
{
    constexpr uint32_t kGuestHeapDefaultBase = 0x00100000u;
    constexpr uint32_t kGuestHeapDefaultAlignment = 16u;
    constexpr uint32_t kGuestHeapSafetyPad = 0x1000u;
    constexpr uint32_t kGuestHeapHardLimit = 0x01F00000u;

    // -----------------------------------------------------------------------
    // Async callback stack pool: [kAsyncCallbackStackFloor, kAsyncCallbackStackTop)
    // — KERNEL-RESERVED memory, carved downward from the top.
    //
    // Placed below the ELF load base so it is disjoint from the game-chosen
    // main stack at top-of-RAM — SDK crt0 calls SetupThread to place that
    // stack at the top of user RAM — so
    // host-dispatched guest callbacks (the sceGsSyncVCallback chain, INTC
    // handlers, alarms) never interleave, via the N=1 scheduler's token
    // handoff, with the game's own live stack frames.
    //
    // On real hardware these contexts run on KERNEL stacks in kernel-reserved
    // memory — never on the interrupted user thread's stack. The EE kernel
    // owns phys [0, 0x00100000): this runtime's kernel-mirror state sits
    // below 0x00012000, the ELF loads at 0x00100000+, the guest heap starts
    // at the ELF's bss end, and guest thread stacks are game-chosen addresses
    // >= 0x00100000. [kAsyncCallbackStackFloor, kAsyncCallbackStackTop) is untouched by both sides, so
    // callback stacks there are disjoint from ALL guest memory by
    // construction.
    //
    // kAsyncCallbackStackFloor/Top themselves live in ps2_runtime.h (next to
    // kAsyncCallbackFallbackSp) so the member default initializers below and
    // this file's re-arm site share one definition instead of three.
    // -----------------------------------------------------------------------

    constexpr uint32_t COP0_CAUSE_EXCCODE_MASK = 0x0000007Cu;
    constexpr uint32_t COP0_CAUSE_BD = 0x80000000u;
    constexpr uint32_t COP0_STATUS_EXL = 0x00000002u;
    constexpr uint32_t COP0_STATUS_BEV = 0x00400000u;
    constexpr uint32_t EXCEPTION_VECTOR_GENERAL = 0x80000080u;
    constexpr uint32_t EXCEPTION_VECTOR_TLB_REFILL = 0x80000000u;
    constexpr uint32_t EXCEPTION_VECTOR_BOOT = 0xBFC00200u;

    // Fiber-owned when running inside a fiber; per-OS-thread fallback otherwise
    // (borrowed host workers, executor between fibers, direct non-fiber callers).
    DispatchHistory &currentDispatchHistory()
    {
        return ps2sched::current_dispatch_history();
    }

    bool computeFileCrc32(const std::string &path, uint32_t &crcOut)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open())
        {
            return false;
        }

        static const std::array<uint32_t, 256> table = []
        {
            std::array<uint32_t, 256> values{};
            for (uint32_t i = 0; i < values.size(); ++i)
            {
                uint32_t value = i;
                for (uint32_t bit = 0; bit < 8; ++bit)
                {
                    value = (value & 1u) ? (0xEDB88320u ^ (value >> 1u)) : (value >> 1u);
                }
                values[i] = value;
            }
            return values;
        }();

        uint32_t crc = 0xFFFFFFFFu;
        std::array<uint8_t, 16 * 1024> buffer{};
        while (file.good())
        {
            file.read(reinterpret_cast<char *>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
            const std::streamsize count = file.gcount();
            for (std::streamsize i = 0; i < count; ++i)
            {
                crc = table[(crc ^ buffer[static_cast<size_t>(i)]) & 0xFFu] ^ (crc >> 8u);
            }
        }
        if (file.bad())
        {
            return false;
        }
        crcOut = ~crc;
        return true;
    }

    void pushDispatchPc(uint32_t pc)
    {
        DispatchHistory &h = currentDispatchHistory();
        h.pcs[h.next] = pc;
        h.next = (h.next + 1u) % static_cast<uint32_t>(h.pcs.size());
        if (h.next == 0u)
        {
            h.wrapped = true;
        }
    }

    std::string formatDispatchHistory()
    {
        const DispatchHistory &h = currentDispatchHistory();
        const uint32_t count = h.wrapped ? static_cast<uint32_t>(h.pcs.size()) : h.next;
        if (count == 0u)
        {
            return "(empty)";
        }

        std::ostringstream oss;
        bool first = true;
        for (uint32_t i = 0u; i < count; ++i)
        {
            const uint32_t idx = (h.next + h.pcs.size() - count + i) % static_cast<uint32_t>(h.pcs.size());
            if (!first)
            {
                oss << " -> ";
            }
            first = false;
            oss << "0x" << std::hex << h.pcs[idx];
        }
        return oss.str();
    }

    uint32_t selectExceptionVector(const R5900Context *ctx, bool tlbRefill)
    {
        if (ctx->cop0_status & COP0_STATUS_BEV)
        {
            return EXCEPTION_VECTOR_BOOT;
        }
        return tlbRefill ? EXCEPTION_VECTOR_TLB_REFILL : EXCEPTION_VECTOR_GENERAL;
    }

    void seedVu0IdleSuccess(R5900Context *ctx)
    {
        if (!ctx)
        {
            return;
        }

        ctx->vu0_clip_flags = 0;
        ctx->vu0_clip_flags2 = 0;
        ctx->vu0_mac_flags = 0;
        ctx->vu0_status = 0;
        ctx->vu0_q = 1.0f;
        ctx->vu0_vpu_stat = 0;
        ctx->vu0_vpu_stat2 = 0;
    }

    void copyVu0ContextToState(const R5900Context *ctx, VU1State &state)
    {
        std::memset(&state, 0, sizeof(state));

        for (uint32_t i = 0; i < 32u; ++i)
        {
            _mm_storeu_ps(state.vf[i], ctx->vu0_vf[i]);
        }
        for (uint32_t i = 0; i < 16u; ++i)
        {
            state.vi[i] = static_cast<int16_t>(ctx->vi[i]);
        }

        _mm_storeu_ps(state.acc, ctx->vu0_acc);
        state.q = ctx->vu0_q;
        state.p = ctx->vu0_p;
        state.i = ctx->vu0_i;
        state.pc = ctx->vu0_pc;
        state.mac = ctx->vu0_mac_flags;
        state.clip = ctx->vu0_clip_flags;
        state.status = ctx->vu0_status;
        state.itop = ctx->vu0_itop;

        state.vf[0][0] = 0.0f;
        state.vf[0][1] = 0.0f;
        state.vf[0][2] = 0.0f;
        state.vf[0][3] = 1.0f;
        state.vi[0] = 0;
    }

    void copyVu0StateToContext(const VU1State &state, R5900Context *ctx)
    {
        for (uint32_t i = 0; i < 32u; ++i)
        {
            ctx->vu0_vf[i] = _mm_loadu_ps(state.vf[i]);
        }
        for (uint32_t i = 0; i < 16u; ++i)
        {
            ctx->vi[i] = static_cast<uint16_t>(state.vi[i]);
        }

        ctx->vu0_acc = _mm_loadu_ps(state.acc);
        ctx->vu0_q = state.q;
        ctx->vu0_p = state.p;
        ctx->vu0_i = state.i;
        ctx->vu0_mac_flags = state.mac;
        ctx->vu0_clip_flags = state.clip;
        ctx->vu0_clip_flags2 = state.clip;
        ctx->vu0_status = static_cast<uint16_t>(state.status);
        ctx->vu0_itop = state.itop;
        ctx->vu0_pc = state.pc;
        ctx->vu0_tpc = state.pc;
        ctx->vu0_vpu_stat = 0;
        ctx->vu0_vpu_stat2 = 0;

        ctx->vu0_vf[0] = _mm_set_ps(1.0f, 0.0f, 0.0f, 0.0f);
        ctx->vi[0] = 0;
    }

    void raiseCop0Exception(R5900Context *ctx, uint32_t exceptionCode, bool tlbRefill = false)
    {
        if (ctx->in_delay_slot)
        {
            ctx->cop0_epc = ctx->branch_pc;
            ctx->cop0_cause = (ctx->cop0_cause & ~COP0_CAUSE_EXCCODE_MASK) |
                              ((exceptionCode << 2) & COP0_CAUSE_EXCCODE_MASK) |
                              COP0_CAUSE_BD;
        }
        else
        {
            ctx->cop0_epc = ctx->pc;
            ctx->cop0_cause = (ctx->cop0_cause & ~(COP0_CAUSE_EXCCODE_MASK | COP0_CAUSE_BD)) |
                              ((exceptionCode << 2) & COP0_CAUSE_EXCCODE_MASK);
        }

        ctx->cop0_status |= COP0_STATUS_EXL;
        ctx->pc = selectExceptionVector(ctx, tlbRefill);
        ctx->in_delay_slot = false;
    }

    std::filesystem::path normalizeAbsolutePath(const std::filesystem::path &path)
    {
        if (path.empty())
        {
            return {};
        }

#if defined(PLATFORM_VITA)
        const std::string generic = path.generic_string();
        const std::size_t colon = generic.find(':');
        if (colon != std::string::npos && colon != 0u)
        {
            const std::size_t slash = generic.find_first_of("/\\");
            if (slash == std::string::npos || colon < slash)
            {
                return path.lexically_normal();
            }
        }
#endif

        std::error_code ec;
        const std::filesystem::path absolute = std::filesystem::absolute(path, ec);
        if (ec)
        {
            return path.lexically_normal();
        }
        return absolute.lexically_normal();
    }

    PS2Runtime::IoPaths &runtimeIoPaths()
    {
        static PS2Runtime::IoPaths paths = []()
        {
            PS2Runtime::IoPaths defaults;
            std::error_code ec;
            const std::filesystem::path cwd = std::filesystem::current_path(ec);
            defaults.elfDirectory = ec ? std::filesystem::path(".") : cwd.lexically_normal();
            defaults.hostRoot = defaults.elfDirectory;
            defaults.cdRoot = defaults.elfDirectory;
            defaults.mcRoot = defaults.elfDirectory / "mc0";
            return defaults;
        }();

        return paths;
    }

    std::string readGuestPrintableString(const uint8_t *rdram, uint32_t addr, size_t maxLen)
    {
        std::string out;
        if (!rdram || maxLen == 0)
        {
            return out;
        }

        out.reserve(std::min<size_t>(maxLen, 64));
        for (size_t i = 0; i < maxLen; ++i)
        {
            const char ch = static_cast<char>(rdram[(addr + static_cast<uint32_t>(i)) & PS2_RAM_MASK]);
            if (ch == '\0')
            {
                break;
            }
            if (ch >= 0x20 && ch < 0x7F)
            {
                out.push_back(ch);
            }
            else
            {
                out.push_back('.');
            }
        }
        return out;
    }
}

static void UploadFrame(Texture2D &tex, PS2Runtime *rt, uint32_t &outWidth, uint32_t &outHeight)
{
    static uint64_t s_lastPresentationTick = std::numeric_limits<uint64_t>::max();
    static bool s_hasLatchedInitialFrame = false;
    static uint32_t s_lastDisplayFbp = std::numeric_limits<uint32_t>::max();
    static uint32_t s_lastSourceFbp = std::numeric_limits<uint32_t>::max();
    static bool s_lastPreferred = false;
    static uint32_t s_lastWidth = 0u;
    static uint32_t s_lastHeight = 0u;
    static bool s_hasUploadedFrame = false;
    static std::vector<uint8_t> s_scratch;
    static std::vector<uint8_t> s_uploadBuffer(DEFAULT_FB_SIZE, 0u);

    const uint64_t currentTick = ps2_syscalls::GetCurrentVSyncTick();
    const bool needsLatch = !s_hasLatchedInitialFrame || currentTick != s_lastPresentationTick;
    if (needsLatch)
    {
        rt->gs().latchHostPresentationFrame();
        s_lastPresentationTick = currentTick;
        s_hasLatchedInitialFrame = true;
    }
    else if (s_hasUploadedFrame)
    {
        outWidth = (s_lastWidth != 0u) ? s_lastWidth : FB_WIDTH;
        outHeight = (s_lastHeight != 0u) ? s_lastHeight : DEFAULT_DISPLAY_HEIGHT;
        return;
    }

    s_scratch.clear();
    uint32_t width = 0u;
    uint32_t height = 0u;
    uint32_t displayFbp = 0u;
    uint32_t sourceFbp = 0u;
    bool usedPreferredDisplaySource = false;
    if (!rt->gs().copyLatchedHostPresentationFrame(s_scratch,
                                                   width,
                                                   height,
                                                   &displayFbp,
                                                   &sourceFbp,
                                                   &usedPreferredDisplaySource))
    {
        Image blank = GenImageColor(FB_WIDTH, FB_HEIGHT, MAGENTA);
        UpdateTexture(tex, blank.data);
        UnloadImage(blank);
        outWidth = FB_WIDTH;
        outHeight = DEFAULT_DISPLAY_HEIGHT;
        s_lastWidth = outWidth;
        s_lastHeight = outHeight;
        s_hasUploadedFrame = true;
        return;
    }

    PS2_IF_AGRESSIVE_LOGS({
        static uint32_t s_uploadDebugCount = 0u;
        if (s_uploadDebugCount < 128u ||
            displayFbp != s_lastDisplayFbp ||
            sourceFbp != s_lastSourceFbp ||
            usedPreferredDisplaySource != s_lastPreferred ||
            width != s_lastWidth ||
            height != s_lastHeight)
        {
            std::cout << "[frame:upload] idx=" << s_uploadDebugCount
                      << " tick=" << currentTick
                      << " displayFbp=" << displayFbp
                      << " sourceFbp=" << sourceFbp
                      << " size=" << width << "x" << height
                      << " preferred=" << static_cast<uint32_t>(usedPreferredDisplaySource ? 1u : 0u)
                      << std::endl;
        }
        ++s_uploadDebugCount;
    });
    s_lastDisplayFbp = displayFbp;
    s_lastSourceFbp = sourceFbp;
    s_lastPreferred = usedPreferredDisplaySource;
    s_lastWidth = width;
    s_lastHeight = height;

    std::fill(s_uploadBuffer.begin(), s_uploadBuffer.end(), 0u);
    if (!s_scratch.empty() && width != 0u && height != 0u)
    {
        const uint32_t copyWidth = std::min<uint32_t>(width, FB_WIDTH);
        const uint32_t copyHeight = std::min<uint32_t>(height, FB_HEIGHT);
        const size_t srcRowBytes = static_cast<size_t>(width) * 4u;
        const size_t dstRowBytes = static_cast<size_t>(FB_WIDTH) * 4u;
        const size_t copyRowBytes = static_cast<size_t>(copyWidth) * 4u;
        for (uint32_t y = 0; y < copyHeight; ++y)
        {
            const size_t srcOffset = static_cast<size_t>(y) * srcRowBytes;
            const size_t dstOffset = static_cast<size_t>(y) * dstRowBytes;
            if (srcOffset + copyRowBytes > s_scratch.size() ||
                dstOffset + copyRowBytes > s_uploadBuffer.size())
            {
                break;
            }
            std::memcpy(s_uploadBuffer.data() + dstOffset, s_scratch.data() + srcOffset, copyRowBytes);
        }
    }

    UpdateTexture(tex, s_uploadBuffer.data());
    outWidth = width;
    outHeight = height;
    s_hasUploadedFrame = true;
}

PS2Runtime::PS2Runtime()
{
    m_iopHost = std::make_unique<PS2IopHostAdapter>(*this);
    m_iopSubsystem = std::make_unique<ps2x::iop::IopSubsystem>(*m_iopHost);
#if defined(PS2X_IOP_ENABLE_PLUGINS) && PS2X_IOP_ENABLE_PLUGINS && \
    !defined(PLATFORM_VITA) && (defined(_WIN32) || defined(__linux__))
    if (const char *applicationDirectory = GetApplicationDirectory();
        applicationDirectory && applicationDirectory[0] != '\0')
    {
        m_iopSubsystem->setPluginSearchPaths({std::filesystem::path(applicationDirectory) / "iop_plugins"});
    }
#endif

    std::memset(&m_cpuContext, 0, sizeof(m_cpuContext));

    // R0 is always zero in MIPS
    m_cpuContext.r[0] = _mm_set1_epi32(0);

    // Stack pointer (SP) and global pointer (GP) will be set by the loaded ELF

    m_loadedModules.clear();
    m_guestHeapBlocks.clear();
    m_guestHeapBase = kGuestHeapDefaultBase;
    m_guestHeapEnd = kGuestHeapDefaultBase;
    m_guestHeapLimit = std::min(kGuestHeapHardLimit, PS2_RAM_SIZE);
    m_guestHeapSuggestedBase = kGuestHeapDefaultBase;
    m_guestHeapConfigured = false;
    // m_asyncCallbackStack (KernelStackPool) keeps its kernel-pool default
    // member initializer here; loadELF() re-arms it for the pool's next load
    // (see the layout comment at kAsyncCallbackStackFloor).

    // Claim the reserved main-thread identity (tid 1 — see State.h's
    // g_nextThreadId starting at 2, and run()'s create_fiber(1, 1, ...) for the
    // guest boot fiber, both of which reserve this same id) for the host thread
    // that constructs this runtime. g_currentThreadId == -1 here means this
    // host thread has never been assigned a guest identity: it is neither a
    // running guest fiber (which carries its own tid, set by the scheduler on
    // its own dedicated executor thread — a different OS thread from this one)
    // nor a borrowed IRQ/alarm/RPC worker (those self-assign -1 as the first
    // statement of their thread function, before any PS2Runtime is reachable).
    // ensureCurrentThreadInfo() lazily creates tid 1's ThreadInfo (THS_RUN,
    // wakeupCount 0) the first time a syscall needs it, and the guest boot
    // fiber (also tid 1, but on the separate executor thread) later finds and
    // reuses that same g_threads entry — so tid 1 never has two ThreadInfos.
    if (g_currentThreadId == -1)
    {
        g_currentThreadId = 1;
    }
}

void PS2Runtime::setDebugUiCallbacks(DebugUiCallback initCallback,
                                     DebugUiCallback drawCallback,
                                     DebugUiCallback shutdownCallback,
                                     void *userData)
{
    if (m_debugUiInitialized && m_debugUiShutdownCallback)
    {
        m_debugUiShutdownCallback(*this, m_debugUiUserData);
        m_debugUiInitialized = false;
    }

    m_debugUiInitCallback = initCallback;
    m_debugUiDrawCallback = drawCallback;
    m_debugUiShutdownCallback = shutdownCallback;
    m_debugUiUserData = userData;
}

PS2Runtime::~PS2Runtime()
{
    try
    {
        requestStop();
        // Fiber pool is cleaned up by scheduler_shutdown() in run().
        ps2_syscalls::detachAllGuestHostThreads();
        m_iopSubsystem.reset();
        m_iopHost.reset();
#if defined(PLATFORM_VITA)
        m_audioBackend.stopAll();
        m_audioBackend.setAudioReady(false);
#else
        if (IsAudioDeviceReady())
        {
            CloseAudioDevice();
            m_audioBackend.setAudioReady(false);
        }
#endif
        if (m_debugUiInitialized && m_debugUiShutdownCallback)
        {
            m_debugUiShutdownCallback(*this, m_debugUiUserData);
            m_debugUiInitialized = false;
        }

        if (IsWindowReady())
        {
            CloseWindow();
        }

        m_loadedModules.clear();
    }
    catch (const std::exception &e)
    {
        std::cerr << "[~PS2Runtime] cleanup exception: " << e.what() << std::endl;
    }
    catch (...)
    {
        std::cerr << "[~PS2Runtime] cleanup exception: unknown" << std::endl;
    }
}

void PS2Runtime::setIopPluginSearchPaths(std::vector<std::filesystem::path> paths)
{
    m_iopSubsystem->setPluginSearchPaths(std::move(paths));
}

ps2x::iop::RpcAbi PS2Runtime::selectIopRpcAbi(const ps2x::iop::RpcAbiRequest &request) const
{
    return m_iopSubsystem->selectRpcAbi(request);
}

ps2x::iop::RpcResult PS2Runtime::handleIopRpc(uint8_t *rdram, R5900Context *ctx, ps2x::iop::RpcRequest request)
{
    auto scope = m_iopHost->enterCall(ctx, rdram);
    request.callToken = scope.token();
    return m_iopSubsystem->handleRpc(request);
}

void PS2Runtime::notifyIopSifTransfer(uint8_t *rdram, const ps2x::iop::SifTransfer &transfer)
{
    auto scope = m_iopHost->enterCall(nullptr, rdram);
    m_iopSubsystem->onSifTransfer(transfer);
}

void PS2Runtime::resetIop()
{
    m_iopSubsystem->reset();
}

ps2x::iop::DebugSnapshot PS2Runtime::iopDebugSnapshot() const
{
    return m_iopSubsystem->debugSnapshot();
}

bool PS2Runtime::syncCoreSubsystems()
{
    uint8_t *const rdram = m_memory.getRDRAM();
    uint8_t *const gsVram = m_memory.getGSVRAM();
    if (!rdram || !gsVram)
    {
        return false;
    }

    if (m_boundRdram == rdram && m_boundGSVram == gsVram)
    {
        return true;
    }

    m_gs.init(gsVram, static_cast<uint32_t>(PS2_GS_VRAM_SIZE), &m_memory.gs());
    m_gifArbiter.setProcessPacketFn([this](const uint8_t *data, uint32_t size)
                                    { m_gs.processGIFPacket(data, size); });
    m_memory.setGifArbiter(&m_gifArbiter);
    m_memory.setVu1MscalCallback([this](uint32_t startPC, uint32_t top, uint32_t itop)
                                 { m_vu1.execute(m_memory.getVU1Code(), PS2_VU1_CODE_SIZE,
                                                 m_memory.getVU1Data(), PS2_VU1_DATA_SIZE,
                                                 m_gs, &m_memory, startPC, top, itop, 65536); });
    m_memory.setVu1MscntCallback([this](uint32_t top, uint32_t itop)
                                 { m_vu1.resume(m_memory.getVU1Code(), PS2_VU1_CODE_SIZE,
                                                m_memory.getVU1Data(), PS2_VU1_DATA_SIZE,
                                                m_gs, &m_memory, top, itop, 65536); });
    resetIop();
    m_vu0.reset();
    m_vu1.reset();

    m_boundRdram = rdram;
    m_boundGSVram = gsVram;
    return true;
}

bool PS2Runtime::initialize(const char *title)
{
    try
    {
        if (!m_memory.initialize())
        {
            std::cerr << "Failed to initialize PS2 memory" << std::endl;
            return false;
        }

        if (!syncCoreSubsystems())
        {
            std::cerr << "Failed to bind runtime core subsystems" << std::endl;
            return false;
        }
#if defined(PS2X_IOP_ENABLE_PLUGINS) && PS2X_IOP_ENABLE_PLUGINS && \
    !defined(PLATFORM_VITA) && (defined(_WIN32) || defined(__linux__))
        std::string pluginError;
        if (!m_iopSubsystem->loadPlugins(&pluginError))
        {
            std::cerr << "Failed to load IOP plugins: " << pluginError << std::endl;
            return false;
        }
#endif
#if defined(PLATFORM_VITA)
        InitWindow(HOST_WINDOW_WIDTH, HOST_WINDOW_HEIGHT, title); // raylib vita does not support audio
#else
        SetConfigFlags(FLAG_WINDOW_RESIZABLE);
        InitWindow(HOST_WINDOW_WIDTH, HOST_WINDOW_HEIGHT, title);
        InitAudioDevice();
        m_audioBackend.setAudioReady(IsAudioDeviceReady());
#endif
        SetTargetFPS(60);
        if (m_debugUiInitCallback)
        {
            m_debugUiInitCallback(*this, m_debugUiUserData);
            m_debugUiInitialized = true;
        }

        return true;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Failed to initialize PS2 runtime: " << e.what() << std::endl;
    }
    catch (...)
    {
        std::cerr << "Failed to initialize PS2 runtime: unknown exception" << std::endl;
    }

    return false;
}

bool PS2Runtime::loadELF(const std::string &elfPath)
{
    configureIoPathsFromElf(elfPath);

    std::ifstream file(elfPath, std::ios::binary);
    if (!file)
    {
        std::cerr << "Failed to open ELF file: " << elfPath << std::endl;
        return false;
    }

    file.seekg(0, std::ios::end);
    const std::streamoff fileSize = file.tellg();
    if (fileSize < static_cast<std::streamoff>(sizeof(ElfHeader)))
    {
        std::cerr << "ELF file is too small: " << elfPath << std::endl;
        return false;
    }
    file.seekg(0, std::ios::beg);

    ElfHeader header{};
    if (!file.read(reinterpret_cast<char *>(&header), sizeof(header)))
    {
        std::cerr << "Failed to read ELF header from: " << elfPath << std::endl;
        return false;
    }

    if (header.magic != ELF_MAGIC)
    {
        std::cerr << "Invalid ELF magic number" << std::endl;
        return false;
    }

    if (header.elf_class != 1u || header.endianness != 1u)
    {
        std::cerr << "Unsupported ELF format (expected 32-bit little-endian)." << std::endl;
        return false;
    }

    if (header.machine != EM_MIPS || header.type != ET_EXEC)
    {
        std::cerr << "Not a MIPS executable ELF file" << std::endl;
        return false;
    }

    if (header.phnum != 0u && header.phentsize < sizeof(ProgramHeader))
    {
        std::cerr << "Unsupported ELF program-header entry size: " << header.phentsize << std::endl;
        return false;
    }

    const uint64_t programHeaderTableEnd =
        static_cast<uint64_t>(header.phoff) +
        static_cast<uint64_t>(header.phnum) * static_cast<uint64_t>(header.phentsize);
    if (programHeaderTableEnd > static_cast<uint64_t>(fileSize))
    {
        std::cerr << "ELF program-header table is out of range." << std::endl;
        return false;
    }

    m_cpuContext.pc = header.entry;

    uint32_t maxLoadedRdramEnd = kGuestHeapDefaultBase;
    uint32_t moduleBase = std::numeric_limits<uint32_t>::max();
    uint32_t moduleEnd = 0u;
    bool loadedAnySegment = false;

    for (uint16_t i = 0; i < header.phnum; i++)
    {
        const uint64_t phOffset =
            static_cast<uint64_t>(header.phoff) +
            static_cast<uint64_t>(i) * static_cast<uint64_t>(header.phentsize);
        if (phOffset + sizeof(ProgramHeader) > static_cast<uint64_t>(fileSize))
        {
            std::cerr << "ELF program header " << i << " is out of range." << std::endl;
            return false;
        }

        ProgramHeader ph{};
        file.seekg(static_cast<std::streamoff>(phOffset), std::ios::beg);
        if (!file.read(reinterpret_cast<char *>(&ph), sizeof(ph)))
        {
            std::cerr << "Failed to read ELF program header " << i << std::endl;
            return false;
        }

        if (ph.type != PT_LOAD || ph.memsz == 0u)
        {
            continue;
        }

        if (ph.filesz > ph.memsz)
        {
            std::cerr << "ELF segment " << i << " has filesz > memsz." << std::endl;
            return false;
        }

        const uint64_t segmentFileEnd = static_cast<uint64_t>(ph.offset) + static_cast<uint64_t>(ph.filesz);
        if (segmentFileEnd > static_cast<uint64_t>(fileSize))
        {
            std::cerr << "ELF segment " << i << " exceeds file bounds." << std::endl;
            return false;
        }

        const bool scratch =
            ph.vaddr >= PS2_SCRATCHPAD_BASE &&
            ph.vaddr < (PS2_SCRATCHPAD_BASE + PS2_SCRATCHPAD_SIZE);

        uint32_t physAddr = 0u;
        try
        {
            physAddr = m_memory.translateAddress(ph.vaddr);
        }
        catch (const std::exception &e)
        {
            std::cerr << "Failed to translate ELF segment " << i
                      << " virtual address 0x" << std::hex << ph.vaddr
                      << std::dec << ": " << e.what() << std::endl;
            return false;
        }
        const uint64_t regionSize = scratch ? static_cast<uint64_t>(PS2_SCRATCHPAD_SIZE)
                                            : static_cast<uint64_t>(PS2_RAM_SIZE);
        const uint64_t segmentMemEnd = static_cast<uint64_t>(physAddr) + static_cast<uint64_t>(ph.memsz);
        if (segmentMemEnd > regionSize)
        {
            std::cerr << "ELF segment " << i << " exceeds "
                      << (scratch ? "scratchpad" : "RDRAM")
                      << " bounds (vaddr=0x" << std::hex << ph.vaddr
                      << " memsz=0x" << ph.memsz << std::dec << ")." << std::endl;
            return false;
        }

        uint8_t *destBase = scratch ? m_memory.getScratchpad() : m_memory.getRDRAM();
        if (!destBase)
        {
            std::cerr << "ELF segment " << i << " has no destination memory backing." << std::endl;
            return false;
        }

        uint8_t *dest = destBase + physAddr;
        if (ph.filesz > 0u)
        {
            file.seekg(static_cast<std::streamoff>(ph.offset), std::ios::beg);
            if (!file.read(reinterpret_cast<char *>(dest), ph.filesz))
            {
                std::cerr << "Failed to read ELF segment " << i << " payload." << std::endl;
                return false;
            }
        }

        if (ph.memsz > ph.filesz)
        {
            std::memset(dest + ph.filesz, 0, ph.memsz - ph.filesz);
        }

        RUNTIME_LOG("Loading segment: 0x" << std::hex << ph.vaddr
                                          << " - 0x" << (static_cast<uint64_t>(ph.vaddr) + static_cast<uint64_t>(ph.memsz))
                                          << " (filesz: 0x" << ph.filesz
                                          << ", memsz: 0x" << ph.memsz << ")"
                                          << std::dec << std::endl);

        if (!scratch)
        {
            maxLoadedRdramEnd = std::max(maxLoadedRdramEnd, static_cast<uint32_t>(segmentMemEnd));
        }

        if (ph.flags & 0x1u) // PF_X
        {
            const uint64_t execEnd = static_cast<uint64_t>(ph.vaddr) + static_cast<uint64_t>(ph.filesz);
            if (execEnd <= std::numeric_limits<uint32_t>::max())
            {
                m_memory.registerCodeRegion(ph.vaddr, static_cast<uint32_t>(execEnd));
            }
        }

        loadedAnySegment = true;
        moduleBase = std::min(moduleBase, ph.vaddr);
        const uint64_t segmentVirtualEnd = static_cast<uint64_t>(ph.vaddr) + static_cast<uint64_t>(ph.memsz);
        const uint32_t clampedVirtualEnd =
            (segmentVirtualEnd > std::numeric_limits<uint32_t>::max())
                ? std::numeric_limits<uint32_t>::max()
                : static_cast<uint32_t>(segmentVirtualEnd);
        moduleEnd = std::max(moduleEnd, clampedVirtualEnd);
    }

    if (!loadedAnySegment)
    {
        std::cerr << "ELF contains no loadable PT_LOAD segments." << std::endl;
        return false;
    }

    if (maxLoadedRdramEnd > PS2_RAM_SIZE)
    {
        maxLoadedRdramEnd = PS2_RAM_SIZE;
    }

    const uint32_t paddedEnd = (maxLoadedRdramEnd > (PS2_RAM_SIZE - kGuestHeapSafetyPad))
                                   ? PS2_RAM_SIZE
                                   : (maxLoadedRdramEnd + kGuestHeapSafetyPad);
    const uint32_t suggestedHeapBase = alignGuestHeapValue(paddedEnd, kGuestHeapDefaultAlignment);
    {
        std::lock_guard<std::mutex> lock(m_guestHeapMutex);
        if (!m_guestHeapConfigured)
        {
            const uint32_t hardLimit = std::min(kGuestHeapHardLimit, PS2_RAM_SIZE);
            m_guestHeapSuggestedBase = std::min(suggestedHeapBase, hardLimit);
            m_guestHeapBase = m_guestHeapSuggestedBase;
            m_guestHeapEnd = m_guestHeapSuggestedBase;
            m_guestHeapLimit = hardLimit;
        }
    }
    {
        std::lock_guard<std::mutex> lock(m_asyncCallbackStackMutex);
        // Re-arm the kernel pool for this load (see the layout comment at
        // kAsyncCallbackStackFloor); a prior run may have carved it down.
        m_asyncCallbackStack.reset();
    }

    LoadedModule module;
    module.name = elfPath.substr(elfPath.find_last_of("/\\") + 1);
    module.baseAddress = (moduleBase == std::numeric_limits<uint32_t>::max()) ? 0x00100000u : moduleBase;
    module.size = (moduleEnd > module.baseAddress) ? static_cast<size_t>(moduleEnd - module.baseAddress) : 0u;
    module.active = true;

    m_loadedModules.push_back(module);

    uint32_t elfCrc32 = 0u;
    const bool elfCrc32Valid = computeFileCrc32(elfPath, elfCrc32);
    if (!elfCrc32Valid)
    {
        std::cerr << "[ps2xIOP] failed to compute ELF CRC32 for '" << elfPath << "'" << std::endl;
    }
    ps2x::iop::GameIdentity identity;
    identity.elfName = module.name;
    identity.entryPoint = m_cpuContext.pc;
    identity.crc32 = elfCrc32;
    std::string iopError;
    if (!m_iopSubsystem->configure(identity, &iopError))
    {
        std::cerr << "[ps2xIOP] failed to configure profile: " << iopError << std::endl;
        return false;
    }

    ps2_game_overrides::applyMatching(*this,
                                      elfPath,
                                      m_cpuContext.pc,
                                      elfCrc32,
                                      elfCrc32Valid);

    RUNTIME_LOG("ELF file loaded successfully. Entry point: 0x" << std::hex << m_cpuContext.pc << std::dec);
    return true;
}

const PS2Runtime::IoPaths &PS2Runtime::getIoPaths()
{
    return runtimeIoPaths();
}

void PS2Runtime::setIoPaths(const IoPaths &paths)
{
    IoPaths normalized = paths;
    normalized.elfPath = normalizeAbsolutePath(normalized.elfPath);
    normalized.elfDirectory = normalizeAbsolutePath(normalized.elfDirectory);
    normalized.hostRoot = normalizeAbsolutePath(normalized.hostRoot);
    normalized.cdRoot = normalizeAbsolutePath(normalized.cdRoot);
    normalized.mcRoot = normalizeAbsolutePath(normalized.mcRoot);
    normalized.cdImage = normalizeAbsolutePath(normalized.cdImage);

    if (normalized.elfDirectory.empty() && !normalized.elfPath.empty())
    {
        normalized.elfDirectory = normalized.elfPath.parent_path();
    }

    if (normalized.hostRoot.empty())
    {
        normalized.hostRoot = normalized.elfDirectory;
    }
    if (normalized.cdRoot.empty())
    {
        normalized.cdRoot = normalized.elfDirectory;
    }
    if (normalized.mcRoot.empty())
    {
        normalized.mcRoot = normalized.elfDirectory / "mc0";
    }

    runtimeIoPaths() = normalized;
}

void PS2Runtime::configureIoPathsFromElf(const std::string &elfPath)
{
    IoPaths paths = runtimeIoPaths();
    paths.elfPath = normalizeAbsolutePath(std::filesystem::path(elfPath));
    if (!paths.elfPath.empty())
    {
        paths.elfDirectory = paths.elfPath.parent_path();
    }

    if (!paths.elfDirectory.empty())
    {
        paths.hostRoot = paths.elfDirectory;
        paths.cdRoot = paths.elfDirectory;
        paths.mcRoot = paths.elfDirectory / "mc0";
    }

    setIoPaths(paths);
}

namespace
{
    bool generatedFunctionTableSlot(uint32_t address, uint32_t &slot)
    {
        if ((address & 3u) != 0u || g_ps2RecompiledFunctionTableSlotCount == 0u)
        {
            return false;
        }

        if (address < g_ps2RecompiledFunctionTableBase || address >= g_ps2RecompiledFunctionTableEnd)
        {
            return false;
        }

        const uint32_t offset = address - g_ps2RecompiledFunctionTableBase;
        slot = offset >> 2;
        return slot < g_ps2RecompiledFunctionTableSlotCount;
    }
}

bool PS2Runtime::replaceFunction(uint32_t address, RecompiledFunction func)
{
    uint32_t slot = 0u;
    if (!generatedFunctionTableSlot(address, slot))
    {
        std::cerr << "[function-table] cannot replace guest PC 0x" << std::hex << address
                  << ": outside generated dense table [0x" << g_ps2RecompiledFunctionTableBase
                  << ", 0x" << g_ps2RecompiledFunctionTableEnd << ")"
                  << std::dec << std::endl;
        return false;
    }

    g_ps2RecompiledFunctionTable[slot] = func;
    return true;
}

bool PS2Runtime::registerFunction(uint32_t address, RecompiledFunction func)
{
    return replaceFunction(address, func);
}

bool PS2Runtime::hasFunction(uint32_t address) const
{
    uint32_t slot = 0u;
    return generatedFunctionTableSlot(address, slot) && g_ps2RecompiledFunctionTable[slot] != nullptr;
}

const char *describeGuestBranchKind(PS2Runtime::GuestBranchKind kind)
{
    switch (kind)
    {
    case PS2Runtime::GuestBranchKind::DirectJump:
        return "DirectJump";
    case PS2Runtime::GuestBranchKind::DirectCall:
        return "DirectCall";
    case PS2Runtime::GuestBranchKind::IndirectJump:
        return "IndirectJump";
    case PS2Runtime::GuestBranchKind::IndirectCall:
        return "IndirectCall";
    case PS2Runtime::GuestBranchKind::Return:
        return "Return";
    default:
        return "Unknown";
    }
}

PS2Runtime::RecompiledFunction PS2Runtime::lookupFunction(uint32_t address)
{
    pushDispatchPc(address);

    uint32_t slot = 0u;
    if (generatedFunctionTableSlot(address, slot))
    {
        RecompiledFunction fn = g_ps2RecompiledFunctionTable[slot];
        if (fn != nullptr)
        {
            return fn;
        }
    }

    std::cerr << "Error: No exact recompiled function for guest PC 0x" << std::hex << address
              << " tableBase=0x" << g_ps2RecompiledFunctionTableBase
              << " tableEnd=0x" << g_ps2RecompiledFunctionTableEnd
              << " codeRegion=" << (m_memory.isCodeAddress(address) ? "yes" : "no")
              << " trace=" << formatDispatchHistory()
              << std::dec << std::endl;

    static RecompiledFunction missingFunction = [](uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t badPc = ctx->pc;
        runtime->reportMissingFunction(rdram,
                                       ctx,
                                       badPc,
                                       0u,
                                       PS2Runtime::GuestBranchKind::IndirectJump,
                                       "dispatch");
    };

    return missingFunction;
}

void PS2Runtime::setMissingFunctionPolicy(MissingFunctionPolicy policy)
{
    m_missingFunctionPolicy.store(static_cast<uint32_t>(policy), std::memory_order_release);
}

PS2Runtime::MissingFunctionPolicy PS2Runtime::missingFunctionPolicy() const
{
    return static_cast<MissingFunctionPolicy>(m_missingFunctionPolicy.load(std::memory_order_acquire));
}

void PS2Runtime::resetMissingFunctionReportOnce()
{
    m_missingFunctionReported.store(false, std::memory_order_release);
}

std::string PS2Runtime::debugCurrentDispatchTrace() const
{
    return formatDispatchHistory();
}

void PS2Runtime::reportMissingFunction(uint8_t *rdram,
                                       R5900Context *ctx,
                                       uint32_t targetPc,
                                       uint32_t sourcePc,
                                       GuestBranchKind kind,
                                       const char *debugName)
{
    const MissingFunctionPolicy policy = missingFunctionPolicy();
    const bool firstReport = !m_missingFunctionReported.exchange(true, std::memory_order_acq_rel);

    const uint32_t pc = ctx->pc;
    const uint32_t ra = static_cast<uint32_t>(_mm_extract_epi32(ctx->r[31], 0));
    const uint32_t sp = static_cast<uint32_t>(_mm_extract_epi32(ctx->r[29], 0));
    const uint32_t gp = static_cast<uint32_t>(_mm_extract_epi32(ctx->r[28], 0));
    const uint32_t a0 = static_cast<uint32_t>(_mm_extract_epi32(ctx->r[4], 0));
    const uint32_t a1 = static_cast<uint32_t>(_mm_extract_epi32(ctx->r[5], 0));
    const uint32_t v0 = static_cast<uint32_t>(_mm_extract_epi32(ctx->r[2], 0));
    const uint32_t v1 = static_cast<uint32_t>(_mm_extract_epi32(ctx->r[3], 0));

    auto readGuestU32At = [rdram](uint32_t addr, uint32_t &out) -> bool
    {
        // TODO this !rdram exist only because of test fix those test later
        if (!rdram || addr > PS2_RAM_SIZE - sizeof(uint32_t))
        {
            out = 0u;
            return false;
        }

        std::memcpy(&out, rdram + addr, sizeof(uint32_t));
        return true;
    };

    auto readGuestU32Offset = [&readGuestU32At](uint32_t base, uint32_t offset, uint32_t &out) -> bool
    {
        if (base > PS2_RAM_SIZE - sizeof(uint32_t) || offset > PS2_RAM_SIZE - sizeof(uint32_t) - base)
        {
            out = 0u;
            return false;
        }

        return readGuestU32At(base + offset, out);
    };

    uint32_t a0Word0 = 0u;
    uint32_t a0Word4 = 0u;
    uint32_t a0Word8 = 0u;
    uint32_t a0WordC = 0u;
    const bool a0Readable =
        readGuestU32Offset(a0, 0x00u, a0Word0) &&
        readGuestU32Offset(a0, 0x04u, a0Word4) &&
        readGuestU32Offset(a0, 0x08u, a0Word8) &&
        readGuestU32Offset(a0, 0x0cu, a0WordC);

    uint32_t vtableSlot0 = 0u;
    uint32_t vtableSlot4 = 0u;
    uint32_t vtableSlot8 = 0u;
    uint32_t vtableSlotC = 0u;
    const bool vtableReadable =
        a0Readable && a0Word0 != 0u &&
        readGuestU32Offset(a0Word0, 0x00u, vtableSlot0) &&
        readGuestU32Offset(a0Word0, 0x04u, vtableSlot4) &&
        readGuestU32Offset(a0Word0, 0x08u, vtableSlot8) &&
        readGuestU32Offset(a0Word0, 0x0cu, vtableSlotC);

    if (firstReport)
    {
        std::ostringstream oss;
        oss << "[guest-branch:missing-target] kind=" << describeGuestBranchKind(kind)
            << " op=" << (debugName ? debugName : "<unknown>")
            << " source=0x" << std::hex << sourcePc
            << " target=0x" << targetPc
            << " pc=0x" << pc
            << " ra=0x" << ra
            << " sp=0x" << sp
            << " gp=0x" << gp
            << " a0=0x" << a0
            << " a1=0x" << a1
            << " v0=0x" << v0
            << " v1=0x" << v1
            << " a0Readable=" << (a0Readable ? "yes" : "no")
            << " a0[0]=0x" << a0Word0
            << " a0[4]=0x" << a0Word4
            << " a0[8]=0x" << a0Word8
            << " a0[c]=0x" << a0WordC
            << " vtableReadable=" << (vtableReadable ? "yes" : "no")
            << " vtbl[0]=0x" << vtableSlot0
            << " vtbl[4]=0x" << vtableSlot4
            << " vtbl[8]=0x" << vtableSlot8
            << " vtbl[c]=0x" << vtableSlotC
            << " codeRegion=" << (m_memory.isCodeAddress(targetPc) ? "yes" : "no")
            << " policy=" << static_cast<uint32_t>(policy)
            << " trace=" << formatDispatchHistory()
            << std::dec;

        static std::mutex s_missingFunctionLogMutex;
        {
            std::lock_guard<std::mutex> lock(s_missingFunctionLogMutex);
            std::cerr << oss.str() << std::endl;
        }
    }

    if (firstReport && policy == MissingFunctionPolicy::BreakOnce)
    {
#if defined(_MSC_VER)
        __debugbreak();
#endif // TODO others breakpoints
    }

    if (ctx)
    {
        ctx->pc = targetPc;
    }

    if (policy == MissingFunctionPolicy::Stop)
    {
        requestStop();
    }
}

bool PS2Runtime::dispatchGuestBranch(uint8_t *rdram,
                                     R5900Context *ctx,
                                     uint32_t targetPc,
                                     uint32_t sourcePc,
                                     uint32_t fallthroughPc,
                                     GuestBranchKind kind,
                                     const char *debugName)
{
    ctx->pc = targetPc;
    const bool isCall = (kind == GuestBranchKind::DirectCall || kind == GuestBranchKind::IndirectCall);

    if (kind == GuestBranchKind::Return)
    {
        if (!hasFunction(targetPc))
        {
            reportMissingFunction(rdram, ctx, targetPc, sourcePc, kind, debugName);
        }

        // Prevent nested dispatch.
        ctx->pc = targetPc;
        return false;
    }

    if (!hasFunction(targetPc))
    {
        reportMissingFunction(rdram, ctx, targetPc, sourcePc, kind, debugName);

        const MissingFunctionPolicy policy = missingFunctionPolicy();

        if (policy == MissingFunctionPolicy::SkipCallDebug && isCall)
        {
            ctx->pc = fallthroughPc;
            return true;
        }

        if (policy == MissingFunctionPolicy::ContinueToTarget)
        {
            ctx->pc = targetPc;
            return true;
        }

        return false;
    }

    RecompiledFunction targetFn = lookupFunction(targetPc);
    const uint32_t entryPc = ctx->pc;
    targetFn(rdram, ctx, this);

    if (isStopRequested() || ctx->pc == 0u)
    {
        return false;
    }

    if (!isCall)
    {
        return false;
    }

    if (ctx->pc == entryPc)
    {
        ctx->pc = fallthroughPc;
    }

    return ctx->pc == fallthroughPc;
}

void PS2Runtime::SignalException(R5900Context *ctx, PS2Exception exception)
{
    if (exception == EXCEPTION_INTEGER_OVERFLOW)
    {
        HandleIntegerOverflow(ctx);
        return;
    }

    raiseCop0Exception(ctx, static_cast<uint32_t>(exception),
                       exception == EXCEPTION_TLB_REFILL);
}

void PS2Runtime::executeVU0Microprogram(uint8_t *rdram, R5900Context *ctx, uint32_t address)
{
    (void)rdram;

    uint8_t *const vu0Code = m_memory.getVU0Code();
    uint8_t *const vu0Data = m_memory.getVU0Data();
    const uint32_t startPC = address & ~0x7u;

    if (!vu0Code || !vu0Data || startPC + 8u > PS2_VU0_CODE_SIZE)
    {
        seedVu0IdleSuccess(ctx);
        return;
    }

    m_vu0.reset();
    copyVu0ContextToState(ctx, m_vu0.state());
    m_vu0.execute(vu0Code, PS2_VU0_CODE_SIZE,
                  vu0Data, PS2_VU0_DATA_SIZE,
                  m_gs, &m_memory,
                  startPC, 0u, ctx->vu0_itop, 4096);
    copyVu0StateToContext(m_vu0.state(), ctx);
}

void PS2Runtime::vu0StartMicroProgram(uint8_t *rdram, R5900Context *ctx, uint32_t address)
{
    // VCALLMS and VCALLMSR both route here.
    executeVU0Microprogram(rdram, ctx, address);
}

void PS2Runtime::handleSyscall(uint8_t *rdram, R5900Context *ctx)
{
    handleSyscall(rdram, ctx, 0);
}

void PS2Runtime::handleSyscall(uint8_t *rdram, R5900Context *ctx, uint32_t encodedSyscallId)
{
    if (ctx->in_delay_slot)
    {
        throw std::runtime_error("Attempted to execute a syscall inside a branch delay slot! "
                                 "This breaks the atomic basic block model and is structurally unsupported by the emulator.");
    }

    const uint32_t syscallId = (encodedSyscallId != 0u)
                                   ? encodedSyscallId
                                   : getRegU32(ctx, 3); // $v1 / $3 is the EE kernel syscall number

    if (ps2_syscalls::dispatchNumericSyscall(syscallId, rdram, ctx, this))
    {
        return;
    }

    // God help you
    ps2_syscalls::TODO(rdram, ctx, this, encodedSyscallId);
}

void PS2Runtime::handleBreak(uint8_t *rdram, R5900Context *ctx)
{
    raiseCop0Exception(ctx, EXCEPTION_BREAKPOINT);
}

void PS2Runtime::drainCompletedDmacHandlers(uint8_t *rdram)
{
    for (uint32_t cause : m_memory.consumeCompletedDmacCauses())
    {
        ps2_syscalls::dispatchDmacHandlersForCause(rdram, this, cause);
    }
}

void PS2Runtime::handleTrap(uint8_t *rdram, R5900Context *ctx)
{
    raiseCop0Exception(ctx, EXCEPTION_TRAP);
}

void PS2Runtime::handleTLBR(uint8_t *rdram, R5900Context *ctx)
{
    uint32_t vpn = 0;
    uint32_t pfn = 0;
    uint32_t mask = 0;
    bool valid = false;

    const uint32_t index = ctx->cop0_index & 0x3Fu;
    if (!m_memory.tlbRead(index, vpn, pfn, mask, valid))
    {
        raiseCop0Exception(ctx, EXCEPTION_RESERVED_INSTRUCTION);
        return;
    }

    // Preserve low ASID bits in EntryHi.
    ctx->cop0_entryhi = (ctx->cop0_entryhi & 0x00000FFFu) | (vpn & 0xFFFFF000u);
    ctx->cop0_entrylo0 = (ctx->cop0_entrylo0 & ~0x03FFFFC2u) |
                         ((pfn & 0x000FFFFFu) << 6) |
                         (valid ? 0x2u : 0u);
    ctx->cop0_pagemask = mask & 0x01FFE000u;
}

void PS2Runtime::handleTLBWI(uint8_t *rdram, R5900Context *ctx)
{
    const uint32_t index = ctx->cop0_index & 0x3Fu;
    const uint32_t vpn = ctx->cop0_entryhi & 0xFFFFF000u;
    const uint32_t pfn = (ctx->cop0_entrylo0 >> 6) & 0x000FFFFFu;
    const uint32_t mask = ctx->cop0_pagemask & 0x01FFE000u;
    const bool valid = (ctx->cop0_entrylo0 & 0x2u) != 0u;

    if (!m_memory.tlbWrite(index, vpn, pfn, mask, valid))
    {
        raiseCop0Exception(ctx, EXCEPTION_RESERVED_INSTRUCTION);
    }
}

void PS2Runtime::handleTLBWR(uint8_t *rdram, R5900Context *ctx)
{
    const uint32_t entryCount = static_cast<uint32_t>(m_memory.tlbEntryCount());
    if (entryCount == 0)
    {
        raiseCop0Exception(ctx, EXCEPTION_RESERVED_INSTRUCTION);
        return;
    }

    const uint32_t wired = std::min(ctx->cop0_wired, entryCount - 1);
    uint32_t random = ctx->cop0_random % entryCount;
    if (random < wired)
    {
        random = wired;
    }

    const uint32_t vpn = ctx->cop0_entryhi & 0xFFFFF000u;
    const uint32_t pfn = (ctx->cop0_entrylo0 >> 6) & 0x000FFFFFu;
    const uint32_t mask = ctx->cop0_pagemask & 0x01FFE000u;
    const bool valid = (ctx->cop0_entrylo0 & 0x2u) != 0u;

    if (!m_memory.tlbWrite(random, vpn, pfn, mask, valid))
    {
        raiseCop0Exception(ctx, EXCEPTION_RESERVED_INSTRUCTION);
        return;
    }

    // Keep COP0 bookkeeping in sync with the selected slot.
    ctx->cop0_index = (ctx->cop0_index & ~0x3Fu) | (random & 0x3Fu);
    ctx->cop0_random = (random <= wired) ? (entryCount - 1) : (random - 1);
}

void PS2Runtime::handleTLBP(uint8_t *rdram, R5900Context *ctx)
{
    const int32_t index = m_memory.tlbProbe(ctx->cop0_entryhi & 0xFFFFF000u);
    if (index >= 0)
    {
        ctx->cop0_index = (ctx->cop0_index & ~0x8000003Fu) |
                          (static_cast<uint32_t>(index) & 0x3Fu);
    }
    else
    {
        // MIPS sets probe failure bit (P) in Index[31].
        ctx->cop0_index |= 0x80000000u;
    }
}

void PS2Runtime::clearLLBit(R5900Context *ctx)
{
    // LL/SC reservation is tracked separately from COP0 Status.
    ctx->llbit = 0;
    ctx->lladdr = 0;
}

uint32_t PS2Runtime::alignGuestHeapValue(uint32_t value, uint32_t alignment)
{
    if (alignment == 0)
    {
        return value;
    }

    const uint32_t mask = alignment - 1u;
    if (value > (std::numeric_limits<uint32_t>::max() - mask))
    {
        return std::numeric_limits<uint32_t>::max();
    }
    return (value + mask) & ~mask;
}

bool PS2Runtime::isGuestHeapAlignmentValid(uint32_t alignment)
{
    return alignment != 0u && (alignment & (alignment - 1u)) == 0u;
}

uint32_t PS2Runtime::normalizeGuestHeapAlignment(uint32_t alignment)
{
    if (!isGuestHeapAlignmentValid(alignment))
    {
        return kGuestHeapDefaultAlignment;
    }
    return std::max(alignment, kGuestHeapDefaultAlignment);
}

uint32_t PS2Runtime::clampGuestHeapBase(uint32_t guestBase) const
{
    uint32_t normalized = guestBase;
    if (normalized >= PS2_RAM_SIZE)
    {
        normalized &= PS2_RAM_MASK;
    }
    const uint32_t hardLimit = std::min(kGuestHeapHardLimit, PS2_RAM_SIZE);
    return std::min(normalized, hardLimit);
}

uint32_t PS2Runtime::clampGuestHeapLimit(uint32_t guestLimit) const
{
    const uint32_t hardLimit = std::min(kGuestHeapHardLimit, PS2_RAM_SIZE);
    if (guestLimit == 0u || guestLimit > hardLimit)
    {
        return hardLimit;
    }
    return guestLimit;
}

void PS2Runtime::resetGuestHeapLocked(uint32_t guestBase, uint32_t guestLimit)
{
    uint32_t base = alignGuestHeapValue(clampGuestHeapBase(guestBase), kGuestHeapDefaultAlignment);
    uint32_t limit = clampGuestHeapLimit(guestLimit);
    if (base == 0u)
    {
        const uint32_t fallbackBase = (m_guestHeapSuggestedBase != 0u) ? m_guestHeapSuggestedBase : kGuestHeapDefaultBase;
        base = alignGuestHeapValue(clampGuestHeapBase(fallbackBase), kGuestHeapDefaultAlignment);
    }

    if (limit <= base)
    {
        base = alignGuestHeapValue(clampGuestHeapBase(m_guestHeapSuggestedBase), kGuestHeapDefaultAlignment);
        limit = clampGuestHeapLimit(0u);
    }

    if (limit <= base)
    {
        base = 0u;
        limit = 0u;
    }

    m_guestHeapBlocks.clear();
    if (limit > base)
    {
        m_guestHeapBlocks.push_back({base, limit - base, true});
    }

    m_guestHeapBase = base;
    m_guestHeapEnd = base;
    m_guestHeapLimit = limit;
    m_guestHeapConfigured = true;
}

void PS2Runtime::ensureGuestHeapInitializedLocked()
{
    if (m_guestHeapConfigured)
    {
        return;
    }

    const uint32_t suggested = (m_guestHeapSuggestedBase == 0u) ? kGuestHeapDefaultBase : m_guestHeapSuggestedBase;
    resetGuestHeapLocked(suggested, clampGuestHeapLimit(0u));
}

int32_t PS2Runtime::findGuestHeapBlockIndexLocked(uint32_t guestAddr) const
{
    const uint32_t normalizedAddr = guestAddr & PS2_RAM_MASK;
    for (size_t i = 0; i < m_guestHeapBlocks.size(); ++i)
    {
        const GuestHeapBlock &block = m_guestHeapBlocks[i];
        if (!block.free && block.addr == normalizedAddr)
        {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

uint32_t PS2Runtime::allocateGuestBlockLocked(uint32_t size, uint32_t alignment)
{
    if (size == 0u)
    {
        return 0u;
    }

    const uint32_t normalizedAlignment = normalizeGuestHeapAlignment(alignment);
    if (size > (std::numeric_limits<uint32_t>::max() - (kGuestHeapDefaultAlignment - 1u)))
    {
        return 0u;
    }

    const uint32_t allocSize = alignGuestHeapValue(size, kGuestHeapDefaultAlignment);
    if (allocSize == 0u)
    {
        return 0u;
    }

    for (size_t i = 0; i < m_guestHeapBlocks.size(); ++i)
    {
        const GuestHeapBlock block = m_guestHeapBlocks[i];
        if (!block.free)
        {
            continue;
        }

        const uint64_t blockStart = block.addr;
        const uint64_t blockEnd = blockStart + static_cast<uint64_t>(block.size);
        const uint32_t alignedAddr = alignGuestHeapValue(block.addr, normalizedAlignment);
        if (alignedAddr < block.addr)
        {
            continue;
        }

        const uint64_t alignedStart = alignedAddr;
        if (alignedStart > blockEnd)
        {
            continue;
        }

        const uint64_t allocEnd = alignedStart + static_cast<uint64_t>(allocSize);
        if (allocEnd > blockEnd)
        {
            continue;
        }

        const uint32_t prefixSize = static_cast<uint32_t>(alignedStart - blockStart);
        const uint32_t suffixSize = static_cast<uint32_t>(blockEnd - allocEnd);

        std::vector<GuestHeapBlock> replacement;
        replacement.reserve(3);
        if (prefixSize > 0u)
        {
            replacement.push_back({block.addr, prefixSize, true});
        }
        replacement.push_back({alignedAddr, allocSize, false});
        if (suffixSize > 0u)
        {
            replacement.push_back({static_cast<uint32_t>(allocEnd), suffixSize, true});
        }

        m_guestHeapBlocks.erase(m_guestHeapBlocks.begin() + static_cast<std::ptrdiff_t>(i));
        m_guestHeapBlocks.insert(m_guestHeapBlocks.begin() + static_cast<std::ptrdiff_t>(i),
                                 replacement.begin(),
                                 replacement.end());

        m_guestHeapEnd = std::max(m_guestHeapEnd, static_cast<uint32_t>(allocEnd));
        return alignedAddr;
    }

    return 0u;
}

void PS2Runtime::coalesceGuestHeapLocked()
{
    if (m_guestHeapBlocks.empty())
    {
        return;
    }

    size_t i = 1;
    while (i < m_guestHeapBlocks.size())
    {
        GuestHeapBlock &prev = m_guestHeapBlocks[i - 1];
        GuestHeapBlock &curr = m_guestHeapBlocks[i];
        const uint64_t prevEnd = static_cast<uint64_t>(prev.addr) + static_cast<uint64_t>(prev.size);
        if (prev.free && curr.free && prevEnd == curr.addr)
        {
            prev.size += curr.size;
            m_guestHeapBlocks.erase(m_guestHeapBlocks.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        ++i;
    }
}

void PS2Runtime::freeGuestBlockLocked(uint32_t guestAddr)
{
    const int32_t index = findGuestHeapBlockIndexLocked(guestAddr);
    if (index < 0)
    {
        return;
    }

    m_guestHeapBlocks[static_cast<size_t>(index)].free = true;
    coalesceGuestHeapLocked();
}

void PS2Runtime::configureGuestHeap(uint32_t guestBase, uint32_t guestLimit)
{
    std::lock_guard<std::mutex> lock(m_guestHeapMutex);
    uint32_t normalizedBase = alignGuestHeapValue(clampGuestHeapBase(guestBase), kGuestHeapDefaultAlignment);
    if (normalizedBase == 0u)
    {
        normalizedBase = (m_guestHeapSuggestedBase != 0u) ? m_guestHeapSuggestedBase : kGuestHeapDefaultBase;
    }
    m_guestHeapSuggestedBase = normalizedBase;
    resetGuestHeapLocked(normalizedBase, guestLimit);
}

uint32_t PS2Runtime::guestMalloc(uint32_t size, uint32_t alignment)
{
    std::lock_guard<std::mutex> lock(m_guestHeapMutex);
    ensureGuestHeapInitializedLocked();
    return allocateGuestBlockLocked(size, alignment);
}

uint32_t PS2Runtime::guestCalloc(uint32_t count, uint32_t size, uint32_t alignment)
{
    if (count == 0u || size == 0u)
    {
        return 0u;
    }
    if (count > (std::numeric_limits<uint32_t>::max() / size))
    {
        return 0u;
    }

    const uint32_t totalSize = count * size;
    const uint32_t guestAddr = guestMalloc(totalSize, alignment);
    if (guestAddr != 0u)
    {
        uint8_t *rdram = m_memory.getRDRAM();
        if (rdram)
        {
            uint32_t physAddr = guestAddr & PS2_RAM_MASK;
            if (physAddr + totalSize <= PS2_RAM_SIZE)
                std::memset(rdram + physAddr, 0, totalSize);
        }
    }

    return guestAddr;
}

uint32_t PS2Runtime::guestRealloc(uint32_t guestAddr, uint32_t newSize, uint32_t alignment)
{
    if (guestAddr == 0u)
    {
        return guestMalloc(newSize, alignment);
    }
    if (newSize == 0u)
    {
        guestFree(guestAddr);
        return 0u;
    }

    if (newSize > (std::numeric_limits<uint32_t>::max() - (kGuestHeapDefaultAlignment - 1u)))
    {
        return 0u;
    }

    const uint32_t normalizedAlignment = normalizeGuestHeapAlignment(alignment);
    const uint32_t requestedSize = alignGuestHeapValue(newSize, kGuestHeapDefaultAlignment);

    std::lock_guard<std::mutex> lock(m_guestHeapMutex);
    ensureGuestHeapInitializedLocked();

    const int32_t index = findGuestHeapBlockIndexLocked(guestAddr);
    if (index < 0)
    {
        return 0u;
    }

    const size_t blockIndex = static_cast<size_t>(index);
    const uint32_t oldAddr = m_guestHeapBlocks[blockIndex].addr;
    const uint32_t oldSize = m_guestHeapBlocks[blockIndex].size;

    if (requestedSize <= oldSize)
    {
        if (requestedSize < oldSize)
        {
            const uint32_t tailAddr = oldAddr + requestedSize;
            const uint32_t tailSize = oldSize - requestedSize;
            m_guestHeapBlocks[blockIndex].size = requestedSize;
            m_guestHeapBlocks.insert(m_guestHeapBlocks.begin() + static_cast<std::ptrdiff_t>(blockIndex + 1u),
                                     GuestHeapBlock{tailAddr, tailSize, true});
            coalesceGuestHeapLocked();
        }
        return oldAddr;
    }

    if (blockIndex + 1u < m_guestHeapBlocks.size())
    {
        GuestHeapBlock &next = m_guestHeapBlocks[blockIndex + 1u];
        const uint64_t blockEnd = static_cast<uint64_t>(m_guestHeapBlocks[blockIndex].addr) +
                                  static_cast<uint64_t>(m_guestHeapBlocks[blockIndex].size);
        if (next.free && blockEnd == next.addr)
        {
            const uint64_t combined = static_cast<uint64_t>(m_guestHeapBlocks[blockIndex].size) +
                                      static_cast<uint64_t>(next.size);
            if (combined >= requestedSize)
            {
                const uint32_t extraNeeded = requestedSize - m_guestHeapBlocks[blockIndex].size;
                m_guestHeapBlocks[blockIndex].size = requestedSize;
                if (next.size == extraNeeded)
                {
                    m_guestHeapBlocks.erase(m_guestHeapBlocks.begin() + static_cast<std::ptrdiff_t>(blockIndex + 1u));
                }
                else
                {
                    next.addr += extraNeeded;
                    next.size -= extraNeeded;
                }
                m_guestHeapEnd = std::max(m_guestHeapEnd, oldAddr + requestedSize);
                return oldAddr;
            }
        }
    }

    const uint32_t newAddr = allocateGuestBlockLocked(newSize, normalizedAlignment);
    if (newAddr == 0u)
    {
        return 0u;
    }

    uint8_t *rdram = m_memory.getRDRAM();
    if (rdram)
    {
        const uint32_t copyBytes = std::min(oldSize, newSize);
        uint32_t dstPhys = newAddr & PS2_RAM_MASK;
        uint32_t srcPhys = oldAddr & PS2_RAM_MASK;
        if (dstPhys + copyBytes <= PS2_RAM_SIZE && srcPhys + copyBytes <= PS2_RAM_SIZE)
            std::memmove(rdram + dstPhys, rdram + srcPhys, copyBytes);
    }

    freeGuestBlockLocked(oldAddr);
    return newAddr;
}

void PS2Runtime::guestFree(uint32_t guestAddr)
{
    if (guestAddr == 0u)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(m_guestHeapMutex);
    ensureGuestHeapInitializedLocked();
    freeGuestBlockLocked(guestAddr);
}

uint32_t PS2Runtime::guestHeapBase() const
{
    std::lock_guard<std::mutex> lock(m_guestHeapMutex);
    return m_guestHeapConfigured ? m_guestHeapBase : m_guestHeapSuggestedBase;
}

uint32_t PS2Runtime::guestHeapEnd() const
{
    std::lock_guard<std::mutex> lock(m_guestHeapMutex);
    return m_guestHeapConfigured ? m_guestHeapEnd : m_guestHeapSuggestedBase;
}

uint32_t PS2Runtime::guestHeapLimit() const
{
    std::lock_guard<std::mutex> lock(m_guestHeapMutex);
    return m_guestHeapConfigured ? m_guestHeapLimit : m_guestHeapSuggestedBase;
}

// Carve arithmetic lifted verbatim out of reserveAsyncCallbackStack(). Two
// clamps present in the pre-encapsulation version were confirmed dead and
// dropped here rather than ported:
//   - `if (top > PS2_RAM_SIZE) top = PS2_RAM_SIZE;` — top starts at
//     kAsyncCallbackStackTop (0x00100000) and every successful carve strictly
//     decreases it (base < top is required to succeed), so top can never
//     approach PS2_RAM_SIZE (0x02000000); the branch never fired.
//   - the per-call `top &= ~(kGuestHeapDefaultAlignment - 1u)` re-align —
//     top starts 16-aligned (0x00100000) and every stored top is a `base`
//     that was itself masked to `align`, which normalizeGuestHeapAlignment()
//     guarantees is a power of two >= kGuestHeapDefaultAlignment (16); a
//     value aligned to a multiple of 16 is already 16-aligned, so the
//     re-align was always a no-op.
uint32_t KernelStackPool::carve(uint32_t size, uint32_t align)
{
    if (top <= size)
    {
        return 0u;
    }

    uint32_t base = top - size;
    base &= ~(align - 1u);
    if (base < floor || base >= top)
    {
        return 0u;
    }

    const uint32_t reservedTop = top;
    top = base;
    // One line per reservation (a handful per boot): permanent evidence of
    // where host-dispatched callback stacks live, so any future overlap with
    // guest memory is visible in the boot log.
    std::cerr << "[async-stack] reserved [0x" << std::hex << base
              << ", 0x" << reservedTop << ") stackTop=0x" << (reservedTop - 0x10u)
              << std::dec << '\n';
    return reservedTop - 0x10u;
}

uint32_t PS2Runtime::reserveAsyncCallbackStack(uint32_t size, uint32_t alignment)
{
    if (size == 0u)
    {
        return 0u;
    }

    const uint32_t normalizedAlignment = normalizeGuestHeapAlignment(alignment);
    const uint32_t allocSize = alignGuestHeapValue(size, kGuestHeapDefaultAlignment);
    if (allocSize == 0u)
    {
        return 0u;
    }

    std::lock_guard<std::mutex> lock(m_asyncCallbackStackMutex);
    return m_asyncCallbackStack.carve(allocSize, normalizedAlignment);
}

void PS2Runtime::dispatchLoop(uint8_t *rdram, R5900Context *ctx)
{
    uint32_t lastPc = std::numeric_limits<uint32_t>::max();
    uint32_t samePcCount = 0;
    constexpr uint32_t kSamePcYieldInterval = 0x4000u;

    while (!isStopRequested())
    {
        // Cooperative scheduling point. The recompiler emits the
        // shouldPreemptGuestExecution() hook only at INTRA-function back-edges;
        // a guest loop that spins ACROSS function dispatches (call/return
        // chains, recover-pc storms) has its back-edge HERE, not inside any
        // recompiled function, so without this call such a loop never reaches
        // yield_point() and holds the guest token forever, starving host
        // workers (interrupt worker VBlank/INTC delivery) parked in
        // async_guest_begin(). The fast path is a counter test, so this is as
        // cheap as the emitted per-back-edge checks. The return value is
        // irrelevant: whether or not we yielded, ctx->pc is a clean
        // function-boundary resume point.
        (void)shouldPreemptGuestExecution();

        const uint32_t pc = ctx->pc;

        if (pc == lastPc)
        {
            ++samePcCount;
            if ((samePcCount % kSamePcYieldInterval) == 0u)
            {
                PS2_IF_AGRESSIVE_LOGS({
                    RUNTIME_LOG("CPU is doing some work at PC 0x" << std::hex << pc << ". PC not updating.");
                });
                std::this_thread::yield();
            }
        }
        else
        {
            samePcCount = 0;
            lastPc = pc;
        }

        m_debugPc.store(pc, std::memory_order_relaxed);
        m_debugRa.store(static_cast<uint32_t>(_mm_extract_epi32(ctx->r[31], 0)), std::memory_order_relaxed);
        m_debugSp.store(static_cast<uint32_t>(_mm_extract_epi32(ctx->r[29], 0)), std::memory_order_relaxed);
        m_debugGp.store(static_cast<uint32_t>(_mm_extract_epi32(ctx->r[28], 0)), std::memory_order_relaxed);

        RecompiledFunction fn = lookupFunction(pc);
        const uint32_t dispatchedPc = pc;
        const uint32_t dispatchedRa = static_cast<uint32_t>(_mm_extract_epi32(ctx->r[31], 0));
        fn(rdram, ctx, this);

        if (ctx->pc == 0u)
        {
            const uint32_t ra = static_cast<uint32_t>(_mm_extract_epi32(ctx->r[31], 0));
            const uint32_t sp = static_cast<uint32_t>(_mm_extract_epi32(ctx->r[29], 0));
            const uint32_t gp = static_cast<uint32_t>(_mm_extract_epi32(ctx->r[28], 0));
            PS2_IF_AGRESSIVE_LOGS({
                std::cerr << "[dispatch:pc-zero] from=0x" << std::hex << dispatchedPc
                          << " fromRa=0x" << dispatchedRa
                          << " ra=0x" << ra
                          << " sp=0x" << sp
                          << " gp=0x" << gp
                          << " trace=" << formatDispatchHistory()
                          << std::dec << std::endl;
            });

            // PC=0 means this guest thread returned (usually via jr $ra with RA=0).
            // Do not request a global runtime stop here: other guest threads may still run.
            break;
        }
    }
}

bool PS2Runtime::shouldPreemptGuestExecution()
{
    return ps2sched::yield_point();
}

uint8_t PS2Runtime::Load8(uint8_t *rdram, R5900Context *ctx, uint32_t vaddr)
{
    try
    {
        return m_memory.read8(vaddr);
    }
    catch (const std::exception &)
    {
        SignalException(ctx, EXCEPTION_ADDRESS_ERROR_LOAD);
        return 0;
    }
}

uint16_t PS2Runtime::Load16(uint8_t *rdram, R5900Context *ctx, uint32_t vaddr)
{
    try
    {
        return m_memory.read16(vaddr);
    }
    catch (const std::exception &)
    {
        SignalException(ctx, EXCEPTION_ADDRESS_ERROR_LOAD);
        return 0;
    }
}

uint32_t PS2Runtime::Load32(uint8_t *rdram, R5900Context *ctx, uint32_t vaddr)
{
    try
    {
        return m_memory.read32(vaddr);
    }
    catch (const std::exception &)
    {
        SignalException(ctx, EXCEPTION_ADDRESS_ERROR_LOAD);
        return 0;
    }
}

uint64_t PS2Runtime::Load64(uint8_t *rdram, R5900Context *ctx, uint32_t vaddr)
{
    try
    {
        return m_memory.read64(vaddr);
    }
    catch (const std::exception &)
    {
        SignalException(ctx, EXCEPTION_ADDRESS_ERROR_LOAD);
        return 0;
    }
}

__m128i PS2Runtime::Load128(uint8_t *rdram, R5900Context *ctx, uint32_t vaddr)
{
    try
    {
        return m_memory.read128(vaddr);
    }
    catch (const std::exception &)
    {
        SignalException(ctx, EXCEPTION_ADDRESS_ERROR_LOAD);
        return _mm_setzero_si128();
    }
}

void PS2Runtime::Store8(uint8_t *rdram, R5900Context *ctx, uint32_t vaddr, uint8_t value)
{
    ps2TraceGuestWrite(rdram, vaddr, 1u, value, 0u, "WRITE8", ctx);
    try
    {
        m_memory.write8(vaddr, value);
    }
    catch (const std::exception &)
    {
        SignalException(ctx, EXCEPTION_ADDRESS_ERROR_STORE);
    }
}

void PS2Runtime::Store16(uint8_t *rdram, R5900Context *ctx, uint32_t vaddr, uint16_t value)
{
    ps2TraceGuestWrite(rdram, vaddr, 2u, value, 0u, "WRITE16", ctx);
    try
    {
        m_memory.write16(vaddr, value);
    }
    catch (const std::exception &)
    {
        SignalException(ctx, EXCEPTION_ADDRESS_ERROR_STORE);
    }
}

void PS2Runtime::Store32(uint8_t *rdram, R5900Context *ctx, uint32_t vaddr, uint32_t value)
{
    ps2TraceGuestWrite(rdram, vaddr, 4u, value, 0u, "WRITE32", ctx);
    try
    {
        m_memory.write32(vaddr, value);
        drainCompletedDmacHandlers(rdram);
    }
    catch (const std::exception &)
    {
        SignalException(ctx, EXCEPTION_ADDRESS_ERROR_STORE);
    }
}

void PS2Runtime::Store64(uint8_t *rdram, R5900Context *ctx, uint32_t vaddr, uint64_t value)
{
    ps2TraceGuestWrite(rdram, vaddr, 8u, value, 0u, "WRITE64", ctx);
    try
    {
        m_memory.write64(vaddr, value);
    }
    catch (const std::exception &)
    {
        SignalException(ctx, EXCEPTION_ADDRESS_ERROR_STORE);
    }
}

void PS2Runtime::Store128(uint8_t *rdram, R5900Context *ctx, uint32_t vaddr, __m128i value)
{
    alignas(16) uint64_t _parts[2];
    _mm_storeu_si128(reinterpret_cast<__m128i *>(_parts), value);
    ps2TraceGuestWrite(rdram, vaddr, 16u, _parts[0], _parts[1], "WRITE128", ctx);
    try
    {
        m_memory.write128(vaddr, value);
    }
    catch (const std::exception &)
    {
        SignalException(ctx, EXCEPTION_ADDRESS_ERROR_STORE);
    }
}

void PS2Runtime::kickGifDmaChainFromMMIO(uint8_t *rdram,
                                         R5900Context *ctx,
                                         uint32_t dPcrValue,
                                         uint32_t dStatValue,
                                         uint32_t tadr,
                                         uint32_t chcr)
{
    constexpr uint32_t D_PCR = 0x1000E020u;
    constexpr uint32_t D_STAT = 0x1000E010u;
    constexpr uint32_t GIF_TADR = 0x1000A030u;
    constexpr uint32_t GIF_CHCR = 0x1000A000u;

    ps2TraceGuestWrite(rdram, D_PCR, 4u, dPcrValue, 0u, "WRITE32", ctx);
    m_memory.writeIORegister(D_PCR, dPcrValue);
    ps2TraceGuestWrite(rdram, D_STAT, 4u, dStatValue, 0u, "WRITE32", ctx);
    m_memory.writeIORegister(D_STAT, dStatValue);
    ps2TraceGuestWrite(rdram, GIF_TADR, 4u, tadr, 0u, "WRITE32", ctx);
    m_memory.writeIORegister(GIF_TADR, tadr);
    ps2TraceGuestWrite(rdram, GIF_CHCR, 4u, chcr, 0u, "WRITE32", ctx);
    if (m_memory.tryProcessNativeGifImageUploadChain(m_gs, tadr, chcr))
    {
        drainCompletedDmacHandlers(rdram);
        return;
    }
    if (m_memory.tryProcessNativeGifPackedChain(m_gs, tadr, chcr))
    {
        drainCompletedDmacHandlers(rdram);
        return;
    }
    m_memory.writeIORegister(GIF_CHCR, chcr);
    m_memory.processPendingTransfers();
    drainCompletedDmacHandlers(rdram);
}

void PS2Runtime::requestStop()
{
    m_stopRequested.store(true, std::memory_order_relaxed);
    ps2_syscalls::notifyRuntimeStop();
}

void PS2Runtime::requestStopFlagOnly()
{
    m_stopRequested.store(true, std::memory_order_relaxed);
}

bool PS2Runtime::isStopRequested() const
{
    return m_stopRequested.load(std::memory_order_relaxed);
}

void PS2Runtime::HandleIntegerOverflow(R5900Context *ctx)
{
    raiseCop0Exception(ctx, EXCEPTION_INTEGER_OVERFLOW);
}

void PS2Runtime::run()
{
    m_stopRequested.store(false, std::memory_order_relaxed);
    ps2_stubs::resetSifState();
    resetIop();
    ps2_stubs::resetAudioStubState();
    ps2_stubs::resetGsSyncVCallbackState();
    ps2_stubs::resetMpegStubState();
    ps2_syscalls::initializeGuestKernelState(m_memory.getRDRAM());
    m_cpuContext.r[4] = _mm_setzero_si128();
    m_cpuContext.r[5] = _mm_setzero_si128();
    // Bootstrap $sp at top of RAM, as the hardware loader does; the guest's
    // crt0 immediately replaces it via SetupThread, which points $sp at the
    // top of the game-chosen main stack. Callback stacks live in the
    // kernel-reserved pool (see kAsyncCallbackStackFloor), not here, so this
    // cannot collide with them.
    m_cpuContext.r[29] = _mm_set_epi64x(0, static_cast<int64_t>(PS2_RAM_SIZE - 0x10u));

    RUNTIME_LOG("Starting execution at address 0x" << std::hex << m_cpuContext.pc << std::dec);

    // A blank image to use as a framebuffer
    Image blank = GenImageColor(FB_WIDTH, FB_HEIGHT, BLANK);
    Texture2D frameTex = LoadTextureFromImage(blank);
    UnloadImage(blank);

    // Initialize the fiber/pool scheduler.
    ps2sched::scheduler_init();
    ps2sched::scheduler_set_stop_callback(+[](void* p) { static_cast<PS2Runtime*>(p)->requestStopFlagOnly(); }, this);

    // Create the main guest fiber (tid=1).
    uint8_t *rdram = m_memory.getRDRAM();
    {
        const uint32_t entry = m_cpuContext.pc;
        const uint32_t sp    = static_cast<uint32_t>(_mm_extract_epi32(m_cpuContext.r[29], 0));
        const uint32_t gp    = static_cast<uint32_t>(_mm_extract_epi32(m_cpuContext.r[28], 0));
        ps2sched::create_fiber(1, 1, entry, sp, gp, 0u, this, rdram);
    }

    ps2_syscalls::EnsureVSyncWorkerRunning(m_memory.getRDRAM(), this);

    uint64_t tick = 0;
    while (!isStopRequested())
    {
        PS2_IF_AGRESSIVE_LOGS({
            tick++;
            if ((tick % 120) == 0)
            {
                uint64_t curDma = m_memory.dmaStartCount();
                uint64_t curGif = m_memory.gifCopyCount();
                uint64_t curGs = m_memory.gsWriteCount();
                uint64_t curVif = m_memory.vifWriteCount();
                const GSRegisters &gs = m_memory.gs();
                const uint32_t dbgPc = m_debugPc.load(std::memory_order_relaxed);
                const uint32_t dbgRa = m_debugRa.load(std::memory_order_relaxed);
                const uint32_t dbgSp = m_debugSp.load(std::memory_order_relaxed);
                const uint32_t dbgGp = m_debugGp.load(std::memory_order_relaxed);
                const int activeThreads = g_activeThreads.load(std::memory_order_relaxed);

                RUNTIME_LOG("[run:tick] tick=" << tick
                                               << " pc=0x" << std::hex << dbgPc
                                               << " ra=0x" << dbgRa
                                               << " sp=0x" << dbgSp
                                               << " gp=0x" << dbgGp
                                               << " dispfb1=0x" << gs.dispfb1
                                               << " display1=0x" << gs.display1
                                               << std::dec
                                               << " activeThreads=" << activeThreads
                                               << " dma=" << curDma
                                               << " gif=" << curGif
                                               << " gsw=" << curGs
                                               << " vif=" << curVif
                                               << std::endl);
            }
        });
        uint32_t presentWidth = FB_WIDTH;
        uint32_t presentHeight = DEFAULT_DISPLAY_HEIGHT;
        UploadFrame(frameTex, this, presentWidth, presentHeight);

        BeginDrawing();
        ClearBackground(BLACK);
        const float srcWidth = static_cast<float>(std::max<uint32_t>(1u, presentWidth));
        const float srcHeight = static_cast<float>(std::max<uint32_t>(1u, presentHeight));
        const float screenWidth = static_cast<float>(GetScreenWidth());
        const float screenHeight = static_cast<float>(GetScreenHeight());
        const float scale = std::min(screenWidth / srcWidth, screenHeight / srcHeight);
        const float dstWidth = srcWidth * scale;
        const float dstHeight = srcHeight * scale;
        const Rectangle srcRect{0.0f, 0.0f, srcWidth, srcHeight};
        const Rectangle dstRect{
            (screenWidth - dstWidth) * 0.5f,
            (screenHeight - dstHeight) * 0.5f,
            dstWidth,
            dstHeight};
        DrawTexturePro(frameTex, srcRect, dstRect, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
        if (m_debugUiInitialized && m_debugUiDrawCallback)
        {
            m_debugUiDrawCallback(*this, m_debugUiUserData);
        }
        EndDrawing();

        if (WindowShouldClose())
        {
            RUNTIME_LOG("[run] window close requested, breaking out of loop");
            requestStop();
            break;
        }
    }

    requestStop();

    // Signal all guest fibers to stop and join the pool threads.
    ps2sched::scheduler_shutdown();
    ps2sched::scheduler_set_stop_callback(nullptr, nullptr);

    if (m_debugUiInitialized && m_debugUiShutdownCallback)
    {
        m_debugUiShutdownCallback(*this, m_debugUiUserData);
        m_debugUiInitialized = false;
    }
    UnloadTexture(frameTex);
    CloseWindow();
}
