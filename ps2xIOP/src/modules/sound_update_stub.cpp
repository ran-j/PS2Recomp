#include "module_factories.h"

#include <array>
#include <algorithm>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace ps2x::iop::detail
{
    namespace
    {
        class SoundUpdateStubService final : public IopService
        {
        public:
            SoundUpdateStubService(IopHost &host, SoundUpdateStubBindings bindings)
                : m_host(host), m_bindings(std::move(bindings)), m_sids{m_bindings.sid}
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
                m_updateCounter = 0u;
            }

            [[nodiscard]] RpcResult handleRpc(const RpcRequest &request) override
            {
                if (request.sid != m_bindings.sid)
                {
                    return {};
                }

                RpcResult result;
                result.handled = true;
                result.resultAddress = request.receive.address;
                result.signalNowaitCompletion = m_bindings.signalNowaitCompletion;

                if (std::find(m_bindings.suppressedCompletionCallbacks.begin(),
                              m_bindings.suppressedCompletionCallbacks.end(),
                              request.endFunction) != m_bindings.suppressedCompletionCallbacks.end())
                {
                    result.signalCompletion = true;
                    result.callbackPolicy = CallbackPolicy::Suppress;
                }

                if (m_bindings.zeroReceiveBuffer &&
                    request.receive.address != 0u && request.receive.size != 0u)
                {
                    (void)m_host.zeroGuest(request.receive.address, request.receive.size);
                }

                uint32_t counter = 0u;
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    counter = ++m_updateCounter;
                }

                constexpr uint32_t activeStreams = 0u;
                if (request.receive.address != 0u &&
                    request.receive.size >= m_bindings.activeStreamCountOffset + sizeof(activeStreams))
                {
                    const uint32_t address = request.receive.address + m_bindings.activeStreamCountOffset;
                    (void)m_host.writeGuest(address, &activeStreams, sizeof(activeStreams));
                }

                if (request.receive.address != 0u &&
                    request.receive.size >= m_bindings.responseCounterOffset + sizeof(counter))
                {
                    const uint32_t address = request.receive.address + m_bindings.responseCounterOffset;
                    (void)m_host.writeGuest(address, &counter, sizeof(counter));
                }

                return result;
            }

            void appendDebugMetrics(std::vector<DebugMetric> &metrics) const override
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                metrics.push_back({"update_counter", m_updateCounter, false});
            }

        private:
            IopHost &m_host;
            SoundUpdateStubBindings m_bindings;
            std::array<uint32_t, 1> m_sids;
            mutable std::mutex m_mutex;
            uint32_t m_updateCounter = 0u;
        };
    }

    std::unique_ptr<IopService> createSoundUpdateStubService(IopHost &host,
                                                             SoundUpdateStubBindings bindings)
    {
        if (bindings.serviceName.empty() || bindings.sid == 0u ||
            bindings.activeStreamCountOffset == bindings.responseCounterOffset)
        {
            throw std::invalid_argument("invalid SOUND update stub bindings");
        }
        std::unordered_set<uint32_t> callbacks;
        for (const uint32_t callback : bindings.suppressedCompletionCallbacks)
        {
            if (callback == 0u || !callbacks.emplace(callback).second)
            {
                throw std::invalid_argument("invalid SOUND update callback binding");
            }
        }
        return std::make_unique<SoundUpdateStubService>(host, std::move(bindings));
    }
}
