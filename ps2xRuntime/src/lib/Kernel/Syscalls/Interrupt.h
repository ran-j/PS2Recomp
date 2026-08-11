#pragma once

#include "ps2_syscalls.h"

namespace ps2_syscalls
{
    void dispatchDmacHandlersForCause(uint8_t *rdram, PS2Runtime *runtime, uint32_t cause);
    uint64_t GetCurrentVSyncTick(PS2Runtime *runtime);
    void WaitVSyncTick(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, int fixedResult);
    void SetVSyncFlag(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void EnableIntc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void iEnableIntc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void DisableIntc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void iDisableIntc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void AddIntcHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void AddIntcHandler2(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void RemoveIntcHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void AddDmacHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void AddDmacHandler2(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void RemoveDmacHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void EnableIntcHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void DisableIntcHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void EnableDmacHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void DisableDmacHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void EnableDmac(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void iEnableDmac(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void DisableDmac(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void iDisableDmac(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
}
