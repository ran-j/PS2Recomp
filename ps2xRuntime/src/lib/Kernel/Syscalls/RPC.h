#pragma once

#include "ps2_syscalls.h"

namespace ps2_syscalls
{
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
    void noteDtxSifDmaTransfer(uint8_t *rdram, uint32_t srcAddr, uint32_t dstAddr, uint32_t sizeBytes);
    bool handleSoundDriverRpcService(uint8_t *rdram, PS2Runtime *runtime,
                                     uint32_t sid, uint32_t rpcNum,
                                     uint32_t sendBuf, uint32_t sendSize,
                                     uint32_t recvBuf, uint32_t recvSize,
                                     uint32_t &resultPtr,
                                     bool &signalNowaitCompletion);
    void prepareSoundDriverStatusTransfer(uint8_t *rdram, uint32_t srcAddr, uint32_t size);
    void finalizeSoundDriverStatusTransfer(uint8_t *rdram, uint32_t srcAddr, uint32_t dstAddr, uint32_t size);
    void sceSifCallRpc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceSifSendCmd(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceRpcGetPacket(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
}
