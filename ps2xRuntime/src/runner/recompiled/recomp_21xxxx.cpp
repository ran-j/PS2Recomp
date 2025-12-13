// Auto-generated split file - DO NOT EDIT DIRECTLY
// Edit the original ps2_recompiled_functions.cpp and re-run split_recompiled.py

#include "ps2_recompiled_functions.h"
#include "ps2_runtime_macros.h"
#include "ps2_runtime.h"
#include "ps2_recompiled_stubs.h"
#include "ps2_stubs.h"

void entry_210018(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // 0x210018: 0x200202d
    SET_GPR_U64(ctx, 4, GPR_U64(ctx, 16) + GPR_U64(ctx, 0));
    // 0x21001c: 0xc083d6e
    SET_GPR_U32(ctx, 31, 0x210024);
    SET_GPR_S32(ctx, 5, ADD32(GPR_U32(ctx, 0), 8));
    _sceMpegNextBit(rdram, ctx, runtime); return;
}


// Function: entry_210024
// Address: 0x210024 - 0x210030

void entry_210024(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // 0x210024: 0x200202d
    SET_GPR_U64(ctx, 4, GPR_U64(ctx, 16) + GPR_U64(ctx, 0));
    // 0x210028: 0xc083d6e
    SET_GPR_U32(ctx, 31, 0x210030);
    SET_GPR_S32(ctx, 5, ADD32(GPR_U32(ctx, 0), 1));
    _sceMpegNextBit(rdram, ctx, runtime); return;
}


// Function: entry_210030
// Address: 0x210030 - 0x21003c

void entry_210030(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // 0x210030: 0x200202d
    SET_GPR_U64(ctx, 4, GPR_U64(ctx, 16) + GPR_U64(ctx, 0));
    // 0x210034: 0xc083d6e
    SET_GPR_U32(ctx, 31, 0x21003c);
    SET_GPR_S32(ctx, 5, ADD32(GPR_U32(ctx, 0), 7));
    _sceMpegNextBit(rdram, ctx, runtime); return;
}


// Function: entry_21003c
// Address: 0x21003c - 0x210048

void entry_21003c(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // 0x21003c: 0x200202d
    SET_GPR_U64(ctx, 4, GPR_U64(ctx, 16) + GPR_U64(ctx, 0));
    // 0x210040: 0xc083d6e
    SET_GPR_U32(ctx, 31, 0x210048);
    SET_GPR_S32(ctx, 5, ADD32(GPR_U32(ctx, 0), 1));
    _sceMpegNextBit(rdram, ctx, runtime); return;
}


// Function: entry_210048
// Address: 0x210048 - 0x210054

void entry_210048(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // 0x210048: 0x200202d
    SET_GPR_U64(ctx, 4, GPR_U64(ctx, 16) + GPR_U64(ctx, 0));
    // 0x21004c: 0xc083d6e
    SET_GPR_U32(ctx, 31, 0x210054);
    SET_GPR_S32(ctx, 5, ADD32(GPR_U32(ctx, 0), 20));
    _sceMpegNextBit(rdram, ctx, runtime); return;
}


// Function: entry_210054
// Address: 0x210054 - 0x210060

void entry_210054(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // 0x210054: 0x200202d
    SET_GPR_U64(ctx, 4, GPR_U64(ctx, 16) + GPR_U64(ctx, 0));
    // 0x210058: 0xc083d6e
    SET_GPR_U32(ctx, 31, 0x210060);
    SET_GPR_S32(ctx, 5, ADD32(GPR_U32(ctx, 0), 1));
    _sceMpegNextBit(rdram, ctx, runtime); return;
}


// Function: entry_210060
// Address: 0x210060 - 0x21006c

void entry_210060(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // 0x210060: 0x200202d
    SET_GPR_U64(ctx, 4, GPR_U64(ctx, 16) + GPR_U64(ctx, 0));
    // 0x210064: 0xc083d6e
    SET_GPR_U32(ctx, 31, 0x21006c);
    SET_GPR_S32(ctx, 5, ADD32(GPR_U32(ctx, 0), 22));
    _sceMpegNextBit(rdram, ctx, runtime); return;
}


// Function: entry_21006c
// Address: 0x21006c - 0x210078

void entry_21006c(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // 0x21006c: 0x200202d
    SET_GPR_U64(ctx, 4, GPR_U64(ctx, 16) + GPR_U64(ctx, 0));
    // 0x210070: 0xc083d6e
    SET_GPR_U32(ctx, 31, 0x210078);
    SET_GPR_S32(ctx, 5, ADD32(GPR_U32(ctx, 0), 1));
    _sceMpegNextBit(rdram, ctx, runtime); return;
}


// Function: entry_210078
// Address: 0x210078 - 0x210090

void entry_210078(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // 0x210078: 0x200202d
    SET_GPR_U64(ctx, 4, GPR_U64(ctx, 16) + GPR_U64(ctx, 0));
    // 0x21007c: 0xdfbf0010
    SET_GPR_U64(ctx, 31, READ64(ADD32(GPR_U32(ctx, 29), 16)));
    // 0x210080: 0xdfb00000
    SET_GPR_U64(ctx, 16, READ64(ADD32(GPR_U32(ctx, 29), 0)));
    // 0x210084: 0x24050016
    SET_GPR_S32(ctx, 5, ADD32(GPR_U32(ctx, 0), 22));
    // 0x210088: 0x8083d6e
    SET_GPR_S32(ctx, 29, ADD32(GPR_U32(ctx, 29), 32));
    _sceMpegNextBit(rdram, ctx, runtime); return;
}


// Function: _decPicture
// Address: 0x210090 - 0x2100cc

void entry_2100cc(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // Dispatch for mid-function entry points
    if (ctx->pc != 0 && ctx->pc != 0x2100cc) {
        switch (ctx->pc) {
            case 0x2100d8: ctx->pc = 0; goto label_2100d8;
            case 0x2100f8: ctx->pc = 0; goto label_2100f8;
            case 0x21010c: ctx->pc = 0; goto label_21010c;
            case 0x210114: ctx->pc = 0; goto label_210114;
            case 0x21011c: ctx->pc = 0; goto label_21011c;
            case 0x210120: ctx->pc = 0; goto label_210120;
            default: break;
        }
        ctx->pc = 0; // Reset PC and continue from start
    }

    // 0x2100cc: 0xae000120
    WRITE32(ADD32(GPR_U32(ctx, 16), 288), GPR_U32(ctx, 0));
    // 0x2100d0: 0x8e030174
    SET_GPR_U32(ctx, 3, READ32(ADD32(GPR_U32(ctx, 16), 372)));
    // 0x2100d4: 0x24020002
    SET_GPR_S32(ctx, 2, ADD32(GPR_U32(ctx, 0), 2));
label_2100d8:
    // 0x2100d8: 0x1062000e
    SET_GPR_U32(ctx, 2, SLT32(GPR_S32(ctx, 3), 3));
    if (GPR_U32(ctx, 3) == GPR_U32(ctx, 2)) {
        goto label_210114;
    }
    // 0x2100e0: 0x10400005
    SET_GPR_S32(ctx, 2, ADD32(GPR_U32(ctx, 0), 1));
    if (GPR_U32(ctx, 2) == GPR_U32(ctx, 0)) {
        goto label_2100f8;
    }
    // 0x2100e8: 0x10620008
    SET_GPR_U32(ctx, 5, ((uint32_t)37 << 16));
    if (GPR_U32(ctx, 3) == GPR_U32(ctx, 2)) {
        goto label_21010c;
    }
    // 0x2100f0: 0x1000000b
    SET_GPR_U32(ctx, 17, READ32(ADD32(GPR_U32(ctx, 16), 448)));
    if (GPR_U32(ctx, 0) == GPR_U32(ctx, 0)) {
        goto label_210120;
    }
label_2100f8:
    // 0x2100f8: 0x24020003
    SET_GPR_S32(ctx, 2, ADD32(GPR_U32(ctx, 0), 3));
    // 0x2100fc: 0x14620007
    SET_GPR_U32(ctx, 5, ((uint32_t)37 << 16));
    if (GPR_U32(ctx, 3) != GPR_U32(ctx, 2)) {
        goto label_21011c;
    }
    // 0x210104: 0x10000009
    SET_GPR_U32(ctx, 17, READ32(ADD32(GPR_U32(ctx, 16), 448)));
    if (GPR_U32(ctx, 0) == GPR_U32(ctx, 0)) {
        entry_21012c(rdram, ctx, runtime); return;
    }
label_21010c:
    // 0x21010c: 0x10000007
    SET_GPR_U32(ctx, 17, READ32(ADD32(GPR_U32(ctx, 16), 464)));
    if (GPR_U32(ctx, 0) == GPR_U32(ctx, 0)) {
        entry_21012c(rdram, ctx, runtime); return;
    }
label_210114:
    // 0x210114: 0x10000005
    SET_GPR_U32(ctx, 17, READ32(ADD32(GPR_U32(ctx, 16), 480)));
    if (GPR_U32(ctx, 0) == GPR_U32(ctx, 0)) {
        entry_21012c(rdram, ctx, runtime); return;
    }
label_21011c:
    // 0x21011c: 0x8e1101c0
    SET_GPR_U32(ctx, 17, READ32(ADD32(GPR_U32(ctx, 16), 448)));
label_210120:
    // 0x210120: 0x24a504d8
    SET_GPR_S32(ctx, 5, ADD32(GPR_U32(ctx, 5), 1240));
    // 0x210124: 0xc082e5e
    SET_GPR_U32(ctx, 31, 0x21012c);
    SET_GPR_U64(ctx, 4, GPR_U64(ctx, 16) + GPR_U64(ctx, 0));
    fn__Error(rdram, ctx, runtime); return;
}


// Function: entry_21012c
// Address: 0x21012c - 0x210134

void entry_21012c(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // 0x21012c: 0xc0838ee
    SET_GPR_U32(ctx, 31, 0x210134);
    SET_GPR_U64(ctx, 4, GPR_U64(ctx, 16) + GPR_U64(ctx, 0));
    _pictureData0(rdram, ctx, runtime); return;
}


// Function: entry_210134
// Address: 0x210134 - 0x210160

void entry_210134(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // Dispatch for mid-function entry points
    if (ctx->pc != 0 && ctx->pc != 0x210134) {
        switch (ctx->pc) {
            case 0x210144: ctx->pc = 0; goto label_210144;
            default: break;
        }
        ctx->pc = 0; // Reset PC and continue from start
    }

    // 0x210134: 0x40182d
    SET_GPR_U64(ctx, 3, GPR_U64(ctx, 2) + GPR_U64(ctx, 0));
    // 0x210138: 0x10600002
    SET_GPR_S32(ctx, 2, ADD32(GPR_U32(ctx, 0), 1));
    if (GPR_U32(ctx, 3) == GPR_U32(ctx, 0)) {
        goto label_210144;
    }
    // 0x210140: 0xae220028
    WRITE32(ADD32(GPR_U32(ctx, 17), 40), GPR_U32(ctx, 2));
label_210144:
    // 0x210144: 0xdfbf0020
    SET_GPR_U64(ctx, 31, READ64(ADD32(GPR_U32(ctx, 29), 32)));
    // 0x210148: 0x60102d
    SET_GPR_U64(ctx, 2, GPR_U64(ctx, 3) + GPR_U64(ctx, 0));
    // 0x21014c: 0xdfb10010
    SET_GPR_U64(ctx, 17, READ64(ADD32(GPR_U32(ctx, 29), 16)));
    // 0x210150: 0xdfb00000
    SET_GPR_U64(ctx, 16, READ64(ADD32(GPR_U32(ctx, 29), 0)));
    // 0x210154: 0x3e00008
    SET_GPR_S32(ctx, 29, ADD32(GPR_U32(ctx, 29), 48));
    ctx->pc = GPR_U32(ctx, 31); return;
    // 0x21015c: 0x0
    // NOP
    // Fall-through to next function
    ctx->pc = 0x210160; return;
}


// Function: _outputFrame
// Address: 0x210160 - 0x2101a0

void entry_2101a0(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // Dispatch for mid-function entry points
    if (ctx->pc != 0 && ctx->pc != 0x2101a0) {
        switch (ctx->pc) {
            case 0x2101a8: ctx->pc = 0; goto label_2101a8;
            case 0x2101bc: ctx->pc = 0; goto label_2101bc;
            case 0x2101c0: ctx->pc = 0; goto label_2101c0;
            default: break;
        }
        ctx->pc = 0; // Reset PC and continue from start
    }

    // 0x2101a0: 0x1000000b
    SET_GPR_U32(ctx, 3, READ32(ADD32(GPR_U32(ctx, 16), 248)));
    if (GPR_U32(ctx, 0) == GPR_U32(ctx, 0)) {
        ctx->pc = 0x2101D0; return;
    }
label_2101a8:
    // 0x2101a8: 0x54430004
    if (GPR_U32(ctx, 2) != GPR_U32(ctx, 3)) {
        SET_GPR_U32(ctx, 5, READ32(ADD32(GPR_U32(ctx, 16), 456)));
        goto label_2101bc;
    }
    // 0x2101b0: 0x8e0501d4
    SET_GPR_U32(ctx, 5, READ32(ADD32(GPR_U32(ctx, 16), 468)));
    // 0x2101b4: 0x10000002
    SET_GPR_U32(ctx, 6, READ32(ADD32(GPR_U32(ctx, 16), 484)));
    if (GPR_U32(ctx, 0) == GPR_U32(ctx, 0)) {
        goto label_2101c0;
    }
label_2101bc:
    // 0x2101bc: 0x8e0601d8
    SET_GPR_U32(ctx, 6, READ32(ADD32(GPR_U32(ctx, 16), 472)));
label_2101c0:
    // 0x2101c0: 0x24e7ffff
    SET_GPR_S32(ctx, 7, ADD32(GPR_U32(ctx, 7), 4294967295));
    // 0x2101c4: 0xc0842a0
    SET_GPR_U32(ctx, 31, 0x2101cc);
    SET_GPR_U64(ctx, 4, GPR_U64(ctx, 16) + GPR_U64(ctx, 0));
    _dispRefImageField(rdram, ctx, runtime); return;
}


// Function: entry_2101cc
// Address: 0x2101cc - 0x2101f0

void entry_2101cc(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // Dispatch for mid-function entry points
    if (ctx->pc != 0 && ctx->pc != 0x2101cc) {
        switch (ctx->pc) {
            case 0x2101d0: ctx->pc = 0; goto label_2101d0;
            case 0x2101e4: ctx->pc = 0; goto label_2101e4;
            default: break;
        }
        ctx->pc = 0; // Reset PC and continue from start
    }

    // 0x2101cc: 0x8e0300f8
    SET_GPR_U32(ctx, 3, READ32(ADD32(GPR_U32(ctx, 16), 248)));
label_2101d0:
    // 0x2101d0: 0x24020001
    SET_GPR_S32(ctx, 2, ADD32(GPR_U32(ctx, 0), 1));
    // 0x2101d4: 0x14620003
    SET_GPR_U64(ctx, 31, READ64(ADD32(GPR_U32(ctx, 29), 16)));
    if (GPR_U32(ctx, 3) != GPR_U32(ctx, 2)) {
        goto label_2101e4;
    }
    // 0x2101dc: 0x24020002
    SET_GPR_S32(ctx, 2, ADD32(GPR_U32(ctx, 0), 2));
    // 0x2101e0: 0xae0200f8
    WRITE32(ADD32(GPR_U32(ctx, 16), 248), GPR_U32(ctx, 2));
label_2101e4:
    // 0x2101e4: 0xdfb00000
    SET_GPR_U64(ctx, 16, READ64(ADD32(GPR_U32(ctx, 29), 0)));
    // 0x2101e8: 0x3e00008
    SET_GPR_S32(ctx, 29, ADD32(GPR_U32(ctx, 29), 32));
    ctx->pc = GPR_U32(ctx, 31); return;
}


// Function: _updateRefImage
// Address: 0x2101f0 - 0x2104a8

void entry_210524(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // 0x210524: 0x200202d
    SET_GPR_U64(ctx, 4, GPR_U64(ctx, 16) + GPR_U64(ctx, 0));
    // 0x210528: 0xc082e5e
    SET_GPR_U32(ctx, 31, 0x210530);
    SET_GPR_U64(ctx, 5, GPR_U64(ctx, 29) + GPR_U64(ctx, 0));
    fn__Error(rdram, ctx, runtime); return;
}


// Function: entry_210530
// Address: 0x210530 - 0x210548

void entry_210530(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // Dispatch for mid-function entry points
    if (ctx->pc != 0 && ctx->pc != 0x210530) {
        switch (ctx->pc) {
            case 0x210534: ctx->pc = 0; goto label_210534;
            default: break;
        }
        ctx->pc = 0; // Reset PC and continue from start
    }

    // 0x210530: 0x220102d
    SET_GPR_U64(ctx, 2, GPR_U64(ctx, 17) + GPR_U64(ctx, 0));
label_210534:
    // 0x210534: 0xdfbf0120
    SET_GPR_U64(ctx, 31, READ64(ADD32(GPR_U32(ctx, 29), 288)));
    // 0x210538: 0xdfb10110
    SET_GPR_U64(ctx, 17, READ64(ADD32(GPR_U32(ctx, 29), 272)));
    // 0x21053c: 0xdfb00100
    SET_GPR_U64(ctx, 16, READ64(ADD32(GPR_U32(ctx, 29), 256)));
    // 0x210540: 0x3e00008
    SET_GPR_S32(ctx, 29, ADD32(GPR_U32(ctx, 29), 304));
    ctx->pc = GPR_U32(ctx, 31); return;
}


// Function: _cpr8
// Address: 0x210548 - 0x210648

void entry_210648(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // 0x210648: 0x3c021000
    SET_GPR_U32(ctx, 2, ((uint32_t)4096 << 16));
    // 0x21064c: 0x3c041000
    SET_GPR_U32(ctx, 4, ((uint32_t)4096 << 16));
    // 0x210650: 0x3442d480
    SET_GPR_U32(ctx, 2, OR32(GPR_U32(ctx, 2), 54400));
    // 0x210654: 0x3484d410
    SET_GPR_U32(ctx, 4, OR32(GPR_U32(ctx, 4), 54288));
    // 0x210658: 0xac400000
    WRITE32(ADD32(GPR_U32(ctx, 2), 0), GPR_U32(ctx, 0));
    // 0x21065c: 0x3c031000
    SET_GPR_U32(ctx, 3, ((uint32_t)4096 << 16));
    // 0x210660: 0xac920000
    WRITE32(ADD32(GPR_U32(ctx, 4), 0), GPR_U32(ctx, 18));
    // 0x210664: 0x3463d420
    SET_GPR_U32(ctx, 3, OR32(GPR_U32(ctx, 3), 54304));
    // 0x210668: 0x3c041000
    SET_GPR_U32(ctx, 4, ((uint32_t)4096 << 16));
    // 0x21066c: 0xac740000
    WRITE32(ADD32(GPR_U32(ctx, 3), 0), GPR_U32(ctx, 20));
    // 0x210670: 0x3484d400
    SET_GPR_U32(ctx, 4, OR32(GPR_U32(ctx, 4), 54272));
    // 0x210674: 0x24020101
    SET_GPR_S32(ctx, 2, ADD32(GPR_U32(ctx, 0), 257));
    // 0x210678: 0xac820000
    WRITE32(ADD32(GPR_U32(ctx, 4), 0), GPR_U32(ctx, 2));
    // 0x21067c: 0xc07e76a
    SET_GPR_U32(ctx, 31, 0x210684);
    SET_GPR_S32(ctx, 16, ADD32(GPR_U32(ctx, 16), 1));
    EIntr(rdram, ctx, runtime); return;
}


// Function: entry_210684
// Address: 0x210684 - 0x2106b4

void entry_210684(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // Dispatch for mid-function entry points
    if (ctx->pc != 0 && ctx->pc != 0x210684) {
        switch (ctx->pc) {
            case 0x210690: ctx->pc = 0; goto label_210690;
            default: break;
        }
        ctx->pc = 0; // Reset PC and continue from start
    }

    // 0x210684: 0x3c031000
    SET_GPR_U32(ctx, 3, ((uint32_t)4096 << 16));
    // 0x210688: 0x23e9021
    SET_GPR_U32(ctx, 18, ADD32(GPR_U32(ctx, 17), GPR_U32(ctx, 30)));
    // 0x21068c: 0x3463d400
    SET_GPR_U32(ctx, 3, OR32(GPR_U32(ctx, 3), 54272));
label_210690:
    // 0x210690: 0x8c620000
    SET_GPR_U32(ctx, 2, READ32(ADD32(GPR_U32(ctx, 3), 0)));
    // 0x210694: 0x30420100
    SET_GPR_U32(ctx, 2, AND32(GPR_U32(ctx, 2), 256));
    // 0x210698: 0x0
    // NOP
    // 0x21069c: 0x0
    // NOP
    // 0x2106a0: 0x0
    // NOP
    // 0x2106a4: 0x1440fffa
    if (GPR_U32(ctx, 2) != GPR_U32(ctx, 0)) {
        goto label_210690;
    }
    // 0x2106ac: 0xc07e758
    SET_GPR_U32(ctx, 31, 0x2106b4);
    DIntr(rdram, ctx, runtime); return;
}


// Function: entry_2106b4
// Address: 0x2106b4 - 0x2106ec

void entry_2106b4(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // 0x2106b4: 0x3c021000
    SET_GPR_U32(ctx, 2, ((uint32_t)4096 << 16));
    // 0x2106b8: 0x3c041000
    SET_GPR_U32(ctx, 4, ((uint32_t)4096 << 16));
    // 0x2106bc: 0x3442d080
    SET_GPR_U32(ctx, 2, OR32(GPR_U32(ctx, 2), 53376));
    // 0x2106c0: 0x3484d010
    SET_GPR_U32(ctx, 4, OR32(GPR_U32(ctx, 4), 53264));
    // 0x2106c4: 0xac400000
    WRITE32(ADD32(GPR_U32(ctx, 2), 0), GPR_U32(ctx, 0));
    // 0x2106c8: 0x3c031000
    SET_GPR_U32(ctx, 3, ((uint32_t)4096 << 16));
    // 0x2106cc: 0xac910000
    WRITE32(ADD32(GPR_U32(ctx, 4), 0), GPR_U32(ctx, 17));
    // 0x2106d0: 0x3463d020
    SET_GPR_U32(ctx, 3, OR32(GPR_U32(ctx, 3), 53280));
    // 0x2106d4: 0x3c041000
    SET_GPR_U32(ctx, 4, ((uint32_t)4096 << 16));
    // 0x2106d8: 0xac740000
    WRITE32(ADD32(GPR_U32(ctx, 3), 0), GPR_U32(ctx, 20));
    // 0x2106dc: 0x3484d000
    SET_GPR_U32(ctx, 4, OR32(GPR_U32(ctx, 4), 53248));
    // 0x2106e0: 0x24020100
    SET_GPR_S32(ctx, 2, ADD32(GPR_U32(ctx, 0), 256));
    // 0x2106e4: 0xc07e76a
    SET_GPR_U32(ctx, 31, 0x2106ec);
    WRITE32(ADD32(GPR_U32(ctx, 4), 0), GPR_U32(ctx, 2));
    EIntr(rdram, ctx, runtime); return;
}


// Function: entry_2106ec
// Address: 0x2106ec - 0x2107e0

void entry_2106ec(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // Dispatch for mid-function entry points
    if (ctx->pc != 0 && ctx->pc != 0x2106ec) {
        switch (ctx->pc) {
            case 0x2106f8: ctx->pc = 0; goto label_2106f8;
            case 0x210720: ctx->pc = 0; goto label_210720;
            case 0x210754: ctx->pc = 0; goto label_210754;
            case 0x21075c: ctx->pc = 0; goto label_21075c;
            case 0x210788: ctx->pc = 0; goto label_210788;
            case 0x2107b8: ctx->pc = 0; goto label_2107b8;
            case 0x2107d8: ctx->pc = 0; goto label_2107d8;
            default: break;
        }
        ctx->pc = 0; // Reset PC and continue from start
    }

    // 0x2106ec: 0x3c031000
    SET_GPR_U32(ctx, 3, ((uint32_t)4096 << 16));
    // 0x2106f0: 0x8ec6000c
    SET_GPR_U32(ctx, 6, READ32(ADD32(GPR_U32(ctx, 22), 12)));
    // 0x2106f4: 0x3463d000
    SET_GPR_U32(ctx, 3, OR32(GPR_U32(ctx, 3), 53248));
label_2106f8:
    // 0x2106f8: 0x8c620000
    SET_GPR_U32(ctx, 2, READ32(ADD32(GPR_U32(ctx, 3), 0)));
    // 0x2106fc: 0x30420100
    SET_GPR_U32(ctx, 2, AND32(GPR_U32(ctx, 2), 256));
    // 0x210700: 0x0
    // NOP
    // 0x210704: 0x0
    // NOP
    // 0x210708: 0x0
    // NOP
    // 0x21070c: 0x1440fffa
    if (GPR_U32(ctx, 2) != GPR_U32(ctx, 0)) {
        goto label_2106f8;
    }
    // 0x210714: 0x3c031000
    SET_GPR_U32(ctx, 3, ((uint32_t)4096 << 16));
    // 0x210718: 0x3463d020
    SET_GPR_U32(ctx, 3, OR32(GPR_U32(ctx, 3), 53280));
    // 0x21071c: 0x0
    // NOP
label_210720:
    // 0x210720: 0x8c620000
    SET_GPR_U32(ctx, 2, READ32(ADD32(GPR_U32(ctx, 3), 0)));
    // 0x210724: 0x0
    // NOP
    // 0x210728: 0x0
    // NOP
    // 0x21072c: 0x0
    // NOP
    // 0x210730: 0x0
    // NOP
    // 0x210734: 0x1440fffa
    if (GPR_U32(ctx, 2) != GPR_U32(ctx, 0)) {
        goto label_210720;
    }
    // 0x21073c: 0x240882d
    SET_GPR_U64(ctx, 17, GPR_U64(ctx, 18) + GPR_U64(ctx, 0));
    // 0x210740: 0x206102a
    SET_GPR_U32(ctx, 2, SLT32(GPR_S32(ctx, 16), GPR_S32(ctx, 6)));
    // 0x210744: 0x1440ffbe
    SET_GPR_U64(ctx, 18, GPR_U64(ctx, 19) + GPR_U64(ctx, 0));
    if (GPR_U32(ctx, 2) != GPR_U32(ctx, 0)) {
        ctx->pc = 0x210640; return;
    }
    // 0x21074c: 0x10000003
    SET_GPR_U32(ctx, 7, READ32(ADD32(GPR_U32(ctx, 29), 0)));
    if (GPR_U32(ctx, 0) == GPR_U32(ctx, 0)) {
        goto label_21075c;
    }
label_210754:
    // 0x210754: 0x24b70001
    SET_GPR_S32(ctx, 23, ADD32(GPR_U32(ctx, 5), 1));
    // 0x210758: 0x8fa70000
    SET_GPR_U32(ctx, 7, READ32(ADD32(GPR_U32(ctx, 29), 0)));
label_21075c:
    // 0x21075c: 0x2e0282d
    SET_GPR_U64(ctx, 5, GPR_U64(ctx, 23) + GPR_U64(ctx, 0));
    // 0x210760: 0x240300c0
    SET_GPR_S32(ctx, 3, ADD32(GPR_U32(ctx, 0), 192));
    // 0x210764: 0x8ce200e4
    SET_GPR_U32(ctx, 2, READ32(ADD32(GPR_U32(ctx, 7), 228)));
    // 0x210768: 0x8fa70004
    SET_GPR_U32(ctx, 7, READ32(ADD32(GPR_U32(ctx, 29), 4)));
    // 0x21076c: 0xa7202a
    SET_GPR_U32(ctx, 4, SLT32(GPR_S32(ctx, 5), GPR_S32(ctx, 7)));
    // 0x210770: 0x8fa70008
    SET_GPR_U32(ctx, 7, READ32(ADD32(GPR_U32(ctx, 29), 8)));
    // 0x210774: 0xe00013
    ctx->lo = GPR_U32(ctx, 7);
    // 0x210778: 0x70430000
    { int64_t acc = ((int64_t)ctx->hi << 32) | ctx->lo; int64_t prod = (int64_t)GPR_S32(ctx, 2) * (int64_t)GPR_S32(ctx, 3); int64_t result = acc + prod; ctx->lo = (uint32_t)result; ctx->hi = (uint32_t)(result >> 32); }
    // 0x21077c: 0x3812
    SET_GPR_U32(ctx, 7, ctx->lo);
    // 0x210780: 0x1480ffab
    WRITE32(ADD32(GPR_U32(ctx, 29), 8), GPR_U32(ctx, 7));
    if (GPR_U32(ctx, 4) != GPR_U32(ctx, 0)) {
        ctx->pc = 0x210630; return;
    }
label_210788:
    // 0x210788: 0xdfbf00a0
    SET_GPR_U64(ctx, 31, READ64(ADD32(GPR_U32(ctx, 29), 160)));
    // 0x21078c: 0xdfbe0090
    SET_GPR_U64(ctx, 30, READ64(ADD32(GPR_U32(ctx, 29), 144)));
    // 0x210790: 0xdfb70080
    SET_GPR_U64(ctx, 23, READ64(ADD32(GPR_U32(ctx, 29), 128)));
    // 0x210794: 0xdfb60070
    SET_GPR_U64(ctx, 22, READ64(ADD32(GPR_U32(ctx, 29), 112)));
    // 0x210798: 0xdfb50060
    SET_GPR_U64(ctx, 21, READ64(ADD32(GPR_U32(ctx, 29), 96)));
    // 0x21079c: 0xdfb40050
    SET_GPR_U64(ctx, 20, READ64(ADD32(GPR_U32(ctx, 29), 80)));
    // 0x2107a0: 0xdfb30040
    SET_GPR_U64(ctx, 19, READ64(ADD32(GPR_U32(ctx, 29), 64)));
    // 0x2107a4: 0xdfb20030
    SET_GPR_U64(ctx, 18, READ64(ADD32(GPR_U32(ctx, 29), 48)));
    // 0x2107a8: 0xdfb10020
    SET_GPR_U64(ctx, 17, READ64(ADD32(GPR_U32(ctx, 29), 32)));
    // 0x2107ac: 0xdfb00010
    SET_GPR_U64(ctx, 16, READ64(ADD32(GPR_U32(ctx, 29), 16)));
    // 0x2107b0: 0x3e00008
    SET_GPR_S32(ctx, 29, ADD32(GPR_U32(ctx, 29), 176));
    ctx->pc = GPR_U32(ctx, 31); return;
label_2107b8:
    // 0x2107b8: 0x8c820008
    SET_GPR_U32(ctx, 2, READ32(ADD32(GPR_U32(ctx, 4), 8)));
    // 0x2107bc: 0x24030002
    SET_GPR_S32(ctx, 3, ADD32(GPR_U32(ctx, 0), 2));
    // 0x2107c0: 0x10430005
    SET_GPR_S32(ctx, 2, ADD32(GPR_U32(ctx, 0), 1));
    if (GPR_U32(ctx, 2) == GPR_U32(ctx, 3)) {
        goto label_2107d8;
    }
    // 0x2107c8: 0x8c820118
    SET_GPR_U32(ctx, 2, READ32(ADD32(GPR_U32(ctx, 4), 280)));
    // 0x2107cc: 0xac830008
    WRITE32(ADD32(GPR_U32(ctx, 4), 8), GPR_U32(ctx, 3));
    // 0x2107d0: 0xac8200ac
    WRITE32(ADD32(GPR_U32(ctx, 4), 172), GPR_U32(ctx, 2));
    // 0x2107d4: 0x24020001
    SET_GPR_S32(ctx, 2, ADD32(GPR_U32(ctx, 0), 1));
label_2107d8:
    // 0x2107d8: 0x3e00008
    WRITE32(ADD32(GPR_U32(ctx, 4), 2080), GPR_U32(ctx, 2));
    ctx->pc = GPR_U32(ctx, 31); return;
}


// Function: _getPtsDtsFlags
// Address: 0x2107e0 - 0x210860

void entry_210860(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // 0x210860: 0x8e760090
    SET_GPR_U32(ctx, 22, READ32(ADD32(GPR_U32(ctx, 19), 144)));
    // 0x210864: 0x40202d
    SET_GPR_U64(ctx, 4, GPR_U64(ctx, 2) + GPR_U64(ctx, 0));
    // 0x210868: 0xc07fa24
    SET_GPR_U32(ctx, 31, 0x210870);
    SET_GPR_U32(ctx, 5, AND32(GPR_U32(ctx, 22), 1));
    ctx->pc = 0x1fe890; return;
}


// Function: entry_210870
// Address: 0x210870 - 0x210884

void entry_210870(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // 0x210870: 0xde640078
    SET_GPR_U64(ctx, 4, READ64(ADD32(GPR_U32(ctx, 19), 120)));
    // 0x210874: 0x2883c
    SET_GPR_U64(ctx, 17, GPR_U64(ctx, 2) << (32 + 0));
    // 0x210878: 0x11883f
    SET_GPR_S64(ctx, 17, GPR_S64(ctx, 17) >> (32 + 0));
    // 0x21087c: 0xc07fa24
    SET_GPR_U32(ctx, 31, 0x210884);
    SET_GPR_U64(ctx, 5, GPR_U64(ctx, 16) + GPR_U64(ctx, 0));
    ctx->pc = 0x1fe890; return;
}


// Function: entry_210884
// Address: 0x210884 - 0x2108a8

void entry_210884(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // 0x210884: 0x217f8
    SET_GPR_U64(ctx, 2, GPR_U64(ctx, 2) << 31);
    // 0x210888: 0x2103f
    SET_GPR_S64(ctx, 2, GPR_S64(ctx, 2) >> (32 + 0));
    // 0x21088c: 0x240202d
    SET_GPR_U64(ctx, 4, GPR_U64(ctx, 18) + GPR_U64(ctx, 0));
    // 0x210890: 0x511021
    SET_GPR_U32(ctx, 2, ADD32(GPR_U32(ctx, 2), GPR_U32(ctx, 17)));
    // 0x210894: 0x2e21021
    SET_GPR_U32(ctx, 2, ADD32(GPR_U32(ctx, 23), GPR_U32(ctx, 2)));
    // 0x210898: 0xfea20000
    WRITE64(ADD32(GPR_U32(ctx, 21), 0), GPR_U64(ctx, 2));
    // 0x21089c: 0xde650078
    SET_GPR_U64(ctx, 5, READ64(ADD32(GPR_U32(ctx, 19), 120)));
    // 0x2108a0: 0xc07fa24
    SET_GPR_U32(ctx, 31, 0x2108a8);
    SET_GPR_U32(ctx, 5, AND32(GPR_U32(ctx, 5), 1));
    ctx->pc = 0x1fe890; return;
}


// Function: entry_2108a8
// Address: 0x2108a8 - 0x210970

void entry_2108a8(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // Dispatch for mid-function entry points
    if (ctx->pc != 0 && ctx->pc != 0x2108a8) {
        switch (ctx->pc) {
            case 0x2108b8: ctx->pc = 0; goto label_2108b8;
            case 0x2108c0: ctx->pc = 0; goto label_2108c0;
            case 0x2108f0: ctx->pc = 0; goto label_2108f0;
            default: break;
        }
        ctx->pc = 0; // Reset PC and continue from start
    }

    // 0x2108a8: 0x10400005
    SET_GPR_S32(ctx, 2, ADD32(GPR_U32(ctx, 22), 1));
    if (GPR_U32(ctx, 2) == GPR_U32(ctx, 0)) {
        goto label_2108c0;
    }
    // 0x2108b0: 0x10000003
    WRITE32(ADD32(GPR_U32(ctx, 19), 144), GPR_U32(ctx, 2));
    if (GPR_U32(ctx, 0) == GPR_U32(ctx, 0)) {
        goto label_2108c0;
    }
label_2108b8:
    // 0x2108b8: 0xde820018
    SET_GPR_U64(ctx, 2, READ64(ADD32(GPR_U32(ctx, 20), 24)));
    // 0x2108bc: 0xfea20000
    WRITE64(ADD32(GPR_U32(ctx, 21), 0), GPR_U64(ctx, 2));
label_2108c0:
    // 0x2108c0: 0x8e6300f8
    SET_GPR_U32(ctx, 3, READ32(ADD32(GPR_U32(ctx, 19), 248)));
    // 0x2108c4: 0x24020002
    SET_GPR_S32(ctx, 2, ADD32(GPR_U32(ctx, 0), 2));
    // 0x2108c8: 0x54620009
    if (GPR_U32(ctx, 3) != GPR_U32(ctx, 2)) {
        SET_GPR_U32(ctx, 5, READ32(ADD32(GPR_U32(ctx, 20), 64)));
        goto label_2108f0;
    }
    // 0x2108d0: 0xde6200f0
    SET_GPR_U64(ctx, 2, READ64(ADD32(GPR_U32(ctx, 19), 240)));
    // 0x2108d4: 0x4420006
    if (GPR_S32(ctx, 2) < 0) {
        SET_GPR_U32(ctx, 5, READ32(ADD32(GPR_U32(ctx, 20), 64)));
        goto label_2108f0;
    }
    // 0x2108dc: 0xfea20000
    WRITE64(ADD32(GPR_U32(ctx, 21), 0), GPR_U64(ctx, 2));
    // 0x2108e0: 0x2402ffff
    SET_GPR_S32(ctx, 2, ADD32(GPR_U32(ctx, 0), 4294967295));
    // 0x2108e4: 0xae6000f8
    WRITE32(ADD32(GPR_U32(ctx, 19), 248), GPR_U32(ctx, 0));
    // 0x2108e8: 0xfe6200f0
    WRITE64(ADD32(GPR_U32(ctx, 19), 240), GPR_U64(ctx, 2));
    // 0x2108ec: 0x8e850040
    SET_GPR_U32(ctx, 5, READ32(ADD32(GPR_U32(ctx, 20), 64)));
label_2108f0:
    // 0x2108f0: 0x8e84003c
    SET_GPR_U32(ctx, 4, READ32(ADD32(GPR_U32(ctx, 20), 60)));
    // 0x2108f4: 0x8e820034
    SET_GPR_U32(ctx, 2, READ32(ADD32(GPR_U32(ctx, 20), 52)));
    // 0x2108f8: 0x52978
    SET_GPR_U64(ctx, 5, GPR_U64(ctx, 5) << 5);
    // 0x2108fc: 0x421b8
    SET_GPR_U64(ctx, 4, GPR_U64(ctx, 4) << 6);
    // 0x210900: 0x8e860030
    SET_GPR_U32(ctx, 6, READ32(ADD32(GPR_U32(ctx, 20), 48)));
    // 0x210904: 0x8e87002c
    SET_GPR_U32(ctx, 7, READ32(ADD32(GPR_U32(ctx, 20), 44)));
    // 0x210908: 0xa42825
    SET_GPR_U32(ctx, 5, OR32(GPR_U32(ctx, 5), GPR_U32(ctx, 4)));
    // 0x21090c: 0x8e830038
    SET_GPR_U32(ctx, 3, READ32(ADD32(GPR_U32(ctx, 20), 56)));
    // 0x210910: 0x21238
    SET_GPR_U64(ctx, 2, GPR_U64(ctx, 2) << 8);
    // 0x210914: 0xde840020
    SET_GPR_U64(ctx, 4, READ64(ADD32(GPR_U32(ctx, 20), 32)));
    // 0x210918: 0x471025
    SET_GPR_U32(ctx, 2, OR32(GPR_U32(ctx, 2), GPR_U32(ctx, 7)));
    // 0x21091c: 0x630f8
    SET_GPR_U64(ctx, 6, GPR_U64(ctx, 6) << 3);
    // 0x210920: 0x319f8
    SET_GPR_U64(ctx, 3, GPR_U64(ctx, 3) << 7);
    // 0x210924: 0xffc40000
    WRITE64(ADD32(GPR_U32(ctx, 30), 0), GPR_U64(ctx, 4));
    // 0x210928: 0x661825
    SET_GPR_U32(ctx, 3, OR32(GPR_U32(ctx, 3), GPR_U32(ctx, 6)));
    // 0x21092c: 0x451025
    SET_GPR_U32(ctx, 2, OR32(GPR_U32(ctx, 2), GPR_U32(ctx, 5)));
    // 0x210930: 0xdfbf00a0
    SET_GPR_U64(ctx, 31, READ64(ADD32(GPR_U32(ctx, 29), 160)));
    // 0x210934: 0x431025
    SET_GPR_U32(ctx, 2, OR32(GPR_U32(ctx, 2), GPR_U32(ctx, 3)));
    // 0x210938: 0xdfbe0090
    SET_GPR_U64(ctx, 30, READ64(ADD32(GPR_U32(ctx, 29), 144)));
    // 0x21093c: 0x8fa30000
    SET_GPR_U32(ctx, 3, READ32(ADD32(GPR_U32(ctx, 29), 0)));
    // 0x210940: 0xdfb70080
    SET_GPR_U64(ctx, 23, READ64(ADD32(GPR_U32(ctx, 29), 128)));
    // 0x210944: 0xdfb60070
    SET_GPR_U64(ctx, 22, READ64(ADD32(GPR_U32(ctx, 29), 112)));
    // 0x210948: 0xdfb50060
    SET_GPR_U64(ctx, 21, READ64(ADD32(GPR_U32(ctx, 29), 96)));
    // 0x21094c: 0xdfb40050
    SET_GPR_U64(ctx, 20, READ64(ADD32(GPR_U32(ctx, 29), 80)));
    // 0x210950: 0xdfb30040
    SET_GPR_U64(ctx, 19, READ64(ADD32(GPR_U32(ctx, 29), 64)));
    // 0x210954: 0xdfb20030
    SET_GPR_U64(ctx, 18, READ64(ADD32(GPR_U32(ctx, 29), 48)));
    // 0x210958: 0xdfb10020
    SET_GPR_U64(ctx, 17, READ64(ADD32(GPR_U32(ctx, 29), 32)));
    // 0x21095c: 0xdfb00010
    SET_GPR_U64(ctx, 16, READ64(ADD32(GPR_U32(ctx, 29), 16)));
    // 0x210960: 0xfc620000
    WRITE64(ADD32(GPR_U32(ctx, 3), 0), GPR_U64(ctx, 2));
    // 0x210964: 0x3e00008
    SET_GPR_S32(ctx, 29, ADD32(GPR_U32(ctx, 29), 176));
    ctx->pc = GPR_U32(ctx, 31); return;
    // 0x21096c: 0x0
    // NOP
    // Fall-through to next function
    ctx->pc = 0x210970; return;
}


// Function: _dispRefImage
// Address: 0x210970 - 0x21099c

void entry_21099c(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // 0x21099c: 0x8e070858
    SET_GPR_U32(ctx, 7, READ32(ADD32(GPR_U32(ctx, 16), 2136)));
    // 0x2109a0: 0x3c060028
    SET_GPR_U32(ctx, 6, ((uint32_t)40 << 16));
    // 0x2109a4: 0x24c6b068
    SET_GPR_S32(ctx, 6, ADD32(GPR_U32(ctx, 6), 4294946920));
    // 0x2109a8: 0x200202d
    SET_GPR_U64(ctx, 4, GPR_U64(ctx, 16) + GPR_U64(ctx, 0));
    // 0x2109ac: 0xdce20020
    SET_GPR_U64(ctx, 2, READ64(ADD32(GPR_U32(ctx, 7), 32)));
    // 0x2109b0: 0x220282d
    SET_GPR_U64(ctx, 5, GPR_U64(ctx, 17) + GPR_U64(ctx, 0));
    // 0x2109b4: 0x8ce30010
    SET_GPR_U32(ctx, 3, READ32(ADD32(GPR_U32(ctx, 7), 16)));
    // 0x2109b8: 0x216f8
    SET_GPR_U64(ctx, 2, GPR_U64(ctx, 2) << 27);
    // 0x2109bc: 0x2103f
    SET_GPR_S64(ctx, 2, GPR_S64(ctx, 2) >> (32 + 0));
    // 0x2109c0: 0xae030080
    WRITE32(ADD32(GPR_U32(ctx, 16), 128), GPR_U32(ctx, 3));
    // 0x2109c4: 0x3042000f
    SET_GPR_U32(ctx, 2, AND32(GPR_U32(ctx, 2), 15));
    // 0x2109c8: 0x21080
    SET_GPR_U32(ctx, 2, SLL32(GPR_U32(ctx, 2), 2));
    // 0x2109cc: 0x8e23005c
    SET_GPR_U32(ctx, 3, READ32(ADD32(GPR_U32(ctx, 17), 92)));
    // 0x2109d0: 0x461021
    SET_GPR_U32(ctx, 2, ADD32(GPR_U32(ctx, 2), GPR_U32(ctx, 6)));
    // 0x2109d4: 0x9c460000
    SET_GPR_U32(ctx, 6, READ32(ADD32(GPR_U32(ctx, 2), 0)));
    // 0x2109d8: 0xae0300cc
    WRITE32(ADD32(GPR_U32(ctx, 16), 204), GPR_U32(ctx, 3));
    // 0x2109dc: 0xfe060088
    WRITE64(ADD32(GPR_U32(ctx, 16), 136), GPR_U64(ctx, 6));
    // 0x2109e0: 0x8e220060
    SET_GPR_U32(ctx, 2, READ32(ADD32(GPR_U32(ctx, 17), 96)));
    // 0x2109e4: 0xae0200d0
    WRITE32(ADD32(GPR_U32(ctx, 16), 208), GPR_U32(ctx, 2));
    // 0x2109e8: 0x8e230044
    SET_GPR_U32(ctx, 3, READ32(ADD32(GPR_U32(ctx, 17), 68)));
    // 0x2109ec: 0xae0300b4
    WRITE32(ADD32(GPR_U32(ctx, 16), 180), GPR_U32(ctx, 3));
    // 0x2109f0: 0x8e220048
    SET_GPR_U32(ctx, 2, READ32(ADD32(GPR_U32(ctx, 17), 72)));
    // 0x2109f4: 0xae0200b8
    WRITE32(ADD32(GPR_U32(ctx, 16), 184), GPR_U32(ctx, 2));
    // 0x2109f8: 0x8e23004c
    SET_GPR_U32(ctx, 3, READ32(ADD32(GPR_U32(ctx, 17), 76)));
    // 0x2109fc: 0xae0300bc
    WRITE32(ADD32(GPR_U32(ctx, 16), 188), GPR_U32(ctx, 3));
    // 0x210a00: 0x8e220050
    SET_GPR_U32(ctx, 2, READ32(ADD32(GPR_U32(ctx, 17), 80)));
    // 0x210a04: 0xae0200c0
    WRITE32(ADD32(GPR_U32(ctx, 16), 192), GPR_U32(ctx, 2));
    // 0x210a08: 0x8e230054
    SET_GPR_U32(ctx, 3, READ32(ADD32(GPR_U32(ctx, 17), 84)));
    // 0x210a0c: 0xae0300c4
    WRITE32(ADD32(GPR_U32(ctx, 16), 196), GPR_U32(ctx, 3));
    // 0x210a10: 0x8e220058
    SET_GPR_U32(ctx, 2, READ32(ADD32(GPR_U32(ctx, 17), 88)));
    // 0x210a14: 0xc08412a
    SET_GPR_U32(ctx, 31, 0x210a1c);
    WRITE32(ADD32(GPR_U32(ctx, 16), 200), GPR_U32(ctx, 2));
    _isOutSizeOK(rdram, ctx, runtime); return;
}


// Function: entry_210a1c
// Address: 0x210a1c - 0x210a44

void entry_210a1c(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // 0x210a1c: 0x10400013
    SET_GPR_S32(ctx, 2, ADD32(GPR_U32(ctx, 0), 1));
    if (GPR_U32(ctx, 2) == GPR_U32(ctx, 0)) {
        ctx->pc = 0x210A6C; return;
    }
    // 0x210a24: 0x8e230028
    SET_GPR_U32(ctx, 3, READ32(ADD32(GPR_U32(ctx, 17), 40)));
    // 0x210a28: 0x14620011
    SET_GPR_U64(ctx, 31, READ64(ADD32(GPR_U32(ctx, 29), 32)));
    if (GPR_U32(ctx, 3) != GPR_U32(ctx, 2)) {
        ctx->pc = 0x210A70; return;
    }
    // 0x210a30: 0x8e0200b0
    SET_GPR_U32(ctx, 2, READ32(ADD32(GPR_U32(ctx, 16), 176)));
    // 0x210a34: 0x10400005
    SET_GPR_U64(ctx, 5, GPR_U64(ctx, 17) + GPR_U64(ctx, 0));
    if (GPR_U32(ctx, 2) == GPR_U32(ctx, 0)) {
        ctx->pc = 0x210A4C; return;
    }
    // 0x210a3c: 0xc0844d2
    SET_GPR_U32(ctx, 31, 0x210a44);
    SET_GPR_U64(ctx, 4, GPR_U64(ctx, 16) + GPR_U64(ctx, 0));
    _csc_storeRefImage(rdram, ctx, runtime); return;
}


// Function: entry_210a44
// Address: 0x210a44 - 0x210a54

void entry_210a44(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // Dispatch for mid-function entry points
    if (ctx->pc != 0 && ctx->pc != 0x210a44) {
        switch (ctx->pc) {
            case 0x210a4c: ctx->pc = 0; goto label_210a4c;
            default: break;
        }
        ctx->pc = 0; // Reset PC and continue from start
    }

    // 0x210a44: 0x10000004
    SET_GPR_U64(ctx, 4, GPR_U64(ctx, 16) + GPR_U64(ctx, 0));
    if (GPR_U32(ctx, 0) == GPR_U32(ctx, 0)) {
        ctx->pc = 0x210A58; return;
    }
label_210a4c:
    // 0x210a4c: 0xc084152
    SET_GPR_U32(ctx, 31, 0x210a54);
    SET_GPR_U64(ctx, 4, GPR_U64(ctx, 16) + GPR_U64(ctx, 0));
    _cpr8(rdram, ctx, runtime); return;
}


// Function: entry_210a54
// Address: 0x210a54 - 0x210a80

void entry_210a54(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // Dispatch for mid-function entry points
    if (ctx->pc != 0 && ctx->pc != 0x210a54) {
        switch (ctx->pc) {
            case 0x210a58: ctx->pc = 0; goto label_210a58;
            case 0x210a6c: ctx->pc = 0; goto label_210a6c;
            case 0x210a70: ctx->pc = 0; goto label_210a70;
            default: break;
        }
        ctx->pc = 0; // Reset PC and continue from start
    }

    // 0x210a54: 0x200202d
    SET_GPR_U64(ctx, 4, GPR_U64(ctx, 16) + GPR_U64(ctx, 0));
label_210a58:
    // 0x210a58: 0xdfbf0020
    SET_GPR_U64(ctx, 31, READ64(ADD32(GPR_U32(ctx, 29), 32)));
    // 0x210a5c: 0xdfb10010
    SET_GPR_U64(ctx, 17, READ64(ADD32(GPR_U32(ctx, 29), 16)));
    // 0x210a60: 0xdfb00000
    SET_GPR_U64(ctx, 16, READ64(ADD32(GPR_U32(ctx, 29), 0)));
    // 0x210a64: 0x80841ee
    SET_GPR_S32(ctx, 29, ADD32(GPR_U32(ctx, 29), 48));
    ctx->pc = 0x2107b8; return;
label_210a6c:
    // 0x210a6c: 0xdfbf0020
    SET_GPR_U64(ctx, 31, READ64(ADD32(GPR_U32(ctx, 29), 32)));
label_210a70:
    // 0x210a70: 0xdfb10010
    SET_GPR_U64(ctx, 17, READ64(ADD32(GPR_U32(ctx, 29), 16)));
    // 0x210a74: 0xdfb00000
    SET_GPR_U64(ctx, 16, READ64(ADD32(GPR_U32(ctx, 29), 0)));
    // 0x210a78: 0x3e00008
    SET_GPR_S32(ctx, 29, ADD32(GPR_U32(ctx, 29), 48));
    ctx->pc = GPR_U32(ctx, 31); return;
}


// Function: _dispRefImageField
// Address: 0x210a80 - 0x210af8

void entry_210af8(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // 0x210af8: 0x8e270858
    SET_GPR_U32(ctx, 7, READ32(ADD32(GPR_U32(ctx, 17), 2136)));
    // 0x210afc: 0x220202d
    SET_GPR_U64(ctx, 4, GPR_U64(ctx, 17) + GPR_U64(ctx, 0));
    // 0x210b00: 0x280282d
    SET_GPR_U64(ctx, 5, GPR_U64(ctx, 20) + GPR_U64(ctx, 0));
    // 0x210b04: 0x8ce20010
    SET_GPR_U32(ctx, 2, READ32(ADD32(GPR_U32(ctx, 7), 16)));
    // 0x210b08: 0x24e80038
    SET_GPR_S32(ctx, 8, ADD32(GPR_U32(ctx, 7), 56));
    // 0x210b0c: 0x24e60028
    SET_GPR_S32(ctx, 6, ADD32(GPR_U32(ctx, 7), 40));
    // 0x210b10: 0xfe300088
    WRITE64(ADD32(GPR_U32(ctx, 17), 136), GPR_U64(ctx, 16));
    // 0x210b14: 0xae220080
    WRITE32(ADD32(GPR_U32(ctx, 17), 128), GPR_U32(ctx, 2));
    // 0x210b18: 0xc0841f8
    SET_GPR_U32(ctx, 31, 0x210b20);
    SET_GPR_S32(ctx, 7, ADD32(GPR_U32(ctx, 7), 48));
    _getPtsDtsFlags(rdram, ctx, runtime); return;
}


// Function: entry_210b20
// Address: 0x210b20 - 0x210b88

void entry_210b20(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // 0x210b20: 0x8e270858
    SET_GPR_U32(ctx, 7, READ32(ADD32(GPR_U32(ctx, 17), 2136)));
    // 0x210b24: 0x2c0402d
    SET_GPR_U64(ctx, 8, GPR_U64(ctx, 22) + GPR_U64(ctx, 0));
    // 0x210b28: 0x220202d
    SET_GPR_U64(ctx, 4, GPR_U64(ctx, 17) + GPR_U64(ctx, 0));
    // 0x210b2c: 0x240282d
    SET_GPR_U64(ctx, 5, GPR_U64(ctx, 18) + GPR_U64(ctx, 0));
    // 0x210b30: 0x8ce30028
    SET_GPR_U32(ctx, 3, READ32(ADD32(GPR_U32(ctx, 7), 40)));
    // 0x210b34: 0xfe300088
    WRITE64(ADD32(GPR_U32(ctx, 17), 136), GPR_U64(ctx, 16));
    // 0x210b38: 0xae230080
    WRITE32(ADD32(GPR_U32(ctx, 17), 128), GPR_U32(ctx, 3));
    // 0x210b3c: 0xdce20020
    SET_GPR_U64(ctx, 2, READ64(ADD32(GPR_U32(ctx, 7), 32)));
    // 0x210b40: 0x8e66005c
    SET_GPR_U32(ctx, 6, READ32(ADD32(GPR_U32(ctx, 19), 92)));
    // 0x210b44: 0x481025
    SET_GPR_U32(ctx, 2, OR32(GPR_U32(ctx, 2), GPR_U32(ctx, 8)));
    // 0x210b48: 0xdce30038
    SET_GPR_U64(ctx, 3, READ64(ADD32(GPR_U32(ctx, 7), 56)));
    // 0x210b4c: 0xae2600cc
    WRITE32(ADD32(GPR_U32(ctx, 17), 204), GPR_U32(ctx, 6));
    // 0x210b50: 0xfce20020
    WRITE64(ADD32(GPR_U32(ctx, 7), 32), GPR_U64(ctx, 2));
    // 0x210b54: 0x681825
    SET_GPR_U32(ctx, 3, OR32(GPR_U32(ctx, 3), GPR_U32(ctx, 8)));
    // 0x210b58: 0x8e660060
    SET_GPR_U32(ctx, 6, READ32(ADD32(GPR_U32(ctx, 19), 96)));
    // 0x210b5c: 0xfce30038
    WRITE64(ADD32(GPR_U32(ctx, 7), 56), GPR_U64(ctx, 3));
    // 0x210b60: 0xae2600d0
    WRITE32(ADD32(GPR_U32(ctx, 17), 208), GPR_U32(ctx, 6));
    // 0x210b64: 0x8e620044
    SET_GPR_U32(ctx, 2, READ32(ADD32(GPR_U32(ctx, 19), 68)));
    // 0x210b68: 0xae2200b4
    WRITE32(ADD32(GPR_U32(ctx, 17), 180), GPR_U32(ctx, 2));
    // 0x210b6c: 0x8e830048
    SET_GPR_U32(ctx, 3, READ32(ADD32(GPR_U32(ctx, 20), 72)));
    // 0x210b70: 0xae2300b8
    WRITE32(ADD32(GPR_U32(ctx, 17), 184), GPR_U32(ctx, 3));
    // 0x210b74: 0x8e620050
    SET_GPR_U32(ctx, 2, READ32(ADD32(GPR_U32(ctx, 19), 80)));
    // 0x210b78: 0xae2200c0
    WRITE32(ADD32(GPR_U32(ctx, 17), 192), GPR_U32(ctx, 2));
    // 0x210b7c: 0x8e830054
    SET_GPR_U32(ctx, 3, READ32(ADD32(GPR_U32(ctx, 20), 84)));
    // 0x210b80: 0xc08412a
    SET_GPR_U32(ctx, 31, 0x210b88);
    WRITE32(ADD32(GPR_U32(ctx, 17), 196), GPR_U32(ctx, 3));
    _isOutSizeOK(rdram, ctx, runtime); return;
}


// Function: entry_210b88
// Address: 0x210b88 - 0x210bc8

void entry_210b88(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // 0x210b88: 0x10400021
    SET_GPR_S32(ctx, 2, ADD32(GPR_U32(ctx, 0), 1));
    if (GPR_U32(ctx, 2) == GPR_U32(ctx, 0)) {
        ctx->pc = 0x210C10; return;
    }
    // 0x210b90: 0x8e430028
    SET_GPR_U32(ctx, 3, READ32(ADD32(GPR_U32(ctx, 18), 40)));
    // 0x210b94: 0x1462001f
    SET_GPR_U64(ctx, 31, READ64(ADD32(GPR_U32(ctx, 29), 112)));
    if (GPR_U32(ctx, 3) != GPR_U32(ctx, 2)) {
        ctx->pc = 0x210C14; return;
    }
    // 0x210b9c: 0x8ea20028
    SET_GPR_U32(ctx, 2, READ32(ADD32(GPR_U32(ctx, 21), 40)));
    // 0x210ba0: 0x1443001d
    SET_GPR_U64(ctx, 22, READ64(ADD32(GPR_U32(ctx, 29), 96)));
    if (GPR_U32(ctx, 2) != GPR_U32(ctx, 3)) {
        ctx->pc = 0x210C18; return;
    }
    // 0x210ba8: 0x8e420010
    SET_GPR_U32(ctx, 2, READ32(ADD32(GPR_U32(ctx, 18), 16)));
    // 0x210bac: 0x21040
    SET_GPR_U32(ctx, 2, SLL32(GPR_U32(ctx, 2), 1));
    // 0x210bb0: 0xae420010
    WRITE32(ADD32(GPR_U32(ctx, 18), 16), GPR_U32(ctx, 2));
    // 0x210bb4: 0x8e2300b0
    SET_GPR_U32(ctx, 3, READ32(ADD32(GPR_U32(ctx, 17), 176)));
    // 0x210bb8: 0x10600005
    SET_GPR_U64(ctx, 4, GPR_U64(ctx, 17) + GPR_U64(ctx, 0));
    if (GPR_U32(ctx, 3) == GPR_U32(ctx, 0)) {
        ctx->pc = 0x210BD0; return;
    }
    // 0x210bc0: 0xc0844d2
    SET_GPR_U32(ctx, 31, 0x210bc8);
    SET_GPR_U64(ctx, 5, GPR_U64(ctx, 18) + GPR_U64(ctx, 0));
    _csc_storeRefImage(rdram, ctx, runtime); return;
}


// Function: entry_210bc8
// Address: 0x210bc8 - 0x210bd8

void entry_210bc8(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // Dispatch for mid-function entry points
    if (ctx->pc != 0 && ctx->pc != 0x210bc8) {
        switch (ctx->pc) {
            case 0x210bd0: ctx->pc = 0; goto label_210bd0;
            default: break;
        }
        ctx->pc = 0; // Reset PC and continue from start
    }

    // 0x210bc8: 0x10000004
    SET_GPR_U32(ctx, 2, READ32(ADD32(GPR_U32(ctx, 18), 16)));
    if (GPR_U32(ctx, 0) == GPR_U32(ctx, 0)) {
        ctx->pc = 0x210BDC; return;
    }
label_210bd0:
    // 0x210bd0: 0xc084152
    SET_GPR_U32(ctx, 31, 0x210bd8);
    SET_GPR_U64(ctx, 5, GPR_U64(ctx, 18) + GPR_U64(ctx, 0));
    _cpr8(rdram, ctx, runtime); return;
}


// Function: entry_210bd8
// Address: 0x210bd8 - 0x210c38

void entry_210bd8(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // Dispatch for mid-function entry points
    if (ctx->pc != 0 && ctx->pc != 0x210bd8) {
        switch (ctx->pc) {
            case 0x210bdc: ctx->pc = 0; goto label_210bdc;
            case 0x210c10: ctx->pc = 0; goto label_210c10;
            case 0x210c14: ctx->pc = 0; goto label_210c14;
            case 0x210c18: ctx->pc = 0; goto label_210c18;
            default: break;
        }
        ctx->pc = 0; // Reset PC and continue from start
    }

    // 0x210bd8: 0x8e420010
    SET_GPR_U32(ctx, 2, READ32(ADD32(GPR_U32(ctx, 18), 16)));
label_210bdc:
    // 0x210bdc: 0x220202d
    SET_GPR_U64(ctx, 4, GPR_U64(ctx, 17) + GPR_U64(ctx, 0));
    // 0x210be0: 0xdfbf0070
    SET_GPR_U64(ctx, 31, READ64(ADD32(GPR_U32(ctx, 29), 112)));
    // 0x210be4: 0x21043
    SET_GPR_S32(ctx, 2, SRA32(GPR_S32(ctx, 2), 1));
    // 0x210be8: 0xdfb60060
    SET_GPR_U64(ctx, 22, READ64(ADD32(GPR_U32(ctx, 29), 96)));
    // 0x210bec: 0xae420010
    WRITE32(ADD32(GPR_U32(ctx, 18), 16), GPR_U32(ctx, 2));
    // 0x210bf0: 0xdfb50050
    SET_GPR_U64(ctx, 21, READ64(ADD32(GPR_U32(ctx, 29), 80)));
    // 0x210bf4: 0xdfb40040
    SET_GPR_U64(ctx, 20, READ64(ADD32(GPR_U32(ctx, 29), 64)));
    // 0x210bf8: 0xdfb30030
    SET_GPR_U64(ctx, 19, READ64(ADD32(GPR_U32(ctx, 29), 48)));
    // 0x210bfc: 0xdfb20020
    SET_GPR_U64(ctx, 18, READ64(ADD32(GPR_U32(ctx, 29), 32)));
    // 0x210c00: 0xdfb10010
    SET_GPR_U64(ctx, 17, READ64(ADD32(GPR_U32(ctx, 29), 16)));
    // 0x210c04: 0xdfb00000
    SET_GPR_U64(ctx, 16, READ64(ADD32(GPR_U32(ctx, 29), 0)));
    // 0x210c08: 0x80841ee
    SET_GPR_S32(ctx, 29, ADD32(GPR_U32(ctx, 29), 128));
    ctx->pc = 0x2107b8; return;
label_210c10:
    // 0x210c10: 0xdfbf0070
    SET_GPR_U64(ctx, 31, READ64(ADD32(GPR_U32(ctx, 29), 112)));
label_210c14:
    // 0x210c14: 0xdfb60060
    SET_GPR_U64(ctx, 22, READ64(ADD32(GPR_U32(ctx, 29), 96)));
label_210c18:
    // 0x210c18: 0xdfb50050
    SET_GPR_U64(ctx, 21, READ64(ADD32(GPR_U32(ctx, 29), 80)));
    // 0x210c1c: 0xdfb40040
    SET_GPR_U64(ctx, 20, READ64(ADD32(GPR_U32(ctx, 29), 64)));
    // 0x210c20: 0xdfb30030
    SET_GPR_U64(ctx, 19, READ64(ADD32(GPR_U32(ctx, 29), 48)));
    // 0x210c24: 0xdfb20020
    SET_GPR_U64(ctx, 18, READ64(ADD32(GPR_U32(ctx, 29), 32)));
    // 0x210c28: 0xdfb10010
    SET_GPR_U64(ctx, 17, READ64(ADD32(GPR_U32(ctx, 29), 16)));
    // 0x210c2c: 0xdfb00000
    SET_GPR_U64(ctx, 16, READ64(ADD32(GPR_U32(ctx, 29), 0)));
    // 0x210c30: 0x3e00008
    SET_GPR_S32(ctx, 29, ADD32(GPR_U32(ctx, 29), 128));
    ctx->pc = GPR_U32(ctx, 31); return;
}


// Function: dmaRefImage
// Address: 0x210c38 - 0x210d24

void entry_210d24(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // 0x210d24: 0xf
    // SYNC instruction - memory barrier
// In recompiled code, we don't need explicit memory barriers
    // 0x210d28: 0x8e050810
    SET_GPR_U32(ctx, 5, READ32(ADD32(GPR_U32(ctx, 16), 2064)));
    // 0x210d2c: 0x24020140
    SET_GPR_S32(ctx, 2, ADD32(GPR_U32(ctx, 0), 320));
    // 0x210d30: 0x3c061000
    SET_GPR_U32(ctx, 6, ((uint32_t)4096 << 16));
    // 0x210d34: 0x3c071000
    SET_GPR_U32(ctx, 7, ((uint32_t)4096 << 16));
    // 0x210d38: 0xa21818
    { int64_t result = (int64_t)GPR_S32(ctx, 5) * (int64_t)GPR_S32(ctx, 2); ctx->lo = (uint32_t)result; ctx->hi = (uint32_t)(result >> 32); }
    // 0x210d3c: 0x34c6d480
    SET_GPR_U32(ctx, 6, OR32(GPR_U32(ctx, 6), 54400));
    // 0x210d40: 0x2649c340
    SET_GPR_S32(ctx, 9, ADD32(GPR_U32(ctx, 18), 4294951744));
    // 0x210d44: 0x34e7d430
    SET_GPR_U32(ctx, 7, OR32(GPR_U32(ctx, 7), 54320));
    // 0x210d48: 0x3c021000
    SET_GPR_U32(ctx, 2, ((uint32_t)4096 << 16));
    // 0x210d4c: 0x24080105
    SET_GPR_S32(ctx, 8, ADD32(GPR_U32(ctx, 0), 261));
    // 0x210d50: 0x3442d420
    SET_GPR_U32(ctx, 2, OR32(GPR_U32(ctx, 2), 54304));
    // 0x210d54: 0xdfbf0030
    SET_GPR_U64(ctx, 31, READ64(ADD32(GPR_U32(ctx, 29), 48)));
    // 0x210d58: 0x712821
    SET_GPR_U32(ctx, 5, ADD32(GPR_U32(ctx, 3), GPR_U32(ctx, 17)));
    // 0x210d5c: 0xdfb20020
    SET_GPR_U64(ctx, 18, READ64(ADD32(GPR_U32(ctx, 29), 32)));
    // 0x210d60: 0x8ca40000
    SET_GPR_U32(ctx, 4, READ32(ADD32(GPR_U32(ctx, 5), 0)));
    // 0x210d64: 0x3c031000
    SET_GPR_U32(ctx, 3, ((uint32_t)4096 << 16));
    // 0x210d68: 0x3463d400
    SET_GPR_U32(ctx, 3, OR32(GPR_U32(ctx, 3), 54272));
    // 0x210d6c: 0xdfb10010
    SET_GPR_U64(ctx, 17, READ64(ADD32(GPR_U32(ctx, 29), 16)));
    // 0x210d70: 0xacc40000
    WRITE32(ADD32(GPR_U32(ctx, 6), 0), GPR_U32(ctx, 4));
    // 0x210d74: 0xace90000
    WRITE32(ADD32(GPR_U32(ctx, 7), 0), GPR_U32(ctx, 9));
    // 0x210d78: 0xac400000
    WRITE32(ADD32(GPR_U32(ctx, 2), 0), GPR_U32(ctx, 0));
    // 0x210d7c: 0xdfb00000
    SET_GPR_U64(ctx, 16, READ64(ADD32(GPR_U32(ctx, 29), 0)));
    // 0x210d80: 0xac680000
    WRITE32(ADD32(GPR_U32(ctx, 3), 0), GPR_U32(ctx, 8));
    // 0x210d84: 0x807e76a
    SET_GPR_S32(ctx, 29, ADD32(GPR_U32(ctx, 29), 64));
    EIntr(rdram, ctx, runtime); return;
    // 0x210d8c: 0x0
    // NOP
    // Fall-through to next function
    ctx->pc = 0x210d90; return;
}


// Function: receiveDataFromIPU
// Address: 0x210d90 - 0x210dac

void entry_210dac(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // 0x210dac: 0x3c030fff
    SET_GPR_U32(ctx, 3, ((uint32_t)4095 << 16));
    // 0x210db0: 0x3c048000
    SET_GPR_U32(ctx, 4, ((uint32_t)32768 << 16));
    // 0x210db4: 0x3463ffff
    SET_GPR_U32(ctx, 3, OR32(GPR_U32(ctx, 3), 65535));
    // 0x210db8: 0x3c021000
    SET_GPR_U32(ctx, 2, ((uint32_t)4096 << 16));
    // 0x210dbc: 0x2038024
    SET_GPR_U32(ctx, 16, AND32(GPR_U32(ctx, 16), GPR_U32(ctx, 3)));
    // 0x210dc0: 0x3442b010
    SET_GPR_U32(ctx, 2, OR32(GPR_U32(ctx, 2), 45072));
    // 0x210dc4: 0x2048025
    SET_GPR_U32(ctx, 16, OR32(GPR_U32(ctx, 16), GPR_U32(ctx, 4)));
    // 0x210dc8: 0x3c031000
    SET_GPR_U32(ctx, 3, ((uint32_t)4096 << 16));
    // 0x210dcc: 0xac500000
    WRITE32(ADD32(GPR_U32(ctx, 2), 0), GPR_U32(ctx, 16));
    // 0x210dd0: 0x118903
    SET_GPR_S32(ctx, 17, SRA32(GPR_S32(ctx, 17), 4));
    // 0x210dd4: 0x3463b020
    SET_GPR_U32(ctx, 3, OR32(GPR_U32(ctx, 3), 45088));
    // 0x210dd8: 0x3c021000
    SET_GPR_U32(ctx, 2, ((uint32_t)4096 << 16));
    // 0x210ddc: 0xac710000
    WRITE32(ADD32(GPR_U32(ctx, 3), 0), GPR_U32(ctx, 17));
    // 0x210de0: 0x3442b000
    SET_GPR_U32(ctx, 2, OR32(GPR_U32(ctx, 2), 45056));
    // 0x210de4: 0x24030100
    SET_GPR_S32(ctx, 3, ADD32(GPR_U32(ctx, 0), 256));
    // 0x210de8: 0xdfbf0020
    SET_GPR_U64(ctx, 31, READ64(ADD32(GPR_U32(ctx, 29), 32)));
    // 0x210dec: 0xdfb10010
    SET_GPR_U64(ctx, 17, READ64(ADD32(GPR_U32(ctx, 29), 16)));
    // 0x210df0: 0xdfb00000
    SET_GPR_U64(ctx, 16, READ64(ADD32(GPR_U32(ctx, 29), 0)));
    // 0x210df4: 0xac430000
    WRITE32(ADD32(GPR_U32(ctx, 2), 0), GPR_U32(ctx, 3));
    // 0x210df8: 0x807e76a
    SET_GPR_S32(ctx, 29, ADD32(GPR_U32(ctx, 29), 48));
    EIntr(rdram, ctx, runtime); return;
}


// Function: _doCSC
// Address: 0x210e00 - 0x210e54

void entry_210e54(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // 0x210e54: 0x3c020fff
    SET_GPR_U32(ctx, 2, ((uint32_t)4095 << 16));
    // 0x210e58: 0x3c041000
    SET_GPR_U32(ctx, 4, ((uint32_t)4096 << 16));
    // 0x210e5c: 0x3442ffff
    SET_GPR_U32(ctx, 2, OR32(GPR_U32(ctx, 2), 65535));
    // 0x210e60: 0x3484b010
    SET_GPR_U32(ctx, 4, OR32(GPR_U32(ctx, 4), 45072));
    // 0x210e64: 0x2021024
    SET_GPR_U32(ctx, 2, AND32(GPR_U32(ctx, 16), GPR_U32(ctx, 2)));
    // 0x210e68: 0x3c031000
    SET_GPR_U32(ctx, 3, ((uint32_t)4096 << 16));
    // 0x210e6c: 0xac820000
    WRITE32(ADD32(GPR_U32(ctx, 4), 0), GPR_U32(ctx, 2));
    // 0x210e70: 0x3463b020
    SET_GPR_U32(ctx, 3, OR32(GPR_U32(ctx, 3), 45088));
    // 0x210e74: 0xac730000
    WRITE32(ADD32(GPR_U32(ctx, 3), 0), GPR_U32(ctx, 19));
    // 0x210e78: 0x3c021000
    SET_GPR_U32(ctx, 2, ((uint32_t)4096 << 16));
    // 0x210e7c: 0x3442b000
    SET_GPR_U32(ctx, 2, OR32(GPR_U32(ctx, 2), 45056));
    // 0x210e80: 0x24030100
    SET_GPR_S32(ctx, 3, ADD32(GPR_U32(ctx, 0), 256));
    // 0x210e84: 0xc07e76a
    SET_GPR_U32(ctx, 31, 0x210e8c);
    WRITE32(ADD32(GPR_U32(ctx, 2), 0), GPR_U32(ctx, 3));
    EIntr(rdram, ctx, runtime); return;
}


// Function: entry_210e8c
// Address: 0x210e8c - 0x210e9c

void entry_210e8c(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // 0x210e8c: 0x3c057000
    SET_GPR_U32(ctx, 5, ((uint32_t)28672 << 16));
    // 0x210e90: 0x220202d
    SET_GPR_U64(ctx, 4, GPR_U64(ctx, 17) + GPR_U64(ctx, 0));
    // 0x210e94: 0xc083c30
    SET_GPR_U32(ctx, 31, 0x210e9c);
    SET_GPR_U32(ctx, 5, OR32(GPR_U32(ctx, 18), GPR_U32(ctx, 5)));
    _sendIpuCommand(rdram, ctx, runtime); return;
}


// Function: entry_210e9c
// Address: 0x210e9c - 0x210eb0

void entry_210e9c(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // 0x210e9c: 0x8e240858
    SET_GPR_U32(ctx, 4, READ32(ADD32(GPR_U32(ctx, 17), 2136)));
    // 0x210ea0: 0x24020004
    SET_GPR_S32(ctx, 2, ADD32(GPR_U32(ctx, 0), 4));
    // 0x210ea4: 0xafa20000
    WRITE32(ADD32(GPR_U32(ctx, 29), 0), GPR_U32(ctx, 2));
    // 0x210ea8: 0xc082c64
    SET_GPR_U32(ctx, 31, 0x210eb0);
    SET_GPR_U64(ctx, 5, GPR_U64(ctx, 29) + GPR_U64(ctx, 0));
    _dispatchMpegCallback(rdram, ctx, runtime); return;
}


// Function: entry_210eb0
// Address: 0x210eb0 - 0x210f18

void entry_210eb0(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // Dispatch for mid-function entry points
    if (ctx->pc != 0 && ctx->pc != 0x210eb0) {
        switch (ctx->pc) {
            case 0x210eb8: ctx->pc = 0; goto label_210eb8;
            case 0x210ee0: ctx->pc = 0; goto label_210ee0;
            default: break;
        }
        ctx->pc = 0; // Reset PC and continue from start
    }

    // 0x210eb0: 0x3c031000
    SET_GPR_U32(ctx, 3, ((uint32_t)4096 << 16));
    // 0x210eb4: 0x3463b000
    SET_GPR_U32(ctx, 3, OR32(GPR_U32(ctx, 3), 45056));
label_210eb8:
    // 0x210eb8: 0x8c620000
    SET_GPR_U32(ctx, 2, READ32(ADD32(GPR_U32(ctx, 3), 0)));
    // 0x210ebc: 0x21202
    SET_GPR_U32(ctx, 2, SRL32(GPR_U32(ctx, 2), 8));
    // 0x210ec0: 0x30420001
    SET_GPR_U32(ctx, 2, AND32(GPR_U32(ctx, 2), 1));
    // 0x210ec4: 0x0
    // NOP
    // 0x210ec8: 0x0
    // NOP
    // 0x210ecc: 0x1440fffa
    if (GPR_U32(ctx, 2) != GPR_U32(ctx, 0)) {
        goto label_210eb8;
    }
    // 0x210ed4: 0x3c031000
    SET_GPR_U32(ctx, 3, ((uint32_t)4096 << 16));
    // 0x210ed8: 0x34632010
    SET_GPR_U32(ctx, 3, OR32(GPR_U32(ctx, 3), 8208));
    // 0x210edc: 0x0
    // NOP
label_210ee0:
    // 0x210ee0: 0x8c620000
    SET_GPR_U32(ctx, 2, READ32(ADD32(GPR_U32(ctx, 3), 0)));
    // 0x210ee4: 0x0
    // NOP
    // 0x210ee8: 0x0
    // NOP
    // 0x210eec: 0x0
    // NOP
    // 0x210ef0: 0x0
    // NOP
    // 0x210ef4: 0x440fffa
    if (GPR_S32(ctx, 2) < 0) {
        goto label_210ee0;
    }
    // 0x210efc: 0xdfbf0060
    SET_GPR_U64(ctx, 31, READ64(ADD32(GPR_U32(ctx, 29), 96)));
    // 0x210f00: 0xdfb30050
    SET_GPR_U64(ctx, 19, READ64(ADD32(GPR_U32(ctx, 29), 80)));
    // 0x210f04: 0xdfb20040
    SET_GPR_U64(ctx, 18, READ64(ADD32(GPR_U32(ctx, 29), 64)));
    // 0x210f08: 0xdfb10030
    SET_GPR_U64(ctx, 17, READ64(ADD32(GPR_U32(ctx, 29), 48)));
    // 0x210f0c: 0xdfb00020
    SET_GPR_U64(ctx, 16, READ64(ADD32(GPR_U32(ctx, 29), 32)));
    // 0x210f10: 0x3e00008
    SET_GPR_S32(ctx, 29, ADD32(GPR_U32(ctx, 29), 112));
    ctx->pc = GPR_U32(ctx, 31); return;
}


// Function: _ch3dmaCSC
// Address: 0x210f18 - 0x210f9c

void entry_210f9c(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // 0x210f9c: 0x8e05000c
    SET_GPR_U32(ctx, 5, READ32(ADD32(GPR_U32(ctx, 16), 12)));
    // 0x210fa0: 0x3c021000
    SET_GPR_U32(ctx, 2, ((uint32_t)4096 << 16));
    // 0x210fa4: 0x3442b010
    SET_GPR_U32(ctx, 2, OR32(GPR_U32(ctx, 2), 45072));
    // 0x210fa8: 0x3404ffc0
    SET_GPR_U32(ctx, 4, OR32(GPR_U32(ctx, 0), 65472));
    // 0x210fac: 0xac450000
    WRITE32(ADD32(GPR_U32(ctx, 2), 0), GPR_U32(ctx, 5));
    // 0x210fb0: 0x24030100
    SET_GPR_S32(ctx, 3, ADD32(GPR_U32(ctx, 0), 256));
    // 0x210fb4: 0x3c011001
    SET_GPR_U32(ctx, 1, ((uint32_t)4097 << 16));
    // 0x210fb8: 0xac24b020
    WRITE32(ADD32(GPR_U32(ctx, 1), 4294946848), GPR_U32(ctx, 4));
    // 0x210fbc: 0x3c011001
    SET_GPR_U32(ctx, 1, ((uint32_t)4097 << 16));
    // 0x210fc0: 0xc07e76a
    SET_GPR_U32(ctx, 31, 0x210fc8);
    WRITE32(ADD32(GPR_U32(ctx, 1), 4294946816), GPR_U32(ctx, 3));
    EIntr(rdram, ctx, runtime); return;
}


// Function: entry_210fc8
// Address: 0x210fc8 - 0x211028

void entry_210fc8(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // Dispatch for mid-function entry points
    if (ctx->pc != 0 && ctx->pc != 0x210fc8) {
        switch (ctx->pc) {
            case 0x211000: ctx->pc = 0; goto label_211000;
            default: break;
        }
        ctx->pc = 0; // Reset PC and continue from start
    }

    // 0x210fc8: 0x3c031000
    SET_GPR_U32(ctx, 3, ((uint32_t)4096 << 16));
    // 0x210fcc: 0x3c027000
    SET_GPR_U32(ctx, 2, ((uint32_t)28672 << 16));
    // 0x210fd0: 0x34632000
    SET_GPR_U32(ctx, 3, OR32(GPR_U32(ctx, 3), 8192));
    // 0x210fd4: 0x344203ff
    SET_GPR_U32(ctx, 2, OR32(GPR_U32(ctx, 2), 1023));
    // 0x210fd8: 0xac620000
    WRITE32(ADD32(GPR_U32(ctx, 3), 0), GPR_U32(ctx, 2));
    // 0x210fdc: 0x3c04000f
    SET_GPR_U32(ctx, 4, ((uint32_t)15 << 16));
    // 0x210fe0: 0x3484fc00
    SET_GPR_U32(ctx, 4, OR32(GPR_U32(ctx, 4), 64512));
    // 0x210fe4: 0x3c030fff
    SET_GPR_U32(ctx, 3, ((uint32_t)4095 << 16));
    // 0x210fe8: 0x8e02000c
    SET_GPR_U32(ctx, 2, READ32(ADD32(GPR_U32(ctx, 16), 12)));
    // 0x210fec: 0x3463ffff
    SET_GPR_U32(ctx, 3, OR32(GPR_U32(ctx, 3), 65535));
    // 0x210ff0: 0x441021
    SET_GPR_U32(ctx, 2, ADD32(GPR_U32(ctx, 2), GPR_U32(ctx, 4)));
    // 0x210ff4: 0x431024
    SET_GPR_U32(ctx, 2, AND32(GPR_U32(ctx, 2), GPR_U32(ctx, 3)));
    // 0x210ff8: 0x1000001d
    WRITE32(ADD32(GPR_U32(ctx, 16), 12), GPR_U32(ctx, 2));
    if (GPR_U32(ctx, 0) == GPR_U32(ctx, 0)) {
        ctx->pc = 0x211070; return;
    }
label_211000:
    // 0x211000: 0x8e020000
    SET_GPR_U32(ctx, 2, READ32(ADD32(GPR_U32(ctx, 16), 0)));
    // 0x211004: 0x1443001a
    if (GPR_U32(ctx, 2) != GPR_U32(ctx, 3)) {
        ctx->pc = 0x211070; return;
    }
    // 0x21100c: 0x8e040000
    SET_GPR_U32(ctx, 4, READ32(ADD32(GPR_U32(ctx, 16), 0)));
    // 0x211010: 0x8e030008
    SET_GPR_U32(ctx, 3, READ32(ADD32(GPR_U32(ctx, 16), 8)));
    // 0x211014: 0x41280
    SET_GPR_U32(ctx, 2, SLL32(GPR_U32(ctx, 4), 10));
    // 0x211018: 0x441023
    SET_GPR_U32(ctx, 2, SUB32(GPR_U32(ctx, 2), GPR_U32(ctx, 4)));
    // 0x21101c: 0x621823
    SET_GPR_U32(ctx, 3, SUB32(GPR_U32(ctx, 3), GPR_U32(ctx, 2)));
    // 0x211020: 0xc07e758
    SET_GPR_U32(ctx, 31, 0x211028);
    WRITE32(ADD32(GPR_U32(ctx, 16), 8), GPR_U32(ctx, 3));
    DIntr(rdram, ctx, runtime); return;
}


// Function: entry_211028
// Address: 0x211028 - 0x211058

void entry_211028(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // 0x211028: 0x8e04000c
    SET_GPR_U32(ctx, 4, READ32(ADD32(GPR_U32(ctx, 16), 12)));
    // 0x21102c: 0x3c031000
    SET_GPR_U32(ctx, 3, ((uint32_t)4096 << 16));
    // 0x211030: 0x3463b010
    SET_GPR_U32(ctx, 3, OR32(GPR_U32(ctx, 3), 45072));
    // 0x211034: 0x24050100
    SET_GPR_S32(ctx, 5, ADD32(GPR_U32(ctx, 0), 256));
    // 0x211038: 0xac640000
    WRITE32(ADD32(GPR_U32(ctx, 3), 0), GPR_U32(ctx, 4));
    // 0x21103c: 0x8e020008
    SET_GPR_U32(ctx, 2, READ32(ADD32(GPR_U32(ctx, 16), 8)));
    // 0x211040: 0x21180
    SET_GPR_U32(ctx, 2, SLL32(GPR_U32(ctx, 2), 6));
    // 0x211044: 0x3c011001
    SET_GPR_U32(ctx, 1, ((uint32_t)4097 << 16));
    // 0x211048: 0xac22b020
    WRITE32(ADD32(GPR_U32(ctx, 1), 4294946848), GPR_U32(ctx, 2));
    // 0x21104c: 0x3c011001
    SET_GPR_U32(ctx, 1, ((uint32_t)4097 << 16));
    // 0x211050: 0xc07e76a
    SET_GPR_U32(ctx, 31, 0x211058);
    WRITE32(ADD32(GPR_U32(ctx, 1), 4294946816), GPR_U32(ctx, 5));
    EIntr(rdram, ctx, runtime); return;
}


// Function: entry_211058
// Address: 0x211058 - 0x211090

void entry_211058(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // Dispatch for mid-function entry points
    if (ctx->pc != 0 && ctx->pc != 0x211058) {
        switch (ctx->pc) {
            case 0x211070: ctx->pc = 0; goto label_211070;
            case 0x21107c: ctx->pc = 0; goto label_21107c;
            default: break;
        }
        ctx->pc = 0; // Reset PC and continue from start
    }

    // 0x211058: 0x8e030008
    SET_GPR_U32(ctx, 3, READ32(ADD32(GPR_U32(ctx, 16), 8)));
    // 0x21105c: 0x3c021000
    SET_GPR_U32(ctx, 2, ((uint32_t)4096 << 16));
    // 0x211060: 0x3c047000
    SET_GPR_U32(ctx, 4, ((uint32_t)28672 << 16));
    // 0x211064: 0x34422000
    SET_GPR_U32(ctx, 2, OR32(GPR_U32(ctx, 2), 8192));
    // 0x211068: 0x641825
    SET_GPR_U32(ctx, 3, OR32(GPR_U32(ctx, 3), GPR_U32(ctx, 4)));
    // 0x21106c: 0xac430000
    WRITE32(ADD32(GPR_U32(ctx, 2), 0), GPR_U32(ctx, 3));
label_211070:
    // 0x211070: 0xf
    // SYNC instruction - memory barrier
// In recompiled code, we don't need explicit memory barriers
    // 0x211074: 0x42000038
    ctx->cop0_status |= 0x1; // Enable interrupts
    // 0x211078: 0x102d
    SET_GPR_U64(ctx, 2, GPR_U64(ctx, 0) + GPR_U64(ctx, 0));
label_21107c:
    // 0x21107c: 0xdfbf0010
    SET_GPR_U64(ctx, 31, READ64(ADD32(GPR_U32(ctx, 29), 16)));
    // 0x211080: 0xdfb00000
    SET_GPR_U64(ctx, 16, READ64(ADD32(GPR_U32(ctx, 29), 0)));
    // 0x211084: 0x3e00008
    SET_GPR_S32(ctx, 29, ADD32(GPR_U32(ctx, 29), 32));
    ctx->pc = GPR_U32(ctx, 31); return;
    // 0x21108c: 0x0
    // NOP
    // Fall-through to next function
    ctx->pc = 0x211090; return;
}


// Function: _doCSC2
// Address: 0x211090 - 0x211130

void entry_211130(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // 0x211130: 0x40902d
    SET_GPR_U64(ctx, 18, GPR_U64(ctx, 2) + GPR_U64(ctx, 0));
    // 0x211134: 0x24030008
    SET_GPR_S32(ctx, 3, ADD32(GPR_U32(ctx, 0), 8));
    // 0x211138: 0x3c021000
    SET_GPR_U32(ctx, 2, ((uint32_t)4096 << 16));
    // 0x21113c: 0x24040003
    SET_GPR_S32(ctx, 4, ADD32(GPR_U32(ctx, 0), 3));
    // 0x211140: 0x3442e010
    SET_GPR_U32(ctx, 2, OR32(GPR_U32(ctx, 2), 57360));
    // 0x211144: 0xc07db9e
    SET_GPR_U32(ctx, 31, 0x21114c);
    WRITE32(ADD32(GPR_U32(ctx, 2), 0), GPR_U32(ctx, 3));
    FUN_001f6e78(rdram, ctx, runtime); return;
}


// Function: entry_21114c
// Address: 0x21114c - 0x211154

void entry_21114c(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // 0x21114c: 0xc07e758
    SET_GPR_U32(ctx, 31, 0x211154);
    DIntr(rdram, ctx, runtime); return;
}


// Function: entry_211154
// Address: 0x211154 - 0x211190

void entry_211154(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // 0x211154: 0x3c030fff
    SET_GPR_U32(ctx, 3, ((uint32_t)4095 << 16));
    // 0x211158: 0x3c021000
    SET_GPR_U32(ctx, 2, ((uint32_t)4096 << 16));
    // 0x21115c: 0x3463ffff
    SET_GPR_U32(ctx, 3, OR32(GPR_U32(ctx, 3), 65535));
    // 0x211160: 0x3442b010
    SET_GPR_U32(ctx, 2, OR32(GPR_U32(ctx, 2), 45072));
    // 0x211164: 0x2031824
    SET_GPR_U32(ctx, 3, AND32(GPR_U32(ctx, 16), GPR_U32(ctx, 3)));
    // 0x211168: 0x3c041000
    SET_GPR_U32(ctx, 4, ((uint32_t)4096 << 16));
    // 0x21116c: 0xac430000
    WRITE32(ADD32(GPR_U32(ctx, 2), 0), GPR_U32(ctx, 3));
    // 0x211170: 0x3484b020
    SET_GPR_U32(ctx, 4, OR32(GPR_U32(ctx, 4), 45088));
    // 0x211174: 0x3402ffc0
    SET_GPR_U32(ctx, 2, OR32(GPR_U32(ctx, 0), 65472));
    // 0x211178: 0x3c031000
    SET_GPR_U32(ctx, 3, ((uint32_t)4096 << 16));
    // 0x21117c: 0xac820000
    WRITE32(ADD32(GPR_U32(ctx, 4), 0), GPR_U32(ctx, 2));
    // 0x211180: 0x3463b000
    SET_GPR_U32(ctx, 3, OR32(GPR_U32(ctx, 3), 45056));
    // 0x211184: 0x24020100
    SET_GPR_S32(ctx, 2, ADD32(GPR_U32(ctx, 0), 256));
    // 0x211188: 0xc07e76a
    SET_GPR_U32(ctx, 31, 0x211190);
    WRITE32(ADD32(GPR_U32(ctx, 3), 0), GPR_U32(ctx, 2));
    EIntr(rdram, ctx, runtime); return;
}


// Function: entry_211190
// Address: 0x211190 - 0x2111b8

void entry_211190(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // 0x211190: 0x3c031000
    SET_GPR_U32(ctx, 3, ((uint32_t)4096 << 16));
    // 0x211194: 0x3c027000
    SET_GPR_U32(ctx, 2, ((uint32_t)28672 << 16));
    // 0x211198: 0x34632000
    SET_GPR_U32(ctx, 3, OR32(GPR_U32(ctx, 3), 8192));
    // 0x21119c: 0x344203ff
    SET_GPR_U32(ctx, 2, OR32(GPR_U32(ctx, 2), 1023));
    // 0x2111a0: 0xac620000
    WRITE32(ADD32(GPR_U32(ctx, 3), 0), GPR_U32(ctx, 2));
    // 0x2111a4: 0x24060004
    SET_GPR_S32(ctx, 6, ADD32(GPR_U32(ctx, 0), 4));
    // 0x2111a8: 0x8e240858
    SET_GPR_U32(ctx, 4, READ32(ADD32(GPR_U32(ctx, 17), 2136)));
    // 0x2111ac: 0x3a0282d
    SET_GPR_U64(ctx, 5, GPR_U64(ctx, 29) + GPR_U64(ctx, 0));
    // 0x2111b0: 0xc082c64
    SET_GPR_U32(ctx, 31, 0x2111b8);
    WRITE32(ADD32(GPR_U32(ctx, 29), 0), GPR_U32(ctx, 6));
    _dispatchMpegCallback(rdram, ctx, runtime); return;
}


// Function: entry_2111b8
// Address: 0x2111b8 - 0x2111f0

void entry_2111b8(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // Dispatch for mid-function entry points
    if (ctx->pc != 0 && ctx->pc != 0x2111b8) {
        switch (ctx->pc) {
            case 0x2111c0: ctx->pc = 0; goto label_2111c0;
            default: break;
        }
        ctx->pc = 0; // Reset PC and continue from start
    }

    // 0x2111b8: 0x8fa40024
    SET_GPR_U32(ctx, 4, READ32(ADD32(GPR_U32(ctx, 29), 36)));
    // 0x2111bc: 0x8fa30030
    SET_GPR_U32(ctx, 3, READ32(ADD32(GPR_U32(ctx, 29), 48)));
label_2111c0:
    // 0x2111c0: 0x8fa20020
    SET_GPR_U32(ctx, 2, READ32(ADD32(GPR_U32(ctx, 29), 32)));
    // 0x2111c4: 0x43102a
    SET_GPR_U32(ctx, 2, SLT32(GPR_S32(ctx, 2), GPR_S32(ctx, 3)));
    // 0x2111c8: 0x0
    // NOP
    // 0x2111cc: 0x0
    // NOP
    // 0x2111d0: 0x0
    // NOP
    // 0x2111d4: 0x1440fffa
    if (GPR_U32(ctx, 2) != GPR_U32(ctx, 0)) {
        goto label_2111c0;
    }
    // 0x2111dc: 0x10800004
    SET_GPR_U64(ctx, 4, GPR_U64(ctx, 17) + GPR_U64(ctx, 0));
    if (GPR_U32(ctx, 4) == GPR_U32(ctx, 0)) {
        entry_2111f0(rdram, ctx, runtime); return;
    }
    // 0x2111e4: 0x3c050025
    SET_GPR_U32(ctx, 5, ((uint32_t)37 << 16));
    // 0x2111e8: 0xc082e5e
    SET_GPR_U32(ctx, 31, 0x2111f0);
    SET_GPR_S32(ctx, 5, ADD32(GPR_U32(ctx, 5), 1320));
    fn__Error(rdram, ctx, runtime); return;
}


// Function: entry_2111f0
// Address: 0x2111f0 - 0x21121c

void entry_2111f0(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // Dispatch for mid-function entry points
    if (ctx->pc != 0 && ctx->pc != 0x2111f0) {
        switch (ctx->pc) {
            case 0x2111f8: ctx->pc = 0; goto label_2111f8;
            default: break;
        }
        ctx->pc = 0; // Reset PC and continue from start
    }

    // 0x2111f0: 0x3c031000
    SET_GPR_U32(ctx, 3, ((uint32_t)4096 << 16));
    // 0x2111f4: 0x34632010
    SET_GPR_U32(ctx, 3, OR32(GPR_U32(ctx, 3), 8208));
label_2111f8:
    // 0x2111f8: 0x8c620000
    SET_GPR_U32(ctx, 2, READ32(ADD32(GPR_U32(ctx, 3), 0)));
    // 0x2111fc: 0x0
    // NOP
    // 0x211200: 0x0
    // NOP
    // 0x211204: 0x0
    // NOP
    // 0x211208: 0x0
    // NOP
    // 0x21120c: 0x440fffa
    if (GPR_S32(ctx, 2) < 0) {
        goto label_2111f8;
    }
    // 0x211214: 0xc07db84
    SET_GPR_U32(ctx, 31, 0x21121c);
    SET_GPR_S32(ctx, 4, ADD32(GPR_U32(ctx, 0), 3));
    DisableDmac(rdram, ctx, runtime); return;
}


// Function: entry_21121c
// Address: 0x21121c - 0x211228

void entry_21121c(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // 0x21121c: 0x240282d
    SET_GPR_U64(ctx, 5, GPR_U64(ctx, 18) + GPR_U64(ctx, 0));
    // 0x211220: 0xc07da64
    SET_GPR_U32(ctx, 31, 0x211228);
    SET_GPR_S32(ctx, 4, ADD32(GPR_U32(ctx, 0), 3));
    RemoveDmacHandler(rdram, ctx, runtime); return;
}


// Function: entry_211228
// Address: 0x211228 - 0x211240

void entry_211228(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // 0x211228: 0xdfbf0070
    SET_GPR_U64(ctx, 31, READ64(ADD32(GPR_U32(ctx, 29), 112)));
    // 0x21122c: 0xdfb20060
    SET_GPR_U64(ctx, 18, READ64(ADD32(GPR_U32(ctx, 29), 96)));
    // 0x211230: 0xdfb10050
    SET_GPR_U64(ctx, 17, READ64(ADD32(GPR_U32(ctx, 29), 80)));
    // 0x211234: 0xdfb00040
    SET_GPR_U64(ctx, 16, READ64(ADD32(GPR_U32(ctx, 29), 64)));
    // 0x211238: 0x3e00008
    SET_GPR_S32(ctx, 29, ADD32(GPR_U32(ctx, 29), 128));
    ctx->pc = GPR_U32(ctx, 31); return;
}


// Function: _ch4dma
// Address: 0x211240 - 0x21128c

void entry_21128c(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // 0x21128c: 0x8e040004
    SET_GPR_U32(ctx, 4, READ32(ADD32(GPR_U32(ctx, 16), 4)));
    // 0x211290: 0x3c021000
    SET_GPR_U32(ctx, 2, ((uint32_t)4096 << 16));
    // 0x211294: 0x3442b410
    SET_GPR_U32(ctx, 2, OR32(GPR_U32(ctx, 2), 46096));
    // 0x211298: 0x3c031000
    SET_GPR_U32(ctx, 3, ((uint32_t)4096 << 16));
    // 0x21129c: 0xac440000
    WRITE32(ADD32(GPR_U32(ctx, 2), 0), GPR_U32(ctx, 4));
    // 0x2112a0: 0x3463b420
    SET_GPR_U32(ctx, 3, OR32(GPR_U32(ctx, 3), 46112));
    // 0x2112a4: 0xac710000
    WRITE32(ADD32(GPR_U32(ctx, 3), 0), GPR_U32(ctx, 17));
    // 0x2112a8: 0x3c021000
    SET_GPR_U32(ctx, 2, ((uint32_t)4096 << 16));
    // 0x2112ac: 0x3442b400
    SET_GPR_U32(ctx, 2, OR32(GPR_U32(ctx, 2), 46080));
    // 0x2112b0: 0x24030101
    SET_GPR_S32(ctx, 3, ADD32(GPR_U32(ctx, 0), 257));
    // 0x2112b4: 0xc07e76a
    SET_GPR_U32(ctx, 31, 0x2112bc);
    WRITE32(ADD32(GPR_U32(ctx, 2), 0), GPR_U32(ctx, 3));
    EIntr(rdram, ctx, runtime); return;
}


// Function: entry_2112bc
// Address: 0x2112bc - 0x2112f4

void entry_2112bc(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // Dispatch for mid-function entry points
    if (ctx->pc != 0 && ctx->pc != 0x2112bc) {
        switch (ctx->pc) {
            case 0x2112ec: ctx->pc = 0; goto label_2112ec;
            default: break;
        }
        ctx->pc = 0; // Reset PC and continue from start
    }

    // 0x2112bc: 0x8e040004
    SET_GPR_U32(ctx, 4, READ32(ADD32(GPR_U32(ctx, 16), 4)));
    // 0x2112c0: 0x3c02000f
    SET_GPR_U32(ctx, 2, ((uint32_t)15 << 16));
    // 0x2112c4: 0x8e050000
    SET_GPR_U32(ctx, 5, READ32(ADD32(GPR_U32(ctx, 16), 0)));
    // 0x2112c8: 0x3442fff0
    SET_GPR_U32(ctx, 2, OR32(GPR_U32(ctx, 2), 65520));
    // 0x2112cc: 0x3c030fff
    SET_GPR_U32(ctx, 3, ((uint32_t)4095 << 16));
    // 0x2112d0: 0x822021
    SET_GPR_U32(ctx, 4, ADD32(GPR_U32(ctx, 4), GPR_U32(ctx, 2)));
    // 0x2112d4: 0x3463ffff
    SET_GPR_U32(ctx, 3, OR32(GPR_U32(ctx, 3), 65535));
    // 0x2112d8: 0xb12823
    SET_GPR_U32(ctx, 5, SUB32(GPR_U32(ctx, 5), GPR_U32(ctx, 17)));
    // 0x2112dc: 0x832024
    SET_GPR_U32(ctx, 4, AND32(GPR_U32(ctx, 4), GPR_U32(ctx, 3)));
    // 0x2112e0: 0xae050000
    WRITE32(ADD32(GPR_U32(ctx, 16), 0), GPR_U32(ctx, 5));
    // 0x2112e4: 0x10000011
    WRITE32(ADD32(GPR_U32(ctx, 16), 4), GPR_U32(ctx, 4));
    if (GPR_U32(ctx, 0) == GPR_U32(ctx, 0)) {
        ctx->pc = 0x21132C; return;
    }
label_2112ec:
    // 0x2112ec: 0xc07e758
    SET_GPR_U32(ctx, 31, 0x2112f4);
    DIntr(rdram, ctx, runtime); return;
}


// Function: entry_2112f4
// Address: 0x2112f4 - 0x211328

void entry_2112f4(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // 0x2112f4: 0x8e040004
    SET_GPR_U32(ctx, 4, READ32(ADD32(GPR_U32(ctx, 16), 4)));
    // 0x2112f8: 0x3c021000
    SET_GPR_U32(ctx, 2, ((uint32_t)4096 << 16));
    // 0x2112fc: 0x3442b410
    SET_GPR_U32(ctx, 2, OR32(GPR_U32(ctx, 2), 46096));
    // 0x211300: 0x3c031000
    SET_GPR_U32(ctx, 3, ((uint32_t)4096 << 16));
    // 0x211304: 0xac440000
    WRITE32(ADD32(GPR_U32(ctx, 2), 0), GPR_U32(ctx, 4));
    // 0x211308: 0x3463b420
    SET_GPR_U32(ctx, 3, OR32(GPR_U32(ctx, 3), 46112));
    // 0x21130c: 0x3c021000
    SET_GPR_U32(ctx, 2, ((uint32_t)4096 << 16));
    // 0x211310: 0x24050101
    SET_GPR_S32(ctx, 5, ADD32(GPR_U32(ctx, 0), 257));
    // 0x211314: 0x8e040000
    SET_GPR_U32(ctx, 4, READ32(ADD32(GPR_U32(ctx, 16), 0)));
    // 0x211318: 0x3442b400
    SET_GPR_U32(ctx, 2, OR32(GPR_U32(ctx, 2), 46080));
    // 0x21131c: 0xac640000
    WRITE32(ADD32(GPR_U32(ctx, 3), 0), GPR_U32(ctx, 4));
    // 0x211320: 0xc07e76a
    SET_GPR_U32(ctx, 31, 0x211328);
    WRITE32(ADD32(GPR_U32(ctx, 2), 0), GPR_U32(ctx, 5));
    EIntr(rdram, ctx, runtime); return;
}


// Function: entry_211328
// Address: 0x211328 - 0x211348

void entry_211328(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // Dispatch for mid-function entry points
    if (ctx->pc != 0 && ctx->pc != 0x211328) {
        switch (ctx->pc) {
            case 0x21132c: ctx->pc = 0; goto label_21132c;
            case 0x211330: ctx->pc = 0; goto label_211330;
            default: break;
        }
        ctx->pc = 0; // Reset PC and continue from start
    }

    // 0x211328: 0xae000000
    WRITE32(ADD32(GPR_U32(ctx, 16), 0), GPR_U32(ctx, 0));
label_21132c:
    // 0x21132c: 0x102d
    SET_GPR_U64(ctx, 2, GPR_U64(ctx, 0) + GPR_U64(ctx, 0));
label_211330:
    // 0x211330: 0xdfbf0020
    SET_GPR_U64(ctx, 31, READ64(ADD32(GPR_U32(ctx, 29), 32)));
    // 0x211334: 0xdfb10010
    SET_GPR_U64(ctx, 17, READ64(ADD32(GPR_U32(ctx, 29), 16)));
    // 0x211338: 0xdfb00000
    SET_GPR_U64(ctx, 16, READ64(ADD32(GPR_U32(ctx, 29), 0)));
    // 0x21133c: 0x3e00008
    SET_GPR_S32(ctx, 29, ADD32(GPR_U32(ctx, 29), 48));
    ctx->pc = GPR_U32(ctx, 31); return;
    // 0x211344: 0x0
    // NOP
    // Fall-through to next function
    ctx->pc = 0x211348; return;
}


// Function: _csc_storeRefImage
// Address: 0x211348 - 0x211390

void entry_211390(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // Dispatch for mid-function entry points
    if (ctx->pc != 0 && ctx->pc != 0x211390) {
        switch (ctx->pc) {
            case 0x2113b0: ctx->pc = 0; goto label_2113b0;
            case 0x2113c0: ctx->pc = 0; goto label_2113c0;
            default: break;
        }
        ctx->pc = 0; // Reset PC and continue from start
    }

    // 0x211390: 0x3c031000
    SET_GPR_U32(ctx, 3, ((uint32_t)4096 << 16));
    // 0x211394: 0x34632010
    SET_GPR_U32(ctx, 3, OR32(GPR_U32(ctx, 3), 8208));
    // 0x211398: 0x8c620000
    SET_GPR_U32(ctx, 2, READ32(ADD32(GPR_U32(ctx, 3), 0)));
    // 0x21139c: 0x30424000
    SET_GPR_U32(ctx, 2, AND32(GPR_U32(ctx, 2), 16384));
    // 0x2113a0: 0x10400003
    SET_GPR_U32(ctx, 2, ((uint32_t)16384 << 16));
    if (GPR_U32(ctx, 2) == GPR_U32(ctx, 0)) {
        goto label_2113b0;
    }
    // 0x2113a8: 0x3c011000
    SET_GPR_U32(ctx, 1, ((uint32_t)4096 << 16));
    // 0x2113ac: 0xac222010
    WRITE32(ADD32(GPR_U32(ctx, 1), 8208), GPR_U32(ctx, 2));
label_2113b0:
    // 0x2113b0: 0x3c031000
    SET_GPR_U32(ctx, 3, ((uint32_t)4096 << 16));
    // 0x2113b4: 0x2a750400
    SET_GPR_U32(ctx, 21, SLT32(GPR_S32(ctx, 19), 1024));
    // 0x2113b8: 0x34632010
    SET_GPR_U32(ctx, 3, OR32(GPR_U32(ctx, 3), 8208));
    // 0x2113bc: 0x0
    // NOP
label_2113c0:
    // 0x2113c0: 0x8c620000
    SET_GPR_U32(ctx, 2, READ32(ADD32(GPR_U32(ctx, 3), 0)));
    // 0x2113c4: 0x0
    // NOP
    // 0x2113c8: 0x0
    // NOP
    // 0x2113cc: 0x0
    // NOP
    // 0x2113d0: 0x0
    // NOP
    // 0x2113d4: 0x440fffa
    if (GPR_S32(ctx, 2) < 0) {
        goto label_2113c0;
    }
    // 0x2113dc: 0x200202d
    SET_GPR_U64(ctx, 4, GPR_U64(ctx, 16) + GPR_U64(ctx, 0));
    // 0x2113e0: 0xc083c30
    SET_GPR_U32(ctx, 31, 0x2113e8);
    SET_GPR_U64(ctx, 5, GPR_U64(ctx, 0) + GPR_U64(ctx, 0));
    _sendIpuCommand(rdram, ctx, runtime); return;
}


// Function: entry_2113e8
// Address: 0x2113e8 - 0x211454

void entry_2113e8(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // Dispatch for mid-function entry points
    if (ctx->pc != 0 && ctx->pc != 0x2113e8) {
        switch (ctx->pc) {
            case 0x2113f8: ctx->pc = 0; goto label_2113f8;
            default: break;
        }
        ctx->pc = 0; // Reset PC and continue from start
    }

    // 0x2113e8: 0x3c041000
    SET_GPR_U32(ctx, 4, ((uint32_t)4096 << 16));
    // 0x2113ec: 0x8e430000
    SET_GPR_U32(ctx, 3, READ32(ADD32(GPR_U32(ctx, 18), 0)));
    // 0x2113f0: 0x34842010
    SET_GPR_U32(ctx, 4, OR32(GPR_U32(ctx, 4), 8208));
    // 0x2113f4: 0x0
    // NOP
label_2113f8:
    // 0x2113f8: 0x8c820000
    SET_GPR_U32(ctx, 2, READ32(ADD32(GPR_U32(ctx, 4), 0)));
    // 0x2113fc: 0x0
    // NOP
    // 0x211400: 0x0
    // NOP
    // 0x211404: 0x0
    // NOP
    // 0x211408: 0x0
    // NOP
    // 0x21140c: 0x440fffa
    if (GPR_S32(ctx, 2) < 0) {
        goto label_2113f8;
    }
    // 0x211414: 0x24020018
    SET_GPR_S32(ctx, 2, ADD32(GPR_U32(ctx, 0), 24));
    // 0x211418: 0x3c110fff
    SET_GPR_U32(ctx, 17, ((uint32_t)4095 << 16));
    // 0x21141c: 0x2621018
    { int64_t result = (int64_t)GPR_S32(ctx, 19) * (int64_t)GPR_S32(ctx, 2); ctx->lo = (uint32_t)result; ctx->hi = (uint32_t)(result >> 32); }
    // 0x211420: 0x3631ffff
    SET_GPR_U32(ctx, 17, OR32(GPR_U32(ctx, 17), 65535));
    // 0x211424: 0x711824
    SET_GPR_U32(ctx, 3, AND32(GPR_U32(ctx, 3), GPR_U32(ctx, 17)));
    // 0x211428: 0x3414ffff
    SET_GPR_U32(ctx, 20, OR32(GPR_U32(ctx, 0), 65535));
    // 0x21142c: 0xafa30024
    WRITE32(ADD32(GPR_U32(ctx, 29), 36), GPR_U32(ctx, 3));
    // 0x211430: 0x282202b
    SET_GPR_U32(ctx, 4, SLTU32(GPR_U32(ctx, 20), GPR_U32(ctx, 2)));
    // 0x211434: 0x10800037
    WRITE32(ADD32(GPR_U32(ctx, 29), 32), GPR_U32(ctx, 2));
    if (GPR_U32(ctx, 4) == GPR_U32(ctx, 0)) {
        ctx->pc = 0x211514; return;
    }
    // 0x21143c: 0x3c050021
    SET_GPR_U32(ctx, 5, ((uint32_t)33 << 16));
    // 0x211440: 0x24040004
    SET_GPR_S32(ctx, 4, ADD32(GPR_U32(ctx, 0), 4));
    // 0x211444: 0x24a51240
    SET_GPR_S32(ctx, 5, ADD32(GPR_U32(ctx, 5), 4672));
    // 0x211448: 0x302d
    SET_GPR_U64(ctx, 6, GPR_U64(ctx, 0) + GPR_U64(ctx, 0));
    // 0x21144c: 0xc07da60
    SET_GPR_U32(ctx, 31, 0x211454);
    SET_GPR_S32(ctx, 7, ADD32(GPR_U32(ctx, 29), 32));
    AddDmacHandler1(rdram, ctx, runtime); return;
}


// Function: entry_211454
// Address: 0x211454 - 0x211470

void entry_211454(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // 0x211454: 0x40902d
    SET_GPR_U64(ctx, 18, GPR_U64(ctx, 2) + GPR_U64(ctx, 0));
    // 0x211458: 0x24030010
    SET_GPR_S32(ctx, 3, ADD32(GPR_U32(ctx, 0), 16));
    // 0x21145c: 0x3c021000
    SET_GPR_U32(ctx, 2, ((uint32_t)4096 << 16));
    // 0x211460: 0x24040004
    SET_GPR_S32(ctx, 4, ADD32(GPR_U32(ctx, 0), 4));
    // 0x211464: 0x3442e010
    SET_GPR_U32(ctx, 2, OR32(GPR_U32(ctx, 2), 57360));
    // 0x211468: 0xc07db9e
    SET_GPR_U32(ctx, 31, 0x211470);
    WRITE32(ADD32(GPR_U32(ctx, 2), 0), GPR_U32(ctx, 3));
    FUN_001f6e78(rdram, ctx, runtime); return;
}


// Function: entry_211470
// Address: 0x211470 - 0x211478

void entry_211470(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // 0x211470: 0xc07e758
    SET_GPR_U32(ctx, 31, 0x211478);
    DIntr(rdram, ctx, runtime); return;
}


// Function: entry_211478
// Address: 0x211478 - 0x2114a8

void entry_211478(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // 0x211478: 0x8fa40024
    SET_GPR_U32(ctx, 4, READ32(ADD32(GPR_U32(ctx, 29), 36)));
    // 0x21147c: 0x3c021000
    SET_GPR_U32(ctx, 2, ((uint32_t)4096 << 16));
    // 0x211480: 0x3442b410
    SET_GPR_U32(ctx, 2, OR32(GPR_U32(ctx, 2), 46096));
    // 0x211484: 0x3c031000
    SET_GPR_U32(ctx, 3, ((uint32_t)4096 << 16));
    // 0x211488: 0xac440000
    WRITE32(ADD32(GPR_U32(ctx, 2), 0), GPR_U32(ctx, 4));
    // 0x21148c: 0x3463b420
    SET_GPR_U32(ctx, 3, OR32(GPR_U32(ctx, 3), 46112));
    // 0x211490: 0xac740000
    WRITE32(ADD32(GPR_U32(ctx, 3), 0), GPR_U32(ctx, 20));
    // 0x211494: 0x3c021000
    SET_GPR_U32(ctx, 2, ((uint32_t)4096 << 16));
    // 0x211498: 0x3442b400
    SET_GPR_U32(ctx, 2, OR32(GPR_U32(ctx, 2), 46080));
    // 0x21149c: 0x24030101
    SET_GPR_S32(ctx, 3, ADD32(GPR_U32(ctx, 0), 257));
    // 0x2114a0: 0xc07e76a
    SET_GPR_U32(ctx, 31, 0x2114a8);
    WRITE32(ADD32(GPR_U32(ctx, 2), 0), GPR_U32(ctx, 3));
    EIntr(rdram, ctx, runtime); return;
}


// Function: entry_2114a8
// Address: 0x2114a8 - 0x2114e0

void entry_2114a8(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // 0x2114a8: 0x8fa30024
    SET_GPR_U32(ctx, 3, READ32(ADD32(GPR_U32(ctx, 29), 36)));
    // 0x2114ac: 0x3c02000f
    SET_GPR_U32(ctx, 2, ((uint32_t)15 << 16));
    // 0x2114b0: 0x8fa40020
    SET_GPR_U32(ctx, 4, READ32(ADD32(GPR_U32(ctx, 29), 32)));
    // 0x2114b4: 0x3442fff0
    SET_GPR_U32(ctx, 2, OR32(GPR_U32(ctx, 2), 65520));
    // 0x2114b8: 0x621821
    SET_GPR_U32(ctx, 3, ADD32(GPR_U32(ctx, 3), GPR_U32(ctx, 2)));
    // 0x2114bc: 0x711824
    SET_GPR_U32(ctx, 3, AND32(GPR_U32(ctx, 3), GPR_U32(ctx, 17)));
    // 0x2114c0: 0x942023
    SET_GPR_U32(ctx, 4, SUB32(GPR_U32(ctx, 4), GPR_U32(ctx, 20)));
    // 0x2114c4: 0xafa30024
    WRITE32(ADD32(GPR_U32(ctx, 29), 36), GPR_U32(ctx, 3));
    // 0x2114c8: 0x12a00007
    WRITE32(ADD32(GPR_U32(ctx, 29), 32), GPR_U32(ctx, 4));
    if (GPR_U32(ctx, 21) == GPR_U32(ctx, 0)) {
        ctx->pc = 0x2114E8; return;
    }
    // 0x2114d0: 0x8e0500d8
    SET_GPR_U32(ctx, 5, READ32(ADD32(GPR_U32(ctx, 16), 216)));
    // 0x2114d4: 0x260302d
    SET_GPR_U64(ctx, 6, GPR_U64(ctx, 19) + GPR_U64(ctx, 0));
    // 0x2114d8: 0xc084380
    SET_GPR_U32(ctx, 31, 0x2114e0);
    SET_GPR_U64(ctx, 4, GPR_U64(ctx, 16) + GPR_U64(ctx, 0));
    _doCSC(rdram, ctx, runtime); return;
}


// Function: entry_2114e0
// Address: 0x2114e0 - 0x2114f8

void entry_2114e0(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // Dispatch for mid-function entry points
    if (ctx->pc != 0 && ctx->pc != 0x2114e0) {
        switch (ctx->pc) {
            case 0x2114e8: ctx->pc = 0; goto label_2114e8;
            default: break;
        }
        ctx->pc = 0; // Reset PC and continue from start
    }

    // 0x2114e0: 0x10000005
    if (GPR_U32(ctx, 0) == GPR_U32(ctx, 0)) {
        entry_2114f8(rdram, ctx, runtime); return;
    }
label_2114e8:
    // 0x2114e8: 0x8e0500d8
    SET_GPR_U32(ctx, 5, READ32(ADD32(GPR_U32(ctx, 16), 216)));
    // 0x2114ec: 0x260302d
    SET_GPR_U64(ctx, 6, GPR_U64(ctx, 19) + GPR_U64(ctx, 0));
    // 0x2114f0: 0xc084424
    SET_GPR_U32(ctx, 31, 0x2114f8);
    SET_GPR_U64(ctx, 4, GPR_U64(ctx, 16) + GPR_U64(ctx, 0));
    _doCSC2(rdram, ctx, runtime); return;
}


// Function: entry_2114f8
// Address: 0x2114f8 - 0x211500

void entry_2114f8(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // 0x2114f8: 0xc07db84
    SET_GPR_U32(ctx, 31, 0x211500);
    SET_GPR_S32(ctx, 4, ADD32(GPR_U32(ctx, 0), 4));
    DisableDmac(rdram, ctx, runtime); return;
}


// Function: entry_211500
// Address: 0x211500 - 0x21150c

void entry_211500(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // 0x211500: 0x240282d
    SET_GPR_U64(ctx, 5, GPR_U64(ctx, 18) + GPR_U64(ctx, 0));
    // 0x211504: 0xc07da64
    SET_GPR_U32(ctx, 31, 0x21150c);
    SET_GPR_S32(ctx, 4, ADD32(GPR_U32(ctx, 0), 4));
    RemoveDmacHandler(rdram, ctx, runtime); return;
}


// Function: entry_21150c
// Address: 0x21150c - 0x21151c

void entry_21150c(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // Dispatch for mid-function entry points
    if (ctx->pc != 0 && ctx->pc != 0x21150c) {
        switch (ctx->pc) {
            case 0x211514: ctx->pc = 0; goto label_211514;
            default: break;
        }
        ctx->pc = 0; // Reset PC and continue from start
    }

    // 0x21150c: 0x1000001e
    SET_GPR_U32(ctx, 4, READ32(ADD32(GPR_U32(ctx, 16), 2136)));
    if (GPR_U32(ctx, 0) == GPR_U32(ctx, 0)) {
        ctx->pc = 0x211588; return;
    }
label_211514:
    // 0x211514: 0xc07e758
    SET_GPR_U32(ctx, 31, 0x21151c);
    DIntr(rdram, ctx, runtime); return;
}


// Function: entry_21151c
// Address: 0x21151c - 0x211554

void entry_21151c(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // 0x21151c: 0x8e440000
    SET_GPR_U32(ctx, 4, READ32(ADD32(GPR_U32(ctx, 18), 0)));
    // 0x211520: 0x3c021000
    SET_GPR_U32(ctx, 2, ((uint32_t)4096 << 16));
    // 0x211524: 0x3442b410
    SET_GPR_U32(ctx, 2, OR32(GPR_U32(ctx, 2), 46096));
    // 0x211528: 0x3c051000
    SET_GPR_U32(ctx, 5, ((uint32_t)4096 << 16));
    // 0x21152c: 0x912024
    SET_GPR_U32(ctx, 4, AND32(GPR_U32(ctx, 4), GPR_U32(ctx, 17)));
    // 0x211530: 0x34a5b420
    SET_GPR_U32(ctx, 5, OR32(GPR_U32(ctx, 5), 46112));
    // 0x211534: 0xac440000
    WRITE32(ADD32(GPR_U32(ctx, 2), 0), GPR_U32(ctx, 4));
    // 0x211538: 0x3c031000
    SET_GPR_U32(ctx, 3, ((uint32_t)4096 << 16));
    // 0x21153c: 0x3463b400
    SET_GPR_U32(ctx, 3, OR32(GPR_U32(ctx, 3), 46080));
    // 0x211540: 0x24040101
    SET_GPR_S32(ctx, 4, ADD32(GPR_U32(ctx, 0), 257));
    // 0x211544: 0x8fa20020
    SET_GPR_U32(ctx, 2, READ32(ADD32(GPR_U32(ctx, 29), 32)));
    // 0x211548: 0xaca20000
    WRITE32(ADD32(GPR_U32(ctx, 5), 0), GPR_U32(ctx, 2));
    // 0x21154c: 0xc07e76a
    SET_GPR_U32(ctx, 31, 0x211554);
    WRITE32(ADD32(GPR_U32(ctx, 3), 0), GPR_U32(ctx, 4));
    EIntr(rdram, ctx, runtime); return;
}


// Function: entry_211554
// Address: 0x211554 - 0x21156c

void entry_211554(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // 0x211554: 0x12a00007
    WRITE32(ADD32(GPR_U32(ctx, 29), 32), GPR_U32(ctx, 0));
    if (GPR_U32(ctx, 21) == GPR_U32(ctx, 0)) {
        ctx->pc = 0x211574; return;
    }
    // 0x21155c: 0x8e0500d8
    SET_GPR_U32(ctx, 5, READ32(ADD32(GPR_U32(ctx, 16), 216)));
    // 0x211560: 0x260302d
    SET_GPR_U64(ctx, 6, GPR_U64(ctx, 19) + GPR_U64(ctx, 0));
    // 0x211564: 0xc084380
    SET_GPR_U32(ctx, 31, 0x21156c);
    SET_GPR_U64(ctx, 4, GPR_U64(ctx, 16) + GPR_U64(ctx, 0));
    _doCSC(rdram, ctx, runtime); return;
}


// Function: entry_21156c
// Address: 0x21156c - 0x211584

void entry_21156c(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // Dispatch for mid-function entry points
    if (ctx->pc != 0 && ctx->pc != 0x21156c) {
        switch (ctx->pc) {
            case 0x211574: ctx->pc = 0; goto label_211574;
            default: break;
        }
        ctx->pc = 0; // Reset PC and continue from start
    }

    // 0x21156c: 0x10000006
    SET_GPR_U32(ctx, 4, READ32(ADD32(GPR_U32(ctx, 16), 2136)));
    if (GPR_U32(ctx, 0) == GPR_U32(ctx, 0)) {
        ctx->pc = 0x211588; return;
    }
label_211574:
    // 0x211574: 0x8e0500d8
    SET_GPR_U32(ctx, 5, READ32(ADD32(GPR_U32(ctx, 16), 216)));
    // 0x211578: 0x260302d
    SET_GPR_U64(ctx, 6, GPR_U64(ctx, 19) + GPR_U64(ctx, 0));
    // 0x21157c: 0xc084424
    SET_GPR_U32(ctx, 31, 0x211584);
    SET_GPR_U64(ctx, 4, GPR_U64(ctx, 16) + GPR_U64(ctx, 0));
    _doCSC2(rdram, ctx, runtime); return;
}


// Function: entry_211584
// Address: 0x211584 - 0x211598

void entry_211584(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // Dispatch for mid-function entry points
    if (ctx->pc != 0 && ctx->pc != 0x211584) {
        switch (ctx->pc) {
            case 0x211588: ctx->pc = 0; goto label_211588;
            default: break;
        }
        ctx->pc = 0; // Reset PC and continue from start
    }

    // 0x211584: 0x8e040858
    SET_GPR_U32(ctx, 4, READ32(ADD32(GPR_U32(ctx, 16), 2136)));
label_211588:
    // 0x211588: 0x24020003
    SET_GPR_S32(ctx, 2, ADD32(GPR_U32(ctx, 0), 3));
    // 0x21158c: 0xafa20000
    WRITE32(ADD32(GPR_U32(ctx, 29), 0), GPR_U32(ctx, 2));
    // 0x211590: 0xc082c64
    SET_GPR_U32(ctx, 31, 0x211598);
    SET_GPR_U64(ctx, 5, GPR_U64(ctx, 29) + GPR_U64(ctx, 0));
    _dispatchMpegCallback(rdram, ctx, runtime); return;
}


// Function: entry_211598
// Address: 0x211598 - 0x2115c0

void entry_211598(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // 0x211598: 0xdfbf0090
    SET_GPR_U64(ctx, 31, READ64(ADD32(GPR_U32(ctx, 29), 144)));
    // 0x21159c: 0xdfb50080
    SET_GPR_U64(ctx, 21, READ64(ADD32(GPR_U32(ctx, 29), 128)));
    // 0x2115a0: 0xdfb40070
    SET_GPR_U64(ctx, 20, READ64(ADD32(GPR_U32(ctx, 29), 112)));
    // 0x2115a4: 0xdfb30060
    SET_GPR_U64(ctx, 19, READ64(ADD32(GPR_U32(ctx, 29), 96)));
    // 0x2115a8: 0xdfb20050
    SET_GPR_U64(ctx, 18, READ64(ADD32(GPR_U32(ctx, 29), 80)));
    // 0x2115ac: 0xdfb10040
    SET_GPR_U64(ctx, 17, READ64(ADD32(GPR_U32(ctx, 29), 64)));
    // 0x2115b0: 0xdfb00030
    SET_GPR_U64(ctx, 16, READ64(ADD32(GPR_U32(ctx, 29), 48)));
    // 0x2115b4: 0x3e00008
    SET_GPR_S32(ctx, 29, ADD32(GPR_U32(ctx, 29), 160));
    ctx->pc = GPR_U32(ctx, 31); return;
    // 0x2115bc: 0x0
    // NOP
    // Fall-through to next function
    ctx->pc = 0x2115c0; return;
}


// Function: _sysbitInit
// Address: 0x2115c0 - 0x2115f8

void entry_2116d0(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // 0x2116d0: 0x40902d
    SET_GPR_U64(ctx, 18, GPR_U64(ctx, 2) + GPR_U64(ctx, 0));
    // 0x2116d4: 0x200202d
    SET_GPR_U64(ctx, 4, GPR_U64(ctx, 16) + GPR_U64(ctx, 0));
    // 0x2116d8: 0xc084586
    SET_GPR_U32(ctx, 31, 0x2116e0);
    SET_GPR_U64(ctx, 5, GPR_U64(ctx, 17) + GPR_U64(ctx, 0));
    _sysbitFlush(rdram, ctx, runtime); return;
}


// Function: entry_2116e0
// Address: 0x2116e0 - 0x211700

void entry_2116e0(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // 0x2116e0: 0x240102d
    SET_GPR_U64(ctx, 2, GPR_U64(ctx, 18) + GPR_U64(ctx, 0));
    // 0x2116e4: 0xdfbf0030
    SET_GPR_U64(ctx, 31, READ64(ADD32(GPR_U32(ctx, 29), 48)));
    // 0x2116e8: 0xdfb20020
    SET_GPR_U64(ctx, 18, READ64(ADD32(GPR_U32(ctx, 29), 32)));
    // 0x2116ec: 0xdfb10010
    SET_GPR_U64(ctx, 17, READ64(ADD32(GPR_U32(ctx, 29), 16)));
    // 0x2116f0: 0xdfb00000
    SET_GPR_U64(ctx, 16, READ64(ADD32(GPR_U32(ctx, 29), 0)));
    // 0x2116f4: 0x3e00008
    SET_GPR_S32(ctx, 29, ADD32(GPR_U32(ctx, 29), 64));
    ctx->pc = GPR_U32(ctx, 31); return;
    // 0x2116fc: 0x0
    // NOP
    // Fall-through to next function
    ctx->pc = 0x211700; return;
}


// Function: _sysbitMarker
// Address: 0x211700 - 0x21171c

void entry_21171c(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // 0x21171c: 0x40882d
    SET_GPR_U64(ctx, 17, GPR_U64(ctx, 2) + GPR_U64(ctx, 0));
    // 0x211720: 0x200202d
    SET_GPR_U64(ctx, 4, GPR_U64(ctx, 16) + GPR_U64(ctx, 0));
    // 0x211724: 0xc084586
    SET_GPR_U32(ctx, 31, 0x21172c);
    SET_GPR_S32(ctx, 5, ADD32(GPR_U32(ctx, 0), 1));
    _sysbitFlush(rdram, ctx, runtime); return;
}


// Function: entry_21172c
// Address: 0x21172c - 0x211748

void entry_21172c(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // 0x21172c: 0x220102d
    SET_GPR_U64(ctx, 2, GPR_U64(ctx, 17) + GPR_U64(ctx, 0));
    // 0x211730: 0xdfbf0020
    SET_GPR_U64(ctx, 31, READ64(ADD32(GPR_U32(ctx, 29), 32)));
    // 0x211734: 0xdfb10010
    SET_GPR_U64(ctx, 17, READ64(ADD32(GPR_U32(ctx, 29), 16)));
    // 0x211738: 0xdfb00000
    SET_GPR_U64(ctx, 16, READ64(ADD32(GPR_U32(ctx, 29), 0)));
    // 0x21173c: 0x3e00008
    SET_GPR_S32(ctx, 29, ADD32(GPR_U32(ctx, 29), 48));
    ctx->pc = GPR_U32(ctx, 31); return;
    // 0x211744: 0x0
    // NOP
    // Fall-through to next function
    ctx->pc = 0x211748; return;
}


// Function: _sysbitJump
// Address: 0x211748 - 0x2117a0

void entry_2117e4(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // 0x2117e4: 0x3c051000
    SET_GPR_U32(ctx, 5, ((uint32_t)4096 << 16));
    // 0x2117e8: 0x3c070001
    SET_GPR_U32(ctx, 7, ((uint32_t)1 << 16));
    // 0x2117ec: 0x34a5f520
    SET_GPR_U32(ctx, 5, OR32(GPR_U32(ctx, 5), 62752));
    // 0x2117f0: 0x3c061000
    SET_GPR_U32(ctx, 6, ((uint32_t)4096 << 16));
    // 0x2117f4: 0x8ca20000
    SET_GPR_U32(ctx, 2, READ32(ADD32(GPR_U32(ctx, 5), 0)));
    // 0x2117f8: 0x34c6f590
    SET_GPR_U32(ctx, 6, OR32(GPR_U32(ctx, 6), 62864));
    // 0x2117fc: 0x3c031000
    SET_GPR_U32(ctx, 3, ((uint32_t)4096 << 16));
    // 0x211800: 0x3c04fffe
    SET_GPR_U32(ctx, 4, ((uint32_t)65534 << 16));
    // 0x211804: 0x471025
    SET_GPR_U32(ctx, 2, OR32(GPR_U32(ctx, 2), GPR_U32(ctx, 7)));
    // 0x211808: 0x3463b000
    SET_GPR_U32(ctx, 3, OR32(GPR_U32(ctx, 3), 45056));
    // 0x21180c: 0xacc20000
    WRITE32(ADD32(GPR_U32(ctx, 6), 0), GPR_U32(ctx, 2));
    // 0x211810: 0x3484ffff
    SET_GPR_U32(ctx, 4, OR32(GPR_U32(ctx, 4), 65535));
    // 0x211814: 0xac700000
    WRITE32(ADD32(GPR_U32(ctx, 3), 0), GPR_U32(ctx, 16));
    // 0x211818: 0xdfbf0010
    SET_GPR_U64(ctx, 31, READ64(ADD32(GPR_U32(ctx, 29), 16)));
    // 0x21181c: 0x8ca20000
    SET_GPR_U32(ctx, 2, READ32(ADD32(GPR_U32(ctx, 5), 0)));
    // 0x211820: 0xdfb00000
    SET_GPR_U64(ctx, 16, READ64(ADD32(GPR_U32(ctx, 29), 0)));
    // 0x211824: 0x441024
    SET_GPR_U32(ctx, 2, AND32(GPR_U32(ctx, 2), GPR_U32(ctx, 4)));
    // 0x211828: 0xacc20000
    WRITE32(ADD32(GPR_U32(ctx, 6), 0), GPR_U32(ctx, 2));
    // 0x21182c: 0x807e76a
    SET_GPR_S32(ctx, 29, ADD32(GPR_U32(ctx, 29), 32));
    EIntr(rdram, ctx, runtime); return;
    // 0x211834: 0x0
    // NOP
    // Fall-through to next function
    ctx->pc = 0x211838; return;
}


// Function: setD4_CHCR
// Address: 0x211838 - 0x21184c

void entry_21184c(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // Dispatch for mid-function entry points
    if (ctx->pc != 0 && ctx->pc != 0x21184c) {
        switch (ctx->pc) {
            case 0x2118a0: ctx->pc = 0; goto label_2118a0;
            default: break;
        }
        ctx->pc = 0; // Reset PC and continue from start
    }

    // 0x21184c: 0x3c051000
    SET_GPR_U32(ctx, 5, ((uint32_t)4096 << 16));
    // 0x211850: 0x3c070001
    SET_GPR_U32(ctx, 7, ((uint32_t)1 << 16));
    // 0x211854: 0x34a5f520
    SET_GPR_U32(ctx, 5, OR32(GPR_U32(ctx, 5), 62752));
    // 0x211858: 0x3c061000
    SET_GPR_U32(ctx, 6, ((uint32_t)4096 << 16));
    // 0x21185c: 0x8ca20000
    SET_GPR_U32(ctx, 2, READ32(ADD32(GPR_U32(ctx, 5), 0)));
    // 0x211860: 0x34c6f590
    SET_GPR_U32(ctx, 6, OR32(GPR_U32(ctx, 6), 62864));
    // 0x211864: 0x3c031000
    SET_GPR_U32(ctx, 3, ((uint32_t)4096 << 16));
    // 0x211868: 0x3c04fffe
    SET_GPR_U32(ctx, 4, ((uint32_t)65534 << 16));
    // 0x21186c: 0x471025
    SET_GPR_U32(ctx, 2, OR32(GPR_U32(ctx, 2), GPR_U32(ctx, 7)));
    // 0x211870: 0x3463b400
    SET_GPR_U32(ctx, 3, OR32(GPR_U32(ctx, 3), 46080));
    // 0x211874: 0xacc20000
    WRITE32(ADD32(GPR_U32(ctx, 6), 0), GPR_U32(ctx, 2));
    // 0x211878: 0x3484ffff
    SET_GPR_U32(ctx, 4, OR32(GPR_U32(ctx, 4), 65535));
    // 0x21187c: 0xac700000
    WRITE32(ADD32(GPR_U32(ctx, 3), 0), GPR_U32(ctx, 16));
    // 0x211880: 0xdfbf0010
    SET_GPR_U64(ctx, 31, READ64(ADD32(GPR_U32(ctx, 29), 16)));
    // 0x211884: 0x8ca20000
    SET_GPR_U32(ctx, 2, READ32(ADD32(GPR_U32(ctx, 5), 0)));
    // 0x211888: 0xdfb00000
    SET_GPR_U64(ctx, 16, READ64(ADD32(GPR_U32(ctx, 29), 0)));
    // 0x21188c: 0x441024
    SET_GPR_U32(ctx, 2, AND32(GPR_U32(ctx, 2), GPR_U32(ctx, 4)));
    // 0x211890: 0xacc20000
    WRITE32(ADD32(GPR_U32(ctx, 6), 0), GPR_U32(ctx, 2));
    // 0x211894: 0x807e76a
    SET_GPR_S32(ctx, 29, ADD32(GPR_U32(ctx, 29), 32));
    EIntr(rdram, ctx, runtime); return;
    // 0x21189c: 0x0
    // NOP
label_2118a0:
    // 0x2118a0: 0x27bdffe0
    SET_GPR_S32(ctx, 29, ADD32(GPR_U32(ctx, 29), 4294967264));
    // 0x2118a4: 0xffb00000
    WRITE64(ADD32(GPR_U32(ctx, 29), 0), GPR_U64(ctx, 16));
    // 0x2118a8: 0x80802d
    SET_GPR_U64(ctx, 16, GPR_U64(ctx, 4) + GPR_U64(ctx, 0));
    // 0x2118ac: 0xffbf0010
    WRITE64(ADD32(GPR_U32(ctx, 29), 16), GPR_U64(ctx, 31));
    // 0x2118b0: 0xc08460e
    SET_GPR_U32(ctx, 31, 0x2118b8);
    SET_GPR_S32(ctx, 4, ADD32(GPR_U32(ctx, 0), 1));
    setD4_CHCR(rdram, ctx, runtime); return;
}


// Function: entry_2118b8
// Address: 0x2118b8 - 0x211924

void entry_2118b8(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // Dispatch for mid-function entry points
    if (ctx->pc != 0 && ctx->pc != 0x2118b8) {
        switch (ctx->pc) {
            case 0x211900: ctx->pc = 0; goto label_211900;
            default: break;
        }
        ctx->pc = 0; // Reset PC and continue from start
    }

    // 0x2118b8: 0x3c021000
    SET_GPR_U32(ctx, 2, ((uint32_t)4096 << 16));
    // 0x2118bc: 0x3c061000
    SET_GPR_U32(ctx, 6, ((uint32_t)4096 << 16));
    // 0x2118c0: 0x3442b410
    SET_GPR_U32(ctx, 2, OR32(GPR_U32(ctx, 2), 46096));
    // 0x2118c4: 0x34c6b430
    SET_GPR_U32(ctx, 6, OR32(GPR_U32(ctx, 6), 46128));
    // 0x2118c8: 0x8c430000
    SET_GPR_U32(ctx, 3, READ32(ADD32(GPR_U32(ctx, 2), 0)));
    // 0x2118cc: 0x3c041000
    SET_GPR_U32(ctx, 4, ((uint32_t)4096 << 16));
    // 0x2118d0: 0x3484b420
    SET_GPR_U32(ctx, 4, OR32(GPR_U32(ctx, 4), 46112));
    // 0x2118d4: 0x3c051000
    SET_GPR_U32(ctx, 5, ((uint32_t)4096 << 16));
    // 0x2118d8: 0xae030000
    WRITE32(ADD32(GPR_U32(ctx, 16), 0), GPR_U32(ctx, 3));
    // 0x2118dc: 0x34a5b400
    SET_GPR_U32(ctx, 5, OR32(GPR_U32(ctx, 5), 46080));
    // 0x2118e0: 0x3c071000
    SET_GPR_U32(ctx, 7, ((uint32_t)4096 << 16));
    // 0x2118e4: 0x8cc30000
    SET_GPR_U32(ctx, 3, READ32(ADD32(GPR_U32(ctx, 6), 0)));
    // 0x2118e8: 0x34e72010
    SET_GPR_U32(ctx, 7, OR32(GPR_U32(ctx, 7), 8208));
    // 0x2118ec: 0xae030004
    WRITE32(ADD32(GPR_U32(ctx, 16), 4), GPR_U32(ctx, 3));
    // 0x2118f0: 0x8c820000
    SET_GPR_U32(ctx, 2, READ32(ADD32(GPR_U32(ctx, 4), 0)));
    // 0x2118f4: 0xae020008
    WRITE32(ADD32(GPR_U32(ctx, 16), 8), GPR_U32(ctx, 2));
    // 0x2118f8: 0x8ca30000
    SET_GPR_U32(ctx, 3, READ32(ADD32(GPR_U32(ctx, 5), 0)));
    // 0x2118fc: 0xae03000c
    WRITE32(ADD32(GPR_U32(ctx, 16), 12), GPR_U32(ctx, 3));
label_211900:
    // 0x211900: 0x8ce20000
    SET_GPR_U32(ctx, 2, READ32(ADD32(GPR_U32(ctx, 7), 0)));
    // 0x211904: 0x304200f0
    SET_GPR_U32(ctx, 2, AND32(GPR_U32(ctx, 2), 240));
    // 0x211908: 0x0
    // NOP
    // 0x21190c: 0x0
    // NOP
    // 0x211910: 0x0
    // NOP
    // 0x211914: 0x1440fffa
    if (GPR_U32(ctx, 2) != GPR_U32(ctx, 0)) {
        goto label_211900;
    }
    // 0x21191c: 0xc0845f4
    SET_GPR_U32(ctx, 31, 0x211924);
    SET_GPR_U64(ctx, 4, GPR_U64(ctx, 0) + GPR_U64(ctx, 0));
    setD3_CHCR(rdram, ctx, runtime); return;
}


// Function: entry_211924
// Address: 0x211924 - 0x211988

void entry_211924(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // 0x211924: 0x3c021000
    SET_GPR_U32(ctx, 2, ((uint32_t)4096 << 16));
    // 0x211928: 0x3c071000
    SET_GPR_U32(ctx, 7, ((uint32_t)4096 << 16));
    // 0x21192c: 0x3442b010
    SET_GPR_U32(ctx, 2, OR32(GPR_U32(ctx, 2), 45072));
    // 0x211930: 0x34e7b020
    SET_GPR_U32(ctx, 7, OR32(GPR_U32(ctx, 7), 45088));
    // 0x211934: 0x8c430000
    SET_GPR_U32(ctx, 3, READ32(ADD32(GPR_U32(ctx, 2), 0)));
    // 0x211938: 0x3c051000
    SET_GPR_U32(ctx, 5, ((uint32_t)4096 << 16));
    // 0x21193c: 0x34a5b000
    SET_GPR_U32(ctx, 5, OR32(GPR_U32(ctx, 5), 45056));
    // 0x211940: 0x3c061000
    SET_GPR_U32(ctx, 6, ((uint32_t)4096 << 16));
    // 0x211944: 0xae030010
    WRITE32(ADD32(GPR_U32(ctx, 16), 16), GPR_U32(ctx, 3));
    // 0x211948: 0x34c62020
    SET_GPR_U32(ctx, 6, OR32(GPR_U32(ctx, 6), 8224));
    // 0x21194c: 0x3c041000
    SET_GPR_U32(ctx, 4, ((uint32_t)4096 << 16));
    // 0x211950: 0xdfbf0010
    SET_GPR_U64(ctx, 31, READ64(ADD32(GPR_U32(ctx, 29), 16)));
    // 0x211954: 0x8ce30000
    SET_GPR_U32(ctx, 3, READ32(ADD32(GPR_U32(ctx, 7), 0)));
    // 0x211958: 0x34842010
    SET_GPR_U32(ctx, 4, OR32(GPR_U32(ctx, 4), 8208));
    // 0x21195c: 0xae030014
    WRITE32(ADD32(GPR_U32(ctx, 16), 20), GPR_U32(ctx, 3));
    // 0x211960: 0x8ca20000
    SET_GPR_U32(ctx, 2, READ32(ADD32(GPR_U32(ctx, 5), 0)));
    // 0x211964: 0xae020018
    WRITE32(ADD32(GPR_U32(ctx, 16), 24), GPR_U32(ctx, 2));
    // 0x211968: 0x8cc30000
    SET_GPR_U32(ctx, 3, READ32(ADD32(GPR_U32(ctx, 6), 0)));
    // 0x21196c: 0xae03001c
    WRITE32(ADD32(GPR_U32(ctx, 16), 28), GPR_U32(ctx, 3));
    // 0x211970: 0x8c820000
    SET_GPR_U32(ctx, 2, READ32(ADD32(GPR_U32(ctx, 4), 0)));
    // 0x211974: 0xae020020
    WRITE32(ADD32(GPR_U32(ctx, 16), 32), GPR_U32(ctx, 2));
    // 0x211978: 0xdfb00000
    SET_GPR_U64(ctx, 16, READ64(ADD32(GPR_U32(ctx, 29), 0)));
    // 0x21197c: 0x3e00008
    SET_GPR_S32(ctx, 29, ADD32(GPR_U32(ctx, 29), 32));
    ctx->pc = GPR_U32(ctx, 31); return;
    // 0x211984: 0x0
    // NOP
    // Fall-through to next function
    ctx->pc = 0x211988; return;
}


// Function: sceIpuRestartDMA
// Address: 0x211988 - 0x211a0c

void entry_211a0c(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // Dispatch for mid-function entry points
    if (ctx->pc != 0 && ctx->pc != 0x211a0c) {
        switch (ctx->pc) {
            case 0x211a18: ctx->pc = 0; goto label_211a18;
            case 0x211a48: ctx->pc = 0; goto label_211a48;
            case 0x211ac0: ctx->pc = 0; goto label_211ac0;
            case 0x211ac4: ctx->pc = 0; goto label_211ac4;
            case 0x211ad8: ctx->pc = 0; goto label_211ad8;
            case 0x211af4: ctx->pc = 0; goto label_211af4;
            case 0x211b00: ctx->pc = 0; goto label_211b00;
            case 0x211b24: ctx->pc = 0; goto label_211b24;
            case 0x211b34: ctx->pc = 0; goto label_211b34;
            case 0x211b38: ctx->pc = 0; goto label_211b38;
            default: break;
        }
        ctx->pc = 0; // Reset PC and continue from start
    }

    // 0x211a0c: 0x3c031000
    SET_GPR_U32(ctx, 3, ((uint32_t)4096 << 16));
    // 0x211a10: 0x34632010
    SET_GPR_U32(ctx, 3, OR32(GPR_U32(ctx, 3), 8208));
    // 0x211a14: 0x0
    // NOP
label_211a18:
    // 0x211a18: 0x8c620000
    SET_GPR_U32(ctx, 2, READ32(ADD32(GPR_U32(ctx, 3), 0)));
    // 0x211a1c: 0x0
    // NOP
    // 0x211a20: 0x0
    // NOP
    // 0x211a24: 0x0
    // NOP
    // 0x211a28: 0x0
    // NOP
    // 0x211a2c: 0x440fffa
    if (GPR_S32(ctx, 2) < 0) {
        goto label_211a18;
    }
    // 0x211a34: 0x3c021000
    SET_GPR_U32(ctx, 2, ((uint32_t)4096 << 16));
    // 0x211a38: 0x3c031000
    SET_GPR_U32(ctx, 3, ((uint32_t)4096 << 16));
    // 0x211a3c: 0x34422000
    SET_GPR_U32(ctx, 2, OR32(GPR_U32(ctx, 2), 8192));
    // 0x211a40: 0x34632010
    SET_GPR_U32(ctx, 3, OR32(GPR_U32(ctx, 3), 8208));
    // 0x211a44: 0xac530000
    WRITE32(ADD32(GPR_U32(ctx, 2), 0), GPR_U32(ctx, 19));
label_211a48:
    // 0x211a48: 0x8c620000
    SET_GPR_U32(ctx, 2, READ32(ADD32(GPR_U32(ctx, 3), 0)));
    // 0x211a4c: 0x0
    // NOP
    // 0x211a50: 0x0
    // NOP
    // 0x211a54: 0x0
    // NOP
    // 0x211a58: 0x0
    // NOP
    // 0x211a5c: 0x440fffa
    if (GPR_S32(ctx, 2) < 0) {
        goto label_211a48;
    }
    // 0x211a64: 0x12200016
    SET_GPR_U64(ctx, 31, READ64(ADD32(GPR_U32(ctx, 29), 64)));
    if (GPR_U32(ctx, 17) == GPR_U32(ctx, 0)) {
        goto label_211ac0;
    }
    // 0x211a6c: 0x12400015
    SET_GPR_U64(ctx, 19, READ64(ADD32(GPR_U32(ctx, 29), 48)));
    if (GPR_U32(ctx, 18) == GPR_U32(ctx, 0)) {
        goto label_211ac4;
    }
    // 0x211a74: 0x3c021000
    SET_GPR_U32(ctx, 2, ((uint32_t)4096 << 16));
    // 0x211a78: 0x3c041000
    SET_GPR_U32(ctx, 4, ((uint32_t)4096 << 16));
    // 0x211a7c: 0x3442b410
    SET_GPR_U32(ctx, 2, OR32(GPR_U32(ctx, 2), 46096));
    // 0x211a80: 0x3484b430
    SET_GPR_U32(ctx, 4, OR32(GPR_U32(ctx, 4), 46128));
    // 0x211a84: 0xac510000
    WRITE32(ADD32(GPR_U32(ctx, 2), 0), GPR_U32(ctx, 17));
    // 0x211a88: 0x3c031000
    SET_GPR_U32(ctx, 3, ((uint32_t)4096 << 16));
    // 0x211a8c: 0x3463b420
    SET_GPR_U32(ctx, 3, OR32(GPR_U32(ctx, 3), 46112));
    // 0x211a90: 0xdfbf0040
    SET_GPR_U64(ctx, 31, READ64(ADD32(GPR_U32(ctx, 29), 64)));
    // 0x211a94: 0x8e020004
    SET_GPR_U32(ctx, 2, READ32(ADD32(GPR_U32(ctx, 16), 4)));
    // 0x211a98: 0xdfb30030
    SET_GPR_U64(ctx, 19, READ64(ADD32(GPR_U32(ctx, 29), 48)));
    // 0x211a9c: 0xac820000
    WRITE32(ADD32(GPR_U32(ctx, 4), 0), GPR_U32(ctx, 2));
    // 0x211aa0: 0xac720000
    WRITE32(ADD32(GPR_U32(ctx, 3), 0), GPR_U32(ctx, 18));
    // 0x211aa4: 0xdfb20020
    SET_GPR_U64(ctx, 18, READ64(ADD32(GPR_U32(ctx, 29), 32)));
    // 0x211aa8: 0x8e04000c
    SET_GPR_U32(ctx, 4, READ32(ADD32(GPR_U32(ctx, 16), 12)));
    // 0x211aac: 0xdfb10010
    SET_GPR_U64(ctx, 17, READ64(ADD32(GPR_U32(ctx, 29), 16)));
    // 0x211ab0: 0xdfb00000
    SET_GPR_U64(ctx, 16, READ64(ADD32(GPR_U32(ctx, 29), 0)));
    // 0x211ab4: 0x34840100
    SET_GPR_U32(ctx, 4, OR32(GPR_U32(ctx, 4), 256));
    // 0x211ab8: 0x808460e
    SET_GPR_S32(ctx, 29, ADD32(GPR_U32(ctx, 29), 80));
    setD4_CHCR(rdram, ctx, runtime); return;
label_211ac0:
    // 0x211ac0: 0xdfb30030
    SET_GPR_U64(ctx, 19, READ64(ADD32(GPR_U32(ctx, 29), 48)));
label_211ac4:
    // 0x211ac4: 0xdfb20020
    SET_GPR_U64(ctx, 18, READ64(ADD32(GPR_U32(ctx, 29), 32)));
    // 0x211ac8: 0xdfb10010
    SET_GPR_U64(ctx, 17, READ64(ADD32(GPR_U32(ctx, 29), 16)));
    // 0x211acc: 0xdfb00000
    SET_GPR_U64(ctx, 16, READ64(ADD32(GPR_U32(ctx, 29), 0)));
    // 0x211ad0: 0x3e00008
    SET_GPR_S32(ctx, 29, ADD32(GPR_U32(ctx, 29), 80));
    ctx->pc = GPR_U32(ctx, 31); return;
label_211ad8:
    // 0x211ad8: 0x10800006
    SET_GPR_U64(ctx, 3, GPR_U64(ctx, 0) + GPR_U64(ctx, 0));
    if (GPR_U32(ctx, 4) == GPR_U32(ctx, 0)) {
        goto label_211af4;
    }
    // 0x211ae0: 0x24020001
    SET_GPR_S32(ctx, 2, ADD32(GPR_U32(ctx, 0), 1));
    // 0x211ae4: 0x1082000f
    SET_GPR_U64(ctx, 2, GPR_U64(ctx, 3) + GPR_U64(ctx, 0));
    if (GPR_U32(ctx, 4) == GPR_U32(ctx, 2)) {
        goto label_211b24;
    }
    // 0x211aec: 0x10000012
    if (GPR_U32(ctx, 0) == GPR_U32(ctx, 0)) {
        goto label_211b38;
    }
label_211af4:
    // 0x211af4: 0x3c031000
    SET_GPR_U32(ctx, 3, ((uint32_t)4096 << 16));
    // 0x211af8: 0x34632010
    SET_GPR_U32(ctx, 3, OR32(GPR_U32(ctx, 3), 8208));
    // 0x211afc: 0x0
    // NOP
label_211b00:
    // 0x211b00: 0x8c620000
    SET_GPR_U32(ctx, 2, READ32(ADD32(GPR_U32(ctx, 3), 0)));
    // 0x211b04: 0x0
    // NOP
    // 0x211b08: 0x0
    // NOP
    // 0x211b0c: 0x0
    // NOP
    // 0x211b10: 0x0
    // NOP
    // 0x211b14: 0x440fffa
    if (GPR_S32(ctx, 2) < 0) {
        goto label_211b00;
    }
    // 0x211b1c: 0x10000005
    SET_GPR_U64(ctx, 3, GPR_U64(ctx, 0) + GPR_U64(ctx, 0));
    if (GPR_U32(ctx, 0) == GPR_U32(ctx, 0)) {
        goto label_211b34;
    }
label_211b24:
    // 0x211b24: 0x3c021000
    SET_GPR_U32(ctx, 2, ((uint32_t)4096 << 16));
    // 0x211b28: 0x34422010
    SET_GPR_U32(ctx, 2, OR32(GPR_U32(ctx, 2), 8208));
    // 0x211b2c: 0x8c430000
    SET_GPR_U32(ctx, 3, READ32(ADD32(GPR_U32(ctx, 2), 0)));
    // 0x211b30: 0x31fc2
    SET_GPR_U32(ctx, 3, SRL32(GPR_U32(ctx, 3), 31));
label_211b34:
    // 0x211b34: 0x60102d
    SET_GPR_U64(ctx, 2, GPR_U64(ctx, 3) + GPR_U64(ctx, 0));
label_211b38:
    // 0x211b38: 0x3e00008
    ctx->pc = GPR_U32(ctx, 31); return;
}


// Function: FUN_00211b40
// Address: 0x211b40 - 0x211b54

void FUN_00211b40(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // 0x211b40: 0x27bdffe0
    SET_GPR_S32(ctx, 29, ADD32(GPR_U32(ctx, 29), 4294967264));
    // 0x211b44: 0xffb00000
    WRITE64(ADD32(GPR_U32(ctx, 29), 0), GPR_U64(ctx, 16));
    // 0x211b48: 0xffbf0010
    WRITE64(ADD32(GPR_U32(ctx, 29), 16), GPR_U64(ctx, 31));
    // 0x211b4c: 0xc07e758
    SET_GPR_U32(ctx, 31, 0x211b54);
    SET_GPR_U64(ctx, 16, GPR_U64(ctx, 4) + GPR_U64(ctx, 0));
    DIntr(rdram, ctx, runtime); return;
}


// Function: entry_211b54
// Address: 0x211b54 - 0x211bb8

void entry_211b54(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // Dispatch for mid-function entry points
    if (ctx->pc != 0 && ctx->pc != 0x211b54) {
        switch (ctx->pc) {
            case 0x211ba8: ctx->pc = 0; goto label_211ba8;
            default: break;
        }
        ctx->pc = 0; // Reset PC and continue from start
    }

    // 0x211b54: 0x3c051000
    SET_GPR_U32(ctx, 5, ((uint32_t)4096 << 16));
    // 0x211b58: 0x3c070001
    SET_GPR_U32(ctx, 7, ((uint32_t)1 << 16));
    // 0x211b5c: 0x34a5f520
    SET_GPR_U32(ctx, 5, OR32(GPR_U32(ctx, 5), 62752));
    // 0x211b60: 0x3c061000
    SET_GPR_U32(ctx, 6, ((uint32_t)4096 << 16));
    // 0x211b64: 0x8ca20000
    SET_GPR_U32(ctx, 2, READ32(ADD32(GPR_U32(ctx, 5), 0)));
    // 0x211b68: 0x34c6f590
    SET_GPR_U32(ctx, 6, OR32(GPR_U32(ctx, 6), 62864));
    // 0x211b6c: 0x3c031000
    SET_GPR_U32(ctx, 3, ((uint32_t)4096 << 16));
    // 0x211b70: 0x3c04fffe
    SET_GPR_U32(ctx, 4, ((uint32_t)65534 << 16));
    // 0x211b74: 0x471025
    SET_GPR_U32(ctx, 2, OR32(GPR_U32(ctx, 2), GPR_U32(ctx, 7)));
    // 0x211b78: 0x3463b400
    SET_GPR_U32(ctx, 3, OR32(GPR_U32(ctx, 3), 46080));
    // 0x211b7c: 0xacc20000
    WRITE32(ADD32(GPR_U32(ctx, 6), 0), GPR_U32(ctx, 2));
    // 0x211b80: 0x3484ffff
    SET_GPR_U32(ctx, 4, OR32(GPR_U32(ctx, 4), 65535));
    // 0x211b84: 0xac700000
    WRITE32(ADD32(GPR_U32(ctx, 3), 0), GPR_U32(ctx, 16));
    // 0x211b88: 0xdfbf0010
    SET_GPR_U64(ctx, 31, READ64(ADD32(GPR_U32(ctx, 29), 16)));
    // 0x211b8c: 0x8ca20000
    SET_GPR_U32(ctx, 2, READ32(ADD32(GPR_U32(ctx, 5), 0)));
    // 0x211b90: 0xdfb00000
    SET_GPR_U64(ctx, 16, READ64(ADD32(GPR_U32(ctx, 29), 0)));
    // 0x211b94: 0x441024
    SET_GPR_U32(ctx, 2, AND32(GPR_U32(ctx, 2), GPR_U32(ctx, 4)));
    // 0x211b98: 0xacc20000
    WRITE32(ADD32(GPR_U32(ctx, 6), 0), GPR_U32(ctx, 2));
    // 0x211b9c: 0x807e76a
    SET_GPR_S32(ctx, 29, ADD32(GPR_U32(ctx, 29), 32));
    EIntr(rdram, ctx, runtime); return;
    // 0x211ba4: 0x0
    // NOP
label_211ba8:
    // 0x211ba8: 0x27bdfff0
    SET_GPR_S32(ctx, 29, ADD32(GPR_U32(ctx, 29), 4294967280));
    // 0x211bac: 0xffbf0000
    WRITE64(ADD32(GPR_U32(ctx, 29), 0), GPR_U64(ctx, 31));
    // 0x211bb0: 0xc0846d0
    SET_GPR_U32(ctx, 31, 0x211bb8);
    SET_GPR_S32(ctx, 4, ADD32(GPR_U32(ctx, 0), 1));
    FUN_00211b40(rdram, ctx, runtime); return;
}


// Function: entry_211bb8
// Address: 0x211bb8 - 0x211c40

void entry_211bb8(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {

    // Dispatch for mid-function entry points
    if (ctx->pc != 0 && ctx->pc != 0x211bb8) {
        switch (ctx->pc) {
            case 0x211bd0: ctx->pc = 0; goto label_211bd0;
            case 0x211c00: ctx->pc = 0; goto label_211c00;
            default: break;
        }
        ctx->pc = 0; // Reset PC and continue from start
    }

    // 0x211bb8: 0x3c021000
    SET_GPR_U32(ctx, 2, ((uint32_t)4096 << 16));
    // 0x211bbc: 0x3c034000
    SET_GPR_U32(ctx, 3, ((uint32_t)16384 << 16));
    // 0x211bc0: 0x34422010
    SET_GPR_U32(ctx, 2, OR32(GPR_U32(ctx, 2), 8208));
    // 0x211bc4: 0x3c041000
    SET_GPR_U32(ctx, 4, ((uint32_t)4096 << 16));
    // 0x211bc8: 0xac430000
    WRITE32(ADD32(GPR_U32(ctx, 2), 0), GPR_U32(ctx, 3));
    // 0x211bcc: 0x34842010
    SET_GPR_U32(ctx, 4, OR32(GPR_U32(ctx, 4), 8208));
label_211bd0:
    // 0x211bd0: 0x8c820000
    SET_GPR_U32(ctx, 2, READ32(ADD32(GPR_U32(ctx, 4), 0)));
    // 0x211bd4: 0x0
    // NOP
    // 0x211bd8: 0x0
    // NOP
    // 0x211bdc: 0x0
    // NOP
    // 0x211be0: 0x0
    // NOP
    // 0x211be4: 0x440fffa
    if (GPR_S32(ctx, 2) < 0) {
        goto label_211bd0;
    }
    // 0x211bec: 0x3c021000
    SET_GPR_U32(ctx, 2, ((uint32_t)4096 << 16));
    // 0x211bf0: 0x3c031000
    SET_GPR_U32(ctx, 3, ((uint32_t)4096 << 16));
    // 0x211bf4: 0x34422000
    SET_GPR_U32(ctx, 2, OR32(GPR_U32(ctx, 2), 8192));
    // 0x211bf8: 0x34632010
    SET_GPR_U32(ctx, 3, OR32(GPR_U32(ctx, 3), 8208));
    // 0x211bfc: 0xac400000
    WRITE32(ADD32(GPR_U32(ctx, 2), 0), GPR_U32(ctx, 0));
label_211c00:
    // 0x211c00: 0x8c620000
    SET_GPR_U32(ctx, 2, READ32(ADD32(GPR_U32(ctx, 3), 0)));
    // 0x211c04: 0x0
    // NOP
    // 0x211c08: 0x0
    // NOP
    // 0x211c0c: 0x0
    // NOP
    // 0x211c10: 0x0
    // NOP
    // 0x211c14: 0x440fffa
    if (GPR_S32(ctx, 2) < 0) {
        goto label_211c00;
    }
    // 0x211c1c: 0x3c050028
    SET_GPR_U32(ctx, 5, ((uint32_t)40 << 16));
    // 0x211c20: 0x3c041000
    SET_GPR_U32(ctx, 4, ((uint32_t)4096 << 16));
    // 0x211c24: 0x24a5b0c0
    SET_GPR_S32(ctx, 5, ADD32(GPR_U32(ctx, 5), 4294947008));
    // 0x211c28: 0x34847010
    SET_GPR_U32(ctx, 4, OR32(GPR_U32(ctx, 4), 28688));
    // 0x211c2c: 0x78a20000
    SET_GPR_VEC(ctx, 2, READ128(ADD32(GPR_U32(ctx, 5), 0)));
    // 0x211c30: 0x3c061000
    SET_GPR_U32(ctx, 6, ((uint32_t)4096 << 16));
    // 0x211c34: 0x3c075000
    SET_GPR_U32(ctx, 7, ((uint32_t)20480 << 16));
    // 0x211c38: 0x34c62000
    SET_GPR_U32(ctx, 6, OR32(GPR_U32(ctx, 6), 8192));
    // 0x211c3c: 0x7c820000
    WRITE128(ADD32(GPR_U32(ctx, 4), 0), GPR_VEC(ctx, 2));
    // Fall-through to next function
    ctx->pc = 0x211c40; return;
}



// ===== Mid-function entry points =====
// These handle jumps to addresses inside other functions

void entry_2100d8(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x2100d8 inside entry_2100cc (0x2100cc - 0x21012c)
    ctx->pc = 0x2100d8;
    entry_2100cc(rdram, ctx, runtime);
}

void entry_2100f8(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x2100f8 inside entry_2100cc (0x2100cc - 0x21012c)
    ctx->pc = 0x2100f8;
    entry_2100cc(rdram, ctx, runtime);
}

void entry_21010c(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x21010c inside entry_2100cc (0x2100cc - 0x21012c)
    ctx->pc = 0x21010c;
    entry_2100cc(rdram, ctx, runtime);
}

void entry_210114(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x210114 inside entry_2100cc (0x2100cc - 0x21012c)
    ctx->pc = 0x210114;
    entry_2100cc(rdram, ctx, runtime);
}

void entry_21011c(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x21011c inside entry_2100cc (0x2100cc - 0x21012c)
    ctx->pc = 0x21011c;
    entry_2100cc(rdram, ctx, runtime);
}

void entry_210120(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x210120 inside entry_2100cc (0x2100cc - 0x21012c)
    ctx->pc = 0x210120;
    entry_2100cc(rdram, ctx, runtime);
}

void entry_210144(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x210144 inside entry_210134 (0x210134 - 0x210160)
    ctx->pc = 0x210144;
    entry_210134(rdram, ctx, runtime);
}

void entry_210194(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x210194 inside _outputFrame (0x210160 - 0x2101a0)
    ctx->pc = 0x210194;
    _outputFrame(rdram, ctx, runtime);
}

void entry_2101a8(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x2101a8 inside entry_2101a0 (0x2101a0 - 0x2101cc)
    ctx->pc = 0x2101a8;
    entry_2101a0(rdram, ctx, runtime);
}

void entry_2101bc(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x2101bc inside entry_2101a0 (0x2101a0 - 0x2101cc)
    ctx->pc = 0x2101bc;
    entry_2101a0(rdram, ctx, runtime);
}

void entry_2101c0(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x2101c0 inside entry_2101a0 (0x2101a0 - 0x2101cc)
    ctx->pc = 0x2101c0;
    entry_2101a0(rdram, ctx, runtime);
}

void entry_2101d0(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x2101d0 inside entry_2101cc (0x2101cc - 0x2101f0)
    ctx->pc = 0x2101d0;
    entry_2101cc(rdram, ctx, runtime);
}

void entry_2101e4(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x2101e4 inside entry_2101cc (0x2101cc - 0x2101f0)
    ctx->pc = 0x2101e4;
    entry_2101cc(rdram, ctx, runtime);
}

void entry_210254(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x210254 inside _updateRefImage (0x2101f0 - 0x2104a8)
    ctx->pc = 0x210254;
    _updateRefImage(rdram, ctx, runtime);
}

void entry_210270(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x210270 inside _updateRefImage (0x2101f0 - 0x2104a8)
    ctx->pc = 0x210270;
    _updateRefImage(rdram, ctx, runtime);
}

void entry_210298(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x210298 inside _updateRefImage (0x2101f0 - 0x2104a8)
    ctx->pc = 0x210298;
    _updateRefImage(rdram, ctx, runtime);
}

void entry_2102cc(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x2102cc inside _updateRefImage (0x2101f0 - 0x2104a8)
    ctx->pc = 0x2102cc;
    _updateRefImage(rdram, ctx, runtime);
}

void entry_2102f4(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x2102f4 inside _updateRefImage (0x2101f0 - 0x2104a8)
    ctx->pc = 0x2102f4;
    _updateRefImage(rdram, ctx, runtime);
}

void entry_210300(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x210300 inside _updateRefImage (0x2101f0 - 0x2104a8)
    ctx->pc = 0x210300;
    _updateRefImage(rdram, ctx, runtime);
}

void entry_210314(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x210314 inside _updateRefImage (0x2101f0 - 0x2104a8)
    ctx->pc = 0x210314;
    _updateRefImage(rdram, ctx, runtime);
}

void entry_210328(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x210328 inside _updateRefImage (0x2101f0 - 0x2104a8)
    ctx->pc = 0x210328;
    _updateRefImage(rdram, ctx, runtime);
}

void entry_210364(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x210364 inside _updateRefImage (0x2101f0 - 0x2104a8)
    ctx->pc = 0x210364;
    _updateRefImage(rdram, ctx, runtime);
}

void entry_210390(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x210390 inside _updateRefImage (0x2101f0 - 0x2104a8)
    ctx->pc = 0x210390;
    _updateRefImage(rdram, ctx, runtime);
}

void entry_2103bc(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x2103bc inside _updateRefImage (0x2101f0 - 0x2104a8)
    ctx->pc = 0x2103bc;
    _updateRefImage(rdram, ctx, runtime);
}

void entry_2103cc(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x2103cc inside _updateRefImage (0x2101f0 - 0x2104a8)
    ctx->pc = 0x2103cc;
    _updateRefImage(rdram, ctx, runtime);
}

void entry_2103d8(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x2103d8 inside _updateRefImage (0x2101f0 - 0x2104a8)
    ctx->pc = 0x2103d8;
    _updateRefImage(rdram, ctx, runtime);
}

void entry_2103dc(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x2103dc inside _updateRefImage (0x2101f0 - 0x2104a8)
    ctx->pc = 0x2103dc;
    _updateRefImage(rdram, ctx, runtime);
}

void entry_2103e0(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x2103e0 inside _updateRefImage (0x2101f0 - 0x2104a8)
    ctx->pc = 0x2103e0;
    _updateRefImage(rdram, ctx, runtime);
}

void entry_210400(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x210400 inside _updateRefImage (0x2101f0 - 0x2104a8)
    ctx->pc = 0x210400;
    _updateRefImage(rdram, ctx, runtime);
}

void entry_210414(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x210414 inside _updateRefImage (0x2101f0 - 0x2104a8)
    ctx->pc = 0x210414;
    _updateRefImage(rdram, ctx, runtime);
}

void entry_210418(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x210418 inside _updateRefImage (0x2101f0 - 0x2104a8)
    ctx->pc = 0x210418;
    _updateRefImage(rdram, ctx, runtime);
}

void entry_21041c(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x21041c inside _updateRefImage (0x2101f0 - 0x2104a8)
    ctx->pc = 0x21041c;
    _updateRefImage(rdram, ctx, runtime);
}

void entry_2104ec(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x2104ec inside _isOutSizeOK (0x2104a8 - 0x210524)
    ctx->pc = 0x2104ec;
    _isOutSizeOK(rdram, ctx, runtime);
}

void entry_210504(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x210504 inside _isOutSizeOK (0x2104a8 - 0x210524)
    ctx->pc = 0x210504;
    _isOutSizeOK(rdram, ctx, runtime);
}

void entry_210534(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x210534 inside entry_210530 (0x210530 - 0x210548)
    ctx->pc = 0x210534;
    entry_210530(rdram, ctx, runtime);
}

void entry_2105bc(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x2105bc inside _cpr8 (0x210548 - 0x210648)
    ctx->pc = 0x2105bc;
    _cpr8(rdram, ctx, runtime);
}

void entry_2105c8(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x2105c8 inside _cpr8 (0x210548 - 0x210648)
    ctx->pc = 0x2105c8;
    _cpr8(rdram, ctx, runtime);
}

void entry_2105e4(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x2105e4 inside _cpr8 (0x210548 - 0x210648)
    ctx->pc = 0x2105e4;
    _cpr8(rdram, ctx, runtime);
}

void entry_2105e8(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x2105e8 inside _cpr8 (0x210548 - 0x210648)
    ctx->pc = 0x2105e8;
    _cpr8(rdram, ctx, runtime);
}

void entry_2105f4(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x2105f4 inside _cpr8 (0x210548 - 0x210648)
    ctx->pc = 0x2105f4;
    _cpr8(rdram, ctx, runtime);
}

void entry_21061c(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x21061c inside _cpr8 (0x210548 - 0x210648)
    ctx->pc = 0x21061c;
    _cpr8(rdram, ctx, runtime);
}

void entry_210630(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x210630 inside _cpr8 (0x210548 - 0x210648)
    ctx->pc = 0x210630;
    _cpr8(rdram, ctx, runtime);
}

void entry_210640(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x210640 inside _cpr8 (0x210548 - 0x210648)
    ctx->pc = 0x210640;
    _cpr8(rdram, ctx, runtime);
}

void entry_210690(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x210690 inside entry_210684 (0x210684 - 0x2106b4)
    ctx->pc = 0x210690;
    entry_210684(rdram, ctx, runtime);
}

void entry_2106f8(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x2106f8 inside entry_2106ec (0x2106ec - 0x2107e0)
    ctx->pc = 0x2106f8;
    entry_2106ec(rdram, ctx, runtime);
}

void entry_210720(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x210720 inside entry_2106ec (0x2106ec - 0x2107e0)
    ctx->pc = 0x210720;
    entry_2106ec(rdram, ctx, runtime);
}

void entry_210754(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x210754 inside entry_2106ec (0x2106ec - 0x2107e0)
    ctx->pc = 0x210754;
    entry_2106ec(rdram, ctx, runtime);
}

void entry_21075c(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x21075c inside entry_2106ec (0x2106ec - 0x2107e0)
    ctx->pc = 0x21075c;
    entry_2106ec(rdram, ctx, runtime);
}

void entry_210788(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x210788 inside entry_2106ec (0x2106ec - 0x2107e0)
    ctx->pc = 0x210788;
    entry_2106ec(rdram, ctx, runtime);
}

void entry_2107b8(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x2107b8 inside entry_2106ec (0x2106ec - 0x2107e0)
    ctx->pc = 0x2107b8;
    entry_2106ec(rdram, ctx, runtime);
}

void entry_2107d8(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x2107d8 inside entry_2106ec (0x2106ec - 0x2107e0)
    ctx->pc = 0x2107d8;
    entry_2106ec(rdram, ctx, runtime);
}

void entry_2108b8(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x2108b8 inside entry_2108a8 (0x2108a8 - 0x210970)
    ctx->pc = 0x2108b8;
    entry_2108a8(rdram, ctx, runtime);
}

void entry_2108c0(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x2108c0 inside entry_2108a8 (0x2108a8 - 0x210970)
    ctx->pc = 0x2108c0;
    entry_2108a8(rdram, ctx, runtime);
}

void entry_2108f0(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x2108f0 inside entry_2108a8 (0x2108a8 - 0x210970)
    ctx->pc = 0x2108f0;
    entry_2108a8(rdram, ctx, runtime);
}

void entry_210a4c(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x210a4c inside entry_210a44 (0x210a44 - 0x210a54)
    ctx->pc = 0x210a4c;
    entry_210a44(rdram, ctx, runtime);
}

void entry_210a58(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x210a58 inside entry_210a54 (0x210a54 - 0x210a80)
    ctx->pc = 0x210a58;
    entry_210a54(rdram, ctx, runtime);
}

void entry_210a6c(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x210a6c inside entry_210a54 (0x210a54 - 0x210a80)
    ctx->pc = 0x210a6c;
    entry_210a54(rdram, ctx, runtime);
}

void entry_210a70(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x210a70 inside entry_210a54 (0x210a54 - 0x210a80)
    ctx->pc = 0x210a70;
    entry_210a54(rdram, ctx, runtime);
}

void entry_210ad0(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x210ad0 inside _dispRefImageField (0x210a80 - 0x210af8)
    ctx->pc = 0x210ad0;
    _dispRefImageField(rdram, ctx, runtime);
}

void entry_210ad8(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x210ad8 inside _dispRefImageField (0x210a80 - 0x210af8)
    ctx->pc = 0x210ad8;
    _dispRefImageField(rdram, ctx, runtime);
}

void entry_210bd0(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x210bd0 inside entry_210bc8 (0x210bc8 - 0x210bd8)
    ctx->pc = 0x210bd0;
    entry_210bc8(rdram, ctx, runtime);
}

void entry_210bdc(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x210bdc inside entry_210bd8 (0x210bd8 - 0x210c38)
    ctx->pc = 0x210bdc;
    entry_210bd8(rdram, ctx, runtime);
}

void entry_210c10(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x210c10 inside entry_210bd8 (0x210bd8 - 0x210c38)
    ctx->pc = 0x210c10;
    entry_210bd8(rdram, ctx, runtime);
}

void entry_210c14(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x210c14 inside entry_210bd8 (0x210bd8 - 0x210c38)
    ctx->pc = 0x210c14;
    entry_210bd8(rdram, ctx, runtime);
}

void entry_210c18(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x210c18 inside entry_210bd8 (0x210bd8 - 0x210c38)
    ctx->pc = 0x210c18;
    entry_210bd8(rdram, ctx, runtime);
}

void entry_210c98(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x210c98 inside dmaRefImage (0x210c38 - 0x210d24)
    ctx->pc = 0x210c98;
    dmaRefImage(rdram, ctx, runtime);
}

void entry_210d18(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x210d18 inside dmaRefImage (0x210c38 - 0x210d24)
    ctx->pc = 0x210d18;
    dmaRefImage(rdram, ctx, runtime);
}

void entry_210d1c(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x210d1c inside dmaRefImage (0x210c38 - 0x210d24)
    ctx->pc = 0x210d1c;
    dmaRefImage(rdram, ctx, runtime);
}

void entry_210e30(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x210e30 inside _doCSC (0x210e00 - 0x210e54)
    ctx->pc = 0x210e30;
    _doCSC(rdram, ctx, runtime);
}

void entry_210eb8(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x210eb8 inside entry_210eb0 (0x210eb0 - 0x210f18)
    ctx->pc = 0x210eb8;
    entry_210eb0(rdram, ctx, runtime);
}

void entry_210ee0(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x210ee0 inside entry_210eb0 (0x210eb0 - 0x210f18)
    ctx->pc = 0x210ee0;
    entry_210eb0(rdram, ctx, runtime);
}

void entry_210f70(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x210f70 inside _ch3dmaCSC (0x210f18 - 0x210f9c)
    ctx->pc = 0x210f70;
    _ch3dmaCSC(rdram, ctx, runtime);
}

void entry_210f7c(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x210f7c inside _ch3dmaCSC (0x210f18 - 0x210f9c)
    ctx->pc = 0x210f7c;
    _ch3dmaCSC(rdram, ctx, runtime);
}

void entry_211000(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x211000 inside entry_210fc8 (0x210fc8 - 0x211028)
    ctx->pc = 0x211000;
    entry_210fc8(rdram, ctx, runtime);
}

void entry_211070(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x211070 inside entry_211058 (0x211058 - 0x211090)
    ctx->pc = 0x211070;
    entry_211058(rdram, ctx, runtime);
}

void entry_21107c(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x21107c inside entry_211058 (0x211058 - 0x211090)
    ctx->pc = 0x21107c;
    entry_211058(rdram, ctx, runtime);
}

void entry_2110d0(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x2110d0 inside _doCSC2 (0x211090 - 0x211130)
    ctx->pc = 0x2110d0;
    _doCSC2(rdram, ctx, runtime);
}

void entry_211100(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x211100 inside _doCSC2 (0x211090 - 0x211130)
    ctx->pc = 0x211100;
    _doCSC2(rdram, ctx, runtime);
}

void entry_2111c0(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x2111c0 inside entry_2111b8 (0x2111b8 - 0x2111f0)
    ctx->pc = 0x2111c0;
    entry_2111b8(rdram, ctx, runtime);
}

void entry_2111f8(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x2111f8 inside entry_2111f0 (0x2111f0 - 0x21121c)
    ctx->pc = 0x2111f8;
    entry_2111f0(rdram, ctx, runtime);
}

void entry_211278(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x211278 inside _ch4dma (0x211240 - 0x21128c)
    ctx->pc = 0x211278;
    _ch4dma(rdram, ctx, runtime);
}

void entry_2112ec(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x2112ec inside entry_2112bc (0x2112bc - 0x2112f4)
    ctx->pc = 0x2112ec;
    entry_2112bc(rdram, ctx, runtime);
}

void entry_21132c(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x21132c inside entry_211328 (0x211328 - 0x211348)
    ctx->pc = 0x21132c;
    entry_211328(rdram, ctx, runtime);
}

void entry_211330(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x211330 inside entry_211328 (0x211328 - 0x211348)
    ctx->pc = 0x211330;
    entry_211328(rdram, ctx, runtime);
}

void entry_2113b0(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x2113b0 inside entry_211390 (0x211390 - 0x2113e8)
    ctx->pc = 0x2113b0;
    entry_211390(rdram, ctx, runtime);
}

void entry_2113c0(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x2113c0 inside entry_211390 (0x211390 - 0x2113e8)
    ctx->pc = 0x2113c0;
    entry_211390(rdram, ctx, runtime);
}

void entry_2113f8(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x2113f8 inside entry_2113e8 (0x2113e8 - 0x211454)
    ctx->pc = 0x2113f8;
    entry_2113e8(rdram, ctx, runtime);
}

void entry_2114e8(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x2114e8 inside entry_2114e0 (0x2114e0 - 0x2114f8)
    ctx->pc = 0x2114e8;
    entry_2114e0(rdram, ctx, runtime);
}

void entry_211514(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x211514 inside entry_21150c (0x21150c - 0x21151c)
    ctx->pc = 0x211514;
    entry_21150c(rdram, ctx, runtime);
}

void entry_211574(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x211574 inside entry_21156c (0x21156c - 0x211584)
    ctx->pc = 0x211574;
    entry_21156c(rdram, ctx, runtime);
}

void entry_211588(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x211588 inside entry_211584 (0x211584 - 0x211598)
    ctx->pc = 0x211588;
    entry_211584(rdram, ctx, runtime);
}

void entry_211648(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x211648 inside _sysbitFlush (0x211618 - 0x2116b0)
    ctx->pc = 0x211648;
    _sysbitFlush(rdram, ctx, runtime);
}

void entry_211684(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x211684 inside _sysbitFlush (0x211618 - 0x2116b0)
    ctx->pc = 0x211684;
    _sysbitFlush(rdram, ctx, runtime);
}

void entry_21169c(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x21169c inside _sysbitFlush (0x211618 - 0x2116b0)
    ctx->pc = 0x21169c;
    _sysbitFlush(rdram, ctx, runtime);
}

void entry_2116a8(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x2116a8 inside _sysbitFlush (0x211618 - 0x2116b0)
    ctx->pc = 0x2116a8;
    _sysbitFlush(rdram, ctx, runtime);
}

void entry_211790(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x211790 inside _sysbitJump (0x211748 - 0x2117a0)
    ctx->pc = 0x211790;
    _sysbitJump(rdram, ctx, runtime);
}

void entry_2117c4(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x2117c4 inside _sysbitPtr (0x2117a0 - 0x2117d0)
    ctx->pc = 0x2117c4;
    _sysbitPtr(rdram, ctx, runtime);
}

void entry_2118a0(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x2118a0 inside entry_21184c (0x21184c - 0x2118b8)
    ctx->pc = 0x2118a0;
    entry_21184c(rdram, ctx, runtime);
}

void entry_211900(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x211900 inside entry_2118b8 (0x2118b8 - 0x211924)
    ctx->pc = 0x211900;
    entry_2118b8(rdram, ctx, runtime);
}

void entry_211a18(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x211a18 inside entry_211a0c (0x211a0c - 0x211b40)
    ctx->pc = 0x211a18;
    entry_211a0c(rdram, ctx, runtime);
}

void entry_211a48(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x211a48 inside entry_211a0c (0x211a0c - 0x211b40)
    ctx->pc = 0x211a48;
    entry_211a0c(rdram, ctx, runtime);
}

void entry_211ac0(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x211ac0 inside entry_211a0c (0x211a0c - 0x211b40)
    ctx->pc = 0x211ac0;
    entry_211a0c(rdram, ctx, runtime);
}

void entry_211ac4(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x211ac4 inside entry_211a0c (0x211a0c - 0x211b40)
    ctx->pc = 0x211ac4;
    entry_211a0c(rdram, ctx, runtime);
}

void entry_211ad8(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x211ad8 inside entry_211a0c (0x211a0c - 0x211b40)
    ctx->pc = 0x211ad8;
    entry_211a0c(rdram, ctx, runtime);
}

void entry_211af4(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x211af4 inside entry_211a0c (0x211a0c - 0x211b40)
    ctx->pc = 0x211af4;
    entry_211a0c(rdram, ctx, runtime);
}

void entry_211b00(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x211b00 inside entry_211a0c (0x211a0c - 0x211b40)
    ctx->pc = 0x211b00;
    entry_211a0c(rdram, ctx, runtime);
}

void entry_211b24(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x211b24 inside entry_211a0c (0x211a0c - 0x211b40)
    ctx->pc = 0x211b24;
    entry_211a0c(rdram, ctx, runtime);
}

void entry_211b34(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x211b34 inside entry_211a0c (0x211a0c - 0x211b40)
    ctx->pc = 0x211b34;
    entry_211a0c(rdram, ctx, runtime);
}

void entry_211b38(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x211b38 inside entry_211a0c (0x211a0c - 0x211b40)
    ctx->pc = 0x211b38;
    entry_211a0c(rdram, ctx, runtime);
}

void entry_211ba8(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x211ba8 inside entry_211b54 (0x211b54 - 0x211bb8)
    ctx->pc = 0x211ba8;
    entry_211b54(rdram, ctx, runtime);
}

void entry_211bd0(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x211bd0 inside entry_211bb8 (0x211bb8 - 0x211c40)
    ctx->pc = 0x211bd0;
    entry_211bb8(rdram, ctx, runtime);
}

void entry_211c00(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    // Mid-function entry at 0x211c00 inside entry_211bb8 (0x211bb8 - 0x211c40)
    ctx->pc = 0x211c00;
    entry_211bb8(rdram, ctx, runtime);
}

