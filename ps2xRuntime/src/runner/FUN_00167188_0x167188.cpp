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

// Function: FUN_00167188
// Address: 0x167188 - 0x16727c
void FUN_00167188_0x167188(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
#ifdef PS2_FUNCTION_LOG_TRACKER
    PS_LOG_ENTRY("FUN_00167188_0x167188");
#endif

    switch (ctx->pc) {
        case 0x1671c4u: goto label_1671c4;
        case 0x1671e8u: goto label_1671e8;
        case 0x16721cu: goto label_16721c;
        case 0x16724cu: goto label_16724c;
        case 0x167258u: goto label_167258;
        default: break;
    }

    ctx->pc = 0x167188u;

    // 0x167188: 0x27bdffb0  addiu       $sp, $sp, -0x50
    ctx->pc = 0x167188u;
    SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 4294967216));
    // 0x16718c: 0xffbe0040  sd          $fp, 0x40($sp)
    ctx->pc = 0x16718cu;
    WRITE64(ADD32(GPR_U32(ctx, 29), 64), GPR_U64(ctx, 30));
    // 0x167190: 0xffbf0048  sd          $ra, 0x48($sp)
    ctx->pc = 0x167190u;
    WRITE64(ADD32(GPR_U32(ctx, 29), 72), GPR_U64(ctx, 31));
    // 0x167194: 0x3a0f02d  daddu       $fp, $sp, $zero
    ctx->pc = 0x167194u;
    SET_GPR_U64(ctx, 30, (uint64_t)GPR_U64(ctx, 29) + (uint64_t)GPR_U64(ctx, 0));
    // 0x167198: 0xafc40000  sw          $a0, 0x0($fp)
    ctx->pc = 0x167198u;
    WRITE32(ADD32(GPR_U32(ctx, 30), 0), GPR_U32(ctx, 4));
    // 0x16719c: 0xafc00018  sw          $zero, 0x18($fp)
    ctx->pc = 0x16719cu;
    WRITE32(ADD32(GPR_U32(ctx, 30), 24), GPR_U32(ctx, 0));
    // 0x1671a0: 0x240f0001  addiu       $t7, $zero, 0x1
    ctx->pc = 0x1671a0u;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 0), 1));
    // 0x1671a4: 0xafcf0014  sw          $t7, 0x14($fp)
    ctx->pc = 0x1671a4u;
    WRITE32(ADD32(GPR_U32(ctx, 30), 20), GPR_U32(ctx, 15));
    // 0x1671a8: 0x27cf0008  addiu       $t7, $fp, 0x8
    ctx->pc = 0x1671a8u;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 30), 8));
    // 0x1671ac: 0xafcf0024  sw          $t7, 0x24($fp)
    ctx->pc = 0x1671acu;
    WRITE32(ADD32(GPR_U32(ctx, 30), 36), GPR_U32(ctx, 15));
    // 0x1671b0: 0xafc00020  sw          $zero, 0x20($fp)
    ctx->pc = 0x1671b0u;
    WRITE32(ADD32(GPR_U32(ctx, 30), 32), GPR_U32(ctx, 0));
    // 0x1671b4: 0x27cf0010  addiu       $t7, $fp, 0x10
    ctx->pc = 0x1671b4u;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 30), 16));
    // 0x1671b8: 0x1e0202d  daddu       $a0, $t7, $zero
    ctx->pc = 0x1671b8u;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 15) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1671bc: 0xc073e58  jal         func_1CF960
    ctx->pc = 0x1671BCu;
    SET_GPR_U32(ctx, 31, 0x1671C4u);
    ctx->pc = 0x1CF960u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1CF960u, 0x1671BCu, 0x1671C4u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1671C4u;
label_1671c4:
    // 0x1671c4: 0x40782d  daddu       $t7, $v0, $zero
    ctx->pc = 0x1671c4u;
    SET_GPR_U64(ctx, 15, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1671c8: 0xafcf000c  sw          $t7, 0xC($fp)
    ctx->pc = 0x1671c8u;
    WRITE32(ADD32(GPR_U32(ctx, 30), 12), GPR_U32(ctx, 15));
    // 0x1671cc: 0x8fcf000c  lw          $t7, 0xC($fp)
    ctx->pc = 0x1671ccu;
    SET_GPR_S32(ctx, 15, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 12)));
    // 0x1671d0: 0x1de0000c  bgtz        $t7, . + 4 + (0xC << 2)
    ctx->pc = 0x1671D0u;
    {
        const bool branch_taken_0x1671d0 = (GPR_S32(ctx, 15) > 0);
        if (branch_taken_0x1671d0) {
            ctx->pc = 0x167204u;
            goto label_167204;
        }
    }
    ctx->pc = 0x1671D8u;
    // 0x1671d8: 0x3c04002f  lui         $a0, 0x2F
    ctx->pc = 0x1671d8u;
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)47 << 16));
    // 0x1671dc: 0x24844fb0  addiu       $a0, $a0, 0x4FB0
    ctx->pc = 0x1671dcu;
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 4), 20400));
    // 0x1671e0: 0xc074934  jal         func_1D24D0
    ctx->pc = 0x1671E0u;
    SET_GPR_U32(ctx, 31, 0x1671E8u);
    ctx->pc = 0x1D24D0u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1D24D0u, 0x1671E0u, 0x1671E8u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1671E8u;
label_1671e8:
    // 0x1671e8: 0x3c0ffeff  lui         $t7, 0xFEFF
    ctx->pc = 0x1671e8u;
    SET_GPR_S32(ctx, 15, (int32_t)((uint32_t)65279 << 16));
    // 0x1671ec: 0xafcf0030  sw          $t7, 0x30($fp)
    ctx->pc = 0x1671ecu;
    WRITE32(ADD32(GPR_U32(ctx, 30), 48), GPR_U32(ctx, 15));
    // 0x1671f0: 0x8fcf0030  lw          $t7, 0x30($fp)
    ctx->pc = 0x1671f0u;
    SET_GPR_S32(ctx, 15, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 48)));
    // 0x1671f4: 0x35effff3  ori         $t7, $t7, 0xFFF3
    ctx->pc = 0x1671f4u;
    SET_GPR_U64(ctx, 15, GPR_U64(ctx, 15) | (uint64_t)(uint16_t)65523);
    // 0x1671f8: 0xafcf0030  sw          $t7, 0x30($fp)
    ctx->pc = 0x1671f8u;
    WRITE32(ADD32(GPR_U32(ctx, 30), 48), GPR_U32(ctx, 15));
    // 0x1671fc: 0x10000018  b           . + 4 + (0x18 << 2)
    ctx->pc = 0x1671FCu;
    {
        const bool branch_taken_0x1671fc = (GPR_U64(ctx, 0) == GPR_U64(ctx, 0));
        if (branch_taken_0x1671fc) {
            ctx->pc = 0x167260u;
            goto label_167260;
        }
    }
    ctx->pc = 0x167204u;
label_167204:
    // 0x167204: 0x8fc40000  lw          $a0, 0x0($fp)
    ctx->pc = 0x167204u;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 0)));
    // 0x167208: 0x3c050017  lui         $a1, 0x17
    ctx->pc = 0x167208u;
    SET_GPR_S32(ctx, 5, (int32_t)((uint32_t)23 << 16));
    // 0x16720c: 0x24a59644  addiu       $a1, $a1, -0x69BC
    ctx->pc = 0x16720cu;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 5), 4294940228));
    // 0x167210: 0x8fc6000c  lw          $a2, 0xC($fp)
    ctx->pc = 0x167210u;
    SET_GPR_S32(ctx, 6, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 12)));
    // 0x167214: 0xc059c9f  jal         func_16727C
    ctx->pc = 0x167214u;
    SET_GPR_U32(ctx, 31, 0x16721Cu);
    ctx->pc = 0x16727Cu;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x16727Cu, 0x167214u, 0x16721Cu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x16721Cu;
label_16721c:
    // 0x16721c: 0x40782d  daddu       $t7, $v0, $zero
    ctx->pc = 0x16721cu;
    SET_GPR_U64(ctx, 15, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x167220: 0xafcf0004  sw          $t7, 0x4($fp)
    ctx->pc = 0x167220u;
    WRITE32(ADD32(GPR_U32(ctx, 30), 4), GPR_U32(ctx, 15));
    // 0x167224: 0x8fcf0004  lw          $t7, 0x4($fp)
    ctx->pc = 0x167224u;
    SET_GPR_S32(ctx, 15, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 4)));
    // 0x167228: 0x5e10005  bgez        $t7, . + 4 + (0x5 << 2)
    ctx->pc = 0x167228u;
    {
        const bool branch_taken_0x167228 = (GPR_S32(ctx, 15) >= 0);
        if (branch_taken_0x167228) {
            ctx->pc = 0x167240u;
            goto label_167240;
        }
    }
    ctx->pc = 0x167230u;
    // 0x167230: 0x8fcf0004  lw          $t7, 0x4($fp)
    ctx->pc = 0x167230u;
    SET_GPR_S32(ctx, 15, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 4)));
    // 0x167234: 0xafcf0030  sw          $t7, 0x30($fp)
    ctx->pc = 0x167234u;
    WRITE32(ADD32(GPR_U32(ctx, 30), 48), GPR_U32(ctx, 15));
    // 0x167238: 0x10000009  b           . + 4 + (0x9 << 2)
    ctx->pc = 0x167238u;
    {
        const bool branch_taken_0x167238 = (GPR_U64(ctx, 0) == GPR_U64(ctx, 0));
        if (branch_taken_0x167238) {
            ctx->pc = 0x167260u;
            goto label_167260;
        }
    }
    ctx->pc = 0x167240u;
label_167240:
    // 0x167240: 0x8fc4000c  lw          $a0, 0xC($fp)
    ctx->pc = 0x167240u;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 12)));
    // Best-effort completion signal -- same rationale/verified fix as
    // FUN_00166e98_0x166e98.cpp (identical CreateSema+submit+WaitSema+
    // DeleteSema idiom, IOP async response not emulated).
    ps2_syscalls::SignalSema(rdram, ctx, runtime);
    // 0x167244: 0xc073e68  jal         func_1CF9A0
    ctx->pc = 0x167244u;
    SET_GPR_U32(ctx, 31, 0x16724Cu);
    ctx->pc = 0x1CF9A0u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1CF9A0u, 0x167244u, 0x16724Cu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x16724Cu;
label_16724c:
    // 0x16724c: 0x8fc4000c  lw          $a0, 0xC($fp)
    ctx->pc = 0x16724cu;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 12)));
    // 0x167250: 0xc073e5c  jal         func_1CF970
    ctx->pc = 0x167250u;
    SET_GPR_U32(ctx, 31, 0x167258u);
    ctx->pc = 0x1CF970u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1CF970u, 0x167250u, 0x167258u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x167258u;
label_167258:
    // 0x167258: 0x8fcf0008  lw          $t7, 0x8($fp)
    ctx->pc = 0x167258u;
    SET_GPR_S32(ctx, 15, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 8)));
    // 0x16725c: 0xafcf0030  sw          $t7, 0x30($fp)
    ctx->pc = 0x16725cu;
    WRITE32(ADD32(GPR_U32(ctx, 30), 48), GPR_U32(ctx, 15));
label_167260:
    // 0x167260: 0x8fc20030  lw          $v0, 0x30($fp)
    ctx->pc = 0x167260u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 48)));
    // 0x167264: 0x3c0e82d  daddu       $sp, $fp, $zero
    ctx->pc = 0x167264u;
    SET_GPR_U64(ctx, 29, (uint64_t)GPR_U64(ctx, 30) + (uint64_t)GPR_U64(ctx, 0));
    // 0x167268: 0xdfbe0040  ld          $fp, 0x40($sp)
    ctx->pc = 0x167268u;
    SET_GPR_U64(ctx, 30, READ64(ADD32(GPR_U32(ctx, 29), 64)));
    // 0x16726c: 0xdfbf0048  ld          $ra, 0x48($sp)
    ctx->pc = 0x16726cu;
    SET_GPR_U64(ctx, 31, READ64(ADD32(GPR_U32(ctx, 29), 72)));
    // 0x167270: 0x27bd0050  addiu       $sp, $sp, 0x50
    ctx->pc = 0x167270u;
    SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 80));
    // 0x167274: 0x3e00008  jr          $ra
    ctx->pc = 0x167274u;
    {
        const uint32_t jumpTarget = GPR_U32(ctx, 31);
        ctx->pc = jumpTarget;
        #if defined(PS2X_STRICT_RETURN_DIAGNOSTICS) && PS2X_STRICT_RETURN_DIAGNOSTICS
        (void)runtime->dispatchGuestBranch(rdram, ctx, jumpTarget, 0x167274u, 0u, PS2Runtime::GuestBranchKind::Return, "JR $ra");
        return;
        #else
        ctx->pc = jumpTarget;
        return;
        #endif
    }
    ctx->pc = 0x16727Cu;
}
