#include <stdexcept>
#include "ps2_runtime_macros.h"
#include "ps2_runtime.h"
#include "ps2_recompiled_functions.h"
#include "ps2_recompiled_stubs.h"

#include "ps2_syscalls.h"
#include "ps2_stubs.h"
#include "../lib/Kernel/Syscalls/Sync.h"
#include <atomic>
#include <thread>
#include <chrono>
#include <iostream>

#ifdef PS2_FUNCTION_LOG_TRACKER
#include "ps2_log.h"
#endif

// Function: FUN_001d09d8
// Address: 0x1d09d8 - 0x1d0aa0
void FUN_001d09d8_0x1d09d8(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
#ifdef PS2_FUNCTION_LOG_TRACKER
    PS_LOG_ENTRY("FUN_001d09d8_0x1d09d8");
#endif

    switch (ctx->pc) {
        case 0x1d0a28u: goto label_1d0a28;
        case 0x1d0a48u: goto label_1d0a48;
        case 0x1d0a5cu: goto label_1d0a5c;
        case 0x1d0a70u: goto label_1d0a70;
        case 0x1d0a80u: goto label_1d0a80;
        case 0x1d0a88u: goto label_1d0a88;
        default: break;
    }

    ctx->pc = 0x1d09d8u;

    // 0x1d09d8: 0x27bdffb0  addiu       $sp, $sp, -0x50
    ctx->pc = 0x1d09d8u;
    SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 4294967216));
    // 0x1d09dc: 0xffb10030  sd          $s1, 0x30($sp)
    ctx->pc = 0x1d09dcu;
    WRITE64(ADD32(GPR_U32(ctx, 29), 48), GPR_U64(ctx, 17));
    // 0x1d09e0: 0xffbf0040  sd          $ra, 0x40($sp)
    ctx->pc = 0x1d09e0u;
    WRITE64(ADD32(GPR_U32(ctx, 29), 64), GPR_U64(ctx, 31));
    // 0x1d09e4: 0x80882d  daddu       $s1, $a0, $zero
    ctx->pc = 0x1d09e4u;
    SET_GPR_U64(ctx, 17, (uint64_t)GPR_U64(ctx, 4) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1d09e8: 0xffb00020  sd          $s0, 0x20($sp)
    ctx->pc = 0x1d09e8u;
    WRITE64(ADD32(GPR_U32(ctx, 29), 32), GPR_U64(ctx, 16));
    // 0x1d09ec: 0x40026000  mfc0        $v0, Status
    ctx->pc = 0x1d09ecu;
    SET_GPR_S32(ctx, 2, (int32_t)ctx->cop0_status);
    // 0x1d09f0: 0x3c030001  lui         $v1, 0x1
    ctx->pc = 0x1d09f0u;
    SET_GPR_S32(ctx, 3, (int32_t)((uint32_t)1 << 16));
    // 0x1d09f4: 0x431024  and         $v0, $v0, $v1
    ctx->pc = 0x1d09f4u;
    SET_GPR_U64(ctx, 2, GPR_U64(ctx, 2) & GPR_U64(ctx, 3));
    // 0x1d09f8: 0x54400004  bnel        $v0, $zero, . + 4 + (0x4 << 2)
    ctx->pc = 0x1D09F8u;
    {
        const bool branch_taken_0x1d09f8 = (GPR_U64(ctx, 2) != GPR_U64(ctx, 0));
        if (branch_taken_0x1d09f8) {
            ctx->pc = 0x1D09FCu;
            ctx->in_delay_slot = true;
            ctx->branch_pc = 0x1D09F8u;
            // 0x1d09fc: 0x3c020030  lui         $v0, 0x30 (Delay Slot)
            SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)48 << 16));
            ctx->in_delay_slot = false;
            ctx->pc = 0x1D0A0Cu;
            goto label_1d0a0c;
        }
    }
    ctx->pc = 0x1D0A00u;
    // 0x1d0a00: 0x3c028000  lui         $v0, 0x8000
    ctx->pc = 0x1d0a00u;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)32768 << 16));
    // 0x1d0a04: 0x10000021  b           . + 4 + (0x21 << 2)
    ctx->pc = 0x1D0A04u;
    {
        const bool branch_taken_0x1d0a04 = (GPR_U64(ctx, 0) == GPR_U64(ctx, 0));
        ctx->pc = 0x1D0A08u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1D0A04u;
        // 0x1d0a08: 0x34428008  ori         $v0, $v0, 0x8008 (Delay Slot)
        SET_GPR_U64(ctx, 2, GPR_U64(ctx, 2) | (uint64_t)(uint16_t)32776);
        ctx->in_delay_slot = false;
        if (branch_taken_0x1d0a04) {
            ctx->pc = 0x1D0A8Cu;
            goto label_1d0a8c;
        }
    }
    ctx->pc = 0x1D0A0Cu;
label_1d0a0c:
    // 0x1d0a0c: 0x24030001  addiu       $v1, $zero, 0x1
    ctx->pc = 0x1d0a0cu;
    SET_GPR_S32(ctx, 3, (int32_t)ADD32(GPR_U32(ctx, 0), 1));
    // 0x1d0a10: 0x2442a8b8  addiu       $v0, $v0, -0x5748
    ctx->pc = 0x1d0a10u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 4294944952));
    // 0x1d0a14: 0xafa30004  sw          $v1, 0x4($sp)
    ctx->pc = 0x1d0a14u;
    WRITE32(ADD32(GPR_U32(ctx, 29), 4), GPR_U32(ctx, 3));
    // 0x1d0a18: 0xafa20014  sw          $v0, 0x14($sp)
    ctx->pc = 0x1d0a18u;
    WRITE32(ADD32(GPR_U32(ctx, 29), 20), GPR_U32(ctx, 2));
    // 0x1d0a1c: 0x3a0202d  daddu       $a0, $sp, $zero
    ctx->pc = 0x1d0a1cu;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 29) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1d0a20: 0xc073e58  jal         func_1CF960
    ctx->pc = 0x1D0A20u;
    SET_GPR_U32(ctx, 31, 0x1D0A28u);
    ctx->pc = 0x1D0A24u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1D0A20u;
    // 0x1d0a24: 0xafa00008  sw          $zero, 0x8($sp) (Delay Slot)
    WRITE32(ADD32(GPR_U32(ctx, 29), 8), GPR_U32(ctx, 0));
    ctx->in_delay_slot = false;
    ctx->pc = 0x1CF960u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1CF960u, 0x1D0A20u, 0x1D0A28u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1D0A28u;
label_1d0a28:
    // 0x1d0a28: 0x40802d  daddu       $s0, $v0, $zero
    ctx->pc = 0x1d0a28u;
    SET_GPR_U64(ctx, 16, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1d0a2c: 0x6010004  bgez        $s0, . + 4 + (0x4 << 2)
    ctx->pc = 0x1D0A2Cu;
    {
        const bool branch_taken_0x1d0a2c = (GPR_S32(ctx, 16) >= 0);
        ctx->pc = 0x1D0A30u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1D0A2Cu;
        // 0x1d0a30: 0x220282d  daddu       $a1, $s1, $zero (Delay Slot)
        SET_GPR_U64(ctx, 5, (uint64_t)GPR_U64(ctx, 17) + (uint64_t)GPR_U64(ctx, 0));
        ctx->in_delay_slot = false;
        if (branch_taken_0x1d0a2c) {
            ctx->pc = 0x1D0A40u;
            goto label_1d0a40;
        }
    }
    ctx->pc = 0x1D0A34u;
    // 0x1d0a34: 0x3c028000  lui         $v0, 0x8000
    ctx->pc = 0x1d0a34u;
    SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)32768 << 16));
    // 0x1d0a38: 0x10000014  b           . + 4 + (0x14 << 2)
    ctx->pc = 0x1D0A38u;
    {
        const bool branch_taken_0x1d0a38 = (GPR_U64(ctx, 0) == GPR_U64(ctx, 0));
        ctx->pc = 0x1D0A3Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1D0A38u;
        // 0x1d0a3c: 0x34428003  ori         $v0, $v0, 0x8003 (Delay Slot)
        SET_GPR_U64(ctx, 2, GPR_U64(ctx, 2) | (uint64_t)(uint16_t)32771);
        ctx->in_delay_slot = false;
        if (branch_taken_0x1d0a38) {
            ctx->pc = 0x1D0A8Cu;
            goto label_1d0a8c;
        }
    }
    ctx->pc = 0x1D0A40u;
label_1d0a40:
    // 0x1d0a40: 0xc076b2e  jal         func_1DACB8
    ctx->pc = 0x1D0A40u;
    SET_GPR_U32(ctx, 31, 0x1D0A48u);
    ctx->pc = 0x1D0A44u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1D0A40u;
    // 0x1d0a44: 0x202d  daddu       $a0, $zero, $zero (Delay Slot)
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 0) + (uint64_t)GPR_U64(ctx, 0));
    ctx->in_delay_slot = false;
    ctx->pc = 0x1DACB8u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1DACB8u, 0x1D0A40u, 0x1D0A48u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1D0A48u;
label_1d0a48:
    // 0x1d0a48: 0x3c05001d  lui         $a1, 0x1D
    ctx->pc = 0x1d0a48u;
    SET_GPR_S32(ctx, 5, (int32_t)((uint32_t)29 << 16));
    // 0x1d0a4c: 0x40202d  daddu       $a0, $v0, $zero
    ctx->pc = 0x1d0a4cu;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1d0a50: 0x24a50c18  addiu       $a1, $a1, 0xC18
    ctx->pc = 0x1d0a50u;
    SET_GPR_S32(ctx, 5, (int32_t)ADD32(GPR_U32(ctx, 5), 3096));
    // 0x1d0a54: 0xc076b9a  jal         func_1DAE68
    ctx->pc = 0x1D0A54u;
    SET_GPR_U32(ctx, 31, 0x1D0A5Cu);
    ctx->pc = 0x1D0A58u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1D0A54u;
    // 0x1d0a58: 0x200302d  daddu       $a2, $s0, $zero (Delay Slot)
    SET_GPR_U64(ctx, 6, (uint64_t)GPR_U64(ctx, 16) + (uint64_t)GPR_U64(ctx, 0));
    ctx->in_delay_slot = false;
    ctx->pc = 0x1DAE68u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1DAE68u, 0x1D0A54u, 0x1D0A5Cu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1D0A5Cu;
label_1d0a5c:
    // 0x1d0a5c: 0x40882d  daddu       $s1, $v0, $zero
    ctx->pc = 0x1d0a5cu;
    SET_GPR_U64(ctx, 17, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x1d0a60: 0x6210005  bgez        $s1, . + 4 + (0x5 << 2)
    ctx->pc = 0x1D0A60u;
    {
        const bool branch_taken_0x1d0a60 = (GPR_S32(ctx, 17) >= 0);
        if (branch_taken_0x1d0a60) {
            ctx->pc = 0x1D0A78u;
            goto label_1d0a78;
        }
    }
    ctx->pc = 0x1D0A68u;
    // 0x1d0a68: 0xc073e5c  jal         func_1CF970
    ctx->pc = 0x1D0A68u;
    SET_GPR_U32(ctx, 31, 0x1D0A70u);
    ctx->pc = 0x1D0A6Cu;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1D0A68u;
    // 0x1d0a6c: 0x200202d  daddu       $a0, $s0, $zero (Delay Slot)
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 16) + (uint64_t)GPR_U64(ctx, 0));
    ctx->in_delay_slot = false;
    ctx->pc = 0x1CF970u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1CF970u, 0x1D0A68u, 0x1D0A70u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1D0A70u;
label_1d0a70:
    // 0x1d0a70: 0x10000006  b           . + 4 + (0x6 << 2)
    ctx->pc = 0x1D0A70u;
    {
        const bool branch_taken_0x1d0a70 = (GPR_U64(ctx, 0) == GPR_U64(ctx, 0));
        ctx->pc = 0x1D0A74u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1D0A70u;
        // 0x1d0a74: 0x220102d  daddu       $v0, $s1, $zero (Delay Slot)
        SET_GPR_U64(ctx, 2, (uint64_t)GPR_U64(ctx, 17) + (uint64_t)GPR_U64(ctx, 0));
        ctx->in_delay_slot = false;
        if (branch_taken_0x1d0a70) {
            ctx->pc = 0x1D0A8Cu;
            goto label_1d0a8c;
        }
    }
    ctx->pc = 0x1D0A78u;
label_1d0a78:
    // 0x1d0a78: 0xc073e68  jal         func_1CF9A0
    ctx->pc = 0x1D0A78u;
    SET_GPR_U32(ctx, 31, 0x1D0A80u);
    ctx->pc = 0x1D0A7Cu;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1D0A78u;
    // 0x1d0a7c: 0x200202d  daddu       $a0, $s0, $zero (Delay Slot)
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 16) + (uint64_t)GPR_U64(ctx, 0));
    ctx->in_delay_slot = false;
    // Best-effort completion signal -- same rationale/verified fix as
    // FUN_00166e98_0x166e98.cpp. Confirmed via live diagnostics that this
    // function is called in a genuine, large (tens of thousands of entries)
    // per-item loop by FUN_001d3730 -- NOT a retry-on-failure loop -- so
    // synchronous instant completion here is correct; the earlier semaphore-
    // id exhaustion seen while diagnosing this was this loop legitimately
    // running to its real end, not a livelock.
    ps2_syscalls::SignalSema(rdram, ctx, runtime);
    ctx->pc = 0x1CF9A0u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1CF9A0u, 0x1D0A78u, 0x1D0A80u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1D0A80u;
label_1d0a80:
    // 0x1d0a80: 0xc073e5c  jal         func_1CF970
    ctx->pc = 0x1D0A80u;
    SET_GPR_U32(ctx, 31, 0x1D0A88u);
    ctx->pc = 0x1D0A84u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x1D0A80u;
    // 0x1d0a84: 0x200202d  daddu       $a0, $s0, $zero (Delay Slot)
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 16) + (uint64_t)GPR_U64(ctx, 0));
    ctx->in_delay_slot = false;
    ctx->pc = 0x1CF970u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1CF970u, 0x1D0A80u, 0x1D0A88u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x1D0A88u;
label_1d0a88:
    // 0x1d0a88: 0x102d  daddu       $v0, $zero, $zero
    ctx->pc = 0x1d0a88u;
    SET_GPR_U64(ctx, 2, (uint64_t)GPR_U64(ctx, 0) + (uint64_t)GPR_U64(ctx, 0));
label_1d0a8c:
    // 0x1d0a8c: 0xdfbf0040  ld          $ra, 0x40($sp)
    ctx->pc = 0x1d0a8cu;
    SET_GPR_U64(ctx, 31, READ64(ADD32(GPR_U32(ctx, 29), 64)));
    // 0x1d0a90: 0xdfb10030  ld          $s1, 0x30($sp)
    ctx->pc = 0x1d0a90u;
    SET_GPR_U64(ctx, 17, READ64(ADD32(GPR_U32(ctx, 29), 48)));
    // 0x1d0a94: 0xdfb00020  ld          $s0, 0x20($sp)
    ctx->pc = 0x1d0a94u;
    SET_GPR_U64(ctx, 16, READ64(ADD32(GPR_U32(ctx, 29), 32)));
    // 0x1d0a98: 0x3e00008  jr          $ra
    ctx->pc = 0x1D0A98u;
    {
        const uint32_t jumpTarget = GPR_U32(ctx, 31);
        ctx->pc = 0x1D0A9Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1D0A98u;
        // 0x1d0a9c: 0x27bd0050  addiu       $sp, $sp, 0x50 (Delay Slot)
        SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 80));
        ctx->in_delay_slot = false;
        ctx->pc = jumpTarget;
        #if defined(PS2X_STRICT_RETURN_DIAGNOSTICS) && PS2X_STRICT_RETURN_DIAGNOSTICS
        (void)runtime->dispatchGuestBranch(rdram, ctx, jumpTarget, 0x1D0A98u, 0u, PS2Runtime::GuestBranchKind::Return, "JR $ra");
        return;
        #else
        ctx->pc = jumpTarget;
        return;
        #endif
    }
    ctx->pc = 0x1D0AA0u;
}
