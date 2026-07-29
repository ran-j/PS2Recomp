#include "module_factories.h"

#include <array>
#include <cstdint>
#include <mutex>
#include <sstream>

namespace ps2x::iop::detail
{
    namespace
    {
        constexpr uint32_t kPadmanCommandSid = 0x80000100u;
        constexpr uint32_t kPadmanConnectionSid = 0x80000101u;
        constexpr uint32_t kPadmanRpcFunction = 1u;
        constexpr uint32_t kPadmanInitCommand = 0x10u;
        constexpr uint32_t kPadmanGetModuleVersionCommand = 0x12u;
        constexpr uint32_t kPadmanRpcBufferSize = 0x80u;
        constexpr uint32_t kPadmanModuleVersion = 0x00000422u;
        constexpr uint32_t kPadmanResultOffset = 0x0Cu;

        class PadmanService final : public IopService
        {
        public:
            explicit PadmanService(IopHost &host)
                : m_host(host)
            {
            }

            [[nodiscard]] std::string_view name() const override
            {
                return "PADMAN";
            }

            [[nodiscard]] std::span<const uint32_t> sids() const override
            {
                return kSids;
            }

            void reset() override
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_rejectedCalls = 0u;
                m_malformedCalls = 0u;
                m_lastSid = 0u;
                m_lastFunction = 0u;
                m_lastCommand = 0u;
            }

            [[nodiscard]] RpcResult handleRpc(const RpcRequest &request) override
            {
                if (request.sid != kPadmanCommandSid &&
                    request.sid != kPadmanConnectionSid)
                {
                    return {};
                }

                uint32_t command = 0u;
                const bool validEnvelope =
                    request.function == kPadmanRpcFunction &&
                    request.send.address != 0u &&
                    request.send.size == kPadmanRpcBufferSize &&
                    request.receive.address != 0u &&
                    request.receive.size == kPadmanRpcBufferSize &&
                    m_host.readGuest(request.send.address, &command, sizeof(command));

                const bool supportedCommand =
                    command == kPadmanInitCommand ||
                    command == kPadmanGetModuleVersionCommand;
                if (validEnvelope &&
                    request.sid == kPadmanCommandSid &&
                    supportedCommand)
                {
                    const uint32_t response =
                        command == kPadmanInitCommand ? 1u : kPadmanModuleVersion;
                    if (!m_host.zeroGuest(request.receive.address, request.receive.size) ||
                        !m_host.writeGuest(request.receive.address + kPadmanResultOffset,
                                           &response,
                                           sizeof(response)))
                    {
                        m_host.log(LogLevel::Warning, "PADMAN RPC rejected: invalid receive buffer");
                        return {};
                    }

                    RpcResult result;
                    result.handled = true;
                    result.resultAddress = request.receive.address;
                    return result;
                }

                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    ++m_rejectedCalls;
                    if (!validEnvelope)
                    {
                        ++m_malformedCalls;
                    }
                    m_lastSid = request.sid;
                    m_lastFunction = request.function;
                    m_lastCommand = validEnvelope ? command : 0u;
                }

                std::ostringstream message;
                message << "PADMAN RPC rejected: sid=0x" << std::hex << request.sid
                        << " function=0x" << request.function;
                if (validEnvelope)
                {
                    message << " unsupported-command=0x" << command;
                }
                else
                {
                    message << " malformed-envelope";
                }
                m_host.log(LogLevel::Warning, message.str());

                // Phase one intentionally advertises the real PADMAN endpoints so
                // clients can bind, but does not claim or manufacture command
                // responses until each command's ABI and state effects are modeled.
                return {};
            }

            void appendDebugMetrics(std::vector<DebugMetric> &metrics) const override
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                metrics.push_back({"rejected_calls", m_rejectedCalls, false});
                metrics.push_back({"malformed_calls", m_malformedCalls, false});
                metrics.push_back({"last_sid", m_lastSid, true});
                metrics.push_back({"last_function", m_lastFunction, true});
                metrics.push_back({"last_command", m_lastCommand, true});
            }

        private:
            inline static constexpr std::array<uint32_t, 2> kSids{
                kPadmanCommandSid,
                kPadmanConnectionSid,
            };

            IopHost &m_host;
            mutable std::mutex m_mutex;
            uint64_t m_rejectedCalls = 0u;
            uint64_t m_malformedCalls = 0u;
            uint32_t m_lastSid = 0u;
            uint32_t m_lastFunction = 0u;
            uint32_t m_lastCommand = 0u;
        };
    }

    std::unique_ptr<IopService> createPadmanService(IopHost &host)
    {
        return std::make_unique<PadmanService>(host);
    }
}
