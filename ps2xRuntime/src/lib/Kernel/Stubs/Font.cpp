#include "Common.h"
#include "Font.h"

namespace ps2_stubs
{
    static void writeU32AtGp(uint8_t *rdram, uint32_t gp, int32_t offset, uint32_t value)
    {
        const uint32_t addr = gp + static_cast<uint32_t>(offset);
        if (uint8_t *p = getMemPtr(rdram, addr))
            *reinterpret_cast<uint32_t *>(p) = value;
    }

    void sceeFontInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t gp = getRegU32(ctx, 28);
        const uint32_t a0 = getRegU32(ctx, 4);
        const uint32_t a1 = getRegU32(ctx, 5);
        const uint32_t a2 = getRegU32(ctx, 6);
        const uint32_t a3 = getRegU32(ctx, 7);
        writeU32AtGp(rdram, gp, -0x7b60, a1);
        writeU32AtGp(rdram, gp, -0x7b5c, a2);
        writeU32AtGp(rdram, gp, -0x7b64, a0);
        writeU32AtGp(rdram, gp, -0x7c98, a3);
        writeU32AtGp(rdram, gp, -0x7b4c, 0x7f7f7f7f);
        writeU32AtGp(rdram, gp, -0x7b50, 0x3f800000);
        writeU32AtGp(rdram, gp, -0x7b54, 0x3f800000);
        writeU32AtGp(rdram, gp, -0x7b58, 0);

        if (runtime && a0 != 0u)
        {
            if ((a0 * 256u) + 64u <= PS2_GS_VRAM_SIZE)
            {
                uint32_t clutData[16];
                for (uint32_t i = 0; i < 16u; ++i)
                {
                    uint8_t alpha = static_cast<uint8_t>((i * 0x80u) / 15u);
                    clutData[i] = (i == 0)
                                      ? 0x00000000u
                                      : (0x80u | (0x80u << 8) | (0x80u << 16) | (static_cast<uint32_t>(alpha) << 24));
                }
                constexpr uint32_t kClutQwc = 4u;
                constexpr uint32_t kHeaderQwc = 6u;
                constexpr uint32_t kTotalQwc = kHeaderQwc + kClutQwc;
                uint32_t pktAddr = runtime->guestMalloc(kTotalQwc * 16u, 16u);
                if (pktAddr != 0u)
                {
                    uint8_t *pkt = getMemPtr(rdram, pktAddr);
                    if (pkt)
                    {
                        uint64_t *q = reinterpret_cast<uint64_t *>(pkt);
                        const uint32_t dbp = a0 & 0x3FFFu;
                        constexpr uint8_t psm = 0u;
                        q[0] = makeGiftagAplusD(4u);
                        q[1] = 0xEULL;
                        q[2] = (static_cast<uint64_t>(dbp) << 32) | (1ULL << 48) | (static_cast<uint64_t>(psm) << 56);
                        q[3] = 0x50ULL;
                        q[4] = 0ULL;
                        q[5] = 0x51ULL;
                        q[6] = 16ULL | (1ULL << 32);
                        q[7] = 0x52ULL;
                        q[8] = 0ULL;
                        q[9] = 0x53ULL;
                        q[10] = (2ULL << 58) | (kClutQwc & 0x7FFF) | (1ULL << 15);
                        q[11] = 0ULL;
                        std::memcpy(pkt + 12u * 8u, clutData, 64u);
                        constexpr uint32_t GIF_CHANNEL = 0x1000A000;
                        constexpr uint32_t CHCR_STR_MODE0 = 0x101u;
                        runtime->memory().writeIORegister(GIF_CHANNEL + 0x10u, pktAddr);
                        runtime->memory().writeIORegister(GIF_CHANNEL + 0x20u, kTotalQwc & 0xFFFFu);
                        runtime->memory().writeIORegister(GIF_CHANNEL + 0x00u, CHCR_STR_MODE0);
                        runtime->memory().processPendingTransfers();
                    }
                }
            }
        }

        setReturnS32(ctx, static_cast<int32_t>(a0 + 4));
    }

    void sceeFontLoadFont(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static constexpr uint32_t kFontBase = 0x176148u;
        static constexpr uint32_t kFontEntrySz = 0x24u;

        const uint32_t fontDataAddr = getRegU32(ctx, 4);
        const int fontId = static_cast<int>(getRegU32(ctx, 5));
        const int tbp0 = static_cast<int>(getRegU32(ctx, 7));

        if (!fontDataAddr || !runtime)
        {
            setReturnS32(ctx, tbp0);
            return;
        }

        const uint8_t *fontPtr = getConstMemPtr(rdram, fontDataAddr);
        if (!fontPtr)
        {
            setReturnS32(ctx, tbp0);
            return;
        }

        int width = static_cast<int>(*reinterpret_cast<const uint32_t *>(fontPtr + 0x00u));
        int height = static_cast<int>(*reinterpret_cast<const uint32_t *>(fontPtr + 0x04u));
        uint32_t raw8 = *reinterpret_cast<const uint32_t *>(fontPtr + 0x08u);
        int fontDataSz = static_cast<int>(*reinterpret_cast<const uint32_t *>(fontPtr + 0x0cu));

        uint32_t pointsize = raw8;
        uint32_t fontOff = static_cast<uint32_t>(fontId * static_cast<int>(kFontEntrySz));
        if (raw8 & 0x40000000u)
        {
            pointsize = raw8 - 0x40000000u;
            if (uint8_t *p = getMemPtr(rdram, kFontBase + fontOff + 0x20u))
                *reinterpret_cast<uint32_t *>(p) = 1u;
        }
        else
        {
            if (uint8_t *p = getMemPtr(rdram, kFontBase + fontOff + 0x20u))
                *reinterpret_cast<uint32_t *>(p) = 0u;
        }

        int tw = (width >= 0) ? (width >> 6) : ((width + 0x3f) >> 6);
        int qwc = (fontDataSz >= 0) ? (fontDataSz >> 4) : ((fontDataSz + 0xf) >> 4);

        uint32_t glyphSrc = fontDataAddr + static_cast<uint32_t>(fontDataSz) + 0x10u;
        uint32_t glyphAlloc = runtime->guestMalloc(0x2010u, 0x40u);
        if (uint8_t *p = getMemPtr(rdram, kFontBase + fontOff))
            *reinterpret_cast<uint32_t *>(p) = glyphAlloc;

        if (glyphAlloc != 0u)
        {
            uint8_t *dst = getMemPtr(rdram, glyphAlloc);
            const uint8_t *src = getConstMemPtr(rdram, glyphSrc);
            if (dst && src)
                std::memcpy(dst, src, 0x2010u);
        }

        uint32_t isDoubleByte = 0;
        if (const uint8_t *p = getConstMemPtr(rdram, kFontBase + fontOff + 0x20u))
            isDoubleByte = *reinterpret_cast<const uint32_t *>(p);
        if (isDoubleByte == 0u)
        {
            uint32_t kernSrc = glyphSrc + 0x2010u;
            uint32_t kernAlloc = runtime->guestMalloc(0xc400u, 0x40u);
            if (glyphAlloc != 0u)
                *reinterpret_cast<uint32_t *>(getMemPtr(rdram, glyphAlloc + 0x2000u)) = kernAlloc;
            if (kernAlloc != 0u)
            {
                uint8_t *dst = getMemPtr(rdram, kernAlloc);
                const uint8_t *src = getConstMemPtr(rdram, kernSrc);
                if (dst && src)
                    std::memcpy(dst, src, 0xc400u);
            }
        }

        auto writeFontField = [&](uint32_t off, uint32_t val)
        {
            if (uint8_t *p = getMemPtr(rdram, kFontBase + fontOff + off))
                *reinterpret_cast<uint32_t *>(p) = val;
        };
        writeFontField(0x18u, pointsize);
        writeFontField(0x08u, static_cast<uint32_t>(tbp0));
        writeFontField(0x0cu, static_cast<uint32_t>(tw));

        int logW = 0;
        for (int w = width; w != 1 && w != 0; w = static_cast<int>(static_cast<uint32_t>(w) >> 1))
            logW++;
        writeFontField(0x10u, static_cast<uint32_t>(logW));

        int logH = 0;
        for (int h = height; h != 1 && h != 0; h = static_cast<int>(static_cast<uint32_t>(h) >> 1))
            logH++;
        writeFontField(0x14u, static_cast<uint32_t>(logH));
        writeFontField(0x04u, 0u);
        writeFontField(0x1cu, getRegU32(ctx, 6));

        if (qwc > 0)
        {
            const uint32_t imageBytes = static_cast<uint32_t>(qwc) * 16u;
            const uint8_t psm = 20u;
            const uint32_t headerQwc = 12u;
            const uint32_t imageQwc = static_cast<uint32_t>(qwc);
            const uint32_t totalQwc = headerQwc + imageQwc;
            uint32_t pktAddr = runtime->guestMalloc(totalQwc * 16u, 16u);
            if (pktAddr != 0u)
            {
                uint8_t *pkt = getMemPtr(rdram, pktAddr);
                const uint8_t *imgSrc = getConstMemPtr(rdram, fontDataAddr + 0x10u);
                if (pkt && imgSrc)
                {
                    uint64_t *q = reinterpret_cast<uint64_t *>(pkt);
                    const uint32_t dbp = static_cast<uint32_t>(tbp0) & 0x3FFFu;
                    const uint32_t dbw = static_cast<uint32_t>(tw > 0 ? tw : 1) & 0x3Fu;
                    const uint32_t rrw = static_cast<uint32_t>(width > 0 ? width : 64);
                    const uint32_t rrh = static_cast<uint32_t>(height > 0 ? height : 1);

                    q[0] = makeGiftagAplusD(4u);
                    q[1] = 0xEULL;
                    q[2] = (static_cast<uint64_t>(psm) << 24) | (1ULL << 16) |
                           (static_cast<uint64_t>(dbp) << 32) | (static_cast<uint64_t>(dbw) << 48) |
                           (static_cast<uint64_t>(psm) << 56);
                    q[3] = 0x50ULL;
                    q[4] = 0ULL;
                    q[5] = 0x51ULL;
                    q[6] = (static_cast<uint64_t>(rrh) << 32) | static_cast<uint64_t>(rrw);
                    q[7] = 0x52ULL;
                    q[8] = 0ULL;
                    q[9] = 0x53ULL;
                    q[10] = (2ULL << 58) | (imageQwc & 0x7FFF) | (1ULL << 15);
                    q[11] = 0ULL;
                    std::memcpy(pkt + 12 * 8, imgSrc, imageBytes);

                    constexpr uint32_t GIF_CHANNEL = 0x1000A000;
                    constexpr uint32_t CHCR_STR_MODE0 = 0x101u;
                    runtime->memory().writeIORegister(GIF_CHANNEL + 0x10u, pktAddr);
                    runtime->memory().writeIORegister(GIF_CHANNEL + 0x20u, totalQwc & 0xFFFFu);
                    runtime->memory().writeIORegister(GIF_CHANNEL + 0x00u, CHCR_STR_MODE0);
                }
            }
        }

        int retTbp = tbp0 + ((fontDataSz >= 0 ? fontDataSz : fontDataSz + 0x7f) >> 7);
        setReturnS32(ctx, retTbp);
    }

    static constexpr uint32_t kFontBase = 0x176148u;
    static constexpr uint32_t kFontEntrySz = 0x24u;

    void sceeFontGenerateString(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const float sclx = ctx->f[12];
        const float scly = ctx->f[13];
        const uint32_t bufAddr = getRegU32(ctx, 4);
        const uint64_t paramX = GPR_U64(ctx, 5);
        const int64_t paramY = GPR_S64(ctx, 6);
        const int paramW = static_cast<int>(getRegU32(ctx, 7));
        const int paramH = static_cast<int>(getRegU32(ctx, 8));
        const uint32_t colour = getRegU32(ctx, 9);
        const int alignCh = static_cast<int8_t>(getRegU32(ctx, 10) & 0xffu);
        const int fontId = static_cast<int>(getRegU32(ctx, 11));

        const uint32_t sp = getRegU32(ctx, 29);
        const uint32_t strAddr = FAST_READ32(sp + 0x00u);
        const uint32_t param14 = FAST_READ32(sp + 0x18u);

        if (bufAddr == 0u)
        {
            setReturnS32(ctx, 0);
            ctx->pc = getRegU32(ctx, 31);
            return;
        }

        const uint32_t gp = getRegU32(ctx, 28);
        const uint32_t fontModeAdj = FAST_READ32(gp + static_cast<uint32_t>(static_cast<int32_t>(-0x7c98)));
        const uint32_t shiftAmt = fontModeAdj & 0x1fu;
        const int scrHeight = static_cast<int>(FAST_READ32(gp + static_cast<uint32_t>(static_cast<int32_t>(-0x7b5c))));
        const int scrWidth = static_cast<int>(FAST_READ32(gp + static_cast<uint32_t>(static_cast<int32_t>(-0x7b60))));
        const uint32_t fontClut = FAST_READ32(gp + static_cast<uint32_t>(static_cast<int32_t>(-0x7b64)));

        const uint32_t fontOff = static_cast<uint32_t>(fontId * static_cast<int>(kFontEntrySz));
        const int lineH = static_cast<int>(FAST_READ32(kFontBase + fontOff + 0x18u));

        int iVar21 = 0;
        int iStack_dc = 0;
        uint32_t uStack_d8 = 0;
        int iVar15 = 0;

        int16_t sVar8;
        {
            int yStepRaw = static_cast<int>(static_cast<float>((lineH + 6) * 16) * scly);
            sVar8 = static_cast<int16_t>((static_cast<int>(paramY) + 0x700) * 16) + static_cast<int16_t>(yStepRaw >> static_cast<int>(shiftAmt));
        }

        int16_t baseX = static_cast<int16_t>((static_cast<int>(paramX) + 0x6c0) * 16);

        if (param14 != 0u)
        {
            int64_t clipY1 = static_cast<int64_t>(static_cast<int>(paramY) + paramH);
            int64_t clipX1 = static_cast<int64_t>(static_cast<int>(paramX) + paramW);
            if (clipY1 > scrHeight - 1)
                clipY1 = static_cast<int64_t>(scrHeight - 1);
            if (clipX1 > scrWidth - 1)
                clipX1 = static_cast<int64_t>(scrWidth - 1);
            int64_t clipY0 = 0;
            if (paramY > 0)
                clipY0 = paramY;
            uint64_t clipX0 = 0;
            if (static_cast<int64_t>(paramX) > 0)
                clipX0 = paramX;

            uint64_t scissor = clipX0 | (static_cast<uint64_t>(static_cast<uint32_t>(clipX1)) << 16) | (static_cast<uint64_t>(static_cast<uint32_t>(clipY0)) << 32) | (static_cast<uint64_t>(static_cast<uint32_t>(clipY1)) << 48);

            FAST_WRITE64(bufAddr + 0x00, 0x1000000000000005ull);
            FAST_WRITE64(bufAddr + 0x08, 0x0eull);
            FAST_WRITE64(bufAddr + 0x10, scissor);
            FAST_WRITE64(bufAddr + 0x18, 0x40ull);
            FAST_WRITE64(bufAddr + 0x20, 0x20000ull);
            FAST_WRITE64(bufAddr + 0x28, 0x47ull);
            FAST_WRITE64(bufAddr + 0x30, 0x44ull);
            FAST_WRITE64(bufAddr + 0x38, 0x42ull);
            FAST_WRITE64(bufAddr + 0x40, 0x160ull);
            FAST_WRITE64(bufAddr + 0x48, 0x14ull);
            FAST_WRITE64(bufAddr + 0x50, 0x156ull);
            FAST_WRITE64(bufAddr + 0x58, 0ull);
            FAST_WRITE64(bufAddr + 0x60, 0x1000000000000001ull);
            FAST_WRITE64(bufAddr + 0x68, 0x0eull);

            uint64_t iVar5 = static_cast<uint64_t>(FAST_READ32(kFontBase + fontOff + 0x08u));
            uint64_t iVar22 = static_cast<uint64_t>(FAST_READ32(kFontBase + fontOff + 0x0cu));
            uint64_t iVar3 = static_cast<uint64_t>(FAST_READ32(kFontBase + fontOff + 0x10u));
            uint64_t iVar4 = static_cast<uint64_t>(FAST_READ32(kFontBase + fontOff + 0x14u));

            uint64_t tex0 = iVar5 | 0x2000000000000000ull | (iVar22 << 14) | 0x400000000ull | (iVar3 << 26) | 0x1400000ull | (iVar4 << 30) | (static_cast<uint64_t>(fontClut) << 37);

            FAST_WRITE64(bufAddr + 0x70, tex0);
            FAST_WRITE64(bufAddr + 0x78, 6ull);
            FAST_WRITE64(bufAddr + 0x80, 0x1000000000000001ull);
            FAST_WRITE64(bufAddr + 0x88, 0x0eull);
            FAST_WRITE64(bufAddr + 0x90, static_cast<uint64_t>(colour));
            FAST_WRITE64(bufAddr + 0x98, 1ull);

            iVar21 = 10;
        }

        int iVar22_qw = iVar21 + 1;
        uint32_t s2 = bufAddr + static_cast<uint32_t>(iVar22_qw * 16);
        uint32_t uVar20 = 0;

        size_t sLen = 0;
        {
            const char *hostStr = reinterpret_cast<const char *>(getConstMemPtr(rdram, strAddr));
            if (hostStr)
                sLen = ::strlen(hostStr);
        }

        while (uVar20 < sLen)
        {
            uint8_t bVar1 = FAST_READ8(strAddr + uVar20);
            uint32_t uVar9 = static_cast<uint32_t>(bVar1);
            int8_t chSigned = static_cast<int8_t>(bVar1);

            if (uStack_d8 < 0x21u)
            {
                goto label_check_printable;
            }

            if (uVar9 > 0x20u)
            {
                uint32_t dat176168 = FAST_READ32(kFontBase + fontOff + 0x20u);
                if (dat176168 == 0u)
                {
                    uint32_t fontPtr0 = FAST_READ32(kFontBase + fontOff);
                    uint32_t tableAddr = FAST_READ32(fontPtr0 + 0x2000u);
                    int8_t kern = static_cast<int8_t>(FAST_READ8(tableAddr - 0x1c20u + uStack_d8 * 0xe0u + uVar9));
                    iVar15 += static_cast<int>(static_cast<float>(static_cast<int>(kern)) * sclx);
                }
                goto label_check_printable;
            }

            goto label_space;

        label_check_printable:
            if (uVar9 < 0x21u)
            {
                goto label_space;
            }

            {
                int glyphIdx = static_cast<int>(chSigned);
                uint32_t iVar19_off = static_cast<uint32_t>(glyphIdx * 0x20);

                if (param14 != 0u)
                {
                    uint32_t fontPtr = FAST_READ32(kFontBase + fontOff);
                    int16_t sVar7 = baseX + static_cast<int16_t>(iVar15);

                    iVar22_qw += 2;
                    iStack_dc += 1;

                    uint16_t wU0 = FAST_READ16(fontPtr + iVar19_off + 0);
                    uint16_t wV0 = FAST_READ16(fontPtr + iVar19_off + 2);
                    FAST_WRITE16(s2 + 0x00, wU0);
                    FAST_WRITE16(s2 + 0x02, wV0);

                    int16_t dx0 = static_cast<int16_t>(FAST_READ16(fontPtr + iVar19_off + 8));
                    int16_t dy0 = static_cast<int16_t>(FAST_READ16(fontPtr + iVar19_off + 10));
                    uint16_t wX0 = static_cast<uint16_t>(sVar7 + static_cast<int16_t>(static_cast<int>(static_cast<float>(static_cast<int>(dx0)) * sclx)));
                    int yVal0 = static_cast<int>(static_cast<float>(static_cast<int>(dy0)) * scly) >> static_cast<int>(shiftAmt);
                    uint16_t wY0 = static_cast<uint16_t>(sVar8 + static_cast<int16_t>(yVal0));
                    FAST_WRITE16(s2 + 0x08, wX0);
                    FAST_WRITE16(s2 + 0x0a, wY0);
                    FAST_WRITE32(s2 + 0x0c, 1u);

                    s2 += 0x10u;

                    uint16_t wU1 = FAST_READ16(fontPtr + iVar19_off + 4);
                    uint16_t wV1 = FAST_READ16(fontPtr + iVar19_off + 6);
                    FAST_WRITE16(s2 + 0x00, wU1);
                    FAST_WRITE16(s2 + 0x02, wV1);

                    int16_t dx1 = static_cast<int16_t>(FAST_READ16(fontPtr + iVar19_off + 12));
                    int16_t dy1 = static_cast<int16_t>(FAST_READ16(fontPtr + iVar19_off + 14));
                    uint16_t wX1 = static_cast<uint16_t>(sVar7 + static_cast<int16_t>(static_cast<int>(static_cast<float>(static_cast<int>(dx1)) * sclx)));
                    int yVal1 = static_cast<int>(static_cast<float>(static_cast<int>(dy1)) * scly) >> static_cast<int>(shiftAmt);
                    uint16_t wY1 = static_cast<uint16_t>(sVar8 + static_cast<int16_t>(yVal1));
                    FAST_WRITE16(s2 + 0x08, wX1);
                    FAST_WRITE16(s2 + 0x0a, wY1);
                    FAST_WRITE32(s2 + 0x0c, 1u);

                    s2 += 0x10u;
                }

                {
                    uint32_t fontPtr = FAST_READ32(kFontBase + fontOff);
                    uint32_t advOff = static_cast<uint32_t>((glyphIdx * 2 + 1) * 16 + 8);
                    int16_t advW = static_cast<int16_t>(FAST_READ16(fontPtr + advOff));
                    iVar15 += static_cast<int>(static_cast<float>(static_cast<int>(advW)) * sclx);
                }
            }
            goto label_next;

        label_space:
        {
            int spaceW = static_cast<int>(FAST_READ32(kFontBase + fontOff + 0x1cu));
            iVar15 += static_cast<int>(static_cast<float>(spaceW) * sclx);
        }

        label_next:
            uStack_d8 = uVar9;
            uVar20++;
        }

        if (param14 != 0u)
        {
            if (alignCh != 'L')
            {
                if (alignCh == 'C' || alignCh == 'R')
                {
                    int shift = paramW * 16 - iVar15;
                    if (alignCh == 'C')
                        shift >>= 1;
                    if (iStack_dc > 0)
                    {
                        uint32_t adj = bufAddr + static_cast<uint32_t>(iVar21 * 16) + 0x20u;
                        for (int k = 0; k < iStack_dc; k++)
                        {
                            int16_t oldX0 = static_cast<int16_t>(FAST_READ16(adj - 8u));
                            int16_t oldX1 = static_cast<int16_t>(FAST_READ16(adj + 8u));
                            FAST_WRITE16(adj - 8u, static_cast<uint16_t>(oldX0 + static_cast<int16_t>(shift)));
                            FAST_WRITE16(adj + 8u, static_cast<uint16_t>(oldX1 + static_cast<int16_t>(shift)));
                            adj += 0x20u;
                        }
                    }
                }
                else if (alignCh == 'J' && sLen > 1)
                {
                    int iVar19_div = static_cast<int>(sLen) - 1;
                    if (iVar19_div == 0)
                        iVar19_div = 1;
                    int spacePer = (paramW * 16 - iVar15) / iVar19_div;
                    uint32_t adj = bufAddr + static_cast<uint32_t>(iVar21 * 16) + 0x20u;
                    int accum = 0;
                    for (uint32_t jj = 0; jj < sLen; jj++)
                    {
                        int8_t jch = static_cast<int8_t>(FAST_READ8(strAddr + jj));
                        if (jch > 0x20)
                        {
                            int16_t oldX0 = static_cast<int16_t>(FAST_READ16(adj - 8u));
                            int16_t oldX1 = static_cast<int16_t>(FAST_READ16(adj + 8u));
                            FAST_WRITE16(adj - 8u, static_cast<uint16_t>(oldX0 + static_cast<int16_t>(accum)));
                            FAST_WRITE16(adj + 8u, static_cast<uint16_t>(oldX1 + static_cast<int16_t>(accum)));
                            adj += 0x20u;
                        }
                        accum += spacePer;
                    }
                }
            }

            if (param14 != 0u)
            {
                uint32_t tagAddr = bufAddr + static_cast<uint32_t>(iVar21 * 16);
                FAST_WRITE64(tagAddr + 0x00, static_cast<uint64_t>(static_cast<uint32_t>(iStack_dc)) | 0x4400000000000000ull);
                FAST_WRITE64(tagAddr + 0x08, 0x5353ull);

                uint32_t endAddr = bufAddr + static_cast<uint32_t>(iVar22_qw * 16);
                FAST_WRITE64(endAddr + 0x00, 0x1000000000008001ull);
                FAST_WRITE64(endAddr + 0x08, 0x0eull);

                int iVar19_end = iVar22_qw + 1;
                uint32_t endAddr2 = bufAddr + static_cast<uint32_t>(iVar19_end * 16);
                FAST_WRITE64(endAddr2 + 0x00, 0x01ff0000027f0000ull);
                FAST_WRITE64(endAddr2 + 0x08, 0x40ull);

                iVar22_qw += 2;
            }
        }

        int ret = 0;
        if (param14 != 0u)
            ret = iVar22_qw;
        setReturnS32(ctx, ret);
        ctx->pc = getRegU32(ctx, 31);
    }

    void sceeFontPrintfAt(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t oldSp = getRegU32(ctx, 29);
        const uint32_t frame = oldSp - 0x900u;

        const uint32_t bufAddr = getRegU32(ctx, 4);
        const uint32_t paramX = getRegU32(ctx, 5);
        const uint32_t paramY = getRegU32(ctx, 6);
        const uint32_t fmtAddr = getRegU32(ctx, 7);

        const uint8_t *callerVa = getConstMemPtr(rdram, oldSp + 16u);
        uint8_t *frameVa = getMemPtr(rdram, frame + 0x8f8u);
        if (callerVa && frameVa)
            std::memcpy(frameVa, callerVa, 64u);

        SET_GPR_U32(ctx, 4, frame + 0x20u);
        SET_GPR_U32(ctx, 5, fmtAddr);
        SET_GPR_U32(ctx, 6, frame + 0x8f8u);
        vsprintf(rdram, ctx, runtime);

        const uint32_t gp = getRegU32(ctx, 28);
        uint32_t defaultSclxBits = FAST_READ32(gp + static_cast<uint32_t>(static_cast<int32_t>(-0x7b54)));
        uint32_t defaultSclyBits = FAST_READ32(gp + static_cast<uint32_t>(static_cast<int32_t>(-0x7b50)));
        uint32_t defaultColour = FAST_READ32(gp + static_cast<uint32_t>(static_cast<int32_t>(-0x7b4c)));
        uint32_t defaultFontId = FAST_READ32(gp + static_cast<uint32_t>(static_cast<int32_t>(-0x7b58)));
        uint32_t scrWidth = FAST_READ32(gp + static_cast<uint32_t>(static_cast<int32_t>(-0x7b60)));
        uint32_t scrHeight = FAST_READ32(gp + static_cast<uint32_t>(static_cast<int32_t>(-0x7b5c)));

        std::memcpy(&ctx->f[12], &defaultSclxBits, sizeof(float));
        std::memcpy(&ctx->f[13], &defaultSclyBits, sizeof(float));

        FAST_WRITE32(frame + 0x00u, frame + 0x20u);
        FAST_WRITE32(frame + 0x08u, frame + 0x820u);
        FAST_WRITE32(frame + 0x10u, frame + 0x824u);
        FAST_WRITE32(frame + 0x18u, 1u);

        SET_GPR_U32(ctx, 29, frame);
        SET_GPR_U32(ctx, 4, bufAddr);
        SET_GPR_U32(ctx, 5, paramX);
        SET_GPR_U32(ctx, 6, paramY);
        SET_GPR_U32(ctx, 7, scrWidth);
        SET_GPR_U32(ctx, 8, scrHeight);
        SET_GPR_U32(ctx, 9, defaultColour);
        SET_GPR_U32(ctx, 10, 0x4cu);
        SET_GPR_U32(ctx, 11, defaultFontId);

        sceeFontGenerateString(rdram, ctx, runtime);

        SET_GPR_U32(ctx, 29, oldSp);
        ctx->pc = getRegU32(ctx, 31);
    }

    void sceeFontPrintfAt2(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t oldSp = getRegU32(ctx, 29);
        const uint32_t frame = oldSp - 0x900u;

        const uint32_t bufAddr = getRegU32(ctx, 4);
        const uint32_t paramX = getRegU32(ctx, 5);
        const uint32_t paramY = getRegU32(ctx, 6);
        const uint32_t paramW = getRegU32(ctx, 7);
        const uint32_t paramH = getRegU32(ctx, 8);
        const uint32_t alignRaw = getRegU32(ctx, 9);
        const uint32_t fmtAddr = getRegU32(ctx, 10);
        const uint64_t param8 = GPR_U64(ctx, 11);

        int8_t alignChar = static_cast<int8_t>(alignRaw & 0xffu);

        FAST_WRITE64(frame + 0x8f8u, param8);

        SET_GPR_U32(ctx, 4, frame + 0x20u);
        SET_GPR_U32(ctx, 5, fmtAddr);
        SET_GPR_U32(ctx, 6, frame + 0x8f8u);
        vsprintf(rdram, ctx, runtime);

        const uint32_t gp = getRegU32(ctx, 28);
        uint32_t defaultSclxBits = FAST_READ32(gp + static_cast<uint32_t>(static_cast<int32_t>(-0x7b54)));
        uint32_t defaultSclyBits = FAST_READ32(gp + static_cast<uint32_t>(static_cast<int32_t>(-0x7b50)));
        uint32_t defaultColour = FAST_READ32(gp + static_cast<uint32_t>(static_cast<int32_t>(-0x7b4c)));
        uint32_t defaultFontId = FAST_READ32(gp + static_cast<uint32_t>(static_cast<int32_t>(-0x7b58)));

        std::memcpy(&ctx->f[12], &defaultSclxBits, sizeof(float));
        std::memcpy(&ctx->f[13], &defaultSclyBits, sizeof(float));

        FAST_WRITE32(frame + 0x00u, frame + 0x20u);
        FAST_WRITE32(frame + 0x08u, frame + 0x820u);
        FAST_WRITE32(frame + 0x10u, frame + 0x824u);
        FAST_WRITE32(frame + 0x18u, 1u);

        SET_GPR_U32(ctx, 29, frame);
        SET_GPR_U32(ctx, 4, bufAddr);
        SET_GPR_U32(ctx, 5, paramX);
        SET_GPR_U32(ctx, 6, paramY);
        SET_GPR_U32(ctx, 7, paramW);
        SET_GPR_U32(ctx, 8, paramH);
        SET_GPR_U32(ctx, 9, defaultColour);
        SET_GPR_U32(ctx, 10, static_cast<uint32_t>(static_cast<uint8_t>(alignChar)));
        SET_GPR_U32(ctx, 11, defaultFontId);

        sceeFontGenerateString(rdram, ctx, runtime);

        SET_GPR_U32(ctx, 29, oldSp);
        ctx->pc = getRegU32(ctx, 31);
    }

    void sceeFontClose(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static constexpr uint32_t kFontBase = 0x176148u;
        static constexpr uint32_t kFontEntrySz = 0x24u;
        const int fontId = static_cast<int>(getRegU32(ctx, 4));
        const uint32_t fontOff = static_cast<uint32_t>(fontId * static_cast<int>(kFontEntrySz));
        uint32_t glyphPtr = 0;
        if (const uint8_t *p = getConstMemPtr(rdram, kFontBase + fontOff))
            glyphPtr = *reinterpret_cast<const uint32_t *>(p);
        if (glyphPtr != 0u)
        {
            if (runtime)
            {
                uint32_t kernPtr = 0;
                if (const uint8_t *kp = getConstMemPtr(rdram, glyphPtr + 0x2000u))
                    kernPtr = *reinterpret_cast<const uint32_t *>(kp);
                if (kernPtr != 0u)
                    runtime->guestFree(kernPtr);
                runtime->guestFree(glyphPtr);
            }
            if (uint8_t *p = getMemPtr(rdram, kFontBase + fontOff))
                *reinterpret_cast<uint32_t *>(p) = 0u;
            setReturnS32(ctx, 0);
        }
        else
        {
            setReturnS32(ctx, -1);
        }
    }

    void sceeFontSetColour(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t gp = getRegU32(ctx, 28);
        writeU32AtGp(rdram, gp, -0x7b4c, getRegU32(ctx, 4));
    }

    void sceeFontSetMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t gp = getRegU32(ctx, 28);
        writeU32AtGp(rdram, gp, -0x7c98, getRegU32(ctx, 4));
        setReturnS32(ctx, 0);
    }

    void sceeFontSetFont(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t gp = getRegU32(ctx, 28);
        writeU32AtGp(rdram, gp, -0x7b58, getRegU32(ctx, 4));
    }

    void sceeFontSetScale(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t gp = getRegU32(ctx, 28);
        uint32_t sclx_bits, scly_bits;
        std::memcpy(&sclx_bits, &ctx->f[12], sizeof(float));
        std::memcpy(&scly_bits, &ctx->f[13], sizeof(float));
        writeU32AtGp(rdram, gp, -0x7b54, sclx_bits);
        writeU32AtGp(rdram, gp, -0x7b50, scly_bits);
    }
}
