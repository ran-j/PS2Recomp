#include "module_factories.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ps2x::iop::detail
{
    namespace
    {
        class ClFileService final : public IopService
        {
        public:
            ClFileService(IopHost &host, ClFileBindings bindings)
                : m_host(host),
                  m_bindings(std::move(bindings)),
                  m_sids{m_bindings.sid},
                  m_nextLoadHandle(m_bindings.rpc.firstLoadHandle)
            {
            }

            ~ClFileService() override
            {
                reset();
            }

            [[nodiscard]] std::string_view name() const override
            {
                return m_bindings.serviceName;
            }

            [[nodiscard]] std::span<const uint32_t> sids() const override
            {
                return m_sids;
            }

            void reset() override
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                for (auto &[handle, entry] : m_fileHandles)
                {
                    (void)handle;
                    if (entry.handle != 0u)
                    {
                        m_host.closeHostFile(entry.handle);
                        entry.handle = 0u;
                    }
                }

                m_fileHandles.clear();
                m_loads.clear();
                m_root.clear();
                m_nextFileHandle = 1u;
                m_nextLoadHandle = m_bindings.rpc.firstLoadHandle;
            }

            [[nodiscard]] RpcResult handleRpc(const RpcRequest &request) override
            {
                RpcResult result;
                if (request.sid != m_bindings.sid)
                {
                    return result;
                }
                const Operation operation = decodeFunction(request.function);
                if (operation == Operation::Unknown &&
                    !m_bindings.rpc.acknowledgeUnknownFunctions)
                {
                    return result;
                }

                result.handled = true;
                result.resultAddress = request.receive.address;
                if (request.receive.address != 0u && request.receive.size != 0u)
                {
                    (void)m_host.zeroGuest(request.receive.address,
                                           std::min(request.receive.size,
                                                    m_bindings.rpc.responseClearBytes));
                }

                const auto writeRpcResult = [&](int32_t status, uint32_t value)
                {
                    writeResult(request.receive, status, value);
                };

                switch (operation)
                {
                case Operation::DirectLoad:
                {
                    const uint32_t stringBytes = request.send.size != 0u
                                                     ? std::min(request.send.size,
                                                                m_bindings.rpc.pathBytes)
                                                     : m_bindings.rpc.pathBytes;
                    const std::string guestPath = readGuestString(request.send.address, stringBytes);
                    uint32_t requestedBytes = 0u;
                    uint32_t destinationAddress = 0u;
                    (void)readGuestU32(request.send.address + m_bindings.rpc.directLoadSizeOffset,
                                       requestedBytes);
                    (void)readGuestU32(request.send.address + m_bindings.rpc.directLoadDestinationOffset,
                                       destinationAddress);

                    uint32_t status = m_bindings.rpc.loadStatusFailed;
                    uint32_t fileSize = 0u;
                    const std::string hostPath = resolvePath(guestPath);
                    if (!hostPath.empty())
                    {
                        const uint64_t file = m_host.openHostFile(hostPath);
                        uint64_t hostFileSize = 0u;
                        if (file != 0u && m_host.hostFileSize(file, hostFileSize))
                        {
                            fileSize = static_cast<uint32_t>(
                                std::min<uint64_t>(hostFileSize, 0xFFFFFFFFull));
                            const uint64_t maxRequestedBytes = requestedBytes != 0u
                                                                   ? requestedBytes
                                                                   : hostFileSize;
                            const uint64_t bytesToCopy = std::min(hostFileSize, maxRequestedBytes);

                            status = m_bindings.rpc.loadStatusComplete;
                            if (destinationAddress != 0u && bytesToCopy != 0u)
                            {
                                if (!copyFileToGuest(file, destinationAddress, bytesToCopy))
                                {
                                    status = m_bindings.rpc.loadStatusFailed;
                                }
                            }
                        }
                        if (file != 0u)
                        {
                            m_host.closeHostFile(file);
                        }
                    }

                    uint32_t loadHandle = 0u;
                    {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        loadHandle = allocateLoadLocked(status, fileSize);
                    }
                    writeRpcResult(static_cast<int32_t>(m_bindings.rpc.loadResultQueued), loadHandle);
                    return result;
                }

                case Operation::Initialize:
                    writeRpcResult(0, 1u);
                    return result;

                case Operation::Wait:
                case Operation::SecondaryWait:
                    writeRpcResult(0, 0u);
                    return result;

                case Operation::SetRoot:
                {
                    const std::string root = readGuestString(request.send.address,
                                                             request.send.size != 0u
                                                                 ? request.send.size
                                                                 : m_bindings.rpc.pathBytes);
                    {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        m_root = root;
                    }
                    writeRpcResult(0, 1u);
                    return result;
                }

                case Operation::Open:
                {
                    const std::string guestPath = readGuestString(request.send.address,
                                                                  request.send.size != 0u
                                                                      ? request.send.size
                                                                      : m_bindings.rpc.pathBytes);
                    const std::string hostPath = resolvePath(guestPath);
                    if (hostPath.empty())
                    {
                        writeRpcResult(-1, 0u);
                        return result;
                    }

                    const uint64_t file = m_host.openHostFile(hostPath);
                    if (file == 0u)
                    {
                        writeRpcResult(-1, 0u);
                        return result;
                    }

                    uint64_t hostFileSize = 0u;
                    if (!m_host.hostFileSize(file, hostFileSize))
                    {
                        m_host.closeHostFile(file);
                        writeRpcResult(-1, 0u);
                        return result;
                    }
                    const uint32_t fileSize = static_cast<uint32_t>(
                        std::min<uint64_t>(hostFileSize, 0x7FFFFFFFull));

                    uint32_t handle = 0u;
                    {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        handle = allocateFileHandleLocked(file, fileSize);
                    }
                    if (handle == 0u)
                    {
                        m_host.closeHostFile(file);
                        writeRpcResult(-1, 0u);
                        return result;
                    }

                    writeRpcResult(0, handle);
                    return result;
                }

                case Operation::Close:
                {
                    uint32_t handle = 0u;
                    (void)readGuestU32(request.send.address, handle);

                    uint64_t file = 0u;
                    bool closedLoad = false;
                    {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        const auto fileIt = m_fileHandles.find(handle);
                        if (fileIt != m_fileHandles.end())
                        {
                            file = fileIt->second.handle;
                            m_fileHandles.erase(fileIt);
                        }

                        const auto loadIt = m_loads.find(handle);
                        if (loadIt != m_loads.end())
                        {
                            m_loads.erase(loadIt);
                            closedLoad = true;
                        }
                    }
                    if (file != 0u)
                    {
                        m_host.closeHostFile(file);
                    }

                    const bool closed = file != 0u || closedLoad;
                    writeRpcResult(closed ? 0 : -1, closed ? 1u : 0u);
                    return result;
                }

                case Operation::Read:
                {
                    uint32_t handle = 0u;
                    uint32_t requestedBytes = 0u;
                    uint32_t destinationAddress = 0u;
                    (void)readGuestU32(request.send.address + 0u, handle);
                    (void)readGuestU32(request.send.address + 4u, requestedBytes);
                    (void)readGuestU32(request.send.address + 8u, destinationAddress);

                    if (destinationAddress == 0u)
                    {
                        writeRpcResult(-1, 0u);
                        return result;
                    }

                    std::vector<uint8_t> bytes(std::min(requestedBytes,
                                                        m_bindings.rpc.maximumReadBytes));
                    size_t bytesRead = 0u;
                    bool readFailed = false;
                    {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        const auto fileIt = m_fileHandles.find(handle);
                        if (fileIt == m_fileHandles.end() || fileIt->second.handle == 0u)
                        {
                            readFailed = true;
                        }
                        else if (!bytes.empty())
                        {
                            size_t hostBytesRead = 0u;
                            if (!m_host.readHostFile(fileIt->second.handle,
                                                     fileIt->second.position,
                                                     bytes.data(),
                                                     bytes.size(),
                                                     hostBytesRead))
                            {
                                readFailed = true;
                            }
                            else
                            {
                                bytesRead = hostBytesRead;
                                fileIt->second.position += hostBytesRead;
                            }
                        }
                    }

                    if (readFailed ||
                        (bytesRead != 0u &&
                         !m_host.writeGuest(destinationAddress, bytes.data(), bytesRead)))
                    {
                        writeRpcResult(-1, 0u);
                        return result;
                    }

                    writeRpcResult(0, static_cast<uint32_t>(bytesRead));
                    return result;
                }

                case Operation::GetStatus:
                {
                    uint32_t handle = 0u;
                    (void)readGuestU32(request.send.address, handle);

                    bool loadFound = false;
                    uint32_t loadStatus = 0u;
                    bool fileFound = false;
                    {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        const auto loadIt = m_loads.find(handle);
                        if (loadIt != m_loads.end())
                        {
                            loadFound = true;
                            loadStatus = loadIt->second.status;
                        }
                        else
                        {
                            fileFound = m_fileHandles.find(handle) != m_fileHandles.end();
                        }
                    }

                    if (loadFound)
                    {
                        writeRpcResult(static_cast<int32_t>(loadStatus), 0u);
                    }
                    else
                    {
                        writeRpcResult(0, fileFound ? 0u : m_bindings.rpc.invalidHandleStatus);
                    }
                    return result;
                }

                case Operation::GetSize:
                {
                    uint32_t handle = 0u;
                    (void)readGuestU32(request.send.address, handle);

                    bool found = false;
                    uint32_t size = 0u;
                    {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        const auto loadIt = m_loads.find(handle);
                        if (loadIt != m_loads.end())
                        {
                            found = true;
                            size = loadIt->second.size;
                        }
                        else
                        {
                            const auto fileIt = m_fileHandles.find(handle);
                            if (fileIt != m_fileHandles.end())
                            {
                                found = true;
                                size = fileIt->second.size;
                            }
                        }
                    }

                    writeRpcResult(found ? 0 : -1, found ? size : 0u);
                    return result;
                }

                case Operation::Unknown:
                    writeRpcResult(0, 0u);
                    return result;
                }
                return result;
            }

            void appendDebugMetrics(std::vector<DebugMetric> &metrics) const override
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                metrics.push_back({"open_files", m_fileHandles.size(), false});
                metrics.push_back({"load_records", m_loads.size(), false});
                metrics.push_back({"next_file_handle", m_nextFileHandle, true});
                metrics.push_back({"next_load_handle", m_nextLoadHandle, true});
            }

        private:
            enum class Operation
            {
                DirectLoad,
                GetStatus,
                Initialize,
                Wait,
                GetSize,
                Open,
                Close,
                Read,
                SecondaryWait,
                SetRoot,
                Unknown,
            };

            [[nodiscard]] Operation decodeFunction(uint32_t function) const
            {
                const ClFileRpcLayout &rpc = m_bindings.rpc;
                if (function == rpc.directLoadFunction) return Operation::DirectLoad;
                if (function == rpc.getStatusFunction) return Operation::GetStatus;
                if (function == rpc.initializeFunction) return Operation::Initialize;
                if (function == rpc.waitFunction) return Operation::Wait;
                if (function == rpc.getSizeFunction) return Operation::GetSize;
                if (function == rpc.openFunction) return Operation::Open;
                if (function == rpc.closeFunction) return Operation::Close;
                if (function == rpc.readFunction) return Operation::Read;
                if (function == rpc.secondaryWaitFunction) return Operation::SecondaryWait;
                if (function == rpc.setRootFunction) return Operation::SetRoot;
                return Operation::Unknown;
            }

            struct ClFileHandle
            {
                uint64_t handle = 0u;
                uint32_t size = 0u;
                uint64_t position = 0u;
            };

            struct ClFileLoad
            {
                uint32_t status = 0u;
                uint32_t size = 0u;
            };

            [[nodiscard]] bool readGuestU32(uint32_t address, uint32_t &value) const
            {
                value = 0u;
                return m_host.readGuest(address, &value, sizeof(value));
            }

            [[nodiscard]] std::string readGuestString(uint32_t address, uint32_t maxBytes) const
            {
                if (address == 0u || maxBytes == 0u)
                {
                    return {};
                }

                std::vector<char> bytes(maxBytes);
                if (!m_host.readGuest(address, bytes.data(), bytes.size()))
                {
                    return {};
                }

                size_t length = 0u;
                while (length < bytes.size() && bytes[length] != '\0')
                {
                    ++length;
                }
                return std::string(bytes.data(), length);
            }

            [[nodiscard]] static bool hasDevice(std::string_view path)
            {
                return path.find(':') != std::string_view::npos;
            }

            [[nodiscard]] static std::string joinGuestPath(const std::string &root,
                                                           const std::string &leaf)
            {
                if (root.empty() || leaf.empty() || hasDevice(leaf))
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

            [[nodiscard]] std::string resolvePath(const std::string &path) const
            {
                std::string root;
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    root = m_root;
                }

                const std::string translated = m_host.translateGuestPath(joinGuestPath(root, path));
                return translated;
            }

            [[nodiscard]] uint32_t allocateFileHandleLocked(uint64_t file, uint32_t size)
            {
                if (file == 0u)
                {
                    return 0u;
                }

                for (uint32_t attempt = 0u; attempt < 0xFFFFu; ++attempt)
                {
                    uint32_t handle = m_nextFileHandle++;
                    if (handle == 0u)
                    {
                        handle = m_nextFileHandle++;
                    }
                    if (m_fileHandles.find(handle) == m_fileHandles.end() &&
                        m_loads.find(handle) == m_loads.end())
                    {
                        m_fileHandles.emplace(handle, ClFileHandle{file, size, 0u});
                        return handle;
                    }
                }
                return 0u;
            }

            [[nodiscard]] uint32_t allocateLoadLocked(uint32_t status, uint32_t size)
            {
                for (uint32_t attempt = 0u; attempt < 0xFFFFu; ++attempt)
                {
                    uint32_t handle = m_nextLoadHandle++;
                    if (handle < 3u)
                    {
                        handle = m_bindings.rpc.firstLoadHandle;
                        m_nextLoadHandle = m_bindings.rpc.firstLoadHandle + 1u;
                    }
                    if (m_loads.find(handle) == m_loads.end() &&
                        m_fileHandles.find(handle) == m_fileHandles.end())
                    {
                        m_loads.emplace(handle, ClFileLoad{status, size});
                        return handle;
                    }
                }
                return 0u;
            }

            void writeResult(GuestBuffer receive, int32_t status, uint32_t value)
            {
                if (receive.address != 0u &&
                    receive.size >= m_bindings.rpc.responseStatusOffset + sizeof(uint32_t))
                {
                    const uint32_t encodedStatus = static_cast<uint32_t>(status);
                    (void)m_host.writeGuest(receive.address + m_bindings.rpc.responseStatusOffset,
                                            &encodedStatus,
                                            sizeof(encodedStatus));
                }
                if (receive.address != 0u &&
                    receive.size >= m_bindings.rpc.responseValueOffset + sizeof(uint32_t))
                {
                    (void)m_host.writeGuest(receive.address + m_bindings.rpc.responseValueOffset,
                                            &value,
                                            sizeof(value));
                }
            }

            [[nodiscard]] bool copyFileToGuest(uint64_t file,
                                               uint32_t destinationAddress,
                                               uint64_t bytesToCopy)
            {
                constexpr size_t kChunkBytes = 16u * 1024u;
                if (bytesToCopy > 0xFFFFFFFFull - static_cast<uint64_t>(destinationAddress) + 1ull)
                {
                    return false;
                }

                std::vector<uint8_t> chunk(kChunkBytes);
                uint64_t copied = 0u;
                while (copied < bytesToCopy)
                {
                    const size_t wanted = static_cast<size_t>(
                        std::min<uint64_t>(chunk.size(), bytesToCopy - copied));
                    size_t received = 0u;
                    if (!m_host.readHostFile(file,
                                             copied,
                                             chunk.data(),
                                             wanted,
                                             received) ||
                        received != wanted)
                    {
                        return false;
                    }

                    const uint32_t chunkAddress = destinationAddress + static_cast<uint32_t>(copied);
                    if (!m_host.writeGuest(chunkAddress, chunk.data(), received))
                    {
                        return false;
                    }
                    copied += received;
                }
                return true;
            }

            IopHost &m_host;
            ClFileBindings m_bindings;
            std::array<uint32_t, 1> m_sids;
            mutable std::mutex m_mutex;
            std::unordered_map<uint32_t, ClFileHandle> m_fileHandles;
            std::unordered_map<uint32_t, ClFileLoad> m_loads;
            uint32_t m_nextFileHandle = 1u;
            uint32_t m_nextLoadHandle = 0u;
            std::string m_root;
        };
    }

    std::unique_ptr<IopService> createClFileService(IopHost &host,
                                                    ClFileBindings bindings)
    {
        const ClFileRpcLayout &rpc = bindings.rpc;
        const std::array<uint32_t, 10> functions = {
            rpc.directLoadFunction,
            rpc.getStatusFunction,
            rpc.initializeFunction,
            rpc.waitFunction,
            rpc.getSizeFunction,
            rpc.openFunction,
            rpc.closeFunction,
            rpc.readFunction,
            rpc.secondaryWaitFunction,
            rpc.setRootFunction,
        };
        std::unordered_set<uint32_t> uniqueFunctions;
        for (const uint32_t function : functions)
        {
            if (!uniqueFunctions.emplace(function).second)
            {
                throw std::invalid_argument("duplicate CLFILE RPC function binding");
            }
        }
        if (bindings.serviceName.empty() || bindings.sid == 0u ||
            rpc.pathBytes == 0u || rpc.maximumReadBytes == 0u ||
            rpc.firstLoadHandle < 3u)
        {
            throw std::invalid_argument("invalid CLFILE bindings");
        }
        return std::make_unique<ClFileService>(host, std::move(bindings));
    }
}
