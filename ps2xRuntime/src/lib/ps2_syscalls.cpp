
#include "ps2_syscalls.h"
#include "ps2_runtime.h"
#include "ps2_stubs.h"
#include <iostream>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <unordered_map>
#include <filesystem>

std::unordered_map<int, FILE *> g_fileDescriptors;
int g_nextFd = 3; // Start after stdin, stdout, stderr

// Semaphore tracking
static int g_nextSemaId = 0; // PS2 kernel starts semaphore IDs at 0
static std::unordered_map<int, int> g_semaphores; // id -> count
static std::unordered_map<int, int> g_pollSemaFailCount; // id -> consecutive fail count
static const int POLL_SEMA_AUTO_SIGNAL_THRESHOLD = 100; // Auto-signal after N failed polls

// Thread tracking
static int g_nextThreadId = 2; // 1 is main thread
static int g_currentThreadId = 1;

// Helper to continue execution at return address after syscall
static inline void continueAtRa(R5900Context* ctx) {
    ctx->pc = M128I_U32(ctx->r[31], 0);
}

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
    void FlushCache(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        std::cout << "Syscall: FlushCache (No-op)" << std::endl;
        // No-op for now
        setReturnS32(ctx, 0); continueAtRa(ctx);
    }

    void ResetEE(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        std::cerr << "Syscall: ResetEE - Halting Execution (Not fully implemented)" << std::endl;
        exit(0); // Should we exit or just halt the execution?
    }

    void SetMemoryMode(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        // Affects memory mapping / TLB behavior.
        // std::cout << "Syscall: SetMemoryMode (No-op)" << std::endl;
        setReturnS32(ctx, 0); // Success
    }

    void CreateThread(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        int threadId = g_nextThreadId++;
        std::cout << "CreateThread: Created thread " << threadId << std::endl;
        setReturnS32(ctx, threadId); continueAtRa(ctx);
    }

    void DeleteThread(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        // TODO
    }

    void StartThread(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        int threadId = getRegU32(ctx, 4);
        std::cout << "StartThread: Starting thread " << threadId << std::endl;
        setReturnS32(ctx, 0); continueAtRa(ctx);
    }

    void ExitThread(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        // TODO
        std::cout << "PS2 ExitThread: Thread is exiting (PC=0x" << std::hex << ctx->pc << std::dec << ")" << std::endl;
    }

    void ExitDeleteThread(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        // TODO
    }

    void TerminateThread(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        // TODO
    }

    void SuspendThread(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        // TODO
    }

    void ResumeThread(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        // TODO
    }

    void GetThreadId(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, g_currentThreadId); continueAtRa(ctx);
    }

    void ReferThreadStatus(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        // TODO
    }

    void SleepThread(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        // TODO
    }

    void WakeupThread(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        // TODO
    }

    void iWakeupThread(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        // TODO
    }

    void ChangeThreadPriority(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        // TODO
    }

    void RotateThreadReadyQueue(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        // TODO
    }

    void ReleaseWaitThread(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        // TODO
    }

    void iReleaseWaitThread(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        // TODO
    }

    void CreateSema(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        // CreateSema(SemaParam *param)
        // SemaParam struct: { u32 attr; u32 option; s32 init_count; s32 max_count; }
        // Returns semaphore ID on success, negative on error
        uint32_t paramAddr = getRegU32(ctx, 4);
        int semaId = g_nextSemaId++;

        // Read initial count from SemaParam (offset 8)
        int32_t initCount = 1; // Default
        if (paramAddr > 0 && paramAddr < 0x02000000) {
            uint32_t physAddr = paramAddr & 0x1FFFFFFF;
            initCount = *reinterpret_cast<int32_t*>(rdram + physAddr + 8);
        }

        g_semaphores[semaId] = initCount;
        std::cout << "CreateSema: Created semaphore " << semaId << " with count=" << initCount << std::endl;
        setReturnS32(ctx, semaId); continueAtRa(ctx);
    }

    void DeleteSema(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        int semaId = getRegU32(ctx, 4);
        g_semaphores.erase(semaId);
        setReturnS32(ctx, 0); continueAtRa(ctx);
    }

    void SignalSema(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        int semaId = getRegU32(ctx, 4);
        if (g_semaphores.find(semaId) != g_semaphores.end()) {
            int oldCount = g_semaphores[semaId];
            g_semaphores[semaId]++;
            std::cout << "  SignalSema(" << semaId << "): " << oldCount << " -> " << g_semaphores[semaId];
            std::cout << " [s0=" << M128I_U32(ctx->r[16], 0) << "]" << std::endl;
        }
        setReturnS32(ctx, 0);
    }

    void iSignalSema(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        int semaId = getRegU32(ctx, 4);
        if (g_semaphores.find(semaId) != g_semaphores.end()) {
            g_semaphores[semaId]++;
        }
        setReturnS32(ctx, 0);
    }

    void WaitSema(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        int semaId = getRegU32(ctx, 4);

        // Always trigger VBlank to update counters
        runtime->triggerVBlank();

        // Clear g_mpeg.oid_1 to escape video loop
// Set D_002623B8 to 1 to break FlushFrames spin loop at 0x15F270        // FlushFrames waits until this memory location is non-zero        // Normally set by VBlank interrupt or DMA completion        *reinterpret_cast<uint32_t*>(rdram + 0x1623B8) = 1;
        *reinterpret_cast<uint32_t*>(rdram + 0x169A00) = 0;

        if (g_semaphores.find(semaId) != g_semaphores.end()) {
            if (g_semaphores[semaId] > 0) {
                g_semaphores[semaId]--;
            } else {
                // Semaphore not available - simulate VBlank signal to unblock
                // This is a workaround since we don't have proper blocking semaphores
                g_semaphores[semaId] = 1;
                g_semaphores[semaId]--;
            }
        }
        setReturnS32(ctx, 0);
    }

    void PollSema(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        int semaId = getRegU32(ctx, 4);
        static int pollCount = 0;
        pollCount++;

        // For IOP-related semaphores (typically 5, 6), always return success
        // since we don't have an IOP to signal them
        // This prevents infinite polling loops waiting for IOP responses
        if (semaId >= 5) {
            // Debug: Print CD status value and actual address from $s3
            uint32_t s3 = M128I_U32(ctx->r[19], 0);
            uint32_t actualAddr = s3 + 0xFFFF942Cu;
            uint32_t cdStatusFixed = *reinterpret_cast<uint32_t*>(rdram + 0x27942C);
            uint32_t cdStatusActual = (actualAddr < 0x02000000) ? *reinterpret_cast<uint32_t*>(rdram + actualAddr) : 0;
            if (pollCount <= 5 || pollCount % 50000 == 0) {
                std::cout << "  -> PollSema(" << semaId << ") poll #" << pollCount
                          << ", $s3=0x" << std::hex << s3 << std::dec
                          << ", actual@0x" << std::hex << actualAddr << "=" << std::dec << cdStatusActual
                          << ", fixed@0x27942C=" << cdStatusFixed << std::endl;
            }
            // Patch BOTH locations
            *reinterpret_cast<uint32_t*>(rdram + 0x27942C) = 6;
            if (actualAddr < 0x02000000) {
                *reinterpret_cast<uint32_t*>(rdram + actualAddr) = 6;
            }
            setReturnS32(ctx, 0); // Always succeed for IOP semaphores
            continueAtRa(ctx);
            return;
        }

        if (g_semaphores.find(semaId) != g_semaphores.end() && g_semaphores[semaId] > 0) {
            g_semaphores[semaId]--;
            g_pollSemaFailCount[semaId] = 0;
            setReturnS32(ctx, 0); continueAtRa(ctx);
        } else {
            // Track consecutive failures
            g_pollSemaFailCount[semaId]++;

            if (g_pollSemaFailCount[semaId] >= POLL_SEMA_AUTO_SIGNAL_THRESHOLD) {
                g_semaphores[semaId] = 1;
                g_semaphores[semaId]--;
                g_pollSemaFailCount[semaId] = 0;
                setReturnS32(ctx, 0); continueAtRa(ctx);
            } else {
                setReturnS32(ctx, -1);
                continueAtRa(ctx);
            }
        }
    }

    void iPollSema(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        // TODO
    }

    void ReferSemaStatus(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        // TODO
    }

    void iReferSemaStatus(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        // TODO
    }

    void CreateEventFlag(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        // TODO
    }

    void DeleteEventFlag(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        // TODO
    }

    void SetEventFlag(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        // TODO
    }

    void iSetEventFlag(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        // TODO
    }

    void ClearEventFlag(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        // TODO
    }

    void iClearEventFlag(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        // TODO
    }

    void WaitEventFlag(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        // TODO
    }

    void PollEventFlag(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        // TODO
    }

    void iPollEventFlag(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        // TODO
    }

    void ReferEventFlagStatus(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        // TODO
    }

    void iReferEventFlagStatus(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        // TODO
    }

    void SetAlarm(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        // TODO
    }

    void iSetAlarm(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        // TODO
    }

    void CancelAlarm(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        // TODO
    }

    void iCancelAlarm(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        // TODO
    }

    void EnableIntc(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 1); // Return previous state (enabled)
    }

    void DisableIntc(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 1); // Return previous state
    }

    void EnableDmac(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 1); continueAtRa(ctx);
    }

    void DisableDmac(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 1); continueAtRa(ctx);
    }

    void SifStopModule(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        // TODO
    }

    void SifLoadModule(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        // TODO
    }

    void SifInitRpc(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        // TODO
    }

    void SifBindRpc(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        // TODO
    }

    void SifCallRpc(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        // TODO
    }

    void SifRegisterRpc(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        // TODO
    }

    void SifCheckStatRpc(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        // TODO
    }

    void SifSetRpcQueue(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        // TODO
    }

    void SifRemoveRpcQueue(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        // TODO
    }

    void SifRemoveRpc(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        // TODO
    }

    void fioOpen(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
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

    void fioClose(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
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

    void fioRead(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
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

    void fioWrite(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
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

    void fioLseek(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
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

    void fioMkdir(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
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

    void fioChdir(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
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

    void fioRmdir(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
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

    void fioGetstat(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
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

    void fioRemove(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
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

    void GsSetCrt(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        int interlaced = getRegU32(ctx, 4); // $a0 - 0=non-interlaced, 1=interlaced
        int videoMode = getRegU32(ctx, 5);  // $a1 - 0=NTSC, 1=PAL, 2=VESA, 3=HiVision
        int frameMode = getRegU32(ctx, 6);  // $a2 - 0=field, 1=frame

        std::cout << "PS2 GsSetCrt: interlaced=" << interlaced
                  << ", videoMode=" << videoMode
                  << ", frameMode=" << frameMode << std::endl;
    }

    void GsGetIMR(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        // TODO return IMR value from the Gs hardware this is just a stub.
        // The IMR (Interrupt Mask Register) is a 64-bit register that controls which interrupts are enabled.
        uint64_t imr = 0x0000000000000000ULL;

        std::cout << "PS2 GsGetIMR: Returning IMR=0x" << std::hex << imr << std::dec << std::endl;

        setReturnU64(ctx, imr); // Return in $v0/$v1
    }

    void GsPutIMR(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        uint64_t imr = getRegU32(ctx, 4) | ((uint64_t)getRegU32(ctx, 5) << 32); // $a0 = lower 32 bits, $a1 = upper 32 bits
        std::cout << "PS2 GsPutIMR: Setting IMR=0x" << std::hex << imr << std::dec << std::endl;
        // Do nothing for now.
    }

    void GsSetVideoMode(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        int mode = getRegU32(ctx, 4); // $a0 - video mode (various flags)

        std::cout << "PS2 GsSetVideoMode: mode=0x" << std::hex << mode << std::dec << std::endl;

        // Do nothing for now.
    }

    void GetOsdConfigParam(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
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

    void SetOsdConfigParam(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
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

    void GetRomName(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
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

    void SifLoadElfPart(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        uint32_t pathAddr = getRegU32(ctx, 4); // $a0 - pointer to ELF path

        const char *elfPath = reinterpret_cast<const char *>(getConstMemPtr(rdram, pathAddr));

        std::cout << "PS2 SifLoadElfPart: Would load ELF from " << elfPath << std::endl;
        setReturnS32(ctx, 1); // dummy return value for success
    }

    void sceSifLoadModule(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
    {
        uint32_t moduePath = getRegU32(ctx, 4); // $a0 - pointer to module path

        // Extract path
        const char *modulePath = reinterpret_cast<const char *>(getConstMemPtr(rdram, moduePath));

        std::cout << "PS2 SifLoadModule: Would load module from " << moduePath << std::endl;

        setReturnS32(ctx, 1);
    }

    void TODO(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime)
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
}
