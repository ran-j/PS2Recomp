#pragma once

#include "ps2_syscalls.h"

namespace ps2_syscalls
{
    void CreateSema(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void DeleteSema(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void iDeleteSema(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
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
    void InitAlarm(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void SetAlarm(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void iSetAlarm(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void CancelAlarm(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void iCancelAlarm(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void ReleaseAlarm(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void iReleaseAlarm(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
}
