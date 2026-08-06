#pragma once

#include "ps2_stubs.h"

// Internal helpers for the sceMpegGetPicture frame-rate-pacing path, split out of MPEG.cpp so the
// pacing regression tests can link them directly. Not part of the guest syscall surface
// (MPEG.h); never registered as guest entry points.
namespace ps2_stubs
{
    // due(N): the vsync tick at which movie frame N is due, computed fresh from the frame
    // index every call (never accumulated) so integer rounding cannot drift over a long movie:
    //   base + floor(frameIndex * vsyncHz * fpsDen / fpsNum)
    // Returns base unchanged when the rate is unknown/degenerate (fpsNum/fpsDen <= 0).
    uint64_t mpegFrameDueTick(uint64_t baseTick, uint64_t frameIndex, uint64_t vsyncHz,
                              int fpsNum, int fpsDen);

    // True when num/den is a plausible movie frame rate (10..120 fps). Rejects a not-yet-
    // populated or garbage decoder rate before it is latched for pacing.
    bool mpegFrameRateIsSane(int num, int den);

    // Park the calling thread until the movie's frame-rate cadence reaches dueTick, using the existing
    // vsync-tick primitive. Holds no lock; caps a single park at a few vblanks; breaks out
    // promptly on runtime stop.
    void mpegParkUntilDueTick(uint8_t *rdram, PS2Runtime *runtime, uint64_t dueTick);

    // Test seam: report the frame rate latched for a handle's pacing. The latch captures
    // AVCodecContext::framerate on the first valid, in-range read (30000/1001 fallback until
    // then). Returns true once a real stream rate has been latched for mpegAddr, with num/den
    // set to it; false (num/den left untouched) if the handle is unknown or still on the
    // fallback. Not a guest entry point; lets the pacing suite pin the latch directly
    // (jitter-free), independent of the end-to-end delivery span. Takes g_mpeg_stub_mutex.
    bool mpegLatchedFrameRate(uint32_t mpegAddr, int &num, int &den);
}
