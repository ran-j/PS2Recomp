#include "module_factories.h"

#include <array>
#include <cstdint>
#include <map>
#include <mutex>

namespace ps2x::iop::detail
{
    namespace
    {
        class IopHeapService final : public IopService
        {
        public:
            explicit IopHeapService(IopHost &host)
                : m_host(host)
            {
            }

            [[nodiscard]] std::string_view name() const override
            {
                return "IOP heap";
            }

            [[nodiscard]] std::span<const uint32_t> sids() const override
            {
                return kSids;
            }

            void reset() override
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_allocations.clear();
                m_allocateCalls = 0u;
                m_freeCalls = 0u;
                m_failedCalls = 0u;
                m_rejectedCalls = 0u;
            }

            [[nodiscard]] RpcResult handleRpc(const RpcRequest &request) override
            {
                if (request.send.address == 0u ||
                    request.send.size != sizeof(uint32_t) ||
                    request.receive.address == 0u ||
                    request.receive.size != sizeof(uint32_t))
                {
                    reject();
                    return {};
                }

                uint32_t argument = 0u;
                if (!m_host.readGuest(request.send.address, &argument, sizeof(argument)))
                {
                    reject();
                    return {};
                }

                int32_t response = -1;
                if (request.function == kAllocateFunction)
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    response = static_cast<int32_t>(allocateLocked(argument));
                    ++m_allocateCalls;
                    if (response == 0)
                    {
                        ++m_failedCalls;
                    }
                }
                else if (request.function == kFreeFunction)
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    const auto allocation = m_allocations.find(argument);
                    if (allocation != m_allocations.end())
                    {
                        m_allocations.erase(allocation);
                        response = 0;
                    }
                    else
                    {
                        ++m_failedCalls;
                    }
                    ++m_freeCalls;
                }
                else
                {
                    reject();
                    return {};
                }

                if (!m_host.writeGuest(request.receive.address, &response, sizeof(response)))
                {
                    reject();
                    return {};
                }

                RpcResult result;
                result.handled = true;
                result.resultAddress = request.receive.address;
                result.signalNowaitCompletion = request.mode != 0u;
                return result;
            }

            void appendDebugMetrics(std::vector<DebugMetric> &metrics) const override
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                metrics.push_back({"allocate_calls", m_allocateCalls, false});
                metrics.push_back({"free_calls", m_freeCalls, false});
                metrics.push_back({"failed_calls", m_failedCalls, false});
                metrics.push_back({"rejected_calls", m_rejectedCalls, false});
                metrics.push_back({"active_allocations", m_allocations.size(), false});
                uint64_t allocatedBytes = 0u;
                for (const auto &[address, size] : m_allocations)
                {
                    (void)address;
                    allocatedBytes += size;
                }
                metrics.push_back({"allocated_bytes", allocatedBytes, false});
            }

        private:
            static constexpr uint32_t kSid = 0x80000003u;
            static constexpr uint32_t kAllocateFunction = 1u;
            static constexpr uint32_t kFreeFunction = 2u;
            static constexpr uint32_t kHeapBase = 0x01A00000u;
            static constexpr uint32_t kHeapLimit = 0x01F00000u;
            static constexpr uint32_t kAlignment = 64u;
            inline static constexpr std::array<uint32_t, 1> kSids{kSid};

            [[nodiscard]] static uint32_t alignSize(uint32_t size)
            {
                if (size == 0u || size > UINT32_MAX - (kAlignment - 1u))
                {
                    return 0u;
                }
                return (size + (kAlignment - 1u)) & ~(kAlignment - 1u);
            }

            [[nodiscard]] uint32_t allocateLocked(uint32_t requestedSize)
            {
                const uint32_t size = alignSize(requestedSize);
                if (size == 0u)
                {
                    return 0u;
                }

                uint32_t candidate = kHeapBase;
                for (const auto &[address, allocatedSize] : m_allocations)
                {
                    if (static_cast<uint64_t>(candidate) + size <= address)
                    {
                        break;
                    }

                    const uint64_t blockEnd =
                        static_cast<uint64_t>(address) + allocatedSize;
                    if (blockEnd > candidate)
                    {
                        if (blockEnd > UINT32_MAX)
                        {
                            return 0u;
                        }
                        candidate = static_cast<uint32_t>(blockEnd);
                    }
                }

                if (candidate < kHeapBase ||
                    static_cast<uint64_t>(candidate) + size > kHeapLimit)
                {
                    return 0u;
                }

                m_allocations.emplace(candidate, size);
                return candidate;
            }

            void reject()
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                ++m_rejectedCalls;
            }

            IopHost &m_host;
            mutable std::mutex m_mutex;
            std::map<uint32_t, uint32_t> m_allocations;
            uint64_t m_allocateCalls = 0u;
            uint64_t m_freeCalls = 0u;
            uint64_t m_failedCalls = 0u;
            uint64_t m_rejectedCalls = 0u;
        };
    }

    std::unique_ptr<IopService> createIopHeapService(IopHost &host)
    {
        return std::make_unique<IopHeapService>(host);
    }
}
