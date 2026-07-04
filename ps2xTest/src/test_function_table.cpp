#include "ps2_runtime.h"
#include "runtime/ps2_memory.h"

#include <algorithm>

// For Unit tests link ps2_runtime without the generated runner source.
extern const uint32_t g_ps2RecompiledFunctionTableBase = 0x00000000u;
extern const uint32_t g_ps2RecompiledFunctionTableEnd = PS2_RAM_SIZE;
extern const uint32_t g_ps2RecompiledFunctionTableSlotCount = (g_ps2RecompiledFunctionTableEnd - g_ps2RecompiledFunctionTableBase) >> 2;

PS2Runtime::RecompiledFunction g_ps2RecompiledFunctionTable[g_ps2RecompiledFunctionTableSlotCount] = {};

void reset_ps2_test_function_table()
{
    std::fill(g_ps2RecompiledFunctionTable,
              g_ps2RecompiledFunctionTable + g_ps2RecompiledFunctionTableSlotCount,
              nullptr);
}
