#ifndef PS2_STUBS_H
#define PS2_STUBS_H

#include "ps2_runtime.h"
#include "ps2_call_list.h"
#include <cstdint>

namespace ps2_stubs
{
    #define PS2_DECLARE_STUB(name) void name(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    PS2_STUB_LIST(PS2_DECLARE_STUB)
    #undef PS2_DECLARE_STUB

    void TODO(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
}

#endif // PS2_STUBS_H
