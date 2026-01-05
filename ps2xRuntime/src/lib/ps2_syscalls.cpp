
#include "ps2_syscalls.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "ps2_stubs.h"
#include <iostream>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <unordered_map>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <filesystem>

std::unordered_map<int, FILE *> g_fileDescriptors;
int g_nextFd = 3; // Start after stdin, stdout, stderr

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
    int count = 0;
    int maxCount = 0;
    std::mutex m;
    std::condition_variable cv;
};

static std::unordered_map<int, ThreadInfo> g_threads;
static int g_nextThreadId = 2; // Reserve 1 for the main thread
static thread_local int g_currentThreadId = 1;

static std::unordered_map<int, std::shared_ptr<SemaInfo>> g_semas;
static int g_nextSemaId = 1;
std::atomic<int> g_activeThreads{0};
std::atomic<int> g_schedulerSemaId{0}; // best guess of the game's scheduler semaphore id

int allocatePs2Fd(FILE *file)
{
    if (!file)
        return -1;
    int fd = g_nextFd++;
    g_fileDescriptors[fd] = file;
    return fd;
}

FILE *getHostFile(int ps2Fd)
{
    auto it = g_fileDescriptors.find(ps2Fd);
    if (it != g_fileDescriptors.end())
    {
        return it->second;
    }
    return nullptr;
}

void releasePs2Fd(int ps2Fd)
{
    g_fileDescriptors.erase(ps2Fd);
}

const char *translateFioMode(int ps2Flags)
{
    bool read = (ps2Flags & PS2_FIO_O_RDONLY) || (ps2Flags & PS2_FIO_O_RDWR);
    bool write = (ps2Flags & PS2_FIO_O_WRONLY) || (ps2Flags & PS2_FIO_O_RDWR);
    bool append = (ps2Flags & PS2_FIO_O_APPEND);
    bool create = (ps2Flags & PS2_FIO_O_CREAT);
    bool truncate = (ps2Flags & PS2_FIO_O_TRUNC);

    if (read && write)
    {
        if (create && truncate)
            return "w+b";
        if (create)
            return "a+b";
        return "r+b";
    }
    else if (write)
    {
        if (append)
            return "ab";
        if (create && truncate)
            return "wb";
        if (create)
            return "wx";
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
    std::string pathStr(ps2Path);
    if (pathStr.rfind("host0:", 0) == 0)
    {
        // Map host0: to ./host_fs/ relative to executable
        std::filesystem::path hostBasePath = std::filesystem::current_path() / "host_fs";
        std::filesystem::create_directories(hostBasePath); // Ensure it exists
        return (hostBasePath / pathStr.substr(6)).string();
    }
    else if (pathStr.rfind("cdrom0:", 0) == 0)
    {
        // Map cdrom0: to ./cd_fs/ relative to executable (for example)
        std::filesystem::path cdBasePath = std::filesystem::current_path() / "cd_fs";
        std::filesystem::create_directories(cdBasePath); // Ensure it exists
        return (cdBasePath / pathStr.substr(7)).string();
    }
    std::cerr << "Warning: Unsupported PS2 path prefix: " << pathStr << std::endl;
    return "";
}

#include "ps2_syscalls.h"

namespace ps2_syscalls
{
    namespace
    {
        constexpr uint32_t kCmdqWakeupAddr = 0x10cb00;
        constexpr uint32_t kCmdqRotateAddr = 0x10cb98;
        constexpr uint32_t kCmdqSuspendAddr = 0x10cc34;

        bool isSchedulerReturn(uint32_t ra)
        {
            if (g_currentThreadId == 2)
            {
                return true;
            }
            return ra >= 0x10c920 && ra <= 0x10ca00;
        }

        bool enqueueSchedulerCmd(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime,
                                 uint32_t funcAddr, uint32_t arg, const char *name)
        {
            if (!runtime || !runtime->hasFunction(funcAddr))
            {
                static int missCount = 0;
                if (missCount < 4)
                {
                    std::cout << "[" << name << "] missing cmdq function @0x"
                              << std::hex << funcAddr << std::dec << std::endl;
                    ++missCount;
                }
                return false;
            }

            uint32_t sp = GPR_U32(ctx, 29);
            if (sp < 32)
            {
                return false;
            }
            uint32_t frame = sp - 32;
            WRITE64(frame + 0, GPR_U64(ctx, 16));
            WRITE64(frame + 16, GPR_U64(ctx, 31));

            uint32_t savedPc = ctx->pc;
            SET_GPR_U32(ctx, 29, frame);
            SET_GPR_U32(ctx, 16, arg);

            auto func = runtime->lookupFunction(funcAddr);
            func(rdram, ctx, runtime);

            ctx->pc = savedPc;
            if (GPR_U32(ctx, 29) != sp)
            {
                SET_GPR_U32(ctx, 29, sp);
                SET_GPR_U64(ctx, 16, READ64(frame + 0));
                SET_GPR_U64(ctx, 31, READ64(frame + 16));
            }
            return true;
        }
    } // namespace

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

        int id = g_nextThreadId++;
        g_threads[id] = info;

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
        g_threads.erase(tid);
        setReturnS32(ctx, 0);
    }

    void StartThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int tid = static_cast<int>(getRegU32(ctx, 4)); // $a0 = thread id
        uint32_t arg = getRegU32(ctx, 5);              // $a1 = user arg

        auto it = g_threads.find(tid);
        if (it == g_threads.end())
        {
            std::cerr << "StartThread error: unknown thread id " << tid << std::endl;
            setReturnS32(ctx, -1);
            return;
        }

        ThreadInfo &info = it->second;
        if (info.started)
        {
            setReturnS32(ctx, 0);
            return;
        }

        info.started = true;
        info.arg = arg;

        if (!runtime->hasFunction(info.entry))
        {
            std::cerr << "[StartThread] entry 0x" << std::hex << info.entry << std::dec << " is not registered" << std::endl;
            setReturnS32(ctx, -1);
            return;
        }

        // Spawn a host thread to simulate PS2 thread execution.
        g_activeThreads.fetch_add(1, std::memory_order_relaxed);
        std::thread([=]() mutable
                    {
            R5900Context threadCtxCopy = *ctx; // Copy current CPU state to simulate a new thread context
            R5900Context *threadCtx = &threadCtxCopy;

            if (info.stack && info.stackSize)
            {
                SET_GPR_U32(threadCtx, 29, info.stack + info.stackSize); // SP at top of stack
            }
            if (info.gp)
            {
                SET_GPR_U32(threadCtx, 28, info.gp);
            }

            SET_GPR_U32(threadCtx, 4, info.arg);
            threadCtx->pc = info.entry;

            PS2Runtime::RecompiledFunction func = runtime->lookupFunction(info.entry);
            g_currentThreadId = tid;

            std::cout << "[StartThread] id=" << tid
                      << " entry=0x" << std::hex << info.entry
                      << " sp=0x" << GPR_U32(threadCtx, 29)
                      << " gp=0x" << GPR_U32(threadCtx, 28)
                      << " arg=0x" << info.arg << std::dec << std::endl;

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

        // for now kick sema 1 if it was created empty to let render thread proceed.
        ps2_syscalls::SignalSema(rdram, ctx, runtime);

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
        g_threads.erase(tid);
        setReturnS32(ctx, 0);
    }

    void TerminateThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int tid = static_cast<int>(getRegU32(ctx, 4));
        g_threads.erase(tid);
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
        if (isSchedulerReturn(GPR_U32(ctx, 31)))
        {
            setReturnS32(ctx, 0);
            return;
        }
        if (enqueueSchedulerCmd(rdram, ctx, runtime, kCmdqSuspendAddr, static_cast<uint32_t>(tid), "SuspendThread"))
        {
            return;
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
        if (isSchedulerReturn(GPR_U32(ctx, 31)))
        {
            setReturnS32(ctx, 0);
            return;
        }
        if (enqueueSchedulerCmd(rdram, ctx, runtime, kCmdqWakeupAddr, static_cast<uint32_t>(tid), "WakeupThread"))
        {
            return;
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
        if (enqueueSchedulerCmd(rdram, ctx, runtime, kCmdqWakeupAddr, static_cast<uint32_t>(tid), "iWakeupThread"))
        {
            return;
        }
        setReturnS32(ctx, 0);
    }

    void ChangeThreadPriority(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int tid = static_cast<int>(getRegU32(ctx, 4));
        int newPrio = static_cast<int>(getRegU32(ctx, 5));
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
        if (isSchedulerReturn(GPR_U32(ctx, 31)))
        {
            setReturnS32(ctx, 0);
            return;
        }
        if (enqueueSchedulerCmd(rdram, ctx, runtime, kCmdqRotateAddr, static_cast<uint32_t>(prio), "RotateThreadReadyQueue"))
        {
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
        int init = 0;
        int max = 1;
        if (param)
        {
            // sceSemaParam layout commonly: attr(0), option(1), initCount(2), maxCount(3)
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

        int id = g_nextSemaId++;
        auto info = std::make_shared<SemaInfo>();
        info->count = init;
        info->maxCount = max;
        g_semas.emplace(id, info);
        std::cout << "[CreateSema] id=" << id << " init=" << init << " max=" << max << std::endl;
        setReturnS32(ctx, id);
    }

    void DeleteSema(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int sid = static_cast<int>(getRegU32(ctx, 4));
        g_semas.erase(sid);
        setReturnS32(ctx, 0);
    }

    void SignalSema(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int sid = static_cast<int>(getRegU32(ctx, 4));
        auto it = g_semas.find(sid);
        if (it == g_semas.end() && sid > 1000)
        {
            int fallback = g_schedulerSemaId.load(std::memory_order_relaxed);
            if (fallback > 0)
            {
                sid = fallback;
                it = g_semas.find(sid);
            }
        }
        if (it != g_semas.end())
        {
            auto sema = it->second;
            std::lock_guard<std::mutex> lock(sema->m);
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
        auto it = g_semas.find(sid);
        if (it == g_semas.end() && sid > 1000)
        {
            int fallback = g_schedulerSemaId.load(std::memory_order_relaxed);
            if (fallback > 0)
            {
                sid = fallback;
                it = g_semas.find(sid);
            }
        }
        if (it != g_semas.end())
        {
            auto sema = it->second;
            std::unique_lock<std::mutex> lock(sema->m);
            uint32_t schedSid = *reinterpret_cast<uint32_t *>(runtime->memory().getRDRAM() + (0x363a10 & PS2_RAM_MASK));
            static int globalLog = 0;
            if (globalLog < 5)
            {
                std::cout << "[WaitSema] sid=" << sid << " count=" << sema->count << std::endl;
                ++globalLog;
            }
            if (sid == schedSid)
            {
                static thread_local int queueLog = 0;
                if (queueLog < 3)
                {
                    uint32_t off = 0x363a10 & PS2_RAM_MASK;
                    uint32_t *p = reinterpret_cast<uint32_t *>(runtime->memory().getRDRAM() + off);
                    std::cout << "[WaitSema] sid=" << sid << " count=" << sema->count
                              << " queue[0..3]=" << std::hex << p[0] << " " << p[1] << " " << p[2] << " " << p[3] << std::dec << std::endl;

                    // Dump data
                    uint32_t argPtr = getRegU32(ctx, 17);
                    uint32_t idx = 0;
                    if (argPtr)
                    {
                        idx = READ32(ADD32(argPtr, 0)) & 0x1FF;
                        uint32_t cmdBase = (argPtr + 8) & PS2_RAM_MASK;
                        uint32_t tidBase = (argPtr + 9) & PS2_RAM_MASK;
                        uint32_t cmd = runtime->memory().getRDRAM()[(cmdBase + (idx << 1)) & PS2_RAM_MASK];
                        uint32_t tid = runtime->memory().getRDRAM()[(tidBase + (idx << 1)) & PS2_RAM_MASK];
                        std::cout << "[WaitSema] sched idx=" << idx << " cmd=" << cmd << " tid=" << tid << std::endl;
                    }
                    ++queueLog;
                }
            }
            if (sema->count == 0)
            {
                static thread_local int logCount = 0;
                if (logCount < 3)
                {
                    std::cout << "[WaitSema] sid=" << sid << " blocking until signaled" << std::endl;
                    ++logCount;
                }
                sema->cv.wait(lock, [&]()
                              { return sema->count > 0; });
            }
            if (sema->count > 0)
            {
                sema->count--;
            }
            if (sid < 32)
            {
                g_schedulerSemaId.store(sid, std::memory_order_relaxed);
            }
        }
        setReturnS32(ctx, 0);
    }

    void PollSema(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int sid = static_cast<int>(getRegU32(ctx, 4));
        auto it = g_semas.find(sid);
        if (it != g_semas.end())
        {
            auto sema = it->second;
            std::lock_guard<std::mutex> lock(sema->m);
            if (sema->count > 0)
            {
                sema->count--;
                setReturnS32(ctx, 0);
                return;
            }
        }
        setReturnS32(ctx, 0);
    }

    void iPollSema(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        PollSema(rdram, ctx, runtime);
    }

    void ReferSemaStatus(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void iReferSemaStatus(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        ReferSemaStatus(rdram, ctx, runtime);
    }

    void CreateEventFlag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        // TODO
    }

    void DeleteEventFlag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        // TODO
    }

    void SetEventFlag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        // TODO
    }

    void iSetEventFlag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        // TODO
    }

    void ClearEventFlag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        // TODO
    }

    void iClearEventFlag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        // TODO
    }

    void WaitEventFlag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        // TODO
    }

    void PollEventFlag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        // TODO
    }

    void iPollEventFlag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        // TODO
    }

    void ReferEventFlagStatus(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        // TODO
    }

    void iReferEventFlagStatus(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        // TODO
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
        if (queuePtr == 0)
        {
            // Fall back to a safe, writable address in RDRAM.
            queuePtr = 0x00370000;
        }
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

        const char *mode = translateFioMode(flags);
        std::cout << "fioOpen: '" << hostPath << "' flags=0x" << std::hex << flags << std::dec << " mode='" << mode << "'" << std::endl;

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
        std::cout << "fioClose: fd=" << ps2Fd << std::endl;

        FILE *fp = getHostFile(ps2Fd);
        if (!fp)
        {
            std::cerr << "fioClose warning: Invalid PS2 file descriptor " << ps2Fd << std::endl;
            setReturnS32(ctx, -1); // e.g., -EBADF
            return;
        }

        int ret = ::fclose(fp);
        releasePs2Fd(ps2Fd);

        // returns 0 on success, -1 on error
        setReturnS32(ctx, ret == 0 ? 0 : -1);
    }

    void fioRead(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int ps2Fd = (int)getRegU32(ctx, 4);   // $a0
        uint32_t bufAddr = getRegU32(ctx, 5); // $a1
        size_t size = getRegU32(ctx, 6);      // $a2

        uint8_t *hostBuf = getMemPtr(rdram, bufAddr);
        FILE *fp = getHostFile(ps2Fd);

        if (!hostBuf)
        {
            std::cerr << "fioRead error: Invalid buffer address for fd " << ps2Fd << std::endl;
            setReturnS32(ctx, -1); // -EFAULT
            return;
        }
        if (!fp)
        {
            std::cerr << "fioRead error: Invalid file descriptor " << ps2Fd << std::endl;
            setReturnS32(ctx, -1); // -EBADF
            return;
        }
        if (size == 0)
        {
            setReturnS32(ctx, 0); // Read 0 bytes
            return;
        }

        size_t bytesRead = 0;
        {
            std::lock_guard<std::mutex> lock(g_sys_fd_mutex);
            bytesRead = fread(hostBuf, 1, size, fp);
        }

        if (bytesRead < size && ferror(fp))
        {
            std::cerr << "fioRead error: fread failed for fd " << ps2Fd << ": " << strerror(errno) << std::endl;
            clearerr(fp);
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
        FILE *fp = getHostFile(ps2Fd);

        if (!hostBuf)
        {
            std::cerr << "fioWrite error: Invalid buffer address for fd " << ps2Fd << std::endl;
            setReturnS32(ctx, -1); // -EFAULT
            return;
        }
        if (!fp)
        {
            std::cerr << "fioWrite error: Invalid file descriptor " << ps2Fd << std::endl;
            setReturnS32(ctx, -1); // -EBADF
            return;
        }
        if (size == 0)
        {
            setReturnS32(ctx, 0); // Wrote 0 bytes
            return;
        }

        size_t bytesWritten = ::fwrite(hostBuf, 1, size, fp);

        if (bytesWritten < size)
        {
            if (ferror(fp))
            {
                std::cerr << "fioWrite error: fwrite failed for fd " << ps2Fd << ": " << strerror(errno) << std::endl;
                clearerr(fp);
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
        int32_t offset = getRegU32(ctx, 5);  // $a1 (PS2 seems to use 32-bit offset here commonly)
        int whence = (int)getRegU32(ctx, 6); // $a2 (PS2 FIO_SEEK constants)

        FILE *fp = getHostFile(ps2Fd);
        if (!fp)
        {
            std::cerr << "fioLseek error: Invalid file descriptor " << ps2Fd << std::endl;
            setReturnS32(ctx, -1); // -EBADF
            return;
        }

        int hostWhence;
        switch (whence)
        {
        case PS2_FIO_SEEK_SET:
            hostWhence = SEEK_SET;
            break;
        case PS2_FIO_SEEK_CUR:
            hostWhence = SEEK_CUR;
            break;
        case PS2_FIO_SEEK_END:
            hostWhence = SEEK_END;
            break;
        default:
            std::cerr << "fioLseek error: Invalid whence value " << whence << " for fd " << ps2Fd << std::endl;
            setReturnS32(ctx, -1); // -EINVAL
            return;
        }

        if (::fseek(fp, static_cast<long>(offset), hostWhence) != 0)
        {
            std::cerr << "fioLseek error: fseek failed for fd " << ps2Fd << ": " << strerror(errno) << std::endl;
            setReturnS32(ctx, -1); // Return error code
            return;
        }

        long newPos = ::ftell(fp);
        if (newPos < 0)
        {
            std::cerr << "fioLseek error: ftell failed after fseek for fd " << ps2Fd << ": " << strerror(errno) << std::endl;
            setReturnS32(ctx, -1);
        }
        else
        {
            // maybe we dont need this check. if position fits in 32 bits
            if (newPos > 0xFFFFFFFFL)
            {
                std::cerr << "fioLseek warning: New position exceeds 32-bit for fd " << ps2Fd << std::endl;
                setReturnS32(ctx, -1);
            }
            else
            {
                setReturnS32(ctx, (int32_t)newPos);
            }
        }
    }

    void fioMkdir(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        // TODO maybe we dont need this.
        uint32_t pathAddr = getRegU32(ctx, 4); // $a0
        // int mode = (int)getRegU32(ctx, 5);

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

#ifdef _WIN32
        int ret = -1;
#else
        int ret = ::mkdir(hostPath.c_str(), 0775);
#endif

        if (ret != 0)
        {
            std::cerr << "fioMkdir error: mkdir failed for '" << hostPath << "': " << strerror(errno) << std::endl;
            setReturnS32(ctx, -1); // errno
        }
        else
        {
            setReturnS32(ctx, 0); // Success
        }
    }

    void fioChdir(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        // TODO maybe we dont need this as well.
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

        std::cerr << "fioChdir: Attempting host chdir to '" << hostPath << "' (Stub - Check side effects)" << std::endl;

#ifdef _WIN32
        int ret = -1;
#else
        int ret = ::chdir(hostPath.c_str());
#endif

        if (ret != 0)
        {
            std::cerr << "fioChdir error: chdir failed for '" << hostPath << "': " << strerror(errno) << std::endl;
            setReturnS32(ctx, -1);
        }
        else
        {
            setReturnS32(ctx, 0); // Success
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

#ifdef _WIN32
        int ret = -1;
#else
        int ret = ::rmdir(hostPath.c_str());
#endif

        if (ret != 0)
        {
            std::cerr << "fioRmdir error: rmdir failed for '" << hostPath << "': " << strerror(errno) << std::endl;
            setReturnS32(ctx, -1);
        }
        else
        {
            setReturnS32(ctx, 0); // Success
        }
    }

    void fioGetstat(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        // we wont implement this for now.
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

        setReturnS32(ctx, -1);
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

#ifdef _WIN32
        int ret = -1;
#else
        int ret = ::unlink(hostPath.c_str());
#endif

        if (ret != 0)
        {
            std::cerr << "fioRemove error: unlink failed for '" << hostPath << "': " << strerror(errno) << std::endl;
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
    }

    void GsGetIMR(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        // TODO return IMR value from the Gs hardware this is just a stub.
        // The IMR (Interrupt Mask Register) is a 64-bit register that controls which interrupts are enabled.
        uint64_t imr = 0x0000000000000000ULL;

        std::cout << "PS2 GsGetIMR: Returning IMR=0x" << std::hex << imr << std::dec << std::endl;

        setReturnU64(ctx, imr); // Return in $v0/$v1
    }

    void GsPutIMR(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint64_t imr = getRegU32(ctx, 4) | ((uint64_t)getRegU32(ctx, 5) << 32); // $a0 = lower 32 bits, $a1 = upper 32 bits
        std::cout << "PS2 GsPutIMR: Setting IMR=0x" << std::hex << imr << std::dec << std::endl;
        // Do nothing for now.
    }

    void GsSetVideoMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int mode = getRegU32(ctx, 4); // $a0 - video mode (various flags)

        std::cout << "PS2 GsSetVideoMode: mode=0x" << std::hex << mode << std::dec << std::endl;

        // Do nothing for now.
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

        // Default to English language, USA region
        *param = 0x00000000;

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

        // TODO save user preferences
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

        // Many recompiled libc helper stubs land here without setting $v1.
        if (syscall_num == 0)
        {
            static int logCount = 0;
            if (logCount++ < 5)
            {
                std::cout << "[stub] Treating syscall #0 as no-op success PC=0x" << std::hex << ctx->pc
                          << " RA=0x" << caller_ra << std::dec << std::endl;
            }
            setReturnS32(ctx, 0);
            return;
        }

        if (syscall_num > 0x1FFF)
        {
            static int logCount = 0;
            if (logCount++ < 5)
            {
                std::cout << "[stub] Suspicious syscall id 0x" << std::hex << syscall_num
                          << " treated as success PC=0x" << ctx->pc
                          << " RA=0x" << caller_ra << std::dec << std::endl;
            }
            setReturnS32(ctx, 0);
            return;
        }

        if (syscall_num == 0xff)
        {
            // InitGdSystemEx: graphics driver bootstrap. Treat as success.
            static int logCount = 0;
            if (logCount++ < 3)
            {
                std::cout << "[stub] InitGdSystemEx (syscall 0xff) PC=0x" << std::hex << ctx->pc
                          << " RA=0x" << caller_ra
                          << " args a0=0x" << getRegU32(ctx, 4)
                          << " a1=0x" << getRegU32(ctx, 5)
                          << " a2=0x" << getRegU32(ctx, 6)
                          << " a3=0x" << getRegU32(ctx, 7) << std::dec << std::endl;
            }
            // For now Seed GS display registers to a sane default 640x448 @ PSMCT32 so the blitter has something to show.
            PS2Memory &mem = runtime->memory();
            constexpr uint32_t fbWidth = 640;
            constexpr uint32_t fbHeight = 448;
            constexpr uint32_t defaultFbAddr = 0x00100000;
            uint64_t dispfb = 0;
            uint32_t fbp = (defaultFbAddr & 0x1FFFFFFF) / 2048;
            uint32_t fbw = fbWidth / 64;
            uint32_t psm = 0; // PSMCT32
            dispfb |= (fbp & 0x1FF);
            dispfb |= (static_cast<uint64_t>(fbw & 0x3F) << 10);
            dispfb |= (static_cast<uint64_t>(psm & 0x1F) << 16);
            mem.gs().dispfb1 = dispfb;
            uint64_t display = 0;
            uint64_t dw = fbWidth - 1;
            uint64_t dh = fbHeight - 1;
            display |= (dw & 0x7FF) << 23;
            display |= (dh & 0x7FF) << 34;
            mem.gs().display1 = display;

            // Fill a visible test pattern only until we see a real GIF copy.
            if (!mem.hasSeenGifCopy())
            {
                uint8_t *vram = mem.getGSVRAM();
                if (vram)
                {
                    size_t base = static_cast<size_t>(fbp) * 2048;
                    size_t bytes = static_cast<size_t>(fbWidth) * fbHeight * 4;
                    if (base + bytes <= PS2_GS_VRAM_SIZE)
                    {
                        for (uint32_t y = 0; y < fbHeight; ++y)
                        {
                            for (uint32_t x = 0; x < fbWidth; ++x)
                            {
                                size_t idx = base + (static_cast<size_t>(y) * fbWidth + x) * 4;
                                vram[idx + 0] = static_cast<uint8_t>(x);     // B
                                vram[idx + 1] = static_cast<uint8_t>(y);     // G
                                vram[idx + 2] = static_cast<uint8_t>(x ^ y); // R
                                vram[idx + 3] = 0xFF;                        // A
                            }
                        }
                        uint32_t sum = 0;
                        for (int i = 0; i < 32; ++i)
                        {
                            sum += vram[base + i];
                        }
                        std::cout << "[stub] InitGdSystemEx filled VRAM sum=0x" << std::hex << sum << std::dec << std::endl;
                    }
                }
            }

            setReturnS32(ctx, 0);
            return;
        }

        if (syscall_num == 0x41)
        {
            // njInitView: initialise a view matrix block; seed with identity so later math has sane defaults.
            static int logCount = 0;
            uint32_t viewPtr = getRegU32(ctx, 4);
            float *dst = reinterpret_cast<float *>(getMemPtr(rdram, viewPtr));

            if (dst)
            {
                const float identity[16] = {
                    1.f, 0.f, 0.f, 0.f,
                    0.f, 1.f, 0.f, 0.f,
                    0.f, 0.f, 1.f, 0.f,
                    0.f, 0.f, 0.f, 1.f};
                std::memcpy(dst, identity, sizeof(identity));
            }

            if (logCount++ < 3)
            {
                std::cout << "[stub] njInitView (syscall 0x41) PC=0x" << std::hex << ctx->pc
                          << " RA=0x" << caller_ra
                          << " viewPtr=0x" << viewPtr
                          << (dst ? "" : " (invalid)") << std::dec << std::endl;
            }

            setReturnS32(ctx, 0);
            return;
        }

        if (syscall_num == 0x06 || syscall_num == 0x14 || syscall_num == 0x20)
        {
            // Treat frequent boot/runtime syscalls as success to avoid tight loops.
            static int logCount = 0;
            if (logCount++ < 5)
            {
                std::cout << "[stub] Syscall 0x" << std::hex << syscall_num
                          << " treated as success PC=0x" << ctx->pc
                          << " RA=0x" << caller_ra << std::dec << std::endl;
            }
            setReturnS32(ctx, 0);
            return;
        }

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
        setReturnS32(ctx, -1);
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
