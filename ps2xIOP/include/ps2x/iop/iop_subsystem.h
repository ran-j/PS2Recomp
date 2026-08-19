#pragma once

#include "ps2x/iop/iop_host.h"
#include "ps2x/iop/iop_types.h"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
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

        [[nodiscard]] ModuleLoadResult loadModule(std::string_view path, const void *arguments = nullptr, uint32_t argumentSize = 0);
        [[nodiscard]] ModuleLoadResult loadModuleBuffer(uint32_t guestAddress, const void *arguments = nullptr, uint32_t argumentSize = 0);
        [[nodiscard]] bool stopModule(int32_t moduleId, int32_t *result = nullptr);
        void runEeCycles(uint64_t eeCycles) noexcept;

        [[nodiscard]] RpcAbi selectRpcAbi(const RpcAbiRequest &request) const;
        [[nodiscard]] bool canBindRpc(uint32_t sid) const noexcept;
        [[nodiscard]] RpcResult handleRpc(const RpcRequest &request);
        void onSifTransfer(const SifTransfer &transfer);

        [[nodiscard]] DebugSnapshot debugSnapshot() const;

    private:
        class Impl;
        std::unique_ptr<Impl> m_impl;
    };
}
