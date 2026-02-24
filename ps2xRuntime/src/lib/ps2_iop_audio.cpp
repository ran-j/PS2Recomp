#include "ps2_iop_audio.h"
#include "ps2_runtime.h"

namespace ps2_iop_audio
{
void handleLibSdRpc(PS2Runtime *runtime, uint32_t sid, uint32_t rpcNum,
                    const uint8_t *sendBuf, uint32_t sendSize,
                    uint8_t *recvBuf, uint32_t recvSize)
{
    if (!runtime)
        return;
    runtime->audioBackend().onSoundCommand(sid, rpcNum, sendBuf, sendSize, recvBuf, recvSize);
}
}
