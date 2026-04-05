#include "runtime/ps2_iop.h"
#include "runtime/ps2_iop_audio.h"
#include "runtime/ps2_memory.h"
#include "ps2_runtime.h"
#include "Kernel/Syscalls/RPC.h"

ps2_iop::ps2_iop()
{
    reset();
}

void ps2_iop::init(uint8_t *rdram)
{
    m_rdram = rdram;
}

void ps2_iop::reset()
{
}

bool ps2_iop::handleRPC(PS2Runtime *runtime,
                        uint32_t sid, uint32_t rpcNum,
                        uint32_t sendBufAddr, uint32_t sendSize,
                        uint32_t recvBufAddr, uint32_t recvSize,
                        uint32_t &resultPtr,
                        bool &signalNowaitCompletion)
{
    resultPtr = 0u;
    signalNowaitCompletion = false;

    if (!runtime || !m_rdram)
    {
        return false;
    }

    if (ps2_syscalls::handleSoundDriverRpcService(m_rdram, runtime,
                                                  sid, rpcNum,
                                                  sendBufAddr, sendSize,
                                                  recvBufAddr, recvSize,
                                                  resultPtr,
                                                  signalNowaitCompletion))
    {
        return true;
    }

    if (sid == IOP_SID_LIBSD)
    {
        const uint8_t *sendPtr = sendBufAddr ? getConstMemPtr(m_rdram, sendBufAddr) : nullptr;
        uint8_t *recvPtr = recvBufAddr ? getMemPtr(m_rdram, recvBufAddr) : nullptr;
        ps2_iop_audio::handleLibSdRpc(runtime, sid, rpcNum, sendPtr, sendSize, recvPtr, recvSize);
        resultPtr = recvBufAddr;
        return true;
    }

    return false;
}
