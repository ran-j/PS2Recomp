#pragma once

#include "ps2x/iop/iop_host.h"
#include "ps2x/iop/iop_types.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace ps2x::iop
{
    class IopSubsystem
    {
    public:
        explicit IopSubsystem(IopHost &host);
        ~IopSubsystem();

        IopSubsystem(const IopSubsystem &) = delete;
        IopSubsystem &operator=(const IopSubsystem &) = delete;
        IopSubsystem(IopSubsystem &&) noexcept;
        IopSubsystem &operator=(IopSubsystem &&) noexcept;

        void setPluginSearchPaths(std::vector<std::filesystem::path> paths);
        bool loadPlugins(std::string *error = nullptr);

        bool configure(const GameIdentity &identity, std::string *error = nullptr);
        void reset();

        [[nodiscard]] RpcAbi selectRpcAbi(const RpcAbiRequest &request) const;
        [[nodiscard]] RpcResult handleRpc(const RpcRequest &request);
        void onSifTransfer(const SifTransfer &transfer);

        [[nodiscard]] DebugSnapshot debugSnapshot() const;

    private:
        class Impl;
        std::unique_ptr<Impl> m_impl;
    };
}
