#include "module_factories.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace ps2x::iop::detail
{
    namespace
    {
        class FileIoService final : public IopService
        {
        public:
            explicit FileIoService(IopHost &host)
                : m_host(host)
            {
            }

            ~FileIoService() override
            {
                closeAllFiles();
            }

            [[nodiscard]] std::string_view name() const override
            {
                return "FILEIO";
            }

            [[nodiscard]] std::span<const uint32_t> sids() const override
            {
                return kSids;
            }

            void reset() override
            {
                closeAllFiles();
                std::lock_guard<std::mutex> lock(m_mutex);
                m_initCalls = 0u;
                m_openCalls = 0u;
                m_readCalls = 0u;
                m_closeCalls = 0u;
                m_rejectedCalls = 0u;
                m_lastFunction = 0u;
                m_lastSendSize = 0u;
                m_lastReceiveSize = 0u;
                m_nextFileDescriptor = 3;
            }

            [[nodiscard]] RpcResult handleRpc(const RpcRequest &request) override
            {
                if (request.sid != kSid)
                {
                    return {};
                }

                uint32_t initPointer = 0u;
                uint32_t normalizedPointer = 0u;
                const bool validInit =
                    request.function == kInitFunction &&
                    request.send.address != 0u &&
                    request.send.size == sizeof(initPointer) &&
                    request.receive.address != 0u &&
                    request.receive.size == sizeof(uint32_t) &&
                    m_host.readGuest(request.send.address, &initPointer, sizeof(initPointer)) &&
                    initPointer != 0u &&
                    (initPointer & (kInitPointerAlignment - 1u)) == 0u &&
                    m_host.normalizeGuestAddress(initPointer, normalizedPointer) &&
                    normalizedPointer != 0u;
                constexpr int32_t success = 0;
                if (validInit &&
                    m_host.writeGuest(request.receive.address, &success, sizeof(success)))
                {
                    {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        ++m_initCalls;
                        m_lastFunction = request.function;
                        m_lastSendSize = request.send.size;
                        m_lastReceiveSize = request.receive.size;
                    }

                    RpcResult result;
                    result.handled = true;
                    result.resultAddress = request.receive.address;
                    return result;
                }

                if (request.receive.address != 0u &&
                    request.receive.size == sizeof(int32_t))
                {
                    if (request.function == kOpenFunction &&
                        request.send.address != 0u &&
                        request.send.size >= kOpenHeaderSize &&
                        request.send.size <= kOpenArgumentSize)
                    {
                        int32_t resultValue = -1;
                        std::vector<char> pathBytes(request.send.size - sizeof(uint32_t), '\0');
                        if (m_host.readGuest(request.send.address + sizeof(uint32_t),
                                             pathBytes.data(),
                                             pathBytes.size()))
                        {
                            const auto terminator =
                                std::find(pathBytes.begin(), pathBytes.end(), '\0');
                            if (terminator != pathBytes.end() && terminator != pathBytes.begin())
                            {
                                const std::string guestPath(pathBytes.begin(), terminator);
                                const std::string hostPath = m_host.translateGuestPath(guestPath);
                                const uint64_t hostFile =
                                    hostPath.empty() ? 0u : m_host.openHostFile(hostPath);
                                if (hostFile != 0u)
                                {
                                    std::lock_guard<std::mutex> lock(m_mutex);
                                    resultValue = allocateFileDescriptorLocked(hostFile);
                                }
                            }
                        }

                        if (m_host.writeGuest(request.receive.address,
                                              &resultValue,
                                              sizeof(resultValue)))
                        {
                            std::lock_guard<std::mutex> lock(m_mutex);
                            ++m_openCalls;
                            recordRequestLocked(request);
                            return handledResult(request);
                        }
                    }
                    else if (request.function == kCloseFunction &&
                             request.send.address != 0u &&
                             request.send.size >= sizeof(int32_t))
                    {
                        int32_t fileDescriptor = -1;
                        int32_t resultValue = -1;
                        if (m_host.readGuest(request.send.address,
                                             &fileDescriptor,
                                             sizeof(fileDescriptor)))
                        {
                            uint64_t hostFile = 0u;
                            {
                                std::lock_guard<std::mutex> lock(m_mutex);
                                const auto fileIt = m_openFiles.find(fileDescriptor);
                                if (fileIt != m_openFiles.end())
                                {
                                    hostFile = fileIt->second.handle;
                                    m_openFiles.erase(fileIt);
                                }
                            }
                            if (hostFile != 0u)
                            {
                                m_host.closeHostFile(hostFile);
                                resultValue = 0;
                            }
                        }

                        if (m_host.writeGuest(request.receive.address,
                                              &resultValue,
                                              sizeof(resultValue)))
                        {
                            std::lock_guard<std::mutex> lock(m_mutex);
                            ++m_closeCalls;
                            recordRequestLocked(request);
                            return handledResult(request);
                        }
                    }
                    else if (request.function == kReadFunction &&
                             request.send.address != 0u &&
                             request.send.size >= kReadArgumentSize)
                    {
                        std::array<uint32_t, 4> arguments{};
                        int32_t resultValue = -1;
                        if (m_host.readGuest(request.send.address,
                                             arguments.data(),
                                             sizeof(arguments)) &&
                            arguments[1] != 0u &&
                            arguments[2] <= kMaximumReadSize)
                        {
                            std::vector<uint8_t> bytes(arguments[2]);
                            size_t bytesRead = 0u;
                            {
                                std::lock_guard<std::mutex> lock(m_mutex);
                                const auto fileIt =
                                    m_openFiles.find(static_cast<int32_t>(arguments[0]));
                                if (fileIt != m_openFiles.end())
                                {
                                    const bool readOk =
                                        bytes.empty() ||
                                        m_host.readHostFile(fileIt->second.handle,
                                                            fileIt->second.position,
                                                            bytes.data(),
                                                            bytes.size(),
                                                            bytesRead);
                                    if (readOk &&
                                        (bytesRead == 0u ||
                                         m_host.writeGuest(arguments[1],
                                                           bytes.data(),
                                                           bytesRead)))
                                    {
                                        fileIt->second.position += bytesRead;
                                        resultValue = static_cast<int32_t>(
                                            std::min<size_t>(bytesRead, 0x7FFFFFFFu));
                                    }
                                }
                            }
                        }

                        if (m_host.writeGuest(request.receive.address,
                                              &resultValue,
                                              sizeof(resultValue)))
                        {
                            std::lock_guard<std::mutex> lock(m_mutex);
                            ++m_readCalls;
                            recordRequestLocked(request);
                            return handledResult(request);
                        }
                    }
                }

                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    ++m_rejectedCalls;
                    m_lastFunction = request.function;
                    m_lastSendSize = request.send.size;
                    m_lastReceiveSize = request.receive.size;
                }

                std::ostringstream message;
                message << "FILEIO RPC observed but not emulated:"
                        << " sid=0x" << std::hex << request.sid
                        << " function=0x" << request.function
                        << " send=0x" << request.send.address << "/0x" << request.send.size
                        << " receive=0x" << request.receive.address << "/0x" << request.receive.size;
                m_host.log(LogLevel::Warning, message.str());

                // Binding is supported so the guest can discover the endpoint, but
                // no operation is claimed until its wire format is characterized.
                return {};
            }

            void appendDebugMetrics(std::vector<DebugMetric> &metrics) const override
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                metrics.push_back({"init_calls", m_initCalls, false});
                metrics.push_back({"open_calls", m_openCalls, false});
                metrics.push_back({"read_calls", m_readCalls, false});
                metrics.push_back({"close_calls", m_closeCalls, false});
                metrics.push_back({"open_files", m_openFiles.size(), false});
                metrics.push_back({"rejected_calls", m_rejectedCalls, false});
                metrics.push_back({"last_function", m_lastFunction, true});
                metrics.push_back({"last_send_size", m_lastSendSize, false});
                metrics.push_back({"last_receive_size", m_lastReceiveSize, false});
            }

        private:
            struct OpenFile
            {
                uint64_t handle = 0u;
                uint64_t position = 0u;
            };

            static constexpr uint32_t kSid = 0x80000001u;
            static constexpr uint32_t kOpenFunction = 0u;
            static constexpr uint32_t kCloseFunction = 1u;
            static constexpr uint32_t kReadFunction = 2u;
            static constexpr uint32_t kInitFunction = 0xFFu;
            static constexpr uint32_t kInitPointerAlignment = 0x40u;
            static constexpr uint32_t kOpenHeaderSize = sizeof(uint32_t) + 1u;
            static constexpr uint32_t kOpenArgumentSize = sizeof(uint32_t) + 256u;
            static constexpr uint32_t kReadArgumentSize = 4u * sizeof(uint32_t);
            static constexpr uint32_t kMaximumReadSize = 16u * 1024u * 1024u;
            inline static constexpr std::array<uint32_t, 1> kSids{kSid};

            [[nodiscard]] RpcResult handledResult(const RpcRequest &request) const
            {
                RpcResult result;
                result.handled = true;
                result.resultAddress = request.receive.address;
                result.signalNowaitCompletion = request.mode != 0u;
                return result;
            }

            void recordRequestLocked(const RpcRequest &request)
            {
                m_lastFunction = request.function;
                m_lastSendSize = request.send.size;
                m_lastReceiveSize = request.receive.size;
            }

            [[nodiscard]] int32_t allocateFileDescriptorLocked(uint64_t hostFile)
            {
                for (uint32_t attempts = 0u; attempts < 0x7FFFFFFDu; ++attempts)
                {
                    if (m_nextFileDescriptor < 3)
                    {
                        m_nextFileDescriptor = 3;
                    }
                    const int32_t candidate = m_nextFileDescriptor++;
                    if (m_openFiles.emplace(candidate, OpenFile{hostFile, 0u}).second)
                    {
                        return candidate;
                    }
                }
                m_host.closeHostFile(hostFile);
                return -1;
            }

            void closeAllFiles()
            {
                std::vector<uint64_t> files;
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    files.reserve(m_openFiles.size());
                    for (const auto &[descriptor, file] : m_openFiles)
                    {
                        (void)descriptor;
                        files.push_back(file.handle);
                    }
                    m_openFiles.clear();
                }
                for (const uint64_t file : files)
                {
                    m_host.closeHostFile(file);
                }
            }

            IopHost &m_host;
            mutable std::mutex m_mutex;
            uint64_t m_initCalls = 0u;
            uint64_t m_openCalls = 0u;
            uint64_t m_readCalls = 0u;
            uint64_t m_closeCalls = 0u;
            uint64_t m_rejectedCalls = 0u;
            uint32_t m_lastFunction = 0u;
            uint32_t m_lastSendSize = 0u;
            uint32_t m_lastReceiveSize = 0u;
            int32_t m_nextFileDescriptor = 3;
            std::unordered_map<int32_t, OpenFile> m_openFiles;
        };
    }

    std::unique_ptr<IopService> createFileIoService(IopHost &host)
    {
        return std::make_unique<FileIoService>(host);
    }
}
