#include "runtime/ps2_iop_sdrdrv.h"
#include "runtime/ps2_iop.h"
#include "runtime/ps2_memory.h"
#include "ps2_runtime.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace
{
    constexpr uint32_t kImgHeaderAddr = 0x012F0000u;
    constexpr uint32_t kSectorSize = 2048u;
    constexpr uint32_t kStatusOffset = 0x6Cu;
    constexpr uint32_t kStatusStride = 8u;

    uint32_t g_headerWarnCount = 0u;
    uint32_t g_bodyWarnCount = 0u;

    bool readGuestU32(const uint8_t *rdram, uint32_t addr, uint32_t &out)
    {
        const uint8_t *ptr = getConstMemPtr(rdram, addr);
        if (!ptr)
        {
            return false;
        }

        std::memcpy(&out, ptr, sizeof(out));
        return true;
    }

    bool tryExistingFile(const std::filesystem::path &path, std::filesystem::path &out)
    {
        std::error_code ec;
        if (!path.empty() && std::filesystem::is_regular_file(path, ec) && !ec)
        {
            out = path;
            return true;
        }
        return false;
    }

    bool findSiblingFile(const char *lowerName, const char *upperName, std::filesystem::path &out)
    {
        const PS2Runtime::IoPaths &paths = PS2Runtime::getIoPaths();
        const std::array<std::filesystem::path, 2> roots = {
            paths.cdRoot,
            paths.elfDirectory,
        };

        for (const std::filesystem::path &root : roots)
        {
            if (tryExistingFile(root / lowerName, out) ||
                tryExistingFile(root / upperName, out))
            {
                return true;
            }
        }

        return false;
    }

    bool readHostRange(const std::filesystem::path &path, uint64_t offsetBytes, uint8_t *dst, size_t byteCount)
    {
        if (!dst)
        {
            return false;
        }
        if (byteCount == 0)
        {
            return true;
        }

        std::memset(dst, 0, byteCount);

        std::ifstream file(path, std::ios::binary);
        if (!file.is_open())
        {
            return false;
        }

        file.seekg(static_cast<std::streamoff>(offsetBytes), std::ios::beg);
        if (!file.good())
        {
            return false;
        }

        file.read(reinterpret_cast<char *>(dst), static_cast<std::streamsize>(byteCount));
        return true;
    }

    size_t clampGuestRdramBytes(uint32_t addr, uint32_t requested)
    {
        uint32_t offset = 0u;
        bool scratch = false;
        if (!ps2ResolveGuestPointer(addr, offset, scratch) || scratch || offset >= PS2_RAM_SIZE)
        {
            return 0u;
        }

        return static_cast<size_t>(
            std::min<uint64_t>(requested, static_cast<uint64_t>(PS2_RAM_SIZE - offset)));
    }

    bool loadImageHeader(uint8_t *rdram)
    {
        std::filesystem::path headerPath;
        if (!findSiblingFile("img_hd.bin", "IMG_HD.BIN", headerPath))
        {
            return false;
        }

        std::error_code ec;
        const uint64_t fileBytes = static_cast<uint64_t>(std::filesystem::file_size(headerPath, ec));
        if (ec)
        {
            return false;
        }

        uint8_t *dst = getMemPtr(rdram, kImgHeaderAddr);
        if (!dst)
        {
            return false;
        }

        const size_t bytes = static_cast<size_t>(std::min<uint64_t>(fileBytes, PS2_RAM_SIZE - (kImgHeaderAddr & PS2_RAM_MASK)));
        return readHostRange(headerPath, 0u, dst, bytes);
    }

    void warnImageHeaderLoadFailed()
    {
        if (g_headerWarnCount < 4u)
        {
            std::cerr << "[FatalFrame SDRDRV] failed to load img_hd.bin" << std::endl;
            ++g_headerWarnCount;
        }
    }

    bool readBody(uint8_t *rdram, uint32_t lbn, uint32_t byteCount, uint32_t dstAddr)
    {
        if (byteCount == 0)
        {
            return true;
        }

        uint32_t dstOffset = 0u;
        bool scratch = false;
        if (!ps2ResolveGuestPointer(dstAddr, dstOffset, scratch) || scratch || dstOffset >= PS2_RAM_SIZE)
        {
            return false;
        }

        uint8_t *dst = getMemPtr(rdram, dstAddr);
        if (!dst)
        {
            return false;
        }

        const size_t bytes = static_cast<size_t>(
            std::min<uint64_t>(byteCount, static_cast<uint64_t>(PS2_RAM_SIZE - dstOffset)));

        std::filesystem::path bodyPath;
        if (!findSiblingFile("img_bd.bin", "IMG_BD.BIN", bodyPath))
        {
            bodyPath = PS2Runtime::getIoPaths().cdImage;
        }
        if (bodyPath.empty())
        {
            return false;
        }

        const uint64_t offsetBytes = static_cast<uint64_t>(lbn) * kSectorSize;
        return readHostRange(bodyPath, offsetBytes, dst, bytes);
    }

    void clearRecv(uint8_t *rdram, uint32_t recvBufAddr, uint32_t recvSize)
    {
        if (recvBufAddr == 0u || recvSize == 0u)
        {
            return;
        }

        if (uint8_t *recv = getMemPtr(rdram, recvBufAddr))
        {
            const size_t bytes = clampGuestRdramBytes(recvBufAddr, recvSize);
            std::memset(recv, 0, bytes);
        }
    }

    void markLoadComplete(uint8_t *rdram, uint32_t recvBufAddr, uint32_t recvSize, uint32_t loadId)
    {
        const uint32_t statusOffset = kStatusOffset + ((loadId & 0x1Fu) * kStatusStride);
        if (recvBufAddr == 0u || statusOffset >= recvSize)
        {
            return;
        }

        if (uint8_t *status = getMemPtr(rdram, recvBufAddr + statusOffset))
        {
            *status = 0u;
        }
    }
}

namespace ps2_iop_sdrdrv
{
    void reset()
    {
        g_headerWarnCount = 0u;
        g_bodyWarnCount = 0u;
    }

    bool handleSdrdrvRpc(uint8_t *rdram,
                         uint32_t sid,
                         uint32_t rpcNum,
                         uint32_t sendBufAddr,
                         uint32_t sendSize,
                         uint32_t recvBufAddr,
                         uint32_t recvSize,
                         uint32_t &resultPtr)
    {
        if (sid != IOP_SID_FATAL_FRAME_SDRDRV)
        {
            return false;
        }

        resultPtr = recvBufAddr;
        clearRecv(rdram, recvBufAddr, recvSize);

        if (rpcNum == 0u)
        {
            if (!loadImageHeader(rdram))
            {
                warnImageHeaderLoadFailed();
            }
            return true;
        }

        if (rpcNum == 2u)
        {
            return true;
        }

        if (rpcNum != 1u)
        {
            return true;
        }

        constexpr uint32_t kCommandBytes = 32u;
        const uint32_t commandCount = std::min<uint32_t>(sendSize / kCommandBytes, 32u);
        for (uint32_t commandIndex = 0; commandIndex < commandCount; ++commandIndex)
        {
            std::array<uint32_t, 8> words{};
            const uint32_t commandAddr = sendBufAddr + (commandIndex * kCommandBytes);
            bool validCommand = true;
            for (uint32_t wordIndex = 0; wordIndex < words.size(); ++wordIndex)
            {
                if (!readGuestU32(rdram, commandAddr + (wordIndex * sizeof(uint32_t)), words[wordIndex]))
                {
                    validCommand = false;
                    break;
                }
            }
            if (!validCommand)
            {
                continue;
            }

            switch (words[0])
            {
            case 0x0Cu:
                if (!loadImageHeader(rdram))
                {
                    warnImageHeaderLoadFailed();
                }
                break;

            case 0x0Eu:
            {
                const uint32_t lbn = words[2];
                const uint32_t byteCount = words[3];
                const uint32_t dstAddr = words[4];
                const uint32_t loadId = words[6];
                const bool eeLoad = words[5] == 0u;
                const bool bodyLoaded = !eeLoad || readBody(rdram, lbn, byteCount, dstAddr);

                if (eeLoad && !bodyLoaded)
                {
                    if (uint8_t *dst = getMemPtr(rdram, dstAddr))
                    {
                        uint32_t dstOffset = 0u;
                        bool scratch = false;
                        (void)ps2ResolveGuestPointer(dstAddr, dstOffset, scratch);
                        if (!scratch && dstOffset < PS2_RAM_SIZE)
                        {
                            const size_t bytes = static_cast<size_t>(
                                std::min<uint64_t>(byteCount, static_cast<uint64_t>(PS2_RAM_SIZE - dstOffset)));
                            std::memset(dst, 0, bytes);
                        }
                    }

                    if (g_bodyWarnCount < 8u)
                    {
                        std::cerr << "[FatalFrame SDRDRV] failed data read lbn=0x" << std::hex << lbn
                                  << " bytes=0x" << byteCount
                                  << " dst=0x" << dstAddr << std::dec << std::endl;
                        ++g_bodyWarnCount;
                    }
                }

                markLoadComplete(rdram, recvBufAddr, recvSize, loadId);
                break;
            }

            default:
                break;
            }
        }

        return true;
    }
}
