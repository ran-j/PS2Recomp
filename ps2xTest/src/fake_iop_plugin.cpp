#include "ps2x/iop/plugin_api.h"

#include <cstdint>
#include <cstring>
#include <new>

namespace
{
    constexpr uint32_t kSyntheticSid = 0xF00DCAFEu;
    constexpr uint32_t kCoreCollisionSid = 0x80001300u;
    constexpr uint32_t kSyntheticFunction = 0x42u;
    constexpr uint32_t kCoreCollisionFunction = 0x99u;
    constexpr uint32_t kSyntheticEntryPoint = 0x00123456u;
    constexpr uint32_t kSpecificRecvXEntryPoint = kSyntheticEntryPoint + 0x100u;
    constexpr uint32_t kSyntheticCrc32 = 0xA1B2C3D4u;
    constexpr uint32_t kResponseXor = 0xA5A55A5Au;
    constexpr uint32_t kCoreCollisionResponse = 0xC0DEF00Du;

    template <size_t Size>
    constexpr ps2x_iop_string_view_v1 stringView(const char (&value)[Size])
    {
        return {value, Size - 1u};
    }

    struct FakePluginState
    {
        const ps2x_iop_host_api_v1 *host = nullptr;
        uint64_t resetGeneration = 0;
        uint64_t rpcCalls = 0;
        uint64_t transfers = 0;
    };

    void log(FakePluginState *state, uint32_t level, ps2x_iop_string_view_v1 message)
    {
        if (state && state->host && state->host->log)
        {
            state->host->log(state->host->userdata, level, message);
        }
    }

    void *createProfile(const ps2x_iop_host_api_v1 *host,
                        const ps2x_iop_game_identity_v1 *identity)
    {
        if (!host || host->abi_version != PS2X_IOP_ABI_VERSION_V1 ||
            host->struct_size < sizeof(*host) || !identity ||
            identity->struct_size < sizeof(*identity) ||
            (identity->entry_point != kSyntheticEntryPoint &&
             identity->entry_point != kSpecificRecvXEntryPoint) ||
            identity->crc32 != kSyntheticCrc32)
        {
            return nullptr;
        }

        auto *state = new (std::nothrow) FakePluginState{};
        if (!state)
        {
            return nullptr;
        }
        state->host = host;
        log(state, PS2X_IOP_LOG_INFO_V1, stringView("fake-plugin-create"));
        return state;
    }

    void destroyProfile(void *instance)
    {
        auto *state = static_cast<FakePluginState *>(instance);
        log(state, PS2X_IOP_LOG_INFO_V1, stringView("fake-plugin-destroy"));
        delete state;
    }

    int32_t resetProfile(void *instance)
    {
        auto *state = static_cast<FakePluginState *>(instance);
        if (!state)
        {
            return PS2X_IOP_STATUS_INVALID_ARGUMENT_V1;
        }
        ++state->resetGeneration;
        state->rpcCalls = 0;
        state->transfers = 0;
        return PS2X_IOP_STATUS_OK_V1;
    }

    uint32_t selectRpcAbi(void *instance, const ps2x_iop_rpc_abi_request_v1 *request)
    {
        if (!instance || !request || request->struct_size < sizeof(*request))
        {
            return PS2X_IOP_RPC_ABI_DEFAULT_V1;
        }
        if (request->bound_sid == kSyntheticSid && request->function == kSyntheticFunction)
        {
            return PS2X_IOP_RPC_ABI_STACK_V1;
        }
        return PS2X_IOP_RPC_ABI_DEFAULT_V1;
    }

    int32_t handleRpc(void *instance,
                      const ps2x_iop_rpc_request_v1 *request,
                      ps2x_iop_rpc_result_v1 *result)
    {
        auto *state = static_cast<FakePluginState *>(instance);
        if (!state || !request || request->struct_size < sizeof(*request) ||
            !result || result->struct_size < sizeof(*result))
        {
            return PS2X_IOP_STATUS_INVALID_ARGUMENT_V1;
        }

        result->handled = 0u;
        result->result_address = 0u;
        result->signal_nowait_completion = 0u;
        result->callback_policy = PS2X_IOP_CALLBACK_RUNTIME_DEFAULT_V1;
        if (request->sid == kCoreCollisionSid &&
            request->function == kCoreCollisionFunction)
        {
            if (request->receive.size < sizeof(kCoreCollisionResponse) ||
                !state->host->write_guest ||
                state->host->write_guest(state->host->userdata,
                                         request->receive.address,
                                         &kCoreCollisionResponse,
                                         sizeof(kCoreCollisionResponse)) !=
                    PS2X_IOP_STATUS_OK_V1)
            {
                return PS2X_IOP_STATUS_FAILED_V1;
            }
            ++state->rpcCalls;
            result->handled = 1u;
            result->result_address = request->receive.address;
            return PS2X_IOP_STATUS_OK_V1;
        }

        if (request->sid != kSyntheticSid || request->function != kSyntheticFunction)
        {
            return PS2X_IOP_STATUS_OK_V1;
        }

        uint32_t input = 0u;
        if (request->send.size < sizeof(input) || !state->host->read_guest ||
            state->host->read_guest(state->host->userdata,
                                    request->send.address,
                                    &input,
                                    sizeof(input)) != PS2X_IOP_STATUS_OK_V1)
        {
            return PS2X_IOP_STATUS_FAILED_V1;
        }

        const uint32_t output = input ^ kResponseXor;
        if (request->receive.size < sizeof(output) || !state->host->write_guest ||
            state->host->write_guest(state->host->userdata,
                                     request->receive.address,
                                     &output,
                                     sizeof(output)) != PS2X_IOP_STATUS_OK_V1)
        {
            return PS2X_IOP_STATUS_FAILED_V1;
        }

        ++state->rpcCalls;
        result->handled = 1u;
        result->result_address = request->receive.address;
        result->signal_nowait_completion = 1u;
        result->callback_policy = PS2X_IOP_CALLBACK_SUPPRESS_V1;
        return PS2X_IOP_STATUS_OK_V1;
    }

    int32_t onSifTransfer(void *instance, const ps2x_iop_sif_transfer_v1 *transfer)
    {
        auto *state = static_cast<FakePluginState *>(instance);
        if (!state || !transfer || transfer->struct_size < sizeof(*transfer))
        {
            return PS2X_IOP_STATUS_INVALID_ARGUMENT_V1;
        }
        ++state->transfers;
        return PS2X_IOP_STATUS_OK_V1;
    }

    size_t debugMetricCount(void *instance)
    {
        return instance ? 3u : 0u;
    }

    int32_t debugMetric(void *instance, size_t index, ps2x_iop_debug_metric_v1 *metric)
    {
        const auto *state = static_cast<const FakePluginState *>(instance);
        if (!state || !metric || metric->struct_size < sizeof(*metric) || index >= 3u)
        {
            return PS2X_IOP_STATUS_INVALID_ARGUMENT_V1;
        }

        metric->struct_size = sizeof(*metric);
        metric->hexadecimal = 0u;
        switch (index)
        {
        case 0u:
            metric->name = stringView("reset_generation");
            metric->value = state->resetGeneration;
            break;
        case 1u:
            metric->name = stringView("rpc_calls");
            metric->value = state->rpcCalls;
            break;
        case 2u:
            metric->name = stringView("sif_transfers");
            metric->value = state->transfers;
            break;
        default:
            return PS2X_IOP_STATUS_INVALID_ARGUMENT_V1;
        }
        return PS2X_IOP_STATUS_OK_V1;
    }

    constexpr uint32_t kSids[] = {kSyntheticSid, kCoreCollisionSid};
    constexpr uint32_t kDuplicateSids[] = {kSyntheticSid, kSyntheticSid};
    constexpr uint32_t kAmbiguousSids[] = {kSyntheticSid};
    const ps2x_iop_profile_api_v1 kProfiles[] = {
        {
            PS2X_IOP_ABI_VERSION_V1,
            sizeof(ps2x_iop_profile_api_v1),
            stringView("synthetic-test-profile"),
            {
                sizeof(ps2x_iop_game_matcher_v1),
                stringView("synthetic_iop_test.elf"),
                kSyntheticEntryPoint,
                kSyntheticCrc32,
            },
            2u,
            kSids,
            &createProfile,
            &destroyProfile,
            &resetProfile,
            &selectRpcAbi,
            &handleRpc,
            &onSifTransfer,
            &debugMetricCount,
            &debugMetric,
        },
        {
            PS2X_IOP_ABI_VERSION_V1,
            sizeof(ps2x_iop_profile_api_v1),
            stringView("synthetic-duplicate-sid-profile"),
            {
                sizeof(ps2x_iop_game_matcher_v1),
                stringView("synthetic_duplicate.elf"),
                kSyntheticEntryPoint,
                kSyntheticCrc32,
            },
            2u,
            kDuplicateSids,
            &createProfile,
            &destroyProfile,
            &resetProfile,
            &selectRpcAbi,
            &handleRpc,
            &onSifTransfer,
            &debugMetricCount,
            &debugMetric,
        },
        {
            PS2X_IOP_ABI_VERSION_V1,
            sizeof(ps2x_iop_profile_api_v1),
            stringView("synthetic-ambiguous-recvx-profile"),
            {
                sizeof(ps2x_iop_game_matcher_v1),
                stringView("slus_201.84"),
                0u,
                0u,
            },
            1u,
            kAmbiguousSids,
            &createProfile,
            &destroyProfile,
            &resetProfile,
            &selectRpcAbi,
            &handleRpc,
            &onSifTransfer,
            &debugMetricCount,
            &debugMetric,
        },
        {
            PS2X_IOP_ABI_VERSION_V1,
            sizeof(ps2x_iop_profile_api_v1),
            stringView("synthetic-specific-recvx-profile"),
            {
                sizeof(ps2x_iop_game_matcher_v1),
                stringView("slus_201.84"),
                kSpecificRecvXEntryPoint,
                kSyntheticCrc32,
            },
            1u,
            kAmbiguousSids,
            &createProfile,
            &destroyProfile,
            &resetProfile,
            &selectRpcAbi,
            &handleRpc,
            &onSifTransfer,
            &debugMetricCount,
            &debugMetric,
        },
        {
            PS2X_IOP_ABI_VERSION_V1,
            sizeof(ps2x_iop_profile_api_v1),
            stringView("synthetic-invalid-large-sid-profile"),
            {
                sizeof(ps2x_iop_game_matcher_v1),
                stringView("synthetic_invalid.elf"),
                kSyntheticEntryPoint,
                kSyntheticCrc32,
            },
            257u,
            kAmbiguousSids,
            &createProfile,
            &destroyProfile,
            &resetProfile,
            &selectRpcAbi,
            &handleRpc,
            &onSifTransfer,
            &debugMetricCount,
            &debugMetric,
        },
    };

    const ps2x_iop_plugin_api_v1 kPlugin = {
        PS2X_IOP_ABI_VERSION_V1,
        sizeof(ps2x_iop_plugin_api_v1),
        stringView("ps2x-test-plugin"),
        stringView("1.0.0"),
        5u,
        kProfiles,
    };
}

extern "C" PS2X_IOP_PLUGIN_EXPORT int32_t ps2x_iop_query_v1(
    uint32_t hostAbiVersion,
    ps2x_iop_plugin_api_v1 *pluginApi)
{
    if (hostAbiVersion != PS2X_IOP_ABI_VERSION_V1)
    {
        return PS2X_IOP_STATUS_UNSUPPORTED_V1;
    }
    if (!pluginApi)
    {
        return PS2X_IOP_STATUS_INVALID_ARGUMENT_V1;
    }
    if (pluginApi->struct_size < sizeof(*pluginApi))
    {
        return PS2X_IOP_STATUS_BUFFER_TOO_SMALL_V1;
    }

    *pluginApi = kPlugin;
    return PS2X_IOP_STATUS_OK_V1;
}
