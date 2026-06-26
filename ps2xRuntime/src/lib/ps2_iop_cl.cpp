#include "runtime/ps2_iop_cl.h"
#include "runtime/ps2_iop.h"
#include "runtime/ps2_memory.h"
#include "Kernel/Syscalls/Common.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>

namespace
{
    constexpr uint32_t kClFileBufferBytes = 0x2000u;
    constexpr uint32_t kClFileLoadResultQueued = 5u;
    constexpr uint32_t kClFileLoadStatusFailed = 3u;
    constexpr uint32_t kClFileLoadStatusComplete = 7u;
    constexpr uint32_t kClFileFirstLoadHandle = 0x00010000u;
    constexpr uint32_t kLotrSoundResponseCounterOffset = 4u;

    struct ClFileHandle
    {
        FILE *file = nullptr;
        uint32_t size = 0u;
    };

    struct ClFileLoad
    {
        uint32_t status = kClFileLoadStatusFailed;
        uint32_t size = 0u;
    };

    std::mutex g_clFileMutex;
    std::unordered_map<uint32_t, ClFileHandle> g_clFileHandles;
    std::unordered_map<uint32_t, ClFileLoad> g_clFileLoads;
    uint32_t g_nextFileHandle = 1u;
    uint32_t g_nextLoadHandle = kClFileFirstLoadHandle;
    std::string g_clFileRoot;

    std::mutex g_lotrSoundMutex;
    uint32_t g_lotrSoundCounter = 0u;

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

    std::string readGuestString(uint8_t *rdram, uint32_t addr, size_t maxBytes)
    {
        const char *ptr = reinterpret_cast<const char *>(getConstMemPtr(rdram, addr));
        if (!ptr || maxBytes == 0u)
        {
            return {};
        }

        size_t length = 0u;
        while (length < maxBytes && ptr[length] != '\0')
        {
            ++length;
        }

        return std::string(ptr, length);
    }

    bool hasPs2PathDevice(const std::string &path)
    {
        return path.find(':') != std::string::npos;
    }

    std::string joinPs2Path(const std::string &root, const std::string &leaf)
    {
        if (root.empty() || leaf.empty() || hasPs2PathDevice(leaf))
        {
            return leaf;
        }

        const char tail = root.back();
        if (tail == '/' || tail == '\\' || tail == ':')
        {
            return root + leaf;
        }
        return root + "/" + leaf;
    }

    std::filesystem::path resolveClFilePath(const std::string &path)
    {
        std::string root;
        {
            std::lock_guard<std::mutex> lock(g_clFileMutex);
            root = g_clFileRoot;
        }

        const std::string rootedPath = joinPs2Path(root, path);
        const std::string translated = translatePs2Path(rootedPath.c_str());
        return translated.empty() ? std::filesystem::path{} : std::filesystem::path(translated);
    }

    uint32_t allocateClFileHandleLocked(FILE *file, uint32_t size)
    {
        if (!file)
        {
            return 0u;
        }

        for (uint32_t attempt = 0u; attempt < 0xFFFFu; ++attempt)
        {
            uint32_t handle = g_nextFileHandle++;
            if (handle == 0u)
            {
                handle = g_nextFileHandle++;
            }

            if (g_clFileHandles.find(handle) == g_clFileHandles.end() &&
                g_clFileLoads.find(handle) == g_clFileLoads.end())
            {
                g_clFileHandles.emplace(handle, ClFileHandle{file, size});
                return handle;
            }
        }

        return 0u;
    }

    uint32_t allocateClFileLoadLocked(uint32_t status, uint32_t size)
    {
        for (uint32_t attempt = 0u; attempt < 0xFFFFu; ++attempt)
        {
            uint32_t handle = g_nextLoadHandle++;
            if (handle < 3u)
            {
                handle = kClFileFirstLoadHandle;
                g_nextLoadHandle = kClFileFirstLoadHandle + 1u;
            }

            if (g_clFileLoads.find(handle) == g_clFileLoads.end() &&
                g_clFileHandles.find(handle) == g_clFileHandles.end())
            {
                g_clFileLoads.emplace(handle, ClFileLoad{status, size});
                return handle;
            }
        }

        return 0u;
    }
}

namespace ps2_iop_cl
{
    void reset()
    {
        {
            std::lock_guard<std::mutex> lock(g_clFileMutex);
            for (auto &entry : g_clFileHandles)
            {
                if (entry.second.file)
                {
                    std::fclose(entry.second.file);
                }
            }

            g_clFileHandles.clear();
            g_clFileLoads.clear();
            g_clFileRoot.clear();
            g_nextFileHandle = 1u;
            g_nextLoadHandle = kClFileFirstLoadHandle;
        }

        {
            std::lock_guard<std::mutex> lock(g_lotrSoundMutex);
            g_lotrSoundCounter = 0u;
        }
    }

    bool handleClFileRpc(uint8_t *rdram,
                         uint32_t sid,
                         uint32_t rpcNum,
                         uint32_t sendBufAddr,
                         uint32_t sendSize,
                         uint32_t recvBufAddr,
                         uint32_t recvSize,
                         uint32_t &resultPtr)
    {
        if (sid != IOP_SID_LOTR_CLFILE)
        {
            return false;
        }

        resultPtr = recvBufAddr;
        if (recvBufAddr && recvSize > 0u)
        {
            if (uint8_t *recv = getMemPtr(rdram, recvBufAddr))
            {
                std::memset(recv, 0, std::min<uint32_t>(recvSize, 0x40u));
            }
        }

        auto writeResult = [&](int32_t status, uint32_t value) {
            if (recvBufAddr && recvSize >= sizeof(uint32_t))
            {
                writeGuestU32(rdram, recvBufAddr + 0u, static_cast<uint32_t>(status));
            }
            if (recvBufAddr && recvSize >= sizeof(uint32_t) * 2u)
            {
                writeGuestU32(rdram, recvBufAddr + 4u, value);
            }
        };

        switch (rpcNum)
        {
        case 0x01u:
        {
            const std::string guestPath = readGuestString(rdram, sendBufAddr, sendSize ? std::min<uint32_t>(sendSize, 0x100u) : 0x100u);
            uint32_t requestedBytes = 0u;
            uint32_t dstAddr = 0u;
            (void)readGuestU32(rdram, sendBufAddr + 0x100u, requestedBytes);
            (void)readGuestU32(rdram, sendBufAddr + 0x104u, dstAddr);

            uint32_t status = kClFileLoadStatusFailed;
            uint32_t fileSize = 0u;
            const std::filesystem::path hostPath = resolveClFilePath(guestPath);

            if (!hostPath.empty())
            {
                std::ifstream file(hostPath, std::ios::binary | std::ios::ate);
                if (file.is_open())
                {
                    const std::streampos end = file.tellg();
                    if (end >= std::streampos(0))
                    {
                        const uint64_t hostFileSize = static_cast<uint64_t>(end);
                        fileSize = static_cast<uint32_t>(std::min<uint64_t>(hostFileSize, 0xFFFFFFFFull));

                        uint32_t dstOffset = 0u;
                        bool scratch = false;
                        if (ps2ResolveGuestPointer(dstAddr, dstOffset, scratch) && !scratch && dstOffset < PS2_RAM_SIZE)
                        {
                            uint8_t *dst = getMemPtr(rdram, dstAddr);
                            const uint64_t maxGuestBytes = static_cast<uint64_t>(PS2_RAM_SIZE - dstOffset);
                            const uint64_t maxRequestedBytes = requestedBytes ? requestedBytes : hostFileSize;
                            const size_t bytesToCopy = static_cast<size_t>(
                                std::min<uint64_t>({hostFileSize, maxRequestedBytes, maxGuestBytes}));

                            status = kClFileLoadStatusComplete;
                            if (dst && bytesToCopy > 0u)
                            {
                                file.seekg(0, std::ios::beg);
                                file.read(reinterpret_cast<char *>(dst), static_cast<std::streamsize>(bytesToCopy));
                                if (static_cast<size_t>(file.gcount()) != bytesToCopy)
                                {
                                    status = kClFileLoadStatusFailed;
                                }
                            }
                        }
                    }
                }
            }

            uint32_t loadHandle = 0u;
            {
                std::lock_guard<std::mutex> lock(g_clFileMutex);
                loadHandle = allocateClFileLoadLocked(status, fileSize);
            }

            writeResult(kClFileLoadResultQueued, loadHandle);
            return true;
        }

        case 0x04u:
            writeResult(0, 1u);
            return true;

        case 0x05u:
            writeResult(0, 0u);
            return true;

        case 0x15u:
            writeResult(0, 0u);
            return true;

        case 0x16u:
        {
            const std::string root = readGuestString(rdram, sendBufAddr, sendSize ? sendSize : 0x100u);
            std::lock_guard<std::mutex> lock(g_clFileMutex);
            g_clFileRoot = root;
            writeResult(0, 1u);
            return true;
        }

        case 0x08u:
        {
            const std::string guestPath = readGuestString(rdram, sendBufAddr, sendSize ? sendSize : 0x100u);
            const std::filesystem::path hostPath = resolveClFilePath(guestPath);
            if (hostPath.empty())
            {
                writeResult(-1, 0u);
                return true;
            }

            FILE *file = std::fopen(hostPath.string().c_str(), "rb");
            if (!file)
            {
                writeResult(-1, 0u);
                return true;
            }

            uint32_t size = 0u;
            if (std::fseek(file, 0, SEEK_END) == 0)
            {
                const long end = std::ftell(file);
                if (end > 0)
                {
                    size = static_cast<uint32_t>(std::min<long>(end, 0x7FFFFFFF));
                }
                std::fseek(file, 0, SEEK_SET);
            }

            uint32_t handle = 0u;
            {
                std::lock_guard<std::mutex> lock(g_clFileMutex);
                handle = allocateClFileHandleLocked(file, size);
            }
            if (!handle)
            {
                std::fclose(file);
                writeResult(-1, 0u);
                return true;
            }

            writeResult(0, handle);
            return true;
        }

        case 0x09u:
        {
            uint32_t handle = 0u;
            readGuestU32(rdram, sendBufAddr, handle);

            FILE *file = nullptr;
            bool closedLoad = false;
            {
                std::lock_guard<std::mutex> lock(g_clFileMutex);
                auto it = g_clFileHandles.find(handle);
                if (it != g_clFileHandles.end())
                {
                    file = it->second.file;
                    g_clFileHandles.erase(it);
                }

                auto loadIt = g_clFileLoads.find(handle);
                if (loadIt != g_clFileLoads.end())
                {
                    g_clFileLoads.erase(loadIt);
                    closedLoad = true;
                }
            }
            if (file)
            {
                std::fclose(file);
            }

            const bool closed = file || closedLoad;
            writeResult(closed ? 0 : -1, closed ? 1u : 0u);
            return true;
        }

        case 0x0Au:
        {
            uint32_t handle = 0u;
            uint32_t requested = 0u;
            uint32_t dstAddr = 0u;
            readGuestU32(rdram, sendBufAddr + 0u, handle);
            readGuestU32(rdram, sendBufAddr + 4u, requested);
            readGuestU32(rdram, sendBufAddr + 8u, dstAddr);

            FILE *file = nullptr;
            {
                std::lock_guard<std::mutex> lock(g_clFileMutex);
                auto it = g_clFileHandles.find(handle);
                if (it != g_clFileHandles.end())
                {
                    file = it->second.file;
                }
            }

            uint8_t *dst = getMemPtr(rdram, dstAddr);
            if (!file || !dst)
            {
                writeResult(-1, 0u);
                return true;
            }

            const uint32_t bytesToRead = std::min<uint32_t>(requested, kClFileBufferBytes);
            const size_t bytesRead = std::fread(dst, 1u, bytesToRead, file);
            if (bytesRead < bytesToRead && std::ferror(file))
            {
                std::clearerr(file);
                writeResult(-1, 0u);
                return true;
            }

            writeResult(0, static_cast<uint32_t>(bytesRead));
            return true;
        }

        case 0x03u:
        {
            uint32_t handle = 0u;
            readGuestU32(rdram, sendBufAddr, handle);

            std::lock_guard<std::mutex> lock(g_clFileMutex);
            auto loadIt = g_clFileLoads.find(handle);
            if (loadIt != g_clFileLoads.end())
            {
                writeResult(static_cast<int32_t>(loadIt->second.status), 0u);
                return true;
            }

            writeResult(0, g_clFileHandles.find(handle) != g_clFileHandles.end() ? 0u : 9u);
            return true;
        }

        case 0x06u:
        {
            uint32_t handle = 0u;
            readGuestU32(rdram, sendBufAddr, handle);

            std::lock_guard<std::mutex> lock(g_clFileMutex);
            auto loadIt = g_clFileLoads.find(handle);
            if (loadIt != g_clFileLoads.end())
            {
                writeResult(0, loadIt->second.size);
                return true;
            }

            auto it = g_clFileHandles.find(handle);
            writeResult(it != g_clFileHandles.end() ? 0 : -1,
                        it != g_clFileHandles.end() ? it->second.size : 0u);
            return true;
        }

        default:
            writeResult(0, 0u);
            return true;
        }
    }

    bool handleSoundRpc(uint8_t *rdram,
                        uint32_t sid,
                        uint32_t rpcNum,
                        uint32_t sendBufAddr,
                        uint32_t sendSize,
                        uint32_t recvBufAddr,
                        uint32_t recvSize,
                        uint32_t &resultPtr,
                        bool &signalNowaitCompletion)
    {
        if (sid != IOP_SID_LOTR_SOUND)
        {
            return false;
        }

        (void)rpcNum;
        (void)sendBufAddr;
        (void)sendSize;

        resultPtr = recvBufAddr;
        signalNowaitCompletion = true;

        if (recvBufAddr && recvSize > 0u)
        {
            if (uint8_t *recv = getMemPtr(rdram, recvBufAddr))
            {
                std::memset(recv, 0, recvSize);
            }
        }

        uint32_t counter = 0u;
        {
            std::lock_guard<std::mutex> lock(g_lotrSoundMutex);
            counter = ++g_lotrSoundCounter;
        }

        if (recvBufAddr && recvSize >= kLotrSoundResponseCounterOffset + sizeof(uint32_t))
        {
            // LotR's SOUND_JP callback reads word 0 as the number of active
            // stream records and word 1 as the IOP update counter.
            writeGuestU32(rdram, recvBufAddr + kLotrSoundResponseCounterOffset, counter);
        }

        return true;
    }
}
