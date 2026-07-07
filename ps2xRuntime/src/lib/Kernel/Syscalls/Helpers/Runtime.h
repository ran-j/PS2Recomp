#include "ThreadExit.h"
#include "ps2_scheduler.h"
#include <chrono>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

// Swaps a wait-list out from under its mutex and delivers a validated external
// wakeup to every waiter that was on it. This is ONLY the pure swap-and-drain
// shape: callers whose critical section also mutates other state alongside
// the swap (e.g. marking the object deleted) must keep doing that inline
// instead of calling this, so the lock is still held across both writes.
static inline void wakeWaiters(std::mutex &m, std::vector<std::pair<int, uint64_t>> &list)
{
    std::vector<std::pair<int, uint64_t>> waiters;
    {
        std::lock_guard<std::mutex> lk(m);
        waiters.swap(list);
    }
    for (const auto &[tid, token] : waiters)
    {
        ps2sched::enqueue_external_wakeup_validated(tid, token);
    }
}

// Bounded backoff for a borrowed host worker that hit a blocking syscall.
// Translates a non-fiber BlockResult into the correct token handling, then
// sleeps with exponential backoff (1us -> 1ms cap). After kMaxSpins iterations
// it logs ONCE and keeps sleeping at the cap so a self-deadlocked interrupt
// handler cannot busy-spin the CPU or starve the guest executor. State is held
// in a per-call counter object so each blocking site ramps independently.
struct NonFiberBackoff
{
    int spins = 0;
    std::chrono::microseconds delay{1};

    // Sleeps this worker once with exponential backoff. The syscall's Mesa loop
    // decides whether to re-check its wait condition and loop again.
    void step(ps2sched::BlockResult br)
    {
        // Drop/reacquire the guest token only if we actually own it.
        if (br == ps2sched::BlockResult::NonFiberOwner)
        {
            ps2sched::async_guest_end();
            std::this_thread::sleep_for(delay);
            ps2sched::async_guest_begin();
        }
        else // NonFiberNoTok (or, defensively, any non-Parked non-fiber result)
        {
            std::this_thread::sleep_for(delay);
        }

        constexpr int kMaxSpins = 50;
        if (spins == kMaxSpins)
        {
            std::fprintf(stderr,
                "[ps2sched] WARNING: borrowed host worker has blocked on a guest "
                "condition for %d retries; capping backoff at 1ms (possible "
                "interrupt-context deadlock)\n", kMaxSpins);
        }
        if (spins < kMaxSpins)
        {
            ++spins;
            delay *= 2;
            if (delay > std::chrono::microseconds(1000))
                delay = std::chrono::microseconds(1000);
        }
    }
};

// One-shot: drop/reacquire token (if owned) and yield once. SleepThread for
// a borrowed worker has no wait-list to re-check, so it does not loop.
inline void nonFiberBlockBackoff(ps2sched::BlockResult br)
{
    if (br == ps2sched::BlockResult::NonFiberOwner)
    {
        ps2sched::async_guest_end();
        std::this_thread::yield();
        ps2sched::async_guest_begin();
    }
    else
    {
        std::this_thread::yield();
    }
}

static void throwIfTerminated(const std::shared_ptr<ThreadInfo> &info)
{
    if (info && info->terminated.load())
    {
        throw ThreadExitException();
    }
}

// Waiting is done via arm_park()/block_current(), which already
// cooperatively yields to other fibers/borrowed host workers, so no explicit
// guest-execution release/reacquire step is needed.
static void waitWhileSuspended(const std::shared_ptr<ThreadInfo> &info)
{
    if (!info) return;
    while (info->suspendCount > 0 && !info->terminated.load()) {
        // arm_park() before publishing to any wait-list so a concurrent
        // clear_suspend that fires between publish and block_current sees
        // wake_pending rather than missing the wakeup.
        ps2sched::arm_park();
        ps2sched::block_current();
    }
    if (info->terminated.load()) { throw ThreadExitException(); }
    if (info->suspendCount == 0) {
        std::lock_guard<std::mutex> lock(info->m);
        info->status = THS_RUN;
    }
}

static std::shared_ptr<ThreadInfo> lookupThreadInfo(int tid)
{
    std::lock_guard<std::mutex> lock(g_thread_map_mutex);
    auto it = g_threads.find(tid);
    if (it != g_threads.end())
    {
        return it->second;
    }
    return nullptr;
}

static std::shared_ptr<ThreadInfo> ensureCurrentThreadInfo(R5900Context *ctx)
{
    const int tid = g_currentThreadId;
    std::lock_guard<std::mutex> lock(g_thread_map_mutex);
    auto it = g_threads.find(tid);
    if (it != g_threads.end())
    {
        return it->second;
    }

    auto info = std::make_shared<ThreadInfo>();
    info->started = true;
    info->status = THS_RUN;
    info->currentPriority = info->priority;
    info->suspendCount = 0;
    if (ctx)
    {
        info->entry = ctx->pc;
        info->stack = getRegU32(ctx, 29);
        info->gp = getRegU32(ctx, 28);
    }
    info->waitType = TSW_NONE;
    info->waitId = 0;

    g_threads.emplace(tid, info);
    return info;
}

// Resolves tid==0 (TH_SELF) to g_currentThreadId, looks up the corresponding
// ThreadInfo, and writes the appropriate error return on failure. Callers
// pass tid by reference so the resolved (non-zero) id is visible afterward.
// Shared prologue for syscalls that operate on "self or an explicit tid":
// TerminateThread, SuspendThread, ResumeThread, ReferThreadStatus,
// CancelWakeupThread, ChangeThreadPriority. Do NOT use for syscalls with
// different tid==0 semantics (WakeupThread, ReleaseWaitThread, the
// i-prefixed variants, RotateThreadReadyQueue).
static std::shared_ptr<ThreadInfo> resolveSelfOrThread(R5900Context *ctx, int &tid)
{
    if (tid == 0)
    {
        if (g_currentThreadId == -1)
        {
            setReturnS32(ctx, KE_ILLEGAL_THID);
            return nullptr;
        }
        tid = g_currentThreadId;
    }
    auto info = (tid == g_currentThreadId) ? ensureCurrentThreadInfo(ctx) : lookupThreadInfo(tid);
    if (!info)
        setReturnS32(ctx, KE_UNKNOWN_THID);
    return info;
}

// Marks a thread as exiting itself (caller holds info.m). Shared by
// ExitThread and ExitDeleteThread only. TerminateThread must NOT route
// through this: it intentionally sets only terminated/forceRelease and
// leaves waitType/waitId/wakeupCount observable via ReferThreadStatus until
// the target's own wait loop clears them.
static void markSelfExitingLocked(ThreadInfo &info)
{
    info.terminated = true;
    info.forceRelease = true;
    info.waitType = TSW_NONE;
    info.waitId = 0;
    info.wakeupCount = 0;
}

static std::shared_ptr<SemaInfo> lookupSemaInfo(int sid)
{
    std::lock_guard<std::mutex> lock(g_sema_map_mutex);
    auto it = g_semas.find(sid);
    if (it != g_semas.end())
    {
        return it->second;
    }
    return nullptr;
}

static std::shared_ptr<EventFlagInfo> lookupEventFlagInfo(int eid)
{
    std::lock_guard<std::mutex> lock(g_event_flag_map_mutex);
    auto it = g_eventFlags.find(eid);
    if (it != g_eventFlags.end())
    {
        return it->second;
    }
    return nullptr;
}

static void setRegU32(R5900Context *ctx, int reg, uint32_t value)
{
    if (!ctx || reg < 0 || reg > 31)
        return;
    SET_GPR_U32(ctx, reg, value);
}

static std::chrono::microseconds alarmTicksToDuration(uint16_t ticks)
{
    constexpr uint64_t kAlarmTickUsec = 64u; // Approximate EE H-SYNC tick period.
    const uint64_t clampedTicks = (ticks == 0u) ? 1u : static_cast<uint64_t>(ticks);
    return std::chrono::microseconds(clampedTicks * kAlarmTickUsec);
}


static void rpcCopyToRdram(uint8_t *rdram, uint32_t dst, uint32_t src, size_t size)
{
    if (!rdram || size == 0)
        return;

    constexpr size_t kMaxRpcTransferBytes = 1u * 1024u * 1024u;
    const size_t clampedSize = std::min(size, kMaxRpcTransferBytes);
    if (clampedSize != size)
    {
        static uint32_t warnCount = 0;
        if (warnCount < 8)
        {
            std::cerr << "[SifCallRpc] clamping copy size from " << size
                      << " to " << clampedSize
                      << " bytes (dst=0x" << std::hex << dst
                      << " src=0x" << src << std::dec << ")" << std::endl;
            ++warnCount;
        }
    }

    for (size_t i = 0; i < clampedSize; ++i)
    {
        const uint32_t dstAddr = dst + static_cast<uint32_t>(i);
        const uint32_t srcAddr = src + static_cast<uint32_t>(i);
        uint8_t *dstPtr = getMemPtr(rdram, dstAddr);
        const uint8_t *srcPtr = getConstMemPtr(rdram, srcAddr);
        if (!dstPtr || !srcPtr)
        {
            break;
        }
        *dstPtr = *srcPtr;
    }
}

static void rpcZeroRdram(uint8_t *rdram, uint32_t dst, size_t size)
{
    if (!rdram || size == 0)
        return;

    constexpr size_t kMaxRpcTransferBytes = 1u * 1024u * 1024u;
    const size_t clampedSize = std::min(size, kMaxRpcTransferBytes);
    if (clampedSize != size)
    {
        static uint32_t warnCount = 0;
        if (warnCount < 8)
        {
            std::cerr << "[SifCallRpc] clamping zero size from " << size
                      << " to " << clampedSize
                      << " bytes (dst=0x" << std::hex << dst << std::dec << ")" << std::endl;
            ++warnCount;
        }
    }

    for (size_t i = 0; i < clampedSize; ++i)
    {
        const uint32_t dstAddr = dst + static_cast<uint32_t>(i);
        uint8_t *dstPtr = getMemPtr(rdram, dstAddr);
        if (!dstPtr)
        {
            break;
        }
        *dstPtr = 0;
    }
}

static bool readStackU32(uint8_t *rdram, uint32_t sp, uint32_t offset, uint32_t &out)
{
    uint8_t *ptr = getMemPtr(rdram, sp + offset);
    if (!ptr)
        return false;
    out = *reinterpret_cast<uint32_t *>(ptr);
    return true;
}

enum class RpcInvokeExitReason
{
    Returned,
    NullPc,
    MissingFunction,
    StepLimit,
    SamePcLimit
};

static const char *rpcInvokeExitReasonName(RpcInvokeExitReason reason)
{
    switch (reason)
    {
    case RpcInvokeExitReason::Returned:
        return "returned";
    case RpcInvokeExitReason::NullPc:
        return "null-pc";
    case RpcInvokeExitReason::MissingFunction:
        return "missing-function";
    case RpcInvokeExitReason::StepLimit:
        return "step-limit";
    case RpcInvokeExitReason::SamePcLimit:
        return "same-pc-limit";
    default:
        return "unknown";
    }
}

// rpcInvokeFunction runs on the CALLING FIBER (the guest thread that issued
// the RPC syscall), not on a worker thread. Do NOT wrap calls to this in
// AsyncGuestScope — the calling fiber already holds the guest execution slot.
// If, in the future, RPC server dispatch is moved to a dedicated worker thread,
// that worker must wrap its guest invocation in AsyncGuestScope.
static bool rpcInvokeFunction(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime,
                              uint32_t funcAddr, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t *outV0)
{
    if (!runtime || !ctx || !funcAddr || !runtime->hasFunction(funcAddr))
        return false;

    constexpr uint32_t kRpcInvokeStackSize = 0x4000u;
    constexpr uint32_t kRpcInvokeReturnSentinel = 0x00FFF000u;
    constexpr uint32_t kRpcInvokeMaxSteps = 0x8000u;

    R5900Context tmp = *ctx;
    setRegU32(&tmp, 4, a0);
    setRegU32(&tmp, 5, a1);
    setRegU32(&tmp, 6, a2);
    setRegU32(&tmp, 7, a3);

    // Per-invocation scratch stack: a fresh guest-heap region reserved for the
    // duration of THIS invoke and released by RAII on every return path below
    // (and on a ThreadExitException thrown out of the invoke loop). Isolates
    // interleaving fibers — the N=1 executor runs them all on one OS thread —
    // AND same-fiber re-entry: a nested override or an exit-handler invoke gets
    // its own stack and never clobbers the outer frame.
    GuestScratchStack invokeStack(runtime, kRpcInvokeStackSize);
    if (invokeStack.valid())
    {
        setRegU32(&tmp, 29, invokeStack.top());
    }

    setRegU32(&tmp, 31, kRpcInvokeReturnSentinel);
    tmp.pc = funcAddr;

    uint32_t steps = 0u;
    uint32_t lastPc = 0xFFFFFFFFu;
    uint32_t samePcCount = 0u;
    RpcInvokeExitReason exitReason = RpcInvokeExitReason::MissingFunction;
    while (tmp.pc != 0u &&
           tmp.pc != kRpcInvokeReturnSentinel &&
           runtime->hasFunction(tmp.pc) &&
           steps < kRpcInvokeMaxSteps)
    {
        const uint32_t pc = tmp.pc;
        if (pc == lastPc)
        {
            ++samePcCount;
            if (samePcCount > 0x2000u)
            {
                exitReason = RpcInvokeExitReason::SamePcLimit;
                break;
            }
        }
        else
        {
            lastPc = pc;
            samePcCount = 0u;
        }

        PS2Runtime::RecompiledFunction func = runtime->lookupFunction(pc);
        func(rdram, &tmp, runtime);
        ++steps;
    }

    if (outV0)
    {
        *outV0 = getRegU32(&tmp, 2);
    }

    if (tmp.pc == kRpcInvokeReturnSentinel)
    {
        return true;
    }

    if (tmp.pc == 0u)
    {
        exitReason = RpcInvokeExitReason::NullPc;
    }
    else if (steps >= kRpcInvokeMaxSteps)
    {
        exitReason = RpcInvokeExitReason::StepLimit;
    }
    else if (!runtime->hasFunction(tmp.pc))
    {
        exitReason = RpcInvokeExitReason::MissingFunction;
    }

    static std::atomic<uint32_t> s_rpcInvokeFailureLogs{0u};
    constexpr uint32_t kMaxRpcInvokeFailureLogs = 64u;
    const uint32_t logIndex = s_rpcInvokeFailureLogs.fetch_add(1u, std::memory_order_relaxed);
    if (logIndex < kMaxRpcInvokeFailureLogs)
    {
        PS2_IF_AGRESSIVE_LOGS({
            std::cerr << "[SyscallOverride:invoke-failed]"
                      << " func=0x" << std::hex << funcAddr
                      << " exitPc=0x" << tmp.pc
                      << " ra=0x" << getRegU32(&tmp, 31)
                      << std::dec
                      << " steps=" << steps
                      << " reason=" << rpcInvokeExitReasonName(exitReason)
                      << std::endl;
        });
    }

    return false;
}

static uint32_t rpcAllocPacketAddr(uint8_t *rdram)
{
    if (kRpcPacketPoolCount == 0)
        return 0;

    uint32_t slot = g_rpc_packet_index++ % kRpcPacketPoolCount;
    uint32_t addr = kRpcPacketPoolBase + (slot * kRpcPacketSize);
    rpcZeroRdram(rdram, addr, kRpcPacketSize);
    return addr;
}

static uint32_t rpcAllocServerAddr(uint8_t *rdram)
{
    if (kRpcServerPoolCount == 0)
        return 0;

    uint32_t slot = g_rpc_server_index++ % kRpcServerPoolCount;
    uint32_t addr = kRpcServerPoolBase + (slot * kRpcServerStride);
    rpcZeroRdram(rdram, addr, kRpcServerStride);
    return addr;
}

struct IrqHandlerInfo
{
    int id = 0;
    uint32_t cause = 0;
    uint32_t handler = 0;
    uint32_t arg = 0;
    uint32_t gp = 0;
    uint32_t sp = 0;
    bool enabled = true;
    int order = 0;
};

static std::unordered_map<int, IrqHandlerInfo> g_intcHandlers;
static std::unordered_map<int, IrqHandlerInfo> g_dmacHandlers;
static int g_nextIntcHandlerId = 1;
static int g_nextDmacHandlerId = 1;

static int g_intc_head_order = 0;
static int g_intc_tail_order = 1000;
static int g_dmac_head_order = 0;
static int g_dmac_tail_order = 1000;

inline std::string translatePs2Path(const char *ps2Path)
{
    if (!ps2Path || !*ps2Path)
    {
        return {};
    }

    std::string pathStr(ps2Path);
    std::string lower = toLowerAscii(pathStr);

    auto resolveWithBase = [&](const std::filesystem::path &base, const std::string &suffix) -> std::string
    {
        const std::string normalizedSuffix = normalizePs2PathSuffix(suffix);
        std::filesystem::path resolved = base;
        if (!normalizedSuffix.empty())
        {
            resolved /= std::filesystem::path(normalizedSuffix);
        }
        return resolved.lexically_normal().string();
    };

    if (lower.rfind("host0:", 0) == 0 || lower.rfind("host:", 0) == 0)
    {
        const std::size_t prefixLength = (lower.rfind("host0:", 0) == 0) ? 6 : 5;
        return resolveWithBase(getConfiguredHostRoot(), pathStr.substr(prefixLength));
    }

    if (lower.rfind("cdrom0:", 0) == 0 || lower.rfind("cdrom:", 0) == 0)
    {
        const std::size_t prefixLength = (lower.rfind("cdrom0:", 0) == 0) ? 7 : 6;
        return resolveWithBase(getConfiguredCdRoot(), pathStr.substr(prefixLength));
    }

    if (lower.rfind(kMc0Prefix, 0) == 0)
    {
        const std::size_t prefixLength = sizeof(kMc0Prefix) - 1;
        return resolveWithBase(getConfiguredMcRoot(), pathStr.substr(prefixLength));
    }

    if (!pathStr.empty() && (pathStr.front() == '/' || pathStr.front() == '\\'))
    {
        return resolveWithBase(getConfiguredCdRoot(), pathStr);
    }

    if (pathStr.size() > 1 && pathStr[1] == ':')
    {
        return pathStr;
    }

    return resolveWithBase(getConfiguredCdRoot(), pathStr);
}

static bool localtimeSafe(const std::time_t *t, std::tm *out)
{
#ifdef _WIN32
    return localtime_s(out, t) == 0;
#else
    return localtime_r(t, out) != nullptr;
#endif
}

static void encodePs2Time(std::time_t t, uint8_t out[8])
{
    std::tm tm{};
    if (!localtimeSafe(&t, &tm))
    {
        std::memset(out, 0, 8);
        return;
    }

    uint16_t year = static_cast<uint16_t>(tm.tm_year + 1900);
    out[0] = 0;
    out[1] = static_cast<uint8_t>(tm.tm_sec);
    out[2] = static_cast<uint8_t>(tm.tm_min);
    out[3] = static_cast<uint8_t>(tm.tm_hour);
    out[4] = static_cast<uint8_t>(tm.tm_mday);
    out[5] = static_cast<uint8_t>(tm.tm_mon + 1);
    out[6] = static_cast<uint8_t>(year & 0xFF);
    out[7] = static_cast<uint8_t>((year >> 8) & 0xFF);
}

static std::time_t fileTimeToTimeT(std::filesystem::file_time_type ft)
{
    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ft - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
    return std::chrono::system_clock::to_time_t(sctp);
}

static bool gmtimeSafe(const std::time_t *t, std::tm *out)
{
#ifdef _WIN32
    return gmtime_s(out, t) == 0;
#else
    return gmtime_r(t, out) != nullptr;
#endif
}

static int getTimezoneOffsetMinutes()
{
    std::time_t now = std::time(nullptr);
    std::tm local{};
    std::tm gmt{};
    if (!localtimeSafe(&now, &local) || !gmtimeSafe(&now, &gmt))
        return 0;

    std::time_t localTime = std::mktime(&local);
    std::time_t gmtTime = std::mktime(&gmt);
    if (localTime == static_cast<std::time_t>(-1) || gmtTime == static_cast<std::time_t>(-1))
        return 0;

    double diff = std::difftime(localTime, gmtTime);
    return static_cast<int>(diff / 60.0);
}

static uint32_t packOsdConfig(uint32_t spdifMode, uint32_t screenType, uint32_t videoOutput,
                              uint32_t japLanguage, uint32_t ps1drvConfig, uint32_t version,
                              uint32_t language, int timezoneOffset)
{
    uint32_t raw = 0;
    raw |= (spdifMode & 0x1) << 0;
    raw |= (screenType & 0x3) << 1;
    raw |= (videoOutput & 0x1) << 3;
    raw |= (japLanguage & 0x1) << 4;
    raw |= (ps1drvConfig & 0xFF) << 5;
    raw |= (version & 0x7) << 13;
    raw |= (language & 0x1F) << 16;
    raw |= (static_cast<uint32_t>(timezoneOffset) & 0x7FF) << 21;
    return raw;
}

static uint32_t packOsdConfig2(uint32_t format, uint32_t daylightSaving, uint32_t timeFormat,
                               uint32_t dateFormat, uint32_t version, uint32_t language)
{
    const uint32_t flags =
        ((daylightSaving & 0x1u) << 4) |
        ((timeFormat & 0x1u) << 5) |
        ((dateFormat & 0x3u) << 6);

    return (format & 0xFFu) |
           ((flags & 0xFFu) << 8) |
           ((version & 0xFFu) << 16) |
           ((language & 0xFFu) << 24);
}

static int decodeTimezoneOffset(uint32_t raw)
{
    int tz = static_cast<int>((raw >> 21) & 0x7FF);
    if (tz & 0x400)
        tz |= ~0x7FF;
    return tz;
}

static int clampTimezoneOffset(int tz)
{
    if (tz < -1024)
        return -1024;
    if (tz > 1023)
        return 1023;
    return tz;
}

static uint32_t sanitizeOsdConfigRaw(uint32_t raw)
{
    uint32_t spdifMode = raw & 0x1;
    uint32_t screenType = (raw >> 1) & 0x3;
    if (screenType > 2)
        screenType = 0;
    uint32_t videoOutput = (raw >> 3) & 0x1;
    uint32_t japLanguage = (raw >> 4) & 0x1;
    uint32_t ps1drvConfig = (raw >> 5) & 0xFF;
    uint32_t version = (raw >> 13) & 0x7;
    if (version > 2)
        version = 1;
    uint32_t language = (raw >> 16) & 0x1F;
    int tz = clampTimezoneOffset(decodeTimezoneOffset(raw));
    return packOsdConfig(spdifMode, screenType, videoOutput, japLanguage, ps1drvConfig, version, language, tz);
}

static uint32_t sanitizeOsdConfig2Raw(uint32_t raw)
{
    const uint32_t format = raw & 0xFFu;
    const uint32_t flags = (raw >> 8) & 0xFFu;
    const uint32_t daylightSaving = (flags >> 4) & 0x1u;
    const uint32_t timeFormat = (flags >> 5) & 0x1u;
    uint32_t dateFormat = (flags >> 6) & 0x3u;
    if (dateFormat > 2u)
        dateFormat = 0u;

    uint32_t version = (raw >> 16) & 0xFFu;
    if (version > 2u)
        version = 1u;

    const uint32_t language = (raw >> 24) & 0xFFu;
    return packOsdConfig2(format, daylightSaving, timeFormat, dateFormat, version, language);
}

static uint32_t syncOsdConfigRawVersionLanguage(uint32_t raw, uint32_t version, uint32_t language)
{
    raw &= ~((0x7u << 13) | (0x1Fu << 16));
    raw |= (version & 0x7u) << 13;
    raw |= (language & 0x1Fu) << 16;
    return sanitizeOsdConfigRaw(raw);
}

static uint32_t makeReadableOsdConfig2RawLocked()
{
    const uint32_t version = (g_osd_config_raw >> 13) & 0x7u;
    const uint32_t language = (g_osd_config_raw >> 16) & 0x1Fu;
    uint32_t raw = g_osd_config2_raw & 0x0000FFFFu;
    raw |= (version & 0xFFu) << 16;
    raw |= (language & 0xFFu) << 24;
    return sanitizeOsdConfig2Raw(raw);
}

static void ensureOsdConfigInitialized()
{
    std::lock_guard<std::mutex> lock(g_osd_mutex);
    if (g_osd_config_initialized)
        return;

    int tz = clampTimezoneOffset(getTimezoneOffsetMinutes());
    uint32_t spdifMode = 1;   // disabled
    uint32_t screenType = 0;  // 4:3
    uint32_t videoOutput = 0; // RGB
    uint32_t japLanguage = 1; // non-japanese
    uint32_t ps1drvConfig = 0;
    uint32_t version = 1;  // OSD2
    uint32_t language = 1; // English
    g_osd_config_raw = packOsdConfig(spdifMode, screenType, videoOutput, japLanguage, ps1drvConfig, version, language, tz);
    g_osd_config2_raw = packOsdConfig2(0, 0, 0, 0, version, language);
    g_osd_config_initialized = true;
}

static uint32_t allocTlsAddr(uint8_t *rdram)
{
    if (!rdram || kTlsPoolCount == 0)
        return 0;

    std::lock_guard<std::mutex> lock(g_tls_mutex);
    uint32_t slot = g_tls_index++ % kTlsPoolCount;
    uint32_t addr = kTlsPoolBase + (slot * kTlsBlockSize);
    rpcZeroRdram(rdram, addr, kTlsBlockSize);
    return addr;
}

static uint32_t allocBootModeAddr(uint8_t *rdram, size_t bytes)
{
    if (!rdram)
        return 0;

    size_t aligned = (bytes + 15u) & ~15u;
    if (g_bootmode_pool_offset + aligned > kBootModePoolBytes)
        return 0;

    uint32_t addr = kBootModePoolBase + g_bootmode_pool_offset;
    g_bootmode_pool_offset += static_cast<uint32_t>(aligned);
    rpcZeroRdram(rdram, addr, aligned);
    return addr;
}

static uint32_t createBootModeEntry(uint8_t *rdram, uint8_t id, uint16_t value, uint8_t lenField, const uint32_t *data, uint8_t dataCount)
{
    uint8_t allocCount = (dataCount == 0) ? 1 : dataCount;
    size_t bytes = static_cast<size_t>(1 + allocCount) * sizeof(uint32_t);
    uint32_t addr = allocBootModeAddr(rdram, bytes);
    if (!addr)
        return 0;

    uint32_t header = (static_cast<uint32_t>(lenField) << 24) |
                      (static_cast<uint32_t>(id) << 16) |
                      (static_cast<uint32_t>(value) & 0xFFFFu);

    uint32_t *dst = reinterpret_cast<uint32_t *>(getMemPtr(rdram, addr));
    if (!dst)
        return 0;

    dst[0] = header;
    for (uint8_t i = 0; i < allocCount; ++i)
    {
        dst[1 + i] = (data && i < dataCount) ? data[i] : 0;
    }

    return addr;
}

static void ensureBootModeTable(uint8_t *rdram)
{
    std::lock_guard<std::mutex> lock(g_bootmode_mutex);
    if (g_bootmode_initialized)
        return;

    g_bootmode_pool_offset = 0;
    g_bootmode_addresses.clear();

    const uint32_t boot3Data[1] = {0};
    const uint32_t boot5Data[1] = {0};

    g_bootmode_addresses[1] = createBootModeEntry(rdram, 1, 0, 0, nullptr, 0);
    g_bootmode_addresses[3] = createBootModeEntry(rdram, 3, 0, 1, boot3Data, 1);
    g_bootmode_addresses[4] = createBootModeEntry(rdram, 4, 0, 0, nullptr, 0);
    g_bootmode_addresses[5] = createBootModeEntry(rdram, 5, 0, 1, boot5Data, 1);
    g_bootmode_addresses[6] = createBootModeEntry(rdram, 6, 0, 0, nullptr, 0);
    g_bootmode_addresses[7] = createBootModeEntry(rdram, 7, 0, 0, nullptr, 0);

    g_bootmode_initialized = true;
}
