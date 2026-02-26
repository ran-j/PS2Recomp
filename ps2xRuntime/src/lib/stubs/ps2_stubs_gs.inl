namespace
{
    std::mutex g_gs_sync_v_callback_mutex;
    uint32_t g_gs_sync_v_callback_func = 0u;
    uint32_t g_gs_sync_v_callback_gp = 0u;
    uint32_t g_gs_sync_v_callback_sp = 0u;
    uint32_t g_gs_sync_v_callback_stack_base = 0u;
    uint32_t g_gs_sync_v_callback_stack_top = 0u;
    uint64_t g_gs_sync_v_callback_tick = 0u;
    uint32_t g_gs_sync_v_callback_bad_pc_logs = 0u;
}

void resetGsSyncVCallbackState()
{
    std::lock_guard<std::mutex> lock(g_gs_sync_v_callback_mutex);
    g_gs_sync_v_callback_func = 0u;
    g_gs_sync_v_callback_gp = 0u;
    g_gs_sync_v_callback_sp = 0u;
    g_gs_sync_v_callback_stack_base = 0u;
    g_gs_sync_v_callback_stack_top = 0u;
    g_gs_sync_v_callback_tick = 0u;
    g_gs_sync_v_callback_bad_pc_logs = 0u;
}

void dispatchGsSyncVCallback(uint8_t *rdram, PS2Runtime *runtime)
{
    if (!rdram || !runtime)
    {
        return;
    }

    uint32_t callback = 0u;
    uint32_t gp = 0u;
    uint32_t sp = 0u;
    uint32_t callbackStackTop = 0u;
    uint64_t tick = 0u;
    {
        std::lock_guard<std::mutex> lock(g_gs_sync_v_callback_mutex);
        callback = g_gs_sync_v_callback_func;
        gp = g_gs_sync_v_callback_gp;
        sp = g_gs_sync_v_callback_sp;
        callbackStackTop = g_gs_sync_v_callback_stack_top;
        if (callback == 0u)
        {
            return;
        }
        tick = ++g_gs_sync_v_callback_tick;
    }

    if (!runtime->hasFunction(callback))
    {
        return;
    }

    if (callbackStackTop == 0u)
    {
        constexpr uint32_t kCallbackStackSize = 0x4000u;
        const uint32_t stackBase = runtime->guestMalloc(kCallbackStackSize, 16u);
        if (stackBase != 0u)
        {
            std::lock_guard<std::mutex> lock(g_gs_sync_v_callback_mutex);
            if (g_gs_sync_v_callback_stack_top == 0u)
            {
                g_gs_sync_v_callback_stack_base = stackBase;
                g_gs_sync_v_callback_stack_top = stackBase + kCallbackStackSize - 0x10u;
            }
            callbackStackTop = g_gs_sync_v_callback_stack_top;
        }
    }

    try
    {
        R5900Context callbackCtx{};
        SET_GPR_U32(&callbackCtx, 28, gp);
        SET_GPR_U32(&callbackCtx, 29, (callbackStackTop != 0u) ? callbackStackTop : ((sp != 0u) ? sp : (PS2_RAM_SIZE - 0x10u)));
        SET_GPR_U32(&callbackCtx, 31, 0u);
        SET_GPR_U32(&callbackCtx, 4, static_cast<uint32_t>(tick));
        callbackCtx.pc = callback;

        uint32_t steps = 0u;
        while (callbackCtx.pc != 0u && !runtime->isStopRequested() && steps < 1024u)
        {
            if (!runtime->hasFunction(callbackCtx.pc))
            {
                if (g_gs_sync_v_callback_bad_pc_logs < 16u)
                {
                    std::cerr << "[sceGsSyncVCallback:bad-pc] pc=0x" << std::hex << callbackCtx.pc
                              << " ra=0x" << getRegU32(&callbackCtx, 31)
                              << " sp=0x" << getRegU32(&callbackCtx, 29)
                              << " gp=0x" << getRegU32(&callbackCtx, 28)
                              << std::dec << std::endl;
                    ++g_gs_sync_v_callback_bad_pc_logs;
                }
                callbackCtx.pc = 0u;
                break;
            }

            auto step = runtime->lookupFunction(callbackCtx.pc);
            if (!step)
            {
                break;
            }
            ++steps;
            step(rdram, &callbackCtx, runtime);
        }
    }
    catch (const std::exception &e)
    {
        static uint32_t warnCount = 0u;
        if (warnCount < 8u)
        {
            std::cerr << "[sceGsSyncVCallback] callback exception: " << e.what() << std::endl;
            ++warnCount;
        }
    }
}

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
    const uint32_t totalImageBytes = rowBytes * static_cast<uint32_t>(img.height);
    const uint32_t headerQwc = 12u;
    const uint32_t imageQwc = (totalImageBytes + 15u) / 16u;
    const uint32_t totalQwc = headerQwc + imageQwc;

    uint32_t pktAddr = runtime->guestMalloc(totalQwc * 16u, 16u);
    if (pktAddr == 0)
    {
        setReturnS32(ctx, -1);
        return;
    }

    uint8_t *pkt = getMemPtr(rdram, pktAddr);
    const uint8_t *src = getConstMemPtr(rdram, srcAddr);
    if (!pkt || !src)
    {
        setReturnS32(ctx, -1);
        return;
    }

    uint32_t dbp = (static_cast<uint32_t>(img.vram_addr) * 2048u) / 256u;
    uint32_t dsax = static_cast<uint32_t>(img.x);
    uint32_t dsay = static_cast<uint32_t>(img.y);

    uint64_t *q = reinterpret_cast<uint64_t *>(pkt);
    q[0] = 0x1000000000000004ULL;
    q[1] = 0x0E0E0E0E0E0E0E0EULL;
    q[2] = (static_cast<uint64_t>(img.psm & 0x3Fu) << 24) | (static_cast<uint64_t>(1u) << 16) |
           (static_cast<uint64_t>(dbp & 0x3FFFu) << 32) | (static_cast<uint64_t>(fbw & 0x3Fu) << 48) |
           (static_cast<uint64_t>(img.psm & 0x3Fu) << 56);
    q[3] = 0x50ULL;
    q[4] = (static_cast<uint64_t>(dsay & 0x7FFu) << 48) | (static_cast<uint64_t>(dsax & 0x7FFu) << 32);
    q[5] = 0x51ULL;
    q[6] = (static_cast<uint64_t>(img.height) << 32) | static_cast<uint64_t>(img.width);
    q[7] = 0x52ULL;
    q[8] = 0ULL;
    q[9] = 0x53ULL;
    q[10] = (static_cast<uint64_t>(2) << 58) | (static_cast<uint64_t>(imageQwc) & 0x7FFF) |
            (1ULL << 15);
    q[11] = 0ULL;

    std::memcpy(pkt + 12 * 8, src, totalImageBytes);

    constexpr uint32_t GIF_CHANNEL = 0x1000A000;
    constexpr uint32_t CHCR_STR_MODE0 = 0x101u;
    auto &mem = runtime->memory();
    mem.writeIORegister(GIF_CHANNEL + 0x10u, pktAddr);
    mem.writeIORegister(GIF_CHANNEL + 0x20u, totalQwc & 0xFFFFu);
    mem.writeIORegister(GIF_CHANNEL + 0x00u, CHCR_STR_MODE0);

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
    const uint32_t totalImageBytes = rowBytes * static_cast<uint32_t>(img.height);

    uint8_t *dst = getMemPtr(rdram, dstAddr);
    if (!dst)
    {
        setReturnS32(ctx, -1);
        return;
    }

    uint32_t sbp = (static_cast<uint32_t>(img.vram_addr) * 2048u) / 256u;
    uint64_t bitbltbuf = (static_cast<uint64_t>(sbp & 0x3FFFu) << 0) |
                        (static_cast<uint64_t>(fbw & 0x3Fu) << 16) |
                        (static_cast<uint64_t>(img.psm & 0x3Fu) << 24) |
                        (static_cast<uint64_t>(0u) << 32) |
                        (static_cast<uint64_t>(1u) << 48) |
                        (static_cast<uint64_t>(0u) << 56);
    uint64_t trxpos = (static_cast<uint64_t>(img.x & 0x7FFu) << 0) |
                      (static_cast<uint64_t>(img.y & 0x7FFu) << 16) |
                      (static_cast<uint64_t>(0u) << 32) |
                      (static_cast<uint64_t>(0u) << 48);
    uint64_t trxreg = static_cast<uint64_t>(img.height) << 32 | static_cast<uint64_t>(img.width);

    uint32_t pktAddr = runtime->guestMalloc(80u, 16u);
    if (pktAddr == 0)
    {
        setReturnS32(ctx, -1);
        return;
    }

    uint8_t *pkt = getMemPtr(rdram, pktAddr);
    if (!pkt)
    {
        setReturnS32(ctx, -1);
        return;
    }

    uint64_t *q = reinterpret_cast<uint64_t *>(pkt);
    q[0] = 0x1000000000000004ULL;
    q[1] = 0x0E0E0E0E0E0E0E0EULL;
    q[2] = bitbltbuf;
    q[3] = 0x50ULL;
    q[4] = trxpos;
    q[5] = 0x51ULL;
    q[6] = trxreg;
    q[7] = 0x52ULL;
    q[8] = 1ULL;
    q[9] = 0x53ULL;

    constexpr uint32_t GIF_CHANNEL = 0x1000A000;
    constexpr uint32_t CHCR_STR_MODE0 = 0x101u;
    auto &mem = runtime->memory();
    mem.writeIORegister(GIF_CHANNEL + 0x10u, pktAddr);
    mem.writeIORegister(GIF_CHANNEL + 0x20u, 5u);
    mem.writeIORegister(GIF_CHANNEL + 0x00u, CHCR_STR_MODE0);
    mem.processPendingTransfers();

    runtime->gs().consumeLocalToHostBytes(dst, totalImageBytes);

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
    uint8_t *ptr = getMemPtr(rdram, envAddr);
    if (!ptr)
    {
        setReturnS32(ctx, -1);
        return;
    }
    constexpr uint32_t GIF_CHANNEL = 0x1000A000;
    constexpr uint32_t QWC = 5;
    constexpr uint32_t CHCR_STR_MODE0 = 0x101u;
    auto &mem = runtime->memory();
    mem.writeIORegister(GIF_CHANNEL + 0x10u, envAddr);
    mem.writeIORegister(GIF_CHANNEL + 0x20u, QWC);
    mem.writeIORegister(GIF_CHANNEL + 0x00u, CHCR_STR_MODE0);
    setReturnS32(ctx, 0);
}

void sceGsPutDrawEnv(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    uint32_t envAddr = getRegU32(ctx, 4);
    uint8_t *ptr = getMemPtr(rdram, envAddr);
    if (!ptr)
    {
        setReturnS32(ctx, -1);
        return;
    }

    constexpr uint32_t GIF_CHANNEL = 0x1000A000;
    constexpr uint32_t QWC = 9;
    constexpr uint32_t CHCR_STR_MODE0 = 0x101u;
    auto &mem = runtime->memory();
    mem.writeIORegister(GIF_CHANNEL + 0x10u, envAddr);
    mem.writeIORegister(GIF_CHANNEL + 0x20u, QWC);
    mem.writeIORegister(GIF_CHANNEL + 0x00u, CHCR_STR_MODE0);

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

        uint64_t pmode = makePmode(1, 0, 0, 0, 0, 0x80);
        uint64_t smode2 = (interlace & 0x1) | ((ffmode & 0x1) << 1);
        uint64_t dispfb = makeDispFb(0, 10, 0, 0, 0);
        uint64_t display = makeDisplay(0, 0, 0, 0, 639, 447);
        uint64_t bgcolor = 0ULL;

        if (runtime)
        {
            uint32_t pktAddr = runtime->guestMalloc(192u, 16u);
            if (pktAddr != 0u)
            {
                uint8_t *pkt = getMemPtr(rdram, pktAddr);
                if (pkt)
                {
                    uint64_t *q = reinterpret_cast<uint64_t *>(pkt);
                    q[0] = 0x1000000000000005ULL;
                    q[1] = 0x0E0E0E0E0E0E0E0EULL;
                    q[2] = pmode;
                    q[3] = 0x41ULL;
                    q[4] = smode2;
                    q[5] = 0x42ULL;
                    q[6] = dispfb;
                    q[7] = 0x59ULL;
                    q[8] = display;
                    q[9] = 0x5aULL;
                    q[10] = bgcolor;
                    q[11] = 0x5fULL;
                    constexpr uint32_t GIF_CHANNEL = 0x1000A000;
                    constexpr uint32_t CHCR_STR_MODE0 = 0x101u;
                    auto &mem = runtime->memory();
                    mem.writeIORegister(GIF_CHANNEL + 0x10u, pktAddr);
                    mem.writeIORegister(GIF_CHANNEL + 0x20u, 12u);
                    mem.writeIORegister(GIF_CHANNEL + 0x00u, CHCR_STR_MODE0);
                }
            }
        }
    }

    setReturnS32(ctx, 0);
}

void sceGsResetPath(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    setReturnS32(ctx, 0);
}

void sceGsSetDefClear(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    (void)rdram;
    (void)ctx;
    (void)runtime;
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
    uint32_t envAddr = getRegU32(ctx, 4);
    uint32_t param_2 = getRegU32(ctx, 5);
    int32_t w = static_cast<int32_t>(static_cast<int16_t>(getRegU32(ctx, 6) & 0xFFFF));
    int32_t h = static_cast<int32_t>(static_cast<int16_t>(getRegU32(ctx, 7) & 0xFFFF));
    uint32_t param_5 = readStackU32(rdram, ctx, 16);
    uint32_t param_6 = readStackU32(rdram, ctx, 20);

    if (w <= 0)
        w = 640;
    if (h <= 0)
        h = 448;

    uint32_t psm = param_2 & 0xFU;
    uint32_t fbw = ((static_cast<uint32_t>(w) + 63u) >> 6) & 0x3FU;
    sceGszbufaddr(rdram, ctx, runtime);
    int32_t zbuf = static_cast<int32_t>(static_cast<int16_t>(getRegU32(ctx, 2) & 0xFFFF));

    uint8_t *const ptr = getMemPtr(rdram, envAddr);
    if (!ptr)
    {
        setReturnS32(ctx, 8);
        return;
    }

    uint64_t *const words = reinterpret_cast<uint64_t *>(ptr);

    words[0] = 0x1000000000008008ULL;
    words[1] = 0x000000000000000EULL;

    words[2] = (static_cast<uint64_t>(fbw) << 16) | (static_cast<uint64_t>(psm) << 24);
    words[3] = 0x4c;

    words[4] = (static_cast<uint64_t>(zbuf) & 0xFFFFULL) | (static_cast<uint64_t>(param_6 & 0xF) << 24) |
               (param_5 == 0 ? 0x100000000ULL : 0ULL);
    words[5] = 0x4e;

    int32_t off_x = 0x800 - (w >> 1);
    int32_t off_y = 0x800 - (h >> 1);
    words[6] = (static_cast<uint64_t>(static_cast<uint32_t>(off_y) & 0xFFFF) << 36) |
               (static_cast<uint32_t>(off_x) & 0xFFFF) * 16ULL;
    words[7] = 0x18;

    words[8] = (static_cast<uint64_t>(static_cast<uint32_t>(h - 1) & 0xFFFF) << 48) |
               (static_cast<uint64_t>(static_cast<uint32_t>(w - 1) & 0xFFFF) << 16);
    words[9] = 0x40;

    words[10] = 1;
    words[11] = 0x1a;

    words[12] = 1;
    words[13] = 0x46;

    words[14] = (param_2 & 2) ? 1ULL : 0ULL;
    words[15] = 0x45;

    words[16] = (param_5 == 0) ? 0x30000ULL : ((static_cast<uint64_t>(param_5 & 3) << 17) | 0x10000ULL);
    words[17] = 0x47;

    setReturnS32(ctx, 8);
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
    static int cur = 0;
    cur ^= 1;
    setReturnS32(ctx, cur);
}

void sceGsSyncPath(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    int32_t mode = static_cast<int32_t>(getRegU32(ctx, 4));
    auto &mem = runtime->memory();

    if (mode == 0)
    {
        mem.processPendingTransfers();

        uint32_t count = 0;
        constexpr uint32_t kTimeout = 0x1000000;

        while ((mem.readIORegister(0x10009000) & 0x100) != 0)
        {
            if (++count > kTimeout)
            {
                setReturnS32(ctx, -1);
                return;
            }
        }

        while ((mem.readIORegister(0x1000A000) & 0x100) != 0)
        {
            if (++count > kTimeout)
            {
                setReturnS32(ctx, -1);
                return;
            }
        }

        while ((mem.readIORegister(0x10003C00) & 0x1F000003) != 0)
        {
            if (++count > kTimeout)
            {
                setReturnS32(ctx, -1);
                return;
            }
        }

        while ((mem.readIORegister(0x10003020) & 0xC00) != 0)
        {
            if (++count > kTimeout)
            {
                setReturnS32(ctx, -1);
                return;
            }
        }

        setReturnS32(ctx, 0);
    }
    else
    {
        uint32_t result = 0;

        if ((mem.readIORegister(0x10009000) & 0x100) != 0)
            result |= 1;
        if ((mem.readIORegister(0x1000A000) & 0x100) != 0)
            result |= 2;
        if ((mem.readIORegister(0x10003C00) & 0x1F000003) != 0)
            result |= 4;
        if ((mem.readIORegister(0x10003020) & 0xC00) != 0)
            result |= 0x10;

        setReturnS32(ctx, result);
    }
}

void sceGsSyncV(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    setReturnS32(ctx, 0);
}

void sceGsSyncVCallback(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    (void)rdram;

    const uint32_t newCallback = getRegU32(ctx, 4);
    const uint32_t gp = getRegU32(ctx, 28);
    const uint32_t sp = getRegU32(ctx, 29);

    uint32_t oldCallback = 0u;
    {
        std::lock_guard<std::mutex> lock(g_gs_sync_v_callback_mutex);
        oldCallback = g_gs_sync_v_callback_func;
        g_gs_sync_v_callback_func = newCallback;
        if (newCallback != 0u)
        {
            g_gs_sync_v_callback_gp = gp;
            g_gs_sync_v_callback_sp = sp;
        }
    }

    setReturnU32(ctx, oldCallback);
}

void sceGszbufaddr(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    (void)rdram;
    uint32_t param_1 = getRegU32(ctx, 4);
    int32_t w = static_cast<int32_t>(static_cast<int16_t>(getRegU32(ctx, 6) & 0xFFFF));
    int32_t h = static_cast<int32_t>(static_cast<int16_t>(getRegU32(ctx, 7) & 0xFFFF));

    int32_t width_blocks = (w + 63) >> 6;
    if (w + 63 < 0)
        width_blocks = (w + 126) >> 6;

    int32_t height_blocks;
    if ((param_1 & 2) != 0)
    {
        int32_t v = (h + 63) >> 6;
        if (h + 63 < 0)
            v = (h + 126) >> 6;
        height_blocks = v;
    }
    else
    {
        int32_t v = (h + 31) >> 5;
        if (h + 31 < 0)
            v = (h + 62) >> 5;
        height_blocks = v;
    }

    int32_t product = width_blocks * height_blocks;

    uint64_t gparam_val = 0;
    if (runtime)
    {
        uint8_t *scratch = runtime->memory().getScratchpad();
        if (scratch)
        {
            std::memcpy(&gparam_val, scratch + 0x100, sizeof(gparam_val));
        }
    }
    if ((gparam_val & 0xFFFF0000FFFFULL) == 1ULL)
        product = (product * 0x10000) >> 16;
    else
        product = (product * 0x20000) >> 16;

    setReturnS32(ctx, product);
}
