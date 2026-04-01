#pragma once

#include <cstdint>
#include "ps2_call_list.h"
#include "runtime/ps2_memory.h"

struct R5900Context;
class  PS2Runtime;

struct PS2MpegCompatLayout
{
    uint32_t mpegObjectAddr = 0;
    uint32_t videoStateAddr = 0;
    uint32_t movieStateAddr = 0;
    uint32_t syntheticFramesBeforeEnd = 1u;
    uint32_t playingVideoStateValue = 0u;
    uint32_t playingMovieStateValue = 2u;
    uint32_t finishedVideoStateValue = 3u;
    uint32_t finishedMovieStateValue = 3u;

    [[nodiscard]] bool matchesMpegObject(uint32_t addr) const
    {
        return mpegObjectAddr != 0u && ((addr & PS2_RAM_MASK) == (mpegObjectAddr & PS2_RAM_MASK));
    }

    [[nodiscard]] bool hasFinishTargets() const
    {
        return videoStateAddr != 0u || movieStateAddr != 0u;
    }
};


namespace ps2_stubs
{
#define PS2_DECLARE_STUB(name) void name(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    PS2_STUB_LIST(PS2_DECLARE_STUB)
#undef PS2_DECLARE_STUB

    void resetSifState();

    void setMpegCompatLayout(const PS2MpegCompatLayout &layout);
    void clearMpegCompatLayout();
}
