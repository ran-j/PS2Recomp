#include "ps2_iop.h"

IOP::IOP()
{
    reset();
}

void IOP::init(uint8_t *rdram)
{
    m_rdram = rdram;
}

void IOP::reset()
{
}

bool IOP::handleRPC(uint32_t /*sid*/, uint32_t /*rpcNum*/,
                    uint32_t /*sendBufAddr*/, uint32_t /*sendSize*/,
                    uint32_t /*recvBufAddr*/, uint32_t /*recvSize*/)
{
    return false;
}
