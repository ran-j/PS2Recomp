#include "Common.h"
#include "System.h"

namespace ps2_stubs
{
void builtin_set_imask(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    static int logCount = 0;
    if (logCount < 8)
    {
        RUNTIME_LOG("ps2_stub builtin_set_imask");
        ++logCount;
    }
    setReturnS32(ctx, 0);
}


void sceIDC(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    setReturnS32(ctx, 0);
}


void sceSDC(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    setReturnS32(ctx, 0);
}


void exit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    if (runtime)
    {
        runtime->requestStop();
    }
    setReturnS32(ctx, 0);
}


void getpid(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    setReturnS32(ctx, 1);
}


void sceSetBrokenLink(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceSetBrokenLink", rdram, ctx, runtime);
}


void sceSetPtm(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceSetPtm", rdram, ctx, runtime);
}

void sceDevVif0Reset(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    setReturnS32(ctx, 0);
}

void sceDevVu0Reset(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    setReturnS32(ctx, 0);
}

}
