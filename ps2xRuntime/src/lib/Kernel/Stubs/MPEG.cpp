#include "Common.h"
#include "MPEG.h"

namespace ps2_stubs
{
namespace
{
struct MpegRegisteredCallback
{
    uint32_t type = 0u;
    uint32_t func = 0u;
    uint32_t data = 0u;
    uint32_t handle = 0u;
};

struct MpegPlaybackState
{
    uint32_t picturesServed = 0u;
};

struct MpegStubState
{
    bool initialized = false;
    uint32_t nextCallbackHandle = 1u;
    std::unordered_map<uint32_t, std::vector<MpegRegisteredCallback>> callbacksByMpeg;
    std::unordered_map<uint32_t, MpegPlaybackState> playbackByMpeg;
    PS2MpegCompatLayout compat;
};

std::mutex g_mpeg_stub_mutex;
MpegStubState g_mpeg_stub_state;

constexpr uint32_t kStubMovieWidth = 320u;
constexpr uint32_t kStubMovieHeight = 240u;

uint32_t mpegCompatSyntheticFrames(const PS2MpegCompatLayout &layout)
{
    return layout.syntheticFramesBeforeEnd != 0u ? layout.syntheticFramesBeforeEnd : 1u;
}

MpegPlaybackState &getPlaybackState(uint32_t mpegAddr)
{
    return g_mpeg_stub_state.playbackByMpeg[mpegAddr];
}

void resetMpegStubStateUnlocked()
{
    const PS2MpegCompatLayout compat = g_mpeg_stub_state.compat;
    g_mpeg_stub_state.initialized = false;
    g_mpeg_stub_state.nextCallbackHandle = 1u;
    g_mpeg_stub_state.callbacksByMpeg.clear();
    g_mpeg_stub_state.playbackByMpeg.clear();
    g_mpeg_stub_state.compat = compat;
}
}

void setMpegCompatLayout(const PS2MpegCompatLayout &layout)
{
    std::lock_guard<std::mutex> lock(g_mpeg_stub_mutex);
    g_mpeg_stub_state.compat = layout;
}

void clearMpegCompatLayout()
{
    std::lock_guard<std::mutex> lock(g_mpeg_stub_mutex);
    g_mpeg_stub_state.compat = {};
}

void resetMpegStubState()
{
    std::lock_guard<std::mutex> lock(g_mpeg_stub_mutex);
    resetMpegStubStateUnlocked();
}

void sceMpegFlush(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceMpegFlush", rdram, ctx, runtime);
}


void sceMpegAddBs(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceMpegAddBs", rdram, ctx, runtime);
}

void sceMpegAddCallback(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    (void)rdram;
    (void)runtime;

    const uint32_t mpegAddr = getRegU32(ctx, 4);
    const uint32_t callbackType = getRegU32(ctx, 5);
    const uint32_t callbackFunc = getRegU32(ctx, 6);
    const uint32_t callbackData = getRegU32(ctx, 7);

    std::lock_guard<std::mutex> lock(g_mpeg_stub_mutex);
    g_mpeg_stub_state.initialized = true;
    (void)getPlaybackState(mpegAddr);

    const uint32_t handle = g_mpeg_stub_state.nextCallbackHandle++;
    g_mpeg_stub_state.callbacksByMpeg[mpegAddr].push_back(
        MpegRegisteredCallback{callbackType, callbackFunc, callbackData, handle});

    setReturnU32(ctx, handle);
}

void sceMpegAddStrCallback(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    (void)rdram;
    (void)runtime;
    setReturnU32(ctx, 0u);
}

void sceMpegClearRefBuff(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    (void)ctx;
    (void)runtime;
    static const uint32_t kRefGlobalAddrs[] = {
        0x171800u, 0x17180Cu, 0x171818u, 0x171804u, 0x171810u, 0x17181Cu
    };
    for (uint32_t addr : kRefGlobalAddrs)
    {
        uint8_t *p = getMemPtr(rdram, addr);
        if (!p)
            continue;
        uint32_t ptr = *reinterpret_cast<uint32_t *>(p);
        if (ptr != 0u)
        {
            uint8_t *q = getMemPtr(rdram, ptr + 0x28u);
            if (q)
                *reinterpret_cast<uint32_t *>(q) = 0u;
        }
    }
    setReturnU32(ctx, 1u);
}

static void mpegGuestWrite32(uint8_t *rdram, uint32_t addr, uint32_t value)
{
    if (uint8_t *p = getMemPtr(rdram, addr))
        *reinterpret_cast<uint32_t *>(p) = value;
}
static void mpegGuestWrite64(uint8_t *rdram, uint32_t addr, uint64_t value)
{
    if (uint8_t *p = getMemPtr(rdram, addr))
    {
        *reinterpret_cast<uint32_t *>(p) = static_cast<uint32_t>(value);
        *reinterpret_cast<uint32_t *>(p + 4) = static_cast<uint32_t>(value >> 32);
    }
}

void sceMpegCreate(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t param_1 = getRegU32(ctx, 4);   // a0
    const uint32_t param_2 = getRegU32(ctx, 5);   // a1
    const uint32_t param_3 = getRegU32(ctx, 6);   // a2

    const uint32_t uVar3 = (param_2 + 3u) & 0xFFFFFFFCu;
    const int32_t iVar2_signed = static_cast<int32_t>(param_3) - static_cast<int32_t>(uVar3 - param_2);

    if (iVar2_signed <= 0x117)
    {
        setReturnU32(ctx, 0u);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_mpeg_stub_mutex);
        getPlaybackState(param_1) = {};
    }

    const uint32_t puVar4 = uVar3 + 0x108u;
    const uint32_t innerSize = static_cast<uint32_t>(iVar2_signed) - 0x118u;

    mpegGuestWrite32(rdram, param_1 + 0x40, uVar3);

    const uint32_t a1_init = uVar3 + 0x118u;
    mpegGuestWrite32(rdram, puVar4 + 0x0, a1_init);
    mpegGuestWrite32(rdram, puVar4 + 0x4, innerSize);
    mpegGuestWrite32(rdram, puVar4 + 0x8, a1_init);
    mpegGuestWrite32(rdram, puVar4 + 0xC, a1_init);

    const uint32_t allocResult = runtime ? runtime->guestMalloc(0x600, 8u) : (uVar3 + 0x200u);
    mpegGuestWrite32(rdram, uVar3 + 0x44, allocResult);

    // param_1[0..2] = 0; param_1[4..0xe] = 0xffffffff/0 as per decompilation
    mpegGuestWrite32(rdram, param_1 + 0x00, 0);
    mpegGuestWrite32(rdram, param_1 + 0x04, 0);
    mpegGuestWrite32(rdram, param_1 + 0x08, 0);
    mpegGuestWrite64(rdram, param_1 + 0x10, 0xFFFFFFFFFFFFFFFFULL);
    mpegGuestWrite64(rdram, param_1 + 0x18, 0xFFFFFFFFFFFFFFFFULL);
    mpegGuestWrite64(rdram, param_1 + 0x20, 0);
    mpegGuestWrite64(rdram, param_1 + 0x28, 0xFFFFFFFFFFFFFFFFULL);
    mpegGuestWrite64(rdram, param_1 + 0x30, 0xFFFFFFFFFFFFFFFFULL);
    mpegGuestWrite64(rdram, param_1 + 0x38, 0);

    static const unsigned s_zeroOffsets[] = {
        0xB4, 0xB8, 0xBC, 0xC0, 0xC4, 0xC8, 0xCC, 0xD0, 0xD4, 0xD8, 0xDC, 0xE0, 0xE4, 0xE8, 0xF8,
        0x0C, 0x14, 0x2C, 0x34, 0x3C,
        0x48, 0xFC, 0x100, 0x104, 0x70, 0x90, 0xAC
    };
    for (unsigned off : s_zeroOffsets)
        mpegGuestWrite32(rdram, uVar3 + off, 0u);
    mpegGuestWrite64(rdram, uVar3 + 0x78, 0);
    mpegGuestWrite64(rdram, uVar3 + 0x88, 0);

    mpegGuestWrite64(rdram, uVar3 + 0xF0, 0xFFFFFFFFFFFFFFFFULL);
    mpegGuestWrite32(rdram, uVar3 + 0x1C, 0x1209F8u);
    mpegGuestWrite32(rdram, uVar3 + 0x24, 0x120A08u);
    mpegGuestWrite32(rdram, uVar3 + 0xB0, 1u);
    mpegGuestWrite32(rdram, uVar3 + 0x9C, 0xFFFFFFFFu);
    mpegGuestWrite32(rdram, uVar3 + 0x80, 0xFFFFFFFFu);
    mpegGuestWrite32(rdram, uVar3 + 0x94, 0xFFFFFFFFu);
    mpegGuestWrite32(rdram, uVar3 + 0x98, 0xFFFFFFFFu);

    mpegGuestWrite32(rdram, 0x1717BCu, param_1);

    static const uint32_t s_refValues[] = {
        0x171A50u, 0x171C58u, 0x171CC0u, 0x171D28u, 0x171D90u,
        0x171AB8u, 0x171B20u, 0x171B88u, 0x171BF0u
    };
    for (unsigned i = 0; i < 9u; ++i)
        mpegGuestWrite32(rdram, 0x171800u + i * 4u, s_refValues[i]);

    uint32_t setDynamicRet = a1_init;
    if (uint8_t *p = getMemPtr(rdram, puVar4 + 8))
        setDynamicRet = *reinterpret_cast<uint32_t *>(p);
    mpegGuestWrite32(rdram, puVar4 + 12, setDynamicRet);

    setReturnU32(ctx, setDynamicRet);
}

void sceMpegDelete(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    (void)rdram;
    (void)runtime;

    const uint32_t mpegAddr = getRegU32(ctx, 4);
    std::lock_guard<std::mutex> lock(g_mpeg_stub_mutex);
    g_mpeg_stub_state.callbacksByMpeg.erase(mpegAddr);
    g_mpeg_stub_state.playbackByMpeg.erase(mpegAddr);
    setReturnU32(ctx, 0u);
}

void sceMpegDemuxPss(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceMpegDemuxPss", rdram, ctx, runtime);
}

void sceMpegDemuxPssRing(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    (void)rdram;
    (void)runtime;

    const uint32_t availableBytes = getRegU32(ctx, 6);
    setReturnS32(ctx, static_cast<int32_t>(availableBytes));
}

void sceMpegDispCenterOffX(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceMpegDispCenterOffX", rdram, ctx, runtime);
}

void sceMpegDispCenterOffY(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceMpegDispCenterOffY", rdram, ctx, runtime);
}

void sceMpegDispHeight(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceMpegDispHeight", rdram, ctx, runtime);
}

void sceMpegDispWidth(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceMpegDispWidth", rdram, ctx, runtime);
}

void sceMpegGetDecodeMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceMpegGetDecodeMode", rdram, ctx, runtime);
}

void sceMpegGetPicture(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    (void)runtime;
    const uint32_t mpegAddr = getRegU32(ctx, 4);
    uint32_t picturesServed = 0u;
    PS2MpegCompatLayout compat{};
    {
        std::lock_guard<std::mutex> lock(g_mpeg_stub_mutex);
        MpegPlaybackState &playback = getPlaybackState(mpegAddr);
        mpegGuestWrite32(rdram, mpegAddr + 0x00u, kStubMovieWidth);
        mpegGuestWrite32(rdram, mpegAddr + 0x04u, kStubMovieHeight);
        mpegGuestWrite32(rdram, mpegAddr + 0x08u, playback.picturesServed);
        picturesServed = playback.picturesServed;
        compat = g_mpeg_stub_state.compat;
        playback.picturesServed += 1u;
    }

    if (uint8_t *base = getMemPtr(rdram, mpegAddr))
    {
        const uint32_t iVar1 = *reinterpret_cast<uint32_t *>(base + 0x40);
        if (uint8_t *inner = getMemPtr(rdram, iVar1))
        {
            *reinterpret_cast<uint32_t *>(inner + 0xb0) = 1;
            *reinterpret_cast<uint32_t *>(inner + 0xd8) = (getRegU32(ctx, 5) & 0x0FFFFFFFu) | 0x20000000u;
            *reinterpret_cast<uint32_t *>(inner + 0xe4) = getRegU32(ctx, 6);
            *reinterpret_cast<uint32_t *>(inner + 0xdc) = 0;
            *reinterpret_cast<uint32_t *>(inner + 0xe0) = 0;
        }
    }

    if (compat.matchesMpegObject(mpegAddr) &&
        compat.hasFinishTargets() &&
        (picturesServed + 1u) >= mpegCompatSyntheticFrames(compat))
    {
        // No decoder yet: synthesize a safe frame so the guest can
        // initialize its movie presentation path, then mark playback finished.
        if (compat.videoStateAddr != 0u)
        {
            mpegGuestWrite32(rdram, compat.videoStateAddr, compat.finishedVideoStateValue);
        }
        if (compat.movieStateAddr != 0u)
        {
            mpegGuestWrite32(rdram, compat.movieStateAddr, compat.finishedMovieStateValue);
        }
    }

    setReturnU32(ctx, 0u);
}

void sceMpegGetPictureRAW8(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceMpegGetPictureRAW8", rdram, ctx, runtime);
}

void sceMpegGetPictureRAW8xy(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceMpegGetPictureRAW8xy", rdram, ctx, runtime);
}

void sceMpegInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    (void)rdram;
    (void)runtime;

    std::lock_guard<std::mutex> lock(g_mpeg_stub_mutex);
    resetMpegStubStateUnlocked();
    g_mpeg_stub_state.initialized = true;
    setReturnU32(ctx, 0u);
}

void sceMpegIsEnd(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    (void)rdram;
    (void)runtime;
    const uint32_t mpegAddr = getRegU32(ctx, 4);

    std::lock_guard<std::mutex> lock(g_mpeg_stub_mutex);
    g_mpeg_stub_state.initialized = true;
    const MpegPlaybackState &playback = getPlaybackState(mpegAddr);
    if (g_mpeg_stub_state.compat.matchesMpegObject(mpegAddr))
    {
        setReturnS32(ctx, playback.picturesServed >= mpegCompatSyntheticFrames(g_mpeg_stub_state.compat) ? 1 : 0);
        return;
    }

    // Generic fallback: keep decode threads alive until a game-specific path
    // decides to stop playback.
    setReturnS32(ctx, 0);
}

void sceMpegIsRefBuffEmpty(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceMpegIsRefBuffEmpty", rdram, ctx, runtime);
}

void sceMpegReset(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    (void)runtime;
    const uint32_t param_1 = getRegU32(ctx, 4);
    {
        std::lock_guard<std::mutex> lock(g_mpeg_stub_mutex);
        g_mpeg_stub_state.playbackByMpeg[param_1] = {};
    }
    uint8_t *base = getMemPtr(rdram, param_1);
    if (!base)
    {
        return;
    }
    uint32_t inner = *reinterpret_cast<uint32_t *>(base + 0x40);
    if (inner == 0u)
        return;
    mpegGuestWrite32(rdram, param_1 + 0x00u, 0u);
    mpegGuestWrite32(rdram, param_1 + 0x04u, 0u);
    mpegGuestWrite32(rdram, param_1 + 0x08u, 0u);
    mpegGuestWrite32(rdram, inner + 0x00, 0u);
    mpegGuestWrite32(rdram, inner + 0x04, 0u);
    mpegGuestWrite32(rdram, inner + 0x08, 0u);
    mpegGuestWrite32(rdram, param_1 + 0x08, 0u);
    mpegGuestWrite32(rdram, inner + 0x80, 0xFFFFFFFFu);
    mpegGuestWrite32(rdram, inner + 0xAC, 0u);
    mpegGuestWrite32(rdram, 0x171904u, 0u);
}

void sceMpegResetDefaultPtsGap(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceMpegResetDefaultPtsGap", rdram, ctx, runtime);
}

void sceMpegSetDecodeMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceMpegSetDecodeMode", rdram, ctx, runtime);
}

void sceMpegSetDefaultPtsGap(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceMpegSetDefaultPtsGap", rdram, ctx, runtime);
}

void sceMpegSetImageBuff(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceMpegSetImageBuff", rdram, ctx, runtime);
}
}
