#include "module_factories.h"

#include <array>
#include <cstdint>
#include <mutex>
#include <sstream>

namespace ps2x::iop::detail
{
    namespace
    {
        class DuelistsRpcProbeService final : public IopService
        {
        public:
            explicit DuelistsRpcProbeService(IopHost &host)
                : m_host(host)
            {
            }

            [[nodiscard]] std::string_view name() const override
            {
                return "Duelists custom RPC probe";
            }

            [[nodiscard]] std::span<const uint32_t> sids() const override
            {
                return kSids;
            }

            void reset() override
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_handledCalls = 0u;
                m_f002Calls = 0u;
                m_registrationCount = 0u;
                m_rangeRegistrationCount = 0u;
                m_rejectedCalls = 0u;
                m_lastFunction = 0u;
                m_lastSendAddress = 0u;
                m_lastSendSize = 0u;
                m_lastReceiveAddress = 0u;
                m_lastReceiveSize = 0u;
                m_lastSendReadable = false;
                m_lastWords = {};
                m_selfToken = 0u;
                m_lastRegistrationIndex = 0u;
                m_lastRegistrationPointer = 0u;
                m_registeredRangeBase = 0u;
                m_registeredRangeSize = 0u;
                m_registry = {};
            }

            [[nodiscard]] RpcResult handleRpc(const RpcRequest &request) override
            {
                if (request.sid != kSid)
                {
                    return {};
                }

                std::array<uint32_t, 4> words{};
                const uint32_t readableWords =
                    request.send.address == 0u ? 0u :
                    (request.send.size / sizeof(uint32_t) < words.size()
                         ? request.send.size / sizeof(uint32_t)
                         : static_cast<uint32_t>(words.size()));
                bool wordsReadable = request.send.address != 0u && readableWords == 0u;
                if (readableWords != 0u)
                {
                    wordsReadable = m_host.readGuest(request.send.address,
                                                     words.data(),
                                                     readableWords * sizeof(uint32_t));
                }

                const bool validTypedEnvelope =
                    request.send.address != 0u &&
                    request.send.size == kTypedSendSize &&
                    request.receive.address != 0u &&
                    request.receive.size == kTypedReceiveSize;
                const bool validF005 =
                    request.function == kF005Function &&
                    validTypedEnvelope;
                if (validF005 &&
                    m_host.zeroGuest(request.receive.address, request.receive.size))
                {
                    {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        ++m_handledCalls;
                        recordRequest(request, words, wordsReadable);
                    }

                    RpcResult result;
                    result.handled = true;
                    result.resultAddress = request.receive.address;
                    return result;
                }

                const bool validF002 =
                    request.function == kF002Function &&
                    validTypedEnvelope &&
                    wordsReadable &&
                    readableWords >= 2u &&
                    words[1] != 0u;
                std::array<uint32_t, 4> f002Response{};
                f002Response[0] = words[1];
                if (validF002 &&
                    m_host.writeGuest(request.receive.address,
                                      f002Response.data(),
                                      sizeof(f002Response)))
                {
                    {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        ++m_handledCalls;
                        ++m_f002Calls;
                        recordRequest(request, words, wordsReadable);
                    }

                    RpcResult result;
                    result.handled = true;
                    result.resultAddress = request.receive.address;
                    return result;
                }

                const bool structurallyValid5F10 =
                    request.function == k5F10Function &&
                    validTypedEnvelope &&
                    wordsReadable &&
                    readableWords >= 3u &&
                    words[0] != 0u &&
                    words[1] < m_registry.size() &&
                    words[2] != 0u;
                if (structurallyValid5F10)
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    if ((m_selfToken == 0u || m_selfToken == words[0]) &&
                        m_host.zeroGuest(request.receive.address, request.receive.size))
                    {
                        ++m_handledCalls;
                        ++m_registrationCount;
                        m_selfToken = words[0];
                        m_registry[words[1]] = words[2];
                        m_lastRegistrationIndex = words[1];
                        m_lastRegistrationPointer = words[2];
                        recordRequest(request, words, wordsReadable);

                        RpcResult result;
                        result.handled = true;
                        result.resultAddress = request.receive.address;
                        return result;
                    }
                }

                const bool structurallyValid5F12 =
                    request.function == k5F12Function &&
                    validTypedEnvelope &&
                    wordsReadable &&
                    readableWords >= 3u &&
                    words[0] != 0u &&
                    words[1] != 0u &&
                    words[2] != 0u &&
                    words[1] <= UINT32_MAX - words[2];
                if (structurallyValid5F12)
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    if (m_selfToken != 0u &&
                        words[0] == m_selfToken &&
                        m_host.zeroGuest(request.receive.address, request.receive.size))
                    {
                        ++m_handledCalls;
                        ++m_rangeRegistrationCount;
                        m_registeredRangeBase = words[1];
                        m_registeredRangeSize = words[2];
                        recordRequest(request, words, wordsReadable);

                        RpcResult result;
                        result.handled = true;
                        result.resultAddress = request.receive.address;
                        return result;
                    }
                }

                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    ++m_rejectedCalls;
                    recordRequest(request, words, wordsReadable);
                }

                std::ostringstream message;
                message << "Duelists custom RPC observed but not emulated:"
                        << " sid=0x" << std::hex << request.sid
                        << " function=0x" << request.function
                        << " send=0x" << request.send.address << "/0x" << request.send.size
                        << " receive=0x" << request.receive.address << "/0x" << request.receive.size
                        << " words=";
                if (!wordsReadable)
                {
                    message << "<unreadable>";
                }
                for (uint32_t index = 0u; wordsReadable && index < readableWords; ++index)
                {
                    if (index != 0u)
                    {
                        message << ',';
                    }
                    message << "0x" << words[index];
                }
                m_host.log(LogLevel::Warning, message.str());

                // Binding is intentional, but protocol behavior is not yet characterized.
                // Leave the receive buffer untouched and let the caller observe rejection.
                return {};
            }

            void appendDebugMetrics(std::vector<DebugMetric> &metrics) const override
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                metrics.push_back({"handled_calls", m_handledCalls, false});
                metrics.push_back({"f002_calls", m_f002Calls, false});
                metrics.push_back({"registration_count", m_registrationCount, false});
                metrics.push_back({"range_registration_count", m_rangeRegistrationCount, false});
                metrics.push_back({"registered_range_base", m_registeredRangeBase, true});
                metrics.push_back({"registered_range_size", m_registeredRangeSize, false});
                metrics.push_back({"self_token", m_selfToken, true});
                metrics.push_back({"last_registration_index", m_lastRegistrationIndex, false});
                metrics.push_back({"last_registration_pointer", m_lastRegistrationPointer, true});
                metrics.push_back({"rejected_calls", m_rejectedCalls, false});
                metrics.push_back({"last_function", m_lastFunction, true});
                metrics.push_back({"last_send_address", m_lastSendAddress, true});
                metrics.push_back({"last_send_size", m_lastSendSize, false});
                metrics.push_back({"last_receive_address", m_lastReceiveAddress, true});
                metrics.push_back({"last_receive_size", m_lastReceiveSize, false});
                metrics.push_back({"last_send_readable", m_lastSendReadable ? 1u : 0u, false});
                for (size_t index = 0u; index < m_lastWords.size(); ++index)
                {
                    metrics.push_back({"last_word_" + std::to_string(index),
                                       m_lastWords[index],
                                       true});
                }
            }

        private:
            void recordRequest(const RpcRequest &request,
                               const std::array<uint32_t, 4> &words,
                               bool sendReadable)
            {
                m_lastFunction = request.function;
                m_lastSendAddress = request.send.address;
                m_lastSendSize = request.send.size;
                m_lastReceiveAddress = request.receive.address;
                m_lastReceiveSize = request.receive.size;
                m_lastSendReadable = sendReadable;
                m_lastWords = words;
            }

            static constexpr uint32_t kSid = 0x05730601u;
            static constexpr uint32_t kF002Function = 0xF002u;
            static constexpr uint32_t kF005Function = 0xF005u;
            static constexpr uint32_t k5F10Function = 0x5F10u;
            static constexpr uint32_t k5F12Function = 0x5F12u;
            static constexpr uint32_t kTypedSendSize = 0x40u;
            static constexpr uint32_t kTypedReceiveSize = 0x10u;
            inline static constexpr std::array<uint32_t, 1> kSids{kSid};

            IopHost &m_host;
            mutable std::mutex m_mutex;
            uint64_t m_handledCalls = 0u;
            uint64_t m_f002Calls = 0u;
            uint64_t m_registrationCount = 0u;
            uint64_t m_rangeRegistrationCount = 0u;
            uint64_t m_rejectedCalls = 0u;
            uint32_t m_lastRegistrationIndex = 0u;
            uint32_t m_lastRegistrationPointer = 0u;
            uint32_t m_selfToken = 0u;
            uint32_t m_registeredRangeBase = 0u;
            uint32_t m_registeredRangeSize = 0u;
            uint32_t m_lastFunction = 0u;
            uint32_t m_lastSendAddress = 0u;
            uint32_t m_lastSendSize = 0u;
            uint32_t m_lastReceiveAddress = 0u;
            uint32_t m_lastReceiveSize = 0u;
            bool m_lastSendReadable = false;
            std::array<uint32_t, 4> m_lastWords{};
            std::array<uint32_t, 12> m_registry{};
        };
    }

    std::unique_ptr<IopService> createDuelistsRpcProbeService(IopHost &host)
    {
        return std::make_unique<DuelistsRpcProbeService>(host);
    }
}
