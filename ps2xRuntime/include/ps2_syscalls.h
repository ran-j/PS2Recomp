#ifndef PS2_SYSCALLS_H
#define PS2_SYSCALLS_H

#include "ps2_runtime.h"
#include <mutex>
#include <atomic>

// Number of active host threads spawned for PS2 thread emulation
extern std::atomic<int> g_activeThreads;

static std::mutex g_sys_fd_mutex;

#define PS2_FIO_O_RDONLY 0x0001
#define PS2_FIO_O_WRONLY 0x0002
#define PS2_FIO_O_RDWR 0x0003
#define PS2_FIO_O_NBLOCK 0x0010
#define PS2_FIO_O_APPEND 0x0100
#define PS2_FIO_O_CREAT 0x0200
#define PS2_FIO_O_TRUNC 0x0400
#define PS2_FIO_O_EXCL 0x0800
#define PS2_FIO_O_NOWAIT 0x8000

#define PS2_SEEK_SET 0
#define PS2_SEEK_CUR 1
#define PS2_SEEK_END 2

namespace ps2_syscalls
{
    void FlushCache(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void ResetEE(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void SetMemoryMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);

    void CreateThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void DeleteThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void StartThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void ExitThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void ExitDeleteThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void TerminateThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void SuspendThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void ResumeThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void GetThreadId(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void ReferThreadStatus(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void SleepThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void WakeupThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void iWakeupThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void ChangeThreadPriority(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void RotateThreadReadyQueue(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void ReleaseWaitThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void iReleaseWaitThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);

    void CreateSema(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void DeleteSema(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void SignalSema(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void iSignalSema(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void WaitSema(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void PollSema(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void iPollSema(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void ReferSemaStatus(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void iReferSemaStatus(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);

    void CreateEventFlag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void DeleteEventFlag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void SetEventFlag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void iSetEventFlag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void ClearEventFlag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void iClearEventFlag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void WaitEventFlag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void PollEventFlag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void iPollEventFlag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void ReferEventFlagStatus(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void iReferEventFlagStatus(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);

    void SetAlarm(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void iSetAlarm(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void CancelAlarm(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void iCancelAlarm(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);

    void EnableIntc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void DisableIntc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void EnableDmac(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void DisableDmac(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);

    void SifStopModule(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void SifLoadModule(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void SifInitRpc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void SifBindRpc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void SifCallRpc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void SifRegisterRpc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void SifCheckStatRpc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void SifSetRpcQueue(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void SifRemoveRpcQueue(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void SifRemoveRpc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceSifCallRpc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceSifSendCmd(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void _sceRpcGetPacket(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);

    void fioOpen(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void fioClose(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void fioRead(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void fioWrite(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void fioLseek(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void fioMkdir(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void fioChdir(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void fioRmdir(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void fioGetstat(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void fioRemove(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);

    void GsSetCrt(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void GsGetIMR(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void GsPutIMR(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void GsSetVideoMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);

    void GetOsdConfigParam(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void SetOsdConfigParam(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void GetRomName(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void SifLoadElfPart(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceSifLoadModule(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);

    void TODO(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
}

#endif // PS2_SYSCALLS_H
