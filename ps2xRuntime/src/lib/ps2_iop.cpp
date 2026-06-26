#include "runtime/ps2_iop.h"
#include "runtime/ps2_iop_audio.h"
#include "runtime/ps2_iop_cl.h"
#include "runtime/ps2_iop_dbcman.h"
#include "runtime/ps2_iop_sdrdrv.h"
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
    ps2_iop_cl::reset();
    ps2_iop_dbcman::reset();
    ps2_iop_sdrdrv::reset();
}

bool ps2_iop::handleRPC(PS2Runtime *runtime,
                        uint32_t sid,
                        uint32_t rpcNum,
                        uint32_t sendBufAddr,
                        uint32_t sendSize,
                        uint32_t recvBufAddr,
                        uint32_t recvSize,
                        uint32_t &resultPtr,
                        bool &signalNowaitCompletion)
{
    resultPtr = 0u;
    signalNowaitCompletion = false;

    if (ps2_syscalls::handleSoundDriverRpcService(m_rdram,
                                                  runtime,
                                                  sid,
                                                  rpcNum,
                                                  sendBufAddr,
                                                  sendSize,
                                                  recvBufAddr,
                                                  recvSize,
                                                  resultPtr,
                                                  signalNowaitCompletion))
    {
        return true;
    }

    if (ps2_iop_dbcman::handleDbcManRpc(m_rdram,
                                        sid,
                                        rpcNum,
                                        sendBufAddr,
                                        sendSize,
                                        recvBufAddr,
                                        recvSize,
                                        resultPtr))
    {
        return true;
    }

    if (ps2_iop_audio::handleLibSdRpc(m_rdram,
                                      runtime,
                                      sid,
                                      rpcNum,
                                      sendBufAddr,
                                      sendSize,
                                      recvBufAddr,
                                      recvSize,
                                      resultPtr))
    {
        return true;
    }

    if (ps2_iop_cl::handleSoundRpc(m_rdram, sid,
                                   rpcNum, sendBufAddr,
                                   sendSize, recvBufAddr,
                                   recvSize, resultPtr,
                                   signalNowaitCompletion))
    {
        return true;
    }

    if (ps2_iop_cl::handleClFileRpc(m_rdram, sid,
                                    rpcNum, sendBufAddr,
                                    sendSize, recvBufAddr,
                                    recvSize, resultPtr))
    {
        return true;
    }

    if (ps2_iop_sdrdrv::handleSdrdrvRpc(m_rdram, sid,
                                        rpcNum, sendBufAddr,
                                        sendSize, recvBufAddr,
                                        recvSize, resultPtr))
    {
        return true;
    }

    return false;
}
