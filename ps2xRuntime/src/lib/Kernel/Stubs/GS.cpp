#include "Common.h"
#include "GS.h"
#include "ps2_log.h"
#include "runtime/ps2_gs_common.h"
#include "runtime/ps2_gs_psmct16.h"

namespace ps2_stubs
{
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
        struct GsDebugProbePoint
        {
            uint32_t x;
            uint32_t y;
        };

        constexpr GsDebugProbePoint kGhostProbePoints[] = {
            {220u, 176u},
            {260u, 208u},
            {320u, 208u},
            {260u, 240u},
            {320u, 240u},
            {260u, 272u},
            {320u, 272u},
        };

        uint64_t makeClearPrim(bool useContext2)
        {
            return static_cast<uint64_t>(GS_PRIM_SPRITE) |
                   (static_cast<uint64_t>(useContext2 ? 1u : 0u) << 9);
        }

        uint64_t makeClearRgbaq(uint32_t rgba)
        {
            return static_cast<uint64_t>(rgba);
        }

        uint64_t makeClearXyz(int32_t x, int32_t y)
        {
            return static_cast<uint64_t>(static_cast<uint16_t>(x << 4)) |
                   (static_cast<uint64_t>(static_cast<uint16_t>(y << 4)) << 16);
        }

        void seedGsClearPacket(GsClearMem &clear,
                               int32_t width,
                               int32_t height,
                               uint32_t rgba,
                               uint32_t ztest,
                               bool useContext2)
        {
            const int32_t offX = 0x800 - (width >> 1);
            const int32_t offY = 0x800 - (height >> 1);
            const uint64_t clearTest = makeTest(0u);
            const uint64_t restoreTest = makeTest(ztest);
            const uint64_t prim = makeClearPrim(useContext2);
            const uint64_t rgbaq = makeClearRgbaq(rgba);
            const uint64_t xyz0 = makeClearXyz(offX, offY);
            const uint64_t xyz1 = makeClearXyz(offX + width, offY + height);
            const uint64_t testReg = useContext2 ? GS_REG_TEST_2 : GS_REG_TEST_1;

            clear.testa = {clearTest, testReg};
            clear.prim = {prim, GS_REG_PRIM};
            clear.rgbaq = {rgbaq, GS_REG_RGBAQ};
            clear.xyz2a = {xyz0, GS_REG_XYZ2};
            clear.xyz2b = {xyz1, GS_REG_XYZ2};
            clear.testb = {restoreTest, testReg};
        }

        bool hasSeededGsClearPacket(const GsClearMem &clear)
        {
            return clear.rgbaq.reg == GS_REG_RGBAQ &&
                   clear.xyz2a.reg == GS_REG_XYZ2 &&
                   clear.xyz2b.reg == GS_REG_XYZ2;
        }

        struct GsTrailingArgs2
        {
            uint32_t arg0 = 0u;
            uint32_t arg1 = 0u;
        };

        struct GsTrailingArgs3
        {
            uint32_t arg0 = 0u;
            uint32_t arg1 = 0u;
            uint32_t arg2 = 0u;
        };

        GsTrailingArgs2 decodeGsTrailingArgs2(uint8_t *rdram, R5900Context *ctx)
        {
            const uint32_t reg8 = getRegU32(ctx, 8);
            const uint32_t reg9 = getRegU32(ctx, 9);
            const uint32_t stack0 = readStackU32(rdram, ctx, 16);
            const uint32_t stack1 = readStackU32(rdram, ctx, 20);

            const bool hasRegArgs = (reg8 != 0u || reg9 != 0u);
            const bool hasStackArgs = (stack0 != 0u || stack1 != 0u);
            if (hasRegArgs || !hasStackArgs)
            {
                return {reg8, reg9};
            }

            return {stack0, stack1};
        }

        GsTrailingArgs3 decodeGsTrailingArgs3(uint8_t *rdram, R5900Context *ctx)
        {
            const uint32_t reg8 = getRegU32(ctx, 8);
            const uint32_t reg9 = getRegU32(ctx, 9);
            const uint32_t reg10 = getRegU32(ctx, 10);
            const uint32_t stack0 = readStackU32(rdram, ctx, 16);
            const uint32_t stack1 = readStackU32(rdram, ctx, 20);
            const uint32_t stack2 = readStackU32(rdram, ctx, 24);

            const bool hasRegArgs = (reg8 != 0u || reg9 != 0u || reg10 != 0u);
            const bool hasStackArgs = (stack0 != 0u || stack1 != 0u || stack2 != 0u);
            if (hasRegArgs || !hasStackArgs)
            {
                return {reg8, reg9, reg10};
            }

            return {stack0, stack1, stack2};
        }

        bool sampleFramebufferPixel(uint8_t *vram,
                                    uint32_t vramSize,
                                    uint32_t fbp,
                                    uint32_t fbw,
                                    uint32_t psm,
                                    uint32_t x,
                                    uint32_t y,
                                    uint32_t &outPixel)
        {
            if (!vram || fbw == 0u)
            {
                return false;
            }

            const uint32_t bytesPerPixel =
                (psm == GS_PSM_CT16 || psm == GS_PSM_CT16S) ? 2u : (psm == GS_PSM_CT32 || psm == GS_PSM_CT24) ? 4u
                                                                                                              : 0u;
            if (bytesPerPixel == 0u)
            {
                return false;
            }

            const uint32_t widthBlocks = (fbw != 0u) ? fbw : 1u;
            const uint32_t strideBytes = fbw * 64u * bytesPerPixel;
            const uint32_t baseBytes = fbp * 8192u;
            uint32_t offset = baseBytes + (y * strideBytes) + (x * bytesPerPixel);
            if (psm == GS_PSM_CT16)
            {
                offset = GSPSMCT16::addrPSMCT16(GSInternal::framePageBaseToBlock(fbp), widthBlocks, x, y);
            }
            else if (psm == GS_PSM_CT16S)
            {
                offset = GSPSMCT16::addrPSMCT16S(GSInternal::framePageBaseToBlock(fbp), widthBlocks, x, y);
            }
            if (offset + bytesPerPixel > vramSize)
            {
                return false;
            }

            if (bytesPerPixel == 4u)
            {
                std::memcpy(&outPixel, vram + offset, sizeof(outPixel));
                if (psm == GS_PSM_CT24)
                {
                    outPixel |= 0xFF000000u;
                }
                return true;
            }

            uint16_t packed = 0u;
            std::memcpy(&packed, vram + offset, sizeof(packed));
            const uint32_t r = ((packed >> 0) & 0x1Fu) << 3;
            const uint32_t g = ((packed >> 5) & 0x1Fu) << 3;
            const uint32_t b = ((packed >> 10) & 0x1Fu) << 3;
            const uint32_t a = (packed & 0x8000u) ? 0x80u : 0x00u;
            outPixel = r | (g << 8) | (b << 16) | (a << 24);
            return true;
        }

        bool sampleFrameRegPixel(PS2Runtime *runtime,
                                 uint64_t frameReg,
                                 uint32_t x,
                                 uint32_t y,
                                 uint32_t &outPixel)
        {
            if (!runtime)
            {
                return false;
            }

            const uint32_t fbp = static_cast<uint32_t>(frameReg & 0x1FFu);
            const uint32_t fbw = static_cast<uint32_t>((frameReg >> 16) & 0x3Fu);
            const uint32_t psm = static_cast<uint32_t>((frameReg >> 24) & 0x3Fu);
            return sampleFramebufferPixel(runtime->memory().getGSVRAM(), PS2_GS_VRAM_SIZE, fbp, fbw, psm, x, y, outPixel);
        }

        bool sampleDispFbPixel(PS2Runtime *runtime,
                               uint64_t dispfb,
                               uint32_t x,
                               uint32_t y,
                               uint32_t &outPixel)
        {
            if (!runtime)
            {
                return false;
            }

            const uint32_t fbp = static_cast<uint32_t>(dispfb & 0x1FFu);
            const uint32_t fbw = static_cast<uint32_t>((dispfb >> 9) & 0x3Fu);
            const uint32_t psm = static_cast<uint32_t>((dispfb >> 15) & 0x1Fu);
            return sampleFramebufferPixel(runtime->memory().getGSVRAM(), PS2_GS_VRAM_SIZE, fbp, fbw, psm, x, y, outPixel);
        }

        void logSwapProbeStage(PS2Runtime *runtime,
                               const char *stage,
                               uint32_t which,
                               uint64_t drawFrameReg,
                               uint64_t dispfb,
                               bool hasClearPacket)
        {
            static uint32_t s_swapProbeCount = 0u;
            if (!runtime || s_swapProbeCount >= 24u)
            {
                return;
            }

            PS2_IF_AGRESSIVE_LOGS({
                RUNTIME_LOG("[gs:probe] stage=" << stage
                                                << " which=" << which
                                                << " clear=" << static_cast<uint32_t>(hasClearPacket ? 1u : 0u));

                for (const auto &probe : kGhostProbePoints)
                {
                    uint32_t page0Pixel = 0u;
                    uint32_t page150Pixel = 0u;
                    const bool havePage0 = sampleFrameRegPixel(runtime, drawFrameReg, probe.x, probe.y, page0Pixel);
                    const bool havePage150 = sampleDispFbPixel(runtime, dispfb, probe.x, probe.y, page150Pixel);
                    if (havePage0)
                    {
                        RUNTIME_LOG(" p0[" << probe.x << "," << probe.y << "]=0x"
                                           << std::hex << page0Pixel << std::dec);
                    }
                    if (havePage150)
                    {
                        RUNTIME_LOG(" p150[" << probe.x << "," << probe.y << "]=0x"
                                             << std::hex << page150Pixel << std::dec);
                    }
                }
                RUNTIME_LOG(std::endl);
                ++s_swapProbeCount;
            });
        }

        void applyGsClearPacket(PS2Runtime *runtime, const GsClearMem &clear)
        {
            if (!runtime || !runtime->syncCoreSubsystems() || !hasSeededGsClearPacket(clear))
            {
                return;
            }

            runtime->gs().writeRegister(static_cast<uint8_t>(clear.testa.reg & 0xFFu), clear.testa.value);
            runtime->gs().writeRegister(static_cast<uint8_t>(clear.prim.reg & 0xFFu), clear.prim.value);
            runtime->gs().writeRegister(static_cast<uint8_t>(clear.rgbaq.reg & 0xFFu), clear.rgbaq.value);
            runtime->gs().writeRegister(static_cast<uint8_t>(clear.xyz2a.reg & 0xFFu), clear.xyz2a.value);
            runtime->gs().writeRegister(static_cast<uint8_t>(clear.xyz2b.reg & 0xFFu), clear.xyz2b.value);
            runtime->gs().writeRegister(static_cast<uint8_t>(clear.testb.reg & 0xFFu), clear.testb.value);
        }
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
            static uint32_t s_missingCallbackLogCount = 0u;
            if (s_missingCallbackLogCount < 32u)
            {
                std::cerr << "[sceGsSyncVCallback:missing] cb=0x" << std::hex << callback
                          << " gp=0x" << gp
                          << " tick=0x" << callbackTick
                          << std::dec << std::endl;
                ++s_missingCallbackLogCount;
            }
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

            static uint32_t s_dispatchLogCount = 0u;
            const bool shouldLogDispatch = (s_dispatchLogCount < 64u);
            if (shouldLogDispatch)
            {
                RUNTIME_LOG("[sceGsSyncVCallback:dispatch] cb=0x" << std::hex << callback
                                                                  << " gp=0x" << gp
                                                                  << " sp=0x" << getRegU32(&callbackCtx, 29)
                                                                  << " tick=0x" << callbackTick
                                                                  << std::dec << std::endl);
            }

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

            if (shouldLogDispatch)
            {
                RUNTIME_LOG("[sceGsSyncVCallback:return] cb=0x" << std::hex << callback
                                                                << " finalPc=0x" << callbackCtx.pc
                                                                << " ra=0x" << getRegU32(&callbackCtx, 31)
                                                                << " steps=0x" << steps
                                                                << std::dec << std::endl);
                ++s_dispatchLogCount;
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
        if (!runtime || !runtime->syncCoreSubsystems() || !readGsImage(rdram, imgAddr, img))
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
        const uint32_t headerQwc = 6u;
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
            runtime->guestFree(pktAddr);
            setReturnS32(ctx, -1);
            return;
        }

        uint32_t dbp = (static_cast<uint32_t>(img.vram_addr) * 2048u) / 256u;
        uint32_t dsax = static_cast<uint32_t>(img.x);
        uint32_t dsay = static_cast<uint32_t>(img.y);

        // Full messy
        uint64_t *q = reinterpret_cast<uint64_t *>(pkt);
        q[0] = makeGiftagAplusD(4u);
        q[1] = 0xEULL;
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

        std::memcpy(pkt + headerQwc * 16u, src, totalImageBytes);

        constexpr uint32_t GIF_CHANNEL = 0x1000A000;
        constexpr uint32_t CHCR_STR_MODE0 = 0x101u;
        auto &mem = runtime->memory();
        mem.writeIORegister(GIF_CHANNEL + 0x10u, pktAddr);
        mem.writeIORegister(GIF_CHANNEL + 0x20u, totalQwc & 0xFFFFu);
        mem.writeIORegister(GIF_CHANNEL + 0x00u, CHCR_STR_MODE0);
        mem.processPendingTransfers();
        runtime->guestFree(pktAddr);

        setReturnS32(ctx, 0);
    }

    void sceGsExecStoreImage(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t imgAddr = getRegU32(ctx, 4);
        uint32_t dstAddr = getRegU32(ctx, 5);

        GsImageMem img{};
        if (!runtime || !runtime->syncCoreSubsystems() || !readGsImage(rdram, imgAddr, img))
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
            runtime->guestFree(pktAddr);
            setReturnS32(ctx, -1);
            return;
        }

        uint64_t *q = reinterpret_cast<uint64_t *>(pkt);
        q[0] = makeGiftagAplusD(4u);
        q[1] = 0xEULL;
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
        runtime->guestFree(pktAddr);

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
            if (runtime && !runtime->syncCoreSubsystems())
            {
                setReturnS32(ctx, -1);
                return;
            }

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
                uint32_t pktAddr = runtime->guestMalloc(128u, 16u);
                if (pktAddr != 0u)
                {
                    uint8_t *pkt = getMemPtr(rdram, pktAddr);
                    if (pkt)
                    {
                        uint64_t *q = reinterpret_cast<uint64_t *>(pkt);
                        q[0] = makeGiftagAplusD(7u);
                        q[1] = 0xEULL;
                        q[2] = pmode;
                        q[3] = 0x41ULL;
                        q[4] = smode2;
                        q[5] = 0x42ULL;
                        q[6] = dispfb;
                        q[7] = 0x59ULL;
                        q[8] = display;
                        q[9] = 0x5aULL;
                        q[10] = dispfb;
                        q[11] = 0x5bULL;
                        q[12] = display;
                        q[13] = 0x5cULL;
                        q[14] = bgcolor;
                        q[15] = 0x5fULL;
                        constexpr uint32_t GIF_CHANNEL = 0x1000A000;
                        constexpr uint32_t CHCR_STR_MODE0 = 0x101u;
                        auto &mem = runtime->memory();
                        mem.writeIORegister(GIF_CHANNEL + 0x10u, pktAddr);
                        mem.writeIORegister(GIF_CHANNEL + 0x20u, 8u);
                        mem.writeIORegister(GIF_CHANNEL + 0x00u, CHCR_STR_MODE0);
                        mem.processPendingTransfers();
                        runtime->guestFree(pktAddr);
                    }
                    else
                    {
                        runtime->guestFree(pktAddr);
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
        const GsTrailingArgs3 trailing = decodeGsTrailingArgs3(rdram, ctx);
        const uint32_t ztest = trailing.arg0;
        const uint32_t zpsm = trailing.arg1;
        const uint32_t clear = trailing.arg2;

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
        const uint64_t display = makeDisplay(636u, 32u, 0u, 0u, w - 1u, h - 1u);

        const int32_t drawWidth = static_cast<int32_t>(w);
        const int32_t drawHeight = static_cast<int32_t>(h);

        uint32_t zbufAddr = 0u;
        {
            R5900Context temp = *ctx;
            sceGszbufaddr(rdram, &temp, runtime);
            zbufAddr = getRegU32(&temp, 2);
        }

        const uint32_t fbp1 = zbufAddr;
        const uint64_t dispfb0 = makeDispFb(fbp1, fbw, psm, 0u, 0u);
        const uint64_t dispfb1 = makeDispFb(0u, fbw, psm, 0u, 0u);

        GsDBuffDcMem db{};
        db.disp[0].pmode = pmode;
        db.disp[0].smode2 = smode2;
        db.disp[0].dispfb = dispfb0;
        db.disp[0].display = display;
        db.disp[0].bgcolor = 0u;
        db.disp[1] = db.disp[0];
        db.disp[1].dispfb = dispfb1;

        const bool seedClear = clear != 0u;
        db.giftag0 = {makeGiftagAplusD(seedClear ? 22u : 16u), 0xEULL};
        seedGsDrawEnv1(db.draw01, drawWidth, drawHeight, 0u, fbw, psm, zbufAddr, zpsm, ztest, false);
        seedGsDrawEnv2(db.draw02, drawWidth, drawHeight, 0u, fbw, psm, zbufAddr, zpsm, ztest, false);
        db.giftag1 = db.giftag0;
        seedGsDrawEnv1(db.draw11, drawWidth, drawHeight, fbp1, fbw, psm, zbufAddr, zpsm, ztest, false);
        seedGsDrawEnv2(db.draw12, drawWidth, drawHeight, fbp1, fbw, psm, zbufAddr, zpsm, ztest, false);
        if (seedClear)
        {
            seedGsClearPacket(db.clear0, drawWidth, drawHeight, 0u, ztest, false);
            seedGsClearPacket(db.clear1, drawWidth, drawHeight, 0u, ztest, true);
        }

        if (!writeGsDBuffDc(rdram, envAddr, db))
        {
            setReturnS32(ctx, -1);
            return;
        }
        setReturnS32(ctx, 0);
    }

    void sceGsSetDefDBuff(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
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

        db.giftag0 = {makeGiftagAplusD(14u), 0x0E0E0E0E0E0E0E0EULL};
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
        const GsTrailingArgs2 trailing = decodeGsTrailingArgs2(rdram, ctx);
        uint32_t dx = trailing.arg0;
        uint32_t dy = trailing.arg1;

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
        const GsTrailingArgs2 trailing = decodeGsTrailingArgs2(rdram, ctx);
        uint32_t param_5 = trailing.arg0;
        uint32_t param_6 = trailing.arg1;

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
        const GsTrailingArgs2 trailing = decodeGsTrailingArgs2(rdram, ctx);
        uint32_t param_5 = trailing.arg0;
        uint32_t param_6 = trailing.arg1;

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

        const bool hasClearPacket = (which == 0u) ? hasSeededGsClearPacket(db.clear0)
                                                  : hasSeededGsClearPacket(db.clear1);
        const uint64_t debugDrawFrameReg = (which == 0u) ? db.draw01.frame1.value
                                                         : db.draw11.frame1.value;

        applyGsDispEnv(runtime, db.disp[which]);
        static uint32_t s_swapDbuffLogCount = 0u;
        if (s_swapDbuffLogCount < 32u)
        {
            const uint32_t dispFbp = static_cast<uint32_t>(db.disp[which].dispfb & 0x1FFu);
            const uint32_t clearContext = (which == 0u)
                                              ? static_cast<uint32_t>((db.clear0.prim.value >> 9) & 0x1u)
                                              : static_cast<uint32_t>((db.clear1.prim.value >> 9) & 0x1u);
            RUNTIME_LOG("[gs:swapdbuff] which=" << which
                                                << " env=0x" << std::hex << envAddr
                                                << " dispfb=0x" << db.disp[which].dispfb
                                                << " display=0x" << db.disp[which].display
                                                << " pmode=0x" << db.disp[which].pmode
                                                << " dispFbp=" << dispFbp
                                                << " clearCtxt=" << clearContext
                                                << std::dec << std::endl);
            ++s_swapDbuffLogCount;
        }
        logSwapProbeStage(runtime, "swap-pre", which, debugDrawFrameReg, db.disp[which].dispfb, hasClearPacket);
        if (which == 0u)
        {
            applyGsRegPairs(runtime, reinterpret_cast<const GsRegPairMem *>(&db.draw01), 8u);
            applyGsRegPairs(runtime, reinterpret_cast<const GsRegPairMem *>(&db.draw02), 8u);
            if (hasSeededGsClearPacket(db.clear0))
            {
                const uint32_t clearContext = static_cast<uint32_t>((db.clear0.prim.value >> 9) & 0x1u);
                runtime->gs().clearFramebufferContext(clearContext, static_cast<uint32_t>(db.clear0.rgbaq.value));
                logSwapProbeStage(runtime, "swap-post-clear", which, db.draw01.frame1.value, db.disp[which].dispfb, true);
            }
            applyGsClearPacket(runtime, db.clear0);
            logSwapProbeStage(runtime, "swap-post", which, db.draw01.frame1.value, db.disp[which].dispfb, hasClearPacket);
        }
        else
        {
            applyGsRegPairs(runtime, reinterpret_cast<const GsRegPairMem *>(&db.draw11), 8u);
            applyGsRegPairs(runtime, reinterpret_cast<const GsRegPairMem *>(&db.draw12), 8u);
            if (hasSeededGsClearPacket(db.clear1))
            {
                const uint32_t clearContext = static_cast<uint32_t>((db.clear1.prim.value >> 9) & 0x1u);
                runtime->gs().clearFramebufferContext(clearContext, static_cast<uint32_t>(db.clear1.rgbaq.value));
                logSwapProbeStage(runtime, "swap-post-clear", which, db.draw11.frame1.value, db.disp[which].dispfb, true);
            }
            applyGsClearPacket(runtime, db.clear1);
            logSwapProbeStage(runtime, "swap-post", which, db.draw11.frame1.value, db.disp[which].dispfb, hasClearPacket);
        }

        setReturnS32(ctx, static_cast<int32_t>(which ^ 1u));
    }

    void sceGsSwapDBuff(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
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
            applyGsRegPairs(runtime, reinterpret_cast<const GsRegPairMem *>(&db.draw0), 8u);
        }
        else
        {
            applyGsRegPairs(runtime, reinterpret_cast<const GsRegPairMem *>(&db.draw1), 8u);
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
            RUNTIME_LOG("[sceGsSyncVCallback:set] new=0x" << std::hex << newCallback
                                                          << " old=0x" << oldCallback
                                                          << " callerPc=0x" << callerPc
                                                          << " callerRa=0x" << callerRa
                                                          << " gp=0x" << gp
                                                          << " sp=0x" << sp
                                                          << std::dec << std::endl);
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

    void Ps2SwapDBuff(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            RUNTIME_LOG("ps2_stub Ps2SwapDBuff");
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }
}
