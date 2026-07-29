#include "module_factories.h"

#include <array>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <sstream>

namespace ps2x::iop::detail
{
    namespace
    {
        constexpr uint32_t kPadmanCommandSid = 0x80000100u;
        constexpr uint32_t kPadmanConnectionSid = 0x80000101u;
        constexpr uint32_t kPadmanRpcFunction = 1u;
        constexpr uint32_t kPadmanOpenCommand = 0x01u;
        constexpr uint32_t kPadmanInitCommand = 0x10u;
        constexpr uint32_t kPadmanGetModuleVersionCommand = 0x12u;
        constexpr uint32_t kPadmanRpcBufferSize = 0x80u;
        constexpr uint32_t kPadmanModuleVersion = 0x00000422u;
        constexpr uint32_t kPadmanResultOffset = 0x0Cu;
        constexpr uint32_t kPadmanOpenPadPointerOffset = 0x14u;
        constexpr uint32_t kXpadHalfSize = 0x80u;
        constexpr uint32_t kXpadAreaSize = kXpadHalfSize * 2u;
        constexpr uint32_t kXpadAlignment = 0x40u;
        constexpr uint8_t kPadDigitalMode = 0x41u;
        constexpr uint8_t kPadStateStable = 6u;

        struct PadmanSession
        {
            bool open = false;
            uint32_t padArea = 0u;
        };

        std::array<uint8_t, kXpadAreaSize> makeNeutralXpadArea()
        {
            std::array<uint8_t, kXpadAreaSize> area{};
            for (uint32_t halfIndex = 0u; halfIndex < 2u; ++halfIndex)
            {
                uint8_t *const half = area.data() + halfIndex * kXpadHalfSize;
                std::fill(half, half + 32u, 0u);
                half[1] = kPadDigitalMode;
                half[2] = 0xFFu;
                half[3] = 0xFFu;
                half[4] = 0x80u;
                half[5] = 0x80u;
                half[6] = 0x80u;
                half[7] = 0x80u;

                const uint32_t frame = halfIndex;
                const uint32_t length = 8u;
                std::memcpy(half + 88u, &frame, sizeof(frame));
                std::memcpy(half + 96u, &length, sizeof(length));
                half[101] = 4u; // digital controller mode ID
                half[102] = 1u; // standard digital controller model
                half[103] = 1u; // button data is ready
                half[104] = 1u; // one available mode
                half[112] = kPadStateStable;
                half[113] = 0u; // request complete
                half[114] = 1u; // active read task
            }
            return area;
        }

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
                m_sessions = {};
                m_openCount = 0u;
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

                if (validEnvelope &&
                    request.sid == kPadmanCommandSid &&
                    command == kPadmanOpenCommand)
                {
                    std::array<uint32_t, 5> arguments{};
                    if (!m_host.readGuest(request.send.address,
                                          arguments.data(),
                                          sizeof(arguments)))
                    {
                        reject(request, command, true, "unreadable OPEN arguments");
                        return {};
                    }

                    const uint32_t port = arguments[1];
                    const uint32_t slot = arguments[2];
                    const uint32_t padArea = arguments[4];
                    if (port >= m_sessions.size() ||
                        slot != 0u ||
                        padArea == 0u ||
                        (padArea & (kXpadAlignment - 1u)) != 0u)
                    {
                        reject(request, command, true, "invalid OPEN port, slot, or pad area");
                        return {};
                    }

                    const auto neutralArea = makeNeutralXpadArea();
                    if (!m_host.writeGuest(padArea,
                                           neutralArea.data(),
                                           neutralArea.size()))
                    {
                        reject(request, command, true, "OPEN pad area is not writable");
                        return {};
                    }

                    const uint32_t success = 1u;
                    if (!m_host.zeroGuest(request.receive.address, request.receive.size) ||
                        !m_host.writeGuest(request.receive.address + kPadmanResultOffset,
                                           &success,
                                           sizeof(success)) ||
                        !m_host.writeGuest(request.receive.address + kPadmanOpenPadPointerOffset,
                                           &padArea,
                                           sizeof(padArea)))
                    {
                        reject(request, command, true, "OPEN receive buffer is not writable");
                        return {};
                    }

                    {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        m_sessions[port] = {true, padArea};
                        ++m_openCount;
                    }

                    RpcResult result;
                    result.handled = true;
                    result.resultAddress = request.receive.address;
                    return result;
                }

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

                reject(request,
                       command,
                       !validEnvelope,
                       validEnvelope ? "unsupported command" : "malformed envelope");

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
                metrics.push_back({"open_count", m_openCount, false});
                metrics.push_back({"port0_open", m_sessions[0].open ? 1u : 0u, false});
                metrics.push_back({"port1_open", m_sessions[1].open ? 1u : 0u, false});
            }

        private:
            void reject(const RpcRequest &request,
                        uint32_t command,
                        bool malformed,
                        const char *reason)
            {
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    ++m_rejectedCalls;
                    if (malformed)
                    {
                        ++m_malformedCalls;
                    }
                    m_lastSid = request.sid;
                    m_lastFunction = request.function;
                    m_lastCommand = command;
                }

                std::ostringstream message;
                message << "PADMAN RPC rejected: sid=0x" << std::hex << request.sid
                        << " function=0x" << request.function
                        << " command=0x" << command
                        << " reason=" << reason;
                m_host.log(LogLevel::Warning, message.str());
            }

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
            std::array<PadmanSession, 2> m_sessions{};
            uint64_t m_openCount = 0u;
        };
    }

    std::unique_ptr<IopService> createPadmanService(IopHost &host)
    {
        return std::make_unique<PadmanService>(host);
    }
}
