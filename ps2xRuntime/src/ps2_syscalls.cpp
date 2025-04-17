
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

std::unordered_map<int, FILE*> g_fileDescriptors;
int g_nextFd = 3; // Start after stdin, stdout, stderr

int allocatePs2Fd(FILE* file)
{
    if (!file)
        return -1;
    int fd = g_nextFd++;
    g_fileDescriptors[fd] = file;
    return fd;
}

FILE* getHostFile(int ps2Fd)
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
    // Add other mappings (mc0:, hdd0:, etc.) as needed
    std::cerr << "Warning: Unsupported PS2 path prefix: " << pathStr << std::endl;
    return "";
}

#include "ps2_syscalls.h"

namespace ps2_syscalls
{
    void FlushCache(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void ResetEE(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void SetMemoryMode(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void CreateThread(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void DeleteThread(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void StartThread(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void ExitThread(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void ExitDeleteThread(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void TerminateThread(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void SuspendThread(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void ResumeThread(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void GetThreadId(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void ReferThreadStatus(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void SleepThread(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void WakeupThread(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void iWakeupThread(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void ChangeThreadPriority(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void RotateThreadReadyQueue(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void ReleaseWaitThread(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void iReleaseWaitThread(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void CreateSema(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void DeleteSema(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void SignalSema(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void iSignalSema(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void WaitSema(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void PollSema(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void iPollSema(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void ReferSemaStatus(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void iReferSemaStatus(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void CreateEventFlag(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void DeleteEventFlag(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void SetEventFlag(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void iSetEventFlag(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void ClearEventFlag(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void iClearEventFlag(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void WaitEventFlag(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void PollEventFlag(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void iPollEventFlag(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void ReferEventFlagStatus(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void iReferEventFlagStatus(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void SetAlarm(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void iSetAlarm(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void CancelAlarm(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void iCancelAlarm(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void EnableIntc(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void DisableIntc(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void EnableDmac(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void DisableDmac(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void SifStopModule(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void SifLoadModule(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void SifInitRpc(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void SifBindRpc(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void SifCallRpc(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void SifRegisterRpc(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void SifCheckStatRpc(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void SifSetRpcQueue(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void SifRemoveRpcQueue(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void SifRemoveRpc(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void fioOpen(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void fioClose(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void fioRead(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void fioWrite(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void fioLseek(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void fioMkdir(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void fioChdir(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void fioRmdir(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void fioGetstat(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void fioRemove(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void GsSetCrt(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void GsGetIMR(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void GsPutIMR(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void GsSetVideoMode(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void GetOsdConfigParam(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void SetOsdConfigParam(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void GetRomName(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void SifLoadElfPart(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void sceSifLoadModule(uint8_t *rdram, R5900Context *ctx)
    {
        // TODO
    }

    void TODO(uint8_t *rdram, R5900Context *ctx)
    {
        uint32_t syscall_num = getRegU32(ctx, 2); // $v0 often holds syscall num before call
        std::cerr << "Warning: Unimplemented syscall called. PC=0x" << std::hex << ctx->pc
                  << " Syscall # (approx): 0x" << syscall_num << std::dec << std::endl;
        setReturnS32(ctx, -1); // Return error
    }

    // Helper functions
    void RenderFrame(uint8_t *rdram)
    {
        // TODO
    }
}