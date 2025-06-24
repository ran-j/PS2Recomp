#ifndef PS2_SYSCALLS_H
#define PS2_SYSCALLS_H

#include "ps2_runtime.h"
#include <mutex>

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
    void FlushCache(uint8_t *rdram, R5900Context *ctx);
    void ResetEE(uint8_t *rdram, R5900Context *ctx);
    void SetMemoryMode(uint8_t *rdram, R5900Context *ctx);

    void CreateThread(uint8_t *rdram, R5900Context *ctx);
    void DeleteThread(uint8_t *rdram, R5900Context *ctx);
    void StartThread(uint8_t *rdram, R5900Context *ctx);
    void ExitThread(uint8_t *rdram, R5900Context *ctx);
    void ExitDeleteThread(uint8_t *rdram, R5900Context *ctx);
    void TerminateThread(uint8_t *rdram, R5900Context *ctx);
    void SuspendThread(uint8_t *rdram, R5900Context *ctx);
    void ResumeThread(uint8_t *rdram, R5900Context *ctx);
    void GetThreadId(uint8_t *rdram, R5900Context *ctx);
    void ReferThreadStatus(uint8_t *rdram, R5900Context *ctx);
    void SleepThread(uint8_t *rdram, R5900Context *ctx);
    void WakeupThread(uint8_t *rdram, R5900Context *ctx);
    void iWakeupThread(uint8_t *rdram, R5900Context *ctx);
    void ChangeThreadPriority(uint8_t *rdram, R5900Context *ctx);
    void RotateThreadReadyQueue(uint8_t *rdram, R5900Context *ctx);
    void ReleaseWaitThread(uint8_t *rdram, R5900Context *ctx);
    void iReleaseWaitThread(uint8_t *rdram, R5900Context *ctx);

    void CreateSema(uint8_t *rdram, R5900Context *ctx);
    void DeleteSema(uint8_t *rdram, R5900Context *ctx);
    void SignalSema(uint8_t *rdram, R5900Context *ctx);
    void iSignalSema(uint8_t *rdram, R5900Context *ctx);
    void WaitSema(uint8_t *rdram, R5900Context *ctx);
    void PollSema(uint8_t *rdram, R5900Context *ctx);
    void iPollSema(uint8_t *rdram, R5900Context *ctx);
    void ReferSemaStatus(uint8_t *rdram, R5900Context *ctx);
    void iReferSemaStatus(uint8_t *rdram, R5900Context *ctx);

    void CreateEventFlag(uint8_t *rdram, R5900Context *ctx);
    void DeleteEventFlag(uint8_t *rdram, R5900Context *ctx);
    void SetEventFlag(uint8_t *rdram, R5900Context *ctx);
    void iSetEventFlag(uint8_t *rdram, R5900Context *ctx);
    void ClearEventFlag(uint8_t *rdram, R5900Context *ctx);
    void iClearEventFlag(uint8_t *rdram, R5900Context *ctx);
    void WaitEventFlag(uint8_t *rdram, R5900Context *ctx);
    void PollEventFlag(uint8_t *rdram, R5900Context *ctx);
    void iPollEventFlag(uint8_t *rdram, R5900Context *ctx);
    void ReferEventFlagStatus(uint8_t *rdram, R5900Context *ctx);
    void iReferEventFlagStatus(uint8_t *rdram, R5900Context *ctx);

    void SetAlarm(uint8_t *rdram, R5900Context *ctx);
    void iSetAlarm(uint8_t *rdram, R5900Context *ctx);
    void CancelAlarm(uint8_t *rdram, R5900Context *ctx);
    void iCancelAlarm(uint8_t *rdram, R5900Context *ctx);

    void EnableIntc(uint8_t *rdram, R5900Context *ctx);
    void DisableIntc(uint8_t *rdram, R5900Context *ctx);
    void EnableDmac(uint8_t *rdram, R5900Context *ctx);
    void DisableDmac(uint8_t *rdram, R5900Context *ctx);

    void SifStopModule(uint8_t *rdram, R5900Context *ctx);
    void SifLoadModule(uint8_t *rdram, R5900Context *ctx);
    void SifInitRpc(uint8_t *rdram, R5900Context *ctx);
    void SifBindRpc(uint8_t *rdram, R5900Context *ctx);
    void SifCallRpc(uint8_t *rdram, R5900Context *ctx);
    void SifRegisterRpc(uint8_t *rdram, R5900Context *ctx);
    void SifCheckStatRpc(uint8_t *rdram, R5900Context *ctx);
    void SifSetRpcQueue(uint8_t *rdram, R5900Context *ctx);
    void SifRemoveRpcQueue(uint8_t *rdram, R5900Context *ctx);
    void SifRemoveRpc(uint8_t *rdram, R5900Context *ctx);

    void fioOpen(uint8_t *rdram, R5900Context *ctx);
    void fioClose(uint8_t *rdram, R5900Context *ctx);
    void fioRead(uint8_t *rdram, R5900Context *ctx);
    void fioWrite(uint8_t *rdram, R5900Context *ctx);
    void fioLseek(uint8_t *rdram, R5900Context *ctx);
    void fioMkdir(uint8_t *rdram, R5900Context *ctx);
    void fioChdir(uint8_t *rdram, R5900Context *ctx);
    void fioRmdir(uint8_t *rdram, R5900Context *ctx);
    void fioGetstat(uint8_t *rdram, R5900Context *ctx);
    void fioRemove(uint8_t *rdram, R5900Context *ctx);

    void GsSetCrt(uint8_t *rdram, R5900Context *ctx);
    void GsGetIMR(uint8_t *rdram, R5900Context *ctx);
    void GsPutIMR(uint8_t *rdram, R5900Context *ctx);
    void GsSetVideoMode(uint8_t *rdram, R5900Context *ctx);

    void GetOsdConfigParam(uint8_t *rdram, R5900Context *ctx);
    void SetOsdConfigParam(uint8_t *rdram, R5900Context *ctx);
    void GetRomName(uint8_t *rdram, R5900Context *ctx);
    void SifLoadElfPart(uint8_t *rdram, R5900Context *ctx);
    void sceSifLoadModule(uint8_t *rdram, R5900Context *ctx);

    void TODO(uint8_t *rdram, R5900Context *ctx);

    // Helper functions
    void RenderFrame(uint8_t *rdram);
}

#endif // PS2_SYSCALLS_H