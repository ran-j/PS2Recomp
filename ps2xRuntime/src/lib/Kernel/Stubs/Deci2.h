#pragma once

#include "ps2_stubs.h"

namespace ps2_stubs
{
    void sceDeci2Close(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceDeci2ExLock(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceDeci2ExRecv(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceDeci2ExReqSend(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceDeci2ExSend(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceDeci2ExUnLock(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceDeci2Open(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceDeci2Poll(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceDeci2ReqSend(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
}
