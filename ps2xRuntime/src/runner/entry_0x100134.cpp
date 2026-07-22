#include <stdexcept>
#include "ps2_runtime_macros.h"
#include "ps2_runtime.h"
#include "ps2_recompiled_functions.h"
#include "ps2_recompiled_stubs.h"

#include "ps2_syscalls.h"
#include "ps2_stubs.h"

#ifdef PS2_FUNCTION_LOG_TRACKER
#include "ps2_log.h"
#endif
#include <iostream>

// Function: entry
// Address: 0x100134 - 0x100374
void entry_0x100134(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
#ifdef PS2_FUNCTION_LOG_TRACKER
    PS_LOG_ENTRY("entry_0x100134");
#endif

    std::cerr << "[DIAG:entry] pc=0x" << std::hex << ctx->pc << " sp=0x" << GPR_U32(ctx, 29) << " ra=0x" << GPR_U32(ctx, 31) << std::dec << std::endl;

    switch (ctx->pc) {
        case 0x100134u: goto label_100134;
        case 0x100138u: goto label_100138;
        case 0x10013cu: goto label_10013c;
        case 0x100140u: goto label_100140;
        case 0x100144u: goto label_100144;
        case 0x100148u: goto label_100148;
        case 0x10014cu: goto label_10014c;
        case 0x100150u: goto label_100150;
        case 0x100154u: goto label_100154;
        case 0x100158u: goto label_100158;
        case 0x10015cu: goto label_10015c;
        case 0x100160u: goto label_100160;
        case 0x100164u: goto label_100164;
        case 0x100168u: goto label_100168;
        case 0x10016cu: goto label_10016c;
        case 0x100170u: goto label_100170;
        case 0x100174u: goto label_100174;
        case 0x100178u: goto label_100178;
        case 0x10017cu: goto label_10017c;
        case 0x100180u: goto label_100180;
        case 0x100184u: goto label_100184;
        case 0x100188u: goto label_100188;
        case 0x10018cu: goto label_10018c;
        case 0x100190u: goto label_100190;
        case 0x100194u: goto label_100194;
        case 0x100198u: goto label_100198;
        case 0x10019cu: goto label_10019c;
        case 0x1001a0u: goto label_1001a0;
        case 0x1001a4u: goto label_1001a4;
        case 0x1001a8u: goto label_1001a8;
        case 0x1001acu: goto label_1001ac;
        case 0x1001b0u: goto label_1001b0;
        case 0x1001b4u: goto label_1001b4;
        case 0x1001b8u: goto label_1001b8;
        case 0x1001bcu: goto label_1001bc;
        case 0x1001c0u: goto label_1001c0;
        case 0x1001c4u: goto label_1001c4;
        case 0x1001c8u: goto label_1001c8;
        case 0x1001ccu: goto label_1001cc;
        case 0x1001d0u: goto label_1001d0;
        case 0x1001d4u: goto label_1001d4;
        case 0x1001d8u: goto label_1001d8;
        case 0x1001dcu: goto label_1001dc;
        case 0x1001e0u: goto label_1001e0;
        case 0x1001e4u: goto label_1001e4;
        case 0x1001e8u: goto label_1001e8;
        case 0x1001ecu: goto label_1001ec;
        case 0x1001f0u: goto label_1001f0;
        case 0x1001f4u: goto label_1001f4;
        case 0x1001f8u: goto label_1001f8;
        case 0x1001fcu: goto label_1001fc;
        case 0x100200u: goto label_100200;
        case 0x100204u: goto label_100204;
        case 0x100208u: goto label_100208;
        case 0x10020cu: goto label_10020c;
        case 0x100210u: goto label_100210;
        case 0x100214u: goto label_100214;
        case 0x100218u: goto label_100218;
        case 0x10021cu: goto label_10021c;
        case 0x100220u: goto label_100220;
        case 0x100224u: goto label_100224;
        case 0x100228u: goto label_100228;
        case 0x10022cu: goto label_10022c;
        case 0x100230u: goto label_100230;
        case 0x100234u: goto label_100234;
        case 0x100238u: goto label_100238;
        case 0x10023cu: goto label_10023c;
        case 0x100240u: goto label_100240;
        case 0x100244u: goto label_100244;
        case 0x100248u: goto label_100248;
        case 0x10024cu: goto label_10024c;
        case 0x100250u: goto label_100250;
        case 0x100254u: goto label_100254;
        case 0x100258u: goto label_100258;
        case 0x10025cu: goto label_10025c;
        case 0x100260u: goto label_100260;
        case 0x100264u: goto label_100264;
        case 0x100268u: goto label_100268;
        case 0x10026cu: goto label_10026c;
        case 0x100270u: goto label_100270;
        case 0x100274u: goto label_100274;
        case 0x100278u: goto label_100278;
        case 0x10027cu: goto label_10027c;
        case 0x100280u: goto label_100280;
        case 0x100284u: goto label_100284;
        case 0x100288u: goto label_100288;
        case 0x10028cu: goto label_10028c;
        case 0x100290u: goto label_100290;
        case 0x100294u: goto label_100294;
        case 0x100298u: goto label_100298;
        case 0x10029cu: goto label_10029c;
        case 0x1002a0u: goto label_1002a0;
        case 0x1002a4u: goto label_1002a4;
        case 0x1002a8u: goto label_1002a8;
        case 0x1002acu: goto label_1002ac;
        case 0x1002b0u: goto label_1002b0;
        case 0x1002b4u: goto label_1002b4;
        case 0x1002b8u: goto label_1002b8;
        case 0x1002bcu: goto label_1002bc;
        case 0x1002c0u: goto label_1002c0;
        case 0x1002c4u: goto label_1002c4;
        case 0x1002c8u: goto label_1002c8;
        case 0x1002ccu: goto label_1002cc;
        case 0x1002d0u: goto label_1002d0;
        case 0x1002d4u: goto label_1002d4;
        case 0x1002d8u: goto label_1002d8;
        case 0x1002dcu: goto label_1002dc;
        case 0x1002e0u: goto label_1002e0;
        case 0x1002e4u: goto label_1002e4;
        case 0x1002e8u: goto label_1002e8;
        case 0x1002ecu: goto label_1002ec;
        case 0x1002f0u: goto label_1002f0;
        case 0x1002f4u: goto label_1002f4;
        case 0x1002f8u: goto label_1002f8;
        case 0x1002fcu: goto label_1002fc;
        case 0x100300u: goto label_100300;
        case 0x100304u: goto label_100304;
        case 0x100308u: goto label_100308;
        case 0x10030cu: goto label_10030c;
        case 0x100310u: goto label_100310;
        case 0x100314u: goto label_100314;
        case 0x100318u: goto label_100318;
        case 0x10031cu: goto label_10031c;
        case 0x100320u: goto label_100320;
        case 0x100324u: goto label_100324;
        case 0x100328u: goto label_100328;
        case 0x10032cu: goto label_10032c;
        case 0x100330u: goto label_100330;
        case 0x100334u: goto label_100334;
        case 0x100338u: goto label_100338;
        case 0x10033cu: goto label_10033c;
        case 0x100340u: goto label_100340;
        case 0x100344u: goto label_100344;
        case 0x100348u: goto label_100348;
        case 0x10034cu: goto label_10034c;
        case 0x100350u: goto label_100350;
        case 0x100354u: goto label_100354;
        case 0x100358u: goto label_100358;
        case 0x10035cu: goto label_10035c;
        case 0x100360u: goto label_100360;
        case 0x100364u: goto label_100364;
        case 0x100368u: goto label_100368;
        case 0x10036cu: goto label_10036c;
        case 0x100370u: goto label_100370;
        default: break;
    }

    ctx->pc = 0x100134u;

label_100134:
    // 0x100134: 0x70000c28  padduw      $at, $zero, $zero
    ctx->pc = 0x100134u;
    SET_GPR_VEC(ctx, 1, ps2_paddu32(GPR_VEC(ctx, 0), GPR_VEC(ctx, 0)));
label_100138:
    // 0x100138: 0x70001428  padduw      $v0, $zero, $zero
    ctx->pc = 0x100138u;
    SET_GPR_VEC(ctx, 2, ps2_paddu32(GPR_VEC(ctx, 0), GPR_VEC(ctx, 0)));
label_10013c:
    // 0x10013c: 0x70001c28  padduw      $v1, $zero, $zero
    ctx->pc = 0x10013cu;
    SET_GPR_VEC(ctx, 3, ps2_paddu32(GPR_VEC(ctx, 0), GPR_VEC(ctx, 0)));
label_100140:
    // 0x100140: 0x70002428  padduw      $a0, $zero, $zero
    ctx->pc = 0x100140u;
    SET_GPR_VEC(ctx, 4, ps2_paddu32(GPR_VEC(ctx, 0), GPR_VEC(ctx, 0)));
label_100144:
    // 0x100144: 0x70002c28  padduw      $a1, $zero, $zero
    ctx->pc = 0x100144u;
    SET_GPR_VEC(ctx, 5, ps2_paddu32(GPR_VEC(ctx, 0), GPR_VEC(ctx, 0)));
label_100148:
    // 0x100148: 0x70003428  padduw      $a2, $zero, $zero
    ctx->pc = 0x100148u;
    SET_GPR_VEC(ctx, 6, ps2_paddu32(GPR_VEC(ctx, 0), GPR_VEC(ctx, 0)));
label_10014c:
    // 0x10014c: 0x70003c28  padduw      $a3, $zero, $zero
    ctx->pc = 0x10014cu;
    SET_GPR_VEC(ctx, 7, ps2_paddu32(GPR_VEC(ctx, 0), GPR_VEC(ctx, 0)));
label_100150:
    // 0x100150: 0x70004428  padduw      $t0, $zero, $zero
    ctx->pc = 0x100150u;
    SET_GPR_VEC(ctx, 8, ps2_paddu32(GPR_VEC(ctx, 0), GPR_VEC(ctx, 0)));
label_100154:
    // 0x100154: 0x70004c28  padduw      $t1, $zero, $zero
    ctx->pc = 0x100154u;
    SET_GPR_VEC(ctx, 9, ps2_paddu32(GPR_VEC(ctx, 0), GPR_VEC(ctx, 0)));
label_100158:
    // 0x100158: 0x70005428  padduw      $t2, $zero, $zero
    ctx->pc = 0x100158u;
    SET_GPR_VEC(ctx, 10, ps2_paddu32(GPR_VEC(ctx, 0), GPR_VEC(ctx, 0)));
label_10015c:
    // 0x10015c: 0x70005c28  padduw      $t3, $zero, $zero
    ctx->pc = 0x10015cu;
    SET_GPR_VEC(ctx, 11, ps2_paddu32(GPR_VEC(ctx, 0), GPR_VEC(ctx, 0)));
label_100160:
    // 0x100160: 0x70006428  padduw      $t4, $zero, $zero
    ctx->pc = 0x100160u;
    SET_GPR_VEC(ctx, 12, ps2_paddu32(GPR_VEC(ctx, 0), GPR_VEC(ctx, 0)));
label_100164:
    // 0x100164: 0x70006c28  padduw      $t5, $zero, $zero
    ctx->pc = 0x100164u;
    SET_GPR_VEC(ctx, 13, ps2_paddu32(GPR_VEC(ctx, 0), GPR_VEC(ctx, 0)));
label_100168:
    // 0x100168: 0x70007428  padduw      $t6, $zero, $zero
    ctx->pc = 0x100168u;
    SET_GPR_VEC(ctx, 14, ps2_paddu32(GPR_VEC(ctx, 0), GPR_VEC(ctx, 0)));
label_10016c:
    // 0x10016c: 0x70007c28  padduw      $t7, $zero, $zero
    ctx->pc = 0x10016cu;
    SET_GPR_VEC(ctx, 15, ps2_paddu32(GPR_VEC(ctx, 0), GPR_VEC(ctx, 0)));
label_100170:
    // 0x100170: 0x70008428  padduw      $s0, $zero, $zero
    ctx->pc = 0x100170u;
    SET_GPR_VEC(ctx, 16, ps2_paddu32(GPR_VEC(ctx, 0), GPR_VEC(ctx, 0)));
label_100174:
    // 0x100174: 0x70008c28  padduw      $s1, $zero, $zero
    ctx->pc = 0x100174u;
    SET_GPR_VEC(ctx, 17, ps2_paddu32(GPR_VEC(ctx, 0), GPR_VEC(ctx, 0)));
label_100178:
    // 0x100178: 0x70009428  padduw      $s2, $zero, $zero
    ctx->pc = 0x100178u;
    SET_GPR_VEC(ctx, 18, ps2_paddu32(GPR_VEC(ctx, 0), GPR_VEC(ctx, 0)));
label_10017c:
    // 0x10017c: 0x70009c28  padduw      $s3, $zero, $zero
    ctx->pc = 0x10017cu;
    SET_GPR_VEC(ctx, 19, ps2_paddu32(GPR_VEC(ctx, 0), GPR_VEC(ctx, 0)));
label_100180:
    // 0x100180: 0x7000a428  padduw      $s4, $zero, $zero
    ctx->pc = 0x100180u;
    SET_GPR_VEC(ctx, 20, ps2_paddu32(GPR_VEC(ctx, 0), GPR_VEC(ctx, 0)));
label_100184:
    // 0x100184: 0x7000ac28  padduw      $s5, $zero, $zero
    ctx->pc = 0x100184u;
    SET_GPR_VEC(ctx, 21, ps2_paddu32(GPR_VEC(ctx, 0), GPR_VEC(ctx, 0)));
label_100188:
    // 0x100188: 0x7000b428  padduw      $s6, $zero, $zero
    ctx->pc = 0x100188u;
    SET_GPR_VEC(ctx, 22, ps2_paddu32(GPR_VEC(ctx, 0), GPR_VEC(ctx, 0)));
label_10018c:
    // 0x10018c: 0x7000bc28  padduw      $s7, $zero, $zero
    ctx->pc = 0x10018cu;
    SET_GPR_VEC(ctx, 23, ps2_paddu32(GPR_VEC(ctx, 0), GPR_VEC(ctx, 0)));
label_100190:
    // 0x100190: 0x7000c428  padduw      $t8, $zero, $zero
    ctx->pc = 0x100190u;
    SET_GPR_VEC(ctx, 24, ps2_paddu32(GPR_VEC(ctx, 0), GPR_VEC(ctx, 0)));
label_100194:
    // 0x100194: 0x7000cc28  padduw      $t9, $zero, $zero
    ctx->pc = 0x100194u;
    SET_GPR_VEC(ctx, 25, ps2_paddu32(GPR_VEC(ctx, 0), GPR_VEC(ctx, 0)));
label_100198:
    // 0x100198: 0x7000e428  padduw      $gp, $zero, $zero
    ctx->pc = 0x100198u;
    SET_GPR_VEC(ctx, 28, ps2_paddu32(GPR_VEC(ctx, 0), GPR_VEC(ctx, 0)));
label_10019c:
    // 0x10019c: 0x7000ec28  padduw      $sp, $zero, $zero
    ctx->pc = 0x10019cu;
    SET_GPR_VEC(ctx, 29, ps2_paddu32(GPR_VEC(ctx, 0), GPR_VEC(ctx, 0)));
label_1001a0:
    // 0x1001a0: 0x7000f428  padduw      $fp, $zero, $zero
    ctx->pc = 0x1001a0u;
    SET_GPR_VEC(ctx, 30, ps2_paddu32(GPR_VEC(ctx, 0), GPR_VEC(ctx, 0)));
label_1001a4:
    // 0x1001a4: 0x7000fc28  padduw      $ra, $zero, $zero
    ctx->pc = 0x1001a4u;
    SET_GPR_VEC(ctx, 31, ps2_paddu32(GPR_VEC(ctx, 0), GPR_VEC(ctx, 0)));
label_1001a8:
    // 0x1001a8: 0x11  mthi        $zero
    ctx->pc = 0x1001a8u;
    ctx->hi = GPR_U64(ctx, 0);
label_1001ac:
    // 0x1001ac: 0x70000011  mthi1       $zero
    ctx->pc = 0x1001acu;
    ctx->hi1 = GPR_U64(ctx, 0);
label_1001b0:
    // 0x1001b0: 0x13  mtlo        $zero
    ctx->pc = 0x1001b0u;
    ctx->lo = GPR_U64(ctx, 0);
label_1001b4:
    // 0x1001b4: 0x70000013  mtlo1       $zero
    ctx->pc = 0x1001b4u;
    ctx->lo1 = GPR_U64(ctx, 0);
label_1001b8:
    // 0x1001b8: 0x4190000  mtsah       $zero, 0x0
    ctx->pc = 0x1001b8u;
    ctx->sa = ((GPR_U32(ctx, 0) ^ (uint32_t)0) & 0x7) << 4;
label_1001bc:
    // 0x1001bc: 0x44800000  mtc1        $zero, $f0
    ctx->pc = 0x1001bcu;
    { uint32_t bits = GPR_U32(ctx, 0); std::memcpy(&ctx->f[0], &bits, sizeof(bits)); }
label_1001c0:
    // 0x1001c0: 0x44800800  mtc1        $zero, $f1
    ctx->pc = 0x1001c0u;
    { uint32_t bits = GPR_U32(ctx, 0); std::memcpy(&ctx->f[1], &bits, sizeof(bits)); }
label_1001c4:
    // 0x1001c4: 0x44801000  mtc1        $zero, $f2
    ctx->pc = 0x1001c4u;
    { uint32_t bits = GPR_U32(ctx, 0); std::memcpy(&ctx->f[2], &bits, sizeof(bits)); }
label_1001c8:
    // 0x1001c8: 0x44801800  mtc1        $zero, $f3
    ctx->pc = 0x1001c8u;
    { uint32_t bits = GPR_U32(ctx, 0); std::memcpy(&ctx->f[3], &bits, sizeof(bits)); }
label_1001cc:
    // 0x1001cc: 0x44802000  mtc1        $zero, $f4
    ctx->pc = 0x1001ccu;
    { uint32_t bits = GPR_U32(ctx, 0); std::memcpy(&ctx->f[4], &bits, sizeof(bits)); }
label_1001d0:
    // 0x1001d0: 0x44802800  mtc1        $zero, $f5
    ctx->pc = 0x1001d0u;
    { uint32_t bits = GPR_U32(ctx, 0); std::memcpy(&ctx->f[5], &bits, sizeof(bits)); }
label_1001d4:
    // 0x1001d4: 0x44803000  mtc1        $zero, $f6
    ctx->pc = 0x1001d4u;
    { uint32_t bits = GPR_U32(ctx, 0); std::memcpy(&ctx->f[6], &bits, sizeof(bits)); }
label_1001d8:
    // 0x1001d8: 0x44803800  mtc1        $zero, $f7
    ctx->pc = 0x1001d8u;
    { uint32_t bits = GPR_U32(ctx, 0); std::memcpy(&ctx->f[7], &bits, sizeof(bits)); }
label_1001dc:
    // 0x1001dc: 0x44804000  mtc1        $zero, $f8
    ctx->pc = 0x1001dcu;
    { uint32_t bits = GPR_U32(ctx, 0); std::memcpy(&ctx->f[8], &bits, sizeof(bits)); }
label_1001e0:
    // 0x1001e0: 0x44804800  mtc1        $zero, $f9
    ctx->pc = 0x1001e0u;
    { uint32_t bits = GPR_U32(ctx, 0); std::memcpy(&ctx->f[9], &bits, sizeof(bits)); }
label_1001e4:
    // 0x1001e4: 0x44805000  mtc1        $zero, $f10
    ctx->pc = 0x1001e4u;
    { uint32_t bits = GPR_U32(ctx, 0); std::memcpy(&ctx->f[10], &bits, sizeof(bits)); }
label_1001e8:
    // 0x1001e8: 0x44805800  mtc1        $zero, $f11
    ctx->pc = 0x1001e8u;
    { uint32_t bits = GPR_U32(ctx, 0); std::memcpy(&ctx->f[11], &bits, sizeof(bits)); }
label_1001ec:
    // 0x1001ec: 0x44806000  mtc1        $zero, $f12
    ctx->pc = 0x1001ecu;
    { uint32_t bits = GPR_U32(ctx, 0); std::memcpy(&ctx->f[12], &bits, sizeof(bits)); }
label_1001f0:
    // 0x1001f0: 0x44806800  mtc1        $zero, $f13
    ctx->pc = 0x1001f0u;
    { uint32_t bits = GPR_U32(ctx, 0); std::memcpy(&ctx->f[13], &bits, sizeof(bits)); }
label_1001f4:
    // 0x1001f4: 0x44807000  mtc1        $zero, $f14
    ctx->pc = 0x1001f4u;
    { uint32_t bits = GPR_U32(ctx, 0); std::memcpy(&ctx->f[14], &bits, sizeof(bits)); }
label_1001f8:
    // 0x1001f8: 0x44807800  mtc1        $zero, $f15
    ctx->pc = 0x1001f8u;
    { uint32_t bits = GPR_U32(ctx, 0); std::memcpy(&ctx->f[15], &bits, sizeof(bits)); }
label_1001fc:
    // 0x1001fc: 0x44808000  mtc1        $zero, $f16
    ctx->pc = 0x1001fcu;
    { uint32_t bits = GPR_U32(ctx, 0); std::memcpy(&ctx->f[16], &bits, sizeof(bits)); }
label_100200:
    // 0x100200: 0x44808800  mtc1        $zero, $f17
    ctx->pc = 0x100200u;
    { uint32_t bits = GPR_U32(ctx, 0); std::memcpy(&ctx->f[17], &bits, sizeof(bits)); }
label_100204:
    // 0x100204: 0x44809000  mtc1        $zero, $f18
    ctx->pc = 0x100204u;
    { uint32_t bits = GPR_U32(ctx, 0); std::memcpy(&ctx->f[18], &bits, sizeof(bits)); }
label_100208:
    // 0x100208: 0x44809800  mtc1        $zero, $f19
    ctx->pc = 0x100208u;
    { uint32_t bits = GPR_U32(ctx, 0); std::memcpy(&ctx->f[19], &bits, sizeof(bits)); }
label_10020c:
    // 0x10020c: 0x4480a000  mtc1        $zero, $f20
    ctx->pc = 0x10020cu;
    { uint32_t bits = GPR_U32(ctx, 0); std::memcpy(&ctx->f[20], &bits, sizeof(bits)); }
label_100210:
    // 0x100210: 0x4480a800  mtc1        $zero, $f21
    ctx->pc = 0x100210u;
    { uint32_t bits = GPR_U32(ctx, 0); std::memcpy(&ctx->f[21], &bits, sizeof(bits)); }
label_100214:
    // 0x100214: 0x4480b000  mtc1        $zero, $f22
    ctx->pc = 0x100214u;
    { uint32_t bits = GPR_U32(ctx, 0); std::memcpy(&ctx->f[22], &bits, sizeof(bits)); }
label_100218:
    // 0x100218: 0x4480b800  mtc1        $zero, $f23
    ctx->pc = 0x100218u;
    { uint32_t bits = GPR_U32(ctx, 0); std::memcpy(&ctx->f[23], &bits, sizeof(bits)); }
label_10021c:
    // 0x10021c: 0x4480c000  mtc1        $zero, $f24
    ctx->pc = 0x10021cu;
    { uint32_t bits = GPR_U32(ctx, 0); std::memcpy(&ctx->f[24], &bits, sizeof(bits)); }
label_100220:
    // 0x100220: 0x4480c800  mtc1        $zero, $f25
    ctx->pc = 0x100220u;
    { uint32_t bits = GPR_U32(ctx, 0); std::memcpy(&ctx->f[25], &bits, sizeof(bits)); }
label_100224:
    // 0x100224: 0x4480d000  mtc1        $zero, $f26
    ctx->pc = 0x100224u;
    { uint32_t bits = GPR_U32(ctx, 0); std::memcpy(&ctx->f[26], &bits, sizeof(bits)); }
label_100228:
    // 0x100228: 0x4480d800  mtc1        $zero, $f27
    ctx->pc = 0x100228u;
    { uint32_t bits = GPR_U32(ctx, 0); std::memcpy(&ctx->f[27], &bits, sizeof(bits)); }
label_10022c:
    // 0x10022c: 0x4480e000  mtc1        $zero, $f28
    ctx->pc = 0x10022cu;
    { uint32_t bits = GPR_U32(ctx, 0); std::memcpy(&ctx->f[28], &bits, sizeof(bits)); }
label_100230:
    // 0x100230: 0x4480e800  mtc1        $zero, $f29
    ctx->pc = 0x100230u;
    { uint32_t bits = GPR_U32(ctx, 0); std::memcpy(&ctx->f[29], &bits, sizeof(bits)); }
label_100234:
    // 0x100234: 0x4480f000  mtc1        $zero, $f30
    ctx->pc = 0x100234u;
    { uint32_t bits = GPR_U32(ctx, 0); std::memcpy(&ctx->f[30], &bits, sizeof(bits)); }
label_100238:
    // 0x100238: 0x4480f800  mtc1        $zero, $f31
    ctx->pc = 0x100238u;
    { uint32_t bits = GPR_U32(ctx, 0); std::memcpy(&ctx->f[31], &bits, sizeof(bits)); }
label_10023c:
    // 0x10023c: 0x46010018  adda.s      $f0, $f1
    ctx->pc = 0x10023cu;
    FPU_SET_ACC(ctx, FPU_ADD_S(ctx->f[0], ctx->f[1]));
label_100240:
    // 0x100240: 0x40f  sync.p
    ctx->pc = 0x100240u;
    // SYNC instruction - memory barrier
// In recompiled code, we don't need explicit memory barriers
label_100244:
    // 0x100244: 0x44c0f800  ctc1        $zero, $FpcCsr
    ctx->pc = 0x100244u;
    ctx->fcr31 = GPR_U32(ctx, 0) & 0x0183FFFF;
label_100248:
    // 0x100248: 0x3c020030  lui         $v0, 0x30
    ctx->pc = 0x100248u;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)48 << 16));
label_10024c:
    // 0x10024c: 0x3c030033  lui         $v1, 0x33
    ctx->pc = 0x10024cu;
    SET_GPR_S32(ctx, 3, (int32_t)((uint32_t)51 << 16));
label_100250:
    // 0x100250: 0x24422380  addiu       $v0, $v0, 0x2380
    ctx->pc = 0x100250u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 9088));
label_100254:
    // 0x100254: 0x2463a7c0  addiu       $v1, $v1, -0x5840
    ctx->pc = 0x100254u;
    SET_GPR_S32(ctx, 3, (int32_t)ADD32(GPR_U32(ctx, 3), 4294944704));
label_100258:
    // 0x100258: 0x3044000f  andi        $a0, $v0, 0xF
    ctx->pc = 0x100258u;
    SET_GPR_U64(ctx, 4, GPR_U64(ctx, 2) & (uint64_t)(uint16_t)15);
label_10025c:
    // 0x10025c: 0x10800006  beqz        $a0, . + 4 + (0x6 << 2)
label_100260:
    if (ctx->pc == 0x100260u) {
        ctx->pc = 0x100264u;
        goto label_100264;
    }
    ctx->pc = 0x10025Cu;
    {
        const bool branch_taken_0x10025c = (GPR_U64(ctx, 4) == GPR_U64(ctx, 0));
        if (branch_taken_0x10025c) {
            ctx->pc = 0x100278u;
            goto label_100278;
        }
    }
    ctx->pc = 0x100264u;
label_100264:
    // 0x100264: 0xa0400000  sb          $zero, 0x0($v0)
    ctx->pc = 0x100264u;
    WRITE8(ADD32(GPR_U32(ctx, 2), 0), (uint8_t)GPR_U32(ctx, 0));
label_100268:
    // 0x100268: 0x24420001  addiu       $v0, $v0, 0x1
    ctx->pc = 0x100268u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 1));
label_10026c:
    // 0x10026c: 0x0  nop
    ctx->pc = 0x10026cu;
    // NOP
label_100270:
    // 0x100270: 0x8040096  j           func_100258
label_100274:
    if (ctx->pc == 0x100274u) {
        ctx->pc = 0x100278u;
        goto label_100278;
    }
    ctx->pc = 0x100270u;
    ctx->pc = 0x100258u;
    if (runtime->shouldPreemptGuestExecution()) {
        return;
    }
    goto label_100258;
    ctx->pc = 0x100278u;
label_100278:
    // 0x100278: 0x3c04ffff  lui         $a0, 0xFFFF
    ctx->pc = 0x100278u;
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)65535 << 16));
label_10027c:
    // 0x10027c: 0x3484fff0  ori         $a0, $a0, 0xFFF0
    ctx->pc = 0x10027cu;
    SET_GPR_U64(ctx, 4, GPR_U64(ctx, 4) | (uint64_t)(uint16_t)65520);
label_100280:
    // 0x100280: 0x642024  and         $a0, $v1, $a0
    ctx->pc = 0x100280u;
    SET_GPR_U64(ctx, 4, GPR_U64(ctx, 3) & GPR_U64(ctx, 4));
label_100284:
    // 0x100284: 0x10440007  beq         $v0, $a0, . + 4 + (0x7 << 2)
label_100288:
    if (ctx->pc == 0x100288u) {
        ctx->pc = 0x10028Cu;
        goto label_10028c;
    }
    ctx->pc = 0x100284u;
    {
        const bool branch_taken_0x100284 = (GPR_U64(ctx, 2) == GPR_U64(ctx, 4));
        if (branch_taken_0x100284) {
            ctx->pc = 0x1002A4u;
            goto label_1002a4;
        }
    }
    ctx->pc = 0x10028Cu;
label_10028c:
    // 0x10028c: 0x7c400000  sq          $zero, 0x0($v0)
    ctx->pc = 0x10028cu;
    WRITE128(ADD32(GPR_U32(ctx, 2), 0), GPR_VEC(ctx, 0));
label_100290:
    // 0x100290: 0x24420010  addiu       $v0, $v0, 0x10
    ctx->pc = 0x100290u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 16));
label_100294:
    // 0x100294: 0x0  nop
    ctx->pc = 0x100294u;
    // NOP
label_100298:
    // 0x100298: 0x0  nop
    ctx->pc = 0x100298u;
    // NOP
label_10029c:
    // 0x10029c: 0x80400a1  j           func_100284
label_1002a0:
    if (ctx->pc == 0x1002A0u) {
        ctx->pc = 0x1002A4u;
        goto label_1002a4;
    }
    ctx->pc = 0x10029Cu;
    ctx->pc = 0x100284u;
    if (runtime->shouldPreemptGuestExecution()) {
        return;
    }
    goto label_100284;
    ctx->pc = 0x1002A4u;
label_1002a4:
    // 0x1002a4: 0x10430007  beq         $v0, $v1, . + 4 + (0x7 << 2)
label_1002a8:
    if (ctx->pc == 0x1002A8u) {
        ctx->pc = 0x1002ACu;
        goto label_1002ac;
    }
    ctx->pc = 0x1002A4u;
    {
        const bool branch_taken_0x1002a4 = (GPR_U64(ctx, 2) == GPR_U64(ctx, 3));
        if (branch_taken_0x1002a4) {
            ctx->pc = 0x1002C4u;
            goto label_1002c4;
        }
    }
    ctx->pc = 0x1002ACu;
label_1002ac:
    // 0x1002ac: 0xa0400000  sb          $zero, 0x0($v0)
    ctx->pc = 0x1002acu;
    WRITE8(ADD32(GPR_U32(ctx, 2), 0), (uint8_t)GPR_U32(ctx, 0));
label_1002b0:
    // 0x1002b0: 0x24420001  addiu       $v0, $v0, 0x1
    ctx->pc = 0x1002b0u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 1));
label_1002b4:
    // 0x1002b4: 0x0  nop
    ctx->pc = 0x1002b4u;
    // NOP
label_1002b8:
    // 0x1002b8: 0x0  nop
    ctx->pc = 0x1002b8u;
    // NOP
label_1002bc:
    // 0x1002bc: 0x80400a9  j           func_1002A4
label_1002c0:
    if (ctx->pc == 0x1002C0u) {
        ctx->pc = 0x1002C4u;
        goto label_1002c4;
    }
    ctx->pc = 0x1002BCu;
    ctx->pc = 0x1002A4u;
    if (runtime->shouldPreemptGuestExecution()) {
        return;
    }
    goto label_1002a4;
    ctx->pc = 0x1002C4u;
label_1002c4:
    // 0x1002c4: 0x3c040030  lui         $a0, 0x30
    ctx->pc = 0x1002c4u;
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)48 << 16));
label_1002c8:
    // 0x1002c8: 0x3c0501f8  lui         $a1, 0x1F8
    ctx->pc = 0x1002c8u;
    SET_GPR_S32(ctx, 5, (int32_t)((uint32_t)504 << 16));
label_1002cc:
    // 0x1002cc: 0x3c060008  lui         $a2, 0x8
    ctx->pc = 0x1002ccu;
    SET_GPR_S32(ctx, 6, (int32_t)((uint32_t)8 << 16));
label_1002d0:
    // 0x1002d0: 0x3c070030  lui         $a3, 0x30
    ctx->pc = 0x1002d0u;
    SET_GPR_S32(ctx, 7, (int32_t)((uint32_t)48 << 16));
label_1002d4:
    // 0x1002d4: 0x3c080010  lui         $t0, 0x10
    ctx->pc = 0x1002d4u;
    SET_GPR_S32(ctx, 8, (int32_t)((uint32_t)16 << 16));
label_1002d8:
    // 0x1002d8: 0x24847bf0  addiu       $a0, $a0, 0x7BF0
    ctx->pc = 0x1002d8u;
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 4), 31728));
label_1002dc:
    // 0x1002dc: 0x24a50000  addiu       $a1, $a1, 0x0
    ctx->pc = 0x1002dcu;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 5), 0));
label_1002e0:
    // 0x1002e0: 0x24c60000  addiu       $a2, $a2, 0x0
    ctx->pc = 0x1002e0u;
    SET_GPR_S32(ctx, 6, (int32_t)ADD32(GPR_U32(ctx, 6), 0));
label_1002e4:
    // 0x1002e4: 0x24e72940  addiu       $a3, $a3, 0x2940
    ctx->pc = 0x1002e4u;
    SET_GPR_S32(ctx, 7, (int32_t)ADD32(GPR_U32(ctx, 7), 10560));
label_1002e8:
    // 0x1002e8: 0x25080380  addiu       $t0, $t0, 0x380
    ctx->pc = 0x1002e8u;
    SET_GPR_S32(ctx, 8, (int32_t)ADD32(GPR_U32(ctx, 8), 896));
label_1002ec:
    // 0x1002ec: 0x80e02d  daddu       $gp, $a0, $zero
    ctx->pc = 0x1002ecu;
    SET_GPR_U64(ctx, 28, (uint64_t)GPR_U64(ctx, 4) + (uint64_t)GPR_U64(ctx, 0));
label_1002f0:
    // 0x1002f0: 0x2403003c  addiu       $v1, $zero, 0x3C
    ctx->pc = 0x1002f0u;
    SET_GPR_S32(ctx, 3, (int32_t)ADD32(GPR_U32(ctx, 0), 60));
label_1002f4:
    // 0x1002f4: 0xc  syscall     0
    ctx->pc = 0x1002f4u;
    runtime->handleSyscall(rdram, ctx, 0x0u);
label_1002f8:
    // 0x1002f8: 0x40e82d  daddu       $sp, $v0, $zero
    ctx->pc = 0x1002f8u;
    SET_GPR_U64(ctx, 29, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
label_1002fc:
    // 0x1002fc: 0x3c040033  lui         $a0, 0x33
    ctx->pc = 0x1002fcu;
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)51 << 16));
label_100300:
    // 0x100300: 0x3c050000  lui         $a1, 0x0
    ctx->pc = 0x100300u;
    SET_GPR_S32(ctx, 5, (int32_t)((uint32_t)0 << 16));
label_100304:
    // 0x100304: 0x2484a7c0  addiu       $a0, $a0, -0x5840
    ctx->pc = 0x100304u;
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 4), 4294944704));
label_100308:
    // 0x100308: 0x24a5ffff  addiu       $a1, $a1, -0x1
    ctx->pc = 0x100308u;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 5), 4294967295));
label_10030c:
    // 0x10030c: 0x2403003d  addiu       $v1, $zero, 0x3D
    ctx->pc = 0x10030cu;
    SET_GPR_S32(ctx, 3, (int32_t)ADD32(GPR_U32(ctx, 0), 61));
label_100310:
    // 0x100310: 0xc  syscall     0
    ctx->pc = 0x100310u;
    runtime->handleSyscall(rdram, ctx, 0x0u);
label_100314:
    // 0x100314: 0xc0764f6  jal         func_1D93D8
label_100318:
    if (ctx->pc == 0x100318u) {
        ctx->pc = 0x10031Cu;
        goto label_10031c;
    }
    ctx->pc = 0x100314u;
    SET_GPR_U32(ctx, 31, 0x10031Cu);
    ctx->pc = 0x1D93D8u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1D93D8u, 0x100314u, 0x10031Cu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        std::cerr << "[DIAG:entry-callfail] target=0x1D93D8u src=0x100314u fallthrough=0x10031Cu sp=0x" << std::hex << GPR_U32(ctx, 29) << " pc=0x" << ctx->pc << std::dec << std::endl;
        return;
    }
    ctx->pc = 0x10031Cu;
label_10031c:
    // 0x10031c: 0xc073ef8  jal         func_1CFBE0
label_100320:
    if (ctx->pc == 0x100320u) {
        ctx->pc = 0x100320u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x10031Cu;
        // 0x100320: 0x202d  daddu       $a0, $zero, $zero (Delay Slot)
        SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 0) + (uint64_t)GPR_U64(ctx, 0));
        ctx->in_delay_slot = false;
        ctx->pc = 0x100324u;
        goto label_100324;
    }
    ctx->pc = 0x10031Cu;
    SET_GPR_U32(ctx, 31, 0x100324u);
    ctx->pc = 0x100320u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x10031Cu;
    // 0x100320: 0x202d  daddu       $a0, $zero, $zero (Delay Slot)
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 0) + (uint64_t)GPR_U64(ctx, 0));
    ctx->in_delay_slot = false;
    ctx->pc = 0x1CFBE0u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1CFBE0u, 0x10031Cu, 0x100324u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        std::cerr << "[DIAG:entry-callfail] target=0x1CFBE0u src=0x10031Cu fallthrough=0x100324u sp=0x" << std::hex << GPR_U32(ctx, 29) << " pc=0x" << ctx->pc << std::dec << std::endl;
        return;
    }
    ctx->pc = 0x100324u;
label_100324:
    // 0x100324: 0x3c100024  lui         $s0, 0x24
    ctx->pc = 0x100324u;
    SET_GPR_S32(ctx, 16, (int32_t)((uint32_t)36 << 16));
label_100328:
    // 0x100328: 0x26101a10  addiu       $s0, $s0, 0x1A10
    ctx->pc = 0x100328u;
    SET_GPR_S32(ctx, 16, (int32_t)ADD32(GPR_U32(ctx, 16), 6672));
label_10032c:
    // 0x10032c: 0x12000003  beqz        $s0, . + 4 + (0x3 << 2)
label_100330:
    if (ctx->pc == 0x100330u) {
        ctx->pc = 0x100334u;
        goto label_100334;
    }
    ctx->pc = 0x10032Cu;
    {
        const bool branch_taken_0x10032c = (GPR_U64(ctx, 16) == GPR_U64(ctx, 0));
        if (branch_taken_0x10032c) {
            ctx->pc = 0x10033Cu;
            goto label_10033c;
        }
    }
    ctx->pc = 0x100334u;
label_100334:
    // 0x100334: 0x200f809  jalr        $s0
label_100338:
    if (ctx->pc == 0x100338u) {
        ctx->pc = 0x10033Cu;
        goto label_10033c;
    }
    ctx->pc = 0x100334u;
    {
        const uint32_t jumpTarget = GPR_U32(ctx, 16);
        SET_GPR_U32(ctx, 31, 0x10033Cu);
        ctx->pc = jumpTarget;
        if (!runtime->dispatchGuestBranch(rdram, ctx, jumpTarget, 0x100334u, 0x10033Cu, PS2Runtime::GuestBranchKind::IndirectCall, "JALR")) {
            return;
        }
    }
    ctx->pc = 0x10033Cu;
label_10033c:
    // 0x10033c: 0x3c040024  lui         $a0, 0x24
    ctx->pc = 0x10033cu;
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)36 << 16));
label_100340:
    // 0x100340: 0x24841a34  addiu       $a0, $a0, 0x1A34
    ctx->pc = 0x100340u;
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 4), 6708));
label_100344:
    // 0x100344: 0x10800003  beqz        $a0, . + 4 + (0x3 << 2)
label_100348:
    if (ctx->pc == 0x100348u) {
        ctx->pc = 0x10034Cu;
        goto label_10034c;
    }
    ctx->pc = 0x100344u;
    {
        const bool branch_taken_0x100344 = (GPR_U64(ctx, 4) == GPR_U64(ctx, 0));
        if (branch_taken_0x100344) {
            ctx->pc = 0x100354u;
            goto label_100354;
        }
    }
    ctx->pc = 0x10034Cu;
label_10034c:
    // 0x10034c: 0xc0709e0  jal         func_1C2780
label_100350:
    if (ctx->pc == 0x100350u) {
        ctx->pc = 0x100354u;
        goto label_100354;
    }
    ctx->pc = 0x10034Cu;
    SET_GPR_U32(ctx, 31, 0x100354u);
    ctx->pc = 0x1C2780u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1C2780u, 0x10034Cu, 0x100354u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        std::cerr << "[DIAG:entry-callfail] target=0x1C2780u src=0x10034Cu fallthrough=0x100354u sp=0x" << std::hex << GPR_U32(ctx, 29) << " pc=0x" << ctx->pc << std::dec << std::endl;
        return;
    }
    ctx->pc = 0x100354u;
label_100354:
    // 0x100354: 0x42000038  ei
    ctx->pc = 0x100354u;
    ctx->cop0_status |= 0x10000; // Enable interrupts
label_100358:
    // 0x100358: 0x3c020030  lui         $v0, 0x30
    ctx->pc = 0x100358u;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)48 << 16));
label_10035c:
    // 0x10035c: 0x24422940  addiu       $v0, $v0, 0x2940
    ctx->pc = 0x10035cu;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 10560));
label_100360:
    // 0x100360: 0x8c440000  lw          $a0, 0x0($v0)
    ctx->pc = 0x100360u;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 2), 0)));
label_100364:
    // 0x100364: 0xc040138  jal         func_1004E0
label_100368:
    if (ctx->pc == 0x100368u) {
        ctx->pc = 0x100368u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x100364u;
        // 0x100368: 0x24450004  addiu       $a1, $v0, 0x4 (Delay Slot)
        SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 2), 4));
        ctx->in_delay_slot = false;
        ctx->pc = 0x10036Cu;
        goto label_10036c;
    }
    ctx->pc = 0x100364u;
    SET_GPR_U32(ctx, 31, 0x10036Cu);
    ctx->pc = 0x100368u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x100364u;
    // 0x100368: 0x24450004  addiu       $a1, $v0, 0x4 (Delay Slot)
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 2), 4));
    ctx->in_delay_slot = false;
    ctx->pc = 0x1004E0u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1004E0u, 0x100364u, 0x10036Cu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        std::cerr << "[DIAG:entry-callfail] target=0x1004E0u src=0x100364u fallthrough=0x10036Cu sp=0x" << std::hex << GPR_U32(ctx, 29) << " pc=0x" << ctx->pc << std::dec << std::endl;
        return;
    }
    ctx->pc = 0x10036Cu;
label_10036c:
    // 0x10036c: 0x8070a22  j           func_1C2888
label_100370:
    if (ctx->pc == 0x100370u) {
        ctx->pc = 0x100370u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x10036Cu;
        // 0x100370: 0x40202d  daddu       $a0, $v0, $zero (Delay Slot)
        SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
        ctx->in_delay_slot = false;
        ctx->pc = 0x100374u;
        goto label_fallthrough_0x10036c;
    }
    ctx->pc = 0x10036Cu;
    ctx->pc = 0x100370u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x10036Cu;
    // 0x100370: 0x40202d  daddu       $a0, $v0, $zero (Delay Slot)
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    ctx->in_delay_slot = false;
    ctx->pc = 0x1C2888u;
    FUN_001c2888_0x1c2888(rdram, ctx, runtime); return;
label_fallthrough_0x10036c:
    ctx->pc = 0x100374u;
}
