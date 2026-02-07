#include "ps2_syscalls.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "ps2_stubs.h"
#include <iostream>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <vector>
#include <unordered_map>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <filesystem>
#include <limits>

#ifndef _WIN32
#include <unistd.h> // for unlink,rmdir,chdir
#include <sys/stat.h> // for mkdir
#endif


std::unordered_map<int, FILE *> g_fileDescriptors;
int g_nextFd = 3; // Start after stdin, stdout, stderr
std::mutex g_sys_fd_mutex;
static std::mutex g_fs_state_mutex;
static std::mutex g_thread_state_mutex;
static std::mutex g_sync_state_mutex;
static std::filesystem::path g_virtualHostCwd;

struct ThreadInfo
{
    uint32_t entry = 0;
    uint32_t stack = 0;
    uint32_t stackSize = 0;
    uint32_t gp = 0;
    uint32_t priority = 0;
    uint32_t attr = 0;
    uint32_t option = 0;
    uint32_t arg = 0;
    bool started = false;
};

struct SemaInfo
{
    uint32_t attr = 0;
    uint32_t option = 0;
    int initCount = 0;
    int count = 0;
    int maxCount = 0;
    int waiters = 0;
    bool deleted = false;
    std::mutex m;
    std::condition_variable cv;
};

struct EventFlagInfo
{
    uint32_t attr = 0;
    uint32_t option = 0;
    uint32_t initPattern = 0;
    uint32_t pattern = 0;
    int waiters = 0;
    bool deleted = false;
    std::mutex m;
    std::condition_variable cv;
};

static std::unordered_map<int, ThreadInfo> g_threads;
static int g_nextThreadId = 2; // Reserve 1 for the main thread
static thread_local int g_currentThreadId = 1;

static std::unordered_map<int, std::shared_ptr<SemaInfo>> g_semas;
static int g_nextSemaId = 1;
static std::unordered_map<int, std::shared_ptr<EventFlagInfo>> g_eventFlags;
static int g_nextEventFlagId = 1;
static uint32_t g_osdConfigParam = 0;
std::atomic<int> g_activeThreads{0};

static constexpr uint32_t EVENT_WAIT_OR = 0x01;
static constexpr uint32_t EVENT_WAIT_CLEAR = 0x10;
static constexpr uint32_t EVENT_WAIT_CLEAR_ALL = 0x20;

static bool eventConditionMet(uint32_t current, uint32_t mask, uint32_t mode)
{
    if (mask == 0)
    {
        return true;
    }

    if (mode & EVENT_WAIT_OR)
    {
        return (current & mask) != 0;
    }

    return (current & mask) == mask;
}

static void applyEventWaitMode(EventFlagInfo &info, uint32_t mask, uint32_t mode)
{
    if (mode & EVENT_WAIT_CLEAR_ALL)
    {
        info.pattern = 0;
    }
    else if (mode & EVENT_WAIT_CLEAR)
    {
        info.pattern &= ~mask;
    }
}

static const std::filesystem::path &getRuntimeBasePath()
{
    static const std::filesystem::path runtimeBasePath = std::filesystem::current_path();
    return runtimeBasePath;
}

static const std::filesystem::path &getHostBasePath()
{
    static const std::filesystem::path hostBasePath = getRuntimeBasePath() / "host_fs";
    return hostBasePath;
}

static const std::filesystem::path &getCdBasePath()
{
    static const std::filesystem::path cdBasePath = getRuntimeBasePath() / "cd_fs";
    return cdBasePath;
}

static const std::filesystem::path &getMc0BasePath()
{
    static const std::filesystem::path mc0BasePath = getRuntimeBasePath() / "mc0_fs";
    return mc0BasePath;
}

static bool pathStartsWith(const std::filesystem::path &pathValue, const std::filesystem::path &basePath)
{
    auto pathIt = pathValue.begin();
    auto baseIt = basePath.begin();

    for (; baseIt != basePath.end(); ++baseIt, ++pathIt)
    {
        if (pathIt == pathValue.end() || *pathIt != *baseIt)
        {
            return false;
        }
    }
    return true;
}

static std::string mapPathUnderBase(const std::filesystem::path &basePath, std::string relativePath)
{
    std::filesystem::path normalizedBase = basePath.lexically_normal();
    std::filesystem::create_directories(normalizedBase);

    while (!relativePath.empty() && (relativePath.front() == '/' || relativePath.front() == '\\'))
    {
        relativePath.erase(relativePath.begin());
    }

    const std::filesystem::path candidatePath = (normalizedBase / relativePath).lexically_normal();
    if (!pathStartsWith(candidatePath, normalizedBase))
    {
        std::cerr << "Warning: Rejected path escaping base root: " << candidatePath << std::endl;
        return "";
    }

    return candidatePath.string();
}

static std::shared_ptr<SemaInfo> findSema(int sid)
{
    std::lock_guard<std::mutex> lock(g_sync_state_mutex);
    auto it = g_semas.find(sid);
    if (it == g_semas.end())
    {
        return {};
    }
    return it->second;
}

static std::shared_ptr<EventFlagInfo> findEventFlag(int id)
{
    std::lock_guard<std::mutex> lock(g_sync_state_mutex);
    auto it = g_eventFlags.find(id);
    if (it == g_eventFlags.end())
    {
        return {};
    }
    return it->second;
}

static int hostFileSeek64(FILE *fp, int64_t offset, int whence)
{
#ifdef _WIN32
    return _fseeki64(fp, offset, whence);
#else
    return fseeko(fp, static_cast<off_t>(offset), whence);
#endif
}

static int64_t hostFileTell64(FILE *fp)
{
#ifdef _WIN32
    return _ftelli64(fp);
#else
    const off_t pos = ftello(fp);
    if (pos < 0)
    {
        return -1;
    }
    return static_cast<int64_t>(pos);
#endif
}

int allocatePs2Fd(FILE *file)
{
    if (!file)
        return -1;
    std::lock_guard<std::mutex> lock(g_sys_fd_mutex);
    int fd = g_nextFd++;
    g_fileDescriptors[fd] = file;
    return fd;
}

const char *translateFioMode(int ps2Flags)
{
    int accessMode = ps2Flags & PS2_FIO_O_RDWR;
    bool read = accessMode == PS2_FIO_O_RDONLY || accessMode == PS2_FIO_O_RDWR || accessMode == 0;
    bool write = accessMode == PS2_FIO_O_WRONLY || accessMode == PS2_FIO_O_RDWR;
    bool append = (ps2Flags & PS2_FIO_O_APPEND);
    bool truncate = (ps2Flags & PS2_FIO_O_TRUNC);

    if (read && write)
    {
        if (append)
            return "a+b";
        if (truncate)
            return "w+b";
        return "r+b";
    }
    else if (write)
    {
        if (append)
            return "ab";
        if (truncate)
            return "wb";
        return "r+b";
    }
    else if (read)
    {
        return "rb";
    }
    return "rb";
}

std::string translatePs2Path(const char *ps2Path)
{
    if (!ps2Path || *ps2Path == '\0')
    {
        return "";
    }

    std::string pathStr(ps2Path);
    for (char &c : pathStr)
    {
        if (c == '\\')
        {
            c = '/';
        }
    }

    auto mapToHostCwd = [&](std::string relativePath) -> std::string
    {
        const std::filesystem::path hostBasePath = getHostBasePath().lexically_normal();

        if (!relativePath.empty() && (relativePath.front() == '/' || relativePath.front() == '\\'))
        {
            return mapPathUnderBase(hostBasePath, std::move(relativePath));
        }

        std::filesystem::path cwd;
        {
            std::lock_guard<std::mutex> lock(g_fs_state_mutex);
            if (g_virtualHostCwd.empty())
            {
                g_virtualHostCwd = hostBasePath;
            }
            cwd = g_virtualHostCwd.lexically_normal();
            if (!pathStartsWith(cwd, hostBasePath))
            {
                g_virtualHostCwd = hostBasePath;
                cwd = hostBasePath;
            }

            // If emulated cwd was removed externally, fall back to host root.
            std::error_code ec;
            if (!std::filesystem::is_directory(cwd, ec))
            {
                g_virtualHostCwd = hostBasePath;
                cwd = hostBasePath;
            }
        }

        const std::filesystem::path candidatePath = (cwd / relativePath).lexically_normal();
        if (!pathStartsWith(candidatePath, hostBasePath))
        {
            std::cerr << "Warning: Rejected host cwd-relative path escaping host_fs: " << candidatePath << std::endl;
            return "";
        }

        return candidatePath.string();
    };

    const std::filesystem::path hostBasePath = getHostBasePath().lexically_normal();
    const std::filesystem::path cdBasePath = getCdBasePath().lexically_normal();
    const std::filesystem::path mc0BasePath = getMc0BasePath().lexically_normal();

    if (pathStr.rfind("host0:", 0) == 0)
    {
        return mapPathUnderBase(hostBasePath, pathStr.substr(6));
    }
    if (pathStr.rfind("host:", 0) == 0)
    {
        return mapPathUnderBase(hostBasePath, pathStr.substr(5));
    }
    if (pathStr.rfind("cdrom0:", 0) == 0)
    {
        return mapPathUnderBase(cdBasePath, pathStr.substr(7));
    }
    if (pathStr.rfind("cdrom:", 0) == 0)
    {
        return mapPathUnderBase(cdBasePath, pathStr.substr(6));
    }
    if (pathStr.rfind("mc0:", 0) == 0)
    {
        return mapPathUnderBase(mc0BasePath, pathStr.substr(4));
    }

    const size_t colon = pathStr.find(':');
    if (colon != std::string::npos)
    {
        static int warnCount = 0;
        if (warnCount < 16)
        {
            std::cerr << "Warning: Unsupported PS2 path prefix, mapping to host_fs: " << pathStr << std::endl;
            ++warnCount;
        }
        return mapToHostCwd(pathStr.substr(colon + 1));
    }

    // Treat unprefixed paths as host filesystem paths, relative to emulated cwd.
    return mapToHostCwd(pathStr);
}

namespace ps2_syscalls
{

    void FlushCache(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        std::cout << "Syscall: FlushCache (No-op)" << std::endl;
        // No-op for now
        setReturnS32(ctx, 0);
    }

    void ResetEE(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        std::cerr << "Syscall: ResetEE - Halting Execution (Not fully implemented)" << std::endl;
        exit(0); // Should we exit or just halt the execution?
    }

    void SetMemoryMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        // Affects memory mapping / TLB behavior.
        // std::cout << "Syscall: SetMemoryMode (No-op)" << std::endl;
        setReturnS32(ctx, 0); // Success
    }

    void CreateThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t paramAddr = getRegU32(ctx, 4); // $a0 points to ThreadParam
        const uint32_t *param = reinterpret_cast<const uint32_t *>(getConstMemPtr(rdram, paramAddr));

        if (!param)
        {
            std::cerr << "CreateThread error: invalid ThreadParam address 0x" << std::hex << paramAddr << std::dec << std::endl;
            setReturnS32(ctx, -1);
            return;
        }

        ThreadInfo info{};
        info.attr = param[0];
        info.entry = param[1];
        info.stack = param[2];
        info.stackSize = param[3];
        info.gp = param[5];       // Often gp is at offset 20
        info.priority = param[4]; // Commonly priority/init attr slot
        info.option = param[6];

        int id = 0;
        {
            std::lock_guard<std::mutex> lock(g_thread_state_mutex);
            id = g_nextThreadId++;
            g_threads[id] = info;
        }

        std::cout << "[CreateThread] id=" << id
                  << " entry=0x" << std::hex << info.entry
                  << " stack=0x" << info.stack
                  << " size=0x" << info.stackSize
                  << " gp=0x" << info.gp
                  << " prio=" << std::dec << info.priority << std::endl;

        setReturnS32(ctx, id);
    }

    void DeleteThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int tid = static_cast<int>(getRegU32(ctx, 4)); // $a0
        {
            std::lock_guard<std::mutex> lock(g_thread_state_mutex);
            g_threads.erase(tid);
        }
        setReturnS32(ctx, 0);
    }

    void StartThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int tid = static_cast<int>(getRegU32(ctx, 4)); // $a0 = thread id
        uint32_t arg = getRegU32(ctx, 5);              // $a1 = user arg

        ThreadInfo threadInfo{};
        {
            std::lock_guard<std::mutex> lock(g_thread_state_mutex);
            auto it = g_threads.find(tid);
            if (it == g_threads.end())
            {
                std::cerr << "StartThread error: unknown thread id " << tid << std::endl;
                setReturnS32(ctx, -1);
                return;
            }

            if (it->second.started)
            {
                setReturnS32(ctx, 0);
                return;
            }

            threadInfo = it->second;
        }

        if (!runtime || !runtime->hasFunction(threadInfo.entry))
        {
            std::cerr << "[StartThread] entry 0x" << std::hex << threadInfo.entry << std::dec
                      << " is not registered or runtime is unavailable" << std::endl;
            setReturnS32(ctx, -1);
            return;
        }

        // TODO check later skip audio threads to avoid runaway recursion/stack overflows.
        if (threadInfo.entry == 0x2f42a0 || threadInfo.entry == 0x2f4258)
        {
            std::lock_guard<std::mutex> lock(g_thread_state_mutex);
            auto it = g_threads.find(tid);
            if (it != g_threads.end())
            {
                it->second.started = true;
                it->second.arg = arg;
            }

            std::cout << "[StartThread] id=" << tid
                      << " entry=0x" << std::hex << threadInfo.entry << std::dec
                      << " skipped (audio thread stub)" << std::endl;
            setReturnS32(ctx, 0);
            return;
        }

        {
            std::lock_guard<std::mutex> lock(g_thread_state_mutex);
            auto it = g_threads.find(tid);
            if (it == g_threads.end())
            {
                std::cerr << "StartThread error: thread deleted before start " << tid << std::endl;
                setReturnS32(ctx, -1);
                return;
            }

            if (it->second.started)
            {
                setReturnS32(ctx, 0);
                return;
            }

            it->second.started = true;
            it->second.arg = arg;
            threadInfo = it->second;
        }

        const R5900Context parentCtx = *ctx;

        // Spawn a host thread to simulate PS2 thread execution.
        g_activeThreads.fetch_add(1, std::memory_order_relaxed);
        std::thread([=]() mutable
                    {
                        R5900Context threadCtxCopy = parentCtx; // Copy caller CPU state to simulate a new thread context
                        R5900Context *threadCtx = &threadCtxCopy;

                        if (threadInfo.stack && threadInfo.stackSize)
                        {
                            SET_GPR_U32(threadCtx, 29, threadInfo.stack + threadInfo.stackSize); // SP at top of stack
                        }
                        if (threadInfo.gp)
                        {
                            SET_GPR_U32(threadCtx, 28, threadInfo.gp);
                        }

                        SET_GPR_U32(threadCtx, 4, threadInfo.arg);
                        threadCtx->pc = threadInfo.entry;

                        PS2Runtime::RecompiledFunction func = runtime->lookupFunction(threadInfo.entry);
                        g_currentThreadId = tid;

                        std::cout << "[StartThread] id=" << tid
                                  << " entry=0x" << std::hex << threadInfo.entry
                                  << " sp=0x" << GPR_U32(threadCtx, 29)
                                  << " gp=0x" << GPR_U32(threadCtx, 28)
                                  << " arg=0x" << threadInfo.arg << std::dec << std::endl;

                        try
                        {
                            func(rdram, threadCtx, runtime);
                        }
                        catch (const std::exception &e)
                        {
                            std::cerr << "[StartThread] id=" << tid << " exception: " << e.what() << std::endl;
                        }

                        std::cout << "[StartThread] id=" << tid << " returned (pc=0x"
                                  << std::hex << threadCtx->pc << std::dec << ")" << std::endl;

                        g_activeThreads.fetch_sub(1, std::memory_order_relaxed); })
            .detach();

        // for now report success to the caller.
        setReturnS32(ctx, 0);
    }

    void ExitThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        std::cout << "PS2 ExitThread: Thread is exiting (PC=0x" << std::hex << ctx->pc << std::dec << ")" << std::endl;
        setReturnS32(ctx, 0);
    }

    void ExitDeleteThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int tid = static_cast<int>(getRegU32(ctx, 4));
        {
            std::lock_guard<std::mutex> lock(g_thread_state_mutex);
            g_threads.erase(tid);
        }
        setReturnS32(ctx, 0);
    }

    void TerminateThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int tid = static_cast<int>(getRegU32(ctx, 4));
        {
            std::lock_guard<std::mutex> lock(g_thread_state_mutex);
            g_threads.erase(tid);
        }
        setReturnS32(ctx, 0);
    }

    void SuspendThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        int tid = static_cast<int>(getRegU32(ctx, 4));
        if (logCount < 16)
        {
            std::cout << "[SuspendThread] tid=" << tid << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void ResumeThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void GetThreadId(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, g_currentThreadId);
    }

    void ReferThreadStatus(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int tid = static_cast<int>(getRegU32(ctx, 4));
        uint32_t statusAddr = getRegU32(ctx, 5);

        if (statusAddr == 0)
        {
            setReturnS32(ctx, -1);
            return;
        }

        ThreadInfo thread{};
        {
            std::lock_guard<std::mutex> lock(g_thread_state_mutex);
            auto it = g_threads.find(tid);
            if (it == g_threads.end())
            {
                setReturnS32(ctx, -1);
                return;
            }
            thread = it->second;
        }

        uint32_t *status = reinterpret_cast<uint32_t *>(getMemPtr(rdram, statusAddr));
        if (!status)
        {
            setReturnS32(ctx, -1);
            return;
        }

        // Minimal ThreadStatus layout with commonly consumed fields.
        status[0] = thread.attr;
        status[1] = thread.option;
        status[2] = thread.started ? 1u : 0u;
        status[3] = thread.entry;
        status[4] = thread.stack;
        status[5] = thread.stackSize;
        status[6] = thread.gp;
        status[7] = thread.priority;
        status[8] = thread.arg;
        status[9] = 0;

        setReturnS32(ctx, 0);
    }

    void SleepThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 16)
        {
            std::cout << "[SleepThread] tid=" << g_currentThreadId << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void WakeupThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        int tid = static_cast<int>(getRegU32(ctx, 4));
        if (logCount < 32)
        {
            std::cout << "[WakeupThread] tid=" << tid << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void iWakeupThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        int tid = static_cast<int>(getRegU32(ctx, 4));
        if (logCount < 32)
        {
            std::cout << "[iWakeupThread] tid=" << tid << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void CancelWakeupThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 32)
        {
            std::cout << "[CancelWakeupThread]" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void iCancelWakeupThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 32)
        {
            std::cout << "[iCancelWakeupThread]" << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void ChangeThreadPriority(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int tid = static_cast<int>(getRegU32(ctx, 4));
        int newPrio = static_cast<int>(getRegU32(ctx, 5));
        std::lock_guard<std::mutex> lock(g_thread_state_mutex);
        auto it = g_threads.find(tid);
        if (it != g_threads.end())
        {
            it->second.priority = newPrio;
        }
        setReturnS32(ctx, 0);
    }

    void RotateThreadReadyQueue(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        int prio = static_cast<int>(getRegU32(ctx, 4));
        if (logCount < 16)
        {
            std::cout << "[RotateThreadReadyQueue] prio=" << prio << std::endl;
            ++logCount;
        }
        if (prio >= 128)
        {
            setReturnS32(ctx, -1);
            return;
        }
        setReturnS32(ctx, 0);
    }

    void ReleaseWaitThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void iReleaseWaitThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void CreateSema(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t paramAddr = getRegU32(ctx, 4); // $a0
        const uint32_t *param = reinterpret_cast<const uint32_t *>(getConstMemPtr(rdram, paramAddr));
        uint32_t attr = 0;
        uint32_t option = 0;
        int init = 0;
        int max = 1;
        if (param)
        {
            // sceSemaParam layout commonly: attr(0), option(1), initCount(2), maxCount(3)
            attr = param[0];
            option = param[1];
            init = static_cast<int>(param[2]);
            max = static_cast<int>(param[3]);
        }
        if (max <= 0)
        {
            max = 1; // avoid dead semaphores, but maybe not good ideia
        }
        if (init > max)
        {
            init = max;
        }

        int id = 0;
        auto info = std::make_shared<SemaInfo>();
        info->attr = attr;
        info->option = option;
        info->initCount = init;
        info->count = init;
        info->maxCount = max;
        {
            std::lock_guard<std::mutex> lock(g_sync_state_mutex);
            id = g_nextSemaId++;
            g_semas.emplace(id, info);
        }
        std::cout << "[CreateSema] id=" << id << " init=" << init << " max=" << max << std::endl;
        setReturnS32(ctx, id);
    }

    void DeleteSema(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int sid = static_cast<int>(getRegU32(ctx, 4));
        std::shared_ptr<SemaInfo> sema;
        {
            std::lock_guard<std::mutex> lock(g_sync_state_mutex);
            auto it = g_semas.find(sid);
            if (it != g_semas.end())
            {
                sema = it->second;
                g_semas.erase(it);
            }
        }

        if (!sema)
        {
            setReturnS32(ctx, -1);
            return;
        }

        {
            std::lock_guard<std::mutex> lock(sema->m);
            sema->deleted = true;
        }
        sema->cv.notify_all();
        setReturnS32(ctx, 0);
    }

    void SignalSema(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int sid = static_cast<int>(getRegU32(ctx, 4));
        auto sema = findSema(sid);
        if (!sema)
        {
            setReturnS32(ctx, -1);
            return;
        }
        {
            std::lock_guard<std::mutex> lock(sema->m);
            if (sema->deleted)
            {
                setReturnS32(ctx, -1);
                return;
            }
            if (sema->count < sema->maxCount)
            {
                sema->count++;
            }
            sema->cv.notify_one();
        }
        setReturnS32(ctx, 0);
    }

    void iSignalSema(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        SignalSema(rdram, ctx, runtime);
    }

    void WaitSema(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int sid = static_cast<int>(getRegU32(ctx, 4));
        auto sema = findSema(sid);
        if (!sema)
        {
            setReturnS32(ctx, -1);
            return;
        }
        std::unique_lock<std::mutex> lock(sema->m);
        if (sema->deleted)
        {
            setReturnS32(ctx, -1);
            return;
        }
        static int globalLog = 0;
        if (globalLog < 5)
        {
            std::cout << "[WaitSema] sid=" << sid << " count=" << sema->count << std::endl;
            ++globalLog;
        }

        if (sema->count == 0)
        {
            static thread_local int logCount = 0;
            if (logCount < 3)
            {
                std::cout << "[WaitSema] sid=" << sid << " blocking until signaled" << std::endl;
                ++logCount;
            }

            sema->waiters++;
            sema->cv.wait(lock, [&]()
                          { return sema->count > 0 || sema->deleted; });
            if (sema->waiters > 0)
            {
                sema->waiters--;
            }
        }

        if (sema->deleted)
        {
            setReturnS32(ctx, -1);
            return;
        }

        if (sema->count > 0)
        {
            sema->count--;
            setReturnS32(ctx, 0);
            return;
        }

        setReturnS32(ctx, -1);
    }

    void PollSema(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int sid = static_cast<int>(getRegU32(ctx, 4));
        auto sema = findSema(sid);
        if (!sema)
        {
            setReturnS32(ctx, -1);
            return;
        }
        std::lock_guard<std::mutex> lock(sema->m);
        if (sema->deleted)
        {
            setReturnS32(ctx, -1);
            return;
        }
        if (sema->count > 0)
        {
            sema->count--;
            setReturnS32(ctx, 0);
            return;
        }

        setReturnS32(ctx, -1);
    }

    void iPollSema(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        PollSema(rdram, ctx, runtime);
    }

    void ReferSemaStatus(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int sid = static_cast<int>(getRegU32(ctx, 4));
        uint32_t statusAddr = getRegU32(ctx, 5);

        auto sema = findSema(sid);
        if (!sema || statusAddr == 0)
        {
            setReturnS32(ctx, -1);
            return;
        }

        uint32_t *status = reinterpret_cast<uint32_t *>(getMemPtr(rdram, statusAddr));
        if (!status)
        {
            setReturnS32(ctx, -1);
            return;
        }

        std::lock_guard<std::mutex> lock(sema->m);

        // Minimal sceSemaInfo-compatible layout.
        status[0] = sema->attr;
        status[1] = sema->option;
        status[2] = static_cast<uint32_t>(sema->initCount);
        status[3] = static_cast<uint32_t>(sema->maxCount);
        status[4] = static_cast<uint32_t>(sema->count);
        status[5] = static_cast<uint32_t>(sema->waiters);

        setReturnS32(ctx, 0);
    }

    void iReferSemaStatus(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ReferSemaStatus(rdram, ctx, runtime);
    }

    void CreateEventFlag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t paramAddr = getRegU32(ctx, 4); // $a0
        const uint32_t *param = reinterpret_cast<const uint32_t *>(getConstMemPtr(rdram, paramAddr));

        uint32_t attr = 0;
        uint32_t option = 0;
        uint32_t initPattern = 0;
        if (param)
        {
            // Common sceEventFlagParam layout: attr, option, initPattern.
            attr = param[0];
            option = param[1];
            initPattern = param[2];
        }

        int id = 0;
        auto info = std::make_shared<EventFlagInfo>();
        info->attr = attr;
        info->option = option;
        info->initPattern = initPattern;
        info->pattern = initPattern;

        {
            std::lock_guard<std::mutex> lock(g_sync_state_mutex);
            id = g_nextEventFlagId++;
            g_eventFlags.emplace(id, info);
        }
        setReturnS32(ctx, id);
    }

    void DeleteEventFlag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int id = static_cast<int>(getRegU32(ctx, 4));
        std::shared_ptr<EventFlagInfo> flag;
        {
            std::lock_guard<std::mutex> lock(g_sync_state_mutex);
            auto it = g_eventFlags.find(id);
            if (it != g_eventFlags.end())
            {
                flag = it->second;
                g_eventFlags.erase(it);
            }
        }

        if (!flag)
        {
            setReturnS32(ctx, -1);
            return;
        }

        {
            std::lock_guard<std::mutex> lock(flag->m);
            flag->deleted = true;
        }
        flag->cv.notify_all();
        setReturnS32(ctx, 0);
    }

    void SetEventFlag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int id = static_cast<int>(getRegU32(ctx, 4));
        uint32_t bits = getRegU32(ctx, 5);

        auto flag = findEventFlag(id);
        if (!flag)
        {
            setReturnS32(ctx, -1);
            return;
        }
        {
            std::lock_guard<std::mutex> lock(flag->m);
            if (flag->deleted)
            {
                setReturnS32(ctx, -1);
                return;
            }
            flag->pattern |= bits;
        }
        flag->cv.notify_all();
        setReturnS32(ctx, 0);
    }

    void iSetEventFlag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        SetEventFlag(rdram, ctx, runtime);
    }

    void ClearEventFlag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int id = static_cast<int>(getRegU32(ctx, 4));
        uint32_t bits = getRegU32(ctx, 5);

        auto flag = findEventFlag(id);
        if (!flag)
        {
            setReturnS32(ctx, -1);
            return;
        }
        {
            std::lock_guard<std::mutex> lock(flag->m);
            if (flag->deleted)
            {
                setReturnS32(ctx, -1);
                return;
            }
            // EE kernel semantics: clear with `current &= bits`.
            flag->pattern &= bits;
        }
        setReturnS32(ctx, 0);
    }

    void iClearEventFlag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ClearEventFlag(rdram, ctx, runtime);
    }

    void WaitEventFlag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int id = static_cast<int>(getRegU32(ctx, 4));
        uint32_t bits = getRegU32(ctx, 5);
        uint32_t mode = getRegU32(ctx, 6);
        uint32_t resultAddr = getRegU32(ctx, 7);

        auto flag = findEventFlag(id);
        if (!flag)
        {
            setReturnS32(ctx, -1);
            return;
        }
        std::unique_lock<std::mutex> lock(flag->m);
        if (flag->deleted)
        {
            setReturnS32(ctx, -1);
            return;
        }

        flag->waiters++;
        flag->cv.wait(lock, [&]()
                      { return flag->deleted || eventConditionMet(flag->pattern, bits, mode); });

        if (flag->waiters > 0)
        {
            flag->waiters--;
        }

        if (flag->deleted)
        {
            setReturnS32(ctx, -1);
            return;
        }

        const uint32_t matched = flag->pattern;
        if (resultAddr != 0)
        {
            uint32_t *result = reinterpret_cast<uint32_t *>(getMemPtr(rdram, resultAddr));
            if (!result)
            {
                setReturnS32(ctx, -1);
                return;
            }
            *result = matched;
        }

        applyEventWaitMode(*flag, bits, mode);
        setReturnS32(ctx, 0);
    }

    void PollEventFlag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int id = static_cast<int>(getRegU32(ctx, 4));
        uint32_t bits = getRegU32(ctx, 5);
        uint32_t mode = getRegU32(ctx, 6);
        uint32_t resultAddr = getRegU32(ctx, 7);

        auto flag = findEventFlag(id);
        if (!flag)
        {
            setReturnS32(ctx, -1);
            return;
        }
        std::lock_guard<std::mutex> lock(flag->m);
        if (flag->deleted)
        {
            setReturnS32(ctx, -1);
            return;
        }
        if (!eventConditionMet(flag->pattern, bits, mode))
        {
            setReturnS32(ctx, -1);
            return;
        }

        const uint32_t matched = flag->pattern;
        if (resultAddr != 0)
        {
            uint32_t *result = reinterpret_cast<uint32_t *>(getMemPtr(rdram, resultAddr));
            if (!result)
            {
                setReturnS32(ctx, -1);
                return;
            }
            *result = matched;
        }

        applyEventWaitMode(*flag, bits, mode);
        setReturnS32(ctx, 0);
    }

    void iPollEventFlag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        PollEventFlag(rdram, ctx, runtime);
    }

    void ReferEventFlagStatus(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int id = static_cast<int>(getRegU32(ctx, 4));
        uint32_t statusAddr = getRegU32(ctx, 5);

        auto flag = findEventFlag(id);
        if (!flag || statusAddr == 0)
        {
            setReturnS32(ctx, -1);
            return;
        }

        uint32_t *status = reinterpret_cast<uint32_t *>(getMemPtr(rdram, statusAddr));
        if (!status)
        {
            setReturnS32(ctx, -1);
            return;
        }

        std::lock_guard<std::mutex> lock(flag->m);
        if (flag->deleted)
        {
            setReturnS32(ctx, -1);
            return;
        }

        // Minimal sceEventFlagInfo-compatible layout.
        status[0] = flag->attr;
        status[1] = flag->option;
        status[2] = flag->initPattern;
        status[3] = flag->pattern;
        status[4] = static_cast<uint32_t>(flag->waiters);
        status[5] = 0;

        setReturnS32(ctx, 0);
    }

    void iReferEventFlagStatus(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ReferEventFlagStatus(rdram, ctx, runtime);
    }

    // According to GPT the real PS2 uses a timer interrupt to invoke a callback. For now, fire  the callback immediately
    void SetAlarm(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t usec = getRegU32(ctx, 4);
        uint32_t handler = getRegU32(ctx, 5);
        uint32_t arg = getRegU32(ctx, 6);

        static int logCount = 0;
        if (logCount < 5)
        {
            std::cout << "[SetAlarm] usec=" << usec
                      << " handler=0x" << std::hex << handler
                      << " arg=0x" << arg << std::dec << std::endl;
            ++logCount;
        }

        // If the handler looks like a semaphore id, just kick it now.
        if (arg)
        {
            R5900Context localCtx = *ctx;
            R5900Context *ctxPtr = &localCtx;
            SET_GPR_U32(ctxPtr, 4, arg);
            SignalSema(rdram, ctxPtr, runtime);
        }

        setReturnS32(ctx, 0);
    }

    void iSetAlarm(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        SetAlarm(rdram, ctx, runtime);
    }

    void CancelAlarm(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void iCancelAlarm(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        CancelAlarm(rdram, ctx, runtime);
    }

    void EnableIntc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void DisableIntc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void EnableDmac(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void DisableDmac(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void SifStopModule(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void SifLoadModule(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t pathAddr = getRegU32(ctx, 4);
        const char *path = reinterpret_cast<const char *>(getConstMemPtr(rdram, pathAddr));
        static int logCount = 0;
        if (logCount < 3)
        {
            std::cout << "[SifLoadModule] path=" << (path ? path : "<bad>") << std::endl;
            ++logCount;
        }
        // Return a fake module id > 0 to indicate success.
        setReturnS32(ctx, 1);
    }

    void SifInitRpc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void SifBindRpc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t clientPtr = getRegU32(ctx, 4);
        uint32_t rpcId = getRegU32(ctx, 5);
        uint32_t mode = getRegU32(ctx, 6);

        uint32_t *p = reinterpret_cast<uint32_t *>(getMemPtr(rdram, clientPtr));
        if (p)
        {
            // server cookie/non-null marker
            p[0] = clientPtr ? clientPtr : 1;
            // rpc number (typical offset 12)
            p[3] = rpcId;
            // mode (offset 32)
            p[8] = mode;
            // some callers read a word at +36 to test readiness
            p[9] = 1;
        }

        static int logCount = 0;
        if (logCount < 5)
        {
            std::cout << "[SifBindRpc] client=0x" << std::hex << clientPtr
                      << " rpcId=0x" << rpcId
                      << " mode=0x" << mode << std::dec << std::endl;
            ++logCount;
        }

        setReturnS32(ctx, 0);
    }

    void SifCallRpc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t clientPtr = getRegU32(ctx, 4);
        uint32_t rpcId = getRegU32(ctx, 5);
        uint32_t mode = getRegU32(ctx, 6);
        uint32_t sendBuf = getRegU32(ctx, 7);

        uint32_t *p = reinterpret_cast<uint32_t *>(getMemPtr(rdram, clientPtr));
        if (p)
        {
            // Mark completion flag at +36.
            p[9] = 1;
        }

        static int logCount = 0;
        if (logCount < 5)
        {
            std::cout << "[SifCallRpc] client=0x" << std::hex << clientPtr
                      << " rpcId=0x" << rpcId
                      << " mode=0x" << mode
                      << " sendBuf=0x" << sendBuf << std::dec << std::endl;
            ++logCount;
        }

        setReturnS32(ctx, 0);
    }

    void SifRegisterRpc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void SifCheckStatRpc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 1);
    }

    void SifSetRpcQueue(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void SifRemoveRpcQueue(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void SifRemoveRpc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void sceSifCallRpc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        SifCallRpc(rdram, ctx, runtime);
    }

    void sceSifSendCmd(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 5)
        {
            std::cout << "[sceSifSendCmd] cmd=0x" << std::hex << getRegU32(ctx, 4)
                      << " packet=0x" << getRegU32(ctx, 5)
                      << " size=0x" << getRegU32(ctx, 6)
                      << " dest=0x" << getRegU32(ctx, 7) << std::dec << std::endl;
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void _sceRpcGetPacket(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t queuePtr = getRegU32(ctx, 4);
        setReturnS32(ctx, static_cast<int32_t>(queuePtr));
    }

    void fioOpen(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t pathAddr = getRegU32(ctx, 4); // $a0
        int flags = (int)getRegU32(ctx, 5);    // $a1 (PS2 FIO flags)

        const char *ps2Path = reinterpret_cast<const char *>(getConstMemPtr(rdram, pathAddr));
        if (!ps2Path)
        {
            std::cerr << "fioOpen error: Invalid path address" << std::endl;
            setReturnS32(ctx, -1);
            return;
        }

        std::string hostPath = translatePs2Path(ps2Path);
        if (hostPath.empty())
        {
            std::cerr << "fioOpen error: Failed to translate path '" << ps2Path << "'" << std::endl;
            setReturnS32(ctx, -1);
            return;
        }

        const bool createRequested = (flags & PS2_FIO_O_CREAT) != 0;
        const bool exclusiveRequested = (flags & PS2_FIO_O_EXCL) != 0;
        const bool truncateRequested = (flags & PS2_FIO_O_TRUNC) != 0;
        const int accessMode = flags & PS2_FIO_O_RDWR;
        const bool writeRequested = accessMode == PS2_FIO_O_WRONLY || accessMode == PS2_FIO_O_RDWR;

        if (createRequested)
        {
            std::error_code ec;
            const std::filesystem::path parentPath = std::filesystem::path(hostPath).parent_path();
            if (!parentPath.empty())
            {
                std::filesystem::create_directories(parentPath, ec);
            }
        }

        if (createRequested && exclusiveRequested && std::filesystem::exists(hostPath))
        {
            std::cerr << "fioOpen error: Exclusive create requested but file already exists: " << hostPath << std::endl;
            setReturnS32(ctx, -1);
            return;
        }

        if (!createRequested && truncateRequested && writeRequested && !std::filesystem::exists(hostPath))
        {
            std::cerr << "fioOpen error: truncate requested on missing file without O_CREAT: " << hostPath << std::endl;
            setReturnS32(ctx, -1);
            return;
        }

        if (createRequested && !truncateRequested && !std::filesystem::exists(hostPath))
        {
            std::ofstream createFile(hostPath, std::ios::binary);
            if (!createFile)
            {
                std::cerr << "fioOpen error: failed to create file '" << hostPath << "'" << std::endl;
                setReturnS32(ctx, -1);
                return;
            }
        }

        const char *mode = translateFioMode(flags);
        static int openLogCount = 0;
        if (openLogCount < 64)
        {
            std::cout << "fioOpen: '" << hostPath << "' flags=0x" << std::hex << flags << std::dec << " mode='" << mode << "'" << std::endl;
            ++openLogCount;
        }

        FILE *fp = ::fopen(hostPath.c_str(), mode);
        if (!fp)
        {
            std::cerr << "fioOpen error: fopen failed for '" << hostPath << "': " << strerror(errno) << std::endl;
            setReturnS32(ctx, -1); // e.g., -ENOENT, -EACCES
            return;
        }

        int ps2Fd = allocatePs2Fd(fp);
        if (ps2Fd < 0)
        {
            std::cerr << "fioOpen error: Failed to allocate PS2 file descriptor" << std::endl;
            ::fclose(fp);
            setReturnS32(ctx, -1); // e.g., -EMFILE
            return;
        }

        // returns the PS2 file descriptor
        setReturnS32(ctx, ps2Fd);
    }

    void fioClose(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int ps2Fd = (int)getRegU32(ctx, 4); // $a0
        static int closeLogCount = 0;
        if (closeLogCount < 64)
        {
            std::cout << "fioClose: fd=" << ps2Fd << std::endl;
            ++closeLogCount;
        }

        FILE *fp = nullptr;
        {
            std::lock_guard<std::mutex> lock(g_sys_fd_mutex);
            auto it = g_fileDescriptors.find(ps2Fd);
            if (it != g_fileDescriptors.end())
            {
                fp = it->second;
                g_fileDescriptors.erase(it);
            }
        }

        if (!fp)
        {
            std::cerr << "fioClose warning: Invalid PS2 file descriptor " << ps2Fd << std::endl;
            setReturnS32(ctx, -1); // e.g., -EBADF
            return;
        }

        int ret = ::fclose(fp);

        // returns 0 on success, -1 on error
        setReturnS32(ctx, ret == 0 ? 0 : -1);
    }

    void fioRead(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int ps2Fd = (int)getRegU32(ctx, 4);   // $a0
        uint32_t bufAddr = getRegU32(ctx, 5); // $a1
        size_t size = getRegU32(ctx, 6);      // $a2

        uint8_t *hostBuf = getMemPtr(rdram, bufAddr);

        if (!hostBuf)
        {
            std::cerr << "fioRead error: Invalid buffer address for fd " << ps2Fd << std::endl;
            setReturnS32(ctx, -1); // -EFAULT
            return;
        }
        if (size == 0)
        {
            setReturnS32(ctx, 0); // Read 0 bytes
            return;
        }

        size_t bytesRead = 0;
        bool validFd = true;
        bool readError = false;
        {
            std::lock_guard<std::mutex> lock(g_sys_fd_mutex);
            auto it = g_fileDescriptors.find(ps2Fd);
            if (it == g_fileDescriptors.end())
            {
                validFd = false;
            }
            else
            {
                FILE *fp = it->second;
                bytesRead = fread(hostBuf, 1, size, fp);
                readError = (bytesRead < size && ferror(fp));
                if (readError)
                {
                    clearerr(fp);
                }
            }
        }

        if (!validFd)
        {
            std::cerr << "fioRead error: Invalid file descriptor " << ps2Fd << std::endl;
            setReturnS32(ctx, -1); // -EBADF
            return;
        }

        if (readError)
        {
            std::cerr << "fioRead error: fread failed for fd " << ps2Fd << ": " << strerror(errno) << std::endl;
            setReturnS32(ctx, -1); // -EIO or other appropriate error
            return;
        }

        // returns number of bytes read (can be 0 for EOF)
        setReturnS32(ctx, (int32_t)bytesRead);
    }

    void fioWrite(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int ps2Fd = (int)getRegU32(ctx, 4);   // $a0
        uint32_t bufAddr = getRegU32(ctx, 5); // $a1
        size_t size = getRegU32(ctx, 6);      // $a2

        const uint8_t *hostBuf = getConstMemPtr(rdram, bufAddr);

        if (!hostBuf)
        {
            std::cerr << "fioWrite error: Invalid buffer address for fd " << ps2Fd << std::endl;
            setReturnS32(ctx, -1); // -EFAULT
            return;
        }
        if (size == 0)
        {
            setReturnS32(ctx, 0); // Wrote 0 bytes
            return;
        }

        size_t bytesWritten = 0;
        bool validFd = true;
        bool writeError = false;
        {
            std::lock_guard<std::mutex> lock(g_sys_fd_mutex);
            auto it = g_fileDescriptors.find(ps2Fd);
            if (it == g_fileDescriptors.end())
            {
                validFd = false;
            }
            else
            {
                FILE *fp = it->second;
                bytesWritten = ::fwrite(hostBuf, 1, size, fp);
                writeError = (bytesWritten < size && ferror(fp));
                if (writeError)
                {
                    clearerr(fp);
                }
            }
        }

        if (!validFd)
        {
            std::cerr << "fioWrite error: Invalid file descriptor " << ps2Fd << std::endl;
            setReturnS32(ctx, -1); // -EBADF
            return;
        }

        if (bytesWritten < size)
        {
            if (writeError)
            {
                std::cerr << "fioWrite error: fwrite failed for fd " << ps2Fd << ": " << strerror(errno) << std::endl;
                setReturnS32(ctx, -1); // -EIO, -ENOSPC etc.
            }
            else
            {
                // Partial write without error? Possible but idk.
                setReturnS32(ctx, (int32_t)bytesWritten);
            }
            return;
        }

        // returns number of bytes written
        setReturnS32(ctx, (int32_t)bytesWritten);
    }

    void fioLseek(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int ps2Fd = (int)getRegU32(ctx, 4);  // $a0
        int32_t offset = static_cast<int32_t>(getRegU32(ctx, 5)); // $a1 (signed 32-bit offset)
        int whence = (int)getRegU32(ctx, 6); // $a2 (PS2 FIO_SEEK constants)

        bool validFd = true;
        bool seekError = false;
        bool tellError = false;
        bool rangeError = false;
        int64_t newPos = -1;
        {
            std::lock_guard<std::mutex> lock(g_sys_fd_mutex);
            auto it = g_fileDescriptors.find(ps2Fd);
            if (it == g_fileDescriptors.end())
            {
                validFd = false;
            }
            else
            {
                FILE *fp = it->second;

                int64_t basePos = 0;
                if (whence == PS2_FIO_SEEK_SET)
                {
                    basePos = 0;
                }
                else if (whence == PS2_FIO_SEEK_CUR)
                {
                    basePos = hostFileTell64(fp);
                    if (basePos < 0)
                    {
                        tellError = true;
                    }
                }
                else if (whence == PS2_FIO_SEEK_END)
                {
                    const int64_t currentPos = hostFileTell64(fp);
                    if (currentPos < 0)
                    {
                        tellError = true;
                    }
                    else
                    {
                        if (hostFileSeek64(fp, 0, SEEK_END) != 0)
                        {
                            seekError = true;
                        }
                        else
                        {
                            basePos = hostFileTell64(fp);
                            if (basePos < 0)
                            {
                                tellError = true;
                            }
                            if (hostFileSeek64(fp, currentPos, SEEK_SET) != 0)
                            {
                                seekError = true;
                            }
                        }
                    }
                }
                else
                {
                    rangeError = true;
                }

                if (!seekError && !tellError && !rangeError)
                {
                    const int64_t targetPos = basePos + static_cast<int64_t>(offset);
                    if (targetPos < 0 || targetPos > std::numeric_limits<int32_t>::max())
                    {
                        rangeError = true;
                    }
                    else if (hostFileSeek64(fp, targetPos, SEEK_SET) != 0)
                    {
                        seekError = true;
                    }
                    else
                    {
                        newPos = hostFileTell64(fp);
                        if (newPos < 0)
                        {
                            tellError = true;
                        }
                    }
                }
            }
        }

        if (!validFd)
        {
            std::cerr << "fioLseek error: Invalid file descriptor " << ps2Fd << std::endl;
            setReturnS32(ctx, -1); // -EBADF
            return;
        }

        if (whence != PS2_FIO_SEEK_SET && whence != PS2_FIO_SEEK_CUR && whence != PS2_FIO_SEEK_END)
        {
            std::cerr << "fioLseek error: Invalid whence value " << whence << " for fd " << ps2Fd << std::endl;
            setReturnS32(ctx, -1);
            return;
        }

        if (rangeError)
        {
            std::cerr << "fioLseek error: target position out of 32-bit range for fd " << ps2Fd << std::endl;
            setReturnS32(ctx, -1);
            return;
        }

        if (seekError)
        {
            std::cerr << "fioLseek error: fseek failed for fd " << ps2Fd << ": " << strerror(errno) << std::endl;
            setReturnS32(ctx, -1); // Return error code
            return;
        }

        if (tellError || newPos < 0)
        {
            std::cerr << "fioLseek error: ftell failed after fseek for fd " << ps2Fd << ": " << strerror(errno) << std::endl;
            setReturnS32(ctx, -1); // Return error code
        }
        else
        {
            setReturnS32(ctx, static_cast<int32_t>(newPos));
        }
    }

    void fioMkdir(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t pathAddr = getRegU32(ctx, 4); // $a0

        const char *ps2Path = reinterpret_cast<const char *>(getConstMemPtr(rdram, pathAddr));
        if (!ps2Path)
        {
            std::cerr << "fioMkdir error: Invalid path address" << std::endl;
            setReturnS32(ctx, -1); // -EFAULT
            return;
        }
        std::string hostPath = translatePs2Path(ps2Path);
        if (hostPath.empty())
        {
            std::cerr << "fioMkdir error: Failed to translate path '" << ps2Path << "'" << std::endl;
            setReturnS32(ctx, -1);
            return;
        }

        std::error_code ec;
        const bool created = std::filesystem::create_directories(hostPath, ec);
        if (ec)
        {
            std::cerr << "fioMkdir error: mkdir failed for '" << hostPath << "': " << ec.message() << std::endl;
            setReturnS32(ctx, -1);
        }
        else
        {
            (void)created;
            setReturnS32(ctx, 0);
        }
    }

    void fioChdir(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t pathAddr = getRegU32(ctx, 4); // $a0
        const char *ps2Path = reinterpret_cast<const char *>(getConstMemPtr(rdram, pathAddr));
        if (!ps2Path)
        {
            std::cerr << "fioChdir error: Invalid path address" << std::endl;
            setReturnS32(ctx, -1);
            return;
        }

        std::string hostPath = translatePs2Path(ps2Path);
        if (hostPath.empty())
        {
            std::cerr << "fioChdir error: Failed to translate path '" << ps2Path << "'" << std::endl;
            setReturnS32(ctx, -1);
            return;
        }

        std::error_code ec;
        const bool isDir = std::filesystem::is_directory(hostPath, ec);
        if (ec || !isDir)
        {
            std::cerr << "fioChdir error: directory not found '" << hostPath << "'" << std::endl;
            setReturnS32(ctx, -1);
        }
        else
        {
            // Keep host process cwd unchanged; update emulated host cwd when applicable.
            const std::filesystem::path targetPath = std::filesystem::path(hostPath).lexically_normal();
            const std::filesystem::path hostBasePath = getHostBasePath().lexically_normal();
            if (pathStartsWith(targetPath, hostBasePath))
            {
                std::lock_guard<std::mutex> lock(g_fs_state_mutex);
                g_virtualHostCwd = targetPath;
            }
            setReturnS32(ctx, 0);
        }
    }

    void fioRmdir(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t pathAddr = getRegU32(ctx, 4); // $a0
        const char *ps2Path = reinterpret_cast<const char *>(getConstMemPtr(rdram, pathAddr));
        if (!ps2Path)
        {
            std::cerr << "fioRmdir error: Invalid path address" << std::endl;
            setReturnS32(ctx, -1);
            return;
        }
        std::string hostPath = translatePs2Path(ps2Path);
        if (hostPath.empty())
        {
            std::cerr << "fioRmdir error: Failed to translate path '" << ps2Path << "'" << std::endl;
            setReturnS32(ctx, -1);
            return;
        }

        std::error_code ec;
        const auto status = std::filesystem::status(hostPath, ec);
        if (ec || status.type() == std::filesystem::file_type::not_found)
        {
            std::cerr << "fioRmdir error: directory not found '" << hostPath << "'" << std::endl;
            setReturnS32(ctx, -1);
            return;
        }

        if (!std::filesystem::is_directory(status))
        {
            std::cerr << "fioRmdir error: path is not a directory '" << hostPath << "'" << std::endl;
            setReturnS32(ctx, -1);
            return;
        }

        const bool removed = std::filesystem::remove(hostPath, ec);
        if (ec || !removed)
        {
            std::cerr << "fioRmdir error: rmdir failed for '" << hostPath << "'" << std::endl;
            setReturnS32(ctx, -1);
        }
        else
        {
            setReturnS32(ctx, 0); // Success
        }
    }

    void fioGetstat(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t pathAddr = getRegU32(ctx, 4);    // $a0
        uint32_t statBufAddr = getRegU32(ctx, 5); // $a1

        const char *ps2Path = reinterpret_cast<const char *>(getConstMemPtr(rdram, pathAddr));
        uint8_t *ps2StatBuf = getMemPtr(rdram, statBufAddr);

        if (!ps2Path)
        {
            std::cerr << "fioGetstat error: Invalid path addr" << std::endl;
            setReturnS32(ctx, -1);
            return;
        }
        if (!ps2StatBuf)
        {
            std::cerr << "fioGetstat error: Invalid buffer addr" << std::endl;
            setReturnS32(ctx, -1);
            return;
        }

        std::string hostPath = translatePs2Path(ps2Path);
        if (hostPath.empty())
        {
            std::cerr << "fioGetstat error: Bad path translate" << std::endl;
            setReturnS32(ctx, -1);
            return;
        }

        std::error_code ec;
        const auto status = std::filesystem::status(hostPath, ec);
        if (ec || status.type() == std::filesystem::file_type::not_found)
        {
            setReturnS32(ctx, -1);
            return;
        }

        std::memset(ps2StatBuf, 0, 64);

        uint32_t *fields = reinterpret_cast<uint32_t *>(ps2StatBuf);
        const bool isDir = std::filesystem::is_directory(status);

        // Minimal fio_stat mapping: mode, attr, size, ... , hisize.
        fields[0] = isDir ? 0x4000u : 0x2000u;
        fields[1] = 0;

        uint64_t size = 0;
        if (!isDir)
        {
            size = std::filesystem::file_size(hostPath, ec);
            if (ec)
            {
                setReturnS32(ctx, -1);
                return;
            }
        }

        fields[2] = static_cast<uint32_t>(size & 0xFFFFFFFFu);
        fields[9] = static_cast<uint32_t>(size >> 32);

        setReturnS32(ctx, 0);
    }

    void fioRemove(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t pathAddr = getRegU32(ctx, 4); // $a0
        const char *ps2Path = reinterpret_cast<const char *>(getConstMemPtr(rdram, pathAddr));
        if (!ps2Path)
        {
            std::cerr << "fioRemove error: Invalid path" << std::endl;
            setReturnS32(ctx, -1);
            return;
        }

        std::string hostPath = translatePs2Path(ps2Path);
        if (hostPath.empty())
        {
            std::cerr << "fioRemove error: Path translate fail" << std::endl;
            setReturnS32(ctx, -1);
            return;
        }

        std::error_code ec;
        const auto status = std::filesystem::status(hostPath, ec);
        if (ec || status.type() == std::filesystem::file_type::not_found)
        {
            std::cerr << "fioRemove error: file not found '" << hostPath << "'" << std::endl;
            setReturnS32(ctx, -1);
            return;
        }

        if (std::filesystem::is_directory(status))
        {
            std::cerr << "fioRemove error: path is a directory '" << hostPath << "'" << std::endl;
            setReturnS32(ctx, -1);
            return;
        }

        const bool removed = std::filesystem::remove(hostPath, ec);
        if (ec || !removed)
        {
            std::cerr << "fioRemove error: unlink failed for '" << hostPath << "'" << std::endl;
            setReturnS32(ctx, -1);
        }
        else
        {
            setReturnS32(ctx, 0); // Success
        }
    }

    void GsSetCrt(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int interlaced = getRegU32(ctx, 4); // $a0 - 0=non-interlaced, 1=interlaced
        int videoMode = getRegU32(ctx, 5);  // $a1 - 0=NTSC, 1=PAL, 2=VESA, 3=HiVision
        int frameMode = getRegU32(ctx, 6);  // $a2 - 0=field, 1=frame

        std::cout << "PS2 GsSetCrt: interlaced=" << interlaced
                  << ", videoMode=" << videoMode
                  << ", frameMode=" << frameMode << std::endl;

        setReturnS32(ctx, 0);
    }

    void GsGetIMR(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint64_t imr = 0;
        if (runtime)
        {
            imr = runtime->memory().gs().imr;
        }

        std::cout << "PS2 GsGetIMR: Returning IMR=0x" << std::hex << imr << std::dec << std::endl;

        setReturnU64(ctx, imr); // Return in $v0/$v1
    }

    void GsPutIMR(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint64_t imr = getRegU32(ctx, 4) | ((uint64_t)getRegU32(ctx, 5) << 32); // $a0 = lower 32 bits, $a1 = upper 32 bits
        std::cout << "PS2 GsPutIMR: Setting IMR=0x" << std::hex << imr << std::dec << std::endl;
        if (runtime)
        {
            runtime->memory().gs().imr = imr;
        }

        setReturnS32(ctx, 0);
    }

    void GsSetVideoMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int mode = getRegU32(ctx, 4); // $a0 - video mode (various flags)

        std::cout << "PS2 GsSetVideoMode: mode=0x" << std::hex << mode << std::dec << std::endl;

        setReturnS32(ctx, 0);
    }

    void GetOsdConfigParam(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t paramAddr = getRegU32(ctx, 4); // $a0 - pointer to parameter structure

        if (!getMemPtr(rdram, paramAddr))
        {
            std::cerr << "PS2 GetOsdConfigParam error: Invalid parameter address: 0x"
                      << std::hex << paramAddr << std::dec << std::endl;
            setReturnS32(ctx, -1);
            return;
        }

        uint32_t *param = reinterpret_cast<uint32_t *>(getMemPtr(rdram, paramAddr));

        *param = g_osdConfigParam;

        std::cout << "PS2 GetOsdConfigParam: Retrieved OSD parameters" << std::endl;

        setReturnS32(ctx, 0);
    }

    void SetOsdConfigParam(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t paramAddr = getRegU32(ctx, 4); // $a0 - pointer to parameter structure

        if (!getConstMemPtr(rdram, paramAddr))
        {
            std::cerr << "PS2 SetOsdConfigParam error: Invalid parameter address: 0x"
                      << std::hex << paramAddr << std::dec << std::endl;
            setReturnS32(ctx, -1);
            return;
        }

        const uint32_t *param = reinterpret_cast<const uint32_t *>(getConstMemPtr(rdram, paramAddr));
        g_osdConfigParam = *param;
        std::cout << "PS2 SetOsdConfigParam: Set OSD parameters" << std::endl;

        setReturnS32(ctx, 0);
    }

    void GetRomName(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t bufAddr = getRegU32(ctx, 4); // $a0
        size_t bufSize = getRegU32(ctx, 5);   // $a1
        char *hostBuf = reinterpret_cast<char *>(getMemPtr(rdram, bufAddr));
        const char *romName = "ROMVER 0100";

        if (!hostBuf)
        {
            std::cerr << "GetRomName error: Invalid buffer address" << std::endl;
            setReturnS32(ctx, -1); // Error
            return;
        }
        if (bufSize == 0)
        {
            setReturnS32(ctx, 0);
            return;
        }

        strncpy(hostBuf, romName, bufSize - 1);
        hostBuf[bufSize - 1] = '\0';

        // returns the length of the string (excluding null?) or error
        setReturnS32(ctx, (int32_t)strlen(hostBuf));
    }

    void SifLoadElfPart(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t pathAddr = getRegU32(ctx, 4); // $a0 - pointer to ELF path

        const char *elfPath = reinterpret_cast<const char *>(getConstMemPtr(rdram, pathAddr));

        std::cout << "PS2 SifLoadElfPart: Would load ELF from " << elfPath << std::endl;
        setReturnS32(ctx, 1); // dummy return value for success
    }

    void sceSifLoadModule(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t moduePath = getRegU32(ctx, 4); // $a0 - pointer to module path

        // Extract path
        const char *modulePath = reinterpret_cast<const char *>(getConstMemPtr(rdram, moduePath));

        std::cout << "PS2 SifLoadModule: Would load module from " << moduePath << std::endl;

        setReturnS32(ctx, 1);
    }

    void TODO(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t syscall_num = getRegU32(ctx, 3); // Syscall number usually in $v1 ($r3) for SYSCALL instr
        uint32_t caller_ra = getRegU32(ctx, 31);  // $ra

        std::cerr << "Warning: Unimplemented PS2 syscall called. PC=0x" << std::hex << ctx->pc
            << ", RA=0x" << caller_ra
            << ", Syscall # (from $v1)=0x" << syscall_num << std::dec << std::endl;

        std::cerr << "  Args: $a0=0x" << std::hex << getRegU32(ctx, 4)
            << ", $a1=0x" << getRegU32(ctx, 5)
            << ", $a2=0x" << getRegU32(ctx, 6)
            << ", $a3=0x" << getRegU32(ctx, 7) << std::dec << std::endl;

        // Common syscalls:
        // 0x04: Exit
        // 0x06: LoadExecPS2
        // 0x07: ExecPS2
        if (syscall_num == 0x04)
        {
            std::cerr << "  -> Syscall is Exit(), calling ExitThread stub." << std::endl;
            ExitThread(rdram, ctx, runtime);
            return;
        }

        // Return generic error for unimplemented ones
        setReturnS32(ctx, -1); // Return -ENOSYS or similar? Use -1 for simplicity.
    }

    // 0x3C SetupThread: returns stack pointer (stack + stack_size)
    // args: $a0 = stack base, $a1 = stack size, $a2 = gp, $a3 = entry point
    void SetupThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t stackBase = getRegU32(ctx, 4);
        uint32_t stackSize = getRegU32(ctx, 5);
        uint32_t sp = stackBase + stackSize;
        setReturnS32(ctx, sp);
    }

    // 0x5A QueryBootMode (stub): return 0 for now
    void QueryBootMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    // 0x5B GetThreadTLS (stub): return 0
    void GetThreadTLS(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    // 0x74 RegisterExitHandler (stub): return 0
    void RegisterExitHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }
}
