#pragma once

#include <cstdint>
#include "ps2_runtime.h"
#include "ps2_syscalls.h"

void abort(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) { ps2_syscalls::TODO(rdram, ctx, runtime); }
void exit(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) { ps2_syscalls::TODO(rdram, ctx, runtime); }
void _exit(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) { ps2_syscalls::TODO(rdram, ctx, runtime); }
void __start(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) { ps2_syscalls::TODO(rdram, ctx, runtime); }
void _ftext(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) { ps2_syscalls::TODO(rdram, ctx, runtime); }
void __do_global_dtors(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) { ps2_syscalls::TODO(rdram, ctx, runtime); }
void __do_global_ctors(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) { ps2_syscalls::TODO(rdram, ctx, runtime); }
