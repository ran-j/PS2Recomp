#include "runtime/ps2_iop_audio.h"
#include "ps2_runtime.h"

namespace ps2_iop_audio
{
    void reset()
    {
        // Noop.
    }

    bool handleLibSdRpc(uint8_t *rdram,
                        PS2Runtime *runtime,
                        uint32_t sid,
                        uint32_t rpcNum,
                        uint32_t sendBufAddr,
                        uint32_t sendSize,
                        uint32_t recvBufAddr,
                        uint32_t recvSize,
                        uint32_t &resultPtr)
    {
        if (sid != IOP_SID_LIBSD)
            return false;

        const uint8_t *sendPtr = sendBufAddr ? getConstMemPtr(rdram, sendBufAddr) : nullptr;
        uint8_t *recvPtr = recvBufAddr ? getMemPtr(rdram, recvBufAddr) : nullptr;

        runtime->audioBackend().onSoundCommand(sid,
                                               rpcNum,
                                               sendPtr,
                                               sendSize,
                                               recvPtr,
                                               recvSize);

        resultPtr = recvBufAddr;
        return true;
    }
}
