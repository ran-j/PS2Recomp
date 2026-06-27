#ifndef PS2_IOP_AUDIO_H
#define PS2_IOP_AUDIO_H

#include <cstdint>

class PS2Runtime;

namespace ps2_iop_audio
{
    void reset();

    bool handleLibSdRpc(uint8_t* rdram,
        PS2Runtime* runtime,
        uint32_t sid,
        uint32_t rpcNum,
        uint32_t sendBufAddr,
        uint32_t sendSize,
        uint32_t recvBufAddr,
        uint32_t recvSize,
        uint32_t& resultPtr);
}

#endif
