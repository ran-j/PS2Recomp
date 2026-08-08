#include "module_factories.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace ps2x::iop::detail
{
    namespace
    {
        constexpr uint32_t kFileBytes = 32u;
        constexpr uint32_t kPathBytes = 256u;
        constexpr uint32_t kOldPacketBytes = 0x124u;
        constexpr uint32_t kNonDualPacketBytes = 0x128u;
        constexpr uint32_t kDualPacketBytes = 0x12Cu;

        struct PacketLayout
        {
            uint32_t pathOffset = 0u;
            uint32_t destinationOffset = 0u;
        };

        bool packetLayout(uint32_t size, PacketLayout &layout)
        {
            if (size == kOldPacketBytes)
            {
                layout = {0x20u, 0x120u};
                return true;
            }
            if (size == kNonDualPacketBytes)
            {
                layout = {0x24u, 0x124u};
                return true;
            }
            if (size >= kDualPacketBytes)
            {
                layout = {0x24u, 0x124u};
                return true;
            }
            return false;
        }

        std::string stripIsoVersion(std::string value)
        {
            const size_t semicolon = value.find(';');
            if (semicolon == std::string::npos || semicolon + 1u == value.size())
            {
                return value;
            }
            const bool numeric = std::all_of(
                value.begin() + static_cast<std::ptrdiff_t>(semicolon + 1u),
                value.end(),
                [](unsigned char character)
                {
                    return std::isdigit(character) != 0;
                });
            if (numeric)
            {
                value.erase(semicolon);
            }
            return value;
        }

        std::string normalizedKey(std::string path)
        {
            std::replace(path.begin(), path.end(), '\\', '/');
            path = stripIsoVersion(std::move(path));
            std::transform(path.begin(), path.end(), path.begin(),
                           [](unsigned char character)
                           {
                               return static_cast<char>(std::tolower(character));
                           });
            return path;
        }

        bool safeCdPath(std::string_view path)
        {
            if (path.empty())
            {
                return false;
            }

            std::string normalized(path);
            std::replace(normalized.begin(), normalized.end(), '\\', '/');
            std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                           [](unsigned char character)
                           {
                               return static_cast<char>(std::tolower(character));
                           });

            constexpr std::string_view cdrom0 = "cdrom0:";
            constexpr std::string_view cdrom = "cdrom:";
            if (normalized.starts_with(cdrom0))
            {
                normalized.erase(0u, cdrom0.size());
            }
            else if (normalized.starts_with(cdrom))
            {
                normalized.erase(0u, cdrom.size());
            }
            else if (normalized.find(':') != std::string::npos)
            {
                return false;
            }

            size_t begin = 0u;
            while (begin < normalized.size())
            {
                while (begin < normalized.size() && normalized[begin] == '/')
                {
                    ++begin;
                }
                const size_t end = normalized.find('/', begin);
                const std::string_view component(
                    normalized.data() + begin,
                    (end == std::string::npos ? normalized.size() : end) - begin);
                if (component == "..")
                {
                    return false;
                }
                if (end == std::string::npos)
                {
                    break;
                }
                begin = end + 1u;
            }
            return true;
        }

        std::string leafName(std::string path)
        {
            std::replace(path.begin(), path.end(), '\\', '/');
            const size_t separator = path.find_last_of('/');
            if (separator != std::string::npos)
            {
                path.erase(0u, separator + 1u);
            }
            return stripIsoVersion(std::move(path));
        }

        class CdvdSearchFileService final : public IopService
        {
        public:
            CdvdSearchFileService(IopHost &host, CdvdSearchFileBindings bindings)
                : m_host(host), m_bindings(std::move(bindings)),
                  m_sids{m_bindings.sid}
            {
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
                m_files.clear();
                m_searchCalls = 0u;
                m_foundFiles = 0u;
            }

            [[nodiscard]] RpcResult handleRpc(const RpcRequest &request) override
            {
                if (request.sid != m_bindings.sid ||
                    request.function != m_bindings.searchFunction)
                {
                    return {};
                }

                RpcResult result;
                result.handled = true;
                result.resultAddress = request.receive.address;

                int32_t response = 0;
                if (request.receive.address == 0u ||
                    request.receive.size < sizeof(response))
                {
                    return result;
                }
                (void)m_host.writeGuest(request.receive.address,
                                        &response,
                                        sizeof(response));

                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    ++m_searchCalls;
                }

                PacketLayout layout;
                if (request.send.address == 0u ||
                    !packetLayout(request.send.size, layout))
                {
                    return result;
                }

                std::array<char, kPathBytes> pathBytes{};
                uint32_t destination = 0u;
                if (!m_host.readGuest(request.send.address + layout.pathOffset,
                                      pathBytes.data(),
                                      pathBytes.size()) ||
                    !m_host.readGuest(request.send.address + layout.destinationOffset,
                                      &destination,
                                      sizeof(destination)))
                {
                    return result;
                }

                const auto terminator =
                    std::find(pathBytes.begin(), pathBytes.end(), '\0');
                if (terminator == pathBytes.end())
                {
                    return result;
                }
                const std::string path(pathBytes.begin(), terminator);
                if (destination == 0u || !safeCdPath(path))
                {
                    return result;
                }

                CdFileInfo file;
                if (!m_host.searchCdFile(path, file))
                {
                    return result;
                }
                const std::string key = normalizedKey(path);
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_files.emplace(key);
                }

                std::array<uint8_t, kFileBytes> packed{};
                std::copy_n(reinterpret_cast<const uint8_t *>(&file.lsn),
                            sizeof(file.lsn), packed.begin());
                std::copy_n(reinterpret_cast<const uint8_t *>(&file.size),
                            sizeof(file.size), packed.begin() + 4u);
                const std::string leaf =
                    file.name.empty() ? leafName(path) : file.name;
                std::copy_n(leaf.begin(), std::min<size_t>(leaf.size(), 15u),
                            packed.begin() + 8u);
                std::copy(file.date.begin(), file.date.end(), packed.begin() + 24u);

                if (!m_host.writeGuest(destination, packed.data(), packed.size()))
                {
                    return result;
                }

                response = 1;
                if (m_host.writeGuest(request.receive.address,
                                      &response,
                                      sizeof(response)))
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    ++m_foundFiles;
                }
                return result;
            }

            void appendDebugMetrics(std::vector<DebugMetric> &metrics) const override
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                metrics.push_back({"search_calls", m_searchCalls, false});
                metrics.push_back({"search_hits", m_foundFiles, false});
                metrics.push_back({"mapped_files", m_files.size(), false});
            }

        private:
            IopHost &m_host;
            CdvdSearchFileBindings m_bindings;
            std::array<uint32_t, 1> m_sids;
            mutable std::mutex m_mutex;
            std::unordered_set<std::string> m_files;
            uint64_t m_searchCalls = 0u;
            uint64_t m_foundFiles = 0u;
        };
    }

    std::unique_ptr<IopService> createCdvdSearchFileService(
        IopHost &host,
        CdvdSearchFileBindings bindings)
    {
        return std::make_unique<CdvdSearchFileService>(host, std::move(bindings));
    }
}
