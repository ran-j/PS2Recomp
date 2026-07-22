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

// Function: FUN_00194f30
// Address: 0x194f30 - 0x194f78
void FUN_00194f30_0x194f30(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
#ifdef PS2_FUNCTION_LOG_TRACKER
    PS_LOG_ENTRY("FUN_00194f30_0x194f30");
#endif

    switch (ctx->pc) {
        case 0x194f48u: goto label_194f48;
        case 0x194f50u: goto label_194f50;
        case 0x194f58u: goto label_194f58;
        default: break;
    }

    ctx->pc = 0x194f30u;

    // 0x194f30: 0x27bdfff0  addiu       $sp, $sp, -0x10
    ctx->pc = 0x194f30u;
    SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 4294967280));
    // 0x194f34: 0xffb00000  sd          $s0, 0x0($sp)
    ctx->pc = 0x194f34u;
    WRITE64(ADD32(GPR_U32(ctx, 29), 0), GPR_U64(ctx, 16));
    // 0x194f38: 0xffbf0008  sd          $ra, 0x8($sp)
    ctx->pc = 0x194f38u;
    WRITE64(ADD32(GPR_U32(ctx, 29), 8), GPR_U64(ctx, 31));
    // 0x194f3c: 0x80802d  daddu       $s0, $a0, $zero
    ctx->pc = 0x194f3cu;
    SET_GPR_U64(ctx, 16, (uint64_t)GPR_U64(ctx, 4) + (uint64_t)GPR_U64(ctx, 0));
    // 0x194f40: 0xc0656b8  jal         func_195AE0
    ctx->pc = 0x194F40u;
    SET_GPR_U32(ctx, 31, 0x194F48u);
    ctx->pc = 0x194F44u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x194F40u;
    // 0x194f44: 0x248400d0  addiu       $a0, $a0, 0xD0 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 4), 208));
    ctx->in_delay_slot = false;
    ctx->pc = 0x195AE0u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x195AE0u, 0x194F40u, 0x194F48u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x194F48u;
label_194f48:
    // 0x194f48: 0xc0653dc  jal         func_194F70
    ctx->pc = 0x194F48u;
    SET_GPR_U32(ctx, 31, 0x194F50u);
    ctx->pc = 0x194F4Cu;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x194F48u;
    // 0x194f4c: 0x260400b8  addiu       $a0, $s0, 0xB8 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 16), 184));
    ctx->in_delay_slot = false;
    ctx->pc = 0x194F70u;
    // NOTE: this JAL targets a genuine, separately-declared function
    // (sub_00194F70_0x194f70) -- the original codegen here mistranslated it
    // as an internal `goto label_194f70` into a truncated, broken partial
    // inline copy of that function's first two instructions (no epilogue,
    // no jr $ra), which made the outer dispatch loop spin forever re-entering
    // this dead label. Fixed to a proper cross-function call, matching every
    // other JAL in this same file.
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x194F70u, 0x194F48u, 0x194F50u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x194F50u;
label_194f50:
    // 0x194f50: 0xc0658ac  jal         func_1962B0
    ctx->pc = 0x194F50u;
    SET_GPR_U32(ctx, 31, 0x194F58u);
    ctx->pc = 0x194F54u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x194F50u;
    // 0x194f54: 0x260400e0  addiu       $a0, $s0, 0xE0 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 16), 224));
    ctx->in_delay_slot = false;
    ctx->pc = 0x1962B0u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1962B0u, 0x194F50u, 0x194F58u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x194F58u;
label_194f58:
    // 0x194f58: 0x261001e0  addiu       $s0, $s0, 0x1E0
    ctx->pc = 0x194f58u;
    SET_GPR_S32(ctx, 16, (int32_t)ADD32(GPR_U32(ctx, 16), 480));
    // 0x194f5c: 0x200202d  daddu       $a0, $s0, $zero
    ctx->pc = 0x194f5cu;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 16) + (uint64_t)GPR_U64(ctx, 0));
    // 0x194f60: 0xdfbf0008  ld          $ra, 0x8($sp)
    ctx->pc = 0x194f60u;
    SET_GPR_U64(ctx, 31, READ64(ADD32(GPR_U32(ctx, 29), 8)));
    // 0x194f64: 0xdfb00000  ld          $s0, 0x0($sp)
    ctx->pc = 0x194f64u;
    SET_GPR_U64(ctx, 16, READ64(ADD32(GPR_U32(ctx, 29), 0)));
    // 0x194f68: 0x80658de  j           func_196378
    ctx->pc = 0x194F68u;
    ctx->pc = 0x194F6Cu;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x194F68u;
    // 0x194f6c: 0x27bd0010  addiu       $sp, $sp, 0x10 (Delay Slot)
    SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 16));
    ctx->in_delay_slot = false;
    ctx->pc = 0x196378u;
    gap_00196378_0x196378(rdram, ctx, runtime); return;
}
