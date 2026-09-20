#pragma once

#include "ps2x/iop/iop_host.h"
#include "ps2x/iop/iop_types.h"

#include <memory>
#include <span>
#include <string>
#include <vector>

namespace ps2x::iop::detail
{
    class IopService
    {
    public:
        virtual ~IopService() = default;

        [[nodiscard]] virtual std::string_view name() const = 0;
        [[nodiscard]] virtual std::span<const uint32_t> sids() const = 0;
        // A service with aliases is dormant until one of these IOP modules is
        // actually loaded.
        [[nodiscard]] virtual std::span<const std::string_view> moduleAliases() const
        {
            return {};
        }
        virtual void reset() = 0;

        [[nodiscard]] virtual RpcAbi selectRpcAbi(const RpcAbiRequest &request) const
        {
            (void)request;
            return RpcAbi::RuntimeDefault;
        }

        [[nodiscard]] virtual RpcResult handleRpc(const RpcRequest &request) = 0;

        virtual void onSifTransfer(const SifTransfer &transfer)
        {
            (void)transfer;
        }

        virtual void appendDebugMetrics(std::vector<DebugMetric> &metrics) const
        {
            (void)metrics;
        }
    };

    using ServiceList = std::vector<std::unique_ptr<IopService>>;
}
