#include "runtime/ps2_iop_dbcman.h"
#include "runtime/ps2_memory.h"

#include <cstring>
#include <iostream>

namespace
{
    constexpr uint32_t kDbcManSid = 0x80001300u;
    constexpr uint32_t kRpcCheckVersion = 0x80001363u;
    constexpr uint32_t kDbcManVersion = 0x0320u;
    constexpr uint32_t kMaxUnknownRpcLogs = 32u;

    uint32_t g_unknownRpcLogCount = 0u;

    bool writeGuestU32(uint8_t *rdram, uint32_t addr, uint32_t value)
    {
        uint8_t *ptr = getMemPtr(rdram, addr);
        if (!ptr)
        {
            return false;
        }

        std::memcpy(ptr, &value, sizeof(value));
        return true;
    }

    void writeVersionResponse(uint8_t *rdram, uint32_t recvBufAddr, uint32_t recvSize)
    {
        const uint32_t words = recvSize / sizeof(uint32_t);
        const uint32_t count = words < 4u ? words : 4u;
        for (uint32_t index = 0; index < count; ++index)
        {
            writeGuestU32(rdram, recvBufAddr + (index * sizeof(uint32_t)), kDbcManVersion);
        }
    }
}

namespace ps2_iop_dbcman
{
    void reset()
    {
        g_unknownRpcLogCount = 0u;
    }

    bool handleDbcManRpc(uint8_t *rdram,
                         uint32_t sid,
                         uint32_t rpcNum,
                         uint32_t sendBufAddr,
                         uint32_t sendSize,
                         uint32_t recvBufAddr,
                         uint32_t recvSize,
                         uint32_t &resultPtr)
    {
        if (sid != kDbcManSid)
        {
            return false;
        }

        resultPtr = recvBufAddr;
        if (recvBufAddr == 0u || recvSize == 0u)
        {
            return true;
        }

        switch (rpcNum)
        {
        case kRpcCheckVersion:
            // libdbc expects dbcman.irx version 3.20.
            writeVersionResponse(rdram, recvBufAddr, recvSize);
            return true;

        default:
            if (g_unknownRpcLogCount < kMaxUnknownRpcLogs)
            {
                std::cerr << "[DBCMAN:stub]"
                          << " sid=0x" << std::hex << sid
                          << " rpc=0x" << rpcNum
                          << " send=0x" << sendBufAddr
                          << " sendSize=0x" << sendSize
                          << " recv=0x" << recvBufAddr
                          << " recvSize=0x" << recvSize
                          << std::dec << std::endl;
                ++g_unknownRpcLogCount;
            }
            return true;
        }
    }
}
