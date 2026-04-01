#pragma once

#include "ps2_syscalls.h"

namespace ps2_syscalls
{
    void FlushCache(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void iFlushCache(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void EnableCache(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void DisableCache(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void ResetEE(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void SetMemoryMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void InitThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
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
    void iReferThreadStatus(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void SleepThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void WakeupThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void iWakeupThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void CancelWakeupThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void iCancelWakeupThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void ChangeThreadPriority(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void iChangeThreadPriority(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void RotateThreadReadyQueue(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void iRotateThreadReadyQueue(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void ReleaseWaitThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void iReleaseWaitThread(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
}
