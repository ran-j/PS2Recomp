void EnableIntc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    setReturnS32(ctx, 0);
}

void DisableIntc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    setReturnS32(ctx, 0);
}

void AddIntcHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    IrqHandlerInfo info{};
    info.cause = getRegU32(ctx, 4);
    info.handler = getRegU32(ctx, 5);
    info.arg = getRegU32(ctx, 6);
    info.enabled = true;

    const int handlerId = g_nextIntcHandlerId++;
    g_intcHandlers[handlerId] = info;
    setReturnS32(ctx, handlerId);
}

void RemoveIntcHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const int handlerId = static_cast<int>(getRegU32(ctx, 5));
    if (handlerId > 0)
    {
        g_intcHandlers.erase(handlerId);
    }
    setReturnS32(ctx, 0);
}

void AddDmacHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    IrqHandlerInfo info{};
    info.cause = getRegU32(ctx, 4);
    info.handler = getRegU32(ctx, 5);
    info.arg = getRegU32(ctx, 6);
    info.enabled = true;

    const int handlerId = g_nextDmacHandlerId++;
    g_dmacHandlers[handlerId] = info;
    setReturnS32(ctx, handlerId);
}

void RemoveDmacHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const int handlerId = static_cast<int>(getRegU32(ctx, 5));
    if (handlerId > 0)
    {
        g_dmacHandlers.erase(handlerId);
    }
    setReturnS32(ctx, 0);
}

void EnableIntcHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const int handlerId = static_cast<int>(getRegU32(ctx, 5));
    if (auto it = g_intcHandlers.find(handlerId); it != g_intcHandlers.end())
    {
        it->second.enabled = true;
    }
    setReturnS32(ctx, 0);
}

void DisableIntcHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const int handlerId = static_cast<int>(getRegU32(ctx, 5));
    if (auto it = g_intcHandlers.find(handlerId); it != g_intcHandlers.end())
    {
        it->second.enabled = false;
    }
    setReturnS32(ctx, 0);
}

void EnableDmacHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const int handlerId = static_cast<int>(getRegU32(ctx, 5));
    if (auto it = g_dmacHandlers.find(handlerId); it != g_dmacHandlers.end())
    {
        it->second.enabled = true;
    }
    setReturnS32(ctx, 0);
}

void DisableDmacHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const int handlerId = static_cast<int>(getRegU32(ctx, 5));
    if (auto it = g_dmacHandlers.find(handlerId); it != g_dmacHandlers.end())
    {
        it->second.enabled = false;
    }
    setReturnS32(ctx, 0);
}

void EnableDmac(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    setReturnS32(ctx, 0);
}

void DisableDmac(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    setReturnS32(ctx, 0);
}
