#include "module_factories.h"

#include <array>
#include <cstdint>
#include <mutex>
#include <utility>

namespace ps2x::iop::detail
{
    namespace
    {
        class CdvdService final : public IopService
        {
        public:
            CdvdService(IopHost &host, CdvdBindings bindings)
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
                m_diskReadyCalls = 0u;
            }

            [[nodiscard]] RpcResult handleRpc(const RpcRequest &request) override
            {
                if (request.sid != m_bindings.sid ||
                    request.function != m_bindings.diskReadyFunction)
                {
                    return {};
                }

                RpcResult result;
                result.handled = true;
                result.resultAddress = request.receive.address;

                if (request.send.address == 0u ||
                    request.send.size < sizeof(uint32_t) ||
                    request.receive.address == 0u ||
                    request.receive.size < sizeof(m_bindings.readyStatus))
                {
                    return result;
                }

                if (m_host.writeGuest(request.receive.address,
                                      &m_bindings.readyStatus,
                                      sizeof(m_bindings.readyStatus)))
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    ++m_diskReadyCalls;
                }
                return result;
            }

            void appendDebugMetrics(std::vector<DebugMetric> &metrics) const override
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                metrics.push_back({"disk_ready_calls", m_diskReadyCalls, false});
            }

        private:
            IopHost &m_host;
            CdvdBindings m_bindings;
            std::array<uint32_t, 1> m_sids;
            mutable std::mutex m_mutex;
            uint64_t m_diskReadyCalls = 0u;
        };
    }

    std::unique_ptr<IopService> createCdvdService(IopHost &host,
                                                  CdvdBindings bindings)
    {
        return std::make_unique<CdvdService>(host, std::move(bindings));
    }
}
