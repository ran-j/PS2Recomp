#include "module_factories.h"

#include <array>
#include <cstdint>
#include <mutex>
#include <sstream>

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
                std::lock_guard<std::mutex> lock(m_mutex);
                m_initCalls = 0u;
                m_rejectedCalls = 0u;
                m_lastFunction = 0u;
                m_lastSendSize = 0u;
                m_lastReceiveSize = 0u;
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
                metrics.push_back({"rejected_calls", m_rejectedCalls, false});
                metrics.push_back({"last_function", m_lastFunction, true});
                metrics.push_back({"last_send_size", m_lastSendSize, false});
                metrics.push_back({"last_receive_size", m_lastReceiveSize, false});
            }

        private:
            static constexpr uint32_t kSid = 0x80000001u;
            static constexpr uint32_t kInitFunction = 0xFFu;
            static constexpr uint32_t kInitPointerAlignment = 0x40u;
            inline static constexpr std::array<uint32_t, 1> kSids{kSid};

            IopHost &m_host;
            mutable std::mutex m_mutex;
            uint64_t m_initCalls = 0u;
            uint64_t m_rejectedCalls = 0u;
            uint32_t m_lastFunction = 0u;
            uint32_t m_lastSendSize = 0u;
            uint32_t m_lastReceiveSize = 0u;
        };
    }

    std::unique_ptr<IopService> createFileIoService(IopHost &host)
    {
        return std::make_unique<FileIoService>(host);
    }
}
