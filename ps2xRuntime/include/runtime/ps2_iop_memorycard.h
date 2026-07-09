#ifndef PS2_IOP_MEMORYCARD_H
#define PS2_IOP_MEMORYCARD_H

#include <cstdint>

class PS2Runtime;

namespace ps2_iop_memorycard
{
    void reset();

    bool handleMemoryCardRpc(uint8_t *rdram,
                             PS2Runtime *runtime,
                             uint32_t sid,
                             uint32_t rpcNum,
                             uint32_t sendBufAddr,
                             uint32_t sendSize,
                             uint32_t recvBufAddr,
                             uint32_t recvSize,
                             uint32_t &resultPtr,
                             bool &signalNowaitCompletion);
}

#endif
