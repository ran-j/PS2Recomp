#pragma once

#include "ps2_stubs.h"

namespace ps2_stubs
{
    // Upper bound on how many host-fed bytes are held in the pre-decoder stage
    // (see feedMpegCdStreamBytes below) before the stage is abandoned for the
    // rest of the current CD-stream generation. Internal tuning knob, not part
    // of the API contract.
    constexpr size_t kMpegHostFeedStageCapBytes = 4u * 1024u * 1024u;

    void resetMpegStubState();
    void notifyMpegCdStreamStart();
    void notifyMpegCdStreamEof();
    // Push `size` bytes of the active CD movie stream's program-stream/PES data into
    // the guest-driven MPEG decoder from host memory. Must be bracketed by
    // notifyMpegCdStreamStart()/notifyMpegCdStreamEof(). Order-insensitive w.r.t.
    // sceMpegCreate: bytes fed before any decoder exists on the current CD-stream
    // generation are staged (bounded; see kMpegHostFeedStageCapBytes) and replayed
    // into the next decoder created on that generation. Staged bytes are discarded on
    // stream stop/restart. Returns `size` while a CD stream is active (routed or
    // staged), 0 only when no CD stream is active. Thread-safe; stream callbacks are
    // not dispatched on this path.
    size_t feedMpegCdStreamBytes(const uint8_t *data, size_t size);
    void sceMpegFlush(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMpegAddBs(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMpegAddCallback(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMpegAddStrCallback(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMpegClearRefBuff(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMpegCreate(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMpegDelete(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMpegDemuxPss(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMpegDemuxPssRing(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMpegDispCenterOffX(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMpegDispCenterOffY(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMpegDispHeight(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMpegDispWidth(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMpegGetDecodeMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMpegGetPicture(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMpegGetPictureRAW8(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMpegGetPictureRAW8xy(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMpegInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMpegIsEnd(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMpegIsRefBuffEmpty(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMpegReset(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMpegResetDefaultPtsGap(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMpegSetDecodeMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMpegSetDefaultPtsGap(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMpegSetImageBuff(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
}
