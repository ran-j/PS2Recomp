#include "module_factories.h"

#include <array>
#include <cstdint>

namespace ps2x::iop::detail
{
    namespace
    {
        constexpr uint32_t kLibSdSid = 0x80000701u;

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

                m_host.audioCommand(request.sid,
                                    request.function,
                                    request.send,
                                    request.receive);

                RpcResult result;
                result.handled = true;
                result.resultAddress = request.receive.address;
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
