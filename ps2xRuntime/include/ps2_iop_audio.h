#ifndef PS2_IOP_AUDIO_H
#define PS2_IOP_AUDIO_H

#include <cstdint>

class PS2Runtime;

namespace ps2_iop_audio
{
void handleLibSdRpc(PS2Runtime *runtime, uint32_t sid, uint32_t rpcNum,
                    const uint8_t *sendBuf, uint32_t sendSize,
                    uint8_t *recvBuf, uint32_t recvSize);
}

#endif
