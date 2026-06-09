#include "runtime/ps2_iop_dbcman.h"
#include "runtime/ps2_memory.h"
#include <cstring>
#include <iostream>

namespace
{
    constexpr uint32_t IOP_SID_DBCMAN = 0x80001300u;
    constexpr uint32_t DBCMAN_RPC_CHECK_VERSION = 0x80001363u;

    static bool writeIopU32(uint8_t *rdram, uint32_t addr, uint32_t value)
    {
        uint8_t *ptr = getMemPtr(rdram, addr);
        if (!ptr)
        {
            return false;
        }

        std::memcpy(ptr, &value, sizeof(value));
        return true;
    }
}

namespace ps2_iop_dbcman
{
    bool handleDbcManRpc(uint8_t *rdram,
                         uint32_t sid,
                         uint32_t rpcNum,
                         uint32_t sendBufAddr,
                         uint32_t sendSize,
                         uint32_t recvBufAddr,
                         uint32_t recvSize,
                         uint32_t &resultPtr)
    {
        if (sid != IOP_SID_DBCMAN)
        {
            return false;
        }

        if (recvBufAddr == 0u || recvSize == 0u)
        {
            resultPtr = recvBufAddr;
            return true;
        }

        switch (rpcNum)
        {
        case DBCMAN_RPC_CHECK_VERSION:
        {
            // TODO move this to compile flag
            //  libdbc expects dbcman.irx version 3.20
            constexpr uint32_t DBCMAN_VERSION = 0x0320;

            writeIopU32(rdram, recvBufAddr + 0x00u, DBCMAN_VERSION);
            writeIopU32(rdram, recvBufAddr + 0x04u, DBCMAN_VERSION);
            writeIopU32(rdram, recvBufAddr + 0x08u, DBCMAN_VERSION);
            writeIopU32(rdram, recvBufAddr + 0x0Cu, DBCMAN_VERSION);

            resultPtr = recvBufAddr;
            return true;
        }
        default:
        {
            static uint32_t dbcLogCount = 0;
            if (dbcLogCount < 32)
            {
                std::cerr << "[DBCMAN:stub]"
                          << " sid=0x" << std::hex << sid
                          << " rpc=0x" << rpcNum
                          << " send=0x" << sendBufAddr
                          << " sendSize=0x" << sendSize
                          << " recv=0x" << recvBufAddr
                          << " recvSize=0x" << recvSize
                          << std::dec << std::endl;
                ++dbcLogCount;
            }

            resultPtr = recvBufAddr;
            return true;
        }
        }
    }
}
