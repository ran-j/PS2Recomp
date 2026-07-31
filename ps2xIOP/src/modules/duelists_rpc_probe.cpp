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
                m_function5000Calls = 0u;
                m_function5002Calls = 0u;
                m_function5003Calls = 0u;
                m_function5005Calls = 0u;
                m_f003Calls = 0u;
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
                m_last5005Argument0 = 0u;
                m_last5005Argument1 = 0u;
                m_last5003FadeStep = 0u;
                m_lastF003Enabled = 0u;
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

                const bool structurallyValid5000 =
                    request.function == k5000Function &&
                    request.mode == kNowaitMode &&
                    validTypedEnvelope &&
                    wordsReadable &&
                    readableWords >= 1u &&
                    words[0] != 0u &&
                    (words[0] & (kSelfTokenAlignment - 1u)) == 0u;
                if (structurallyValid5000)
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    if (m_selfToken != 0u &&
                        words[0] == m_selfToken &&
                        m_host.zeroGuest(request.receive.address, request.receive.size))
                    {
                        ++m_handledCalls;
                        ++m_function5000Calls;
                        recordRequest(request, words, wordsReadable);

                        RpcResult result;
                        result.handled = true;
                        result.resultAddress = request.receive.address;
                        result.signalNowaitCompletion = true;
                        return result;
                    }
                }

                const bool structurallyValid5002 =
                    request.function == k5002Function &&
                    request.mode == kNowaitMode &&
                    validTypedEnvelope &&
                    wordsReadable &&
                    readableWords >= 1u &&
                    words[0] != 0u &&
                    (words[0] & (kSelfTokenAlignment - 1u)) == 0u;
                if (structurallyValid5002)
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    if (m_selfToken != 0u && words[0] == m_selfToken)
                    {
                        // KCEJEAST returns:
                        // {auto-DMA volume, packed process statuses,
                        //  auto-DMA status, auto-DMA volume}.
                        // None of the characterized commands before the first
                        // 5002 query changes the zero-initialized status.
                        const std::array<uint32_t, 4> idleStatus{};
                        if (m_host.writeGuest(request.receive.address,
                                              idleStatus.data(),
                                              sizeof(idleStatus)))
                        {
                            ++m_handledCalls;
                            ++m_function5002Calls;
                            recordRequest(request, words, wordsReadable);

                            RpcResult result;
                            result.handled = true;
                            result.resultAddress = request.receive.address;
                            result.signalNowaitCompletion = true;
                            return result;
                        }
                    }
                }

                const bool structurallyValid5005 =
                    request.function == k5005Function &&
                    request.mode == kNowaitMode &&
                    validTypedEnvelope &&
                    wordsReadable &&
                    readableWords >= 3u &&
                    words[0] != 0u &&
                    (words[0] & (kSelfTokenAlignment - 1u)) == 0u;
                if (structurallyValid5005)
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    if (m_selfToken != 0u &&
                        words[0] == m_selfToken &&
                        m_host.zeroGuest(request.receive.address, request.receive.size))
                    {
                        ++m_handledCalls;
                        ++m_function5005Calls;
                        m_last5005Argument0 = words[1];
                        m_last5005Argument1 = words[2];
                        recordRequest(request, words, wordsReadable);

                        RpcResult result;
                        result.handled = true;
                        result.resultAddress = request.receive.address;
                        result.signalNowaitCompletion = true;
                        return result;
                    }
                }

                const bool structurallyValid5003 =
                    request.function == k5003Function &&
                    request.mode == kNowaitMode &&
                    validTypedEnvelope &&
                    wordsReadable &&
                    readableWords >= 2u &&
                    words[0] != 0u &&
                    (words[0] & (kSelfTokenAlignment - 1u)) == 0u;
                if (structurallyValid5003)
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    if (m_selfToken != 0u && words[0] == m_selfToken)
                    {
                        // KCEJEAST's 5003 command is SD_vFadeOutAutoDMA.
                        // It normalizes a positive step to negative and leaves
                        // zero/negative values unchanged. The command returns
                        // the service's normal four-word status snapshot.
                        const int32_t requestedStep = static_cast<int32_t>(words[1]);
                        const int32_t fadeStep =
                            requestedStep > 0 ? -requestedStep : requestedStep;
                        const std::array<uint32_t, 4> idleStatus{};
                        if (m_host.writeGuest(request.receive.address,
                                              idleStatus.data(),
                                              sizeof(idleStatus)))
                        {
                            ++m_handledCalls;
                            ++m_function5003Calls;
                            m_last5003FadeStep = static_cast<uint32_t>(fadeStep);
                            recordRequest(request, words, wordsReadable);

                            RpcResult result;
                            result.handled = true;
                            result.resultAddress = request.receive.address;
                            result.signalNowaitCompletion = true;
                            return result;
                        }
                    }
                }

                const bool structurallyValidF003 =
                    request.function == kF003Function &&
                    request.mode == kNowaitMode &&
                    validTypedEnvelope &&
                    wordsReadable &&
                    readableWords >= 2u &&
                    words[0] != 0u &&
                    (words[0] & (kSelfTokenAlignment - 1u)) == 0u &&
                    words[1] <= 1u;
                if (structurallyValidF003)
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    if (m_selfToken != 0u &&
                        words[0] == m_selfToken &&
                        m_host.zeroGuest(request.receive.address, request.receive.size))
                    {
                        ++m_handledCalls;
                        ++m_f003Calls;
                        m_lastF003Enabled = words[1];
                        recordRequest(request, words, wordsReadable);

                        RpcResult result;
                        result.handled = true;
                        result.resultAddress = request.receive.address;
                        result.signalNowaitCompletion = true;
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
                metrics.push_back({"function_5000_calls", m_function5000Calls, false});
                metrics.push_back({"function_5002_calls", m_function5002Calls, false});
                metrics.push_back({"function_5003_calls", m_function5003Calls, false});
                metrics.push_back({"last_5003_fade_step", m_last5003FadeStep, true});
                metrics.push_back({"function_5005_calls", m_function5005Calls, false});
                metrics.push_back({"last_5005_argument_0", m_last5005Argument0, true});
                metrics.push_back({"last_5005_argument_1", m_last5005Argument1, true});
                metrics.push_back({"f003_calls", m_f003Calls, false});
                metrics.push_back({"last_f003_enabled", m_lastF003Enabled, false});
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
            static constexpr uint32_t k5000Function = 0x5000u;
            static constexpr uint32_t k5002Function = 0x5002u;
            static constexpr uint32_t k5003Function = 0x5003u;
            static constexpr uint32_t k5005Function = 0x5005u;
            static constexpr uint32_t kF003Function = 0xF003u;
            static constexpr uint32_t kTypedSendSize = 0x40u;
            static constexpr uint32_t kTypedReceiveSize = 0x10u;
            static constexpr uint32_t kNowaitMode = 1u;
            static constexpr uint32_t kSelfTokenAlignment = 0x40u;
            inline static constexpr std::array<uint32_t, 1> kSids{kSid};

            IopHost &m_host;
            mutable std::mutex m_mutex;
            uint64_t m_handledCalls = 0u;
            uint64_t m_f002Calls = 0u;
            uint64_t m_registrationCount = 0u;
            uint64_t m_rangeRegistrationCount = 0u;
            uint64_t m_function5000Calls = 0u;
            uint64_t m_function5002Calls = 0u;
            uint64_t m_function5003Calls = 0u;
            uint64_t m_function5005Calls = 0u;
            uint64_t m_f003Calls = 0u;
            uint64_t m_rejectedCalls = 0u;
            uint32_t m_lastRegistrationIndex = 0u;
            uint32_t m_lastRegistrationPointer = 0u;
            uint32_t m_selfToken = 0u;
            uint32_t m_registeredRangeBase = 0u;
            uint32_t m_registeredRangeSize = 0u;
            uint32_t m_last5005Argument0 = 0u;
            uint32_t m_last5005Argument1 = 0u;
            uint32_t m_last5003FadeStep = 0u;
            uint32_t m_lastF003Enabled = 0u;
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
