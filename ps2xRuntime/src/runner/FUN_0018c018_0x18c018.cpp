#include <stdexcept>
#include "ps2_runtime_macros.h"
#include "ps2_runtime.h"
#include "ps2_recompiled_functions.h"
#include "ps2_recompiled_stubs.h"

#include "ps2_syscalls.h"
#include "ps2_stubs.h"
#include <iostream>

#ifdef PS2_FUNCTION_LOG_TRACKER
#include "ps2_log.h"
#endif

// Function: FUN_0018c018
// Address: 0x18c018 - 0x18c870
void FUN_0018c018_0x18c018(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
#ifdef PS2_FUNCTION_LOG_TRACKER
    PS_LOG_ENTRY("FUN_0018c018_0x18c018");
#endif

    std::cerr << "[DIAG:18c018-entry] pc=0x" << std::hex << ctx->pc << " sp=0x" << GPR_U32(ctx, 29) << std::dec << std::endl;

    switch (ctx->pc) {
        case 0x18c100u: goto label_18c100;
        case 0x18c124u: goto label_18c124;
        case 0x18c184u: goto label_18c184;
        case 0x18c1a8u: goto label_18c1a8;
        case 0x18c1d8u: goto label_18c1d8;
        case 0x18c208u: goto label_18c208;
        case 0x18c22cu: goto label_18c22c;
        case 0x18c254u: goto label_18c254;
        case 0x18c260u: goto label_18c260;
        case 0x18c274u: goto label_18c274;
        case 0x18c2c8u: goto label_18c2c8;
        case 0x18c344u: goto label_18c344;
        case 0x18c3fcu: goto label_18c3fc;
        case 0x18c420u: goto label_18c420;
        case 0x18c444u: goto label_18c444;
        case 0x18c468u: goto label_18c468;
        case 0x18c4d4u: goto label_18c4d4;
        case 0x18c4e0u: goto label_18c4e0;
        case 0x18c514u: goto label_18c514;
        case 0x18c52cu: goto label_18c52c;
        case 0x18c584u: goto label_18c584;
        case 0x18c598u: goto label_18c598;
        case 0x18c5bcu: goto label_18c5bc;
        case 0x18c5c4u: goto label_18c5c4;
        case 0x18c5d8u: goto label_18c5d8;
        case 0x18c600u: goto label_18c600;
        case 0x18c644u: goto label_18c644;
        case 0x18c688u: goto label_18c688;
        case 0x18c6bcu: goto label_18c6bc;
        case 0x18c6e8u: goto label_18c6e8;
        case 0x18c6f0u: goto label_18c6f0;
        case 0x18c6f8u: goto label_18c6f8;
        case 0x18c70cu: goto label_18c70c;
        case 0x18c72cu: goto label_18c72c;
        case 0x18c790u: goto label_18c790;
        case 0x18c798u: goto label_18c798;
        case 0x18c7a0u: goto label_18c7a0;
        case 0x18c7a8u: goto label_18c7a8;
        case 0x18c7b0u: goto label_18c7b0;
        case 0x18c7c4u: goto label_18c7c4;
        case 0x18c7ccu: goto label_18c7cc;
        case 0x18c7d8u: goto label_18c7d8;
        case 0x18c81cu: goto label_18c81c;
        case 0x18c840u: goto label_18c840;
        case 0x18c854u: goto label_18c854;
        default: break;
    }

    ctx->pc = 0x18c018u;

    // 0x18c018: 0x27bdffd0  addiu       $sp, $sp, -0x30
    ctx->pc = 0x18c018u;
    SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 4294967248));
    // 0x18c01c: 0x340fffff  ori         $t7, $zero, 0xFFFF
    ctx->pc = 0x18c01cu;
    SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
    // 0x18c020: 0xffb20010  sd          $s2, 0x10($sp)
    ctx->pc = 0x18c020u;
    WRITE64(ADD32(GPR_U32(ctx, 29), 16), GPR_U64(ctx, 18));
    // 0x18c024: 0xffb30018  sd          $s3, 0x18($sp)
    ctx->pc = 0x18c024u;
    WRITE64(ADD32(GPR_U32(ctx, 29), 24), GPR_U64(ctx, 19));
    // 0x18c028: 0xffb00000  sd          $s0, 0x0($sp)
    ctx->pc = 0x18c028u;
    WRITE64(ADD32(GPR_U32(ctx, 29), 0), GPR_U64(ctx, 16));
    // 0x18c02c: 0xa0902d  daddu       $s2, $a1, $zero
    ctx->pc = 0x18c02cu;
    SET_GPR_U64(ctx, 18, (uint64_t)GPR_U64(ctx, 5) + (uint64_t)GPR_U64(ctx, 0));
    // 0x18c030: 0xffb10008  sd          $s1, 0x8($sp)
    ctx->pc = 0x18c030u;
    WRITE64(ADD32(GPR_U32(ctx, 29), 8), GPR_U64(ctx, 17));
    // 0x18c034: 0xffb40020  sd          $s4, 0x20($sp)
    ctx->pc = 0x18c034u;
    WRITE64(ADD32(GPR_U32(ctx, 29), 32), GPR_U64(ctx, 20));
    // 0x18c038: 0xffbf0028  sd          $ra, 0x28($sp)
    ctx->pc = 0x18c038u;
    WRITE64(ADD32(GPR_U32(ctx, 29), 40), GPR_U64(ctx, 31));
    // 0x18c03c: 0x14af0019  bne         $a1, $t7, . + 4 + (0x19 << 2)
    ctx->pc = 0x18C03Cu;
    {
        const bool branch_taken_0x18c03c = (GPR_U64(ctx, 5) != GPR_U64(ctx, 15));
        ctx->pc = 0x18C040u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C03Cu;
        // 0x18c040: 0x80982d  daddu       $s3, $a0, $zero (Delay Slot)
        SET_GPR_U64(ctx, 19, (uint64_t)GPR_U64(ctx, 4) + (uint64_t)GPR_U64(ctx, 0));
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c03c) {
            ctx->pc = 0x18C0A4u;
            goto label_18c0a4;
        }
    }
    ctx->pc = 0x18C044u;
    // 0x18c044: 0x240f0001  addiu       $t7, $zero, 0x1
    ctx->pc = 0x18c044u;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 0), 1));
    // 0x18c048: 0x148f0016  bne         $a0, $t7, . + 4 + (0x16 << 2)
    ctx->pc = 0x18C048u;
    {
        const bool branch_taken_0x18c048 = (GPR_U64(ctx, 4) != GPR_U64(ctx, 15));
        ctx->pc = 0x18C04Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C048u;
        // 0x18c04c: 0x340fffff  ori         $t7, $zero, 0xFFFF (Delay Slot)
        SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c048) {
            ctx->pc = 0x18C0A4u;
            goto label_18c0a4;
        }
    }
    ctx->pc = 0x18C050u;
    // 0x18c050: 0x3c0e0027  lui         $t6, 0x27
    ctx->pc = 0x18c050u;
    SET_GPR_S32(ctx, 14, (int32_t)((uint32_t)39 << 16));
    // 0x18c054: 0x3c0f0027  lui         $t7, 0x27
    ctx->pc = 0x18c054u;
    SET_GPR_S32(ctx, 15, (int32_t)((uint32_t)39 << 16));
    // 0x18c058: 0xadc0c4c0  sw          $zero, -0x3B40($t6)
    ctx->pc = 0x18c058u;
    do { uint32_t _value = static_cast<uint32_t>(GPR_U32(ctx, 0)); ps2TraceGuestWrite(rdram, 0x26C4C0u, 4u, _value, 0u, "WRITE32", ctx); FAST_WRITE32(0x26C4C0u, _value); } while (0);
    // 0x18c05c: 0x25cdc4c0  addiu       $t5, $t6, -0x3B40
    ctx->pc = 0x18c05cu;
    SET_GPR_S32(ctx, 13, (int32_t)ADD32(GPR_U32(ctx, 14), 4294952128));
    // 0x18c060: 0xade0c4e4  sw          $zero, -0x3B1C($t7)
    ctx->pc = 0x18c060u;
    do { uint32_t _value = static_cast<uint32_t>(GPR_U32(ctx, 0)); ps2TraceGuestWrite(rdram, 0x26C4E4u, 4u, _value, 0u, "WRITE32", ctx); FAST_WRITE32(0x26C4E4u, _value); } while (0);
    // 0x18c064: 0x3c0e0027  lui         $t6, 0x27
    ctx->pc = 0x18c064u;
    SET_GPR_S32(ctx, 14, (int32_t)((uint32_t)39 << 16));
    // 0x18c068: 0xada00040  sw          $zero, 0x40($t5)
    ctx->pc = 0x18c068u;
    do { uint32_t _value = static_cast<uint32_t>(GPR_U32(ctx, 0)); ps2TraceGuestWrite(rdram, 0x26C500u, 4u, _value, 0u, "WRITE32", ctx); FAST_WRITE32(0x26C500u, _value); } while (0);
    // 0x18c06c: 0xadc0c4ec  sw          $zero, -0x3B14($t6)
    ctx->pc = 0x18c06cu;
    do { uint32_t _value = static_cast<uint32_t>(GPR_U32(ctx, 0)); ps2TraceGuestWrite(rdram, 0x26C4ECu, 4u, _value, 0u, "WRITE32", ctx); FAST_WRITE32(0x26C4ECu, _value); } while (0);
    // 0x18c070: 0x3c0f0027  lui         $t7, 0x27
    ctx->pc = 0x18c070u;
    SET_GPR_S32(ctx, 15, (int32_t)((uint32_t)39 << 16));
    // 0x18c074: 0xade0c4f4  sw          $zero, -0x3B0C($t7)
    ctx->pc = 0x18c074u;
    do { uint32_t _value = static_cast<uint32_t>(GPR_U32(ctx, 0)); ps2TraceGuestWrite(rdram, 0x26C4F4u, 4u, _value, 0u, "WRITE32", ctx); FAST_WRITE32(0x26C4F4u, _value); } while (0);
    // 0x18c078: 0x3c0e0027  lui         $t6, 0x27
    ctx->pc = 0x18c078u;
    SET_GPR_S32(ctx, 14, (int32_t)((uint32_t)39 << 16));
    // 0x18c07c: 0xada00028  sw          $zero, 0x28($t5)
    ctx->pc = 0x18c07cu;
    do { uint32_t _value = static_cast<uint32_t>(GPR_U32(ctx, 0)); ps2TraceGuestWrite(rdram, 0x26C4E8u, 4u, _value, 0u, "WRITE32", ctx); FAST_WRITE32(0x26C4E8u, _value); } while (0);
    // 0x18c080: 0xadc0c4fc  sw          $zero, -0x3B04($t6)
    ctx->pc = 0x18c080u;
    do { uint32_t _value = static_cast<uint32_t>(GPR_U32(ctx, 0)); ps2TraceGuestWrite(rdram, 0x26C4FCu, 4u, _value, 0u, "WRITE32", ctx); FAST_WRITE32(0x26C4FCu, _value); } while (0);
    // 0x18c084: 0xada00030  sw          $zero, 0x30($t5)
    ctx->pc = 0x18c084u;
    do { uint32_t _value = static_cast<uint32_t>(GPR_U32(ctx, 0)); ps2TraceGuestWrite(rdram, 0x26C4F0u, 4u, _value, 0u, "WRITE32", ctx); FAST_WRITE32(0x26C4F0u, _value); } while (0);
    // 0x18c088: 0xada00004  sw          $zero, 0x4($t5)
    ctx->pc = 0x18c088u;
    do { uint32_t _value = static_cast<uint32_t>(GPR_U32(ctx, 0)); ps2TraceGuestWrite(rdram, 0x26C4C4u, 4u, _value, 0u, "WRITE32", ctx); FAST_WRITE32(0x26C4C4u, _value); } while (0);
    // 0x18c08c: 0xada00010  sw          $zero, 0x10($t5)
    ctx->pc = 0x18c08cu;
    do { uint32_t _value = static_cast<uint32_t>(GPR_U32(ctx, 0)); ps2TraceGuestWrite(rdram, 0x26C4D0u, 4u, _value, 0u, "WRITE32", ctx); FAST_WRITE32(0x26C4D0u, _value); } while (0);
    // 0x18c090: 0xada00014  sw          $zero, 0x14($t5)
    ctx->pc = 0x18c090u;
    do { uint32_t _value = static_cast<uint32_t>(GPR_U32(ctx, 0)); ps2TraceGuestWrite(rdram, 0x26C4D4u, 4u, _value, 0u, "WRITE32", ctx); FAST_WRITE32(0x26C4D4u, _value); } while (0);
    // 0x18c094: 0xada00008  sw          $zero, 0x8($t5)
    ctx->pc = 0x18c094u;
    do { uint32_t _value = static_cast<uint32_t>(GPR_U32(ctx, 0)); ps2TraceGuestWrite(rdram, 0x26C4C8u, 4u, _value, 0u, "WRITE32", ctx); FAST_WRITE32(0x26C4C8u, _value); } while (0);
    // 0x18c098: 0xada0000c  sw          $zero, 0xC($t5)
    ctx->pc = 0x18c098u;
    do { uint32_t _value = static_cast<uint32_t>(GPR_U32(ctx, 0)); ps2TraceGuestWrite(rdram, 0x26C4CCu, 4u, _value, 0u, "WRITE32", ctx); FAST_WRITE32(0x26C4CCu, _value); } while (0);
    // 0x18c09c: 0xada00038  sw          $zero, 0x38($t5)
    ctx->pc = 0x18c09cu;
    do { uint32_t _value = static_cast<uint32_t>(GPR_U32(ctx, 0)); ps2TraceGuestWrite(rdram, 0x26C4F8u, 4u, _value, 0u, "WRITE32", ctx); FAST_WRITE32(0x26C4F8u, _value); } while (0);
    // 0x18c0a0: 0x340fffff  ori         $t7, $zero, 0xFFFF
    ctx->pc = 0x18c0a0u;
    SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
label_18c0a4:
    // 0x18c0a4: 0x164f0017  bne         $s2, $t7, . + 4 + (0x17 << 2)
    ctx->pc = 0x18C0A4u;
    {
        const bool branch_taken_0x18c0a4 = (GPR_U64(ctx, 18) != GPR_U64(ctx, 15));
        ctx->pc = 0x18C0A8u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C0A4u;
        // 0x18c0a8: 0x340fffff  ori         $t7, $zero, 0xFFFF (Delay Slot)
        SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c0a4) {
            ctx->pc = 0x18C104u;
            goto label_18c104;
        }
    }
    ctx->pc = 0x18C0ACu;
    // 0x18c0ac: 0x240f0001  addiu       $t7, $zero, 0x1
    ctx->pc = 0x18c0acu;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 0), 1));
    // 0x18c0b0: 0x166f0014  bne         $s3, $t7, . + 4 + (0x14 << 2)
    ctx->pc = 0x18C0B0u;
    {
        const bool branch_taken_0x18c0b0 = (GPR_U64(ctx, 19) != GPR_U64(ctx, 15));
        ctx->pc = 0x18C0B4u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C0B0u;
        // 0x18c0b4: 0x340fffff  ori         $t7, $zero, 0xFFFF (Delay Slot)
        SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c0b0) {
            ctx->pc = 0x18C104u;
            goto label_18c104;
        }
    }
    ctx->pc = 0x18C0B8u;
    // 0x18c0b8: 0x3c0e002d  lui         $t6, 0x2D
    ctx->pc = 0x18c0b8u;
    SET_GPR_S32(ctx, 14, (int32_t)((uint32_t)45 << 16));
    // 0x18c0bc: 0x3c0b0027  lui         $t3, 0x27
    ctx->pc = 0x18c0bcu;
    SET_GPR_S32(ctx, 11, (int32_t)((uint32_t)39 << 16));
    // 0x18c0c0: 0x25cef6e0  addiu       $t6, $t6, -0x920
    ctx->pc = 0x18c0c0u;
    SET_GPR_S32(ctx, 14, (int32_t)ADD32(GPR_U32(ctx, 14), 4294964960));
    // 0x18c0c4: 0x3c0f0027  lui         $t7, 0x27
    ctx->pc = 0x18c0c4u;
    SET_GPR_S32(ctx, 15, (int32_t)((uint32_t)39 << 16));
    // 0x18c0c8: 0xadeec740  sw          $t6, -0x38C0($t7)
    ctx->pc = 0x18c0c8u;
    do { uint32_t _value = static_cast<uint32_t>(GPR_U32(ctx, 14)); ps2TraceGuestWrite(rdram, 0x26C740u, 4u, _value, 0u, "WRITE32", ctx); FAST_WRITE32(0x26C740u, _value); } while (0);
    // 0x18c0cc: 0x256dc540  addiu       $t5, $t3, -0x3AC0
    ctx->pc = 0x18c0ccu;
    SET_GPR_S32(ctx, 13, (int32_t)ADD32(GPR_U32(ctx, 11), 4294952256));
    // 0x18c0d0: 0xad6ec540  sw          $t6, -0x3AC0($t3)
    ctx->pc = 0x18c0d0u;
    do { uint32_t _value = static_cast<uint32_t>(GPR_U32(ctx, 14)); ps2TraceGuestWrite(rdram, 0x26C540u, 4u, _value, 0u, "WRITE32", ctx); FAST_WRITE32(0x26C540u, _value); } while (0);
    // 0x18c0d4: 0x25ac0200  addiu       $t4, $t5, 0x200
    ctx->pc = 0x18c0d4u;
    SET_GPR_S32(ctx, 12, (int32_t)ADD32(GPR_U32(ctx, 13), 512));
    // 0x18c0d8: 0x25a40400  addiu       $a0, $t5, 0x400
    ctx->pc = 0x18c0d8u;
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 13), 1024));
    // 0x18c0dc: 0xad800010  sw          $zero, 0x10($t4)
    ctx->pc = 0x18c0dcu;
    do { uint32_t _value = static_cast<uint32_t>(GPR_U32(ctx, 0)); ps2TraceGuestWrite(rdram, 0x26C750u, 4u, _value, 0u, "WRITE32", ctx); FAST_WRITE32(0x26C750u, _value); } while (0);
    // 0x18c0e0: 0xada00004  sw          $zero, 0x4($t5)
    ctx->pc = 0x18c0e0u;
    do { uint32_t _value = static_cast<uint32_t>(GPR_U32(ctx, 0)); ps2TraceGuestWrite(rdram, 0x26C544u, 4u, _value, 0u, "WRITE32", ctx); FAST_WRITE32(0x26C544u, _value); } while (0);
    // 0x18c0e4: 0xada00008  sw          $zero, 0x8($t5)
    ctx->pc = 0x18c0e4u;
    do { uint32_t _value = static_cast<uint32_t>(GPR_U32(ctx, 0)); ps2TraceGuestWrite(rdram, 0x26C548u, 4u, _value, 0u, "WRITE32", ctx); FAST_WRITE32(0x26C548u, _value); } while (0);
    // 0x18c0e8: 0xada0000c  sw          $zero, 0xC($t5)
    ctx->pc = 0x18c0e8u;
    do { uint32_t _value = static_cast<uint32_t>(GPR_U32(ctx, 0)); ps2TraceGuestWrite(rdram, 0x26C54Cu, 4u, _value, 0u, "WRITE32", ctx); FAST_WRITE32(0x26C54Cu, _value); } while (0);
    // 0x18c0ec: 0xada00010  sw          $zero, 0x10($t5)
    ctx->pc = 0x18c0ecu;
    do { uint32_t _value = static_cast<uint32_t>(GPR_U32(ctx, 0)); ps2TraceGuestWrite(rdram, 0x26C550u, 4u, _value, 0u, "WRITE32", ctx); FAST_WRITE32(0x26C550u, _value); } while (0);
    // 0x18c0f0: 0xad800004  sw          $zero, 0x4($t4)
    ctx->pc = 0x18c0f0u;
    do { uint32_t _value = static_cast<uint32_t>(GPR_U32(ctx, 0)); ps2TraceGuestWrite(rdram, 0x26C744u, 4u, _value, 0u, "WRITE32", ctx); FAST_WRITE32(0x26C744u, _value); } while (0);
    // 0x18c0f4: 0xad800008  sw          $zero, 0x8($t4)
    ctx->pc = 0x18c0f4u;
    do { uint32_t _value = static_cast<uint32_t>(GPR_U32(ctx, 0)); ps2TraceGuestWrite(rdram, 0x26C748u, 4u, _value, 0u, "WRITE32", ctx); FAST_WRITE32(0x26C748u, _value); } while (0);
    // 0x18c0f8: 0xc088178  jal         func_2205E0
    ctx->pc = 0x18C0F8u;
    SET_GPR_U32(ctx, 31, 0x18C100u);
    ctx->pc = 0x18C0FCu;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x18C0F8u;
    // 0x18c0fc: 0xad80000c  sw          $zero, 0xC($t4) (Delay Slot)
    WRITE32(ADD32(GPR_U32(ctx, 12), 12), GPR_U32(ctx, 0));
    ctx->in_delay_slot = false;
    ctx->pc = 0x2205E0u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x2205E0u, 0x18C0F8u, 0x18C100u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        std::cerr << "[DIAG:18c018-callfail] target=0x2205E0u src=0x18C0F8u fallthrough=0x18C100u sp=0x" << std::hex << GPR_U32(ctx, 29) << " pc=0x" << ctx->pc << std::dec << std::endl;
        return;
    }
    std::cerr << "[DIAG:18c018-ok] after_fallthrough=0x18C100u sp=0x" << std::hex << GPR_U32(ctx, 29) << std::dec << std::endl;
    ctx->pc = 0x18C100u;
label_18c100:
    // 0x18c100: 0x340fffff  ori         $t7, $zero, 0xFFFF
    ctx->pc = 0x18c100u;
    SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
label_18c104:
    // 0x18c104: 0x164f0008  bne         $s2, $t7, . + 4 + (0x8 << 2)
    ctx->pc = 0x18C104u;
    {
        const bool branch_taken_0x18c104 = (GPR_U64(ctx, 18) != GPR_U64(ctx, 15));
        ctx->pc = 0x18C108u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C104u;
        // 0x18c108: 0x340fffff  ori         $t7, $zero, 0xFFFF (Delay Slot)
        SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c104) {
            ctx->pc = 0x18C128u;
            goto label_18c128;
        }
    }
    ctx->pc = 0x18C10Cu;
    // 0x18c10c: 0x240f0001  addiu       $t7, $zero, 0x1
    ctx->pc = 0x18c10cu;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 0), 1));
    // 0x18c110: 0x166f0005  bne         $s3, $t7, . + 4 + (0x5 << 2)
    ctx->pc = 0x18C110u;
    {
        const bool branch_taken_0x18c110 = (GPR_U64(ctx, 19) != GPR_U64(ctx, 15));
        ctx->pc = 0x18C114u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C110u;
        // 0x18c114: 0x340fffff  ori         $t7, $zero, 0xFFFF (Delay Slot)
        SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c110) {
            ctx->pc = 0x18C128u;
            goto label_18c128;
        }
    }
    ctx->pc = 0x18C118u;
    // 0x18c118: 0x3c040027  lui         $a0, 0x27
    ctx->pc = 0x18c118u;
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)39 << 16));
    // 0x18c11c: 0xc08806c  jal         func_2201B0
    ctx->pc = 0x18C11Cu;
    SET_GPR_U32(ctx, 31, 0x18C124u);
    ctx->pc = 0x18C120u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x18C11Cu;
    // 0x18c120: 0x2484c980  addiu       $a0, $a0, -0x3680 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 4), 4294953344));
    ctx->in_delay_slot = false;
    ctx->pc = 0x2201B0u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x2201B0u, 0x18C11Cu, 0x18C124u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        std::cerr << "[DIAG:18c018-callfail] target=0x2201B0u src=0x18C11Cu fallthrough=0x18C124u sp=0x" << std::hex << GPR_U32(ctx, 29) << " pc=0x" << ctx->pc << std::dec << std::endl;
        return;
    }
    std::cerr << "[DIAG:18c018-ok] after_fallthrough=0x18C124u sp=0x" << std::hex << GPR_U32(ctx, 29) << std::dec << std::endl;
    ctx->pc = 0x18C124u;
label_18c124:
    // 0x18c124: 0x340fffff  ori         $t7, $zero, 0xFFFF
    ctx->pc = 0x18c124u;
    SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
label_18c128:
    // 0x18c128: 0x164f000e  bne         $s2, $t7, . + 4 + (0xE << 2)
    ctx->pc = 0x18C128u;
    {
        const bool branch_taken_0x18c128 = (GPR_U64(ctx, 18) != GPR_U64(ctx, 15));
        ctx->pc = 0x18C12Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C128u;
        // 0x18c12c: 0x340fffff  ori         $t7, $zero, 0xFFFF (Delay Slot)
        SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c128) {
            ctx->pc = 0x18C164u;
            goto label_18c164;
        }
    }
    ctx->pc = 0x18C130u;
    // 0x18c130: 0x240f0001  addiu       $t7, $zero, 0x1
    ctx->pc = 0x18c130u;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 0), 1));
    // 0x18c134: 0x166f000b  bne         $s3, $t7, . + 4 + (0xB << 2)
    ctx->pc = 0x18C134u;
    {
        const bool branch_taken_0x18c134 = (GPR_U64(ctx, 19) != GPR_U64(ctx, 15));
        ctx->pc = 0x18C138u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C134u;
        // 0x18c138: 0x340fffff  ori         $t7, $zero, 0xFFFF (Delay Slot)
        SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c134) {
            ctx->pc = 0x18C164u;
            goto label_18c164;
        }
    }
    ctx->pc = 0x18C13Cu;
    // 0x18c13c: 0x3c0f002d  lui         $t7, 0x2D
    ctx->pc = 0x18c13cu;
    SET_GPR_S32(ctx, 15, (int32_t)((uint32_t)45 << 16));
    // 0x18c140: 0x3c0d0027  lui         $t5, 0x27
    ctx->pc = 0x18c140u;
    SET_GPR_S32(ctx, 13, (int32_t)((uint32_t)39 << 16));
    // 0x18c144: 0x25eff810  addiu       $t7, $t7, -0x7F0
    ctx->pc = 0x18c144u;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 15), 4294965264));
    // 0x18c148: 0x25aecb80  addiu       $t6, $t5, -0x3480
    ctx->pc = 0x18c148u;
    SET_GPR_S32(ctx, 14, (int32_t)ADD32(GPR_U32(ctx, 13), 4294953856));
    // 0x18c14c: 0xadafcb80  sw          $t7, -0x3480($t5)
    ctx->pc = 0x18c14cu;
    do { uint32_t _value = static_cast<uint32_t>(GPR_U32(ctx, 15)); ps2TraceGuestWrite(rdram, 0x26CB80u, 4u, _value, 0u, "WRITE32", ctx); FAST_WRITE32(0x26CB80u, _value); } while (0);
    // 0x18c150: 0xadc00010  sw          $zero, 0x10($t6)
    ctx->pc = 0x18c150u;
    do { uint32_t _value = static_cast<uint32_t>(GPR_U32(ctx, 0)); ps2TraceGuestWrite(rdram, 0x26CB90u, 4u, _value, 0u, "WRITE32", ctx); FAST_WRITE32(0x26CB90u, _value); } while (0);
    // 0x18c154: 0xadc00004  sw          $zero, 0x4($t6)
    ctx->pc = 0x18c154u;
    do { uint32_t _value = static_cast<uint32_t>(GPR_U32(ctx, 0)); ps2TraceGuestWrite(rdram, 0x26CB84u, 4u, _value, 0u, "WRITE32", ctx); FAST_WRITE32(0x26CB84u, _value); } while (0);
    // 0x18c158: 0xadc00008  sw          $zero, 0x8($t6)
    ctx->pc = 0x18c158u;
    do { uint32_t _value = static_cast<uint32_t>(GPR_U32(ctx, 0)); ps2TraceGuestWrite(rdram, 0x26CB88u, 4u, _value, 0u, "WRITE32", ctx); FAST_WRITE32(0x26CB88u, _value); } while (0);
    // 0x18c15c: 0xadc0000c  sw          $zero, 0xC($t6)
    ctx->pc = 0x18c15cu;
    do { uint32_t _value = static_cast<uint32_t>(GPR_U32(ctx, 0)); ps2TraceGuestWrite(rdram, 0x26CB8Cu, 4u, _value, 0u, "WRITE32", ctx); FAST_WRITE32(0x26CB8Cu, _value); } while (0);
    // 0x18c160: 0x340fffff  ori         $t7, $zero, 0xFFFF
    ctx->pc = 0x18c160u;
    SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
label_18c164:
    // 0x18c164: 0x164f0008  bne         $s2, $t7, . + 4 + (0x8 << 2)
    ctx->pc = 0x18C164u;
    {
        const bool branch_taken_0x18c164 = (GPR_U64(ctx, 18) != GPR_U64(ctx, 15));
        ctx->pc = 0x18C168u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C164u;
        // 0x18c168: 0x340fffff  ori         $t7, $zero, 0xFFFF (Delay Slot)
        SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c164) {
            ctx->pc = 0x18C188u;
            goto label_18c188;
        }
    }
    ctx->pc = 0x18C16Cu;
    // 0x18c16c: 0x240f0001  addiu       $t7, $zero, 0x1
    ctx->pc = 0x18c16cu;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 0), 1));
    // 0x18c170: 0x166f0005  bne         $s3, $t7, . + 4 + (0x5 << 2)
    ctx->pc = 0x18C170u;
    {
        const bool branch_taken_0x18c170 = (GPR_U64(ctx, 19) != GPR_U64(ctx, 15));
        ctx->pc = 0x18C174u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C170u;
        // 0x18c174: 0x340fffff  ori         $t7, $zero, 0xFFFF (Delay Slot)
        SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c170) {
            ctx->pc = 0x18C188u;
            goto label_18c188;
        }
    }
    ctx->pc = 0x18C178u;
    // 0x18c178: 0x3c040027  lui         $a0, 0x27
    ctx->pc = 0x18c178u;
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)39 << 16));
    // 0x18c17c: 0xc088088  jal         func_220220
    ctx->pc = 0x18C17Cu;
    SET_GPR_U32(ctx, 31, 0x18C184u);
    ctx->pc = 0x18C180u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x18C17Cu;
    // 0x18c180: 0x2484cd80  addiu       $a0, $a0, -0x3280 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 4), 4294954368));
    ctx->in_delay_slot = false;
    ctx->pc = 0x220220u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x220220u, 0x18C17Cu, 0x18C184u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        std::cerr << "[DIAG:18c018-callfail] target=0x220220u src=0x18C17Cu fallthrough=0x18C184u sp=0x" << std::hex << GPR_U32(ctx, 29) << " pc=0x" << ctx->pc << std::dec << std::endl;
        return;
    }
    std::cerr << "[DIAG:18c018-ok] after_fallthrough=0x18C184u sp=0x" << std::hex << GPR_U32(ctx, 29) << std::dec << std::endl;
    ctx->pc = 0x18C184u;
label_18c184:
    // 0x18c184: 0x340fffff  ori         $t7, $zero, 0xFFFF
    ctx->pc = 0x18c184u;
    SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
label_18c188:
    // 0x18c188: 0x164f000b  bne         $s2, $t7, . + 4 + (0xB << 2)
    ctx->pc = 0x18C188u;
    {
        const bool branch_taken_0x18c188 = (GPR_U64(ctx, 18) != GPR_U64(ctx, 15));
        ctx->pc = 0x18C18Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C188u;
        // 0x18c18c: 0x340fffff  ori         $t7, $zero, 0xFFFF (Delay Slot)
        SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c188) {
            ctx->pc = 0x18C1B8u;
            goto label_18c1b8;
        }
    }
    ctx->pc = 0x18C190u;
    // 0x18c190: 0x240f0001  addiu       $t7, $zero, 0x1
    ctx->pc = 0x18c190u;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 0), 1));
    // 0x18c194: 0x166f0008  bne         $s3, $t7, . + 4 + (0x8 << 2)
    ctx->pc = 0x18C194u;
    {
        const bool branch_taken_0x18c194 = (GPR_U64(ctx, 19) != GPR_U64(ctx, 15));
        ctx->pc = 0x18C198u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C194u;
        // 0x18c198: 0x340fffff  ori         $t7, $zero, 0xFFFF (Delay Slot)
        SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c194) {
            ctx->pc = 0x18C1B8u;
            goto label_18c1b8;
        }
    }
    ctx->pc = 0x18C19Cu;
    // 0x18c19c: 0x3c100027  lui         $s0, 0x27
    ctx->pc = 0x18c19cu;
    SET_GPR_S32(ctx, 16, (int32_t)((uint32_t)39 << 16));
    // 0x18c1a0: 0xc0880d8  jal         func_220360
    ctx->pc = 0x18C1A0u;
    SET_GPR_U32(ctx, 31, 0x18C1A8u);
    ctx->pc = 0x18C1A4u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x18C1A0u;
    // 0x18c1a4: 0x2604d740  addiu       $a0, $s0, -0x28C0 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 16), 4294956864));
    ctx->in_delay_slot = false;
    ctx->pc = 0x220360u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x220360u, 0x18C1A0u, 0x18C1A8u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        std::cerr << "[DIAG:18c018-callfail] target=0x220360u src=0x18C1A0u fallthrough=0x18C1A8u sp=0x" << std::hex << GPR_U32(ctx, 29) << " pc=0x" << ctx->pc << std::dec << std::endl;
        return;
    }
    std::cerr << "[DIAG:18c018-ok] after_fallthrough=0x18C1A8u sp=0x" << std::hex << GPR_U32(ctx, 29) << std::dec << std::endl;
    ctx->pc = 0x18C1A8u;
label_18c1a8:
    // 0x18c1a8: 0x3c0f002d  lui         $t7, 0x2D
    ctx->pc = 0x18c1a8u;
    SET_GPR_S32(ctx, 15, (int32_t)((uint32_t)45 << 16));
    // 0x18c1ac: 0x25eff6d0  addiu       $t7, $t7, -0x930
    ctx->pc = 0x18c1acu;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 15), 4294964944));
    // 0x18c1b0: 0xae0fd740  sw          $t7, -0x28C0($s0)
    ctx->pc = 0x18c1b0u;
    WRITE32(ADD32(GPR_U32(ctx, 16), 4294956864), GPR_U32(ctx, 15));
    // 0x18c1b4: 0x340fffff  ori         $t7, $zero, 0xFFFF
    ctx->pc = 0x18c1b4u;
    SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
label_18c1b8:
    // 0x18c1b8: 0x164f000b  bne         $s2, $t7, . + 4 + (0xB << 2)
    ctx->pc = 0x18C1B8u;
    {
        const bool branch_taken_0x18c1b8 = (GPR_U64(ctx, 18) != GPR_U64(ctx, 15));
        ctx->pc = 0x18C1BCu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C1B8u;
        // 0x18c1bc: 0x340fffff  ori         $t7, $zero, 0xFFFF (Delay Slot)
        SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c1b8) {
            ctx->pc = 0x18C1E8u;
            goto label_18c1e8;
        }
    }
    ctx->pc = 0x18C1C0u;
    // 0x18c1c0: 0x240f0001  addiu       $t7, $zero, 0x1
    ctx->pc = 0x18c1c0u;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 0), 1));
    // 0x18c1c4: 0x166f0008  bne         $s3, $t7, . + 4 + (0x8 << 2)
    ctx->pc = 0x18C1C4u;
    {
        const bool branch_taken_0x18c1c4 = (GPR_U64(ctx, 19) != GPR_U64(ctx, 15));
        ctx->pc = 0x18C1C8u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C1C4u;
        // 0x18c1c8: 0x340fffff  ori         $t7, $zero, 0xFFFF (Delay Slot)
        SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c1c4) {
            ctx->pc = 0x18C1E8u;
            goto label_18c1e8;
        }
    }
    ctx->pc = 0x18C1CCu;
    // 0x18c1cc: 0x3c100028  lui         $s0, 0x28
    ctx->pc = 0x18c1ccu;
    SET_GPR_S32(ctx, 16, (int32_t)((uint32_t)40 << 16));
    // 0x18c1d0: 0xc0880d8  jal         func_220360
    ctx->pc = 0x18C1D0u;
    SET_GPR_U32(ctx, 31, 0x18C1D8u);
    ctx->pc = 0x18C1D4u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x18C1D0u;
    // 0x18c1d4: 0x260417c0  addiu       $a0, $s0, 0x17C0 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 16), 6080));
    ctx->in_delay_slot = false;
    ctx->pc = 0x220360u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x220360u, 0x18C1D0u, 0x18C1D8u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        std::cerr << "[DIAG:18c018-callfail] target=0x220360u src=0x18C1D0u fallthrough=0x18C1D8u sp=0x" << std::hex << GPR_U32(ctx, 29) << " pc=0x" << ctx->pc << std::dec << std::endl;
        return;
    }
    std::cerr << "[DIAG:18c018-ok] after_fallthrough=0x18C1D8u sp=0x" << std::hex << GPR_U32(ctx, 29) << std::dec << std::endl;
    ctx->pc = 0x18C1D8u;
label_18c1d8:
    // 0x18c1d8: 0x3c0f002d  lui         $t7, 0x2D
    ctx->pc = 0x18c1d8u;
    SET_GPR_S32(ctx, 15, (int32_t)((uint32_t)45 << 16));
    // 0x18c1dc: 0x25eff6c0  addiu       $t7, $t7, -0x940
    ctx->pc = 0x18c1dcu;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 15), 4294964928));
    // 0x18c1e0: 0xae0f17c0  sw          $t7, 0x17C0($s0)
    ctx->pc = 0x18c1e0u;
    WRITE32(ADD32(GPR_U32(ctx, 16), 6080), GPR_U32(ctx, 15));
    // 0x18c1e4: 0x340fffff  ori         $t7, $zero, 0xFFFF
    ctx->pc = 0x18c1e4u;
    SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
label_18c1e8:
    // 0x18c1e8: 0x164f0008  bne         $s2, $t7, . + 4 + (0x8 << 2)
    ctx->pc = 0x18C1E8u;
    {
        const bool branch_taken_0x18c1e8 = (GPR_U64(ctx, 18) != GPR_U64(ctx, 15));
        ctx->pc = 0x18C1ECu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C1E8u;
        // 0x18c1ec: 0x340fffff  ori         $t7, $zero, 0xFFFF (Delay Slot)
        SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c1e8) {
            ctx->pc = 0x18C20Cu;
            goto label_18c20c;
        }
    }
    ctx->pc = 0x18C1F0u;
    // 0x18c1f0: 0x240f0001  addiu       $t7, $zero, 0x1
    ctx->pc = 0x18c1f0u;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 0), 1));
    // 0x18c1f4: 0x166f0005  bne         $s3, $t7, . + 4 + (0x5 << 2)
    ctx->pc = 0x18C1F4u;
    {
        const bool branch_taken_0x18c1f4 = (GPR_U64(ctx, 19) != GPR_U64(ctx, 15));
        ctx->pc = 0x18C1F8u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C1F4u;
        // 0x18c1f8: 0x340fffff  ori         $t7, $zero, 0xFFFF (Delay Slot)
        SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c1f4) {
            ctx->pc = 0x18C20Cu;
            goto label_18c20c;
        }
    }
    ctx->pc = 0x18C1FCu;
    // 0x18c1fc: 0x3c040029  lui         $a0, 0x29
    ctx->pc = 0x18c1fcu;
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)41 << 16));
    // 0x18c200: 0xc060468  jal         func_1811A0
    ctx->pc = 0x18C200u;
    SET_GPR_U32(ctx, 31, 0x18C208u);
    ctx->pc = 0x18C204u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x18C200u;
    // 0x18c204: 0x24845840  addiu       $a0, $a0, 0x5840 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 4), 22592));
    ctx->in_delay_slot = false;
    ctx->pc = 0x1811A0u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1811A0u, 0x18C200u, 0x18C208u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        std::cerr << "[DIAG:18c018-callfail] target=0x1811A0u src=0x18C200u fallthrough=0x18C208u sp=0x" << std::hex << GPR_U32(ctx, 29) << " pc=0x" << ctx->pc << std::dec << std::endl;
        return;
    }
    std::cerr << "[DIAG:18c018-ok] after_fallthrough=0x18C208u sp=0x" << std::hex << GPR_U32(ctx, 29) << std::dec << std::endl;
    ctx->pc = 0x18C208u;
label_18c208:
    // 0x18c208: 0x340fffff  ori         $t7, $zero, 0xFFFF
    ctx->pc = 0x18c208u;
    SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
label_18c20c:
    // 0x18c20c: 0x164f0008  bne         $s2, $t7, . + 4 + (0x8 << 2)
    ctx->pc = 0x18C20Cu;
    {
        const bool branch_taken_0x18c20c = (GPR_U64(ctx, 18) != GPR_U64(ctx, 15));
        ctx->pc = 0x18C210u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C20Cu;
        // 0x18c210: 0x340fffff  ori         $t7, $zero, 0xFFFF (Delay Slot)
        SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c20c) {
            ctx->pc = 0x18C230u;
            goto label_18c230;
        }
    }
    ctx->pc = 0x18C214u;
    // 0x18c214: 0x240f0001  addiu       $t7, $zero, 0x1
    ctx->pc = 0x18c214u;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 0), 1));
    // 0x18c218: 0x166f0005  bne         $s3, $t7, . + 4 + (0x5 << 2)
    ctx->pc = 0x18C218u;
    {
        const bool branch_taken_0x18c218 = (GPR_U64(ctx, 19) != GPR_U64(ctx, 15));
        ctx->pc = 0x18C21Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C218u;
        // 0x18c21c: 0x340fffff  ori         $t7, $zero, 0xFFFF (Delay Slot)
        SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c218) {
            ctx->pc = 0x18C230u;
            goto label_18c230;
        }
    }
    ctx->pc = 0x18C220u;
    // 0x18c220: 0x3c040029  lui         $a0, 0x29
    ctx->pc = 0x18c220u;
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)41 << 16));
    // 0x18c224: 0xc088110  jal         func_220440
    ctx->pc = 0x18C224u;
    SET_GPR_U32(ctx, 31, 0x18C22Cu);
    ctx->pc = 0x18C228u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x18C224u;
    // 0x18c228: 0x24846080  addiu       $a0, $a0, 0x6080 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 4), 24704));
    ctx->in_delay_slot = false;
    ctx->pc = 0x220440u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x220440u, 0x18C224u, 0x18C22Cu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        std::cerr << "[DIAG:18c018-callfail] target=0x220440u src=0x18C224u fallthrough=0x18C22Cu sp=0x" << std::hex << GPR_U32(ctx, 29) << " pc=0x" << ctx->pc << std::dec << std::endl;
        return;
    }
    std::cerr << "[DIAG:18c018-ok] after_fallthrough=0x18C22Cu sp=0x" << std::hex << GPR_U32(ctx, 29) << std::dec << std::endl;
    ctx->pc = 0x18C22Cu;
label_18c22c:
    // 0x18c22c: 0x340fffff  ori         $t7, $zero, 0xFFFF
    ctx->pc = 0x18c22cu;
    SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
label_18c230:
    // 0x18c230: 0x164f0011  bne         $s2, $t7, . + 4 + (0x11 << 2)
    ctx->pc = 0x18C230u;
    {
        const bool branch_taken_0x18c230 = (GPR_U64(ctx, 18) != GPR_U64(ctx, 15));
        ctx->pc = 0x18C234u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C230u;
        // 0x18c234: 0x340fffff  ori         $t7, $zero, 0xFFFF (Delay Slot)
        SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c230) {
            ctx->pc = 0x18C278u;
            goto label_18c278;
        }
    }
    ctx->pc = 0x18C238u;
    // 0x18c238: 0x240f0001  addiu       $t7, $zero, 0x1
    ctx->pc = 0x18c238u;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 0), 1));
    // 0x18c23c: 0x166f000e  bne         $s3, $t7, . + 4 + (0xE << 2)
    ctx->pc = 0x18C23Cu;
    {
        const bool branch_taken_0x18c23c = (GPR_U64(ctx, 19) != GPR_U64(ctx, 15));
        ctx->pc = 0x18C240u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C23Cu;
        // 0x18c240: 0x340fffff  ori         $t7, $zero, 0xFFFF (Delay Slot)
        SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c23c) {
            ctx->pc = 0x18C278u;
            goto label_18c278;
        }
    }
    ctx->pc = 0x18C244u;
    // 0x18c244: 0x3c0f0029  lui         $t7, 0x29
    ctx->pc = 0x18c244u;
    SET_GPR_S32(ctx, 15, (int32_t)((uint32_t)41 << 16));
    // 0x18c248: 0x24110003  addiu       $s1, $zero, 0x3
    ctx->pc = 0x18c248u;
    SET_GPR_S32(ctx, 17, (int32_t)ADD32(GPR_U32(ctx, 0), 3));
    // 0x18c24c: 0x25f06640  addiu       $s0, $t7, 0x6640
    ctx->pc = 0x18c24cu;
    SET_GPR_S32(ctx, 16, (int32_t)ADD32(GPR_U32(ctx, 15), 26176));
    // 0x18c250: 0x2414ffff  addiu       $s4, $zero, -0x1
    ctx->pc = 0x18c250u;
    SET_GPR_S32(ctx, 20, (int32_t)ADD32(GPR_U32(ctx, 0), 4294967295));
label_18c254:
    // 0x18c254: 0x200202d  daddu       $a0, $s0, $zero
    ctx->pc = 0x18c254u;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 16) + (uint64_t)GPR_U64(ctx, 0));
    // 0x18c258: 0xc062be0  jal         func_18AF80
    ctx->pc = 0x18C258u;
    SET_GPR_U32(ctx, 31, 0x18C260u);
    ctx->pc = 0x18C25Cu;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x18C258u;
    // 0x18c25c: 0x2631ffff  addiu       $s1, $s1, -0x1 (Delay Slot)
    SET_GPR_S32(ctx, 17, (int32_t)ADD32(GPR_U32(ctx, 17), 4294967295));
    ctx->in_delay_slot = false;
    ctx->pc = 0x18AF80u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x18AF80u, 0x18C258u, 0x18C260u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        std::cerr << "[DIAG:18c018-callfail] target=0x18AF80u src=0x18C258u fallthrough=0x18C260u sp=0x" << std::hex << GPR_U32(ctx, 29) << " pc=0x" << ctx->pc << std::dec << std::endl;
        return;
    }
    std::cerr << "[DIAG:18c018-ok] after_fallthrough=0x18C260u sp=0x" << std::hex << GPR_U32(ctx, 29) << std::dec << std::endl;
    ctx->pc = 0x18C260u;
label_18c260:
    // 0x18c260: 0x1634fffc  bne         $s1, $s4, . + 4 + (-0x4 << 2)
    ctx->pc = 0x18C260u;
    {
        const bool branch_taken_0x18c260 = (GPR_U64(ctx, 17) != GPR_U64(ctx, 20));
        ctx->pc = 0x18C264u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C260u;
        // 0x18c264: 0x26100100  addiu       $s0, $s0, 0x100 (Delay Slot)
        SET_GPR_S32(ctx, 16, (int32_t)ADD32(GPR_U32(ctx, 16), 256));
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c260) {
            ctx->pc = 0x18C254u;
            if (runtime->shouldPreemptGuestExecution()) {
                return;
            }
            goto label_18c254;
        }
    }
    ctx->pc = 0x18C268u;
    // 0x18c268: 0x3c040029  lui         $a0, 0x29
    ctx->pc = 0x18c268u;
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)41 << 16));
    // 0x18c26c: 0xc062bc6  jal         func_18AF18
    ctx->pc = 0x18C26Cu;
    SET_GPR_U32(ctx, 31, 0x18C274u);
    ctx->pc = 0x18C270u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x18C26Cu;
    // 0x18c270: 0x24846640  addiu       $a0, $a0, 0x6640 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 4), 26176));
    ctx->in_delay_slot = false;
    ctx->pc = 0x18AF18u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x18AF18u, 0x18C26Cu, 0x18C274u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        std::cerr << "[DIAG:18c018-callfail] target=0x18AF18u src=0x18C26Cu fallthrough=0x18C274u sp=0x" << std::hex << GPR_U32(ctx, 29) << " pc=0x" << ctx->pc << std::dec << std::endl;
        return;
    }
    std::cerr << "[DIAG:18c018-ok] after_fallthrough=0x18C274u sp=0x" << std::hex << GPR_U32(ctx, 29) << std::dec << std::endl;
    ctx->pc = 0x18C274u;
label_18c274:
    // 0x18c274: 0x340fffff  ori         $t7, $zero, 0xFFFF
    ctx->pc = 0x18c274u;
    SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
label_18c278:
    // 0x18c278: 0x164f002a  bne         $s2, $t7, . + 4 + (0x2A << 2)
    ctx->pc = 0x18C278u;
    {
        const bool branch_taken_0x18c278 = (GPR_U64(ctx, 18) != GPR_U64(ctx, 15));
        ctx->pc = 0x18C27Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C278u;
        // 0x18c27c: 0x340fffff  ori         $t7, $zero, 0xFFFF (Delay Slot)
        SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c278) {
            ctx->pc = 0x18C324u;
            goto label_18c324;
        }
    }
    ctx->pc = 0x18C280u;
    // 0x18c280: 0x240f0001  addiu       $t7, $zero, 0x1
    ctx->pc = 0x18c280u;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 0), 1));
    // 0x18c284: 0x166f0027  bne         $s3, $t7, . + 4 + (0x27 << 2)
    ctx->pc = 0x18C284u;
    {
        const bool branch_taken_0x18c284 = (GPR_U64(ctx, 19) != GPR_U64(ctx, 15));
        ctx->pc = 0x18C288u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C284u;
        // 0x18c288: 0x340fffff  ori         $t7, $zero, 0xFFFF (Delay Slot)
        SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c284) {
            ctx->pc = 0x18C324u;
            goto label_18c324;
        }
    }
    ctx->pc = 0x18C28Cu;
    // 0x18c28c: 0x3c0f0029  lui         $t7, 0x29
    ctx->pc = 0x18c28cu;
    SET_GPR_S32(ctx, 15, (int32_t)((uint32_t)41 << 16));
    // 0x18c290: 0x582d  daddu       $t3, $zero, $zero
    ctx->pc = 0x18c290u;
    SET_GPR_U64(ctx, 11, (uint64_t)GPR_U64(ctx, 0) + (uint64_t)GPR_U64(ctx, 0));
    // 0x18c294: 0x25ee6ab0  addiu       $t6, $t7, 0x6AB0
    ctx->pc = 0x18c294u;
    SET_GPR_S32(ctx, 14, (int32_t)ADD32(GPR_U32(ctx, 15), 27312));
    // 0x18c298: 0x25cd0010  addiu       $t5, $t6, 0x10
    ctx->pc = 0x18c298u;
    SET_GPR_S32(ctx, 13, (int32_t)ADD32(GPR_U32(ctx, 14), 16));
    // 0x18c29c: 0xadee6ab0  sw          $t6, 0x6AB0($t7)
    ctx->pc = 0x18c29cu;
    do { uint32_t _value = static_cast<uint32_t>(GPR_U32(ctx, 14)); ps2TraceGuestWrite(rdram, 0x296AB0u, 4u, _value, 0u, "WRITE32", ctx); FAST_WRITE32(0x296AB0u, _value); } while (0);
    // 0x18c2a0: 0xadad0004  sw          $t5, 0x4($t5)
    ctx->pc = 0x18c2a0u;
    do { uint32_t _value = static_cast<uint32_t>(GPR_U32(ctx, 13)); ps2TraceGuestWrite(rdram, 0x296AC4u, 4u, _value, 0u, "WRITE32", ctx); FAST_WRITE32(0x296AC4u, _value); } while (0);
    // 0x18c2a4: 0x25c90024  addiu       $t1, $t6, 0x24
    ctx->pc = 0x18c2a4u;
    SET_GPR_S32(ctx, 9, (int32_t)ADD32(GPR_U32(ctx, 14), 36));
    // 0x18c2a8: 0x240f0008  addiu       $t7, $zero, 0x8
    ctx->pc = 0x18c2a8u;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 0), 8));
    // 0x18c2ac: 0xadc0000c  sw          $zero, 0xC($t6)
    ctx->pc = 0x18c2acu;
    do { uint32_t _value = static_cast<uint32_t>(GPR_U32(ctx, 0)); ps2TraceGuestWrite(rdram, 0x296ABCu, 4u, _value, 0u, "WRITE32", ctx); FAST_WRITE32(0x296ABCu, _value); } while (0);
    // 0x18c2b0: 0xadcf0020  sw          $t7, 0x20($t6)
    ctx->pc = 0x18c2b0u;
    do { uint32_t _value = static_cast<uint32_t>(GPR_U32(ctx, 15)); ps2TraceGuestWrite(rdram, 0x296AD0u, 4u, _value, 0u, "WRITE32", ctx); FAST_WRITE32(0x296AD0u, _value); } while (0);
    // 0x18c2b4: 0x1c0502d  daddu       $t2, $t6, $zero
    ctx->pc = 0x18c2b4u;
    SET_GPR_U64(ctx, 10, (uint64_t)GPR_U64(ctx, 14) + (uint64_t)GPR_U64(ctx, 0));
    // 0x18c2b8: 0xadce0004  sw          $t6, 0x4($t6)
    ctx->pc = 0x18c2b8u;
    do { uint32_t _value = static_cast<uint32_t>(GPR_U32(ctx, 14)); ps2TraceGuestWrite(rdram, 0x296AB4u, 4u, _value, 0u, "WRITE32", ctx); FAST_WRITE32(0x296AB4u, _value); } while (0);
    // 0x18c2bc: 0x120602d  daddu       $t4, $t1, $zero
    ctx->pc = 0x18c2bcu;
    SET_GPR_U64(ctx, 12, (uint64_t)GPR_U64(ctx, 9) + (uint64_t)GPR_U64(ctx, 0));
    // 0x18c2c0: 0xada0000c  sw          $zero, 0xC($t5)
    ctx->pc = 0x18c2c0u;
    do { uint32_t _value = static_cast<uint32_t>(GPR_U32(ctx, 0)); ps2TraceGuestWrite(rdram, 0x296ACCu, 4u, _value, 0u, "WRITE32", ctx); FAST_WRITE32(0x296ACCu, _value); } while (0);
    // 0x18c2c4: 0xadcd0010  sw          $t5, 0x10($t6)
    ctx->pc = 0x18c2c4u;
    do { uint32_t _value = static_cast<uint32_t>(GPR_U32(ctx, 13)); ps2TraceGuestWrite(rdram, 0x296AC0u, 4u, _value, 0u, "WRITE32", ctx); FAST_WRITE32(0x296AC0u, _value); } while (0);
label_18c2c8:
    // 0x18c2c8: 0x258ffff4  addiu       $t7, $t4, -0xC
    ctx->pc = 0x18c2c8u;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 12), 4294967284));
    // 0x18c2cc: 0x258e000c  addiu       $t6, $t4, 0xC
    ctx->pc = 0x18c2ccu;
    SET_GPR_S32(ctx, 14, (int32_t)ADD32(GPR_U32(ctx, 12), 12));
    // 0x18c2d0: 0xad8f0000  sw          $t7, 0x0($t4)
    ctx->pc = 0x18c2d0u;
    WRITE32(ADD32(GPR_U32(ctx, 12), 0), GPR_U32(ctx, 15));
    // 0x18c2d4: 0x256b0001  addiu       $t3, $t3, 0x1
    ctx->pc = 0x18c2d4u;
    SET_GPR_S32(ctx, 11, (int32_t)ADD32(GPR_U32(ctx, 11), 1));
    // 0x18c2d8: 0xad8e0004  sw          $t6, 0x4($t4)
    ctx->pc = 0x18c2d8u;
    WRITE32(ADD32(GPR_U32(ctx, 12), 4), GPR_U32(ctx, 14));
    // 0x18c2dc: 0x8d4d0020  lw          $t5, 0x20($t2)
    ctx->pc = 0x18c2dcu;
    SET_GPR_S32(ctx, 13, (int32_t)READ32(ADD32(GPR_U32(ctx, 10), 32)));
    // 0x18c2e0: 0x16d782a  slt         $t7, $t3, $t5
    ctx->pc = 0x18c2e0u;
    SET_GPR_U64(ctx, 15, ((int64_t)GPR_S64(ctx, 11) < (int64_t)GPR_S64(ctx, 13)) ? 1 : 0);
    // 0x18c2e4: 0x15e0fff8  bnez        $t7, . + 4 + (-0x8 << 2)
    ctx->pc = 0x18C2E4u;
    {
        const bool branch_taken_0x18c2e4 = (GPR_U64(ctx, 15) != GPR_U64(ctx, 0));
        ctx->pc = 0x18C2E8u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C2E4u;
        // 0x18c2e8: 0x1c0602d  daddu       $t4, $t6, $zero (Delay Slot)
        SET_GPR_U64(ctx, 12, (uint64_t)GPR_U64(ctx, 14) + (uint64_t)GPR_U64(ctx, 0));
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c2e4) {
            ctx->pc = 0x18C2C8u;
            if (runtime->shouldPreemptGuestExecution()) {
                return;
            }
            goto label_18c2c8;
        }
    }
    ctx->pc = 0x18C2ECu;
    // 0x18c2ec: 0x254f0010  addiu       $t7, $t2, 0x10
    ctx->pc = 0x18c2ecu;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 10), 16));
    // 0x18c2f0: 0x240e000c  addiu       $t6, $zero, 0xC
    ctx->pc = 0x18c2f0u;
    SET_GPR_S32(ctx, 14, (int32_t)ADD32(GPR_U32(ctx, 0), 12));
    // 0x18c2f4: 0xade0000c  sw          $zero, 0xC($t7)
    ctx->pc = 0x18c2f4u;
    WRITE32(ADD32(GPR_U32(ctx, 15), 12), GPR_U32(ctx, 0));
    // 0x18c2f8: 0x1ae7018  mult        $t6, $t5, $t6
    ctx->pc = 0x18c2f8u;
    { int64_t result = (int64_t)GPR_S32(ctx, 13) * (int64_t)GPR_S32(ctx, 14); ctx->lo = (uint64_t)(int64_t)(int32_t)result; ctx->hi = (uint64_t)(int64_t)(int32_t)(result >> 32); SET_GPR_S32(ctx, 14, (int32_t)result); }
    // 0x18c2fc: 0xadef0004  sw          $t7, 0x4($t7)
    ctx->pc = 0x18c2fcu;
    WRITE32(ADD32(GPR_U32(ctx, 15), 4), GPR_U32(ctx, 15));
    // 0x18c300: 0xad4d000c  sw          $t5, 0xC($t2)
    ctx->pc = 0x18c300u;
    WRITE32(ADD32(GPR_U32(ctx, 10), 12), GPR_U32(ctx, 13));
    // 0x18c304: 0xad4f0010  sw          $t7, 0x10($t2)
    ctx->pc = 0x18c304u;
    WRITE32(ADD32(GPR_U32(ctx, 10), 16), GPR_U32(ctx, 15));
    // 0x18c308: 0xad490004  sw          $t1, 0x4($t2)
    ctx->pc = 0x18c308u;
    WRITE32(ADD32(GPR_U32(ctx, 10), 4), GPR_U32(ctx, 9));
    // 0x18c30c: 0x1c97021  addu        $t6, $t6, $t1
    ctx->pc = 0x18c30cu;
    SET_GPR_S32(ctx, 14, (int32_t)ADD32(GPR_U32(ctx, 14), GPR_U32(ctx, 9)));
    // 0x18c310: 0xad2a0000  sw          $t2, 0x0($t1)
    ctx->pc = 0x18c310u;
    WRITE32(ADD32(GPR_U32(ctx, 9), 0), GPR_U32(ctx, 10));
    // 0x18c314: 0x25cefff4  addiu       $t6, $t6, -0xC
    ctx->pc = 0x18c314u;
    SET_GPR_S32(ctx, 14, (int32_t)ADD32(GPR_U32(ctx, 14), 4294967284));
    // 0x18c318: 0xadca0004  sw          $t2, 0x4($t6)
    ctx->pc = 0x18c318u;
    WRITE32(ADD32(GPR_U32(ctx, 14), 4), GPR_U32(ctx, 10));
    // 0x18c31c: 0xad4e0000  sw          $t6, 0x0($t2)
    ctx->pc = 0x18c31cu;
    WRITE32(ADD32(GPR_U32(ctx, 10), 0), GPR_U32(ctx, 14));
    // 0x18c320: 0x340fffff  ori         $t7, $zero, 0xFFFF
    ctx->pc = 0x18c320u;
    SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
label_18c324:
    // 0x18c324: 0x164f0008  bne         $s2, $t7, . + 4 + (0x8 << 2)
    ctx->pc = 0x18C324u;
    {
        const bool branch_taken_0x18c324 = (GPR_U64(ctx, 18) != GPR_U64(ctx, 15));
        ctx->pc = 0x18C328u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C324u;
        // 0x18c328: 0x340fffff  ori         $t7, $zero, 0xFFFF (Delay Slot)
        SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c324) {
            ctx->pc = 0x18C348u;
            goto label_18c348;
        }
    }
    ctx->pc = 0x18C32Cu;
    // 0x18c32c: 0x240f0001  addiu       $t7, $zero, 0x1
    ctx->pc = 0x18c32cu;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 0), 1));
    // 0x18c330: 0x166f0005  bne         $s3, $t7, . + 4 + (0x5 << 2)
    ctx->pc = 0x18C330u;
    {
        const bool branch_taken_0x18c330 = (GPR_U64(ctx, 19) != GPR_U64(ctx, 15));
        ctx->pc = 0x18C334u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C330u;
        // 0x18c334: 0x340fffff  ori         $t7, $zero, 0xFFFF (Delay Slot)
        SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c330) {
            ctx->pc = 0x18C348u;
            goto label_18c348;
        }
    }
    ctx->pc = 0x18C338u;
    // 0x18c338: 0x3c040029  lui         $a0, 0x29
    ctx->pc = 0x18c338u;
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)41 << 16));
    // 0x18c33c: 0xc062aec  jal         func_18ABB0
    ctx->pc = 0x18C33Cu;
    SET_GPR_U32(ctx, 31, 0x18C344u);
    ctx->pc = 0x18C340u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x18C33Cu;
    // 0x18c340: 0x24846b40  addiu       $a0, $a0, 0x6B40 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 4), 27456));
    ctx->in_delay_slot = false;
    ctx->pc = 0x18ABB0u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x18ABB0u, 0x18C33Cu, 0x18C344u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        std::cerr << "[DIAG:18c018-callfail] target=0x18ABB0u src=0x18C33Cu fallthrough=0x18C344u sp=0x" << std::hex << GPR_U32(ctx, 29) << " pc=0x" << ctx->pc << std::dec << std::endl;
        return;
    }
    std::cerr << "[DIAG:18c018-ok] after_fallthrough=0x18C344u sp=0x" << std::hex << GPR_U32(ctx, 29) << std::dec << std::endl;
    ctx->pc = 0x18C344u;
label_18c344:
    // 0x18c344: 0x340fffff  ori         $t7, $zero, 0xFFFF
    ctx->pc = 0x18c344u;
    SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
label_18c348:
    // 0x18c348: 0x164f0024  bne         $s2, $t7, . + 4 + (0x24 << 2)
    ctx->pc = 0x18C348u;
    {
        const bool branch_taken_0x18c348 = (GPR_U64(ctx, 18) != GPR_U64(ctx, 15));
        ctx->pc = 0x18C34Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C348u;
        // 0x18c34c: 0x340fffff  ori         $t7, $zero, 0xFFFF (Delay Slot)
        SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c348) {
            ctx->pc = 0x18C3DCu;
            goto label_18c3dc;
        }
    }
    ctx->pc = 0x18C350u;
    // 0x18c350: 0x240f0001  addiu       $t7, $zero, 0x1
    ctx->pc = 0x18c350u;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 0), 1));
    // 0x18c354: 0x166f0021  bne         $s3, $t7, . + 4 + (0x21 << 2)
    ctx->pc = 0x18C354u;
    {
        const bool branch_taken_0x18c354 = (GPR_U64(ctx, 19) != GPR_U64(ctx, 15));
        ctx->pc = 0x18C358u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C354u;
        // 0x18c358: 0x340fffff  ori         $t7, $zero, 0xFFFF (Delay Slot)
        SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c354) {
            ctx->pc = 0x18C3DCu;
            goto label_18c3dc;
        }
    }
    ctx->pc = 0x18C35Cu;
    // 0x18c35c: 0x3c0c0029  lui         $t4, 0x29
    ctx->pc = 0x18c35cu;
    SET_GPR_S32(ctx, 12, (int32_t)((uint32_t)41 << 16));
    // 0x18c360: 0x240e0001  addiu       $t6, $zero, 0x1
    ctx->pc = 0x18c360u;
    SET_GPR_S32(ctx, 14, (int32_t)ADD32(GPR_U32(ctx, 0), 1));
    // 0x18c364: 0x258c6bc0  addiu       $t4, $t4, 0x6BC0
    ctx->pc = 0x18c364u;
    SET_GPR_S32(ctx, 12, (int32_t)ADD32(GPR_U32(ctx, 12), 27584));
    // 0x18c368: 0xe703c  dsll32      $t6, $t6, 0
    ctx->pc = 0x18c368u;
    SET_GPR_U64(ctx, 14, GPR_U64(ctx, 14) << (32 + 0));
    // 0x18c36c: 0xad800404  sw          $zero, 0x404($t4)
    ctx->pc = 0x18c36cu;
    WRITE32(ADD32(GPR_U32(ctx, 12), 1028), GPR_U32(ctx, 0));
    // 0x18c370: 0xe7027  nor         $t6, $zero, $t6
    ctx->pc = 0x18c370u;
    SET_GPR_U64(ctx, 14, ~(GPR_U64(ctx, 0) | GPR_U64(ctx, 14)));
    // 0x18c374: 0x3c0f002d  lui         $t7, 0x2D
    ctx->pc = 0x18c374u;
    SET_GPR_S32(ctx, 15, (int32_t)((uint32_t)45 << 16));
    // 0x18c378: 0x240d0001  addiu       $t5, $zero, 0x1
    ctx->pc = 0x18c378u;
    SET_GPR_S32(ctx, 13, (int32_t)ADD32(GPR_U32(ctx, 0), 1));
    // 0x18c37c: 0xad800408  sw          $zero, 0x408($t4)
    ctx->pc = 0x18c37cu;
    WRITE32(ADD32(GPR_U32(ctx, 12), 1032), GPR_U32(ctx, 0));
    // 0x18c380: 0x25eff960  addiu       $t7, $t7, -0x6A0
    ctx->pc = 0x18c380u;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 15), 4294965600));
    // 0x18c384: 0xd687c  dsll32      $t5, $t5, 1
    ctx->pc = 0x18c384u;
    SET_GPR_U64(ctx, 13, GPR_U64(ctx, 13) << (32 + 1));
    // 0x18c388: 0xdd8b0408  ld          $t3, 0x408($t4)
    ctx->pc = 0x18c388u;
    SET_GPR_U64(ctx, 11, READ64(ADD32(GPR_U32(ctx, 12), 1032)));
    // 0x18c38c: 0xd6827  nor         $t5, $zero, $t5
    ctx->pc = 0x18c38cu;
    SET_GPR_U64(ctx, 13, ~(GPR_U64(ctx, 0) | GPR_U64(ctx, 13)));
    // 0x18c390: 0x16e5824  and         $t3, $t3, $t6
    ctx->pc = 0x18c390u;
    SET_GPR_U64(ctx, 11, GPR_U64(ctx, 11) & GPR_U64(ctx, 14));
    // 0x18c394: 0x3c0e0029  lui         $t6, 0x29
    ctx->pc = 0x18c394u;
    SET_GPR_S32(ctx, 14, (int32_t)((uint32_t)41 << 16));
    // 0x18c398: 0x16d5824  and         $t3, $t3, $t5
    ctx->pc = 0x18c398u;
    SET_GPR_U64(ctx, 11, GPR_U64(ctx, 11) & GPR_U64(ctx, 13));
    // 0x18c39c: 0xadcf6fd4  sw          $t7, 0x6FD4($t6)
    ctx->pc = 0x18c39cu;
    WRITE32(ADD32(GPR_U32(ctx, 14), 28628), GPR_U32(ctx, 15));
    // 0x18c3a0: 0x3c0d0029  lui         $t5, 0x29
    ctx->pc = 0x18c3a0u;
    SET_GPR_S32(ctx, 13, (int32_t)((uint32_t)41 << 16));
    // 0x18c3a4: 0x3c0f002d  lui         $t7, 0x2D
    ctx->pc = 0x18c3a4u;
    SET_GPR_S32(ctx, 15, (int32_t)((uint32_t)45 << 16));
    // 0x18c3a8: 0x3c0e002d  lui         $t6, 0x2D
    ctx->pc = 0x18c3a8u;
    SET_GPR_S32(ctx, 14, (int32_t)((uint32_t)45 << 16));
    // 0x18c3ac: 0x25effa08  addiu       $t7, $t7, -0x5F8
    ctx->pc = 0x18c3acu;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 15), 4294965768));
    // 0x18c3b0: 0x25cef9d0  addiu       $t6, $t6, -0x630
    ctx->pc = 0x18c3b0u;
    SET_GPR_S32(ctx, 14, (int32_t)ADD32(GPR_U32(ctx, 14), 4294965712));
    // 0x18c3b4: 0xadaf6fe4  sw          $t7, 0x6FE4($t5)
    ctx->pc = 0x18c3b4u;
    do { uint32_t _value = static_cast<uint32_t>(GPR_U32(ctx, 15)); ps2TraceGuestWrite(rdram, 0x296FE4u, 4u, _value, 0u, "WRITE32", ctx); FAST_WRITE32(0x296FE4u, _value); } while (0);
    // 0x18c3b8: 0x3c0f0029  lui         $t7, 0x29
    ctx->pc = 0x18c3b8u;
    SET_GPR_S32(ctx, 15, (int32_t)((uint32_t)41 << 16));
    // 0x18c3bc: 0xadee6ff4  sw          $t6, 0x6FF4($t7)
    ctx->pc = 0x18c3bcu;
    do { uint32_t _value = static_cast<uint32_t>(GPR_U32(ctx, 14)); ps2TraceGuestWrite(rdram, 0x296FF4u, 4u, _value, 0u, "WRITE32", ctx); FAST_WRITE32(0x296FF4u, _value); } while (0);
    // 0x18c3c0: 0xfd8b0408  sd          $t3, 0x408($t4)
    ctx->pc = 0x18c3c0u;
    WRITE64(ADD32(GPR_U32(ctx, 12), 1032), GPR_U64(ctx, 11));
    // 0x18c3c4: 0xad930400  sw          $s3, 0x400($t4)
    ctx->pc = 0x18c3c4u;
    WRITE32(ADD32(GPR_U32(ctx, 12), 1024), GPR_U32(ctx, 19));
    // 0x18c3c8: 0xad800410  sw          $zero, 0x410($t4)
    ctx->pc = 0x18c3c8u;
    WRITE32(ADD32(GPR_U32(ctx, 12), 1040), GPR_U32(ctx, 0));
    // 0x18c3cc: 0xad800418  sw          $zero, 0x418($t4)
    ctx->pc = 0x18c3ccu;
    WRITE32(ADD32(GPR_U32(ctx, 12), 1048), GPR_U32(ctx, 0));
    // 0x18c3d0: 0xad800428  sw          $zero, 0x428($t4)
    ctx->pc = 0x18c3d0u;
    WRITE32(ADD32(GPR_U32(ctx, 12), 1064), GPR_U32(ctx, 0));
    // 0x18c3d4: 0xad800438  sw          $zero, 0x438($t4)
    ctx->pc = 0x18c3d4u;
    WRITE32(ADD32(GPR_U32(ctx, 12), 1080), GPR_U32(ctx, 0));
    // 0x18c3d8: 0x340fffff  ori         $t7, $zero, 0xFFFF
    ctx->pc = 0x18c3d8u;
    SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
label_18c3dc:
    // 0x18c3dc: 0x164f0008  bne         $s2, $t7, . + 4 + (0x8 << 2)
    ctx->pc = 0x18C3DCu;
    {
        const bool branch_taken_0x18c3dc = (GPR_U64(ctx, 18) != GPR_U64(ctx, 15));
        ctx->pc = 0x18C3E0u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C3DCu;
        // 0x18c3e0: 0x340fffff  ori         $t7, $zero, 0xFFFF (Delay Slot)
        SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c3dc) {
            ctx->pc = 0x18C400u;
            goto label_18c400;
        }
    }
    ctx->pc = 0x18C3E4u;
    // 0x18c3e4: 0x240f0001  addiu       $t7, $zero, 0x1
    ctx->pc = 0x18c3e4u;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 0), 1));
    // 0x18c3e8: 0x166f0005  bne         $s3, $t7, . + 4 + (0x5 << 2)
    ctx->pc = 0x18C3E8u;
    {
        const bool branch_taken_0x18c3e8 = (GPR_U64(ctx, 19) != GPR_U64(ctx, 15));
        ctx->pc = 0x18C3ECu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C3E8u;
        // 0x18c3ec: 0x340fffff  ori         $t7, $zero, 0xFFFF (Delay Slot)
        SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c3e8) {
            ctx->pc = 0x18C400u;
            goto label_18c400;
        }
    }
    ctx->pc = 0x18C3F0u;
    // 0x18c3f0: 0x3c040029  lui         $a0, 0x29
    ctx->pc = 0x18c3f0u;
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)41 << 16));
    // 0x18c3f4: 0xc05dce4  jal         func_177390
    ctx->pc = 0x18C3F4u;
    SET_GPR_U32(ctx, 31, 0x18C3FCu);
    ctx->pc = 0x18C3F8u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x18C3F4u;
    // 0x18c3f8: 0x24847040  addiu       $a0, $a0, 0x7040 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 4), 28736));
    ctx->in_delay_slot = false;
    ctx->pc = 0x177390u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x177390u, 0x18C3F4u, 0x18C3FCu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        std::cerr << "[DIAG:18c018-callfail] target=0x177390u src=0x18C3F4u fallthrough=0x18C3FCu sp=0x" << std::hex << GPR_U32(ctx, 29) << " pc=0x" << ctx->pc << std::dec << std::endl;
        return;
    }
    std::cerr << "[DIAG:18c018-ok] after_fallthrough=0x18C3FCu sp=0x" << std::hex << GPR_U32(ctx, 29) << std::dec << std::endl;
    ctx->pc = 0x18C3FCu;
label_18c3fc:
    // 0x18c3fc: 0x340fffff  ori         $t7, $zero, 0xFFFF
    ctx->pc = 0x18c3fcu;
    SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
label_18c400:
    // 0x18c400: 0x164f0008  bne         $s2, $t7, . + 4 + (0x8 << 2)
    ctx->pc = 0x18C400u;
    {
        const bool branch_taken_0x18c400 = (GPR_U64(ctx, 18) != GPR_U64(ctx, 15));
        ctx->pc = 0x18C404u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C400u;
        // 0x18c404: 0x340fffff  ori         $t7, $zero, 0xFFFF (Delay Slot)
        SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c400) {
            ctx->pc = 0x18C424u;
            goto label_18c424;
        }
    }
    ctx->pc = 0x18C408u;
    // 0x18c408: 0x240f0001  addiu       $t7, $zero, 0x1
    ctx->pc = 0x18c408u;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 0), 1));
    // 0x18c40c: 0x166f0005  bne         $s3, $t7, . + 4 + (0x5 << 2)
    ctx->pc = 0x18C40Cu;
    {
        const bool branch_taken_0x18c40c = (GPR_U64(ctx, 19) != GPR_U64(ctx, 15));
        ctx->pc = 0x18C410u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C40Cu;
        // 0x18c410: 0x340fffff  ori         $t7, $zero, 0xFFFF (Delay Slot)
        SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c40c) {
            ctx->pc = 0x18C424u;
            goto label_18c424;
        }
    }
    ctx->pc = 0x18C414u;
    // 0x18c414: 0x3c040029  lui         $a0, 0x29
    ctx->pc = 0x18c414u;
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)41 << 16));
    // 0x18c418: 0xc05db98  jal         func_176E60
    ctx->pc = 0x18C418u;
    SET_GPR_U32(ctx, 31, 0x18C420u);
    ctx->pc = 0x18C41Cu;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x18C418u;
    // 0x18c41c: 0x248470c0  addiu       $a0, $a0, 0x70C0 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 4), 28864));
    ctx->in_delay_slot = false;
    ctx->pc = 0x176E60u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x176E60u, 0x18C418u, 0x18C420u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        std::cerr << "[DIAG:18c018-callfail] target=0x176E60u src=0x18C418u fallthrough=0x18C420u sp=0x" << std::hex << GPR_U32(ctx, 29) << " pc=0x" << ctx->pc << std::dec << std::endl;
        return;
    }
    std::cerr << "[DIAG:18c018-ok] after_fallthrough=0x18C420u sp=0x" << std::hex << GPR_U32(ctx, 29) << std::dec << std::endl;
    ctx->pc = 0x18C420u;
label_18c420:
    // 0x18c420: 0x340fffff  ori         $t7, $zero, 0xFFFF
    ctx->pc = 0x18c420u;
    SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
label_18c424:
    // 0x18c424: 0x164f0008  bne         $s2, $t7, . + 4 + (0x8 << 2)
    ctx->pc = 0x18C424u;
    {
        const bool branch_taken_0x18c424 = (GPR_U64(ctx, 18) != GPR_U64(ctx, 15));
        ctx->pc = 0x18C428u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C424u;
        // 0x18c428: 0x340fffff  ori         $t7, $zero, 0xFFFF (Delay Slot)
        SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c424) {
            ctx->pc = 0x18C448u;
            goto label_18c448;
        }
    }
    ctx->pc = 0x18C42Cu;
    // 0x18c42c: 0x240f0001  addiu       $t7, $zero, 0x1
    ctx->pc = 0x18c42cu;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 0), 1));
    // 0x18c430: 0x166f0005  bne         $s3, $t7, . + 4 + (0x5 << 2)
    ctx->pc = 0x18C430u;
    {
        const bool branch_taken_0x18c430 = (GPR_U64(ctx, 19) != GPR_U64(ctx, 15));
        ctx->pc = 0x18C434u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C430u;
        // 0x18c434: 0x340fffff  ori         $t7, $zero, 0xFFFF (Delay Slot)
        SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c430) {
            ctx->pc = 0x18C448u;
            goto label_18c448;
        }
    }
    ctx->pc = 0x18C438u;
    // 0x18c438: 0x3c040029  lui         $a0, 0x29
    ctx->pc = 0x18c438u;
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)41 << 16));
    // 0x18c43c: 0xc088046  jal         func_220118
    ctx->pc = 0x18C43Cu;
    SET_GPR_U32(ctx, 31, 0x18C444u);
    ctx->pc = 0x18C440u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x18C43Cu;
    // 0x18c440: 0x24847100  addiu       $a0, $a0, 0x7100 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 4), 28928));
    ctx->in_delay_slot = false;
    ctx->pc = 0x220118u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x220118u, 0x18C43Cu, 0x18C444u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        std::cerr << "[DIAG:18c018-callfail] target=0x220118u src=0x18C43Cu fallthrough=0x18C444u sp=0x" << std::hex << GPR_U32(ctx, 29) << " pc=0x" << ctx->pc << std::dec << std::endl;
        return;
    }
    std::cerr << "[DIAG:18c018-ok] after_fallthrough=0x18C444u sp=0x" << std::hex << GPR_U32(ctx, 29) << std::dec << std::endl;
    ctx->pc = 0x18C444u;
label_18c444:
    // 0x18c444: 0x340fffff  ori         $t7, $zero, 0xFFFF
    ctx->pc = 0x18c444u;
    SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
label_18c448:
    // 0x18c448: 0x164f0008  bne         $s2, $t7, . + 4 + (0x8 << 2)
    ctx->pc = 0x18C448u;
    {
        const bool branch_taken_0x18c448 = (GPR_U64(ctx, 18) != GPR_U64(ctx, 15));
        ctx->pc = 0x18C44Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C448u;
        // 0x18c44c: 0x340fffff  ori         $t7, $zero, 0xFFFF (Delay Slot)
        SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c448) {
            ctx->pc = 0x18C46Cu;
            goto label_18c46c;
        }
    }
    ctx->pc = 0x18C450u;
    // 0x18c450: 0x240f0001  addiu       $t7, $zero, 0x1
    ctx->pc = 0x18c450u;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 0), 1));
    // 0x18c454: 0x166f0005  bne         $s3, $t7, . + 4 + (0x5 << 2)
    ctx->pc = 0x18C454u;
    {
        const bool branch_taken_0x18c454 = (GPR_U64(ctx, 19) != GPR_U64(ctx, 15));
        ctx->pc = 0x18C458u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C454u;
        // 0x18c458: 0x340fffff  ori         $t7, $zero, 0xFFFF (Delay Slot)
        SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c454) {
            ctx->pc = 0x18C46Cu;
            goto label_18c46c;
        }
    }
    ctx->pc = 0x18C45Cu;
    // 0x18c45c: 0x3c040030  lui         $a0, 0x30
    ctx->pc = 0x18c45cu;
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)48 << 16));
    // 0x18c460: 0xc05e28e  jal         func_178A38
    ctx->pc = 0x18C460u;
    SET_GPR_U32(ctx, 31, 0x18C468u);
    ctx->pc = 0x18C464u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x18C460u;
    // 0x18c464: 0x24842100  addiu       $a0, $a0, 0x2100 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 4), 8448));
    ctx->in_delay_slot = false;
    ctx->pc = 0x178A38u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x178A38u, 0x18C460u, 0x18C468u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        std::cerr << "[DIAG:18c018-callfail] target=0x178A38u src=0x18C460u fallthrough=0x18C468u sp=0x" << std::hex << GPR_U32(ctx, 29) << " pc=0x" << ctx->pc << std::dec << std::endl;
        return;
    }
    std::cerr << "[DIAG:18c018-ok] after_fallthrough=0x18C468u sp=0x" << std::hex << GPR_U32(ctx, 29) << std::dec << std::endl;
    ctx->pc = 0x18C468u;
label_18c468:
    // 0x18c468: 0x340fffff  ori         $t7, $zero, 0xFFFF
    ctx->pc = 0x18c468u;
    SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
label_18c46c:
    // 0x18c46c: 0x164f000f  bne         $s2, $t7, . + 4 + (0xF << 2)
    ctx->pc = 0x18C46Cu;
    {
        const bool branch_taken_0x18c46c = (GPR_U64(ctx, 18) != GPR_U64(ctx, 15));
        ctx->pc = 0x18C470u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C46Cu;
        // 0x18c470: 0x340fffff  ori         $t7, $zero, 0xFFFF (Delay Slot)
        SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c46c) {
            ctx->pc = 0x18C4ACu;
            goto label_18c4ac;
        }
    }
    ctx->pc = 0x18C474u;
    // 0x18c474: 0x240f0001  addiu       $t7, $zero, 0x1
    ctx->pc = 0x18c474u;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 0), 1));
    // 0x18c478: 0x166f000c  bne         $s3, $t7, . + 4 + (0xC << 2)
    ctx->pc = 0x18C478u;
    {
        const bool branch_taken_0x18c478 = (GPR_U64(ctx, 19) != GPR_U64(ctx, 15));
        ctx->pc = 0x18C47Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C478u;
        // 0x18c47c: 0x340fffff  ori         $t7, $zero, 0xFFFF (Delay Slot)
        SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c478) {
            ctx->pc = 0x18C4ACu;
            goto label_18c4ac;
        }
    }
    ctx->pc = 0x18C480u;
    // 0x18c480: 0x3c0f002d  lui         $t7, 0x2D
    ctx->pc = 0x18c480u;
    SET_GPR_S32(ctx, 15, (int32_t)((uint32_t)45 << 16));
    // 0x18c484: 0x3c0d0029  lui         $t5, 0x29
    ctx->pc = 0x18c484u;
    SET_GPR_S32(ctx, 13, (int32_t)((uint32_t)41 << 16));
    // 0x18c488: 0x25eff6b0  addiu       $t7, $t7, -0x950
    ctx->pc = 0x18c488u;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 15), 4294964912));
    // 0x18c48c: 0x25ae7380  addiu       $t6, $t5, 0x7380
    ctx->pc = 0x18c48cu;
    SET_GPR_S32(ctx, 14, (int32_t)ADD32(GPR_U32(ctx, 13), 29568));
    // 0x18c490: 0xadaf7380  sw          $t7, 0x7380($t5)
    ctx->pc = 0x18c490u;
    do { uint32_t _value = static_cast<uint32_t>(GPR_U32(ctx, 15)); ps2TraceGuestWrite(rdram, 0x297380u, 4u, _value, 0u, "WRITE32", ctx); FAST_WRITE32(0x297380u, _value); } while (0);
    // 0x18c494: 0xadc00030  sw          $zero, 0x30($t6)
    ctx->pc = 0x18c494u;
    do { uint32_t _value = static_cast<uint32_t>(GPR_U32(ctx, 0)); ps2TraceGuestWrite(rdram, 0x2973B0u, 4u, _value, 0u, "WRITE32", ctx); FAST_WRITE32(0x2973B0u, _value); } while (0);
    // 0x18c498: 0x70007ca9  por         $t7, $zero, $zero
    ctx->pc = 0x18c498u;
    SET_GPR_VEC(ctx, 15, PS2_POR(GPR_VEC(ctx, 0), GPR_VEC(ctx, 0)));
    // 0x18c49c: 0xadc00034  sw          $zero, 0x34($t6)
    ctx->pc = 0x18c49cu;
    WRITE32(ADD32(GPR_U32(ctx, 14), 52), GPR_U32(ctx, 0));
    // 0x18c4a0: 0x7dcf0020  sq          $t7, 0x20($t6)
    ctx->pc = 0x18c4a0u;
    WRITE128(ADD32(GPR_U32(ctx, 14), 32), GPR_VEC(ctx, 15));
    // 0x18c4a4: 0x7dcf0010  sq          $t7, 0x10($t6)
    ctx->pc = 0x18c4a4u;
    WRITE128(ADD32(GPR_U32(ctx, 14), 16), GPR_VEC(ctx, 15));
    // 0x18c4a8: 0x340fffff  ori         $t7, $zero, 0xFFFF
    ctx->pc = 0x18c4a8u;
    SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
label_18c4ac:
    // 0x18c4ac: 0x164f000f  bne         $s2, $t7, . + 4 + (0xF << 2)
    ctx->pc = 0x18C4ACu;
    {
        const bool branch_taken_0x18c4ac = (GPR_U64(ctx, 18) != GPR_U64(ctx, 15));
        ctx->pc = 0x18C4B0u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C4ACu;
        // 0x18c4b0: 0x340fffff  ori         $t7, $zero, 0xFFFF (Delay Slot)
        SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c4ac) {
            ctx->pc = 0x18C4ECu;
            goto label_18c4ec;
        }
    }
    ctx->pc = 0x18C4B4u;
    // 0x18c4b4: 0x240f0001  addiu       $t7, $zero, 0x1
    ctx->pc = 0x18c4b4u;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 0), 1));
    // 0x18c4b8: 0x166f000c  bne         $s3, $t7, . + 4 + (0xC << 2)
    ctx->pc = 0x18C4B8u;
    {
        const bool branch_taken_0x18c4b8 = (GPR_U64(ctx, 19) != GPR_U64(ctx, 15));
        ctx->pc = 0x18C4BCu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C4B8u;
        // 0x18c4bc: 0x340fffff  ori         $t7, $zero, 0xFFFF (Delay Slot)
        SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c4b8) {
            ctx->pc = 0x18C4ECu;
            goto label_18c4ec;
        }
    }
    ctx->pc = 0x18C4C0u;
    // 0x18c4c0: 0x3c110029  lui         $s1, 0x29
    ctx->pc = 0x18c4c0u;
    SET_GPR_S32(ctx, 17, (int32_t)((uint32_t)41 << 16));
    // 0x18c4c4: 0x26307600  addiu       $s0, $s1, 0x7600
    ctx->pc = 0x18c4c4u;
    SET_GPR_S32(ctx, 16, (int32_t)ADD32(GPR_U32(ctx, 17), 30208));
    // 0x18c4c8: 0x200202d  daddu       $a0, $s0, $zero
    ctx->pc = 0x18c4c8u;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 16) + (uint64_t)GPR_U64(ctx, 0));
    // 0x18c4cc: 0xc087fe4  jal         func_21FF90
    ctx->pc = 0x18C4CCu;
    SET_GPR_U32(ctx, 31, 0x18C4D4u);
    ctx->pc = 0x18C4D0u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x18C4CCu;
    // 0x18c4d0: 0x26317600  addiu       $s1, $s1, 0x7600 (Delay Slot)
    SET_GPR_S32(ctx, 17, (int32_t)ADD32(GPR_U32(ctx, 17), 30208));
    ctx->in_delay_slot = false;
    ctx->pc = 0x21FF90u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x21FF90u, 0x18C4CCu, 0x18C4D4u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        std::cerr << "[DIAG:18c018-callfail] target=0x21FF90u src=0x18C4CCu fallthrough=0x18C4D4u sp=0x" << std::hex << GPR_U32(ctx, 29) << " pc=0x" << ctx->pc << std::dec << std::endl;
        return;
    }
    std::cerr << "[DIAG:18c018-ok] after_fallthrough=0x18C4D4u sp=0x" << std::hex << GPR_U32(ctx, 29) << std::dec << std::endl;
    ctx->pc = 0x18C4D4u;
label_18c4d4:
    // 0x18c4d4: 0x2610005c  addiu       $s0, $s0, 0x5C
    ctx->pc = 0x18c4d4u;
    SET_GPR_S32(ctx, 16, (int32_t)ADD32(GPR_U32(ctx, 16), 92));
    // 0x18c4d8: 0xc087fe4  jal         func_21FF90
    ctx->pc = 0x18C4D8u;
    SET_GPR_U32(ctx, 31, 0x18C4E0u);
    ctx->pc = 0x18C4DCu;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x18C4D8u;
    // 0x18c4dc: 0x200202d  daddu       $a0, $s0, $zero (Delay Slot)
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 16) + (uint64_t)GPR_U64(ctx, 0));
    ctx->in_delay_slot = false;
    ctx->pc = 0x21FF90u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x21FF90u, 0x18C4D8u, 0x18C4E0u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        std::cerr << "[DIAG:18c018-callfail] target=0x21FF90u src=0x18C4D8u fallthrough=0x18C4E0u sp=0x" << std::hex << GPR_U32(ctx, 29) << " pc=0x" << ctx->pc << std::dec << std::endl;
        return;
    }
    std::cerr << "[DIAG:18c018-ok] after_fallthrough=0x18C4E0u sp=0x" << std::hex << GPR_U32(ctx, 29) << std::dec << std::endl;
    ctx->pc = 0x18C4E0u;
label_18c4e0:
    // 0x18c4e0: 0xae2000c4  sw          $zero, 0xC4($s1)
    ctx->pc = 0x18c4e0u;
    WRITE32(ADD32(GPR_U32(ctx, 17), 196), GPR_U32(ctx, 0));
    // 0x18c4e4: 0xae2000b8  sw          $zero, 0xB8($s1)
    ctx->pc = 0x18c4e4u;
    WRITE32(ADD32(GPR_U32(ctx, 17), 184), GPR_U32(ctx, 0));
    // 0x18c4e8: 0x340fffff  ori         $t7, $zero, 0xFFFF
    ctx->pc = 0x18c4e8u;
    SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
label_18c4ec:
    // 0x18c4ec: 0x164f0015  bne         $s2, $t7, . + 4 + (0x15 << 2)
    ctx->pc = 0x18C4ECu;
    {
        const bool branch_taken_0x18c4ec = (GPR_U64(ctx, 18) != GPR_U64(ctx, 15));
        ctx->pc = 0x18C4F0u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C4ECu;
        // 0x18c4f0: 0x340fffff  ori         $t7, $zero, 0xFFFF (Delay Slot)
        SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c4ec) {
            ctx->pc = 0x18C544u;
            goto label_18c544;
        }
    }
    ctx->pc = 0x18C4F4u;
    // 0x18c4f4: 0x16600013  bnez        $s3, . + 4 + (0x13 << 2)
    ctx->pc = 0x18C4F4u;
    {
        const bool branch_taken_0x18c4f4 = (GPR_U64(ctx, 19) != GPR_U64(ctx, 0));
        if (branch_taken_0x18c4f4) {
            ctx->pc = 0x18C544u;
            goto label_18c544;
        }
    }
    ctx->pc = 0x18C4FCu;
    // 0x18c4fc: 0x3c0f0029  lui         $t7, 0x29
    ctx->pc = 0x18c4fcu;
    SET_GPR_S32(ctx, 15, (int32_t)((uint32_t)41 << 16));
    // 0x18c500: 0x25ef7600  addiu       $t7, $t7, 0x7600
    ctx->pc = 0x18c500u;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 15), 30208));
    // 0x18c504: 0x11e0000e  beqz        $t7, . + 4 + (0xE << 2)
    ctx->pc = 0x18C504u;
    {
        const bool branch_taken_0x18c504 = (GPR_U64(ctx, 15) == GPR_U64(ctx, 0));
        ctx->pc = 0x18C508u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C504u;
        // 0x18c508: 0x1e0582d  daddu       $t3, $t7, $zero (Delay Slot)
        SET_GPR_U64(ctx, 11, (uint64_t)GPR_U64(ctx, 15) + (uint64_t)GPR_U64(ctx, 0));
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c504) {
            ctx->pc = 0x18C540u;
            goto label_18c540;
        }
    }
    ctx->pc = 0x18C50Cu;
    // 0x18c50c: 0x240dfff4  addiu       $t5, $zero, -0xC
    ctx->pc = 0x18c50cu;
    SET_GPR_S32(ctx, 13, (int32_t)ADD32(GPR_U32(ctx, 0), 4294967284));
    // 0x18c510: 0x25ef00b8  addiu       $t7, $t7, 0xB8
    ctx->pc = 0x18c510u;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 15), 184));
label_18c514:
    // 0x18c514: 0x11eb000a  beq         $t7, $t3, . + 4 + (0xA << 2)
    ctx->pc = 0x18C514u;
    {
        const bool branch_taken_0x18c514 = (GPR_U64(ctx, 15) == GPR_U64(ctx, 11));
        ctx->pc = 0x18C518u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C514u;
        // 0x18c518: 0x25eeffa4  addiu       $t6, $t7, -0x5C (Delay Slot)
        SET_GPR_S32(ctx, 14, (int32_t)ADD32(GPR_U32(ctx, 15), 4294967204));
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c514) {
            ctx->pc = 0x18C540u;
            goto label_18c540;
        }
    }
    ctx->pc = 0x18C51Cu;
    // 0x18c51c: 0x11cdfffd  beq         $t6, $t5, . + 4 + (-0x3 << 2)
    ctx->pc = 0x18C51Cu;
    {
        const bool branch_taken_0x18c51c = (GPR_U64(ctx, 14) == GPR_U64(ctx, 13));
        ctx->pc = 0x18C520u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C51Cu;
        // 0x18c520: 0x1c0782d  daddu       $t7, $t6, $zero (Delay Slot)
        SET_GPR_U64(ctx, 15, (uint64_t)GPR_U64(ctx, 14) + (uint64_t)GPR_U64(ctx, 0));
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c51c) {
            ctx->pc = 0x18C514u;
            if (runtime->shouldPreemptGuestExecution()) {
                return;
            }
            goto label_18c514;
        }
    }
    ctx->pc = 0x18C524u;
    // 0x18c524: 0x25cc000c  addiu       $t4, $t6, 0xC
    ctx->pc = 0x18c524u;
    SET_GPR_S32(ctx, 12, (int32_t)ADD32(GPR_U32(ctx, 14), 12));
    // 0x18c528: 0x25ce005c  addiu       $t6, $t6, 0x5C
    ctx->pc = 0x18c528u;
    SET_GPR_S32(ctx, 14, (int32_t)ADD32(GPR_U32(ctx, 14), 92));
label_18c52c:
    // 0x18c52c: 0x118efff9  beq         $t4, $t6, . + 4 + (-0x7 << 2)
    ctx->pc = 0x18C52Cu;
    {
        const bool branch_taken_0x18c52c = (GPR_U64(ctx, 12) == GPR_U64(ctx, 14));
        ctx->pc = 0x18C530u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C52Cu;
        // 0x18c530: 0x25ceffb0  addiu       $t6, $t6, -0x50 (Delay Slot)
        SET_GPR_S32(ctx, 14, (int32_t)ADD32(GPR_U32(ctx, 14), 4294967216));
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c52c) {
            ctx->pc = 0x18C514u;
            if (runtime->shouldPreemptGuestExecution()) {
                return;
            }
            goto label_18c514;
        }
    }
    ctx->pc = 0x18C534u;
    // 0x18c534: 0x1000fffd  b           . + 4 + (-0x3 << 2)
    ctx->pc = 0x18C534u;
    {
        const bool branch_taken_0x18c534 = (GPR_U64(ctx, 0) == GPR_U64(ctx, 0));
        if (branch_taken_0x18c534) {
            ctx->pc = 0x18C52Cu;
            if (runtime->shouldPreemptGuestExecution()) {
                return;
            }
            goto label_18c52c;
        }
    }
    ctx->pc = 0x18C53Cu;
    // 0x18c53c: 0x0  nop
    ctx->pc = 0x18c53cu;
    // NOP
label_18c540:
    // 0x18c540: 0x340fffff  ori         $t7, $zero, 0xFFFF
    ctx->pc = 0x18c540u;
    SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
label_18c544:
    // 0x18c544: 0x164f0007  bne         $s2, $t7, . + 4 + (0x7 << 2)
    ctx->pc = 0x18C544u;
    {
        const bool branch_taken_0x18c544 = (GPR_U64(ctx, 18) != GPR_U64(ctx, 15));
        ctx->pc = 0x18C548u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C544u;
        // 0x18c548: 0x340fffff  ori         $t7, $zero, 0xFFFF (Delay Slot)
        SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c544) {
            ctx->pc = 0x18C564u;
            goto label_18c564;
        }
    }
    ctx->pc = 0x18C54Cu;
    // 0x18c54c: 0x16600005  bnez        $s3, . + 4 + (0x5 << 2)
    ctx->pc = 0x18C54Cu;
    {
        const bool branch_taken_0x18c54c = (GPR_U64(ctx, 19) != GPR_U64(ctx, 0));
        ctx->pc = 0x18C550u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C54Cu;
        // 0x18c550: 0x3c0e0029  lui         $t6, 0x29 (Delay Slot)
        SET_GPR_S32(ctx, 14, (int32_t)((uint32_t)41 << 16));
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c54c) {
            ctx->pc = 0x18C564u;
            goto label_18c564;
        }
    }
    ctx->pc = 0x18C554u;
    // 0x18c554: 0x3c0f002d  lui         $t7, 0x2D
    ctx->pc = 0x18c554u;
    SET_GPR_S32(ctx, 15, (int32_t)((uint32_t)45 << 16));
    // 0x18c558: 0x25eff6b0  addiu       $t7, $t7, -0x950
    ctx->pc = 0x18c558u;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 15), 4294964912));
    // 0x18c55c: 0xadcf7380  sw          $t7, 0x7380($t6)
    ctx->pc = 0x18c55cu;
    WRITE32(ADD32(GPR_U32(ctx, 14), 29568), GPR_U32(ctx, 15));
    // 0x18c560: 0x340fffff  ori         $t7, $zero, 0xFFFF
    ctx->pc = 0x18c560u;
    SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
label_18c564:
    // 0x18c564: 0x164f000d  bne         $s2, $t7, . + 4 + (0xD << 2)
    ctx->pc = 0x18C564u;
    {
        const bool branch_taken_0x18c564 = (GPR_U64(ctx, 18) != GPR_U64(ctx, 15));
        ctx->pc = 0x18C568u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C564u;
        // 0x18c568: 0x340fffff  ori         $t7, $zero, 0xFFFF (Delay Slot)
        SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c564) {
            ctx->pc = 0x18C59Cu;
            goto label_18c59c;
        }
    }
    ctx->pc = 0x18C56Cu;
    // 0x18c56c: 0x1660000b  bnez        $s3, . + 4 + (0xB << 2)
    ctx->pc = 0x18C56Cu;
    {
        const bool branch_taken_0x18c56c = (GPR_U64(ctx, 19) != GPR_U64(ctx, 0));
        if (branch_taken_0x18c56c) {
            ctx->pc = 0x18C59Cu;
            goto label_18c59c;
        }
    }
    ctx->pc = 0x18C574u;
    // 0x18c574: 0x3c100029  lui         $s0, 0x29
    ctx->pc = 0x18c574u;
    SET_GPR_S32(ctx, 16, (int32_t)((uint32_t)41 << 16));
    // 0x18c578: 0x26107100  addiu       $s0, $s0, 0x7100
    ctx->pc = 0x18c578u;
    SET_GPR_S32(ctx, 16, (int32_t)ADD32(GPR_U32(ctx, 16), 28928));
    // 0x18c57c: 0xc08803c  jal         func_2200F0
    ctx->pc = 0x18C57Cu;
    SET_GPR_U32(ctx, 31, 0x18C584u);
    ctx->pc = 0x18C580u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x18C57Cu;
    // 0x18c580: 0x2604002c  addiu       $a0, $s0, 0x2C (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 16), 44));
    ctx->in_delay_slot = false;
    ctx->pc = 0x2200F0u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x2200F0u, 0x18C57Cu, 0x18C584u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        std::cerr << "[DIAG:18c018-callfail] target=0x2200F0u src=0x18C57Cu fallthrough=0x18C584u sp=0x" << std::hex << GPR_U32(ctx, 29) << " pc=0x" << ctx->pc << std::dec << std::endl;
        return;
    }
    std::cerr << "[DIAG:18c018-ok] after_fallthrough=0x18C584u sp=0x" << std::hex << GPR_U32(ctx, 29) << std::dec << std::endl;
    ctx->pc = 0x18C584u;
label_18c584:
    // 0x18c584: 0x3c0f002d  lui         $t7, 0x2D
    ctx->pc = 0x18c584u;
    SET_GPR_S32(ctx, 15, (int32_t)((uint32_t)45 << 16));
    // 0x18c588: 0x2604001c  addiu       $a0, $s0, 0x1C
    ctx->pc = 0x18c588u;
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 16), 28));
    // 0x18c58c: 0x25eff8f0  addiu       $t7, $t7, -0x710
    ctx->pc = 0x18c58cu;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 15), 4294965488));
    // 0x18c590: 0xc088032  jal         func_2200C8
    ctx->pc = 0x18C590u;
    SET_GPR_U32(ctx, 31, 0x18C598u);
    ctx->pc = 0x18C594u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x18C590u;
    // 0x18c594: 0xae0f001c  sw          $t7, 0x1C($s0) (Delay Slot)
    WRITE32(ADD32(GPR_U32(ctx, 16), 28), GPR_U32(ctx, 15));
    ctx->in_delay_slot = false;
    ctx->pc = 0x2200C8u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x2200C8u, 0x18C590u, 0x18C598u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        std::cerr << "[DIAG:18c018-callfail] target=0x2200C8u src=0x18C590u fallthrough=0x18C598u sp=0x" << std::hex << GPR_U32(ctx, 29) << " pc=0x" << ctx->pc << std::dec << std::endl;
        return;
    }
    std::cerr << "[DIAG:18c018-ok] after_fallthrough=0x18C598u sp=0x" << std::hex << GPR_U32(ctx, 29) << std::dec << std::endl;
    ctx->pc = 0x18C598u;
label_18c598:
    // 0x18c598: 0x340fffff  ori         $t7, $zero, 0xFFFF
    ctx->pc = 0x18c598u;
    SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
label_18c59c:
    // 0x18c59c: 0x164f000f  bne         $s2, $t7, . + 4 + (0xF << 2)
    ctx->pc = 0x18C59Cu;
    {
        const bool branch_taken_0x18c59c = (GPR_U64(ctx, 18) != GPR_U64(ctx, 15));
        ctx->pc = 0x18C5A0u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C59Cu;
        // 0x18c5a0: 0x340fffff  ori         $t7, $zero, 0xFFFF (Delay Slot)
        SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c59c) {
            ctx->pc = 0x18C5DCu;
            goto label_18c5dc;
        }
    }
    ctx->pc = 0x18C5A4u;
    // 0x18c5a4: 0x1660000d  bnez        $s3, . + 4 + (0xD << 2)
    ctx->pc = 0x18C5A4u;
    {
        const bool branch_taken_0x18c5a4 = (GPR_U64(ctx, 19) != GPR_U64(ctx, 0));
        if (branch_taken_0x18c5a4) {
            ctx->pc = 0x18C5DCu;
            goto label_18c5dc;
        }
    }
    ctx->pc = 0x18C5ACu;
    // 0x18c5ac: 0x3c100029  lui         $s0, 0x29
    ctx->pc = 0x18c5acu;
    SET_GPR_S32(ctx, 16, (int32_t)((uint32_t)41 << 16));
    // 0x18c5b0: 0x26106bc0  addiu       $s0, $s0, 0x6BC0
    ctx->pc = 0x18c5b0u;
    SET_GPR_S32(ctx, 16, (int32_t)ADD32(GPR_U32(ctx, 16), 27584));
    // 0x18c5b4: 0xc088162  jal         func_220588
    ctx->pc = 0x18C5B4u;
    SET_GPR_U32(ctx, 31, 0x18C5BCu);
    ctx->pc = 0x18C5B8u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x18C5B4u;
    // 0x18c5b8: 0x26040434  addiu       $a0, $s0, 0x434 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 16), 1076));
    ctx->in_delay_slot = false;
    ctx->pc = 0x220588u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x220588u, 0x18C5B4u, 0x18C5BCu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        std::cerr << "[DIAG:18c018-callfail] target=0x220588u src=0x18C5B4u fallthrough=0x18C5BCu sp=0x" << std::hex << GPR_U32(ctx, 29) << " pc=0x" << ctx->pc << std::dec << std::endl;
        return;
    }
    std::cerr << "[DIAG:18c018-ok] after_fallthrough=0x18C5BCu sp=0x" << std::hex << GPR_U32(ctx, 29) << std::dec << std::endl;
    ctx->pc = 0x18C5BCu;
label_18c5bc:
    // 0x18c5bc: 0xc088158  jal         func_220560
    ctx->pc = 0x18C5BCu;
    SET_GPR_U32(ctx, 31, 0x18C5C4u);
    ctx->pc = 0x18C5C0u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x18C5BCu;
    // 0x18c5c0: 0x26040424  addiu       $a0, $s0, 0x424 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 16), 1060));
    ctx->in_delay_slot = false;
    ctx->pc = 0x220560u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x220560u, 0x18C5BCu, 0x18C5C4u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        std::cerr << "[DIAG:18c018-callfail] target=0x220560u src=0x18C5BCu fallthrough=0x18C5C4u sp=0x" << std::hex << GPR_U32(ctx, 29) << " pc=0x" << ctx->pc << std::dec << std::endl;
        return;
    }
    std::cerr << "[DIAG:18c018-ok] after_fallthrough=0x18C5C4u sp=0x" << std::hex << GPR_U32(ctx, 29) << std::dec << std::endl;
    ctx->pc = 0x18C5C4u;
label_18c5c4:
    // 0x18c5c4: 0x3c0f002d  lui         $t7, 0x2D
    ctx->pc = 0x18c5c4u;
    SET_GPR_S32(ctx, 15, (int32_t)((uint32_t)45 << 16));
    // 0x18c5c8: 0x26040414  addiu       $a0, $s0, 0x414
    ctx->pc = 0x18c5c8u;
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 16), 1044));
    // 0x18c5cc: 0x25eff960  addiu       $t7, $t7, -0x6A0
    ctx->pc = 0x18c5ccu;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 15), 4294965600));
    // 0x18c5d0: 0xc088032  jal         func_2200C8
    ctx->pc = 0x18C5D0u;
    SET_GPR_U32(ctx, 31, 0x18C5D8u);
    ctx->pc = 0x18C5D4u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x18C5D0u;
    // 0x18c5d4: 0xae0f0414  sw          $t7, 0x414($s0) (Delay Slot)
    WRITE32(ADD32(GPR_U32(ctx, 16), 1044), GPR_U32(ctx, 15));
    ctx->in_delay_slot = false;
    ctx->pc = 0x2200C8u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x2200C8u, 0x18C5D0u, 0x18C5D8u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        std::cerr << "[DIAG:18c018-callfail] target=0x2200C8u src=0x18C5D0u fallthrough=0x18C5D8u sp=0x" << std::hex << GPR_U32(ctx, 29) << " pc=0x" << ctx->pc << std::dec << std::endl;
        return;
    }
    std::cerr << "[DIAG:18c018-ok] after_fallthrough=0x18C5D8u sp=0x" << std::hex << GPR_U32(ctx, 29) << std::dec << std::endl;
    ctx->pc = 0x18C5D8u;
label_18c5d8:
    // 0x18c5d8: 0x340fffff  ori         $t7, $zero, 0xFFFF
    ctx->pc = 0x18c5d8u;
    SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
label_18c5dc:
    // 0x18c5dc: 0x164f0010  bne         $s2, $t7, . + 4 + (0x10 << 2)
    ctx->pc = 0x18C5DCu;
    {
        const bool branch_taken_0x18c5dc = (GPR_U64(ctx, 18) != GPR_U64(ctx, 15));
        ctx->pc = 0x18C5E0u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C5DCu;
        // 0x18c5e0: 0x340fffff  ori         $t7, $zero, 0xFFFF (Delay Slot)
        SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c5dc) {
            ctx->pc = 0x18C620u;
            goto label_18c620;
        }
    }
    ctx->pc = 0x18C5E4u;
    // 0x18c5e4: 0x1660000e  bnez        $s3, . + 4 + (0xE << 2)
    ctx->pc = 0x18C5E4u;
    {
        const bool branch_taken_0x18c5e4 = (GPR_U64(ctx, 19) != GPR_U64(ctx, 0));
        if (branch_taken_0x18c5e4) {
            ctx->pc = 0x18C620u;
            goto label_18c620;
        }
    }
    ctx->pc = 0x18C5ECu;
    // 0x18c5ec: 0x3c0f0029  lui         $t7, 0x29
    ctx->pc = 0x18c5ecu;
    SET_GPR_S32(ctx, 15, (int32_t)((uint32_t)41 << 16));
    // 0x18c5f0: 0x25ef6a50  addiu       $t7, $t7, 0x6A50
    ctx->pc = 0x18c5f0u;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 15), 27216));
    // 0x18c5f4: 0x11e00009  beqz        $t7, . + 4 + (0x9 << 2)
    ctx->pc = 0x18C5F4u;
    {
        const bool branch_taken_0x18c5f4 = (GPR_U64(ctx, 15) == GPR_U64(ctx, 0));
        ctx->pc = 0x18C5F8u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C5F4u;
        // 0x18c5f8: 0x1e0702d  daddu       $t6, $t7, $zero (Delay Slot)
        SET_GPR_U64(ctx, 14, (uint64_t)GPR_U64(ctx, 15) + (uint64_t)GPR_U64(ctx, 0));
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c5f4) {
            ctx->pc = 0x18C61Cu;
            goto label_18c61c;
        }
    }
    ctx->pc = 0x18C5FCu;
    // 0x18c5fc: 0x25ef0060  addiu       $t7, $t7, 0x60
    ctx->pc = 0x18c5fcu;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 15), 96));
label_18c600:
    // 0x18c600: 0x0  nop
    ctx->pc = 0x18c600u;
    // NOP
    // 0x18c604: 0x0  nop
    ctx->pc = 0x18c604u;
    // NOP
    // 0x18c608: 0x0  nop
    ctx->pc = 0x18c608u;
    // NOP
    // 0x18c60c: 0x0  nop
    ctx->pc = 0x18c60cu;
    // NOP
    // 0x18c610: 0x0  nop
    ctx->pc = 0x18c610u;
    // NOP
    // 0x18c614: 0x15eefffa  bne         $t7, $t6, . + 4 + (-0x6 << 2)
    ctx->pc = 0x18C614u;
    {
        const bool branch_taken_0x18c614 = (GPR_U64(ctx, 15) != GPR_U64(ctx, 14));
        ctx->pc = 0x18C618u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C614u;
        // 0x18c618: 0x25effff4  addiu       $t7, $t7, -0xC (Delay Slot)
        SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 15), 4294967284));
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c614) {
            ctx->pc = 0x18C600u;
            if (runtime->shouldPreemptGuestExecution()) {
                return;
            }
            goto label_18c600;
        }
    }
    ctx->pc = 0x18C61Cu;
label_18c61c:
    // 0x18c61c: 0x340fffff  ori         $t7, $zero, 0xFFFF
    ctx->pc = 0x18c61cu;
    SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
label_18c620:
    // 0x18c620: 0x164f0010  bne         $s2, $t7, . + 4 + (0x10 << 2)
    ctx->pc = 0x18C620u;
    {
        const bool branch_taken_0x18c620 = (GPR_U64(ctx, 18) != GPR_U64(ctx, 15));
        ctx->pc = 0x18C624u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C620u;
        // 0x18c624: 0x340fffff  ori         $t7, $zero, 0xFFFF (Delay Slot)
        SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c620) {
            ctx->pc = 0x18C664u;
            goto label_18c664;
        }
    }
    ctx->pc = 0x18C628u;
    // 0x18c628: 0x1660000e  bnez        $s3, . + 4 + (0xE << 2)
    ctx->pc = 0x18C628u;
    {
        const bool branch_taken_0x18c628 = (GPR_U64(ctx, 19) != GPR_U64(ctx, 0));
        if (branch_taken_0x18c628) {
            ctx->pc = 0x18C664u;
            goto label_18c664;
        }
    }
    ctx->pc = 0x18C630u;
    // 0x18c630: 0x3c0f0029  lui         $t7, 0x29
    ctx->pc = 0x18c630u;
    SET_GPR_S32(ctx, 15, (int32_t)((uint32_t)41 << 16));
    // 0x18c634: 0x25ef6640  addiu       $t7, $t7, 0x6640
    ctx->pc = 0x18c634u;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 15), 26176));
    // 0x18c638: 0x11e00009  beqz        $t7, . + 4 + (0x9 << 2)
    ctx->pc = 0x18C638u;
    {
        const bool branch_taken_0x18c638 = (GPR_U64(ctx, 15) == GPR_U64(ctx, 0));
        ctx->pc = 0x18C63Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C638u;
        // 0x18c63c: 0x1e0702d  daddu       $t6, $t7, $zero (Delay Slot)
        SET_GPR_U64(ctx, 14, (uint64_t)GPR_U64(ctx, 15) + (uint64_t)GPR_U64(ctx, 0));
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c638) {
            ctx->pc = 0x18C660u;
            goto label_18c660;
        }
    }
    ctx->pc = 0x18C640u;
    // 0x18c640: 0x25ef0400  addiu       $t7, $t7, 0x400
    ctx->pc = 0x18c640u;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 15), 1024));
label_18c644:
    // 0x18c644: 0x0  nop
    ctx->pc = 0x18c644u;
    // NOP
    // 0x18c648: 0x0  nop
    ctx->pc = 0x18c648u;
    // NOP
    // 0x18c64c: 0x0  nop
    ctx->pc = 0x18c64cu;
    // NOP
    // 0x18c650: 0x0  nop
    ctx->pc = 0x18c650u;
    // NOP
    // 0x18c654: 0x0  nop
    ctx->pc = 0x18c654u;
    // NOP
    // 0x18c658: 0x15eefffa  bne         $t7, $t6, . + 4 + (-0x6 << 2)
    ctx->pc = 0x18C658u;
    {
        const bool branch_taken_0x18c658 = (GPR_U64(ctx, 15) != GPR_U64(ctx, 14));
        ctx->pc = 0x18C65Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C658u;
        // 0x18c65c: 0x25efff00  addiu       $t7, $t7, -0x100 (Delay Slot)
        SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 15), 4294967040));
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c658) {
            ctx->pc = 0x18C644u;
            if (runtime->shouldPreemptGuestExecution()) {
                return;
            }
            goto label_18c644;
        }
    }
    ctx->pc = 0x18C660u;
label_18c660:
    // 0x18c660: 0x340fffff  ori         $t7, $zero, 0xFFFF
    ctx->pc = 0x18c660u;
    SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
label_18c664:
    // 0x18c664: 0x164f002a  bne         $s2, $t7, . + 4 + (0x2A << 2)
    ctx->pc = 0x18C664u;
    {
        const bool branch_taken_0x18c664 = (GPR_U64(ctx, 18) != GPR_U64(ctx, 15));
        ctx->pc = 0x18C668u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C664u;
        // 0x18c668: 0x340fffff  ori         $t7, $zero, 0xFFFF (Delay Slot)
        SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c664) {
            ctx->pc = 0x18C710u;
            goto label_18c710;
        }
    }
    ctx->pc = 0x18C66Cu;
    // 0x18c66c: 0x16600028  bnez        $s3, . + 4 + (0x28 << 2)
    ctx->pc = 0x18C66Cu;
    {
        const bool branch_taken_0x18c66c = (GPR_U64(ctx, 19) != GPR_U64(ctx, 0));
        ctx->pc = 0x18C670u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C66Cu;
        // 0x18c670: 0x240efd60  addiu       $t6, $zero, -0x2A0 (Delay Slot)
        SET_GPR_S32(ctx, 14, (int32_t)ADD32(GPR_U32(ctx, 0), 4294966624));
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c66c) {
            ctx->pc = 0x18C710u;
            goto label_18c710;
        }
    }
    ctx->pc = 0x18C674u;
    // 0x18c674: 0x3c0f0029  lui         $t7, 0x29
    ctx->pc = 0x18c674u;
    SET_GPR_S32(ctx, 15, (int32_t)((uint32_t)41 << 16));
    // 0x18c678: 0x25ef6080  addiu       $t7, $t7, 0x6080
    ctx->pc = 0x18c678u;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 15), 24704));
    // 0x18c67c: 0x11ee0009  beq         $t7, $t6, . + 4 + (0x9 << 2)
    ctx->pc = 0x18C67Cu;
    {
        const bool branch_taken_0x18c67c = (GPR_U64(ctx, 15) == GPR_U64(ctx, 14));
        ctx->pc = 0x18C680u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C67Cu;
        // 0x18c680: 0x25ee02a0  addiu       $t6, $t7, 0x2A0 (Delay Slot)
        SET_GPR_S32(ctx, 14, (int32_t)ADD32(GPR_U32(ctx, 15), 672));
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c67c) {
            ctx->pc = 0x18C6A4u;
            goto label_18c6a4;
        }
    }
    ctx->pc = 0x18C684u;
    // 0x18c684: 0x25ef05a0  addiu       $t7, $t7, 0x5A0
    ctx->pc = 0x18c684u;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 15), 1440));
label_18c688:
    // 0x18c688: 0x0  nop
    ctx->pc = 0x18c688u;
    // NOP
    // 0x18c68c: 0x0  nop
    ctx->pc = 0x18c68cu;
    // NOP
    // 0x18c690: 0x0  nop
    ctx->pc = 0x18c690u;
    // NOP
    // 0x18c694: 0x0  nop
    ctx->pc = 0x18c694u;
    // NOP
    // 0x18c698: 0x0  nop
    ctx->pc = 0x18c698u;
    // NOP
    // 0x18c69c: 0x15eefffa  bne         $t7, $t6, . + 4 + (-0x6 << 2)
    ctx->pc = 0x18C69Cu;
    {
        const bool branch_taken_0x18c69c = (GPR_U64(ctx, 15) != GPR_U64(ctx, 14));
        ctx->pc = 0x18C6A0u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C69Cu;
        // 0x18c6a0: 0x25efffa0  addiu       $t7, $t7, -0x60 (Delay Slot)
        SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 15), 4294967200));
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c69c) {
            ctx->pc = 0x18C688u;
            if (runtime->shouldPreemptGuestExecution()) {
                return;
            }
            goto label_18c688;
        }
    }
    ctx->pc = 0x18C6A4u;
label_18c6a4:
    // 0x18c6a4: 0x3c0f0029  lui         $t7, 0x29
    ctx->pc = 0x18c6a4u;
    SET_GPR_S32(ctx, 15, (int32_t)((uint32_t)41 << 16));
    // 0x18c6a8: 0x240efde0  addiu       $t6, $zero, -0x220
    ctx->pc = 0x18c6a8u;
    SET_GPR_S32(ctx, 14, (int32_t)ADD32(GPR_U32(ctx, 0), 4294966752));
    // 0x18c6ac: 0x25ef6080  addiu       $t7, $t7, 0x6080
    ctx->pc = 0x18c6acu;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 15), 24704));
    // 0x18c6b0: 0x11ee0009  beq         $t7, $t6, . + 4 + (0x9 << 2)
    ctx->pc = 0x18C6B0u;
    {
        const bool branch_taken_0x18c6b0 = (GPR_U64(ctx, 15) == GPR_U64(ctx, 14));
        ctx->pc = 0x18C6B4u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C6B0u;
        // 0x18c6b4: 0x25ee0220  addiu       $t6, $t7, 0x220 (Delay Slot)
        SET_GPR_S32(ctx, 14, (int32_t)ADD32(GPR_U32(ctx, 15), 544));
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c6b0) {
            ctx->pc = 0x18C6D8u;
            goto label_18c6d8;
        }
    }
    ctx->pc = 0x18C6B8u;
    // 0x18c6b8: 0x25ef0298  addiu       $t7, $t7, 0x298
    ctx->pc = 0x18c6b8u;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 15), 664));
label_18c6bc:
    // 0x18c6bc: 0x0  nop
    ctx->pc = 0x18c6bcu;
    // NOP
    // 0x18c6c0: 0x0  nop
    ctx->pc = 0x18c6c0u;
    // NOP
    // 0x18c6c4: 0x0  nop
    ctx->pc = 0x18c6c4u;
    // NOP
    // 0x18c6c8: 0x0  nop
    ctx->pc = 0x18c6c8u;
    // NOP
    // 0x18c6cc: 0x0  nop
    ctx->pc = 0x18c6ccu;
    // NOP
    // 0x18c6d0: 0x15eefffa  bne         $t7, $t6, . + 4 + (-0x6 << 2)
    ctx->pc = 0x18C6D0u;
    {
        const bool branch_taken_0x18c6d0 = (GPR_U64(ctx, 15) != GPR_U64(ctx, 14));
        ctx->pc = 0x18C6D4u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C6D0u;
        // 0x18c6d4: 0x25efffd8  addiu       $t7, $t7, -0x28 (Delay Slot)
        SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 15), 4294967256));
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c6d0) {
            ctx->pc = 0x18C6BCu;
            if (runtime->shouldPreemptGuestExecution()) {
                return;
            }
            goto label_18c6bc;
        }
    }
    ctx->pc = 0x18C6D8u;
label_18c6d8:
    // 0x18c6d8: 0x3c110029  lui         $s1, 0x29
    ctx->pc = 0x18c6d8u;
    SET_GPR_S32(ctx, 17, (int32_t)((uint32_t)41 << 16));
    // 0x18c6dc: 0x26306080  addiu       $s0, $s1, 0x6080
    ctx->pc = 0x18c6dcu;
    SET_GPR_S32(ctx, 16, (int32_t)ADD32(GPR_U32(ctx, 17), 24704));
    // 0x18c6e0: 0xc087fcc  jal         func_21FF30
    ctx->pc = 0x18C6E0u;
    SET_GPR_U32(ctx, 31, 0x18C6E8u);
    ctx->pc = 0x18C6E4u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x18C6E0u;
    // 0x18c6e4: 0x26040200  addiu       $a0, $s0, 0x200 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 16), 512));
    ctx->in_delay_slot = false;
    ctx->pc = 0x21FF30u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x21FF30u, 0x18C6E0u, 0x18C6E8u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        std::cerr << "[DIAG:18c018-callfail] target=0x21FF30u src=0x18C6E0u fallthrough=0x18C6E8u sp=0x" << std::hex << GPR_U32(ctx, 29) << " pc=0x" << ctx->pc << std::dec << std::endl;
        return;
    }
    std::cerr << "[DIAG:18c018-ok] after_fallthrough=0x18C6E8u sp=0x" << std::hex << GPR_U32(ctx, 29) << std::dec << std::endl;
    ctx->pc = 0x18C6E8u;
label_18c6e8:
    // 0x18c6e8: 0xc087fc0  jal         func_21FF00
    ctx->pc = 0x18C6E8u;
    SET_GPR_U32(ctx, 31, 0x18C6F0u);
    ctx->pc = 0x18C6ECu;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x18C6E8u;
    // 0x18c6ec: 0x260401f0  addiu       $a0, $s0, 0x1F0 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 16), 496));
    ctx->in_delay_slot = false;
    ctx->pc = 0x21FF00u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x21FF00u, 0x18C6E8u, 0x18C6F0u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        std::cerr << "[DIAG:18c018-callfail] target=0x21FF00u src=0x18C6E8u fallthrough=0x18C6F0u sp=0x" << std::hex << GPR_U32(ctx, 29) << " pc=0x" << ctx->pc << std::dec << std::endl;
        return;
    }
    std::cerr << "[DIAG:18c018-ok] after_fallthrough=0x18C6F0u sp=0x" << std::hex << GPR_U32(ctx, 29) << std::dec << std::endl;
    ctx->pc = 0x18C6F0u;
label_18c6f0:
    // 0x18c6f0: 0xc087fb4  jal         func_21FED0
    ctx->pc = 0x18C6F0u;
    SET_GPR_U32(ctx, 31, 0x18C6F8u);
    ctx->pc = 0x18C6F4u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x18C6F0u;
    // 0x18c6f4: 0x26040130  addiu       $a0, $s0, 0x130 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 16), 304));
    ctx->in_delay_slot = false;
    ctx->pc = 0x21FED0u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x21FED0u, 0x18C6F0u, 0x18C6F8u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        std::cerr << "[DIAG:18c018-callfail] target=0x21FED0u src=0x18C6F0u fallthrough=0x18C6F8u sp=0x" << std::hex << GPR_U32(ctx, 29) << " pc=0x" << ctx->pc << std::dec << std::endl;
        return;
    }
    std::cerr << "[DIAG:18c018-ok] after_fallthrough=0x18C6F8u sp=0x" << std::hex << GPR_U32(ctx, 29) << std::dec << std::endl;
    ctx->pc = 0x18C6F8u;
label_18c6f8:
    // 0x18c6f8: 0x3c0f002d  lui         $t7, 0x2D
    ctx->pc = 0x18c6f8u;
    SET_GPR_S32(ctx, 15, (int32_t)((uint32_t)45 << 16));
    // 0x18c6fc: 0x200202d  daddu       $a0, $s0, $zero
    ctx->pc = 0x18c6fcu;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 16) + (uint64_t)GPR_U64(ctx, 0));
    // 0x18c700: 0x25eff600  addiu       $t7, $t7, -0xA00
    ctx->pc = 0x18c700u;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 15), 4294964736));
    // 0x18c704: 0xc088066  jal         func_220198
    ctx->pc = 0x18C704u;
    SET_GPR_U32(ctx, 31, 0x18C70Cu);
    ctx->pc = 0x18C708u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x18C704u;
    // 0x18c708: 0xae2f6080  sw          $t7, 0x6080($s1) (Delay Slot)
    WRITE32(ADD32(GPR_U32(ctx, 17), 24704), GPR_U32(ctx, 15));
    ctx->in_delay_slot = false;
    ctx->pc = 0x220198u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x220198u, 0x18C704u, 0x18C70Cu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        std::cerr << "[DIAG:18c018-callfail] target=0x220198u src=0x18C704u fallthrough=0x18C70Cu sp=0x" << std::hex << GPR_U32(ctx, 29) << " pc=0x" << ctx->pc << std::dec << std::endl;
        return;
    }
    std::cerr << "[DIAG:18c018-ok] after_fallthrough=0x18C70Cu sp=0x" << std::hex << GPR_U32(ctx, 29) << std::dec << std::endl;
    ctx->pc = 0x18C70Cu;
label_18c70c:
    // 0x18c70c: 0x340fffff  ori         $t7, $zero, 0xFFFF
    ctx->pc = 0x18c70cu;
    SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
label_18c710:
    // 0x18c710: 0x164f0007  bne         $s2, $t7, . + 4 + (0x7 << 2)
    ctx->pc = 0x18C710u;
    {
        const bool branch_taken_0x18c710 = (GPR_U64(ctx, 18) != GPR_U64(ctx, 15));
        ctx->pc = 0x18C714u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C710u;
        // 0x18c714: 0x340fffff  ori         $t7, $zero, 0xFFFF (Delay Slot)
        SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c710) {
            ctx->pc = 0x18C730u;
            goto label_18c730;
        }
    }
    ctx->pc = 0x18C718u;
    // 0x18c718: 0x16600005  bnez        $s3, . + 4 + (0x5 << 2)
    ctx->pc = 0x18C718u;
    {
        const bool branch_taken_0x18c718 = (GPR_U64(ctx, 19) != GPR_U64(ctx, 0));
        if (branch_taken_0x18c718) {
            ctx->pc = 0x18C730u;
            goto label_18c730;
        }
    }
    ctx->pc = 0x18C720u;
    // 0x18c720: 0x3c040029  lui         $a0, 0x29
    ctx->pc = 0x18c720u;
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)41 << 16));
    // 0x18c724: 0xc06047a  jal         func_1811E8
    ctx->pc = 0x18C724u;
    SET_GPR_U32(ctx, 31, 0x18C72Cu);
    ctx->pc = 0x18C728u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x18C724u;
    // 0x18c728: 0x24845840  addiu       $a0, $a0, 0x5840 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 4), 22592));
    ctx->in_delay_slot = false;
    ctx->pc = 0x1811E8u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1811E8u, 0x18C724u, 0x18C72Cu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        std::cerr << "[DIAG:18c018-callfail] target=0x1811E8u src=0x18C724u fallthrough=0x18C72Cu sp=0x" << std::hex << GPR_U32(ctx, 29) << " pc=0x" << ctx->pc << std::dec << std::endl;
        return;
    }
    std::cerr << "[DIAG:18c018-ok] after_fallthrough=0x18C72Cu sp=0x" << std::hex << GPR_U32(ctx, 29) << std::dec << std::endl;
    ctx->pc = 0x18C72Cu;
label_18c72c:
    // 0x18c72c: 0x340fffff  ori         $t7, $zero, 0xFFFF
    ctx->pc = 0x18c72cu;
    SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
label_18c730:
    // 0x18c730: 0x164f0007  bne         $s2, $t7, . + 4 + (0x7 << 2)
    ctx->pc = 0x18C730u;
    {
        const bool branch_taken_0x18c730 = (GPR_U64(ctx, 18) != GPR_U64(ctx, 15));
        ctx->pc = 0x18C734u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C730u;
        // 0x18c734: 0x340fffff  ori         $t7, $zero, 0xFFFF (Delay Slot)
        SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c730) {
            ctx->pc = 0x18C750u;
            goto label_18c750;
        }
    }
    ctx->pc = 0x18C738u;
    // 0x18c738: 0x16600005  bnez        $s3, . + 4 + (0x5 << 2)
    ctx->pc = 0x18C738u;
    {
        const bool branch_taken_0x18c738 = (GPR_U64(ctx, 19) != GPR_U64(ctx, 0));
        ctx->pc = 0x18C73Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C738u;
        // 0x18c73c: 0x3c0e0028  lui         $t6, 0x28 (Delay Slot)
        SET_GPR_S32(ctx, 14, (int32_t)((uint32_t)40 << 16));
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c738) {
            ctx->pc = 0x18C750u;
            goto label_18c750;
        }
    }
    ctx->pc = 0x18C740u;
    // 0x18c740: 0x3c0f002d  lui         $t7, 0x2D
    ctx->pc = 0x18c740u;
    SET_GPR_S32(ctx, 15, (int32_t)((uint32_t)45 << 16));
    // 0x18c744: 0x25eff7b0  addiu       $t7, $t7, -0x850
    ctx->pc = 0x18c744u;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 15), 4294965168));
    // 0x18c748: 0xadcf17c0  sw          $t7, 0x17C0($t6)
    ctx->pc = 0x18c748u;
    WRITE32(ADD32(GPR_U32(ctx, 14), 6080), GPR_U32(ctx, 15));
    // 0x18c74c: 0x340fffff  ori         $t7, $zero, 0xFFFF
    ctx->pc = 0x18c74cu;
    SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
label_18c750:
    // 0x18c750: 0x164f0007  bne         $s2, $t7, . + 4 + (0x7 << 2)
    ctx->pc = 0x18C750u;
    {
        const bool branch_taken_0x18c750 = (GPR_U64(ctx, 18) != GPR_U64(ctx, 15));
        ctx->pc = 0x18C754u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C750u;
        // 0x18c754: 0x340fffff  ori         $t7, $zero, 0xFFFF (Delay Slot)
        SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c750) {
            ctx->pc = 0x18C770u;
            goto label_18c770;
        }
    }
    ctx->pc = 0x18C758u;
    // 0x18c758: 0x16600005  bnez        $s3, . + 4 + (0x5 << 2)
    ctx->pc = 0x18C758u;
    {
        const bool branch_taken_0x18c758 = (GPR_U64(ctx, 19) != GPR_U64(ctx, 0));
        ctx->pc = 0x18C75Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C758u;
        // 0x18c75c: 0x3c0e0027  lui         $t6, 0x27 (Delay Slot)
        SET_GPR_S32(ctx, 14, (int32_t)((uint32_t)39 << 16));
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c758) {
            ctx->pc = 0x18C770u;
            goto label_18c770;
        }
    }
    ctx->pc = 0x18C760u;
    // 0x18c760: 0x3c0f002d  lui         $t7, 0x2D
    ctx->pc = 0x18c760u;
    SET_GPR_S32(ctx, 15, (int32_t)((uint32_t)45 << 16));
    // 0x18c764: 0x25eff7b0  addiu       $t7, $t7, -0x850
    ctx->pc = 0x18c764u;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 15), 4294965168));
    // 0x18c768: 0xadcfd740  sw          $t7, -0x28C0($t6)
    ctx->pc = 0x18c768u;
    WRITE32(ADD32(GPR_U32(ctx, 14), 4294956864), GPR_U32(ctx, 15));
    // 0x18c76c: 0x340fffff  ori         $t7, $zero, 0xFFFF
    ctx->pc = 0x18c76cu;
    SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
label_18c770:
    // 0x18c770: 0x164f001a  bne         $s2, $t7, . + 4 + (0x1A << 2)
    ctx->pc = 0x18C770u;
    {
        const bool branch_taken_0x18c770 = (GPR_U64(ctx, 18) != GPR_U64(ctx, 15));
        ctx->pc = 0x18C774u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C770u;
        // 0x18c774: 0x340fffff  ori         $t7, $zero, 0xFFFF (Delay Slot)
        SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c770) {
            ctx->pc = 0x18C7DCu;
            goto label_18c7dc;
        }
    }
    ctx->pc = 0x18C778u;
    // 0x18c778: 0x16600018  bnez        $s3, . + 4 + (0x18 << 2)
    ctx->pc = 0x18C778u;
    {
        const bool branch_taken_0x18c778 = (GPR_U64(ctx, 19) != GPR_U64(ctx, 0));
        if (branch_taken_0x18c778) {
            ctx->pc = 0x18C7DCu;
            goto label_18c7dc;
        }
    }
    ctx->pc = 0x18C780u;
    // 0x18c780: 0x3c100027  lui         $s0, 0x27
    ctx->pc = 0x18c780u;
    SET_GPR_S32(ctx, 16, (int32_t)((uint32_t)39 << 16));
    // 0x18c784: 0x2610cd80  addiu       $s0, $s0, -0x3280
    ctx->pc = 0x18c784u;
    SET_GPR_S32(ctx, 16, (int32_t)ADD32(GPR_U32(ctx, 16), 4294954368));
    // 0x18c788: 0xc0650ca  jal         func_194328
    ctx->pc = 0x18C788u;
    SET_GPR_U32(ctx, 31, 0x18C790u);
    ctx->pc = 0x18C78Cu;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x18C788u;
    // 0x18c78c: 0x26040938  addiu       $a0, $s0, 0x938 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 16), 2360));
    ctx->in_delay_slot = false;
    ctx->pc = 0x194328u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x194328u, 0x18C788u, 0x18C790u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        std::cerr << "[DIAG:18c018-callfail] target=0x194328u src=0x18C788u fallthrough=0x18C790u sp=0x" << std::hex << GPR_U32(ctx, 29) << " pc=0x" << ctx->pc << std::dec << std::endl;
        return;
    }
    std::cerr << "[DIAG:18c018-ok] after_fallthrough=0x18C790u sp=0x" << std::hex << GPR_U32(ctx, 29) << std::dec << std::endl;
    ctx->pc = 0x18C790u;
label_18c790:
    // 0x18c790: 0xc065080  jal         func_194200
    ctx->pc = 0x18C790u;
    SET_GPR_U32(ctx, 31, 0x18C798u);
    ctx->pc = 0x18C794u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x18C790u;
    // 0x18c794: 0x260408c0  addiu       $a0, $s0, 0x8C0 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 16), 2240));
    ctx->in_delay_slot = false;
    ctx->pc = 0x194200u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x194200u, 0x18C790u, 0x18C798u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        std::cerr << "[DIAG:18c018-callfail] target=0x194200u src=0x18C790u fallthrough=0x18C798u sp=0x" << std::hex << GPR_U32(ctx, 29) << " pc=0x" << ctx->pc << std::dec << std::endl;
        return;
    }
    std::cerr << "[DIAG:18c018-ok] after_fallthrough=0x18C798u sp=0x" << std::hex << GPR_U32(ctx, 29) << std::dec << std::endl;
    ctx->pc = 0x18C798u;
label_18c798:
    // 0x18c798: 0xc088078  jal         func_2201E0
    ctx->pc = 0x18C798u;
    SET_GPR_U32(ctx, 31, 0x18C7A0u);
    ctx->pc = 0x18C79Cu;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x18C798u;
    // 0x18c79c: 0x26040640  addiu       $a0, $s0, 0x640 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 16), 1600));
    ctx->in_delay_slot = false;
    ctx->pc = 0x2201E0u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x2201E0u, 0x18C798u, 0x18C7A0u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        std::cerr << "[DIAG:18c018-callfail] target=0x2201E0u src=0x18C798u fallthrough=0x18C7A0u sp=0x" << std::hex << GPR_U32(ctx, 29) << " pc=0x" << ctx->pc << std::dec << std::endl;
        return;
    }
    std::cerr << "[DIAG:18c018-ok] after_fallthrough=0x18C7A0u sp=0x" << std::hex << GPR_U32(ctx, 29) << std::dec << std::endl;
    ctx->pc = 0x18C7A0u;
label_18c7a0:
    // 0x18c7a0: 0xc088080  jal         func_220200
    ctx->pc = 0x18C7A0u;
    SET_GPR_U32(ctx, 31, 0x18C7A8u);
    ctx->pc = 0x18C7A4u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x18C7A0u;
    // 0x18c7a4: 0x260403c0  addiu       $a0, $s0, 0x3C0 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 16), 960));
    ctx->in_delay_slot = false;
    ctx->pc = 0x220200u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x220200u, 0x18C7A0u, 0x18C7A8u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        std::cerr << "[DIAG:18c018-callfail] target=0x220200u src=0x18C7A0u fallthrough=0x18C7A8u sp=0x" << std::hex << GPR_U32(ctx, 29) << " pc=0x" << ctx->pc << std::dec << std::endl;
        return;
    }
    std::cerr << "[DIAG:18c018-ok] after_fallthrough=0x18C7A8u sp=0x" << std::hex << GPR_U32(ctx, 29) << std::dec << std::endl;
    ctx->pc = 0x18C7A8u;
label_18c7a8:
    // 0x18c7a8: 0xc088080  jal         func_220200
    ctx->pc = 0x18C7A8u;
    SET_GPR_U32(ctx, 31, 0x18C7B0u);
    ctx->pc = 0x18C7ACu;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x18C7A8u;
    // 0x18c7ac: 0x26040140  addiu       $a0, $s0, 0x140 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 16), 320));
    ctx->in_delay_slot = false;
    ctx->pc = 0x220200u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x220200u, 0x18C7A8u, 0x18C7B0u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        std::cerr << "[DIAG:18c018-callfail] target=0x220200u src=0x18C7A8u fallthrough=0x18C7B0u sp=0x" << std::hex << GPR_U32(ctx, 29) << " pc=0x" << ctx->pc << std::dec << std::endl;
        return;
    }
    std::cerr << "[DIAG:18c018-ok] after_fallthrough=0x18C7B0u sp=0x" << std::hex << GPR_U32(ctx, 29) << std::dec << std::endl;
    ctx->pc = 0x18C7B0u;
label_18c7b0:
    // 0x18c7b0: 0x3c0f002d  lui         $t7, 0x2D
    ctx->pc = 0x18c7b0u;
    SET_GPR_S32(ctx, 15, (int32_t)((uint32_t)45 << 16));
    // 0x18c7b4: 0x260400cc  addiu       $a0, $s0, 0xCC
    ctx->pc = 0x18c7b4u;
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 16), 204));
    // 0x18c7b8: 0x25eff7c0  addiu       $t7, $t7, -0x840
    ctx->pc = 0x18c7b8u;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 15), 4294965184));
    // 0x18c7bc: 0xc064cba  jal         func_1932E8
    ctx->pc = 0x18C7BCu;
    SET_GPR_U32(ctx, 31, 0x18C7C4u);
    ctx->pc = 0x18C7C0u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x18C7BCu;
    // 0x18c7c0: 0xae0f0030  sw          $t7, 0x30($s0) (Delay Slot)
    WRITE32(ADD32(GPR_U32(ctx, 16), 48), GPR_U32(ctx, 15));
    ctx->in_delay_slot = false;
    ctx->pc = 0x1932E8u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1932E8u, 0x18C7BCu, 0x18C7C4u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        std::cerr << "[DIAG:18c018-callfail] target=0x1932E8u src=0x18C7BCu fallthrough=0x18C7C4u sp=0x" << std::hex << GPR_U32(ctx, 29) << " pc=0x" << ctx->pc << std::dec << std::endl;
        return;
    }
    std::cerr << "[DIAG:18c018-ok] after_fallthrough=0x18C7C4u sp=0x" << std::hex << GPR_U32(ctx, 29) << std::dec << std::endl;
    ctx->pc = 0x18C7C4u;
label_18c7c4:
    // 0x18c7c4: 0xc064cba  jal         func_1932E8
    ctx->pc = 0x18C7C4u;
    SET_GPR_U32(ctx, 31, 0x18C7CCu);
    ctx->pc = 0x18C7C8u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x18C7C4u;
    // 0x18c7c8: 0x26040080  addiu       $a0, $s0, 0x80 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 16), 128));
    ctx->in_delay_slot = false;
    ctx->pc = 0x1932E8u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1932E8u, 0x18C7C4u, 0x18C7CCu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        std::cerr << "[DIAG:18c018-callfail] target=0x1932E8u src=0x18C7C4u fallthrough=0x18C7CCu sp=0x" << std::hex << GPR_U32(ctx, 29) << " pc=0x" << ctx->pc << std::dec << std::endl;
        return;
    }
    std::cerr << "[DIAG:18c018-ok] after_fallthrough=0x18C7CCu sp=0x" << std::hex << GPR_U32(ctx, 29) << std::dec << std::endl;
    ctx->pc = 0x18C7CCu;
label_18c7cc:
    // 0x18c7cc: 0x26100034  addiu       $s0, $s0, 0x34
    ctx->pc = 0x18c7ccu;
    SET_GPR_S32(ctx, 16, (int32_t)ADD32(GPR_U32(ctx, 16), 52));
    // 0x18c7d0: 0xc064cba  jal         func_1932E8
    ctx->pc = 0x18C7D0u;
    SET_GPR_U32(ctx, 31, 0x18C7D8u);
    ctx->pc = 0x18C7D4u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x18C7D0u;
    // 0x18c7d4: 0x200202d  daddu       $a0, $s0, $zero (Delay Slot)
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 16) + (uint64_t)GPR_U64(ctx, 0));
    ctx->in_delay_slot = false;
    ctx->pc = 0x1932E8u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1932E8u, 0x18C7D0u, 0x18C7D8u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        std::cerr << "[DIAG:18c018-callfail] target=0x1932E8u src=0x18C7D0u fallthrough=0x18C7D8u sp=0x" << std::hex << GPR_U32(ctx, 29) << " pc=0x" << ctx->pc << std::dec << std::endl;
        return;
    }
    std::cerr << "[DIAG:18c018-ok] after_fallthrough=0x18C7D8u sp=0x" << std::hex << GPR_U32(ctx, 29) << std::dec << std::endl;
    ctx->pc = 0x18C7D8u;
label_18c7d8:
    // 0x18c7d8: 0x340fffff  ori         $t7, $zero, 0xFFFF
    ctx->pc = 0x18c7d8u;
    SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
label_18c7dc:
    // 0x18c7dc: 0x164f0007  bne         $s2, $t7, . + 4 + (0x7 << 2)
    ctx->pc = 0x18C7DCu;
    {
        const bool branch_taken_0x18c7dc = (GPR_U64(ctx, 18) != GPR_U64(ctx, 15));
        ctx->pc = 0x18C7E0u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C7DCu;
        // 0x18c7e0: 0x340fffff  ori         $t7, $zero, 0xFFFF (Delay Slot)
        SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c7dc) {
            ctx->pc = 0x18C7FCu;
            goto label_18c7fc;
        }
    }
    ctx->pc = 0x18C7E4u;
    // 0x18c7e4: 0x16600005  bnez        $s3, . + 4 + (0x5 << 2)
    ctx->pc = 0x18C7E4u;
    {
        const bool branch_taken_0x18c7e4 = (GPR_U64(ctx, 19) != GPR_U64(ctx, 0));
        ctx->pc = 0x18C7E8u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C7E4u;
        // 0x18c7e8: 0x3c0e0027  lui         $t6, 0x27 (Delay Slot)
        SET_GPR_S32(ctx, 14, (int32_t)((uint32_t)39 << 16));
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c7e4) {
            ctx->pc = 0x18C7FCu;
            goto label_18c7fc;
        }
    }
    ctx->pc = 0x18C7ECu;
    // 0x18c7ec: 0x3c0f002d  lui         $t7, 0x2D
    ctx->pc = 0x18c7ecu;
    SET_GPR_S32(ctx, 15, (int32_t)((uint32_t)45 << 16));
    // 0x18c7f0: 0x25eff820  addiu       $t7, $t7, -0x7E0
    ctx->pc = 0x18c7f0u;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 15), 4294965280));
    // 0x18c7f4: 0xadcfcb80  sw          $t7, -0x3480($t6)
    ctx->pc = 0x18c7f4u;
    WRITE32(ADD32(GPR_U32(ctx, 14), 4294953856), GPR_U32(ctx, 15));
    // 0x18c7f8: 0x340fffff  ori         $t7, $zero, 0xFFFF
    ctx->pc = 0x18c7f8u;
    SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
label_18c7fc:
    // 0x18c7fc: 0x164f0008  bne         $s2, $t7, . + 4 + (0x8 << 2)
    ctx->pc = 0x18C7FCu;
    {
        const bool branch_taken_0x18c7fc = (GPR_U64(ctx, 18) != GPR_U64(ctx, 15));
        ctx->pc = 0x18C800u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C7FCu;
        // 0x18c800: 0x340fffff  ori         $t7, $zero, 0xFFFF (Delay Slot)
        SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c7fc) {
            ctx->pc = 0x18C820u;
            goto label_18c820;
        }
    }
    ctx->pc = 0x18C804u;
    // 0x18c804: 0x16600006  bnez        $s3, . + 4 + (0x6 << 2)
    ctx->pc = 0x18C804u;
    {
        const bool branch_taken_0x18c804 = (GPR_U64(ctx, 19) != GPR_U64(ctx, 0));
        if (branch_taken_0x18c804) {
            ctx->pc = 0x18C820u;
            goto label_18c820;
        }
    }
    ctx->pc = 0x18C80Cu;
    // 0x18c80c: 0x3c040027  lui         $a0, 0x27
    ctx->pc = 0x18c80cu;
    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)39 << 16));
    // 0x18c810: 0x2484c980  addiu       $a0, $a0, -0x3680
    ctx->pc = 0x18c810u;
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 4), 4294953344));
    // 0x18c814: 0xc08806a  jal         func_2201A8
    ctx->pc = 0x18C814u;
    SET_GPR_U32(ctx, 31, 0x18C81Cu);
    ctx->pc = 0x18C818u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x18C814u;
    // 0x18c818: 0x24840068  addiu       $a0, $a0, 0x68 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 4), 104));
    ctx->in_delay_slot = false;
    ctx->pc = 0x2201A8u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x2201A8u, 0x18C814u, 0x18C81Cu, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        std::cerr << "[DIAG:18c018-callfail] target=0x2201A8u src=0x18C814u fallthrough=0x18C81Cu sp=0x" << std::hex << GPR_U32(ctx, 29) << " pc=0x" << ctx->pc << std::dec << std::endl;
        return;
    }
    std::cerr << "[DIAG:18c018-ok] after_fallthrough=0x18C81Cu sp=0x" << std::hex << GPR_U32(ctx, 29) << std::dec << std::endl;
    ctx->pc = 0x18C81Cu;
label_18c81c:
    // 0x18c81c: 0x340fffff  ori         $t7, $zero, 0xFFFF
    ctx->pc = 0x18c81cu;
    SET_GPR_U64(ctx, 15, GPR_U64(ctx, 0) | (uint64_t)(uint16_t)65535);
label_18c820:
    // 0x18c820: 0x164f000d  bne         $s2, $t7, . + 4 + (0xD << 2)
    ctx->pc = 0x18C820u;
    {
        const bool branch_taken_0x18c820 = (GPR_U64(ctx, 18) != GPR_U64(ctx, 15));
        ctx->pc = 0x18C824u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C820u;
        // 0x18c824: 0xdfb00000  ld          $s0, 0x0($sp) (Delay Slot)
        SET_GPR_U64(ctx, 16, READ64(ADD32(GPR_U32(ctx, 29), 0)));
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c820) {
            ctx->pc = 0x18C858u;
            goto label_18c858;
        }
    }
    ctx->pc = 0x18C828u;
    // 0x18c828: 0x1660000c  bnez        $s3, . + 4 + (0xC << 2)
    ctx->pc = 0x18C828u;
    {
        const bool branch_taken_0x18c828 = (GPR_U64(ctx, 19) != GPR_U64(ctx, 0));
        ctx->pc = 0x18C82Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C828u;
        // 0x18c82c: 0xdfb10008  ld          $s1, 0x8($sp) (Delay Slot)
        SET_GPR_U64(ctx, 17, READ64(ADD32(GPR_U32(ctx, 29), 8)));
        ctx->in_delay_slot = false;
        if (branch_taken_0x18c828) {
            ctx->pc = 0x18C85Cu;
            goto label_18c85c;
        }
    }
    ctx->pc = 0x18C830u;
    // 0x18c830: 0x3c110027  lui         $s1, 0x27
    ctx->pc = 0x18c830u;
    SET_GPR_S32(ctx, 17, (int32_t)((uint32_t)39 << 16));
    // 0x18c834: 0x2630c540  addiu       $s0, $s1, -0x3AC0
    ctx->pc = 0x18c834u;
    SET_GPR_S32(ctx, 16, (int32_t)ADD32(GPR_U32(ctx, 17), 4294952256));
    // 0x18c838: 0xc088062  jal         func_220188
    ctx->pc = 0x18C838u;
    SET_GPR_U32(ctx, 31, 0x18C840u);
    ctx->pc = 0x18C83Cu;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x18C838u;
    // 0x18c83c: 0x26040200  addiu       $a0, $s0, 0x200 (Delay Slot)
    SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 16), 512));
    ctx->in_delay_slot = false;
    ctx->pc = 0x220188u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x220188u, 0x18C838u, 0x18C840u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        std::cerr << "[DIAG:18c018-callfail] target=0x220188u src=0x18C838u fallthrough=0x18C840u sp=0x" << std::hex << GPR_U32(ctx, 29) << " pc=0x" << ctx->pc << std::dec << std::endl;
        return;
    }
    std::cerr << "[DIAG:18c018-ok] after_fallthrough=0x18C840u sp=0x" << std::hex << GPR_U32(ctx, 29) << std::dec << std::endl;
    ctx->pc = 0x18C840u;
label_18c840:
    // 0x18c840: 0x3c0f002d  lui         $t7, 0x2D
    ctx->pc = 0x18c840u;
    SET_GPR_S32(ctx, 15, (int32_t)((uint32_t)45 << 16));
    // 0x18c844: 0x200202d  daddu       $a0, $s0, $zero
    ctx->pc = 0x18c844u;
    SET_GPR_U64(ctx, 4, (uint64_t)GPR_U64(ctx, 16) + (uint64_t)GPR_U64(ctx, 0));
    // 0x18c848: 0x25eff6e0  addiu       $t7, $t7, -0x920
    ctx->pc = 0x18c848u;
    SET_GPR_S32(ctx, 15, (int32_t)ADD32(GPR_U32(ctx, 15), 4294964960));
    // 0x18c84c: 0xc088056  jal         func_220158
    ctx->pc = 0x18C84Cu;
    SET_GPR_U32(ctx, 31, 0x18C854u);
    ctx->pc = 0x18C850u;
    ctx->in_delay_slot = true;
    ctx->branch_pc = 0x18C84Cu;
    // 0x18c850: 0xae2fc540  sw          $t7, -0x3AC0($s1) (Delay Slot)
    WRITE32(ADD32(GPR_U32(ctx, 17), 4294952256), GPR_U32(ctx, 15));
    ctx->in_delay_slot = false;
    ctx->pc = 0x220158u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x220158u, 0x18C84Cu, 0x18C854u, PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        std::cerr << "[DIAG:18c018-callfail] target=0x220158u src=0x18C84Cu fallthrough=0x18C854u sp=0x" << std::hex << GPR_U32(ctx, 29) << " pc=0x" << ctx->pc << std::dec << std::endl;
        return;
    }
    std::cerr << "[DIAG:18c018-ok] after_fallthrough=0x18C854u sp=0x" << std::hex << GPR_U32(ctx, 29) << std::dec << std::endl;
    ctx->pc = 0x18C854u;
label_18c854:
    // 0x18c854: 0xdfb00000  ld          $s0, 0x0($sp)
    ctx->pc = 0x18c854u;
    SET_GPR_U64(ctx, 16, READ64(ADD32(GPR_U32(ctx, 29), 0)));
label_18c858:
    // 0x18c858: 0xdfb10008  ld          $s1, 0x8($sp)
    ctx->pc = 0x18c858u;
    SET_GPR_U64(ctx, 17, READ64(ADD32(GPR_U32(ctx, 29), 8)));
label_18c85c:
    // 0x18c85c: 0xdfb20010  ld          $s2, 0x10($sp)
    ctx->pc = 0x18c85cu;
    SET_GPR_U64(ctx, 18, READ64(ADD32(GPR_U32(ctx, 29), 16)));
    // 0x18c860: 0xdfb30018  ld          $s3, 0x18($sp)
    ctx->pc = 0x18c860u;
    SET_GPR_U64(ctx, 19, READ64(ADD32(GPR_U32(ctx, 29), 24)));
    // 0x18c864: 0xdfb40020  ld          $s4, 0x20($sp)
    ctx->pc = 0x18c864u;
    SET_GPR_U64(ctx, 20, READ64(ADD32(GPR_U32(ctx, 29), 32)));
    // 0x18c868: 0xdfbf0028  ld          $ra, 0x28($sp)
    ctx->pc = 0x18c868u;
    std::cerr << "[DIAG:18c018-epilogue] sp=0x" << std::hex << GPR_U32(ctx, 29) << std::dec << std::endl;
    SET_GPR_U64(ctx, 31, READ64(ADD32(GPR_U32(ctx, 29), 40)));
    std::cerr << "[DIAG:18c018-epilogue] loaded ra=0x" << std::hex << GPR_U32(ctx, 31) << std::dec << std::endl;
    // 0x18c86c: 0x3e00008  jr          $ra
    ctx->pc = 0x18C86Cu;
    {
        const uint32_t jumpTarget = GPR_U32(ctx, 31);
        ctx->pc = 0x18C870u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x18C86Cu;
        // 0x18c870: 0x27bd0030  addiu       $sp, $sp, 0x30 (Delay Slot)
        // NOTE: this instruction is OUTSIDE the function's declared boundary
        // (0x18c018-0x18c870, exclusive) per the Ghidra/CSV export, so
        // ps2_recomp never generated it -- confirmed by reading the raw ELF
        // bytes at 0x18c870 (0x27bd0030 = addiu sp,sp,0x30, immediately
        // followed by a nop padding word at 0x18c874). Without it, every
        // call into this function permanently leaked 0x30 bytes of guest
        // stack, corrupting sub_001C26E8's (the global-constructor runner)
        // own saved $ra once a resumed call chain returned here. Hand-added
        // to restore correct behavior without needing a full pipeline regen.
        SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 48));
        ctx->in_delay_slot = false;
        ctx->pc = jumpTarget;
        #if defined(PS2X_STRICT_RETURN_DIAGNOSTICS) && PS2X_STRICT_RETURN_DIAGNOSTICS
        (void)runtime->dispatchGuestBranch(rdram, ctx, jumpTarget, 0x18C86Cu, 0u, PS2Runtime::GuestBranchKind::Return, "JR $ra");
        return;
        #else
        ctx->pc = jumpTarget;
        return;
        #endif
    }
    ctx->pc = 0x18C874u;
}
