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

// Function: FUN_00166e98
// Address: 0x166e98 - 0x166fb4
void FUN_00166e98_0x166e98(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
#ifdef PS2_FUNCTION_LOG_TRACKER
    PS_LOG_ENTRY("FUN_00166e98_0x166e98");
#endif

    switch (ctx->pc) {
        case 0x166eecu: goto label_166eec;
        case 0x166f10u: goto label_166f10;
        case 0x166f54u: goto label_166f54;
        case 0x166f84u: goto label_166f84;
        case 0x166f90u: goto label_166f90;
        default: break;
    }

    ctx->pc = 0x166e98u;

    // 0x166e98: 0x27bdff90  addiu       $sp, $sp, -0x70
    ctx->pc = 0x166e98u;
    SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 4294967184));
    // 0x166e9c: 0xffbe0060  sd          $fp, 0x60($sp)
    ctx->pc = 0x166e9cu;
    WRITE64(ADD32(GPR_U32(ctx, 29), 96), GPR_U64(ctx, 30));
    // 0x166ea0: 0xffbf0068  sd          $ra, 0x68($sp)
    ctx->pc = 0x166ea0u;
    WRITE64(ADD32(GPR_U32(ctx, 29), 104), GPR_U64(ctx, 31));
    // 0x166ea4: 0x3a0f02d  daddu       $fp, $sp, $zero
    ctx->pc = 0x166ea4u;
    SET_GPR_U64(ctx, 30, (uint64_t)GPR_U64(ctx, 29) + (uint64_t)GPR_U64(ctx, 0));
    // 0x166ea8: 0xafc40000  sw          $a0, 0x0($fp)
    ctx->pc = 0x166ea8u;
    WRITE32(ADD32(GPR_U32(ctx, 30), 0), GPR_U32(ctx, 4));
    // 0x166eac: 0xafc50004  sw          $a1, 0x4($fp)
    ctx->pc = 0x166eacu;
    WRITE32(ADD32(GPR_U32(ctx, 30), 4), GPR_U32(ctx, 5));
    // 0x166eb0: 0xafc60008  sw          $a2, 0x8($fp)
    ctx->pc = 0x166eb0u;
    WRITE32(ADD32(GPR_U32(ctx, 30), 8), GPR_U32(ctx, 6));
    // 0x166eb4: 0xafc7000c  sw          $a3, 0xC($fp)
    ctx->pc = 0x166eb4u;
    WRITE32(ADD32(GPR_U32(ctx, 30), 12), GPR_U32(ctx, 7));
    // 0x166eb8: 0xafc80010  sw          $t0, 0x10($fp)
    ctx->pc = 0x166eb8u;
    WRITE32(ADD32(GPR_U32(ctx, 30), 16), GPR_U32(ctx, 8));
    // 0x166ebc: 0xafc90014  sw          $t1, 0x14($fp)
    ctx->pc = 0x166ebcu;
    WRITE32(ADD32(GPR_U32(ctx, 30), 20), GPR_U32(ctx, 9));
    // 0x166ec0: 0xafca0018  sw          $t2, 0x18($fp)
    ctx->pc = 0x166ec0u;
    WRITE32(ADD32(GPR_U32(ctx, 30), 24), GPR_U32(ctx, 10));
    // 0x166ec4: 0xafc00038  sw          $zero, 0x38($fp)
    ctx->pc = 0x166ec4u;
    WRITE32(ADD32(GPR_U32(ctx, 30), 56), GPR_U32(ctx, 0));
    // 0x166ec8: 0x240f0001  addiu       $t7, $zero, 0x1
    ctx->pc = 0x166ec8u;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 0), 1));
    // 0x166ecc: 0xafcf0034  sw          $t7, 0x34($fp)
    ctx->pc = 0x166eccu;
    WRITE32(ADD32(GPR_U32(ctx, 30), 52), GPR_U32(ctx, 15));
    // 0x166ed0: 0x27cf0020  addiu       $t7, $fp, 0x20
    ctx->pc = 0x166ed0u;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 30), 32));
    // 0x166ed4: 0xafcf0044  sw          $t7, 0x44($fp)
    ctx->pc = 0x166ed4u;
    WRITE32(ADD32(GPR_U32(ctx, 30), 68), GPR_U32(ctx, 15));
    // 0x166ed8: 0xafc00040  sw          $zero, 0x40($fp)
    ctx->pc = 0x166ed8u;
    WRITE32(ADD32(GPR_U32(ctx, 30), 64), GPR_U32(ctx, 0));
    // 0x166edc: 0x27cf0030  addiu       $t7, $fp, 0x30
    ctx->pc = 0x166edcu;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 30), 48));
    // 0x166ee0: 0x1e0202d  daddu       $a0, $t7, $zero
    ctx->pc = 0x166ee0u;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 15) + (uint64_t)GPR_U64(ctx, 0));
    // 0x166ee4: 0xc073e58  jal         func_1CF960
    ctx->pc = 0x166EE4u;
    SET_GPR_U32(ctx, 31, 0x166EECu);
    ctx->pc = 0x1CF960u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1CF960u, 0x166EE4u, 0x166EECu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x166EECu;
label_166eec:
    // 0x166eec: 0x40782d  daddu       $t7, $v0, $zero
    ctx->pc = 0x166eecu;
    SET_GPR_U64(ctx, 15, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x166ef0: 0xafcf0024  sw          $t7, 0x24($fp)
    ctx->pc = 0x166ef0u;
    WRITE32(ADD32(GPR_U32(ctx, 30), 36), GPR_U32(ctx, 15));
    // 0x166ef4: 0x8fcf0024  lw          $t7, 0x24($fp)
    ctx->pc = 0x166ef4u;
    SET_GPR_S32(ctx, 15, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 36)));
    // 0x166ef8: 0x1de0000c  bgtz        $t7, . + 4 + (0xC << 2)
    ctx->pc = 0x166EF8u;
    {
        const bool branch_taken_0x166ef8 = (GPR_S32(ctx, 15) > 0);
        if (branch_taken_0x166ef8) {
            ctx->pc = 0x166F2Cu;
            goto label_166f2c;
        }
    }
    ctx->pc = 0x166F00u;
    // 0x166f00: 0x3c04002f  lui         $a0, 0x2F
    ctx->pc = 0x166f00u;
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)47 << 16));
    // 0x166f04: 0x24844fb0  addiu       $a0, $a0, 0x4FB0
    ctx->pc = 0x166f04u;
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 4), 20400));
    // 0x166f08: 0xc074934  jal         func_1D24D0
    ctx->pc = 0x166F08u;
    SET_GPR_U32(ctx, 31, 0x166F10u);
    ctx->pc = 0x1D24D0u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1D24D0u, 0x166F08u, 0x166F10u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x166F10u;
label_166f10:
    // 0x166f10: 0x3c0ffeff  lui         $t7, 0xFEFF
    ctx->pc = 0x166f10u;
    SET_GPR_S32(ctx, 15, (int32_t)((uint32_t)65279 << 16));
    // 0x166f14: 0xafcf0050  sw          $t7, 0x50($fp)
    ctx->pc = 0x166f14u;
    WRITE32(ADD32(GPR_U32(ctx, 30), 80), GPR_U32(ctx, 15));
    // 0x166f18: 0x8fcf0050  lw          $t7, 0x50($fp)
    ctx->pc = 0x166f18u;
    SET_GPR_S32(ctx, 15, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 80)));
    // 0x166f1c: 0x35effff3  ori         $t7, $t7, 0xFFF3
    ctx->pc = 0x166f1cu;
    SET_GPR_U64(ctx, 15, GPR_U64(ctx, 15) | (uint64_t)(uint16_t)65523);
    // 0x166f20: 0xafcf0050  sw          $t7, 0x50($fp)
    ctx->pc = 0x166f20u;
    WRITE32(ADD32(GPR_U32(ctx, 30), 80), GPR_U32(ctx, 15));
    // 0x166f24: 0x1000001c  b           . + 4 + (0x1C << 2)
    ctx->pc = 0x166F24u;
    {
        const bool branch_taken_0x166f24 = (GPR_U64(ctx, 0) == GPR_U64(ctx, 0));
        if (branch_taken_0x166f24) {
            ctx->pc = 0x166F98u;
            goto label_166f98;
        }
    }
    ctx->pc = 0x166F2Cu;
label_166f2c:
    // 0x166f2c: 0x8fc40008  lw          $a0, 0x8($fp)
    ctx->pc = 0x166f2cu;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 8)));
    // 0x166f30: 0x8fc5000c  lw          $a1, 0xC($fp)
    ctx->pc = 0x166f30u;
    SET_GPR_S32(ctx, 5, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 12)));
    // 0x166f34: 0x8fc60010  lw          $a2, 0x10($fp)
    ctx->pc = 0x166f34u;
    SET_GPR_S32(ctx, 6, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 16)));
    // 0x166f38: 0x8fc70014  lw          $a3, 0x14($fp)
    ctx->pc = 0x166f38u;
    SET_GPR_S32(ctx, 7, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 20)));
    // 0x166f3c: 0x8fc80018  lw          $t0, 0x18($fp)
    ctx->pc = 0x166f3cu;
    SET_GPR_S32(ctx, 8, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 24)));
    // 0x166f40: 0x3c090017  lui         $t1, 0x17
    ctx->pc = 0x166f40u;
    SET_GPR_S32(ctx, 9, (int32_t)((uint32_t)23 << 16));
    // 0x166f44: 0x25299644  addiu       $t1, $t1, -0x69BC
    ctx->pc = 0x166f44u;
    SET_GPR_S32(ctx, 9, (int32_t)ADD32(GPR_U32(ctx, 9), 4294940228));
    // 0x166f48: 0x8fca0024  lw          $t2, 0x24($fp)
    ctx->pc = 0x166f48u;
    SET_GPR_S32(ctx, 10, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 36)));
    // 0x166f4c: 0xc059bed  jal         func_166FB4
    ctx->pc = 0x166F4Cu;
    SET_GPR_U32(ctx, 31, 0x166F54u);
    ctx->pc = 0x166FB4u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x166FB4u, 0x166F4Cu, 0x166F54u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x166F54u;
label_166f54:
    // 0x166f54: 0x40782d  daddu       $t7, $v0, $zero
    ctx->pc = 0x166f54u;
    SET_GPR_U64(ctx, 15, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
    // 0x166f58: 0xafcf001c  sw          $t7, 0x1C($fp)
    ctx->pc = 0x166f58u;
    WRITE32(ADD32(GPR_U32(ctx, 30), 28), GPR_U32(ctx, 15));
    // 0x166f5c: 0x8fcf001c  lw          $t7, 0x1C($fp)
    ctx->pc = 0x166f5cu;
    SET_GPR_S32(ctx, 15, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 28)));
    // 0x166f60: 0x5e10005  bgez        $t7, . + 4 + (0x5 << 2)
    ctx->pc = 0x166F60u;
    {
        const bool branch_taken_0x166f60 = (GPR_S32(ctx, 15) >= 0);
        if (branch_taken_0x166f60) {
            ctx->pc = 0x166F78u;
            goto label_166f78;
        }
    }
    ctx->pc = 0x166F68u;
    // 0x166f68: 0x8fcf001c  lw          $t7, 0x1C($fp)
    ctx->pc = 0x166f68u;
    SET_GPR_S32(ctx, 15, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 28)));
    // 0x166f6c: 0xafcf0050  sw          $t7, 0x50($fp)
    ctx->pc = 0x166f6cu;
    WRITE32(ADD32(GPR_U32(ctx, 30), 80), GPR_U32(ctx, 15));
    // 0x166f70: 0x10000009  b           . + 4 + (0x9 << 2)
    ctx->pc = 0x166F70u;
    {
        const bool branch_taken_0x166f70 = (GPR_U64(ctx, 0) == GPR_U64(ctx, 0));
        if (branch_taken_0x166f70) {
            ctx->pc = 0x166F98u;
            goto label_166f98;
        }
    }
    ctx->pc = 0x166F78u;
label_166f78:
    // 0x166f78: 0x8fc40024  lw          $a0, 0x24($fp)
    ctx->pc = 0x166f78u;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 36)));
    // Best-effort completion signal, same spirit as SIF.cpp's
    // trySignalEmbeddedSifCmdSema: this WaitSema (below) blocks forever on
    // real hardware's async IOP-response callback, which our SIF/IOP
    // emulation doesn't implement for this call shape. func_166FB4's own
    // internal "submit" step (a separate, inner semaphore) already completes
    // correctly via the existing heuristic, confirming the request really
    // was queued -- so treat that as good enough and unblock the outer wait
    // immediately with a synthetic success result (0), rather than hanging
    // this thread (and, transitively, every other thread blocked behind it)
    // forever. $a0 already holds this wait's semaphore id (loaded above from
    // fp+0x24, the id CreateSema returned earlier in this function); reuse
    // the real SignalSema syscall handler for consistency with the rest of
    // the runtime's semaphore bookkeeping rather than poking sema state by
    // hand. fp+0x20 is the caller-visible "operation result" out-param (read
    // back at 0x166f90 after WaitSema+DeleteSema succeed) -- zero it first so
    // callers checking it don't misinterpret uninitialized stack contents.
    WRITE32(ADD32(GPR_U32(ctx, 30), 32), 0u);
    ps2_syscalls::SignalSema(rdram, ctx, runtime);
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 36)));
    // 0x166f7c: 0xc073e68  jal         func_1CF9A0
    ctx->pc = 0x166F7Cu;
    SET_GPR_U32(ctx, 31, 0x166F84u);
    ctx->pc = 0x1CF9A0u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1CF9A0u, 0x166F7Cu, 0x166F84u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x166F84u;
label_166f84:
    // 0x166f84: 0x8fc40024  lw          $a0, 0x24($fp)
    ctx->pc = 0x166f84u;
    SET_GPR_S32(ctx, 4, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 36)));
    // 0x166f88: 0xc073e5c  jal         func_1CF970
    ctx->pc = 0x166F88u;
    SET_GPR_U32(ctx, 31, 0x166F90u);
    ctx->pc = 0x1CF970u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1CF970u, 0x166F88u, 0x166F90u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;
    }
    ctx->pc = 0x166F90u;
label_166f90:
    // 0x166f90: 0x8fcf0020  lw          $t7, 0x20($fp)
    ctx->pc = 0x166f90u;
    SET_GPR_S32(ctx, 15, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 32)));
    // 0x166f94: 0xafcf0050  sw          $t7, 0x50($fp)
    ctx->pc = 0x166f94u;
    WRITE32(ADD32(GPR_U32(ctx, 30), 80), GPR_U32(ctx, 15));
label_166f98:
    // 0x166f98: 0x8fc20050  lw          $v0, 0x50($fp)
    ctx->pc = 0x166f98u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 30), 80)));
    // 0x166f9c: 0x3c0e82d  daddu       $sp, $fp, $zero
    ctx->pc = 0x166f9cu;
    SET_GPR_U64(ctx, 29, (uint64_t)GPR_U64(ctx, 30) + (uint64_t)GPR_U64(ctx, 0));
    // 0x166fa0: 0xdfbe0060  ld          $fp, 0x60($sp)
    ctx->pc = 0x166fa0u;
    SET_GPR_U64(ctx, 30, READ64(ADD32(GPR_U32(ctx, 29), 96)));
    // 0x166fa4: 0xdfbf0068  ld          $ra, 0x68($sp)
    ctx->pc = 0x166fa4u;
    SET_GPR_U64(ctx, 31, READ64(ADD32(GPR_U32(ctx, 29), 104)));
    // 0x166fa8: 0x27bd0070  addiu       $sp, $sp, 0x70
    ctx->pc = 0x166fa8u;
    SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 112));
    // 0x166fac: 0x3e00008  jr          $ra
    ctx->pc = 0x166FACu;
    {
        const uint32_t jumpTarget = GPR_U32(ctx, 31);
        ctx->pc = jumpTarget;
        #if defined(PS2X_STRICT_RETURN_DIAGNOSTICS) && PS2X_STRICT_RETURN_DIAGNOSTICS
        (void)runtime->dispatchGuestBranch(rdram, ctx, jumpTarget, 0x166FACu, 0u, PS2Runtime::GuestBranchKind::Return, "JR $ra");
        return;
        #else
        ctx->pc = jumpTarget;
        return;
        #endif
    }
    ctx->pc = 0x166FB4u;
}
