#ifndef PS2_IOP_H
#define PS2_IOP_H

#include <cstdint>

constexpr uint32_t IOP_SID_LIBSD = 0x80000701u;

class ps2_iop
{
public:
    ps2_iop();
    ~ps2_iop() = default;

    void init(uint8_t *rdram);
    void reset();

    bool handleRPC(uint32_t sid, uint32_t rpcNum,
                   uint32_t sendBufAddr, uint32_t sendSize,
                   uint32_t recvBufAddr, uint32_t recvSize);

private:
    uint8_t *m_rdram = nullptr;
};

#endif
