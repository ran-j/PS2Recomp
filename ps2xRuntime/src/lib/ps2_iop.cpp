#include <cstring>

#include "runtime/ps2_iop.h"
#include "runtime/ps2_iop_audio.h"
#include "runtime/ps2_iop_dbcman.h"
#include "runtime/ps2_memory.h"
#include "ps2_runtime.h"
#include "Kernel/Syscalls/RPC.h"

// ps2_iop.cpp

namespace
{
}
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

    if (ps2_iop_dbcman::handleDbcManRpc(m_rdram,
                                        sid, rpcNum,
                                        sendBufAddr, sendSize,
                                        recvBufAddr, recvSize,
                                        resultPtr))
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

    if (sid == IOP_SID_CDVD_SCMD)
    {
        // cdvdman S-command RPC. The EE libcdvd wrappers read the result buffer's first
        // word as a success flag (non-zero == ok) and the following words as outputs.
        // We serve disc S-commands directly via the sceCd* stubs, so here we just report
        // a benign success: word[0]=1, remaining words zeroed. For sceCdReadDvdDualInfo
        // this means "succeeded, single-layer (on_dual=0, layer1_start=0)", which is the
        // correct answer for a flat single-image disc and lets boot proceed.
        uint8_t *recvPtr = recvBufAddr ? getMemPtr(m_rdram, recvBufAddr) : nullptr;
        if (recvPtr && recvSize > 0)
        {
            std::memset(recvPtr, 0, recvSize);
            if (recvSize >= sizeof(uint32_t))
            {
                const uint32_t okFlag = 1u;
                std::memcpy(recvPtr, &okFlag, sizeof(okFlag));
            }
        }
        resultPtr = recvBufAddr;
        return true;
    }

    return false;
}
