#include "ps2x/iop/plugin_api.h"

PS2X_IOP_PLUGIN_EXPORT int32_t ps2x_iop_query_v1(
    uint32_t host_abi_version,
    ps2x_iop_plugin_api_v1 *plugin_api)
{
    (void)host_abi_version;
    if (!plugin_api || plugin_api->struct_size < sizeof(*plugin_api))
    {
        return PS2X_IOP_STATUS_INVALID_ARGUMENT_V1;
    }
    plugin_api->abi_version = PS2X_IOP_ABI_VERSION_V1 + 1u;
    plugin_api->struct_size = sizeof(*plugin_api);
    return PS2X_IOP_STATUS_OK_V1;
}
