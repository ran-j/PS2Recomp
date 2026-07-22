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

// Function: FUN_00167aa8
// Address: 0x167aa8 - 0x167b9c
void FUN_00167aa8_0x167aa8(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
#ifdef PS2_FUNCTION_LOG_TRACKER
    PS_LOG_ENTRY("FUN_00167aa8_0x167aa8");
#endif

    switch (ctx->pc) {
        case 0x167ae4u: goto label_167ae4;
        case 0x167b08u: goto label_167b08;
        case 0x167b3cu: goto label_167b3c;
        case 0x167b6cu: goto label_167b6c;
        case 0x167b78u: goto label_167b78;
        default: break;
    }

    ctx->pc = 0x167aa8u;

    // 0x167aa8: 0x27bdffb0  addiu       $sp, $sp, -0x50
    ctx->pc = 0x167aa8u;
    SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 4294967216));
    // 0x167aac: 0xffbe0040  sd          $fp, 0x40($sp)
    ctx->pc = 0x167aacu;
    WRITE64(ADD32(GPR_U32(ctx, 29), 64), GPR_U64(ctx, 30));
    // 0x167ab0: 0xffbf0048  sd          $ra, 0x48($sp)
    ctx->pc = 0x167ab0u;
    WRITE64(ADD32(GPR_U32(ctx, 29), 72), GPR_U64(ctx, 31));
    // 0x167ab4: 0x3a0f02d  daddu       $fp, $sp, $zero
    ctx->pc = 0x167ab4u;
    SET_GPR_U64(ctx, 30, (uint64_t)GPR_U64(ctx, 29) + (uint64_t)GPR_U64(ctx, 0));
    // 0x167ab8: 0xafc40000  sw          $a0, 0x0($fp)
    ctx->pc = 0x167ab8u;
    WRITE32(ADD32(GPR_U32(ctx, 30), 0), GPR_U32(ctx, 4));
    // 0x167abc: 0xafc00018  sw          $zero, 0x18($fp)
    ctx->pc = 0x167abcu;
    WRITE32(ADD32(GPR_U32(ctx, 30), 24), GPR_U32(ctx, 0));
    // 0x167ac0: 0x240f0001  addiu       $t7, $zero, 0x1
    ctx->pc = 0x167ac0u;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 0), 1));
    // 0x167ac4: 0xafcf0014  sw          $t7, 0x14($fp)
    ctx->pc = 0x167ac4u;
    WRITE32(ADD32(GPR_U32(ctx, 30), 20), GPR_U32(ctx, 15));
    // 0x167ac8: 0x27cf0008  addiu       $t7, $fp, 0x8
    ctx->pc = 0x167ac8u;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 30), 8));
    // 0x167acc: 0xafcf0024  sw          $t7, 0x24($fp)
    ctx->pc = 0x167accu;
    WRITE32(ADD32(GPR_U32(ctx, 30), 36), GPR_U32(ctx, 15));
    // 0x167ad0: 0xafc00020  sw          $zero, 0x20($fp)
    ctx->pc = 0x167ad0u;
    WRITE32(ADD32(GPR_U32(ctx, 30), 32), GPR_U32(ctx, 0));
    // 0x167ad4: 0x27cf0010  addiu       $t7, $fp, 0x10
    ctx->pc = 0x167ad4u;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 30), 16));
    // 0x167ad8: 0x1e0202d  daddu       $a0, $t7, $zero
    ctx->pc = 0x167ad8u;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 15) + (uint64_t)GPR_U64(ctx, 0));
    // 0x167adc: 0xc073e58  jal         func_1CF960
    ctx->pc = 0x167ADCu;
    SET_GPR_U32(ctx, 31, 0x167AE4u);
    ctx->pc = 0x1CF960u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1CF960u, 0x167ADCu, 0x167AE4u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x167AE4u;
label_167ae4:
    // 0x167ae4: 0x40782d  daddu       $t7, $v0, $zero
    ctx->pc = 0x167ae4u;
    SET_GPR_U64(ctx, 15, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x167ae8: 0xafcf000c  sw          $t7, 0xC($fp)
    ctx->pc = 0x167ae8u;
    WRITE32(ADD32(GPR_U32(ctx, 30), 12), GPR_U32(ctx, 15));
    // 0x167aec: 0x8fcf000c  lw          $t7, 0xC($fp)
    ctx->pc = 0x167aecu;
    SET_GPR_S32(ctx, 15, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 12)));
    // 0x167af0: 0x1de0000c  bgtz        $t7, . + 4 + (0xC << 2)
    ctx->pc = 0x167AF0u;
    {
        const bool branch_taken_0x167af0 = (GPR_S32(ctx, 15) > 0);
        if (branch_taken_0x167af0) {
            ctx->pc = 0x167B24u;
            goto label_167b24;
        }
    }
    ctx->pc = 0x167AF8u;
    // 0x167af8: 0x3c04002f  lui         $a0, 0x2F
    ctx->pc = 0x167af8u;
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)47 << 16));
    // 0x167afc: 0x24844fb0  addiu       $a0, $a0, 0x4FB0
    ctx->pc = 0x167afcu;
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 4), 20400));
    // 0x167b00: 0xc074934  jal         func_1D24D0
    ctx->pc = 0x167B00u;
    SET_GPR_U32(ctx, 31, 0x167B08u);
    ctx->pc = 0x1D24D0u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1D24D0u, 0x167B00u, 0x167B08u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x167B08u;
label_167b08:
    // 0x167b08: 0x3c0ffeff  lui         $t7, 0xFEFF
    ctx->pc = 0x167b08u;
    SET_GPR_S32(ctx, 15, (int32_t)((uint32_t)65279 << 16));
    // 0x167b0c: 0xafcf0030  sw          $t7, 0x30($fp)
    ctx->pc = 0x167b0cu;
    WRITE32(ADD32(GPR_U32(ctx, 30), 48), GPR_U32(ctx, 15));
    // 0x167b10: 0x8fcf0030  lw          $t7, 0x30($fp)
    ctx->pc = 0x167b10u;
    SET_GPR_S32(ctx, 15, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 48)));
    // 0x167b14: 0x35effff3  ori         $t7, $t7, 0xFFF3
    ctx->pc = 0x167b14u;
    SET_GPR_U64(ctx, 15, GPR_U64(ctx, 15) | (uint64_t)(uint16_t)65523);
    // 0x167b18: 0xafcf0030  sw          $t7, 0x30($fp)
    ctx->pc = 0x167b18u;
    WRITE32(ADD32(GPR_U32(ctx, 30), 48), GPR_U32(ctx, 15));
    // 0x167b1c: 0x10000018  b           . + 4 + (0x18 << 2)
    ctx->pc = 0x167B1Cu;
    {
        const bool branch_taken_0x167b1c = (GPR_U64(ctx, 0) == GPR_U64(ctx, 0));
        if (branch_taken_0x167b1c) {
            ctx->pc = 0x167B80u;
            goto label_167b80;
        }
    }
    ctx->pc = 0x167B24u;
label_167b24:
    // 0x167b24: 0x8fc40000  lw          $a0, 0x0($fp)
    ctx->pc = 0x167b24u;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 0)));
    // 0x167b28: 0x3c050017  lui         $a1, 0x17
    ctx->pc = 0x167b28u;
    SET_GPR_S32(ctx, 5, (int32_t)((uint32_t)23 << 16));
    // 0x167b2c: 0x24a59644  addiu       $a1, $a1, -0x69BC
    ctx->pc = 0x167b2cu;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 5), 4294940228));
    // 0x167b30: 0x8fc6000c  lw          $a2, 0xC($fp)
    ctx->pc = 0x167b30u;
    SET_GPR_S32(ctx, 6, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 12)));
    // 0x167b34: 0xc059ee7  jal         func_167B9C
    ctx->pc = 0x167B34u;
    SET_GPR_U32(ctx, 31, 0x167B3Cu);
    ctx->pc = 0x167B9Cu;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x167B9Cu, 0x167B34u, 0x167B3Cu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x167B3Cu;
label_167b3c:
    // 0x167b3c: 0x40782d  daddu       $t7, $v0, $zero
    ctx->pc = 0x167b3cu;
    SET_GPR_U64(ctx, 15, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x167b40: 0xafcf0004  sw          $t7, 0x4($fp)
    ctx->pc = 0x167b40u;
    WRITE32(ADD32(GPR_U32(ctx, 30), 4), GPR_U32(ctx, 15));
    // 0x167b44: 0x8fcf0004  lw          $t7, 0x4($fp)
    ctx->pc = 0x167b44u;
    SET_GPR_S32(ctx, 15, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 4)));
    // 0x167b48: 0x5e10005  bgez        $t7, . + 4 + (0x5 << 2)
    ctx->pc = 0x167B48u;
    {
        const bool branch_taken_0x167b48 = (GPR_S32(ctx, 15) >= 0);
        if (branch_taken_0x167b48) {
            ctx->pc = 0x167B60u;
            goto label_167b60;
        }
    }
    ctx->pc = 0x167B50u;
    // 0x167b50: 0x8fcf0004  lw          $t7, 0x4($fp)
    ctx->pc = 0x167b50u;
    SET_GPR_S32(ctx, 15, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 4)));
    // 0x167b54: 0xafcf0030  sw          $t7, 0x30($fp)
    ctx->pc = 0x167b54u;
    WRITE32(ADD32(GPR_U32(ctx, 30), 48), GPR_U32(ctx, 15));
    // 0x167b58: 0x10000009  b           . + 4 + (0x9 << 2)
    ctx->pc = 0x167B58u;
    {
        const bool branch_taken_0x167b58 = (GPR_U64(ctx, 0) == GPR_U64(ctx, 0));
        if (branch_taken_0x167b58) {
            ctx->pc = 0x167B80u;
            goto label_167b80;
        }
    }
    ctx->pc = 0x167B60u;
label_167b60:
    // 0x167b60: 0x8fc4000c  lw          $a0, 0xC($fp)
    ctx->pc = 0x167b60u;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 12)));
    // Best-effort completion signal -- same rationale/verified fix as
    // FUN_00166e98_0x166e98.cpp (identical CreateSema+submit+WaitSema+
    // DeleteSema idiom, IOP async response not emulated).
    ps2_syscalls::SignalSema(rdram, ctx, runtime);
    // 0x167b64: 0xc073e68  jal         func_1CF9A0
    ctx->pc = 0x167B64u;
    SET_GPR_U32(ctx, 31, 0x167B6Cu);
    ctx->pc = 0x1CF9A0u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1CF9A0u, 0x167B64u, 0x167B6Cu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x167B6Cu;
label_167b6c:
    // 0x167b6c: 0x8fc4000c  lw          $a0, 0xC($fp)
    ctx->pc = 0x167b6cu;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 12)));
    // 0x167b70: 0xc073e5c  jal         func_1CF970
    ctx->pc = 0x167B70u;
    SET_GPR_U32(ctx, 31, 0x167B78u);
    ctx->pc = 0x1CF970u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1CF970u, 0x167B70u, 0x167B78u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x167B78u;
label_167b78:
    // 0x167b78: 0x8fcf0008  lw          $t7, 0x8($fp)
    ctx->pc = 0x167b78u;
    SET_GPR_S32(ctx, 15, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 8)));
    // 0x167b7c: 0xafcf0030  sw          $t7, 0x30($fp)
    ctx->pc = 0x167b7cu;
    WRITE32(ADD32(GPR_U32(ctx, 30), 48), GPR_U32(ctx, 15));
label_167b80:
    // 0x167b80: 0x8fc20030  lw          $v0, 0x30($fp)
    ctx->pc = 0x167b80u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 48)));
    // 0x167b84: 0x3c0e82d  daddu       $sp, $fp, $zero
    ctx->pc = 0x167b84u;
    SET_GPR_U64(ctx, 29, (uint64_t)GPR_U64(ctx, 30) + (uint64_t)GPR_U64(ctx, 0));
    // 0x167b88: 0xdfbe0040  ld          $fp, 0x40($sp)
    ctx->pc = 0x167b88u;
    SET_GPR_U64(ctx, 30, READ64(ADD32(GPR_U32(ctx, 29), 64)));
    // 0x167b8c: 0xdfbf0048  ld          $ra, 0x48($sp)
    ctx->pc = 0x167b8cu;
    SET_GPR_U64(ctx, 31, READ64(ADD32(GPR_U32(ctx, 29), 72)));
    // 0x167b90: 0x27bd0050  addiu       $sp, $sp, 0x50
    ctx->pc = 0x167b90u;
    SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 80));
    // 0x167b94: 0x3e00008  jr          $ra
    ctx->pc = 0x167B94u;
    {
        const uint32_t jumpTarget = GPR_U32(ctx, 31);
        ctx->pc = jumpTarget;
        #if defined(PS2X_STRICT_RETURN_DIAGNOSTICS) && PS2X_STRICT_RETURN_DIAGNOSTICS
        (void)runtime->dispatchGuestBranch(rdram, ctx, jumpTarget, 0x167B94u, 0u, PS2Runtime::GuestBranchKind::Return, "JR $ra");
        return;
        #else
        ctx->pc = jumpTarget;
        return;
        #endif
    }
    ctx->pc = 0x167B9Cu;
}
