#include <stdexcept>
#include <iostream>
#include "ps2_runtime_macros.h"
#include "ps2_runtime.h"
#include "ps2_recompiled_functions.h"
#include "ps2_recompiled_stubs.h"

#include "ps2_syscalls.h"
#include "ps2_stubs.h"

#ifdef PS2_FUNCTION_LOG_TRACKER
#include "ps2_log.h"
#endif

// Function: sub_001C26E8
// Address: 0x1c26e8 - 0x1c2758
void sub_001C26E8_0x1c26e8(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
#ifdef PS2_FUNCTION_LOG_TRACKER
    PS_LOG_ENTRY("sub_001C26E8_0x1c26e8");
#endif

    switch (ctx->pc) {
        case 0x1c26e8u: goto label_1c26e8;
        case 0x1c26ecu: goto label_1c26ec;
        case 0x1c26f0u: goto label_1c26f0;
        case 0x1c26f4u: goto label_1c26f4;
        case 0x1c26f8u: goto label_1c26f8;
        case 0x1c26fcu: goto label_1c26fc;
        case 0x1c2700u: goto label_1c2700;
        case 0x1c2704u: goto label_1c2704;
        case 0x1c2708u: goto label_1c2708;
        case 0x1c270cu: goto label_1c270c;
        case 0x1c2710u: goto label_1c2710;
        case 0x1c2714u: goto label_1c2714;
        case 0x1c2718u: goto label_1c2718;
        case 0x1c271cu: goto label_1c271c;
        case 0x1c2720u: goto label_1c2720;
        case 0x1c2724u: goto label_1c2724;
        case 0x1c2728u: goto label_1c2728;
        case 0x1c272cu: goto label_1c272c;
        case 0x1c2730u: goto label_1c2730;
        case 0x1c2734u: goto label_1c2734;
        case 0x1c2738u: goto label_1c2738;
        case 0x1c273cu: goto label_1c273c;
        case 0x1c2740u: goto label_1c2740;
        case 0x1c2744u: goto label_1c2744;
        case 0x1c2748u: goto label_1c2748;
        case 0x1c274cu: goto label_1c274c;
        case 0x1c2750u: goto label_1c2750;
        case 0x1c2754u: goto label_1c2754;
        default: break;
    }

    ctx->pc = 0x1c26e8u;

label_1c26e8:
    // 0x1c26e8: 0x27bdffe0  addiu       $sp, $sp, -0x20
    ctx->pc = 0x1c26e8u;
    SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 4294967264));
label_1c26ec:
    // 0x1c26ec: 0x3c0f0026  lui         $t7, 0x26
    ctx->pc = 0x1c26ecu;
    SET_GPR_S32(ctx, 15, (int32_t)((uint32_t)38 << 16));
label_1c26f0:
    // 0x1c26f0: 0xffb00000  sd          $s0, 0x0($sp)
    ctx->pc = 0x1c26f0u;
    WRITE64(ADD32(GPR_U32(ctx, 29), 0), GPR_U64(ctx, 16));
label_1c26f4:
    // 0x1c26f4: 0x25efc8e0  addiu       $t7, $t7, -0x3720
    ctx->pc = 0x1c26f4u;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 15), 4294953184));
label_1c26f8:
    // 0x1c26f8: 0xffb10008  sd          $s1, 0x8($sp)
    ctx->pc = 0x1c26f8u;
    WRITE64(ADD32(GPR_U32(ctx, 29), 8), GPR_U64(ctx, 17));
label_1c26fc:
    // 0x1c26fc: 0x240effff  addiu       $t6, $zero, -0x1
    ctx->pc = 0x1c26fcu;
    SET_GPR_S32(ctx, 14, (int32_t)ADD32(GPR_U32(ctx, 0), 4294967295));
label_1c2700:
    // 0x1c2700: 0xffbf0010  sd          $ra, 0x10($sp)
    ctx->pc = 0x1c2700u;
    WRITE64(ADD32(GPR_U32(ctx, 29), 16), GPR_U64(ctx, 31));
label_1c2704:
    // 0x1c2704: 0x8dedfffc  lw          $t5, -0x4($t7)
    ctx->pc = 0x1c2704u;
    SET_GPR_S32(ctx, 13, (int32_t)READ32(ADD32(GPR_U32(ctx, 15), 4294967292)));
label_1c2708:
    // 0x1c2708: 0x11ae0008  beq         $t5, $t6, . + 4 + (0x8 << 2)
label_1c270c:
    if (ctx->pc == 0x1C270Cu) {
        ctx->pc = 0x1C270Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C2708u;
        // 0x1c270c: 0x25f0fffc  addiu       $s0, $t7, -0x4 (Delay Slot)
        SET_GPR_S32(ctx, 16, (int32_t)ADD32(GPR_U32(ctx, 15), 4294967292));
        ctx->in_delay_slot = false;
        ctx->pc = 0x1C2710u;
        goto label_1c2710;
    }
    ctx->pc = 0x1C2708u;
    {
        const bool branch_taken_0x1c2708 = (GPR_U64(ctx, 13) == GPR_U64(ctx, 14));
        ctx->pc = 0x1C270Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C2708u;
        // 0x1c270c: 0x25f0fffc  addiu       $s0, $t7, -0x4 (Delay Slot)
        SET_GPR_S32(ctx, 16, (int32_t)ADD32(GPR_U32(ctx, 15), 4294967292));
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c2708) {
            ctx->pc = 0x1C272Cu;
            goto label_1c272c;
        }
    }
    ctx->pc = 0x1C2710u;
label_1c2710:
    // 0x1c2710: 0x1a0702d  daddu       $t6, $t5, $zero
    ctx->pc = 0x1c2710u;
    SET_GPR_U64(ctx, 14, (uint64_t)GPR_U64(ctx, 13) + (uint64_t)GPR_U64(ctx, 0));
label_1c2714:
    // 0x1c2714: 0x2411ffff  addiu       $s1, $zero, -0x1
    ctx->pc = 0x1c2714u;
    SET_GPR_S32(ctx, 17, (int32_t)ADD32(GPR_U32(ctx, 0), 4294967295));
label_1c2718:
    // 0x1c2718: 0x1c0f809  jalr        $t6
label_1c271c:
    if (ctx->pc == 0x1C271Cu) {
        ctx->pc = 0x1C271Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C2718u;
        // 0x1c271c: 0x2610fffc  addiu       $s0, $s0, -0x4 (Delay Slot)
        SET_GPR_S32(ctx, 16, (int32_t)ADD32(GPR_U32(ctx, 16), 4294967292));
        ctx->in_delay_slot = false;
        ctx->pc = 0x1C2720u;
        goto label_1c2720;
    }
    ctx->pc = 0x1C2718u;
    {
        const uint32_t jumpTarget = GPR_U32(ctx, 14);
        std::cerr << "[DIAG:ctor-call] target=0x" << std::hex << jumpTarget
                  << " s0=0x" << GPR_U32(ctx, 16) << std::dec << std::endl;
        SET_GPR_U32(ctx, 31, 0x1C2720u);
        ctx->pc = 0x1C271Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C2718u;
        // 0x1c271c: 0x2610fffc  addiu       $s0, $s0, -0x4 (Delay Slot)
        SET_GPR_S32(ctx, 16, (int32_t)ADD32(GPR_U32(ctx, 16), 4294967292));
        ctx->in_delay_slot = false;
        ctx->pc = jumpTarget;
        if (!runtime->dispatchGuestBranch(rdram, ctx, jumpTarget, 0x1C2718u, 0x1C2720u, PS2Runtime::GuestBranchKind::IndirectCall, "JALR")) {
            return;
        }
    }
    ctx->pc = 0x1C2720u;
label_1c2720:
    std::cerr << "[DIAG:loopcheck] sp=0x" << std::hex << GPR_U32(ctx, 29)
              << " s0=0x" << GPR_U32(ctx, 16) << " ra=0x" << GPR_U32(ctx, 31) << std::dec << std::endl;
    // 0x1c2720: 0x8e0f0000  lw          $t7, 0x0($s0)
    ctx->pc = 0x1c2720u;
    SET_GPR_S32(ctx, 15, (int32_t)READ32(ADD32(GPR_U32(ctx, 16), 0)));
label_1c2724:
    // 0x1c2724: 0x15f1fffc  bne         $t7, $s1, . + 4 + (-0x4 << 2)
label_1c2728:
    if (ctx->pc == 0x1C2728u) {
        ctx->pc = 0x1C2728u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C2724u;
        // 0x1c2728: 0x1e0702d  daddu       $t6, $t7, $zero (Delay Slot)
        SET_GPR_U64(ctx, 14, (uint64_t)GPR_U64(ctx, 15) + (uint64_t)GPR_U64(ctx, 0));
        ctx->in_delay_slot = false;
        ctx->pc = 0x1C272Cu;
        goto label_1c272c;
    }
    ctx->pc = 0x1C2724u;
    {
        const bool branch_taken_0x1c2724 = (GPR_U64(ctx, 15) != GPR_U64(ctx, 17));
        ctx->pc = 0x1C2728u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C2724u;
        // 0x1c2728: 0x1e0702d  daddu       $t6, $t7, $zero (Delay Slot)
        SET_GPR_U64(ctx, 14, (uint64_t)GPR_U64(ctx, 15) + (uint64_t)GPR_U64(ctx, 0));
        ctx->in_delay_slot = false;
        if (branch_taken_0x1c2724) {
            ctx->pc = 0x1C2718u;
            if (runtime->shouldPreemptGuestExecution()) {
                return;
            }
            goto label_1c2718;
        }
    }
    ctx->pc = 0x1C272Cu;
label_1c272c:
    // 0x1c272c: 0xdfb00000  ld          $s0, 0x0($sp)
    ctx->pc = 0x1c272cu;
    SET_GPR_U64(ctx, 16, READ64(ADD32(GPR_U32(ctx, 29), 0)));
label_1c2730:
    // 0x1c2730: 0xdfb10008  ld          $s1, 0x8($sp)
    ctx->pc = 0x1c2730u;
    SET_GPR_U64(ctx, 17, READ64(ADD32(GPR_U32(ctx, 29), 8)));
label_1c2734:
    // 0x1c2734: 0xdfbf0010  ld          $ra, 0x10($sp)
    ctx->pc = 0x1c2734u;
    SET_GPR_U64(ctx, 31, READ64(ADD32(GPR_U32(ctx, 29), 16)));
label_1c2738:
    // 0x1c2738: 0x3e00008  jr          $ra
label_1c273c:
    if (ctx->pc == 0x1C273Cu) {
        ctx->pc = 0x1C273Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C2738u;
        // 0x1c273c: 0x27bd0020  addiu       $sp, $sp, 0x20 (Delay Slot)
        SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 32));
        ctx->in_delay_slot = false;
        ctx->pc = 0x1C2740u;
        goto label_1c2740;
    }
    ctx->pc = 0x1C2738u;
    {
        const uint32_t jumpTarget = GPR_U32(ctx, 31);
        ctx->pc = 0x1C273Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C2738u;
        // 0x1c273c: 0x27bd0020  addiu       $sp, $sp, 0x20 (Delay Slot)
        SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 32));
        ctx->in_delay_slot = false;
        ctx->pc = jumpTarget;
        #if defined(PS2X_STRICT_RETURN_DIAGNOSTICS) && PS2X_STRICT_RETURN_DIAGNOSTICS
        (void)runtime->dispatchGuestBranch(rdram, ctx, jumpTarget, 0x1C2738u, 0u, PS2Runtime::GuestBranchKind::Return, "JR $ra");
        return;
        #else
        ctx->pc = jumpTarget;
        return;
        #endif
    }
    ctx->pc = 0x1C2740u;
label_1c2740:
    // 0x1c2740: 0x27bdfff0  addiu       $sp, $sp, -0x10
    ctx->pc = 0x1c2740u;
    SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 4294967280));
label_1c2744:
    // 0x1c2744: 0xffbf0000  sd          $ra, 0x0($sp)
    ctx->pc = 0x1c2744u;
    WRITE64(ADD32(GPR_U32(ctx, 29), 0), GPR_U64(ctx, 31));
label_1c2748:
    // 0x1c2748: 0xdfbf0000  ld          $ra, 0x0($sp)
    ctx->pc = 0x1c2748u;
    SET_GPR_U64(ctx, 31, READ64(ADD32(GPR_U32(ctx, 29), 0)));
label_1c274c:
    // 0x1c274c: 0x3e00008  jr          $ra
label_1c2750:
    if (ctx->pc == 0x1C2750u) {
        ctx->pc = 0x1C2750u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C274Cu;
        // 0x1c2750: 0x27bd0010  addiu       $sp, $sp, 0x10 (Delay Slot)
        SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 16));
        ctx->in_delay_slot = false;
        ctx->pc = 0x1C2754u;
        goto label_1c2754;
    }
    ctx->pc = 0x1C274Cu;
    {
        const uint32_t jumpTarget = GPR_U32(ctx, 31);
        ctx->pc = 0x1C2750u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1C274Cu;
        // 0x1c2750: 0x27bd0010  addiu       $sp, $sp, 0x10 (Delay Slot)
        SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 16));
        ctx->in_delay_slot = false;
        ctx->pc = jumpTarget;
        #if defined(PS2X_STRICT_RETURN_DIAGNOSTICS) && PS2X_STRICT_RETURN_DIAGNOSTICS
        (void)runtime->dispatchGuestBranch(rdram, ctx, jumpTarget, 0x1C274Cu, 0u, PS2Runtime::GuestBranchKind::Return, "JR $ra");
        return;
        #else
        ctx->pc = jumpTarget;
        return;
        #endif
    }
    ctx->pc = 0x1C2754u;
label_1c2754:
    // 0x1c2754: 0x0  nop
    ctx->pc = 0x1c2754u;
    // NOP
    ctx->pc = 0x1c2758u;
}
