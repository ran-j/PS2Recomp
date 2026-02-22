void sceGsExecLoadImage(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    uint32_t imgAddr = getRegU32(ctx, 4);
    uint32_t srcAddr = getRegU32(ctx, 5);

    GsImageMem img{};
    if (!runtime || !readGsImage(rdram, imgAddr, img))
    {
        setReturnS32(ctx, -1);
        return;
    }

    const uint32_t rowBytes = bytesForPixels(img.psm, static_cast<uint32_t>(img.width));
    if (rowBytes == 0)
    {
        setReturnS32(ctx, -1);
        return;
    }

    uint32_t fbw = img.vram_width ? img.vram_width : std::max<uint32_t>(1, (img.width + 63) / 64);
    uint32_t base = static_cast<uint32_t>(img.vram_addr) * 2048u;
    uint32_t stride = bytesForPixels(img.psm, fbw * 64u);
    if (stride == 0)
    {
        setReturnS32(ctx, -1);
        return;
    }

    uint8_t *gsvram = runtime->memory().getGSVRAM();
    uint8_t *src = getMemPtr(rdram, srcAddr);
    if (!gsvram || !src)
    {
        setReturnS32(ctx, -1);
        return;
    }

    static int logCount = 0;
    if (logCount < 8)
    {
        std::cout << "ps2_stub sceGsExecLoadImage: x=" << img.x
                  << " y=" << img.y
                  << " w=" << img.width
                  << " h=" << img.height
                  << " vram=0x" << std::hex << img.vram_addr
                  << " fbw=" << std::dec << static_cast<int>(fbw)
                  << " psm=" << static_cast<int>(img.psm)
                  << " src=0x" << std::hex << srcAddr << std::dec << std::endl;
        ++logCount;
    }

    for (uint32_t row = 0; row < img.height; ++row)
    {
        uint32_t dstOff = base + (static_cast<uint32_t>(img.y) + row) * stride + bytesForPixels(img.psm, static_cast<uint32_t>(img.x));
        uint32_t srcOff = row * rowBytes;
        if (dstOff >= PS2_GS_VRAM_SIZE)
            break;
        uint32_t copyBytes = rowBytes;
        if (dstOff + copyBytes > PS2_GS_VRAM_SIZE)
            copyBytes = PS2_GS_VRAM_SIZE - dstOff;
        std::memcpy(gsvram + dstOff, src + srcOff, copyBytes);
    }

    if (img.width >= 320 && img.height >= 200)
    {
        auto &gs = runtime->memory().gs();
        gs.dispfb1 = makeDispFb(img.vram_addr, fbw, img.psm, 0, 0);
        gs.display1 = makeDisplay(0, 0, 0, 0, img.width - 1, img.height - 1);
    }

    setReturnS32(ctx, 0);
}

void sceGsExecStoreImage(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    uint32_t imgAddr = getRegU32(ctx, 4);
    uint32_t dstAddr = getRegU32(ctx, 5);

    GsImageMem img{};
    if (!runtime || !readGsImage(rdram, imgAddr, img))
    {
        setReturnS32(ctx, -1);
        return;
    }

    const uint32_t rowBytes = bytesForPixels(img.psm, static_cast<uint32_t>(img.width));
    if (rowBytes == 0)
    {
        setReturnS32(ctx, -1);
        return;
    }

    uint32_t fbw = img.vram_width ? img.vram_width : std::max<uint32_t>(1, (img.width + 63) / 64);
    uint32_t base = static_cast<uint32_t>(img.vram_addr) * 2048u;
    uint32_t stride = bytesForPixels(img.psm, fbw * 64u);
    if (stride == 0)
    {
        setReturnS32(ctx, -1);
        return;
    }

    uint8_t *gsvram = runtime->memory().getGSVRAM();
    uint8_t *dst = getMemPtr(rdram, dstAddr);
    if (!gsvram || !dst)
    {
        setReturnS32(ctx, -1);
        return;
    }

    static int logCount = 0;
    if (logCount < 8)
    {
        std::cout << "ps2_stub sceGsExecStoreImage: x=" << img.x
                  << " y=" << img.y
                  << " w=" << img.width
                  << " h=" << img.height
                  << " vram=0x" << std::hex << img.vram_addr
                  << " fbw=" << std::dec << static_cast<int>(fbw)
                  << " psm=" << static_cast<int>(img.psm)
                  << " dst=0x" << std::hex << dstAddr << std::dec << std::endl;
        ++logCount;
    }

    for (uint32_t row = 0; row < img.height; ++row)
    {
        uint32_t srcOff = base + (static_cast<uint32_t>(img.y) + row) * stride + bytesForPixels(img.psm, static_cast<uint32_t>(img.x));
        uint32_t dstOff = row * rowBytes;
        if (srcOff >= PS2_GS_VRAM_SIZE)
            break;
        uint32_t copyBytes = rowBytes;
        if (srcOff + copyBytes > PS2_GS_VRAM_SIZE)
            copyBytes = PS2_GS_VRAM_SIZE - srcOff;
        std::memcpy(dst + dstOff, gsvram + srcOff, copyBytes);
    }

    setReturnS32(ctx, 0);
}

void sceGsGetGParam(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    uint32_t addr = writeGsGParamToScratch(runtime);
    setReturnU32(ctx, addr);
}

void sceGsPutDispEnv(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    uint32_t envAddr = getRegU32(ctx, 4);
    GsDispEnvMem env{};
    if (readGsDispEnv(rdram, envAddr, env))
    {
        auto &gs = runtime->memory().gs();
        gs.display1 = env.display;
        gs.dispfb1 = env.dispfb;
    }
    setReturnS32(ctx, 0);
}

void sceGsPutDrawEnv(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    uint32_t envAddr = getRegU32(ctx, 4);
    uint32_t psm = getRegU32(ctx, 5);
    uint32_t w = getRegU32(ctx, 6);
    uint32_t h = getRegU32(ctx, 7);

    if (w == 0)
        w = 640;
    if (h == 0)
        h = 448;

    GsDrawEnvMem env{};
    env.offset_x = static_cast<uint16_t>(2048 - (w / 2));
    env.offset_y = static_cast<uint16_t>(2048 - (h / 2));
    env.clip_x = 0;
    env.clip_y = 0;
    env.clip_w = static_cast<uint16_t>(w);
    env.clip_h = static_cast<uint16_t>(h);
    env.vram_addr = 0;
    env.fbw = static_cast<uint8_t>((w + 63) / 64);
    env.psm = static_cast<uint8_t>(psm);
    env.vram_x = 0;
    env.vram_y = 0;
    env.draw_mask = 0;
    env.auto_clear = 1;
    env.bg_r = 1;
    env.bg_g = 1;
    env.bg_b = 1;
    env.bg_a = 0x80;
    env.bg_q = 0.0f;

    uint8_t *ptr = getMemPtr(rdram, envAddr);
    if (ptr)
    {
        std::memcpy(ptr, &env, sizeof(env));
    }
    setReturnS32(ctx, 0);
}

void sceGsResetGraph(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    uint32_t mode = getRegU32(ctx, 4);
    uint32_t interlace = getRegU32(ctx, 5);
    uint32_t omode = getRegU32(ctx, 6);
    uint32_t ffmode = getRegU32(ctx, 7);

    if (mode == 0)
    {
        g_gparam.interlace = static_cast<uint8_t>(interlace & 0x1);
        g_gparam.omode = static_cast<uint8_t>(omode & 0xFF);
        g_gparam.ffmode = static_cast<uint8_t>(ffmode & 0x1);
        writeGsGParamToScratch(runtime);

        auto &gs = runtime->memory().gs();
        gs.pmode = makePmode(1, 0, 0, 0, 0, 0x80);
        gs.smode2 = (interlace & 0x1) | ((ffmode & 0x1) << 1);
        gs.dispfb1 = makeDispFb(0, 10, 0, 0, 0);
        gs.display1 = makeDisplay(0, 0, 0, 0, 639, 447);
    }

    setReturnS32(ctx, 0);
}

void sceGsResetPath(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    setReturnS32(ctx, 0);
}

void sceGsSetDefClear(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t clearAddr = getRegU32(ctx, 4);
    if (uint8_t *clear = getMemPtr(rdram, clearAddr))
    {
        std::memset(clear, 0, 64);
    }
    setReturnS32(ctx, 0);
}

void sceGsSetDefDBuffDc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    setReturnS32(ctx, 0);
}

void sceGsSetDefDispEnv(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    uint32_t envAddr = getRegU32(ctx, 4);
    uint32_t psm = getRegU32(ctx, 5);
    uint32_t w = getRegU32(ctx, 6);
    uint32_t h = getRegU32(ctx, 7);
    uint32_t dx = readStackU32(rdram, ctx, 16);
    uint32_t dy = readStackU32(rdram, ctx, 20);

    if (w == 0)
        w = 640;
    if (h == 0)
        h = 448;

    uint32_t fbw = (w + 63) / 64;
    uint64_t dispfb = makeDispFb(0, fbw, psm, 0, 0);
    uint64_t display = makeDisplay(dx, dy, 0, 0, w - 1, h - 1);

    writeGsDispEnv(rdram, envAddr, display, dispfb);
    setReturnS32(ctx, 0);
}

void sceGsSetDefDrawEnv(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t envAddr = getRegU32(ctx, 4);
    uint32_t psm = getRegU32(ctx, 5);
    uint32_t w = getRegU32(ctx, 6);
    uint32_t h = getRegU32(ctx, 7);
    const uint32_t vramAddr = readStackU32(rdram, ctx, 16);
    const uint32_t vramX = readStackU32(rdram, ctx, 20);
    const uint32_t vramY = readStackU32(rdram, ctx, 24);

    if (w == 0)
        w = 640;
    if (h == 0)
        h = 448;

    GsDrawEnvMem env{};
    env.offset_x = static_cast<uint16_t>(2048 - (w / 2));
    env.offset_y = static_cast<uint16_t>(2048 - (h / 2));
    env.clip_x = 0;
    env.clip_y = 0;
    env.clip_w = static_cast<uint16_t>(w);
    env.clip_h = static_cast<uint16_t>(h);
    env.vram_addr = static_cast<uint16_t>(vramAddr & 0xFFFFu);
    env.fbw = static_cast<uint8_t>((w + 63u) / 64u);
    env.psm = static_cast<uint8_t>(psm & 0xFFu);
    env.vram_x = static_cast<uint16_t>(vramX & 0xFFFFu);
    env.vram_y = static_cast<uint16_t>(vramY & 0xFFFFu);
    env.draw_mask = 0;
    env.auto_clear = 1;
    env.bg_r = 0;
    env.bg_g = 0;
    env.bg_b = 0;
    env.bg_a = 0x80;
    env.bg_q = 0.0f;

    if (uint8_t *ptr = getMemPtr(rdram, envAddr))
    {
        std::memcpy(ptr, &env, sizeof(env));
    }

    setReturnS32(ctx, 0);
}

void sceGsSetDefDrawEnv2(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    sceGsSetDefDrawEnv(rdram, ctx, runtime);
}

void sceGsSetDefLoadImage(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    uint32_t imgAddr = getRegU32(ctx, 4);
    const GsSetDefImageArgs args = decodeGsSetDefImageArgs(rdram, ctx);

    GsImageMem img{};
    img.x = static_cast<uint16_t>(args.x);
    img.y = static_cast<uint16_t>(args.y);
    img.width = static_cast<uint16_t>(args.width);
    img.height = static_cast<uint16_t>(args.height);
    img.vram_addr = static_cast<uint16_t>(args.vramAddr);
    img.vram_width = static_cast<uint8_t>(args.vramWidth);
    img.psm = static_cast<uint8_t>(args.psm);

    writeGsImage(rdram, imgAddr, img);
    setReturnS32(ctx, 0);
}

void sceGsSetDefStoreImage(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    sceGsSetDefLoadImage(rdram, ctx, runtime);
}

void sceGsSwapDBuffDc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    // can we get away with that ? kkkk
    static int cur = 0;
    cur ^= 1;
    setReturnS32(ctx, cur);
}

void sceGsSyncPath(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    setReturnS32(ctx, 0);
}

void sceGsSyncV(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    ps2_syscalls::WaitVSyncTick(rdram, runtime);
    setReturnS32(ctx, 0);
}

void sceGsSyncVCallback(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    ps2_syscalls::WaitVSyncTick(rdram, runtime);
    setReturnS32(ctx, 0);
}

void sceGszbufaddr(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    setReturnU32(ctx, getRegU32(ctx, 4));
}
