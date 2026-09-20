#include "module_factories.h"
#include "rpc_reply.h"

#include <array>
#include <cstdint>
#include <mutex>
#include <sstream>
#include <string>

namespace ps2x::iop::detail
{
    namespace
    {
        constexpr uint32_t kDbcManSid = 0x80001300u;
        constexpr uint32_t kRpcCheckVersion = 0x80001363u;
        constexpr uint32_t kMaxUnknownRpcLogs = 32u;

        constexpr std::array<uint16_t, 2> kSupportedVersions{0x0310u, 0x0320u};
        constexpr uint16_t kReportedVersion = kSupportedVersions.front();

        class DbcmanService final : public IopService
        {
        public:
            explicit DbcmanService(IopHost &host)
                : m_host(host)
            {
            }

            [[nodiscard]] std::string_view name() const override
            {
                return "dbcman";
            }

            [[nodiscard]] std::span<const uint32_t> sids() const override
            {
                return kSids;
            }

            [[nodiscard]] std::span<const std::string_view> moduleAliases() const override
            {
                return kModuleAliases;
            }

            void reset() override
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_unknownRpcLogCount = 0u;
                m_versionQueryCount = 0u;
                m_failedVersionReplies = 0u;
            }

            [[nodiscard]] RpcResult handleRpc(const RpcRequest &request) override
            {
                RpcResult result;
                if (request.sid != kDbcManSid)
                {
                    return result;
                }

                result.handled = true;
                result.resultAddress = request.receive.address;
                if (request.receive.address == 0u || request.receive.size == 0u)
                {
                    return result;
                }

                if (request.function == kRpcCheckVersion)
                {
                    const uint32_t version = kReportedVersion;
                    const std::array<uint32_t, 4> reply{version, version, version, version};
                    const bool written = writeRpcWords(m_host, request.receive, reply);
                    bool firstQuery = false;
                    {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        firstQuery = m_versionQueryCount++ == 0u;
                        if (!written)
                            ++m_failedVersionReplies;
                    }
                    if (firstQuery)
                    {
                        std::ostringstream message;
                        message << "[DBCMAN:HLE] check-version reply=0x" << std::hex << version;
                        m_host.log(LogLevel::Info, message.str());
                    }
                    return result;
                }

                bool shouldLog = false;
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    if (m_unknownRpcLogCount < kMaxUnknownRpcLogs)
                    {
                        ++m_unknownRpcLogCount;
                        shouldLog = true;
                    }
                }

                if (shouldLog)
                {
                    std::ostringstream message;
                    message << "[DBCMAN:stub]"
                            << " sid=0x" << std::hex << request.sid
                            << " rpc=0x" << request.function
                            << " send=0x" << request.send.address
                            << " sendSize=0x" << request.send.size
                            << " recv=0x" << request.receive.address
                            << " recvSize=0x" << request.receive.size;
                    m_host.log(LogLevel::Info, message.str());
                }
                return result;
            }

            void appendDebugMetrics(std::vector<DebugMetric> &metrics) const override
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                metrics.push_back({"reported_version", kReportedVersion, true});
                metrics.push_back({"version_queries", m_versionQueryCount, false});
                metrics.push_back({"failed_version_replies", m_failedVersionReplies, false});
                metrics.push_back({"unknown_rpc_logs", m_unknownRpcLogCount, false});
            }

        private:
            inline static constexpr std::array<uint32_t, 1> kSids{kDbcManSid};
            inline static constexpr std::array<std::string_view, 3> kModuleAliases{"dbcman", "dbcm", "dbcmserv"};

            IopHost &m_host;
            mutable std::mutex m_mutex;
            uint32_t m_unknownRpcLogCount = 0u;
            uint64_t m_versionQueryCount = 0u;
            uint64_t m_failedVersionReplies = 0u;
        };
    }

    std::unique_ptr<IopService> createDbcmanService(IopHost &host)
    {
        return std::make_unique<DbcmanService>(host);
    }
}
