#pragma once

#include "ps2_syscalls.h"

namespace ps2_syscalls
{
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
}
