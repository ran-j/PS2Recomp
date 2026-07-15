# Minimal plugin

This plugin matches one ELF basename and handles one function on a synthetic
SID. It is synchronous: it signals NOWAIT completion and suppresses a second
dispatch through a registered EE server.

```c
#include <ps2x/iop/plugin_api.h>

#include <stdlib.h>

#define STRING_VIEW(literal) { (literal), sizeof(literal) - 1u }

enum
{
    MY_SID = 0x6D795349u,
    MY_FUNCTION = 1u,
};

struct my_state
{
    const ps2x_iop_host_api_v1 *host;
};

static void *my_create(const ps2x_iop_host_api_v1 *host,
                       const ps2x_iop_game_identity_v1 *identity)
{
    struct my_state *state;
    (void)identity;

    if (!host ||
        host->abi_version != PS2X_IOP_ABI_VERSION_V1 ||
        host->struct_size < sizeof(*host))
    {
        return NULL;
    }

    state = (struct my_state *)calloc(1u, sizeof(*state));
    if (state)
    {
        state->host = host;
    }
    return state;
}

static void my_destroy(void *instance)
{
    free(instance);
}

static int32_t my_reset(void *instance)
{
    return instance ? PS2X_IOP_STATUS_OK_V1
                    : PS2X_IOP_STATUS_INVALID_ARGUMENT_V1;
}

static int32_t my_handle_rpc(void *instance,
                             const ps2x_iop_rpc_request_v1 *request,
                             ps2x_iop_rpc_result_v1 *result)
{
    struct my_state *state = (struct my_state *)instance;
    const uint32_t value = 1u;
    int32_t status;

    if (!state || !request || !result ||
        request->struct_size < sizeof(*request) ||
        result->struct_size < sizeof(*result))
    {
        return PS2X_IOP_STATUS_INVALID_ARGUMENT_V1;
    }

    result->handled = 0u;
    result->result_address = 0u;
    result->signal_nowait_completion = 0u;
    result->signal_completion = 0u;
    result->callback_policy = PS2X_IOP_CALLBACK_RUNTIME_DEFAULT_V1;
    result->server_dispatch_policy =
        PS2X_IOP_SERVER_DISPATCH_RUNTIME_DEFAULT_V1;

    if (request->sid != MY_SID || request->function != MY_FUNCTION)
    {
        return PS2X_IOP_STATUS_OK_V1;
    }
    if (request->receive.size < sizeof(value))
    {
        return PS2X_IOP_STATUS_BUFFER_TOO_SMALL_V1;
    }
    if (!state->host->write_guest)
    {
        return PS2X_IOP_STATUS_UNSUPPORTED_V1;
    }

    status = state->host->write_guest(state->host->userdata,
                                      request->receive.address,
                                      &value,
                                      sizeof(value));
    if (status != PS2X_IOP_STATUS_OK_V1)
    {
        return status;
    }

    result->handled = 1u;
    result->result_address = request->receive.address;
    result->signal_nowait_completion = 1u;
    result->server_dispatch_policy = PS2X_IOP_SERVER_DISPATCH_SUPPRESS_V1;
    return PS2X_IOP_STATUS_OK_V1;
}

static const uint32_t my_sids[] = { MY_SID };

static const ps2x_iop_profile_api_v1 my_profiles[] = {
    {
        PS2X_IOP_ABI_VERSION_V1,
        sizeof(ps2x_iop_profile_api_v1),
        STRING_VIEW("my-game-profile"),
        {
            sizeof(ps2x_iop_game_matcher_v1),
            STRING_VIEW("SLUS_000.00"),
            0u,
            0u,
        },
        1u,
        my_sids,
        my_create,
        my_destroy,
        my_reset,
        NULL,
        my_handle_rpc,
        NULL,
        NULL,
        NULL,
    },
};

PS2X_IOP_PLUGIN_EXPORT int32_t
ps2x_iop_query_v1(uint32_t host_abi_version,
                  ps2x_iop_plugin_api_v1 *out)
{
    static const ps2x_iop_plugin_api_v1 plugin = {
        PS2X_IOP_ABI_VERSION_V1,
        sizeof(ps2x_iop_plugin_api_v1),
        STRING_VIEW("my-iop-plugin"),
        STRING_VIEW("1.0.0"),
        1u,
        my_profiles,
    };

    if (host_abi_version != PS2X_IOP_ABI_VERSION_V1)
    {
        return PS2X_IOP_STATUS_UNSUPPORTED_V1;
    }
    if (!out)
    {
        return PS2X_IOP_STATUS_INVALID_ARGUMENT_V1;
    }
    if (out->struct_size < sizeof(*out))
    {
        return PS2X_IOP_STATUS_BUFFER_TOO_SMALL_V1;
    }

    *out = plugin;
    return PS2X_IOP_STATUS_OK_V1;
}
```

### Standalone CMake target

The plugin consumes only the public ABI header; it does not link to `ps2xRuntime` or `ps2_iop`.

```cmake
cmake_minimum_required(VERSION 3.21)
project(my_iop_plugin LANGUAGES C)

set(PS2X_IOP_INCLUDE_DIR "" CACHE PATH
    "Directory containing ps2x/iop/plugin_api.h"
)
if(NOT EXISTS "${PS2X_IOP_INCLUDE_DIR}/ps2x/iop/plugin_api.h")
    message(FATAL_ERROR
        "Set PS2X_IOP_INCLUDE_DIR to PS2Recomp/ps2xIOP/include"
    )
endif()

add_library(my_iop_plugin MODULE my_iop_plugin.c)
target_include_directories(my_iop_plugin PRIVATE
    "${PS2X_IOP_INCLUDE_DIR}"
)
set_target_properties(my_iop_plugin PROPERTIES
    PREFIX ""
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)
install(TARGETS my_iop_plugin
    RUNTIME DESTINATION .
    LIBRARY DESTINATION .
)
```

On Windows:

```powershell
cmake -S . -B build -A x64 `
  -DPS2X_IOP_INCLUDE_DIR="C:/path/to/PS2Recomp/ps2xIOP/include"
cmake --build build --config Release
cmake --install build --config Release `
  --prefix "C:/path/to/ps2EntryRunner/iop_plugins"
```

On Linux:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DPS2X_IOP_INCLUDE_DIR=/path/to/PS2Recomp/ps2xIOP/include
cmake --build build -j
cmake --install build --prefix /path/to/ps2EntryRunner/iop_plugins
```

Build or install the resulting `.dll`/`.so` before the runtime calls
`initialize()`. If it is not installed directly, copy it into the executable's
`iop_plugins/` directory.