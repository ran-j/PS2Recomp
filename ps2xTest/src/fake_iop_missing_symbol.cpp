#include "ps2x/iop/plugin_api.h"

extern "C" PS2X_IOP_PLUGIN_EXPORT int32_t ps2x_iop_not_the_query_symbol()
{
    return PS2X_IOP_STATUS_OK_V1;
}
