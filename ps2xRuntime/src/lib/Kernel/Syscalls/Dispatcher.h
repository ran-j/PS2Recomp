#pragma once

#include "ps2_syscalls.h"

namespace ps2_syscalls
{
    bool dispatchNumericSyscall(uint32_t syscallNumber, uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
}
