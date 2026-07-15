#include "module_factories.h"

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
        constexpr uint32_t kDbcManVersion = 0x0320u;
        constexpr uint32_t kMaxUnknownRpcLogs = 32u;

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

            void reset() override
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_unknownRpcLogCount = 0u;
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
                    const uint32_t wordCount = request.receive.size / sizeof(uint32_t);
                    const uint32_t count = wordCount < 4u ? wordCount : 4u;
                    for (uint32_t index = 0u; index < count; ++index)
                    {
                        const uint32_t address = request.receive.address + index * sizeof(uint32_t);
                        (void)m_host.writeGuest(address, &kDbcManVersion, sizeof(kDbcManVersion));
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
                metrics.push_back({"unknown_rpc_logs", m_unknownRpcLogCount, false});
            }

        private:
            inline static constexpr std::array<uint32_t, 1> kSids{kDbcManSid};

            IopHost &m_host;
            mutable std::mutex m_mutex;
            uint32_t m_unknownRpcLogCount = 0u;
        };
    }

    std::unique_ptr<IopService> createDbcmanService(IopHost &host)
    {
        return std::make_unique<DbcmanService>(host);
    }
}
