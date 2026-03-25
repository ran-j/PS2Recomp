namespace
{
    std::mutex g_gs_sync_v_mutex;
    uint64_t g_gs_sync_v_base_tick = 0u;
    std::mutex g_gs_sync_v_callback_mutex;
    uint32_t g_gs_sync_v_callback_func = 0u;
    uint32_t g_gs_sync_v_callback_gp = 0u;
    uint32_t g_gs_sync_v_callback_sp = 0u;
    uint32_t g_gs_sync_v_callback_stack_base = 0u;
    uint32_t g_gs_sync_v_callback_stack_top = 0u;
    uint32_t g_gs_sync_v_callback_bad_pc_logs = 0u;
}

static void resetGsSyncVState()
{
    std::lock_guard<std::mutex> lock(g_gs_sync_v_mutex);
    g_gs_sync_v_base_tick = ps2_syscalls::GetCurrentVSyncTick();
}

static int32_t getGsSyncVFieldForTick(uint64_t tick)
{
    std::lock_guard<std::mutex> lock(g_gs_sync_v_mutex);
    if (tick <= g_gs_sync_v_base_tick)
    {
        return 0;
    }

    return static_cast<int32_t>((tick - g_gs_sync_v_base_tick - 1u) & 1u);
}

void resetGsSyncVCallbackState()
{
    {
        std::lock_guard<std::mutex> lock(g_gs_sync_v_callback_mutex);
        g_gs_sync_v_callback_func = 0u;
        g_gs_sync_v_callback_gp = 0u;
        g_gs_sync_v_callback_sp = 0u;
        g_gs_sync_v_callback_stack_base = 0u;
        g_gs_sync_v_callback_stack_top = 0u;
        g_gs_sync_v_callback_bad_pc_logs = 0u;
    }
    resetGsSyncVState();
}

void dispatchGsSyncVCallback(uint8_t *rdram, PS2Runtime *runtime, uint64_t tick)
{
    if (!rdram || !runtime)
    {
        return;
    }

    uint32_t callback = 0u;
    uint32_t gp = 0u;
    uint32_t callbackStackTop = 0u;
    const uint64_t callbackTick = (tick != 0u) ? tick : ps2_syscalls::GetCurrentVSyncTick();
    {
        std::lock_guard<std::mutex> lock(g_gs_sync_v_callback_mutex);
        callback = g_gs_sync_v_callback_func;
        gp = g_gs_sync_v_callback_gp;
        callbackStackTop = g_gs_sync_v_callback_stack_top;
        if (callback == 0u)
        {
            return;
        }
    }

    if (!runtime->hasFunction(callback))
    {
        return;
    }

    if (callbackStackTop == 0u)
    {
        constexpr uint32_t kCallbackStackSize = 0x4000u;
        const uint32_t stackTop = runtime->reserveAsyncCallbackStack(kCallbackStackSize, 16u);
        if (stackTop != 0u)
        {
            std::lock_guard<std::mutex> lock(g_gs_sync_v_callback_mutex);
            if (g_gs_sync_v_callback_stack_top == 0u)
            {
                g_gs_sync_v_callback_stack_base = stackTop - (kCallbackStackSize - 0x10u);
                g_gs_sync_v_callback_stack_top = stackTop;
            }
            callbackStackTop = g_gs_sync_v_callback_stack_top;
        }
    }

    try
    {
        R5900Context callbackCtx{};
        SET_GPR_U32(&callbackCtx, 28, gp);
        SET_GPR_U32(&callbackCtx, 29, (callbackStackTop != 0u) ? callbackStackTop : (PS2_RAM_SIZE - 0x10u));
        SET_GPR_U32(&callbackCtx, 31, 0u);
        SET_GPR_U32(&callbackCtx, 4, static_cast<uint32_t>(callbackTick));
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

    // Full messy
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
    GsDispEnvMem env{};
    if (!readGsDispEnv(rdram, envAddr, env))
    {
        setReturnS32(ctx, -1);
        return;
    }
    applyGsDispEnv(runtime, env);
    setReturnS32(ctx, 0);
}

void sceGsPutDrawEnv(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    uint32_t envAddr = getRegU32(ctx, 4);
    GsRegPairMem pairs[8]{};
    if (!readGsRegPairs(rdram, envAddr, pairs, 8u))
    {
        setReturnS32(ctx, -1);
        return;
    }
    applyGsRegPairs(runtime, pairs, 8u);
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
        resetGsSyncVState();

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
    const uint32_t envAddr = getRegU32(ctx, 4);
    uint32_t psm = getRegU32(ctx, 5);
    uint32_t w = getRegU32(ctx, 6);
    uint32_t h = getRegU32(ctx, 7);
    const uint32_t ztest = readStackU32(rdram, ctx, 16);
    const uint32_t zpsm = readStackU32(rdram, ctx, 20);
    const uint32_t clear = readStackU32(rdram, ctx, 24);
    (void)clear;

    if (w == 0u)
    {
        w = 640u;
    }
    if (h == 0u)
    {
        h = 448u;
    }

    const uint32_t fbw = std::max<uint32_t>(1u, (w + 63u) / 64u);
    const uint64_t pmode = makePmode(1u, 1u, 0u, 0u, 0u, 0x80u);
    const uint64_t smode2 =
        (static_cast<uint64_t>(g_gparam.interlace & 0x1u) << 0) |
        (static_cast<uint64_t>(g_gparam.ffmode & 0x1u) << 1);
    const uint64_t dispfb = makeDispFb(0u, fbw, psm, 0u, 0u);
    const uint64_t display = makeDisplay(636u, 32u, 0u, 0u, w - 1u, h - 1u);

    const int32_t drawWidth = static_cast<int32_t>(w);
    const int32_t drawHeight = static_cast<int32_t>(h);

    uint32_t zbufAddr = 0u;
    {
        R5900Context temp = *ctx;
        sceGszbufaddr(rdram, &temp, runtime);
        zbufAddr = getRegU32(&temp, 2);
    }

    GsDBuffDcMem db{};
    db.disp[0].pmode = pmode;
    db.disp[0].smode2 = smode2;
    db.disp[0].dispfb = dispfb;
    db.disp[0].display = display;
    db.disp[0].bgcolor = 0u;
    db.disp[1] = db.disp[0];

    db.giftag0 = {makeGiftagAplusD(14u), 0x0E0E0E0E0E0E0E0EULL};
    seedGsDrawEnv1(db.draw01, drawWidth, drawHeight, 0u, fbw, psm, zbufAddr, zpsm, ztest, false);
    seedGsDrawEnv2(db.draw02, drawWidth, drawHeight, 0u, fbw, psm, zbufAddr, zpsm, ztest, false);
    db.giftag1 = db.giftag0;
    seedGsDrawEnv1(db.draw11, drawWidth, drawHeight, 0u, fbw, psm, zbufAddr, zpsm, ztest, false);
    seedGsDrawEnv2(db.draw12, drawWidth, drawHeight, 0u, fbw, psm, zbufAddr, zpsm, ztest, false);

    if (!writeGsDBuffDc(rdram, envAddr, db))
    {
        setReturnS32(ctx, -1);
        return;
    }
    setReturnS32(ctx, 0);
}

void sceGsSetDefDBuff(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime)
{
    const uint32_t envAddr = getRegU32(ctx, 4);
    uint32_t psm = getRegU32(ctx, 5);
    uint32_t w = getRegU32(ctx, 6);
    uint32_t h = getRegU32(ctx, 7);
    const uint32_t ztest = readStackU32(rdram, ctx, 16);
    const uint32_t zpsm = readStackU32(rdram, ctx, 20);
    const uint32_t clear = readStackU32(rdram, ctx, 24);
    (void)clear;

    if (w == 0u)
    {
        w = 640u;
    }
    if (h == 0u)
    {
        h = 448u;
    }

    const uint32_t fbw = std::max<uint32_t>(1u, (w + 63u) / 64u);
    const uint64_t pmode = makePmode(1u, 1u, 0u, 0u, 0u, 0x80u);
    const uint64_t smode2 =
        (static_cast<uint64_t>(g_gparam.interlace & 0x1u) << 0) |
        (static_cast<uint64_t>(g_gparam.ffmode & 0x1u) << 1);
    const uint64_t dispfb = makeDispFb(0u, fbw, psm, 0u, 0u);
    const uint64_t display = makeDisplay(636u, 32u, 0u, 0u, w - 1u, h - 1u);

    const int32_t drawWidth = static_cast<int32_t>(w);
    const int32_t drawHeight = static_cast<int32_t>(h);

    uint32_t zbufAddr = 0u;
    {
        R5900Context temp = *ctx;
        sceGszbufaddr(rdram, &temp, runtime);
        zbufAddr = getRegU32(&temp, 2);
    }

    GsDBuffMem db{};
    db.disp[0].pmode = pmode;
    db.disp[0].smode2 = smode2;
    db.disp[0].dispfb = dispfb;
    db.disp[0].display = display;
    db.disp[0].bgcolor = 0u;
    db.disp[1] = db.disp[0];

    db.giftag0 = { makeGiftagAplusD(14u), 0x0E0E0E0E0E0E0E0EULL };
    seedGsDrawEnv1(db.draw0, drawWidth, drawHeight, 0u, fbw, psm, zbufAddr, zpsm, ztest, false);
    db.giftag1 = db.giftag0;
    seedGsDrawEnv1(db.draw1, drawWidth, drawHeight, 0u, fbw, psm, zbufAddr, zpsm, ztest, false);

    if (!writeGsDBuff(rdram, envAddr, db))
    {
        setReturnS32(ctx, -1);
        return;
    }
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

    GsDrawEnv1Mem env{};
    seedGsDrawEnv1(env,
                   w,
                   h,
                   0u,
                   fbw,
                   psm,
                   static_cast<uint32_t>(zbuf),
                   param_6 & 0xFu,
                   param_5 & 0x3u,
                   (param_2 & 2u) != 0u);

    uint8_t *const ptr = getMemPtr(rdram, envAddr);
    if (!ptr)
    {
        setReturnS32(ctx, 8);
        return;
    }
    std::memcpy(ptr, &env, sizeof(env));

    setReturnS32(ctx, 8);
}

void sceGsSetDefDrawEnv2(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
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

    GsDrawEnv2Mem env{};
    seedGsDrawEnv2(env,
                   w,
                   h,
                   0u,
                   fbw,
                   psm,
                   static_cast<uint32_t>(zbuf),
                   param_6 & 0xFu,
                   param_5 & 0x3u,
                   (param_2 & 2u) != 0u);

    uint8_t *const ptr = getMemPtr(rdram, envAddr);
    if (!ptr)
    {
        setReturnS32(ctx, 8);
        return;
    }

    std::memcpy(ptr, &env, sizeof(env));
    setReturnS32(ctx, 8);
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
    const uint32_t envAddr = getRegU32(ctx, 4);
    const uint32_t which = getRegU32(ctx, 5) & 1u;

    GsDBuffDcMem db{};
    if (!runtime || !readGsDBuffDc(rdram, envAddr, db))
    {
        setReturnS32(ctx, -1);
        return;
    }

    applyGsDispEnv(runtime, db.disp[which]);
    if (which == 0u)
    {
        applyGsRegPairs(runtime, reinterpret_cast<const GsRegPairMem *>(&db.draw01), 8u);
        applyGsRegPairs(runtime, reinterpret_cast<const GsRegPairMem *>(&db.draw02), 8u);
    }
    else
    {
        applyGsRegPairs(runtime, reinterpret_cast<const GsRegPairMem *>(&db.draw11), 8u);
        applyGsRegPairs(runtime, reinterpret_cast<const GsRegPairMem *>(&db.draw12), 8u);
    }

    setReturnS32(ctx, static_cast<int32_t>(which ^ 1u));
}

void sceGsSwapDBuff(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime)
{
    const uint32_t envAddr = getRegU32(ctx, 4);
    const uint32_t which = getRegU32(ctx, 5) & 1u;

    GsDBuffMem db{};
    if (!runtime || !readGsDBuff(rdram, envAddr, db))
    {
        setReturnS32(ctx, -1);
        return;
    }

    applyGsDispEnv(runtime, db.disp[which]);
    if (which == 0u)
    {
        applyGsRegPairs(runtime, reinterpret_cast<const GsRegPairMem*>(&db.draw0), 8u);
    }
    else
    {
        applyGsRegPairs(runtime, reinterpret_cast<const GsRegPairMem*>(&db.draw1), 8u);
    }

    setReturnS32(ctx, static_cast<int32_t>(which ^ 1u));
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
    const uint64_t tick = ps2_syscalls::WaitForNextVSyncTick(rdram, runtime);
    if (g_gparam.interlace != 0u)
    {
        setReturnS32(ctx, getGsSyncVFieldForTick(tick));
        return;
    }

    setReturnS32(ctx, 1);
}

void sceGsSyncVCallback(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t newCallback = getRegU32(ctx, 4);
    const uint32_t callerPc = ctx ? ctx->pc : 0u;
    const uint32_t callerRa = ctx ? getRegU32(ctx, 31) : 0u;
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

    static uint32_t s_syncVCallbackLogCount = 0u;
    if (s_syncVCallbackLogCount < 128u)
    {
        std::cout << "[sceGsSyncVCallback:set] new=0x" << std::hex << newCallback
                  << " old=0x" << oldCallback
                  << " callerPc=0x" << callerPc
                  << " callerRa=0x" << callerRa
                  << " gp=0x" << gp
                  << " sp=0x" << sp
                  << std::dec << std::endl;
        ++s_syncVCallbackLogCount;
    }

    if (newCallback != 0u)
    {
        ps2_syscalls::EnsureVSyncWorkerRunning(rdram, runtime);
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
