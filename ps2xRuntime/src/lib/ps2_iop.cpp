#include "ps2_iop.h"

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

bool ps2_iop::handleRPC(uint32_t /*sid*/, uint32_t /*rpcNum*/,
                    uint32_t /*sendBufAddr*/, uint32_t /*sendSize*/,
                    uint32_t /*recvBufAddr*/, uint32_t /*recvSize*/)
{
    return false;
}
