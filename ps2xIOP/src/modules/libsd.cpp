#include "module_factories.h"

#include <array>
#include <cstdint>

namespace ps2x::iop::detail
{
    namespace
    {
        constexpr uint32_t kLibSdSid = 0x80000701u;
        constexpr uint32_t kSetParamFunction = 0x8010u;
        constexpr uint32_t kRemoteSendSize = 64u;
        constexpr uint32_t kRemoteReceiveSize = 16u;

        class LibSdService final : public IopService
        {
        public:
            explicit LibSdService(IopHost &host)
                : m_host(host)
            {
            }

            [[nodiscard]] std::string_view name() const override
            {
                return "libsd";
            }

            [[nodiscard]] std::span<const uint32_t> sids() const override
            {
                return kSids;
            }

            void reset() override
            {
            }

            [[nodiscard]] RpcResult handleRpc(const RpcRequest &request) override
            {
                if (request.sid != kLibSdSid)
                {
                    return {};
                }

                if (request.function == kSetParamFunction &&
                    (request.send.address == 0u ||
                     request.send.size != kRemoteSendSize ||
                     request.receive.address == 0u ||
                     request.receive.size != kRemoteReceiveSize))
                {
                    return {};
                }

                m_host.audioCommand(request.sid,
                                    request.function,
                                    request.send,
                                    request.receive);
                if (request.function == kSetParamFunction &&
                    !m_host.zeroGuest(request.receive.address,
                                      request.receive.size))
                {
                    return {};
                }

                RpcResult result;
                result.handled = true;
                result.resultAddress = request.receive.address;
                result.signalNowaitCompletion = request.mode != 0u;
                return result;
            }

        private:
            inline static constexpr std::array<uint32_t, 1> kSids{kLibSdSid};

            IopHost &m_host;
        };
    }

    std::unique_ptr<IopService> createLibSdService(IopHost &host)
    {
        return std::make_unique<LibSdService>(host);
    }
}
