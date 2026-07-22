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

// Function: FUN_0016739c
// Address: 0x16739c - 0x1674a0
void FUN_0016739c_0x16739c(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
#ifdef PS2_FUNCTION_LOG_TRACKER
    PS_LOG_ENTRY("FUN_0016739c_0x16739c");
#endif

    switch (ctx->pc) {
        case 0x1673e0u: goto label_1673e0;
        case 0x167404u: goto label_167404;
        case 0x167440u: goto label_167440;
        case 0x167470u: goto label_167470;
        case 0x16747cu: goto label_16747c;
        default: break;
    }

    ctx->pc = 0x16739cu;

    // 0x16739c: 0x27bdffa0  addiu       $sp, $sp, -0x60
    ctx->pc = 0x16739cu;
    SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 4294967200));
    // 0x1673a0: 0xffbe0050  sd          $fp, 0x50($sp)
    ctx->pc = 0x1673a0u;
    WRITE64(ADD32(GPR_U32(ctx, 29), 80), GPR_U64(ctx, 30));
    // 0x1673a4: 0xffbf0058  sd          $ra, 0x58($sp)
    ctx->pc = 0x1673a4u;
    WRITE64(ADD32(GPR_U32(ctx, 29), 88), GPR_U64(ctx, 31));
    // 0x1673a8: 0x3a0f02d  daddu       $fp, $sp, $zero
    ctx->pc = 0x1673a8u;
    SET_GPR_U64(ctx, 30, (uint64_t)GPR_U64(ctx, 29) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1673ac: 0xafc40000  sw          $a0, 0x0($fp)
    ctx->pc = 0x1673acu;
    WRITE32(ADD32(GPR_U32(ctx, 30), 0), GPR_U32(ctx, 4));
    // 0x1673b0: 0xafc50004  sw          $a1, 0x4($fp)
    ctx->pc = 0x1673b0u;
    WRITE32(ADD32(GPR_U32(ctx, 30), 4), GPR_U32(ctx, 5));
    // 0x1673b4: 0xafc60008  sw          $a2, 0x8($fp)
    ctx->pc = 0x1673b4u;
    WRITE32(ADD32(GPR_U32(ctx, 30), 8), GPR_U32(ctx, 6));
    // 0x1673b8: 0xafc00028  sw          $zero, 0x28($fp)
    ctx->pc = 0x1673b8u;
    WRITE32(ADD32(GPR_U32(ctx, 30), 40), GPR_U32(ctx, 0));
    // 0x1673bc: 0x240f0001  addiu       $t7, $zero, 0x1
    ctx->pc = 0x1673bcu;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 0), 1));
    // 0x1673c0: 0xafcf0024  sw          $t7, 0x24($fp)
    ctx->pc = 0x1673c0u;
    WRITE32(ADD32(GPR_U32(ctx, 30), 36), GPR_U32(ctx, 15));
    // 0x1673c4: 0x27cf0010  addiu       $t7, $fp, 0x10
    ctx->pc = 0x1673c4u;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 30), 16));
    // 0x1673c8: 0xafcf0034  sw          $t7, 0x34($fp)
    ctx->pc = 0x1673c8u;
    WRITE32(ADD32(GPR_U32(ctx, 30), 52), GPR_U32(ctx, 15));
    // 0x1673cc: 0xafc00030  sw          $zero, 0x30($fp)
    ctx->pc = 0x1673ccu;
    WRITE32(ADD32(GPR_U32(ctx, 30), 48), GPR_U32(ctx, 0));
    // 0x1673d0: 0x27cf0020  addiu       $t7, $fp, 0x20
    ctx->pc = 0x1673d0u;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 30), 32));
    // 0x1673d4: 0x1e0202d  daddu       $a0, $t7, $zero
    ctx->pc = 0x1673d4u;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 15) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1673d8: 0xc073e58  jal         func_1CF960
    ctx->pc = 0x1673D8u;
    SET_GPR_U32(ctx, 31, 0x1673E0u);
    ctx->pc = 0x1CF960u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1CF960u, 0x1673D8u, 0x1673E0u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1673E0u;
label_1673e0:
    // 0x1673e0: 0x40782d  daddu       $t7, $v0, $zero
    ctx->pc = 0x1673e0u;
    SET_GPR_U64(ctx, 15, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1673e4: 0xafcf0014  sw          $t7, 0x14($fp)
    ctx->pc = 0x1673e4u;
    WRITE32(ADD32(GPR_U32(ctx, 30), 20), GPR_U32(ctx, 15));
    // 0x1673e8: 0x8fcf0014  lw          $t7, 0x14($fp)
    ctx->pc = 0x1673e8u;
    SET_GPR_S32(ctx, 15, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 20)));
    // 0x1673ec: 0x1de0000c  bgtz        $t7, . + 4 + (0xC << 2)
    ctx->pc = 0x1673ECu;
    {
        const bool branch_taken_0x1673ec = (GPR_S32(ctx, 15) > 0);
        if (branch_taken_0x1673ec) {
            ctx->pc = 0x167420u;
            goto label_167420;
        }
    }
    ctx->pc = 0x1673F4u;
    // 0x1673f4: 0x3c04002f  lui         $a0, 0x2F
    ctx->pc = 0x1673f4u;
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)47 << 16));
    // 0x1673f8: 0x24844fb0  addiu       $a0, $a0, 0x4FB0
    ctx->pc = 0x1673f8u;
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 4), 20400));
    // 0x1673fc: 0xc074934  jal         func_1D24D0
    ctx->pc = 0x1673FCu;
    SET_GPR_U32(ctx, 31, 0x167404u);
    ctx->pc = 0x1D24D0u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1D24D0u, 0x1673FCu, 0x167404u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x167404u;
label_167404:
    // 0x167404: 0x3c0ffeff  lui         $t7, 0xFEFF
    ctx->pc = 0x167404u;
    SET_GPR_S32(ctx, 15, (int32_t)((uint32_t)65279 << 16));
    // 0x167408: 0xafcf0040  sw          $t7, 0x40($fp)
    ctx->pc = 0x167408u;
    WRITE32(ADD32(GPR_U32(ctx, 30), 64), GPR_U32(ctx, 15));
    // 0x16740c: 0x8fcf0040  lw          $t7, 0x40($fp)
    ctx->pc = 0x16740cu;
    SET_GPR_S32(ctx, 15, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 64)));
    // 0x167410: 0x35effff3  ori         $t7, $t7, 0xFFF3
    ctx->pc = 0x167410u;
    SET_GPR_U64(ctx, 15, GPR_U64(ctx, 15) | (uint64_t)(uint16_t)65523);
    // 0x167414: 0xafcf0040  sw          $t7, 0x40($fp)
    ctx->pc = 0x167414u;
    WRITE32(ADD32(GPR_U32(ctx, 30), 64), GPR_U32(ctx, 15));
    // 0x167418: 0x1000001a  b           . + 4 + (0x1A << 2)
    ctx->pc = 0x167418u;
    {
        const bool branch_taken_0x167418 = (GPR_U64(ctx, 0) == GPR_U64(ctx, 0));
        if (branch_taken_0x167418) {
            ctx->pc = 0x167484u;
            goto label_167484;
        }
    }
    ctx->pc = 0x167420u;
label_167420:
    // 0x167420: 0x8fc40000  lw          $a0, 0x0($fp)
    ctx->pc = 0x167420u;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 0)));
    // 0x167424: 0x8fc50004  lw          $a1, 0x4($fp)
    ctx->pc = 0x167424u;
    SET_GPR_S32(ctx, 5, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 4)));
    // 0x167428: 0x8fc60008  lw          $a2, 0x8($fp)
    ctx->pc = 0x167428u;
    SET_GPR_S32(ctx, 6, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 8)));
    // 0x16742c: 0x3c070017  lui         $a3, 0x17
    ctx->pc = 0x16742cu;
    SET_GPR_S32(ctx, 7, (int32_t)((uint32_t)23 << 16));
    // 0x167430: 0x24e79644  addiu       $a3, $a3, -0x69BC
    ctx->pc = 0x167430u;
    SET_GPR_S32(ctx, 7, (int32_t)ADD32(GPR_U32(ctx, 7), 4294940228));
    // 0x167434: 0x8fc80014  lw          $t0, 0x14($fp)
    ctx->pc = 0x167434u;
    SET_GPR_S32(ctx, 8, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 20)));
    // 0x167438: 0xc059d28  jal         func_1674A0
    ctx->pc = 0x167438u;
    SET_GPR_U32(ctx, 31, 0x167440u);
    ctx->pc = 0x1674A0u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1674A0u, 0x167438u, 0x167440u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x167440u;
label_167440:
    // 0x167440: 0x40782d  daddu       $t7, $v0, $zero
    ctx->pc = 0x167440u;
    SET_GPR_U64(ctx, 15, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x167444: 0xafcf000c  sw          $t7, 0xC($fp)
    ctx->pc = 0x167444u;
    WRITE32(ADD32(GPR_U32(ctx, 30), 12), GPR_U32(ctx, 15));
    // 0x167448: 0x8fcf000c  lw          $t7, 0xC($fp)
    ctx->pc = 0x167448u;
    SET_GPR_S32(ctx, 15, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 12)));
    // 0x16744c: 0x5e10005  bgez        $t7, . + 4 + (0x5 << 2)
    ctx->pc = 0x16744Cu;
    {
        const bool branch_taken_0x16744c = (GPR_S32(ctx, 15) >= 0);
        if (branch_taken_0x16744c) {
            ctx->pc = 0x167464u;
            goto label_167464;
        }
    }
    ctx->pc = 0x167454u;
    // 0x167454: 0x8fcf000c  lw          $t7, 0xC($fp)
    ctx->pc = 0x167454u;
    SET_GPR_S32(ctx, 15, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 12)));
    // 0x167458: 0xafcf0040  sw          $t7, 0x40($fp)
    ctx->pc = 0x167458u;
    WRITE32(ADD32(GPR_U32(ctx, 30), 64), GPR_U32(ctx, 15));
    // 0x16745c: 0x10000009  b           . + 4 + (0x9 << 2)
    ctx->pc = 0x16745Cu;
    {
        const bool branch_taken_0x16745c = (GPR_U64(ctx, 0) == GPR_U64(ctx, 0));
        if (branch_taken_0x16745c) {
            ctx->pc = 0x167484u;
            goto label_167484;
        }
    }
    ctx->pc = 0x167464u;
label_167464:
    // 0x167464: 0x8fc40014  lw          $a0, 0x14($fp)
    ctx->pc = 0x167464u;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 20)));
    // Best-effort completion signal -- same rationale/verified fix as
    // FUN_00166e98_0x166e98.cpp (identical CreateSema+submit+WaitSema+
    // DeleteSema idiom, IOP async response not emulated).
    ps2_syscalls::SignalSema(rdram, ctx, runtime);
    // 0x167468: 0xc073e68  jal         func_1CF9A0
    ctx->pc = 0x167468u;
    SET_GPR_U32(ctx, 31, 0x167470u);
    ctx->pc = 0x1CF9A0u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1CF9A0u, 0x167468u, 0x167470u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x167470u;
label_167470:
    // 0x167470: 0x8fc40014  lw          $a0, 0x14($fp)
    ctx->pc = 0x167470u;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 20)));
    // 0x167474: 0xc073e5c  jal         func_1CF970
    ctx->pc = 0x167474u;
    SET_GPR_U32(ctx, 31, 0x16747Cu);
    ctx->pc = 0x1CF970u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1CF970u, 0x167474u, 0x16747Cu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x16747Cu;
label_16747c:
    // 0x16747c: 0x8fcf0010  lw          $t7, 0x10($fp)
    ctx->pc = 0x16747cu;
    SET_GPR_S32(ctx, 15, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 16)));
    // 0x167480: 0xafcf0040  sw          $t7, 0x40($fp)
    ctx->pc = 0x167480u;
    WRITE32(ADD32(GPR_U32(ctx, 30), 64), GPR_U32(ctx, 15));
label_167484:
    // 0x167484: 0x8fc20040  lw          $v0, 0x40($fp)
    ctx->pc = 0x167484u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 64)));
    // 0x167488: 0x3c0e82d  daddu       $sp, $fp, $zero
    ctx->pc = 0x167488u;
    SET_GPR_U64(ctx, 29, (uint64_t)GPR_U64(ctx, 30) + (uint64_t)GPR_U64(ctx, 0));
    // 0x16748c: 0xdfbe0050  ld          $fp, 0x50($sp)
    ctx->pc = 0x16748cu;
    SET_GPR_U64(ctx, 30, READ64(ADD32(GPR_U32(ctx, 29), 80)));
    // 0x167490: 0xdfbf0058  ld          $ra, 0x58($sp)
    ctx->pc = 0x167490u;
    SET_GPR_U64(ctx, 31, READ64(ADD32(GPR_U32(ctx, 29), 88)));
    // 0x167494: 0x27bd0060  addiu       $sp, $sp, 0x60
    ctx->pc = 0x167494u;
    SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 96));
    // 0x167498: 0x3e00008  jr          $ra
    ctx->pc = 0x167498u;
    {
        const uint32_t jumpTarget = GPR_U32(ctx, 31);
        ctx->pc = jumpTarget;
        #if defined(PS2X_STRICT_RETURN_DIAGNOSTICS) && PS2X_STRICT_RETURN_DIAGNOSTICS
        (void)runtime->dispatchGuestBranch(rdram, ctx, jumpTarget, 0x167498u, 0u, PS2Runtime::GuestBranchKind::Return, "JR $ra");
        return;
        #else
        ctx->pc = jumpTarget;
        return;
        #endif
    }
    ctx->pc = 0x1674A0u;
}
