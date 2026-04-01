#pragma once

#include "ps2_stubs.h"

namespace ps2_stubs
{
    void DmaAddr(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceDmaCallback(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceDmaDebug(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceDmaGetChan(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceDmaGetEnv(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceDmaLastSyncTime(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceDmaPause(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceDmaPutEnv(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceDmaPutStallAddr(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceDmaRecv(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceDmaRecvI(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceDmaRecvN(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceDmaReset(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceDmaRestart(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceDmaSend(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceDmaSendI(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceDmaSendM(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceDmaSendN(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceDmaSync(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceDmaSyncN(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceDmaWatch(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
}
