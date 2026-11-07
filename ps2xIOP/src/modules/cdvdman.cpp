#include "module_factories.h"
#include "../rpc_reply.h"

#include <array>
#include <cstdint>
#include <mutex>
#include <sstream>
#include <string_view>

namespace ps2x::iop::detail
{
    namespace
    {
        // These endpoints are provided by the PS2 ROM's cdvdman/cdvdfsv
        // services. They must be bindable before a game loads a disc IRX.
        constexpr uint32_t kCcmdSid = 0x80000592u;
        constexpr uint32_t kScmdSid = 0x80000593u;
        constexpr uint32_t kStcmdSid = 0x80000595u;
        constexpr uint32_t kSearchFileSid = 0x8000059Au;

        bool isObservedBootCommand(const RpcRequest &request)
        {
            // Known libcdvd initialization handshake observed in PS2Recomp.
            return request.sid == kScmdSid && request.function == 0x27u;
        }

        void writeBootHandshakeReply(IopHost &host, const RpcRequest &request)
        {
            // The caller polls the first result word. The remaining words are
            // neutral drive-state values until a full CDVD device model exists.
            const std::array<uint32_t, 3> words{1u, 0u, 0u};
            (void)writeRpcWords(host, request.receive, words);
        }

        class CdvdmanService final : public IopService
        {
        public:
            explicit CdvdmanService(IopHost &host) : m_host(host) {}

            [[nodiscard]] std::string_view name() const override { return "cdvdman"; }

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
                RpcResult response{};
                response.handled = true;
                response.resultAddress = request.receive.address;

                if (isObservedBootCommand(request))
                {
                    writeBootHandshakeReply(m_host, request);
                }
                else
                {
                    logUnknown(request);
                    if (request.receive.address != 0u && request.receive.size != 0u)
                    {
                        (void)m_host.zeroGuest(request.receive.address, request.receive.size);
                    }
                }
                return response;
            }

            void appendDebugMetrics(std::vector<DebugMetric> &metrics) const override
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                metrics.push_back({"unknown_rpc_logs", m_unknownRpcLogCount, false});
            }

        private:
            void logUnknown(const RpcRequest &request)
            {
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    if (m_unknownRpcLogCount >= 32u)
                        return;
                    ++m_unknownRpcLogCount;
                }

                std::ostringstream message;
                message << "CDVD unknown RPC sid=0x" << std::hex << request.sid
                        << " function=0x" << request.function
                        << " send=0x" << request.send.address
                        << "/" << std::dec << request.send.size
                        << " recv=0x" << std::hex << request.receive.address
                        << "/" << std::dec << request.receive.size;
                m_host.log(LogLevel::Warning, message.str());
            }

            IopHost &m_host;
            mutable std::mutex m_mutex;
            uint32_t m_unknownRpcLogCount = 0u;
            inline static constexpr std::array<uint32_t, 4> kSids{
                kCcmdSid, kScmdSid, kStcmdSid, kSearchFileSid};
        };
    }

    std::unique_ptr<IopService> createCdvdmanService(IopHost &host)
    {
        return std::make_unique<CdvdmanService>(host);
    }
}
