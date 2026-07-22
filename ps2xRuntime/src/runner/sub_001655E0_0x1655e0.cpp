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

// Function: sub_001655E0
// Address: 0x1655e0 - 0x165940
void sub_001655E0_0x1655e0(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
#ifdef PS2_FUNCTION_LOG_TRACKER
    PS_LOG_ENTRY("sub_001655E0_0x1655e0");
#endif

    switch (ctx->pc) {
        case 0x16560cu: goto label_16560c;
        case 0x165620u: goto label_165620;
        case 0x165630u: goto label_165630;
        case 0x165664u: goto label_165664;
        case 0x1656a8u: goto label_1656a8;
        case 0x1656c0u: goto label_1656c0;
        case 0x165714u: goto label_165714;
        case 0x165730u: goto label_165730;
        case 0x165770u: goto label_165770;
        case 0x165778u: goto label_165778;
        case 0x16578cu: goto label_16578c;
        case 0x1657b4u: goto label_1657b4;
        case 0x1657ecu: goto label_1657ec;
        case 0x1657f8u: goto label_1657f8;
        case 0x165800u: goto label_165800;
        case 0x1658acu: goto label_1658ac;
        case 0x1658ccu: goto label_1658cc;
        case 0x1658ecu: goto label_1658ec;
        case 0x16590cu: goto label_16590c;
        default: break;
    }

    ctx->pc = 0x1655e0u;

    // 0x1655e0: 0x27bdffc0  addiu       $sp, $sp, -0x40
    ctx->pc = 0x1655e0u;
    SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 4294967232));
    // 0x1655e4: 0xffbe0030  sd          $fp, 0x30($sp)
    ctx->pc = 0x1655e4u;
    WRITE64(ADD32(GPR_U32(ctx, 29), 48), GPR_U64(ctx, 30));
    // 0x1655e8: 0xffbf0038  sd          $ra, 0x38($sp)
    ctx->pc = 0x1655e8u;
    WRITE64(ADD32(GPR_U32(ctx, 29), 56), GPR_U64(ctx, 31));
    // 0x1655ec: 0x3a0f02d  daddu       $fp, $sp, $zero
    ctx->pc = 0x1655ecu;
    SET_GPR_U64(ctx, 30, (uint64_t)GPR_U64(ctx, 29) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1655f0: 0xafc40010  sw          $a0, 0x10($fp)
    ctx->pc = 0x1655f0u;
    WRITE32(ADD32(GPR_U32(ctx, 30), 16), GPR_U32(ctx, 4));
    // 0x1655f4: 0xafc50014  sw          $a1, 0x14($fp)
    ctx->pc = 0x1655f4u;
    WRITE32(ADD32(GPR_U32(ctx, 30), 20), GPR_U32(ctx, 5));
    // 0x1655f8: 0xafc60018  sw          $a2, 0x18($fp)
    ctx->pc = 0x1655f8u;
    WRITE32(ADD32(GPR_U32(ctx, 30), 24), GPR_U32(ctx, 6));
    // 0x1655fc: 0xafc7001c  sw          $a3, 0x1C($fp)
    ctx->pc = 0x1655fcu;
    WRITE32(ADD32(GPR_U32(ctx, 30), 28), GPR_U32(ctx, 7));
    // 0x165600: 0x3c0f0030  lui         $t7, 0x30
    ctx->pc = 0x165600u;
    SET_GPR_S32(ctx, 15, (int32_t)((uint32_t)48 << 16));
    // 0x165604: 0x25ef7900  addiu       $t7, $t7, 0x7900
    ctx->pc = 0x165604u;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 15), 30976));
    // 0x165608: 0xafcf0024  sw          $t7, 0x24($fp)
    ctx->pc = 0x165608u;
    WRITE32(ADD32(GPR_U32(ctx, 30), 36), GPR_U32(ctx, 15));
label_16560c:
    // 0x16560c: 0x8fcf0024  lw          $t7, 0x24($fp)
    ctx->pc = 0x16560cu;
    SET_GPR_S32(ctx, 15, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 36)));
    // 0x165610: 0x25ef0064  addiu       $t7, $t7, 0x64
    ctx->pc = 0x165610u;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 15), 100));
    // 0x165614: 0x1e0202d  daddu       $a0, $t7, $zero
    ctx->pc = 0x165614u;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 15) + (uint64_t)GPR_U64(ctx, 0));
    // 0x165618: 0xc074e4c  jal         func_1D3930
    ctx->pc = 0x165618u;
    SET_GPR_U32(ctx, 31, 0x165620u);
    ctx->pc = 0x1D3930u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1D3930u, 0x165618u, 0x165620u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x165620u;
label_165620:
    // 0x165620: 0x40782d  daddu       $t7, $v0, $zero
    ctx->pc = 0x165620u;
    SET_GPR_U64(ctx, 15, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x165624: 0x15e0fff9  bnez        $t7, . + 4 + (-0x7 << 2)
    ctx->pc = 0x165624u;
    {
        static thread_local int spinGuard_16560c = 0;
        const bool branch_taken_0x165624 = (GPR_U64(ctx, 15) != GPR_U64(ctx, 0));
        if (branch_taken_0x165624) {
            if (++spinGuard_16560c > 2000) {
                std::cerr << "[DIAG:spinbreak-16560c] forcing exit after " << spinGuard_16560c
                          << " spins" << std::endl;
                spinGuard_16560c = 0;
                SET_GPR_U64(ctx, 15, 0);
            } else {
                ctx->pc = 0x16560Cu;
                if (runtime->shouldPreemptGuestExecution()) {
                    return;
                }
                goto label_16560c;
            }
        } else {
            spinGuard_16560c = 0;
        }
    }
    ctx->pc = 0x16562Cu;
    // 0x16562c: 0x0  nop
    ctx->pc = 0x16562cu;
    // NOP
label_165630:
    // 0x165630: 0x8fcf0024  lw          $t7, 0x24($fp)
    ctx->pc = 0x165630u;
    SET_GPR_S32(ctx, 15, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 36)));
    // 0x165634: 0x25ef0064  addiu       $t7, $t7, 0x64
    ctx->pc = 0x165634u;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 15), 100));
    // 0x165638: 0xafa00000  sw          $zero, 0x0($sp)
    ctx->pc = 0x165638u;
    WRITE32(ADD32(GPR_U32(ctx, 29), 0), GPR_U32(ctx, 0));
    // 0x16563c: 0x1e0202d  daddu       $a0, $t7, $zero
    ctx->pc = 0x16563cu;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 15) + (uint64_t)GPR_U64(ctx, 0));
    // 0x165640: 0x8fc50010  lw          $a1, 0x10($fp)
    ctx->pc = 0x165640u;
    SET_GPR_S32(ctx, 5, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 16)));
    // 0x165644: 0x302d  daddu       $a2, $zero, $zero
    ctx->pc = 0x165644u;
    SET_GPR_U64(ctx, 6, (uint64_t)GPR_U64(ctx, 0) + (uint64_t)GPR_U64(ctx, 0));
    // 0x165648: 0x8fc70014  lw          $a3, 0x14($fp)
    ctx->pc = 0x165648u;
    SET_GPR_S32(ctx, 7, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 20)));
    // 0x16564c: 0x8fc80018  lw          $t0, 0x18($fp)
    ctx->pc = 0x16564cu;
    SET_GPR_S32(ctx, 8, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 24)));
    // 0x165650: 0x8fc90014  lw          $t1, 0x14($fp)
    ctx->pc = 0x165650u;
    SET_GPR_S32(ctx, 9, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 20)));
    // 0x165654: 0x8fca001c  lw          $t2, 0x1C($fp)
    ctx->pc = 0x165654u;
    SET_GPR_S32(ctx, 10, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 28)));
    // 0x165658: 0x582d  daddu       $t3, $zero, $zero
    ctx->pc = 0x165658u;
    SET_GPR_U64(ctx, 11, (uint64_t)GPR_U64(ctx, 0) + (uint64_t)GPR_U64(ctx, 0));
    // 0x16565c: 0xc074dcc  jal         func_1D3730
    ctx->pc = 0x16565Cu;
    SET_GPR_U32(ctx, 31, 0x165664u);
    ctx->pc = 0x1D3730u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1D3730u, 0x16565Cu, 0x165664u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x165664u;
label_165664:
    // 0x165664: 0x40782d  daddu       $t7, $v0, $zero
    ctx->pc = 0x165664u;
    SET_GPR_U64(ctx, 15, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x165668: 0xafcf0020  sw          $t7, 0x20($fp)
    ctx->pc = 0x165668u;
    WRITE32(ADD32(GPR_U32(ctx, 30), 32), GPR_U32(ctx, 15));
    // 0x16566c: 0x8fcf0020  lw          $t7, 0x20($fp)
    ctx->pc = 0x16566cu;
    SET_GPR_S32(ctx, 15, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 32)));
    // 0x165670: 0x15e00003  bnez        $t7, . + 4 + (0x3 << 2)
    ctx->pc = 0x165670u;
    {
        const bool branch_taken_0x165670 = (GPR_U64(ctx, 15) != GPR_U64(ctx, 0));
        if (branch_taken_0x165670) {
            ctx->pc = 0x165680u;
            goto label_165680;
        }
    }
    ctx->pc = 0x165678u;
    // 0x165678: 0x10000015  b           . + 4 + (0x15 << 2)
    ctx->pc = 0x165678u;
    {
        const bool branch_taken_0x165678 = (GPR_U64(ctx, 0) == GPR_U64(ctx, 0));
        if (branch_taken_0x165678) {
            ctx->pc = 0x1656D0u;
            goto label_1656d0;
        }
    }
    ctx->pc = 0x165680u;
label_165680:
    // 0x165680: 0x8fcf0020  lw          $t7, 0x20($fp)
    ctx->pc = 0x165680u;
    SET_GPR_S32(ctx, 15, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 32)));
    // 0x165684: 0x11e0ffea  beqz        $t7, . + 4 + (-0x16 << 2)
    ctx->pc = 0x165684u;
    {
        const bool branch_taken_0x165684 = (GPR_U64(ctx, 15) == GPR_U64(ctx, 0));
        if (branch_taken_0x165684) {
            ctx->pc = 0x165630u;
            if (runtime->shouldPreemptGuestExecution()) {
                return;
            }
            goto label_165630;
        }
    }
    ctx->pc = 0x16568Cu;
    // 0x16568c: 0x3c04002f  lui         $a0, 0x2F
    ctx->pc = 0x16568cu;
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)47 << 16));
    // 0x165690: 0x24844f38  addiu       $a0, $a0, 0x4F38
    ctx->pc = 0x165690u;
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 4), 20280));
    // 0x165694: 0x3c05002f  lui         $a1, 0x2F
    ctx->pc = 0x165694u;
    SET_GPR_S32(ctx, 5, (int32_t)((uint32_t)47 << 16));
    // 0x165698: 0x24a54f48  addiu       $a1, $a1, 0x4F48
    ctx->pc = 0x165698u;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 5), 20296));
    // 0x16569c: 0x240600c7  addiu       $a2, $zero, 0xC7
    ctx->pc = 0x16569cu;
    SET_GPR_S32(ctx, 6, (int32_t)ADD32(GPR_U32(ctx, 0), 199));
    // 0x1656a0: 0xc074934  jal         func_1D24D0
    ctx->pc = 0x1656A0u;
    SET_GPR_U32(ctx, 31, 0x1656A8u);
    ctx->pc = 0x1D24D0u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1D24D0u, 0x1656A0u, 0x1656A8u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1656A8u;
label_1656a8:
    // 0x1656a8: 0x3c04002f  lui         $a0, 0x2F
    ctx->pc = 0x1656a8u;
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)47 << 16));
    // 0x1656ac: 0x24844f58  addiu       $a0, $a0, 0x4F58
    ctx->pc = 0x1656acu;
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 4), 20312));
    // 0x1656b0: 0x3c05002f  lui         $a1, 0x2F
    ctx->pc = 0x1656b0u;
    SET_GPR_S32(ctx, 5, (int32_t)((uint32_t)47 << 16));
    // 0x1656b4: 0x24a54f70  addiu       $a1, $a1, 0x4F70
    ctx->pc = 0x1656b4u;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 5), 20336));
    // 0x1656b8: 0xc074934  jal         func_1D24D0
    ctx->pc = 0x1656B8u;
    SET_GPR_U32(ctx, 31, 0x1656C0u);
    ctx->pc = 0x1D24D0u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1D24D0u, 0x1656B8u, 0x1656C0u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1656C0u;
label_1656c0:
    // 0x1656c0: 0x0  nop
    ctx->pc = 0x1656c0u;
    // NOP
    // 0x1656c4: 0xd  break       0
    ctx->pc = 0x1656c4u;
    runtime->handleBreak(rdram, ctx);
    // 0x1656c8: 0x1000ffd9  b           . + 4 + (-0x27 << 2)
    ctx->pc = 0x1656C8u;
    {
        const bool branch_taken_0x1656c8 = (GPR_U64(ctx, 0) == GPR_U64(ctx, 0));
        if (branch_taken_0x1656c8) {
            ctx->pc = 0x165630u;
            if (runtime->shouldPreemptGuestExecution()) {
                return;
            }
            goto label_165630;
        }
    }
    ctx->pc = 0x1656D0u;
label_1656d0:
    // 0x1656d0: 0x8fcf0020  lw          $t7, 0x20($fp)
    ctx->pc = 0x1656d0u;
    SET_GPR_S32(ctx, 15, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 32)));
    // 0x1656d4: 0x1e0102d  daddu       $v0, $t7, $zero
    ctx->pc = 0x1656d4u;
    SET_GPR_U64(ctx, 2, (uint64_t)GPR_U64(ctx, 15) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1656d8: 0x3c0e82d  daddu       $sp, $fp, $zero
    ctx->pc = 0x1656d8u;
    SET_GPR_U64(ctx, 29, (uint64_t)GPR_U64(ctx, 30) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1656dc: 0xdfbe0030  ld          $fp, 0x30($sp)
    ctx->pc = 0x1656dcu;
    SET_GPR_U64(ctx, 30, READ64(ADD32(GPR_U32(ctx, 29), 48)));
    // 0x1656e0: 0xdfbf0038  ld          $ra, 0x38($sp)
    ctx->pc = 0x1656e0u;
    SET_GPR_U64(ctx, 31, READ64(ADD32(GPR_U32(ctx, 29), 56)));
    // 0x1656e4: 0x27bd0040  addiu       $sp, $sp, 0x40
    ctx->pc = 0x1656e4u;
    SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 64));
    // 0x1656e8: 0x3e00008  jr          $ra
    ctx->pc = 0x1656E8u;
    {
        const uint32_t jumpTarget = GPR_U32(ctx, 31);
        ctx->pc = jumpTarget;
        #if defined(PS2X_STRICT_RETURN_DIAGNOSTICS) && PS2X_STRICT_RETURN_DIAGNOSTICS
        (void)runtime->dispatchGuestBranch(rdram, ctx, jumpTarget, 0x1656E8u, 0u, PS2Runtime::GuestBranchKind::Return, "JR $ra");
        return;
        #else
        ctx->pc = jumpTarget;
        return;
        #endif
    }
    ctx->pc = 0x1656F0u;
    // 0x1656f0: 0x27bdffe0  addiu       $sp, $sp, -0x20
    ctx->pc = 0x1656f0u;
    SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 4294967264));
    // 0x1656f4: 0xffbe0010  sd          $fp, 0x10($sp)
    ctx->pc = 0x1656f4u;
    WRITE64(ADD32(GPR_U32(ctx, 29), 16), GPR_U64(ctx, 30));
    // 0x1656f8: 0xffbf0018  sd          $ra, 0x18($sp)
    ctx->pc = 0x1656f8u;
    WRITE64(ADD32(GPR_U32(ctx, 29), 24), GPR_U64(ctx, 31));
    // 0x1656fc: 0x3a0f02d  daddu       $fp, $sp, $zero
    ctx->pc = 0x1656fcu;
    SET_GPR_U64(ctx, 30, (uint64_t)GPR_U64(ctx, 29) + (uint64_t)GPR_U64(ctx, 0));
    // 0x165700: 0xafc40000  sw          $a0, 0x0($fp)
    ctx->pc = 0x165700u;
    WRITE32(ADD32(GPR_U32(ctx, 30), 0), GPR_U32(ctx, 4));
    // 0x165704: 0x8fcf0000  lw          $t7, 0x0($fp)
    ctx->pc = 0x165704u;
    SET_GPR_S32(ctx, 15, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 0)));
    // 0x165708: 0xafcf0004  sw          $t7, 0x4($fp)
    ctx->pc = 0x165708u;
    WRITE32(ADD32(GPR_U32(ctx, 30), 4), GPR_U32(ctx, 15));
    // 0x16570c: 0xc073e14  jal         func_1CF850
    ctx->pc = 0x16570Cu;
    SET_GPR_U32(ctx, 31, 0x165714u);
    ctx->pc = 0x1CF850u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1CF850u, 0x16570Cu, 0x165714u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x165714u;
label_165714:
    // 0x165714: 0x40702d  daddu       $t6, $v0, $zero
    ctx->pc = 0x165714u;
    SET_GPR_U64(ctx, 14, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x165718: 0x8fcf0004  lw          $t7, 0x4($fp)
    ctx->pc = 0x165718u;
    SET_GPR_S32(ctx, 15, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 4)));
    // 0x16571c: 0x25ef0008  addiu       $t7, $t7, 0x8
    ctx->pc = 0x16571cu;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 15), 8));
    // 0x165720: 0x1e0202d  daddu       $a0, $t7, $zero
    ctx->pc = 0x165720u;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 15) + (uint64_t)GPR_U64(ctx, 0));
    // 0x165724: 0x1c0282d  daddu       $a1, $t6, $zero
    ctx->pc = 0x165724u;
    SET_GPR_U64(ctx, 5, (uint64_t)GPR_U64(ctx, 14) + (uint64_t)GPR_U64(ctx, 0));
    // 0x165728: 0xc074e5c  jal         func_1D3970
    ctx->pc = 0x165728u;
    SET_GPR_U32(ctx, 31, 0x165730u);
    ctx->pc = 0x1D3970u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1D3970u, 0x165728u, 0x165730u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x165730u;
label_165730:
    // 0x165730: 0x8fcf0004  lw          $t7, 0x4($fp)
    ctx->pc = 0x165730u;
    SET_GPR_S32(ctx, 15, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 4)));
    // 0x165734: 0x25ee0020  addiu       $t6, $t7, 0x20
    ctx->pc = 0x165734u;
    SET_GPR_S32(ctx, 14, (int32_t)ADD32(GPR_U32(ctx, 15), 32));
    // 0x165738: 0x8fcf0004  lw          $t7, 0x4($fp)
    ctx->pc = 0x165738u;
    SET_GPR_S32(ctx, 15, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 4)));
    // 0x16573c: 0x25ef0008  addiu       $t7, $t7, 0x8
    ctx->pc = 0x16573cu;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 15), 8));
    // 0x165740: 0x1c0202d  daddu       $a0, $t6, $zero
    ctx->pc = 0x165740u;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 14) + (uint64_t)GPR_U64(ctx, 0));
    // 0x165744: 0x3c054e55  lui         $a1, 0x4E55
    ctx->pc = 0x165744u;
    SET_GPR_S32(ctx, 5, (int32_t)((uint32_t)20053 << 16));
    // 0x165748: 0x34a54645  ori         $a1, $a1, 0x4645
    ctx->pc = 0x165748u;
    SET_GPR_U64(ctx, 5, GPR_U64(ctx, 5) | (uint64_t)(uint16_t)17989);
    // 0x16574c: 0x3c060016  lui         $a2, 0x16
    ctx->pc = 0x16574cu;
    SET_GPR_S32(ctx, 6, (int32_t)((uint32_t)22 << 16));
    // 0x165750: 0x24c65818  addiu       $a2, $a2, 0x5818
    ctx->pc = 0x165750u;
    SET_GPR_S32(ctx, 6, (int32_t)ADD32(GPR_U32(ctx, 6), 22552));
    // 0x165754: 0x3c070030  lui         $a3, 0x30
    ctx->pc = 0x165754u;
    SET_GPR_S32(ctx, 7, (int32_t)((uint32_t)48 << 16));
    // 0x165758: 0x24e77880  addiu       $a3, $a3, 0x7880
    ctx->pc = 0x165758u;
    SET_GPR_S32(ctx, 7, (int32_t)ADD32(GPR_U32(ctx, 7), 30848));
    // 0x16575c: 0x402d  daddu       $t0, $zero, $zero
    ctx->pc = 0x16575cu;
    SET_GPR_U64(ctx, 8, (uint64_t)GPR_U64(ctx, 0) + (uint64_t)GPR_U64(ctx, 0));
    // 0x165760: 0x482d  daddu       $t1, $zero, $zero
    ctx->pc = 0x165760u;
    SET_GPR_U64(ctx, 9, (uint64_t)GPR_U64(ctx, 0) + (uint64_t)GPR_U64(ctx, 0));
    // 0x165764: 0x1e0502d  daddu       $t2, $t7, $zero
    ctx->pc = 0x165764u;
    SET_GPR_U64(ctx, 10, (uint64_t)GPR_U64(ctx, 15) + (uint64_t)GPR_U64(ctx, 0));
    // 0x165768: 0xc074e82  jal         func_1D3A08
    ctx->pc = 0x165768u;
    SET_GPR_U32(ctx, 31, 0x165770u);
    ctx->pc = 0x1D3A08u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1D3A08u, 0x165768u, 0x165770u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x165770u;
label_165770:
    // 0x165770: 0xc073e20  jal         func_1CF880
    ctx->pc = 0x165770u;
    SET_GPR_U32(ctx, 31, 0x165778u);
    ctx->pc = 0x1CF880u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1CF880u, 0x165770u, 0x165778u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x165778u;
label_165778:
    // 0x165778: 0x8fcf0004  lw          $t7, 0x4($fp)
    ctx->pc = 0x165778u;
    SET_GPR_S32(ctx, 15, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 4)));
    // 0x16577c: 0x25ef0008  addiu       $t7, $t7, 0x8
    ctx->pc = 0x16577cu;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 15), 8));
    // 0x165780: 0x1e0202d  daddu       $a0, $t7, $zero
    ctx->pc = 0x165780u;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 15) + (uint64_t)GPR_U64(ctx, 0));
    // 0x165784: 0xc074f00  jal         func_1D3C00
    ctx->pc = 0x165784u;
    SET_GPR_U32(ctx, 31, 0x16578Cu);
    ctx->pc = 0x1D3C00u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1D3C00u, 0x165784u, 0x16578Cu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x16578Cu;
label_16578c:
    // 0x16578c: 0x40782d  daddu       $t7, $v0, $zero
    ctx->pc = 0x16578cu;
    SET_GPR_U64(ctx, 15, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x165790: 0xafcf0008  sw          $t7, 0x8($fp)
    ctx->pc = 0x165790u;
    WRITE32(ADD32(GPR_U32(ctx, 30), 8), GPR_U32(ctx, 15));
    // 0x165794: 0x8fcf0008  lw          $t7, 0x8($fp)
    ctx->pc = 0x165794u;
    SET_GPR_S32(ctx, 15, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 8)));
    // 0x165798: 0x15e00003  bnez        $t7, . + 4 + (0x3 << 2)
    ctx->pc = 0x165798u;
    {
        const bool branch_taken_0x165798 = (GPR_U64(ctx, 15) != GPR_U64(ctx, 0));
        if (branch_taken_0x165798) {
            ctx->pc = 0x1657A8u;
            goto label_1657a8;
        }
    }
    ctx->pc = 0x1657A0u;
    // 0x1657a0: 0x10000006  b           . + 4 + (0x6 << 2)
    ctx->pc = 0x1657A0u;
    {
        const bool branch_taken_0x1657a0 = (GPR_U64(ctx, 0) == GPR_U64(ctx, 0));
        if (branch_taken_0x1657a0) {
            ctx->pc = 0x1657BCu;
            goto label_1657bc;
        }
    }
    ctx->pc = 0x1657A8u;
label_1657a8:
    // 0x1657a8: 0x8fc40008  lw          $a0, 0x8($fp)
    ctx->pc = 0x1657a8u;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 8)));
    // 0x1657ac: 0xc074f16  jal         func_1D3C58
    ctx->pc = 0x1657ACu;
    SET_GPR_U32(ctx, 31, 0x1657B4u);
    ctx->pc = 0x1D3C58u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1D3C58u, 0x1657ACu, 0x1657B4u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1657B4u;
label_1657b4:
    // 0x1657b4: 0x1000fff0  b           . + 4 + (-0x10 << 2)
    ctx->pc = 0x1657B4u;
    {
        const bool branch_taken_0x1657b4 = (GPR_U64(ctx, 0) == GPR_U64(ctx, 0));
        if (branch_taken_0x1657b4) {
            ctx->pc = 0x165778u;
            if (runtime->shouldPreemptGuestExecution()) {
                return;
            }
            goto label_165778;
        }
    }
    ctx->pc = 0x1657BCu;
label_1657bc:
    // 0x1657bc: 0x8fcf0004  lw          $t7, 0x4($fp)
    ctx->pc = 0x1657bcu;
    SET_GPR_S32(ctx, 15, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 4)));
    // 0x1657c0: 0x8def0004  lw          $t7, 0x4($t7)
    ctx->pc = 0x1657c0u;
    SET_GPR_S32(ctx, 15, (int32_t)READ32(ADD32(GPR_U32(ctx, 15), 4)));
    // 0x1657c4: 0x15e0ffea  bnez        $t7, . + 4 + (-0x16 << 2)
    ctx->pc = 0x1657C4u;
    {
        const bool branch_taken_0x1657c4 = (GPR_U64(ctx, 15) != GPR_U64(ctx, 0));
        if (branch_taken_0x1657c4) {
            ctx->pc = 0x165770u;
            if (runtime->shouldPreemptGuestExecution()) {
                return;
            }
            goto label_165770;
        }
    }
    ctx->pc = 0x1657CCu;
    // 0x1657cc: 0x8fcf0004  lw          $t7, 0x4($fp)
    ctx->pc = 0x1657ccu;
    SET_GPR_S32(ctx, 15, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 4)));
    // 0x1657d0: 0x25ee0020  addiu       $t6, $t7, 0x20
    ctx->pc = 0x1657d0u;
    SET_GPR_S32(ctx, 14, (int32_t)ADD32(GPR_U32(ctx, 15), 32));
    // 0x1657d4: 0x8fcf0004  lw          $t7, 0x4($fp)
    ctx->pc = 0x1657d4u;
    SET_GPR_S32(ctx, 15, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 4)));
    // 0x1657d8: 0x25ef0008  addiu       $t7, $t7, 0x8
    ctx->pc = 0x1657d8u;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 15), 8));
    // 0x1657dc: 0x1c0202d  daddu       $a0, $t6, $zero
    ctx->pc = 0x1657dcu;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 14) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1657e0: 0x1e0282d  daddu       $a1, $t7, $zero
    ctx->pc = 0x1657e0u;
    SET_GPR_U64(ctx, 5, (uint64_t)GPR_U64(ctx, 15) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1657e4: 0xc074eb6  jal         func_1D3AD8
    ctx->pc = 0x1657E4u;
    SET_GPR_U32(ctx, 31, 0x1657ECu);
    ctx->pc = 0x1D3AD8u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1D3AD8u, 0x1657E4u, 0x1657ECu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1657ECu;
label_1657ec:
    // 0x1657ec: 0x8f84aa74  lw          $a0, -0x558C($gp)
    ctx->pc = 0x1657ecu;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 28), 4294945396)));
    // 0x1657f0: 0xc073e60  jal         func_1CF980
    ctx->pc = 0x1657F0u;
    SET_GPR_U32(ctx, 31, 0x1657F8u);
    ctx->pc = 0x1CF980u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1CF980u, 0x1657F0u, 0x1657F8u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1657F8u;
label_1657f8:
    // 0x1657f8: 0xc073de8  jal         func_1CF7A0
    ctx->pc = 0x1657F8u;
    SET_GPR_U32(ctx, 31, 0x165800u);
    ctx->pc = 0x1CF7A0u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1CF7A0u, 0x1657F8u, 0x165800u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x165800u;
label_165800:
    // 0x165800: 0x3c0e82d  daddu       $sp, $fp, $zero
    ctx->pc = 0x165800u;
    SET_GPR_U64(ctx, 29, (uint64_t)GPR_U64(ctx, 30) + (uint64_t)GPR_U64(ctx, 0));
    // 0x165804: 0xdfbe0010  ld          $fp, 0x10($sp)
    ctx->pc = 0x165804u;
    SET_GPR_U64(ctx, 30, READ64(ADD32(GPR_U32(ctx, 29), 16)));
    // 0x165808: 0xdfbf0018  ld          $ra, 0x18($sp)
    ctx->pc = 0x165808u;
    SET_GPR_U64(ctx, 31, READ64(ADD32(GPR_U32(ctx, 29), 24)));
    // 0x16580c: 0x27bd0020  addiu       $sp, $sp, 0x20
    ctx->pc = 0x16580cu;
    SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 32));
    // 0x165810: 0x3e00008  jr          $ra
    ctx->pc = 0x165810u;
    {
        const uint32_t jumpTarget = GPR_U32(ctx, 31);
        ctx->pc = jumpTarget;
        #if defined(PS2X_STRICT_RETURN_DIAGNOSTICS) && PS2X_STRICT_RETURN_DIAGNOSTICS
        (void)runtime->dispatchGuestBranch(rdram, ctx, jumpTarget, 0x165810u, 0u, PS2Runtime::GuestBranchKind::Return, "JR $ra");
        return;
        #else
        ctx->pc = jumpTarget;
        return;
        #endif
    }
    ctx->pc = 0x165818u;
    // 0x165818: 0x27bdffd0  addiu       $sp, $sp, -0x30
    ctx->pc = 0x165818u;
    SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 4294967248));
    // 0x16581c: 0xffbe0020  sd          $fp, 0x20($sp)
    ctx->pc = 0x16581cu;
    WRITE64(ADD32(GPR_U32(ctx, 29), 32), GPR_U64(ctx, 30));
    // 0x165820: 0xffbf0028  sd          $ra, 0x28($sp)
    ctx->pc = 0x165820u;
    WRITE64(ADD32(GPR_U32(ctx, 29), 40), GPR_U64(ctx, 31));
    // 0x165824: 0x3a0f02d  daddu       $fp, $sp, $zero
    ctx->pc = 0x165824u;
    SET_GPR_U64(ctx, 30, (uint64_t)GPR_U64(ctx, 29) + (uint64_t)GPR_U64(ctx, 0));
    // 0x165828: 0xafc40000  sw          $a0, 0x0($fp)
    ctx->pc = 0x165828u;
    WRITE32(ADD32(GPR_U32(ctx, 30), 0), GPR_U32(ctx, 4));
    // 0x16582c: 0xafc50004  sw          $a1, 0x4($fp)
    ctx->pc = 0x16582cu;
    WRITE32(ADD32(GPR_U32(ctx, 30), 4), GPR_U32(ctx, 5));
    // 0x165830: 0xafc60008  sw          $a2, 0x8($fp)
    ctx->pc = 0x165830u;
    WRITE32(ADD32(GPR_U32(ctx, 30), 8), GPR_U32(ctx, 6));
    // 0x165834: 0x8fce0000  lw          $t6, 0x0($fp)
    ctx->pc = 0x165834u;
    SET_GPR_S32(ctx, 14, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 0)));
    // 0x165838: 0xafce0010  sw          $t6, 0x10($fp)
    ctx->pc = 0x165838u;
    WRITE32(ADD32(GPR_U32(ctx, 30), 16), GPR_U32(ctx, 14));
    // 0x16583c: 0x240f0002  addiu       $t7, $zero, 0x2
    ctx->pc = 0x16583cu;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 0), 2));
    // 0x165840: 0x8fce0010  lw          $t6, 0x10($fp)
    ctx->pc = 0x165840u;
    SET_GPR_S32(ctx, 14, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 16)));
    // 0x165844: 0x11cf001d  beq         $t6, $t7, . + 4 + (0x1D << 2)
    ctx->pc = 0x165844u;
    {
        const bool branch_taken_0x165844 = (GPR_U64(ctx, 14) == GPR_U64(ctx, 15));
        if (branch_taken_0x165844) {
            ctx->pc = 0x1658BCu;
            goto label_1658bc;
        }
    }
    ctx->pc = 0x16584Cu;
    // 0x16584c: 0x8fce0010  lw          $t6, 0x10($fp)
    ctx->pc = 0x16584cu;
    SET_GPR_S32(ctx, 14, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 16)));
    // 0x165850: 0x2dcf0003  sltiu       $t7, $t6, 0x3
    ctx->pc = 0x165850u;
    SET_GPR_U64(ctx, 15, ((uint64_t)GPR_U64(ctx, 14) < (uint64_t)(int64_t)(int32_t)3) ? 1 : 0);
    // 0x165854: 0x11e00007  beqz        $t7, . + 4 + (0x7 << 2)
    ctx->pc = 0x165854u;
    {
        const bool branch_taken_0x165854 = (GPR_U64(ctx, 15) == GPR_U64(ctx, 0));
        if (branch_taken_0x165854) {
            ctx->pc = 0x165874u;
            goto label_165874;
        }
    }
    ctx->pc = 0x16585Cu;
    // 0x16585c: 0x240f0001  addiu       $t7, $zero, 0x1
    ctx->pc = 0x16585cu;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 0), 1));
    // 0x165860: 0x8fce0010  lw          $t6, 0x10($fp)
    ctx->pc = 0x165860u;
    SET_GPR_S32(ctx, 14, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 16)));
    // 0x165864: 0x11cf000d  beq         $t6, $t7, . + 4 + (0xD << 2)
    ctx->pc = 0x165864u;
    {
        const bool branch_taken_0x165864 = (GPR_U64(ctx, 14) == GPR_U64(ctx, 15));
        if (branch_taken_0x165864) {
            ctx->pc = 0x16589Cu;
            goto label_16589c;
        }
    }
    ctx->pc = 0x16586Cu;
    // 0x16586c: 0x1000002b  b           . + 4 + (0x2B << 2)
    ctx->pc = 0x16586Cu;
    {
        const bool branch_taken_0x16586c = (GPR_U64(ctx, 0) == GPR_U64(ctx, 0));
        if (branch_taken_0x16586c) {
            ctx->pc = 0x16591Cu;
            goto label_16591c;
        }
    }
    ctx->pc = 0x165874u;
label_165874:
    // 0x165874: 0x240f0003  addiu       $t7, $zero, 0x3
    ctx->pc = 0x165874u;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 0), 3));
    // 0x165878: 0x8fce0010  lw          $t6, 0x10($fp)
    ctx->pc = 0x165878u;
    SET_GPR_S32(ctx, 14, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 16)));
    // 0x16587c: 0x11cf0017  beq         $t6, $t7, . + 4 + (0x17 << 2)
    ctx->pc = 0x16587Cu;
    {
        const bool branch_taken_0x16587c = (GPR_U64(ctx, 14) == GPR_U64(ctx, 15));
        if (branch_taken_0x16587c) {
            ctx->pc = 0x1658DCu;
            goto label_1658dc;
        }
    }
    ctx->pc = 0x165884u;
    // 0x165884: 0x240f0004  addiu       $t7, $zero, 0x4
    ctx->pc = 0x165884u;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 0), 4));
    // 0x165888: 0x8fce0010  lw          $t6, 0x10($fp)
    ctx->pc = 0x165888u;
    SET_GPR_S32(ctx, 14, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 16)));
    // 0x16588c: 0x11cf001b  beq         $t6, $t7, . + 4 + (0x1B << 2)
    ctx->pc = 0x16588Cu;
    {
        const bool branch_taken_0x16588c = (GPR_U64(ctx, 14) == GPR_U64(ctx, 15));
        if (branch_taken_0x16588c) {
            ctx->pc = 0x1658FCu;
            goto label_1658fc;
        }
    }
    ctx->pc = 0x165894u;
    // 0x165894: 0x10000021  b           . + 4 + (0x21 << 2)
    ctx->pc = 0x165894u;
    {
        const bool branch_taken_0x165894 = (GPR_U64(ctx, 0) == GPR_U64(ctx, 0));
        if (branch_taken_0x165894) {
            ctx->pc = 0x16591Cu;
            goto label_16591c;
        }
    }
    ctx->pc = 0x16589Cu;
label_16589c:
    // 0x16589c: 0x8fc40004  lw          $a0, 0x4($fp)
    ctx->pc = 0x16589cu;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 4)));
    // 0x1658a0: 0x8fc50008  lw          $a1, 0x8($fp)
    ctx->pc = 0x1658a0u;
    SET_GPR_S32(ctx, 5, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 8)));
    // 0x1658a4: 0xc059650  jal         func_165940
    ctx->pc = 0x1658A4u;
    SET_GPR_U32(ctx, 31, 0x1658ACu);
    ctx->pc = 0x165940u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x165940u, 0x1658A4u, 0x1658ACu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1658ACu;
label_1658ac:
    // 0x1658ac: 0x40782d  daddu       $t7, $v0, $zero
    ctx->pc = 0x1658acu;
    SET_GPR_U64(ctx, 15, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1658b0: 0xafcf000c  sw          $t7, 0xC($fp)
    ctx->pc = 0x1658b0u;
    WRITE32(ADD32(GPR_U32(ctx, 30), 12), GPR_U32(ctx, 15));
    // 0x1658b4: 0x1000001b  b           . + 4 + (0x1B << 2)
    ctx->pc = 0x1658B4u;
    {
        const bool branch_taken_0x1658b4 = (GPR_U64(ctx, 0) == GPR_U64(ctx, 0));
        if (branch_taken_0x1658b4) {
            ctx->pc = 0x165924u;
            goto label_165924;
        }
    }
    ctx->pc = 0x1658BCu;
label_1658bc:
    // 0x1658bc: 0x8fc40004  lw          $a0, 0x4($fp)
    ctx->pc = 0x1658bcu;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 4)));
    // 0x1658c0: 0x8fc50008  lw          $a1, 0x8($fp)
    ctx->pc = 0x1658c0u;
    SET_GPR_S32(ctx, 5, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 8)));
    // 0x1658c4: 0xc059673  jal         func_1659CC
    ctx->pc = 0x1658C4u;
    SET_GPR_U32(ctx, 31, 0x1658CCu);
    ctx->pc = 0x1659CCu;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1659CCu, 0x1658C4u, 0x1658CCu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1658CCu;
label_1658cc:
    // 0x1658cc: 0x40782d  daddu       $t7, $v0, $zero
    ctx->pc = 0x1658ccu;
    SET_GPR_U64(ctx, 15, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1658d0: 0xafcf000c  sw          $t7, 0xC($fp)
    ctx->pc = 0x1658d0u;
    WRITE32(ADD32(GPR_U32(ctx, 30), 12), GPR_U32(ctx, 15));
    // 0x1658d4: 0x10000013  b           . + 4 + (0x13 << 2)
    ctx->pc = 0x1658D4u;
    {
        const bool branch_taken_0x1658d4 = (GPR_U64(ctx, 0) == GPR_U64(ctx, 0));
        if (branch_taken_0x1658d4) {
            ctx->pc = 0x165924u;
            goto label_165924;
        }
    }
    ctx->pc = 0x1658DCu;
label_1658dc:
    // 0x1658dc: 0x8fc40004  lw          $a0, 0x4($fp)
    ctx->pc = 0x1658dcu;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 4)));
    // 0x1658e0: 0x8fc50008  lw          $a1, 0x8($fp)
    ctx->pc = 0x1658e0u;
    SET_GPR_S32(ctx, 5, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 8)));
    // 0x1658e4: 0xc05968c  jal         func_165A30
    ctx->pc = 0x1658E4u;
    SET_GPR_U32(ctx, 31, 0x1658ECu);
    ctx->pc = 0x165A30u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x165A30u, 0x1658E4u, 0x1658ECu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1658ECu;
label_1658ec:
    // 0x1658ec: 0x40782d  daddu       $t7, $v0, $zero
    ctx->pc = 0x1658ecu;
    SET_GPR_U64(ctx, 15, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1658f0: 0xafcf000c  sw          $t7, 0xC($fp)
    ctx->pc = 0x1658f0u;
    WRITE32(ADD32(GPR_U32(ctx, 30), 12), GPR_U32(ctx, 15));
    // 0x1658f4: 0x1000000b  b           . + 4 + (0xB << 2)
    ctx->pc = 0x1658F4u;
    {
        const bool branch_taken_0x1658f4 = (GPR_U64(ctx, 0) == GPR_U64(ctx, 0));
        if (branch_taken_0x1658f4) {
            ctx->pc = 0x165924u;
            goto label_165924;
        }
    }
    ctx->pc = 0x1658FCu;
label_1658fc:
    // 0x1658fc: 0x8fc40004  lw          $a0, 0x4($fp)
    ctx->pc = 0x1658fcu;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 4)));
    // 0x165900: 0x8fc50008  lw          $a1, 0x8($fp)
    ctx->pc = 0x165900u;
    SET_GPR_S32(ctx, 5, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 8)));
    // 0x165904: 0xc0596ad  jal         func_165AB4
    ctx->pc = 0x165904u;
    SET_GPR_U32(ctx, 31, 0x16590Cu);
    ctx->pc = 0x165AB4u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x165AB4u, 0x165904u, 0x16590Cu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x16590Cu;
label_16590c:
    // 0x16590c: 0x40782d  daddu       $t7, $v0, $zero
    ctx->pc = 0x16590cu;
    SET_GPR_U64(ctx, 15, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x165910: 0xafcf000c  sw          $t7, 0xC($fp)
    ctx->pc = 0x165910u;
    WRITE32(ADD32(GPR_U32(ctx, 30), 12), GPR_U32(ctx, 15));
    // 0x165914: 0x10000003  b           . + 4 + (0x3 << 2)
    ctx->pc = 0x165914u;
    {
        const bool branch_taken_0x165914 = (GPR_U64(ctx, 0) == GPR_U64(ctx, 0));
        if (branch_taken_0x165914) {
            ctx->pc = 0x165924u;
            goto label_165924;
        }
    }
    ctx->pc = 0x16591Cu;
label_16591c:
    // 0x16591c: 0x8fcf0004  lw          $t7, 0x4($fp)
    ctx->pc = 0x16591cu;
    SET_GPR_S32(ctx, 15, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 4)));
    // 0x165920: 0xafcf000c  sw          $t7, 0xC($fp)
    ctx->pc = 0x165920u;
    WRITE32(ADD32(GPR_U32(ctx, 30), 12), GPR_U32(ctx, 15));
label_165924:
    // 0x165924: 0x8fc2000c  lw          $v0, 0xC($fp)
    ctx->pc = 0x165924u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 12)));
    // 0x165928: 0x3c0e82d  daddu       $sp, $fp, $zero
    ctx->pc = 0x165928u;
    SET_GPR_U64(ctx, 29, (uint64_t)GPR_U64(ctx, 30) + (uint64_t)GPR_U64(ctx, 0));
    // 0x16592c: 0xdfbe0020  ld          $fp, 0x20($sp)
    ctx->pc = 0x16592cu;
    SET_GPR_U64(ctx, 30, READ64(ADD32(GPR_U32(ctx, 29), 32)));
    // 0x165930: 0xdfbf0028  ld          $ra, 0x28($sp)
    ctx->pc = 0x165930u;
    SET_GPR_U64(ctx, 31, READ64(ADD32(GPR_U32(ctx, 29), 40)));
    // 0x165934: 0x27bd0030  addiu       $sp, $sp, 0x30
    ctx->pc = 0x165934u;
    SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 48));
    // 0x165938: 0x3e00008  jr          $ra
    ctx->pc = 0x165938u;
    {
        const uint32_t jumpTarget = GPR_U32(ctx, 31);
        ctx->pc = jumpTarget;
        #if defined(PS2X_STRICT_RETURN_DIAGNOSTICS) && PS2X_STRICT_RETURN_DIAGNOSTICS
        (void)runtime->dispatchGuestBranch(rdram, ctx, jumpTarget, 0x165938u, 0u, PS2Runtime::GuestBranchKind::Return, "JR $ra");
        return;
        #else
        ctx->pc = jumpTarget;
        return;
        #endif
    }
    ctx->pc = 0x165940u;
}
