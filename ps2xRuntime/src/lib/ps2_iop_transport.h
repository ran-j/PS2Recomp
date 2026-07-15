#pragma once

#include "ps2_runtime.h"
#include "ps2x/iop/iop_subsystem.h"

#include <utility>

class PS2IopTransport
{
public:
    static bool configureForTesting(
        PS2Runtime *runtime,
        const ps2x::iop::GameIdentity &identity,
        std::string *error = nullptr)
    {
        return runtime && runtime->m_iopSubsystem &&
               runtime->m_iopSubsystem->configure(identity, error);
    }

    [[nodiscard]] static ps2x::iop::RpcAbi selectRpcAbi(
        const PS2Runtime *runtime,
        const ps2x::iop::RpcAbiRequest &request)
    {
        return runtime
                   ? runtime->selectIopRpcAbi(request)
                   : ps2x::iop::RpcAbi::RuntimeDefault;
    }

    [[nodiscard]] static ps2x::iop::RpcResult handleRpc(
        PS2Runtime *runtime,
        uint8_t *rdram,
        R5900Context *context,
        ps2x::iop::RpcRequest request)
    {
        return runtime
                   ? runtime->handleIopRpc(rdram, context, std::move(request))
                   : ps2x::iop::RpcResult{};
    }

    static void notifyTransfer(
        PS2Runtime *runtime,
        uint8_t *rdram,
        const ps2x::iop::SifTransfer &transfer)
    {
        if (runtime)
        {
            runtime->notifyIopSifTransfer(rdram, transfer);
        }
    }

    static void reset(PS2Runtime *runtime)
    {
        if (runtime)
        {
            runtime->resetIop();
        }
    }
};
