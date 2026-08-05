#include "module_factories.h"

#include <array>
#include <algorithm>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ps2x::iop::detail
{
    namespace
    {
        constexpr uint16_t kPlayStreamCommand = 1u;
        constexpr uint32_t kResponseRecordStride = 0x20u;
        constexpr uint32_t kPackedStreamOffset = 4u;
        constexpr uint32_t kStreamSlotMask = 0x3Fu;
        constexpr uint32_t kStreamSlotCount = 48u;
        constexpr uint32_t kCommandStreamSlotShift = 8u;
        constexpr uint32_t kResponseStreamSlotShift = 4u;

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
                m_completedStreamCount = 0u;
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

                std::vector<uint32_t> activeStreamSlots;
                if (m_bindings.completeQueuedPlayStreams && request.receive.address != 0u)
                {
                    // PlayStream leaves the EE slot in state 2. One active record moves it
                    // to state 1; the following empty update lets SOUND_CopyIOPBuffer clear it.
                    activeStreamSlots = findQueuedPlayStreams(request);
                    trimToReceiveCapacity(activeStreamSlots, request.receive.size);
                }

                uint32_t counter = 0u;
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    counter = ++m_updateCounter;
                    m_completedStreamCount += activeStreamSlots.size();
                }

                const uint32_t activeStreams = static_cast<uint32_t>(activeStreamSlots.size());
                if (request.receive.address != 0u &&
                    request.receive.size >= m_bindings.activeStreamCountOffset + sizeof(activeStreams))
                {
                    const uint32_t address = request.receive.address + m_bindings.activeStreamCountOffset;
                    (void)m_host.writeGuest(address, &activeStreams, sizeof(activeStreams));
                }

                for (size_t index = 0u; index < activeStreamSlots.size(); ++index)
                {
                    const uint32_t packedStream = activeStreamSlots[index] << kResponseStreamSlotShift;
                    const uint32_t offset = m_bindings.activeStreamCountOffset + static_cast<uint32_t>(index) * kResponseRecordStride + kPackedStreamOffset;
                    const uint32_t address = request.receive.address + offset;
                    (void)m_host.writeGuest(address, &packedStream, sizeof(packedStream));
                }

                const uint32_t counterOffset = m_bindings.responseCounterOffset +
                                               activeStreams * kResponseRecordStride;
                if (request.receive.address != 0u &&
                    request.receive.size >= counterOffset + sizeof(counter))
                {
                    const uint32_t address = request.receive.address + counterOffset;
                    (void)m_host.writeGuest(address, &counter, sizeof(counter));
                }

                return result;
            }

            void appendDebugMetrics(std::vector<DebugMetric> &metrics) const override
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                metrics.push_back({"update_counter", m_updateCounter, false});
                metrics.push_back({"completed_streams", m_completedStreamCount, false});
            }

        private:
            [[nodiscard]] std::vector<uint32_t> findQueuedPlayStreams(const RpcRequest &request) const
            {
                std::vector<uint32_t> slots;
                if (request.send.address == 0u || request.send.size < sizeof(uint16_t))
                {
                    return slots;
                }

                uint16_t commandCount = 0u;
                if (!m_host.readGuest(request.send.address, &commandCount, sizeof(commandCount)))
                {
                    return slots;
                }

                uint32_t offset = sizeof(commandCount);
                for (uint32_t commandIndex = 0u; commandIndex < commandCount; ++commandIndex)
                {
                    constexpr uint32_t headerSize = sizeof(uint16_t) * 2u;
                    if (offset > request.send.size || request.send.size - offset < headerSize)
                    {
                        break;
                    }

                    std::array<uint16_t, 2> header{};
                    if (!m_host.readGuest(request.send.address + offset,
                                          header.data(),
                                          sizeof(header)))
                    {
                        break;
                    }
                    offset += headerSize;

                    const uint32_t argumentBytes =
                        static_cast<uint32_t>(header[1]) * sizeof(uint16_t);
                    if (argumentBytes > request.send.size - offset)
                    {
                        break;
                    }

                    if (header[0] == kPlayStreamCommand && header[1] >= 2u)
                    {
                        uint16_t encodedSlot = 0u;
                        if (m_host.readGuest(request.send.address + offset + sizeof(uint16_t),
                                             &encodedSlot,
                                             sizeof(encodedSlot)))
                        {
                            const uint32_t slot =
                                (encodedSlot >> kCommandStreamSlotShift) & kStreamSlotMask;
                            if (slot < kStreamSlotCount &&
                                std::find(slots.begin(), slots.end(), slot) == slots.end())
                            {
                                slots.push_back(slot);
                            }
                        }
                    }

                    offset += argumentBytes;
                }
                return slots;
            }

            void trimToReceiveCapacity(std::vector<uint32_t> &slots, uint32_t receiveSize) const
            {
                size_t count = 0u;
                for (; count < slots.size(); ++count)
                {
                    const uint64_t recordOffset =
                        static_cast<uint64_t>(m_bindings.activeStreamCountOffset) +
                        static_cast<uint64_t>(count) * kResponseRecordStride +
                        kPackedStreamOffset;
                    const uint64_t counterOffset =
                        static_cast<uint64_t>(m_bindings.responseCounterOffset) +
                        static_cast<uint64_t>(count + 1u) * kResponseRecordStride;
                    if (recordOffset + sizeof(uint32_t) > receiveSize ||
                        counterOffset + sizeof(uint32_t) > receiveSize)
                    {
                        break;
                    }
                }
                slots.resize(count);
            }

            IopHost &m_host;
            SoundUpdateStubBindings m_bindings;
            std::array<uint32_t, 1> m_sids;
            mutable std::mutex m_mutex;
            uint32_t m_updateCounter = 0u;
            uint64_t m_completedStreamCount = 0u;
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
