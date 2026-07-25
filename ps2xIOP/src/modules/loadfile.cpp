#include "module_factories.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace ps2x::iop::detail
{
    namespace
    {
        constexpr uint32_t kLoadFileSid = 0x80000006u;
        constexpr uint32_t kLoadModule = 0x00u;
        constexpr uint32_t kGetVersion = 0xFFu;
        constexpr uint32_t kLoadFileVersion = 0x30333432u;
        constexpr uint32_t kModulePathOffset = 8u;
        constexpr uint32_t kModulePathBytes = 252u;

        struct LoadModuleResult
        {
            int32_t moduleId;
            int32_t startResult;
        };

        std::string moduleName(std::string path)
        {
            std::transform(path.begin(), path.end(), path.begin(), [](unsigned char value)
                           { return static_cast<char>(std::toupper(value)); });
            const size_t separator = path.find_last_of("\\/");
            if (separator != std::string::npos)
            {
                path.erase(0u, separator + 1u);
            }
            const size_t version = path.find(';');
            if (version != std::string::npos)
            {
                path.erase(version);
            }
            return path;
        }

        bool isHleModule(std::string_view name)
        {
            static constexpr std::array<std::string_view, 8> modules{
                "SIO2MAN.IRX",
                "PADMAN.IRX",
                "MCMAN.IRX",
                "MCSERV.IRX",
                "LIBSD.IRX",
                "SDRDRV.IRX",
                "KCEJEAST.IRX",
                "LIBSD",
            };
            return std::find(modules.begin(), modules.end(), name) != modules.end();
        }

        class LoadFileService final : public IopService
        {
        public:
            explicit LoadFileService(IopHost &host)
                : m_host(host)
            {
            }

            [[nodiscard]] std::string_view name() const override
            {
                return "LOADFILE";
            }

            [[nodiscard]] std::span<const uint32_t> sids() const override
            {
                return kSids;
            }

            void reset() override
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_loadedModules.clear();
                m_nextModuleId = 1;
                m_loadCalls = 0u;
            }

            [[nodiscard]] RpcResult handleRpc(const RpcRequest &request) override
            {
                if (request.sid != kLoadFileSid)
                {
                    return {};
                }

                RpcResult result;
                result.handled = true;
                result.resultAddress = request.receive.address;

                if (request.function == kGetVersion)
                {
                    if (request.receive.address != 0u &&
                        request.receive.size >= sizeof(kLoadFileVersion))
                    {
                        (void)m_host.writeGuest(request.receive.address,
                                                &kLoadFileVersion,
                                                sizeof(kLoadFileVersion));
                    }
                    return result;
                }

                if (request.function != kLoadModule ||
                    request.send.address == 0u ||
                    request.send.size < kModulePathOffset + 1u ||
                    request.receive.address == 0u ||
                    request.receive.size < sizeof(LoadModuleResult))
                {
                    return result;
                }

                const uint32_t available =
                    std::min(kModulePathBytes, request.send.size - kModulePathOffset);
                std::string path(available, '\0');
                if (!m_host.readGuest(request.send.address + kModulePathOffset,
                                      path.data(),
                                      path.size()))
                {
                    return result;
                }
                const size_t terminator = path.find('\0');
                if (terminator == std::string::npos)
                {
                    return result;
                }
                path.resize(terminator);
                const std::string name = moduleName(path);

                LoadModuleResult response{-1, -1};
                if (isHleModule(name))
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    ++m_loadCalls;
                    const auto [entry, inserted] =
                        m_loadedModules.emplace(name, m_nextModuleId);
                    if (inserted)
                    {
                        ++m_nextModuleId;
                    }
                    response = {entry->second, 0};
                }
                (void)m_host.writeGuest(request.receive.address,
                                        &response,
                                        sizeof(response));
                return result;
            }

            void appendDebugMetrics(std::vector<DebugMetric> &metrics) const override
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                metrics.push_back({"loaded_modules", m_loadedModules.size(), false});
                metrics.push_back({"load_calls", m_loadCalls, false});
            }

        private:
            inline static constexpr std::array<uint32_t, 1> kSids{kLoadFileSid};

            IopHost &m_host;
            mutable std::mutex m_mutex;
            std::unordered_map<std::string, int32_t> m_loadedModules;
            int32_t m_nextModuleId = 1;
            uint64_t m_loadCalls = 0u;
        };
    }

    std::unique_ptr<IopService> createLoadFileService(IopHost &host)
    {
        return std::make_unique<LoadFileService>(host);
    }
}
