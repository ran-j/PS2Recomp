#include <stdexcept>
#include "ps2_runtime_macros.h"
#include "ps2_runtime.h"
#include "ps2_recompiled_functions.h"
#include "ps2_recompiled_stubs.h"

#include "ps2_syscalls.h"
#include "ps2_stubs.h"
#include "../lib/Kernel/Syscalls/Sync.h"

#ifdef PS2_FUNCTION_LOG_TRACKER
#include "ps2_log.h"
#endif

// Function: FUN_001d3730
// Address: 0x1d3730 - 0x1d3930
void FUN_001d3730_0x1d3730(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
#ifdef PS2_FUNCTION_LOG_TRACKER
    PS_LOG_ENTRY("FUN_001d3730_0x1d3730");
#endif

    switch (ctx->pc) {
        case 0x1d3788u: goto label_1d3788;
        case 0x1d37f4u: goto label_1d37f4;
        case 0x1d380cu: goto label_1d380c;
        case 0x1d381cu: goto label_1d381c;
        case 0x1d3864u: goto label_1d3864;
        case 0x1d3890u: goto label_1d3890;
        case 0x1d38a0u: goto label_1d38a0;
        case 0x1d38ccu: goto label_1d38cc;
        case 0x1d38dcu: goto label_1d38dc;
        case 0x1d38e4u: goto label_1d38e4;
        case 0x1d38f4u: goto label_1d38f4;
        case 0x1d38fcu: goto label_1d38fc;
        default: break;
    }

    ctx->pc = 0x1d3730u;

    // 0x1d3730: 0x27bdff40  addiu       $sp, $sp, -0xC0
    ctx->pc = 0x1d3730u;
    SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 4294967104));
    // 0x1d3734: 0xffb10030  sd          $s1, 0x30($sp)
    ctx->pc = 0x1d3734u;
    WRITE64(ADD32(GPR_U32(ctx, 29), 48), GPR_U64(ctx, 17));
    // 0x1d3738: 0x80882d  daddu       $s1, $a0, $zero
    ctx->pc = 0x1d3738u;
    SET_GPR_U64(ctx, 17, (uint64_t)GPR_U64(ctx, 4) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1d373c: 0xffbe00a0  sd          $fp, 0xA0($sp)
    ctx->pc = 0x1d373cu;
    WRITE64(ADD32(GPR_U32(ctx, 29), 160), GPR_U64(ctx, 30));
    // 0x1d3740: 0xffb70090  sd          $s7, 0x90($sp)
    ctx->pc = 0x1d3740u;
    WRITE64(ADD32(GPR_U32(ctx, 29), 144), GPR_U64(ctx, 23));
    // 0x1d3744: 0x3c040032  lui         $a0, 0x32
    ctx->pc = 0x1d3744u;
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)50 << 16));
    // 0x1d3748: 0xffb60080  sd          $s6, 0x80($sp)
    ctx->pc = 0x1d3748u;
    WRITE64(ADD32(GPR_U32(ctx, 29), 128), GPR_U64(ctx, 22));
    // 0x1d374c: 0xc0f02d  daddu       $fp, $a2, $zero
    ctx->pc = 0x1d374cu;
    SET_GPR_U64(ctx, 30, (uint64_t)GPR_U64(ctx, 6) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1d3750: 0xffb50070  sd          $s5, 0x70($sp)
    ctx->pc = 0x1d3750u;
    WRITE64(ADD32(GPR_U32(ctx, 29), 112), GPR_U64(ctx, 21));
    // 0x1d3754: 0xa0b02d  daddu       $s6, $a1, $zero
    ctx->pc = 0x1d3754u;
    SET_GPR_U64(ctx, 22, (uint64_t)GPR_U64(ctx, 5) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1d3758: 0xffb40060  sd          $s4, 0x60($sp)
    ctx->pc = 0x1d3758u;
    WRITE64(ADD32(GPR_U32(ctx, 29), 96), GPR_U64(ctx, 20));
    // 0x1d375c: 0xe0a82d  daddu       $s5, $a3, $zero
    ctx->pc = 0x1d375cu;
    SET_GPR_U64(ctx, 21, (uint64_t)GPR_U64(ctx, 7) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1d3760: 0xffb30050  sd          $s3, 0x50($sp)
    ctx->pc = 0x1d3760u;
    WRITE64(ADD32(GPR_U32(ctx, 29), 80), GPR_U64(ctx, 19));
    // 0x1d3764: 0x120a02d  daddu       $s4, $t1, $zero
    ctx->pc = 0x1d3764u;
    SET_GPR_U64(ctx, 20, (uint64_t)GPR_U64(ctx, 9) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1d3768: 0xffb20040  sd          $s2, 0x40($sp)
    ctx->pc = 0x1d3768u;
    WRITE64(ADD32(GPR_U32(ctx, 29), 64), GPR_U64(ctx, 18));
    // 0x1d376c: 0x140982d  daddu       $s3, $t2, $zero
    ctx->pc = 0x1d376cu;
    SET_GPR_U64(ctx, 19, (uint64_t)GPR_U64(ctx, 10) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1d3770: 0xffb00020  sd          $s0, 0x20($sp)
    ctx->pc = 0x1d3770u;
    WRITE64(ADD32(GPR_U32(ctx, 29), 32), GPR_U64(ctx, 16));
    // 0x1d3774: 0x100902d  daddu       $s2, $t0, $zero
    ctx->pc = 0x1d3774u;
    SET_GPR_U64(ctx, 18, (uint64_t)GPR_U64(ctx, 8) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1d3778: 0xffbf00b0  sd          $ra, 0xB0($sp)
    ctx->pc = 0x1d3778u;
    WRITE64(ADD32(GPR_U32(ctx, 29), 176), GPR_U64(ctx, 31));
    // 0x1d377c: 0x160b82d  daddu       $s7, $t3, $zero
    ctx->pc = 0x1d377cu;
    SET_GPR_U64(ctx, 23, (uint64_t)GPR_U64(ctx, 11) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1d3780: 0xc074bdc  jal         func_1D2F70
    ctx->pc = 0x1D3780u;
    SET_GPR_U32(ctx, 31, 0x1D3788u);
    ctx->pc = 0x1D3784u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1D3780u;
    // 0x1d3784: 0x24844100  addiu       $a0, $a0, 0x4100 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 4), 16640));
    ctx->in_delay_slot = false;
    ctx->pc = 0x1D2F70u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1D2F70u, 0x1D3780u, 0x1D3788u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1D3788u;
label_1d3788:
    // 0x1d3788: 0x40802d  daddu       $s0, $v0, $zero
    ctx->pc = 0x1d3788u;
    SET_GPR_U64(ctx, 16, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1d378c: 0x1200005c  beqz        $s0, . + 4 + (0x5C << 2)
    ctx->pc = 0x1D378Cu;
    {
        const bool branch_taken_0x1d378c = (GPR_U64(ctx, 16) == GPR_U64(ctx, 0));
        ctx->pc = 0x1D3790u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1D378Cu;
        // 0x1d3790: 0x2402ffff  addiu       $v0, $zero, -0x1 (Delay Slot)
        SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 0), 4294967295));
        ctx->in_delay_slot = false;
        if (branch_taken_0x1d378c) {
            ctx->pc = 0x1D3900u;
            goto label_1d3900;
        }
    }
    ctx->pc = 0x1D3794u;
    // 0x1d3794: 0x8fa200c0  lw          $v0, 0xC0($sp)
    ctx->pc = 0x1d3794u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 29), 192)));
    // 0x1d3798: 0x8e030018  lw          $v1, 0x18($s0)
    ctx->pc = 0x1d3798u;
    SET_GPR_S32(ctx, 3, (int32_t)READ32(ADD32(GPR_U32(ctx, 16), 24)));
    // 0x1d379c: 0xae220020  sw          $v0, 0x20($s1)
    ctx->pc = 0x1d379cu;
    WRITE32(ADD32(GPR_U32(ctx, 17), 32), GPR_U32(ctx, 2));
    // 0x1d37a0: 0xae230004  sw          $v1, 0x4($s1)
    ctx->pc = 0x1d37a0u;
    WRITE32(ADD32(GPR_U32(ctx, 17), 4), GPR_U32(ctx, 3));
    // 0x1d37a4: 0xae300000  sw          $s0, 0x0($s1)
    ctx->pc = 0x1d37a4u;
    WRITE32(ADD32(GPR_U32(ctx, 17), 0), GPR_U32(ctx, 16));
    // 0x1d37a8: 0xae37001c  sw          $s7, 0x1C($s1)
    ctx->pc = 0x1d37a8u;
    WRITE32(ADD32(GPR_U32(ctx, 17), 28), GPR_U32(ctx, 23));
    // 0x1d37ac: 0x380102d  daddu       $v0, $gp, $zero
    ctx->pc = 0x1d37acu;
    SET_GPR_U64(ctx, 2, (uint64_t)GPR_U64(ctx, 28) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1d37b0: 0xae220018  sw          $v0, 0x18($s1)
    ctx->pc = 0x1d37b0u;
    WRITE32(ADD32(GPR_U32(ctx, 17), 24), GPR_U32(ctx, 2));
    // 0x1d37b4: 0x33c30002  andi        $v1, $fp, 0x2
    ctx->pc = 0x1d37b4u;
    SET_GPR_U64(ctx, 3, GPR_U64(ctx, 30) & (uint64_t)(uint16_t)2);
    // 0x1d37b8: 0xae160020  sw          $s6, 0x20($s0)
    ctx->pc = 0x1d37b8u;
    WRITE32(ADD32(GPR_U32(ctx, 16), 32), GPR_U32(ctx, 22));
    // 0x1d37bc: 0xae120024  sw          $s2, 0x24($s0)
    ctx->pc = 0x1d37bcu;
    WRITE32(ADD32(GPR_U32(ctx, 16), 36), GPR_U32(ctx, 18));
    // 0x1d37c0: 0xae140028  sw          $s4, 0x28($s0)
    ctx->pc = 0x1d37c0u;
    WRITE32(ADD32(GPR_U32(ctx, 16), 40), GPR_U32(ctx, 20));
    // 0x1d37c4: 0xae13002c  sw          $s3, 0x2C($s0)
    ctx->pc = 0x1d37c4u;
    WRITE32(ADD32(GPR_U32(ctx, 16), 44), GPR_U32(ctx, 19));
    // 0x1d37c8: 0xae100014  sw          $s0, 0x14($s0)
    ctx->pc = 0x1d37c8u;
    WRITE32(ADD32(GPR_U32(ctx, 16), 20), GPR_U32(ctx, 16));
    // 0x1d37cc: 0x8e220024  lw          $v0, 0x24($s1)
    ctx->pc = 0x1d37ccu;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 17), 36)));
    // 0x1d37d0: 0xae11001c  sw          $s1, 0x1C($s0)
    ctx->pc = 0x1d37d0u;
    WRITE32(ADD32(GPR_U32(ctx, 16), 28), GPR_U32(ctx, 17));
    // 0x1d37d4: 0x14600011  bnez        $v1, . + 4 + (0x11 << 2)
    ctx->pc = 0x1D37D4u;
    {
        const bool branch_taken_0x1d37d4 = (GPR_U64(ctx, 3) != GPR_U64(ctx, 0));
        ctx->pc = 0x1D37D8u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1D37D4u;
        // 0x1d37d8: 0xae020034  sw          $v0, 0x34($s0) (Delay Slot)
        WRITE32(ADD32(GPR_U32(ctx, 16), 52), GPR_U32(ctx, 2));
        ctx->in_delay_slot = false;
        if (branch_taken_0x1d37d4) {
            ctx->pc = 0x1D381Cu;
            goto label_1d381c;
        }
    }
    ctx->pc = 0x1D37DCu;
    // 0x1d37dc: 0x16b40007  bne         $s5, $s4, . + 4 + (0x7 << 2)
    ctx->pc = 0x1D37DCu;
    {
        const bool branch_taken_0x1d37dc = (GPR_U64(ctx, 21) != GPR_U64(ctx, 20));
        ctx->pc = 0x1D37E0u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1D37DCu;
        // 0x1d37e0: 0x253102a  slt         $v0, $s2, $s3 (Delay Slot)
        SET_GPR_U64(ctx, 2, ((int64_t)GPR_S64(ctx, 18) < (int64_t)GPR_S64(ctx, 19)) ? 1 : 0);
        ctx->in_delay_slot = false;
        if (branch_taken_0x1d37dc) {
            ctx->pc = 0x1D37FCu;
            goto label_1d37fc;
        }
    }
    ctx->pc = 0x1D37E4u;
    // 0x1d37e4: 0x260282d  daddu       $a1, $s3, $zero
    ctx->pc = 0x1d37e4u;
    SET_GPR_U64(ctx, 5, (uint64_t)GPR_U64(ctx, 19) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1d37e8: 0x2a0202d  daddu       $a0, $s5, $zero
    ctx->pc = 0x1d37e8u;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 21) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1d37ec: 0xc074b3e  jal         func_1D2CF8
    ctx->pc = 0x1D37ECu;
    SET_GPR_U32(ctx, 31, 0x1D37F4u);
    ctx->pc = 0x1D37F0u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1D37ECu;
    // 0x1d37f0: 0x242280a  movz        $a1, $s2, $v0 (Delay Slot)
    if (GPR_U64(ctx, 2) == 0) SET_GPR_VEC(ctx, 5, GPR_VEC(ctx, 18));
    ctx->in_delay_slot = false;
    ctx->pc = 0x1D2CF8u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1D2CF8u, 0x1D37ECu, 0x1D37F4u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1D37F4u;
label_1d37f4:
    // 0x1d37f4: 0x1000000a  b           . + 4 + (0xA << 2)
    ctx->pc = 0x1D37F4u;
    {
        const bool branch_taken_0x1d37f4 = (GPR_U64(ctx, 0) == GPR_U64(ctx, 0));
        ctx->pc = 0x1D37F8u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1D37F4u;
        // 0x1d37f8: 0x33c20001  andi        $v0, $fp, 0x1 (Delay Slot)
        SET_GPR_U64(ctx, 2, GPR_U64(ctx, 30) & (uint64_t)(uint16_t)1);
        ctx->in_delay_slot = false;
        if (branch_taken_0x1d37f4) {
            ctx->pc = 0x1D3820u;
            goto label_1d3820;
        }
    }
    ctx->pc = 0x1D37FCu;
label_1d37fc:
    // 0x1d37fc: 0x1a400003  blez        $s2, . + 4 + (0x3 << 2)
    ctx->pc = 0x1D37FCu;
    {
        const bool branch_taken_0x1d37fc = (GPR_S32(ctx, 18) <= 0);
        ctx->pc = 0x1D3800u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1D37FCu;
        // 0x1d3800: 0x2a0202d  daddu       $a0, $s5, $zero (Delay Slot)
        SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 21) + (uint64_t)GPR_U64(ctx, 0));
        ctx->in_delay_slot = false;
        if (branch_taken_0x1d37fc) {
            ctx->pc = 0x1D380Cu;
            goto label_1d380c;
        }
    }
    ctx->pc = 0x1D3804u;
    // 0x1d3804: 0xc074b3e  jal         func_1D2CF8
    ctx->pc = 0x1D3804u;
    SET_GPR_U32(ctx, 31, 0x1D380Cu);
    ctx->pc = 0x1D3808u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1D3804u;
    // 0x1d3808: 0x240282d  daddu       $a1, $s2, $zero (Delay Slot)
    SET_GPR_U64(ctx, 5, (uint64_t)GPR_U64(ctx, 18) + (uint64_t)GPR_U64(ctx, 0));
    ctx->in_delay_slot = false;
    ctx->pc = 0x1D2CF8u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1D2CF8u, 0x1D3804u, 0x1D380Cu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1D380Cu;
label_1d380c:
    // 0x1d380c: 0x1a600003  blez        $s3, . + 4 + (0x3 << 2)
    ctx->pc = 0x1D380Cu;
    {
        const bool branch_taken_0x1d380c = (GPR_S32(ctx, 19) <= 0);
        ctx->pc = 0x1D3810u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1D380Cu;
        // 0x1d3810: 0x280202d  daddu       $a0, $s4, $zero (Delay Slot)
        SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 20) + (uint64_t)GPR_U64(ctx, 0));
        ctx->in_delay_slot = false;
        if (branch_taken_0x1d380c) {
            ctx->pc = 0x1D381Cu;
            goto label_1d381c;
        }
    }
    ctx->pc = 0x1D3814u;
    // 0x1d3814: 0xc074b3e  jal         func_1D2CF8
    ctx->pc = 0x1D3814u;
    SET_GPR_U32(ctx, 31, 0x1D381Cu);
    ctx->pc = 0x1D3818u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1D3814u;
    // 0x1d3818: 0x260282d  daddu       $a1, $s3, $zero (Delay Slot)
    SET_GPR_U64(ctx, 5, (uint64_t)GPR_U64(ctx, 19) + (uint64_t)GPR_U64(ctx, 0));
    ctx->in_delay_slot = false;
    ctx->pc = 0x1D2CF8u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1D2CF8u, 0x1D3814u, 0x1D381Cu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1D381Cu;
label_1d381c:
    // 0x1d381c: 0x33c20001  andi        $v0, $fp, 0x1
    ctx->pc = 0x1d381cu;
    SET_GPR_U64(ctx, 2, GPR_U64(ctx, 30) & (uint64_t)(uint16_t)1);
label_1d3820:
    // 0x1d3820: 0x10400014  beqz        $v0, . + 4 + (0x14 << 2)
    ctx->pc = 0x1D3820u;
    {
        const bool branch_taken_0x1d3820 = (GPR_U64(ctx, 2) == GPR_U64(ctx, 0));
        ctx->pc = 0x1D3824u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1D3820u;
        // 0x1d3824: 0x3c020030  lui         $v0, 0x30 (Delay Slot)
        SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)48 << 16));
        ctx->in_delay_slot = false;
        if (branch_taken_0x1d3820) {
            ctx->pc = 0x1D3874u;
            goto label_1d3874;
        }
    }
    ctx->pc = 0x1D3828u;
    // 0x1d3828: 0x16e00003  bnez        $s7, . + 4 + (0x3 << 2)
    ctx->pc = 0x1D3828u;
    {
        const bool branch_taken_0x1d3828 = (GPR_U64(ctx, 23) != GPR_U64(ctx, 0));
        ctx->pc = 0x1D382Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1D3828u;
        // 0x1d382c: 0x24020001  addiu       $v0, $zero, 0x1 (Delay Slot)
        SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 0), 1));
        ctx->in_delay_slot = false;
        if (branch_taken_0x1d3828) {
            ctx->pc = 0x1D3838u;
            goto label_1d3838;
        }
    }
    ctx->pc = 0x1D3830u;
    // 0x1d3830: 0x10000002  b           . + 4 + (0x2 << 2)
    ctx->pc = 0x1D3830u;
    {
        const bool branch_taken_0x1d3830 = (GPR_U64(ctx, 0) == GPR_U64(ctx, 0));
        ctx->pc = 0x1D3834u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1D3830u;
        // 0x1d3834: 0xae000030  sw          $zero, 0x30($s0) (Delay Slot)
        WRITE32(ADD32(GPR_U32(ctx, 16), 48), GPR_U32(ctx, 0));
        ctx->in_delay_slot = false;
        if (branch_taken_0x1d3830) {
            ctx->pc = 0x1D383Cu;
            goto label_1d383c;
        }
    }
    ctx->pc = 0x1D3838u;
label_1d3838:
    // 0x1d3838: 0xae020030  sw          $v0, 0x30($s0)
    ctx->pc = 0x1d3838u;
    WRITE32(ADD32(GPR_U32(ctx, 16), 48), GPR_U32(ctx, 2));
label_1d383c:
    // 0x1d383c: 0x2402ffff  addiu       $v0, $zero, -0x1
    ctx->pc = 0x1d383cu;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 0), 4294967295));
    // 0x1d3840: 0x3c048000  lui         $a0, 0x8000
    ctx->pc = 0x1d3840u;
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)32768 << 16));
    // 0x1d3844: 0x8e280014  lw          $t0, 0x14($s1)
    ctx->pc = 0x1d3844u;
    SET_GPR_S32(ctx, 8, (int32_t)READ32(ADD32(GPR_U32(ctx, 17), 20)));
    // 0x1d3848: 0x2a0382d  daddu       $a3, $s5, $zero
    ctx->pc = 0x1d3848u;
    SET_GPR_U64(ctx, 7, (uint64_t)GPR_U64(ctx, 21) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1d384c: 0xae220008  sw          $v0, 0x8($s1)
    ctx->pc = 0x1d384cu;
    WRITE32(ADD32(GPR_U32(ctx, 17), 8), GPR_U32(ctx, 2));
    // 0x1d3850: 0x240482d  daddu       $t1, $s2, $zero
    ctx->pc = 0x1d3850u;
    SET_GPR_U64(ctx, 9, (uint64_t)GPR_U64(ctx, 18) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1d3854: 0x3484000a  ori         $a0, $a0, 0xA
    ctx->pc = 0x1d3854u;
    SET_GPR_U64(ctx, 4, GPR_U64(ctx, 4) | (uint64_t)(uint16_t)10);
    // 0x1d3858: 0x200282d  daddu       $a1, $s0, $zero
    ctx->pc = 0x1d3858u;
    SET_GPR_U64(ctx, 5, (uint64_t)GPR_U64(ctx, 16) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1d385c: 0xc074acc  jal         func_1D2B30
    ctx->pc = 0x1D385Cu;
    SET_GPR_U32(ctx, 31, 0x1D3864u);
    ctx->pc = 0x1D3860u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1D385Cu;
    // 0x1d3860: 0x24060040  addiu       $a2, $zero, 0x40 (Delay Slot)
    SET_GPR_S32(ctx, 6, (int32_t)ADD32(GPR_U32(ctx, 0), 64));
    ctx->in_delay_slot = false;
    ctx->pc = 0x1D2B30u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1D2B30u, 0x1D385Cu, 0x1D3864u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1D3864u;
label_1d3864:
    // 0x1d3864: 0x14400026  bnez        $v0, . + 4 + (0x26 << 2)
    ctx->pc = 0x1D3864u;
    {
        const bool branch_taken_0x1d3864 = (GPR_U64(ctx, 2) != GPR_U64(ctx, 0));
        ctx->pc = 0x1D3868u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1D3864u;
        // 0x1d3868: 0x102d  daddu       $v0, $zero, $zero (Delay Slot)
        SET_GPR_U64(ctx, 2, (uint64_t)GPR_U64(ctx, 0) + (uint64_t)GPR_U64(ctx, 0));
        ctx->in_delay_slot = false;
        if (branch_taken_0x1d3864) {
            ctx->pc = 0x1D3900u;
            goto label_1d3900;
        }
    }
    ctx->pc = 0x1D386Cu;
    // 0x1d386c: 0x1000001b  b           . + 4 + (0x1B << 2)
    ctx->pc = 0x1D386Cu;
    {
        const bool branch_taken_0x1d386c = (GPR_U64(ctx, 0) == GPR_U64(ctx, 0));
        if (branch_taken_0x1d386c) {
            ctx->pc = 0x1D38DCu;
            goto label_1d38dc;
        }
    }
    ctx->pc = 0x1D3874u;
label_1d3874:
    // 0x1d3874: 0x24130001  addiu       $s3, $zero, 0x1
    ctx->pc = 0x1d3874u;
    SET_GPR_S32(ctx, 19, (int32_t)ADD32(GPR_U32(ctx, 0), 1));
    // 0x1d3878: 0x2442ab28  addiu       $v0, $v0, -0x54D8
    ctx->pc = 0x1d3878u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 4294945576));
    // 0x1d387c: 0xafa00008  sw          $zero, 0x8($sp)
    ctx->pc = 0x1d387cu;
    WRITE32(ADD32(GPR_U32(ctx, 29), 8), GPR_U32(ctx, 0));
    // 0x1d3880: 0xafa20014  sw          $v0, 0x14($sp)
    ctx->pc = 0x1d3880u;
    WRITE32(ADD32(GPR_U32(ctx, 29), 20), GPR_U32(ctx, 2));
    // 0x1d3884: 0x3a0202d  daddu       $a0, $sp, $zero
    ctx->pc = 0x1d3884u;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 29) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1d3888: 0xc073e58  jal         func_1CF960
    ctx->pc = 0x1D3888u;
    SET_GPR_U32(ctx, 31, 0x1D3890u);
    ctx->pc = 0x1D388Cu;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1D3888u;
    // 0x1d388c: 0xafb30004  sw          $s3, 0x4($sp) (Delay Slot)
    WRITE32(ADD32(GPR_U32(ctx, 29), 4), GPR_U32(ctx, 19));
    ctx->in_delay_slot = false;
    ctx->pc = 0x1CF960u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1CF960u, 0x1D3888u, 0x1D3890u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1D3890u;
label_1d3890:
    // 0x1d3890: 0x4410005  bgez        $v0, . + 4 + (0x5 << 2)
    ctx->pc = 0x1D3890u;
    {
        const bool branch_taken_0x1d3890 = (GPR_S32(ctx, 2) >= 0);
        ctx->pc = 0x1D3894u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1D3890u;
        // 0x1d3894: 0xae220008  sw          $v0, 0x8($s1) (Delay Slot)
        WRITE32(ADD32(GPR_U32(ctx, 17), 8), GPR_U32(ctx, 2));
        ctx->in_delay_slot = false;
        if (branch_taken_0x1d3890) {
            ctx->pc = 0x1D38A8u;
            goto label_1d38a8;
        }
    }
    ctx->pc = 0x1D3898u;
    // 0x1d3898: 0xc074c06  jal         func_1D3018
    ctx->pc = 0x1D3898u;
    SET_GPR_U32(ctx, 31, 0x1D38A0u);
    ctx->pc = 0x1D389Cu;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1D3898u;
    // 0x1d389c: 0x200202d  daddu       $a0, $s0, $zero (Delay Slot)
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 16) + (uint64_t)GPR_U64(ctx, 0));
    ctx->in_delay_slot = false;
    ctx->pc = 0x1D3018u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1D3018u, 0x1D3898u, 0x1D38A0u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1D38A0u;
label_1d38a0:
    // 0x1d38a0: 0x10000017  b           . + 4 + (0x17 << 2)
    ctx->pc = 0x1D38A0u;
    {
        const bool branch_taken_0x1d38a0 = (GPR_U64(ctx, 0) == GPR_U64(ctx, 0));
        ctx->pc = 0x1D38A4u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1D38A0u;
        // 0x1d38a4: 0x2402fffd  addiu       $v0, $zero, -0x3 (Delay Slot)
        SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 0), 4294967293));
        ctx->in_delay_slot = false;
        if (branch_taken_0x1d38a0) {
            ctx->pc = 0x1D3900u;
            goto label_1d3900;
        }
    }
    ctx->pc = 0x1D38A8u;
label_1d38a8:
    // 0x1d38a8: 0xae130030  sw          $s3, 0x30($s0)
    ctx->pc = 0x1d38a8u;
    WRITE32(ADD32(GPR_U32(ctx, 16), 48), GPR_U32(ctx, 19));
    // 0x1d38ac: 0x3c048000  lui         $a0, 0x8000
    ctx->pc = 0x1d38acu;
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)32768 << 16));
    // 0x1d38b0: 0x2a0382d  daddu       $a3, $s5, $zero
    ctx->pc = 0x1d38b0u;
    SET_GPR_U64(ctx, 7, (uint64_t)GPR_U64(ctx, 21) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1d38b4: 0x240482d  daddu       $t1, $s2, $zero
    ctx->pc = 0x1d38b4u;
    SET_GPR_U64(ctx, 9, (uint64_t)GPR_U64(ctx, 18) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1d38b8: 0x8e280014  lw          $t0, 0x14($s1)
    ctx->pc = 0x1d38b8u;
    SET_GPR_S32(ctx, 8, (int32_t)READ32(ADD32(GPR_U32(ctx, 17), 20)));
    // 0x1d38bc: 0x3484000a  ori         $a0, $a0, 0xA
    ctx->pc = 0x1d38bcu;
    SET_GPR_U64(ctx, 4, GPR_U64(ctx, 4) | (uint64_t)(uint16_t)10);
    // 0x1d38c0: 0x200282d  daddu       $a1, $s0, $zero
    ctx->pc = 0x1d38c0u;
    SET_GPR_U64(ctx, 5, (uint64_t)GPR_U64(ctx, 16) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1d38c4: 0xc074acc  jal         func_1D2B30
    ctx->pc = 0x1D38C4u;
    SET_GPR_U32(ctx, 31, 0x1D38CCu);
    ctx->pc = 0x1D38C8u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1D38C4u;
    // 0x1d38c8: 0x24060040  addiu       $a2, $zero, 0x40 (Delay Slot)
    SET_GPR_S32(ctx, 6, (int32_t)ADD32(GPR_U32(ctx, 0), 64));
    ctx->in_delay_slot = false;
    ctx->pc = 0x1D2B30u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1D2B30u, 0x1D38C4u, 0x1D38CCu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1D38CCu;
label_1d38cc:
    // 0x1d38cc: 0x14400007  bnez        $v0, . + 4 + (0x7 << 2)
    ctx->pc = 0x1D38CCu;
    {
        const bool branch_taken_0x1d38cc = (GPR_U64(ctx, 2) != GPR_U64(ctx, 0));
        if (branch_taken_0x1d38cc) {
            ctx->pc = 0x1D38ECu;
            goto label_1d38ec;
        }
    }
    ctx->pc = 0x1D38D4u;
    // 0x1d38d4: 0xc073e5c  jal         func_1CF970
    ctx->pc = 0x1D38D4u;
    SET_GPR_U32(ctx, 31, 0x1D38DCu);
    ctx->pc = 0x1D38D8u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1D38D4u;
    // 0x1d38d8: 0x8e240008  lw          $a0, 0x8($s1) (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 17), 8)));
    ctx->in_delay_slot = false;
    ctx->pc = 0x1CF970u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1CF970u, 0x1D38D4u, 0x1D38DCu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1D38DCu;
label_1d38dc:
    // 0x1d38dc: 0xc074c06  jal         func_1D3018
    ctx->pc = 0x1D38DCu;
    SET_GPR_U32(ctx, 31, 0x1D38E4u);
    ctx->pc = 0x1D38E0u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1D38DCu;
    // 0x1d38e0: 0x200202d  daddu       $a0, $s0, $zero (Delay Slot)
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 16) + (uint64_t)GPR_U64(ctx, 0));
    ctx->in_delay_slot = false;
    ctx->pc = 0x1D3018u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1D3018u, 0x1D38DCu, 0x1D38E4u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1D38E4u;
label_1d38e4:
    // 0x1d38e4: 0x10000006  b           . + 4 + (0x6 << 2)
    ctx->pc = 0x1D38E4u;
    {
        const bool branch_taken_0x1d38e4 = (GPR_U64(ctx, 0) == GPR_U64(ctx, 0));
        ctx->pc = 0x1D38E8u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1D38E4u;
        // 0x1d38e8: 0x2402fffe  addiu       $v0, $zero, -0x2 (Delay Slot)
        SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 0), 4294967294));
        ctx->in_delay_slot = false;
        if (branch_taken_0x1d38e4) {
            ctx->pc = 0x1D3900u;
            goto label_1d3900;
        }
    }
    ctx->pc = 0x1D38ECu;
label_1d38ec:
    // 0x1d38ec: 0xc073e68  jal         func_1CF9A0
    ctx->pc = 0x1D38ECu;
    SET_GPR_U32(ctx, 31, 0x1D38F4u);
    ctx->pc = 0x1D38F0u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1D38ECu;
    // 0x1d38f0: 0x8e240008  lw          $a0, 0x8($s1) (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 17), 8)));
    ctx->in_delay_slot = false;
    // Best-effort completion signal -- same rationale/verified fix as
    // FUN_00166e98_0x166e98.cpp. This is the wait right after the big
    // per-entry loop (FUN_001d09d8, ~65k legitimate iterations, confirmed
    // via live diagnostics to be real forward progress, not a retry bug)
    // finishes -- one final synchronous IOP round-trip whose async response
    // isn't emulated.
    ps2_syscalls::SignalSema(rdram, ctx, runtime);
    ctx->pc = 0x1CF9A0u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1CF9A0u, 0x1D38ECu, 0x1D38F4u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1D38F4u;
label_1d38f4:
    // 0x1d38f4: 0xc073e5c  jal         func_1CF970
    ctx->pc = 0x1D38F4u;
    SET_GPR_U32(ctx, 31, 0x1D38FCu);
    ctx->pc = 0x1D38F8u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1D38F4u;
    // 0x1d38f8: 0x8e240008  lw          $a0, 0x8($s1) (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 17), 8)));
    ctx->in_delay_slot = false;
    ctx->pc = 0x1CF970u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1CF970u, 0x1D38F4u, 0x1D38FCu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1D38FCu;
label_1d38fc:
    // 0x1d38fc: 0x102d  daddu       $v0, $zero, $zero
    ctx->pc = 0x1d38fcu;
    SET_GPR_U64(ctx, 2, (uint64_t)GPR_U64(ctx, 0) + (uint64_t)GPR_U64(ctx, 0));
label_1d3900:
    // 0x1d3900: 0xdfbf00b0  ld          $ra, 0xB0($sp)
    ctx->pc = 0x1d3900u;
    SET_GPR_U64(ctx, 31, READ64(ADD32(GPR_U32(ctx, 29), 176)));
    // 0x1d3904: 0xdfbe00a0  ld          $fp, 0xA0($sp)
    ctx->pc = 0x1d3904u;
    SET_GPR_U64(ctx, 30, READ64(ADD32(GPR_U32(ctx, 29), 160)));
    // 0x1d3908: 0xdfb70090  ld          $s7, 0x90($sp)
    ctx->pc = 0x1d3908u;
    SET_GPR_U64(ctx, 23, READ64(ADD32(GPR_U32(ctx, 29), 144)));
    // 0x1d390c: 0xdfb60080  ld          $s6, 0x80($sp)
    ctx->pc = 0x1d390cu;
    SET_GPR_U64(ctx, 22, READ64(ADD32(GPR_U32(ctx, 29), 128)));
    // 0x1d3910: 0xdfb50070  ld          $s5, 0x70($sp)
    ctx->pc = 0x1d3910u;
    SET_GPR_U64(ctx, 21, READ64(ADD32(GPR_U32(ctx, 29), 112)));
    // 0x1d3914: 0xdfb40060  ld          $s4, 0x60($sp)
    ctx->pc = 0x1d3914u;
    SET_GPR_U64(ctx, 20, READ64(ADD32(GPR_U32(ctx, 29), 96)));
    // 0x1d3918: 0xdfb30050  ld          $s3, 0x50($sp)
    ctx->pc = 0x1d3918u;
    SET_GPR_U64(ctx, 19, READ64(ADD32(GPR_U32(ctx, 29), 80)));
    // 0x1d391c: 0xdfb20040  ld          $s2, 0x40($sp)
    ctx->pc = 0x1d391cu;
    SET_GPR_U64(ctx, 18, READ64(ADD32(GPR_U32(ctx, 29), 64)));
    // 0x1d3920: 0xdfb10030  ld          $s1, 0x30($sp)
    ctx->pc = 0x1d3920u;
    SET_GPR_U64(ctx, 17, READ64(ADD32(GPR_U32(ctx, 29), 48)));
    // 0x1d3924: 0xdfb00020  ld          $s0, 0x20($sp)
    ctx->pc = 0x1d3924u;
    SET_GPR_U64(ctx, 16, READ64(ADD32(GPR_U32(ctx, 29), 32)));
    // 0x1d3928: 0x3e00008  jr          $ra
    ctx->pc = 0x1D3928u;
    {
        const uint32_t jumpTarget = GPR_U32(ctx, 31);
        ctx->pc = 0x1D392Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1D3928u;
        // 0x1d392c: 0x27bd00c0  addiu       $sp, $sp, 0xC0 (Delay Slot)
        SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 192));
        ctx->in_delay_slot = false;
        ctx->pc = jumpTarget;
        #if defined(PS2X_STRICT_RETURN_DIAGNOSTICS) && PS2X_STRICT_RETURN_DIAGNOSTICS
        (void)runtime->dispatchGuestBranch(rdram, ctx, jumpTarget, 0x1D3928u, 0u, PS2Runtime::GuestBranchKind::Return, "JR $ra");
        return;
        #else
        ctx->pc = jumpTarget;
        return;
        #endif
    }
    ctx->pc = 0x1D3930u;
}
