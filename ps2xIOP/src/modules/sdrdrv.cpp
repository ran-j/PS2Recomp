#include "module_factories.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ps2x::iop::detail
{
    namespace
    {
        constexpr uint32_t kEeRamSize = 32u * 1024u * 1024u;

        class SdrdrvService final : public IopService
        {
        public:
            SdrdrvService(IopHost &host, SdrdrvBindings bindings)
                : m_host(host), m_bindings(std::move(bindings)), m_sids{m_bindings.sid}
            {
            }

            std::string_view name() const override { return m_bindings.serviceName; }
            std::span<const uint32_t> sids() const override { return m_sids; }

            void reset() override
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_headerWarnCount = 0;
                m_bodyWarnCount = 0;
            }

            RpcResult handleRpc(const RpcRequest &request) override
            {
                RpcResult result;
                if (request.sid != m_bindings.sid)
                {
                    return result;
                }

                result.handled = true;
                result.resultAddress = request.receive.address;
                if (m_bindings.clearReceiveBeforeDispatch &&
                    request.receive.address && request.receive.size)
                {
                    (void)m_host.zeroGuest(request.receive.address, request.receive.size);
                }

                if (request.function == m_bindings.initFunction)
                {
                    if (!loadImageHeader())
                    {
                        warnHeader();
                    }
                    return result;
                }
                if (request.function == m_bindings.shutdownFunction)
                {
                    return result;
                }
                if (request.function != m_bindings.submitFunction)
                {
                    return result;
                }

                const uint32_t count = std::min(request.send.size / m_bindings.commandBytes,
                                                m_bindings.maxCommands);
                for (uint32_t commandIndex = 0; commandIndex < count; ++commandIndex)
                {
                    std::vector<uint32_t> words(m_bindings.commandBytes / sizeof(uint32_t));
                    const uint32_t commandAddress = request.send.address +
                                                    commandIndex * m_bindings.commandBytes;
                    if (!m_host.readGuest(commandAddress,
                                          words.data(),
                                          m_bindings.commandBytes))
                    {
                        continue;
                    }

                    if (words[0] == m_bindings.headerCommand)
                    {
                        if (!loadImageHeader())
                        {
                            warnHeader();
                        }
                        continue;
                    }
                    if (words[0] != m_bindings.loadCommand)
                    {
                        continue;
                    }

                    const uint32_t lbn = words[m_bindings.lbnWord];
                    const uint32_t byteCount = words[m_bindings.byteCountWord];
                    const uint32_t destination = words[m_bindings.destinationWord];
                    const bool eeLoad = words[m_bindings.destinationKindWord] ==
                                        m_bindings.eeDestinationKind;
                    const uint32_t loadId = words[m_bindings.loadIdWord];
                    const bool loaded = eeLoad
                                            ? readBody(lbn, byteCount, destination)
                                            : m_bindings.pretendNonEeLoadsComplete;
                    if (eeLoad && !loaded)
                    {
                        (void)m_host.zeroGuest(destination, byteCount);
                        bool shouldWarn = false;
                        {
                            std::lock_guard<std::mutex> lock(m_mutex);
                            if (m_bodyWarnCount < m_bindings.bodyWarningLimit)
                            {
                                ++m_bodyWarnCount;
                                shouldWarn = true;
                            }
                        }
                        if (shouldWarn)
                        {
                            std::ostringstream message;
                            message << '[' << m_bindings.serviceName
                                    << "] failed data read lbn=0x" << std::hex << lbn
                                    << " bytes=0x" << byteCount << " dst=0x" << destination;
                            m_host.log(LogLevel::Warning, message.str());
                        }
                    }
                    if (loaded || m_bindings.completeFailedLoads)
                    {
                        markLoadComplete(request.receive, loadId);
                    }
                }
                return result;
            }

            void appendDebugMetrics(std::vector<DebugMetric> &metrics) const override
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                metrics.push_back({"header_warnings", m_headerWarnCount, false});
                metrics.push_back({"body_warnings", m_bodyWarnCount, false});
            }

        private:
            uint64_t openSiblingFile(const std::string &lowerName,
                                     const std::string &upperName)
            {
                const std::array<std::string, 2> roots = {
                    m_host.hostPath(HostPathKind::CdRoot),
                    m_host.hostPath(HostPathKind::ElfDirectory),
                };
                for (const std::string &rootValue : roots)
                {
                    if (rootValue.empty())
                    {
                        continue;
                    }
                    const std::filesystem::path root(rootValue);
                    for (const std::string *name : {&lowerName, &upperName})
                    {
                        if (name->empty())
                        {
                            continue;
                        }
                        const std::filesystem::path candidate = root / *name;
                        const uint64_t handle = m_host.openHostFile(candidate.string());
                        if (handle != 0u)
                        {
                            return handle;
                        }
                    }
                }
                return 0u;
            }

            bool copyHostRange(uint64_t handle,
                               uint64_t offset,
                               uint32_t destination,
                               uint64_t byteCount)
            {
                if (byteCount == 0)
                {
                    return true;
                }
                std::array<uint8_t, 16 * 1024> chunk{};
                uint64_t copied = 0;
                while (copied < byteCount)
                {
                    const size_t wanted = static_cast<size_t>(std::min<uint64_t>(chunk.size(), byteCount - copied));
                    std::fill(chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(wanted), 0u);
                    size_t got = 0u;
                    if (!m_host.readHostFile(handle,
                                             offset + copied,
                                             chunk.data(),
                                             wanted,
                                             got) ||
                        got > wanted ||
                        !m_host.writeGuest(destination + static_cast<uint32_t>(copied),
                                           chunk.data(),
                                           wanted))
                    {
                        return false;
                    }
                    copied += wanted;
                }
                return true;
            }

            bool loadImageHeader()
            {
                const uint64_t handle = openSiblingFile(m_bindings.imageHeaderLowerName,
                                                        m_bindings.imageHeaderUpperName);
                if (handle == 0u)
                {
                    return false;
                }
                uint64_t fileSize = 0u;
                if (!m_host.hostFileSize(handle, fileSize))
                {
                    m_host.closeHostFile(handle);
                    return false;
                }
                uint32_t normalized = 0;
                if (!m_host.normalizeGuestAddress(m_bindings.imageHeaderAddress, normalized) ||
                    normalized >= kEeRamSize)
                {
                    m_host.closeHostFile(handle);
                    return false;
                }
                const bool copied = copyHostRange(handle,
                                                  0u,
                                                  m_bindings.imageHeaderAddress,
                                                  std::min<uint64_t>(fileSize,
                                                                     kEeRamSize - normalized));
                m_host.closeHostFile(handle);
                return copied;
            }

            bool readBody(uint32_t lbn, uint32_t byteCount, uint32_t destination)
            {
                uint32_t normalized = 0;
                if (!m_host.normalizeGuestAddress(destination, normalized) || normalized >= kEeRamSize)
                {
                    return false;
                }
                uint64_t handle = openSiblingFile(m_bindings.imageBodyLowerName,
                                                  m_bindings.imageBodyUpperName);
                if (handle == 0u && m_bindings.fallbackBodyToCdImage)
                {
                    handle = m_host.openHostFile(m_host.hostPath(HostPathKind::CdImage));
                }
                if (handle == 0u)
                {
                    return false;
                }
                const uint64_t bytes = std::min<uint64_t>(byteCount, kEeRamSize - normalized);
                const bool copied = copyHostRange(handle,
                                                  static_cast<uint64_t>(lbn) * m_bindings.sectorSize,
                                                  destination,
                                                  bytes);
                m_host.closeHostFile(handle);
                return copied;
            }

            void markLoadComplete(GuestBuffer receive, uint32_t loadId)
            {
                const uint32_t offset = m_bindings.statusOffset +
                                        ((loadId & m_bindings.statusSlotMask) *
                                         m_bindings.statusStride);
                if (receive.address && offset < receive.size)
                {
                    const uint8_t complete = m_bindings.completeValue;
                    (void)m_host.writeGuest(receive.address + offset, &complete, sizeof(complete));
                }
            }

            void warnHeader()
            {
                bool shouldWarn = false;
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    if (m_headerWarnCount < m_bindings.headerWarningLimit)
                    {
                        ++m_headerWarnCount;
                        shouldWarn = true;
                    }
                }
                if (shouldWarn)
                {
                    m_host.log(LogLevel::Warning,
                               '[' + m_bindings.serviceName + "] failed to load image header");
                }
            }

            IopHost &m_host;
            SdrdrvBindings m_bindings;
            std::array<uint32_t, 1> m_sids;
            mutable std::mutex m_mutex;
            uint32_t m_headerWarnCount = 0;
            uint32_t m_bodyWarnCount = 0;
        };
    }

    std::unique_ptr<IopService> createSdrdrvService(IopHost &host,
                                                    SdrdrvBindings bindings)
    {
        const uint32_t largestWord = std::max({bindings.lbnWord,
                                               bindings.byteCountWord,
                                               bindings.destinationWord,
                                               bindings.destinationKindWord,
                                               bindings.loadIdWord});
        if (bindings.serviceName.empty() ||
            bindings.sid == 0u ||
            bindings.imageHeaderAddress == 0u ||
            bindings.commandBytes == 0u ||
            (bindings.commandBytes % sizeof(uint32_t)) != 0u ||
            largestWord >= bindings.commandBytes / sizeof(uint32_t) ||
            bindings.maxCommands == 0u ||
            bindings.sectorSize == 0u ||
            bindings.statusStride == 0u ||
            bindings.initFunction == bindings.submitFunction ||
            bindings.initFunction == bindings.shutdownFunction ||
            bindings.submitFunction == bindings.shutdownFunction ||
            bindings.headerCommand == bindings.loadCommand ||
            (bindings.imageHeaderLowerName.empty() &&
             bindings.imageHeaderUpperName.empty()) ||
            (bindings.imageBodyLowerName.empty() &&
             bindings.imageBodyUpperName.empty() &&
             !bindings.fallbackBodyToCdImage))
        {
            throw std::invalid_argument("invalid SDRDRV bindings");
        }
        return std::make_unique<SdrdrvService>(host, std::move(bindings));
    }
}
