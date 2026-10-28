#pragma once

#include <cstdint>

struct R5900Context;
class PS2Runtime;

// Executes EE code that has no recompiled function: code a game loads or
// builds in RAM at run time (overlays, code modules linked from data files).
// Runs from ctx->pc and returns once control reaches recompiled code (with
// ctx->pc set to it), when the scheduler needs a checkpoint, or when a call
// into recompiled code unwinds. Same signature as a recompiled function.
void ps2InterpretGuestCode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);

// True if the function table has recompiled code for this exact address.
bool ps2HasRecompiledFunction(uint32_t address);
