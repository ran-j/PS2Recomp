#include "runtime/ps2_ee_interpreter.h"

#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"

#include <cmath>
#include <cstring>
#include <iostream>
#include <unordered_set>

// Instruction semantics mirror what ps2xRecomp emits for the same opcode, so
// interpreted and recompiled code behave identically (register extension
// rules, HI/LO handling, PS2 FPU helpers, MMIO-aware memory access).

namespace
{
    // How often (in instructions) the interpreter offers the scheduler a
    // checkpoint; also the cycle count charged for it.
    constexpr uint32_t kCheckpointInterval = 32u;

    struct Fields
    {
        uint32_t word;
        uint32_t rs, rt, rd, sa, funct;
        int32_t simm;
        uint32_t uimm;
    };

    Fields decode(uint32_t word)
    {
        return {word, (word >> 21) & 31u, (word >> 16) & 31u, (word >> 11) & 31u, (word >> 6) & 31u, word & 63u,
                static_cast<int32_t>(static_cast<int16_t>(word & 0xFFFFu)), word & 0xFFFFu};
    }

    void reportUnimplemented(uint32_t pc, uint32_t word, const char *what)
    {
        static std::unordered_set<uint32_t> reported;
        if (!reported.insert(word & 0xFC1F07FFu).second)
            return;
        std::cerr << "[ee-interp] unimplemented " << what << " 0x" << std::hex << word << " at pc=0x" << pc
                  << std::dec << " (treated as NOP)" << std::endl;
    }

    enum class Flow
    {
        Next,      // fall through to pc + 4
        Branch,    // control transfer with a delay slot (handled by caller)
        Stop,      // leave the interpreter (ctx->pc already set)
    };

    bool isControlTransfer(uint32_t word)
    {
        const uint32_t op = word >> 26;
        if (op == 0x00)
        {
            const uint32_t funct = word & 63u;
            return funct == 0x08 || funct == 0x09; // JR, JALR
        }
        if (op == 0x01)
        {
            const uint32_t rt = (word >> 16) & 31u;
            return rt <= 0x03 || (rt >= 0x10 && rt <= 0x13); // BLTZ/BGEZ(L)(AL)
        }
        if (op == 0x02 || op == 0x03)
            return true;                                     // J, JAL
        if ((op >= 0x04 && op <= 0x07) || (op >= 0x14 && op <= 0x17))
            return true;                                     // BEQ..BGTZ(L)
        if (op == 0x11 && ((word >> 21) & 31u) == 0x08)
            return true;                                     // BC1x
        return false;
    }

    // Executes one instruction that is not a control transfer.
    Flow executeSimple(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, uint32_t pc, const Fields &f)
    {
        const uint32_t op = f.word >> 26;
        switch (op)
        {
        case 0x00: // SPECIAL
            switch (f.funct)
            {
            case 0x00: SET_GPR_S32(ctx, f.rd, (int32_t)SLL32(GPR_U32(ctx, f.rt), f.sa)); return Flow::Next;
            case 0x02: SET_GPR_S32(ctx, f.rd, (int32_t)SRL32(GPR_U32(ctx, f.rt), f.sa)); return Flow::Next;
            case 0x03: SET_GPR_S32(ctx, f.rd, SRA32(GPR_S32(ctx, f.rt), f.sa)); return Flow::Next;
            case 0x04: SET_GPR_S32(ctx, f.rd, (int32_t)SLL32(GPR_U32(ctx, f.rt), GPR_U32(ctx, f.rs) & 0x1F)); return Flow::Next;
            case 0x06: SET_GPR_S32(ctx, f.rd, (int32_t)SRL32(GPR_U32(ctx, f.rt), GPR_U32(ctx, f.rs) & 0x1F)); return Flow::Next;
            case 0x07: SET_GPR_S32(ctx, f.rd, SRA32(GPR_S32(ctx, f.rt), GPR_U32(ctx, f.rs) & 0x1F)); return Flow::Next;
            case 0x0A: if (GPR_U64(ctx, f.rt) == 0) SET_GPR_VEC(ctx, f.rd, GPR_VEC(ctx, f.rs)); return Flow::Next; // MOVZ
            case 0x0B: if (GPR_U64(ctx, f.rt) != 0) SET_GPR_VEC(ctx, f.rd, GPR_VEC(ctx, f.rs)); return Flow::Next; // MOVN
            case 0x0C: // SYSCALL (generated code continues with the next instruction)
                ctx->pc = pc + 4u;
                runtime->handleSyscall(rdram, ctx, (f.word >> 6) & 0xFFFFFu);
                return Flow::Next;
            case 0x0D: runtime->handleBreak(rdram, ctx); return Flow::Next;
            case 0x0F: return Flow::Next; // SYNC
            case 0x10: SET_GPR_U64(ctx, f.rd, ctx->hi); return Flow::Next;
            case 0x11: ctx->hi = GPR_U64(ctx, f.rs); return Flow::Next;
            case 0x12: SET_GPR_U64(ctx, f.rd, ctx->lo); return Flow::Next;
            case 0x13: ctx->lo = GPR_U64(ctx, f.rs); return Flow::Next;
            case 0x14: SET_GPR_U64(ctx, f.rd, GPR_U64(ctx, f.rt) << (GPR_U32(ctx, f.rs) & 0x3F)); return Flow::Next;
            case 0x16: SET_GPR_U64(ctx, f.rd, GPR_U64(ctx, f.rt) >> (GPR_U32(ctx, f.rs) & 0x3F)); return Flow::Next;
            case 0x17: SET_GPR_S64(ctx, f.rd, GPR_S64(ctx, f.rt) >> (GPR_U32(ctx, f.rs) & 0x3F)); return Flow::Next;
            case 0x18: // MULT
            {
                const int64_t result = (int64_t)GPR_S32(ctx, f.rs) * (int64_t)GPR_S32(ctx, f.rt);
                ctx->lo = (uint64_t)(int64_t)(int32_t)result;
                ctx->hi = (uint64_t)(int64_t)(int32_t)(result >> 32);
                SET_GPR_S32(ctx, f.rd, (int32_t)result);
                return Flow::Next;
            }
            case 0x19: // MULTU
            {
                const uint64_t result = (uint64_t)GPR_U32(ctx, f.rs) * (uint64_t)GPR_U32(ctx, f.rt);
                ctx->lo = (uint64_t)(int64_t)(int32_t)result;
                ctx->hi = (uint64_t)(int64_t)(int32_t)(result >> 32);
                SET_GPR_S32(ctx, f.rd, (int32_t)result);
                return Flow::Next;
            }
            case 0x1A: // DIV
            {
                const int32_t divisor = GPR_S32(ctx, f.rt);
                const int32_t dividend = GPR_S32(ctx, f.rs);
                if (divisor != 0)
                {
                    if (divisor == -1 && dividend == INT32_MIN)
                    {
                        ctx->lo = (uint64_t)(int64_t)INT32_MIN;
                        ctx->hi = 0;
                    }
                    else
                    {
                        ctx->lo = (uint64_t)(int64_t)(dividend / divisor);
                        ctx->hi = (uint64_t)(int64_t)(dividend % divisor);
                    }
                }
                else
                {
                    ctx->lo = (dividend < 0) ? 1ull : 0xFFFFFFFFFFFFFFFFull;
                    ctx->hi = (uint64_t)(int64_t)dividend;
                }
                return Flow::Next;
            }
            case 0x1B: // DIVU
            {
                const uint32_t divisor = GPR_U32(ctx, f.rt);
                if (divisor != 0)
                {
                    ctx->lo = (uint64_t)(int64_t)(int32_t)(GPR_U32(ctx, f.rs) / divisor);
                    ctx->hi = (uint64_t)(int64_t)(int32_t)(GPR_U32(ctx, f.rs) % divisor);
                }
                else
                {
                    ctx->lo = 0xFFFFFFFFFFFFFFFFull;
                    ctx->hi = (uint64_t)(int64_t)(int32_t)GPR_U32(ctx, f.rs);
                }
                return Flow::Next;
            }
            case 0x20: // ADD
            {
                const int64_t result = (int64_t)GPR_S32(ctx, f.rs) + (int64_t)GPR_S32(ctx, f.rt);
                if (result > INT32_MAX || result < INT32_MIN)
                    runtime->SignalException(ctx, EXCEPTION_INTEGER_OVERFLOW);
                else
                    SET_GPR_S32(ctx, f.rd, (int32_t)result);
                return Flow::Next;
            }
            case 0x21: SET_GPR_S32(ctx, f.rd, (int32_t)ADD32(GPR_U32(ctx, f.rs), GPR_U32(ctx, f.rt))); return Flow::Next;
            case 0x22: // SUB
            {
                uint32_t tmp;
                bool ov;
                SUB32_OV(GPR_U32(ctx, f.rs), GPR_U32(ctx, f.rt), tmp, ov);
                if (ov)
                    runtime->SignalException(ctx, EXCEPTION_INTEGER_OVERFLOW);
                else
                    SET_GPR_S32(ctx, f.rd, (int32_t)tmp);
                return Flow::Next;
            }
            case 0x23: SET_GPR_S32(ctx, f.rd, (int32_t)SUB32(GPR_U32(ctx, f.rs), GPR_U32(ctx, f.rt))); return Flow::Next;
            case 0x24: SET_GPR_U64(ctx, f.rd, GPR_U64(ctx, f.rs) & GPR_U64(ctx, f.rt)); return Flow::Next;
            case 0x25: SET_GPR_U64(ctx, f.rd, GPR_U64(ctx, f.rs) | GPR_U64(ctx, f.rt)); return Flow::Next;
            case 0x26: SET_GPR_U64(ctx, f.rd, GPR_U64(ctx, f.rs) ^ GPR_U64(ctx, f.rt)); return Flow::Next;
            case 0x27: SET_GPR_U64(ctx, f.rd, ~(GPR_U64(ctx, f.rs) | GPR_U64(ctx, f.rt))); return Flow::Next;
            case 0x28: SET_GPR_U32(ctx, f.rd, ctx->sa); return Flow::Next;                 // MFSA
            case 0x29: ctx->sa = GPR_U32(ctx, f.rs) & 0x7F; return Flow::Next;             // MTSA
            case 0x2A: SET_GPR_U64(ctx, f.rd, ((int64_t)GPR_S64(ctx, f.rs) < (int64_t)GPR_S64(ctx, f.rt)) ? 1 : 0); return Flow::Next;
            case 0x2B: SET_GPR_U64(ctx, f.rd, ((uint64_t)GPR_U64(ctx, f.rs) < (uint64_t)GPR_U64(ctx, f.rt)) ? 1 : 0); return Flow::Next;
            case 0x2C: // DADD
            {
                const int64_t a = GPR_S64(ctx, f.rs), b = GPR_S64(ctx, f.rt), r = (int64_t)((uint64_t)a + (uint64_t)b);
                if (((a ^ b) >= 0) && ((a ^ r) < 0))
                    runtime->SignalException(ctx, EXCEPTION_INTEGER_OVERFLOW);
                else
                    SET_GPR_S64(ctx, f.rd, r);
                return Flow::Next;
            }
            case 0x2D: SET_GPR_U64(ctx, f.rd, (uint64_t)GPR_U64(ctx, f.rs) + (uint64_t)GPR_U64(ctx, f.rt)); return Flow::Next;
            case 0x2E: // DSUB
            {
                const int64_t a = GPR_S64(ctx, f.rs), b = GPR_S64(ctx, f.rt), r = (int64_t)((uint64_t)a - (uint64_t)b);
                if (((a ^ b) < 0) && ((a ^ r) < 0))
                    runtime->SignalException(ctx, EXCEPTION_INTEGER_OVERFLOW);
                else
                    SET_GPR_S64(ctx, f.rd, r);
                return Flow::Next;
            }
            case 0x2F: SET_GPR_U64(ctx, f.rd, GPR_U64(ctx, f.rs) - GPR_U64(ctx, f.rt)); return Flow::Next;
            case 0x30: if (GPR_S64(ctx, f.rs) >= GPR_S64(ctx, f.rt)) runtime->handleTrap(rdram, ctx); return Flow::Next;
            case 0x31: if (GPR_U64(ctx, f.rs) >= GPR_U64(ctx, f.rt)) runtime->handleTrap(rdram, ctx); return Flow::Next;
            case 0x32: if (GPR_S64(ctx, f.rs) < GPR_S64(ctx, f.rt)) runtime->handleTrap(rdram, ctx); return Flow::Next;
            case 0x33: if (GPR_U64(ctx, f.rs) < GPR_U64(ctx, f.rt)) runtime->handleTrap(rdram, ctx); return Flow::Next;
            case 0x34: if (GPR_U64(ctx, f.rs) == GPR_U64(ctx, f.rt)) runtime->handleTrap(rdram, ctx); return Flow::Next;
            case 0x36: if (GPR_U64(ctx, f.rs) != GPR_U64(ctx, f.rt)) runtime->handleTrap(rdram, ctx); return Flow::Next;
            case 0x38: SET_GPR_U64(ctx, f.rd, GPR_U64(ctx, f.rt) << f.sa); return Flow::Next;
            case 0x3A: SET_GPR_U64(ctx, f.rd, GPR_U64(ctx, f.rt) >> f.sa); return Flow::Next;
            case 0x3B: SET_GPR_S64(ctx, f.rd, GPR_S64(ctx, f.rt) >> f.sa); return Flow::Next;
            case 0x3C: SET_GPR_U64(ctx, f.rd, GPR_U64(ctx, f.rt) << (32 + f.sa)); return Flow::Next;
            case 0x3E: SET_GPR_U64(ctx, f.rd, GPR_U64(ctx, f.rt) >> (32 + f.sa)); return Flow::Next;
            case 0x3F: SET_GPR_S64(ctx, f.rd, GPR_S64(ctx, f.rt) >> (32 + f.sa)); return Flow::Next;
            default:
                reportUnimplemented(pc, f.word, "SPECIAL");
                return Flow::Next;
            }

        case 0x08: // ADDI
        {
            const int64_t result = (int64_t)GPR_S32(ctx, f.rs) + (int64_t)f.simm;
            if (result > INT32_MAX || result < INT32_MIN)
                runtime->SignalException(ctx, EXCEPTION_INTEGER_OVERFLOW);
            else
                SET_GPR_S32(ctx, f.rt, (int32_t)result);
            return Flow::Next;
        }
        case 0x09: SET_GPR_S32(ctx, f.rt, (int32_t)ADD32(GPR_U32(ctx, f.rs), f.simm)); return Flow::Next;
        case 0x0A: SET_GPR_U64(ctx, f.rt, ((int64_t)GPR_S64(ctx, f.rs) < (int64_t)(int32_t)f.simm) ? 1 : 0); return Flow::Next;
        case 0x0B: SET_GPR_U64(ctx, f.rt, ((uint64_t)GPR_U64(ctx, f.rs) < (uint64_t)(int64_t)(int32_t)f.simm) ? 1 : 0); return Flow::Next;
        case 0x0C: SET_GPR_U64(ctx, f.rt, GPR_U64(ctx, f.rs) & (uint64_t)f.uimm); return Flow::Next;
        case 0x0D: SET_GPR_U64(ctx, f.rt, GPR_U64(ctx, f.rs) | (uint64_t)f.uimm); return Flow::Next;
        case 0x0E: SET_GPR_U64(ctx, f.rt, GPR_U64(ctx, f.rs) ^ (uint64_t)f.uimm); return Flow::Next;
        case 0x0F: SET_GPR_S32(ctx, f.rt, (int32_t)(f.uimm << 16)); return Flow::Next;

        case 0x10: // COP0
            if (f.rs == 0x00) // MFC0
            {
                if (f.rd == 9)
                    SET_GPR_S32(ctx, f.rt, (int32_t)runtime->readCop0Count(ctx));
                else if (f.rd == 12)
                    SET_GPR_S32(ctx, f.rt, (int32_t)ctx->cop0_status);
                else if (f.rd == 13)
                    SET_GPR_S32(ctx, f.rt, (int32_t)ctx->cop0_cause);
                else if (f.rd == 14)
                    SET_GPR_S32(ctx, f.rt, (int32_t)ctx->cop0_epc);
                else
                    reportUnimplemented(pc, f.word, "MFC0");
                return Flow::Next;
            }
            if (f.rs == 0x04) // MTC0
            {
                if (f.rd == 9)
                    runtime->writeCop0Count(ctx, GPR_U32(ctx, f.rt));
                else if (f.rd == 12)
                    ctx->cop0_status = GPR_U32(ctx, f.rt) & 0xFF57FFFF;
                else
                    reportUnimplemented(pc, f.word, "MTC0");
                return Flow::Next;
            }
            if (f.rs == 0x10 && f.funct == 0x38) // EI
            {
                ctx->cop0_status |= 0x10000;
                return Flow::Next;
            }
            if (f.rs == 0x10 && f.funct == 0x39) // DI
            {
                ctx->cop0_status &= ~0x10000u;
                return Flow::Next;
            }
            reportUnimplemented(pc, f.word, "COP0");
            return Flow::Next;

        case 0x11: // COP1
            switch (f.rs)
            {
            case 0x00: { uint32_t bits; std::memcpy(&bits, &ctx->f[f.rd], sizeof(bits)); SET_GPR_U32(ctx, f.rt, bits); return Flow::Next; }
            case 0x02: SET_GPR_U32(ctx, f.rt, f.rd == 31 ? ctx->fcr31 : 0u); return Flow::Next; // CFC1
            case 0x04: { uint32_t bits = GPR_U32(ctx, f.rt); std::memcpy(&ctx->f[f.rd], &bits, sizeof(bits)); return Flow::Next; }
            case 0x06: if (f.rd == 31) ctx->fcr31 = GPR_U32(ctx, f.rt) & 0x0183FFFF; return Flow::Next; // CTC1
            case 0x10: // S format: fs = rd field, ft = rt field, fd = sa field
            {
                const uint32_t fs = f.rd, ft = f.rt, fd = f.sa;
                auto compare = [&](bool result) { ctx->fcr31 = result ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000u); };
                switch (f.funct)
                {
                case 0x00: ctx->f[fd] = FPU_ADD_S(ctx->f[fs], ctx->f[ft]); return Flow::Next;
                case 0x01: ctx->f[fd] = FPU_SUB_S(ctx->f[fs], ctx->f[ft]); return Flow::Next;
                case 0x02: ctx->f[fd] = FPU_MUL_S(ctx->f[fs], ctx->f[ft]); return Flow::Next;
                case 0x03:
                    if (ctx->f[ft] == 0.0f)
                        ctx->fcr31 |= 0x100000;
                    ctx->f[fd] = FPU_DIV_S(ctx->f[fs], ctx->f[ft]);
                    return Flow::Next;
                case 0x04: ctx->f[fd] = FPU_SQRT_S(ctx->f[ft]); return Flow::Next;
                case 0x05: ctx->f[fd] = FPU_ABS_S(ctx->f[fs]); return Flow::Next;
                case 0x06: ctx->f[fd] = FPU_MOV_S(ctx->f[fs]); return Flow::Next;
                case 0x07: ctx->f[fd] = FPU_NEG_S(ctx->f[fs]); return Flow::Next;
                case 0x16: ctx->f[fd] = FPU_RSQRT_S(ctx->f[fs], ctx->f[ft]); return Flow::Next;
                case 0x18: FPU_SET_ACC(ctx, FPU_ADD_S(ctx->f[fs], ctx->f[ft])); return Flow::Next;
                case 0x19: FPU_SET_ACC(ctx, FPU_SUB_S(ctx->f[fs], ctx->f[ft])); return Flow::Next;
                case 0x1A: FPU_SET_ACC(ctx, FPU_MUL_S(ctx->f[fs], ctx->f[ft])); return Flow::Next;
                case 0x1C: ctx->f[fd] = FPU_ADD_S(ctx->f_acc, FPU_MUL_S(ctx->f[fs], ctx->f[ft])); return Flow::Next;
                case 0x1D: ctx->f[fd] = FPU_SUB_S(ctx->f_acc, FPU_MUL_S(ctx->f[fs], ctx->f[ft])); return Flow::Next;
                case 0x1E: FPU_SET_ACC(ctx, FPU_ADD_S(ctx->f_acc, FPU_MUL_S(ctx->f[fs], ctx->f[ft]))); return Flow::Next;
                case 0x1F: FPU_SET_ACC(ctx, FPU_SUB_S(ctx->f_acc, FPU_MUL_S(ctx->f[fs], ctx->f[ft]))); return Flow::Next;
                case 0x24: { int32_t tmp = FPU_CVT_W_S(ctx->f[fs]); std::memcpy(&ctx->f[fd], &tmp, sizeof(tmp)); return Flow::Next; }
                case 0x28: ctx->f[fd] = std::max(ctx->f[fs], ctx->f[ft]); return Flow::Next;
                case 0x29: ctx->f[fd] = std::min(ctx->f[fs], ctx->f[ft]); return Flow::Next;
                case 0x30: ctx->fcr31 &= ~0x800000u; return Flow::Next;
                case 0x32: compare(FPU_C_EQ_S(ctx->f[fs], ctx->f[ft])); return Flow::Next;
                case 0x34: compare(FPU_C_OLT_S(ctx->f[fs], ctx->f[ft])); return Flow::Next;
                case 0x36: compare(FPU_C_OLE_S(ctx->f[fs], ctx->f[ft])); return Flow::Next;
                case 0x3C: compare(FPU_C_LT_S(ctx->f[fs], ctx->f[ft])); return Flow::Next;
                case 0x3E: compare(FPU_C_LE_S(ctx->f[fs], ctx->f[ft])); return Flow::Next;
                default:
                    reportUnimplemented(pc, f.word, "COP1.S");
                    return Flow::Next;
                }
            }
            case 0x14: // W format
                if (f.funct == 0x20)
                {
                    int32_t tmp;
                    std::memcpy(&tmp, &ctx->f[f.rd], sizeof(tmp));
                    ctx->f[f.sa] = FPU_CVT_S_W(tmp);
                    return Flow::Next;
                }
                reportUnimplemented(pc, f.word, "COP1.W");
                return Flow::Next;
            default:
                reportUnimplemented(pc, f.word, "COP1");
                return Flow::Next;
            }

        case 0x18: // DADDI
        {
            const int64_t a = GPR_S64(ctx, f.rs), b = f.simm, r = (int64_t)((uint64_t)a + (uint64_t)b);
            if (((a ^ b) >= 0) && ((a ^ r) < 0))
                runtime->SignalException(ctx, EXCEPTION_INTEGER_OVERFLOW);
            else
                SET_GPR_S64(ctx, f.rt, r);
            return Flow::Next;
        }
        case 0x19: SET_GPR_S64(ctx, f.rt, GPR_S64(ctx, f.rs) + (int64_t)f.simm); return Flow::Next; // DADDIU

        case 0x1C: // MMI (the subset compilers emit for scalar code)
            switch (f.funct)
            {
            case 0x10: SET_GPR_U64(ctx, f.rd, ctx->hi1); return Flow::Next;
            case 0x11: ctx->hi1 = GPR_U64(ctx, f.rs); return Flow::Next;
            case 0x12: SET_GPR_U64(ctx, f.rd, ctx->lo1); return Flow::Next;
            case 0x13: ctx->lo1 = GPR_U64(ctx, f.rs); return Flow::Next;
            case 0x18: // MULT1
            {
                const int64_t result = (int64_t)GPR_S32(ctx, f.rs) * (int64_t)GPR_S32(ctx, f.rt);
                ctx->lo1 = (uint64_t)(int64_t)(int32_t)result;
                ctx->hi1 = (uint64_t)(int64_t)(int32_t)(result >> 32);
                SET_GPR_S32(ctx, f.rd, (int32_t)result);
                return Flow::Next;
            }
            case 0x19: // MULTU1
            {
                const uint64_t result = (uint64_t)GPR_U32(ctx, f.rs) * (uint64_t)GPR_U32(ctx, f.rt);
                ctx->lo1 = (uint64_t)(int64_t)(int32_t)result;
                ctx->hi1 = (uint64_t)(int64_t)(int32_t)(result >> 32);
                SET_GPR_S32(ctx, f.rd, (int32_t)result);
                return Flow::Next;
            }
            case 0x00: // MADD
            {
                const uint64_t acc = Ps2HiLoToU64(ctx->hi, ctx->lo);
                const int64_t result = (int64_t)acc + (int64_t)GPR_S32(ctx, f.rs) * (int64_t)GPR_S32(ctx, f.rt);
                ctx->lo = Ps2SignExt32ToU64((uint32_t)result);
                ctx->hi = Ps2SignExt32ToU64((uint32_t)(result >> 32));
                SET_GPR_S32(ctx, f.rd, (int32_t)result);
                return Flow::Next;
            }
            case 0x01: // MADDU
            {
                const uint64_t acc = Ps2HiLoToU64(ctx->hi, ctx->lo);
                const uint64_t result = acc + (uint64_t)GPR_U32(ctx, f.rs) * (uint64_t)GPR_U32(ctx, f.rt);
                ctx->lo = Ps2SignExt32ToU64((uint32_t)result);
                ctx->hi = Ps2SignExt32ToU64((uint32_t)(result >> 32));
                SET_GPR_S32(ctx, f.rd, (int32_t)result);
                return Flow::Next;
            }
            case 0x09: // MMI2
                if (f.sa == 0x0E) { SET_GPR_VEC(ctx, f.rd, PS2_PCPYLD(GPR_VEC(ctx, f.rs), GPR_VEC(ctx, f.rt))); return Flow::Next; }
                if (f.sa == 0x12) { SET_GPR_VEC(ctx, f.rd, PS2_PAND(GPR_VEC(ctx, f.rs), GPR_VEC(ctx, f.rt))); return Flow::Next; }
                if (f.sa == 0x13) { SET_GPR_VEC(ctx, f.rd, PS2_PXOR(GPR_VEC(ctx, f.rs), GPR_VEC(ctx, f.rt))); return Flow::Next; }
                break;
            case 0x29: // MMI3
                if (f.sa == 0x0E) { SET_GPR_VEC(ctx, f.rd, _mm_unpackhi_epi64(GPR_VEC(ctx, f.rs), GPR_VEC(ctx, f.rt))); return Flow::Next; } // PCPYUD
                if (f.sa == 0x12) { SET_GPR_VEC(ctx, f.rd, PS2_POR(GPR_VEC(ctx, f.rs), GPR_VEC(ctx, f.rt))); return Flow::Next; }
                if (f.sa == 0x13) { SET_GPR_VEC(ctx, f.rd, PS2_PNOR(GPR_VEC(ctx, f.rs), GPR_VEC(ctx, f.rt))); return Flow::Next; }
                break;
            default:
                break;
            }
            reportUnimplemented(pc, f.word, "MMI");
            return Flow::Next;

        case 0x1A: // LDL
        {
            const uint32_t addr = ADD32(GPR_U32(ctx, f.rs), f.simm), aligned = addr & ~7u, offset = addr & 7u;
            const uint64_t mem = READ64(aligned);
            const uint32_t shift = (7u - offset) << 3;
            const uint64_t keepMask = (shift == 0) ? 0ull : ((1ull << shift) - 1ull);
            SET_GPR_U64(ctx, f.rt, (GPR_U64(ctx, f.rt) & keepMask) | (mem << shift));
            return Flow::Next;
        }
        case 0x1B: // LDR
        {
            const uint32_t addr = ADD32(GPR_U32(ctx, f.rs), f.simm), aligned = addr & ~7u, offset = addr & 7u;
            const uint64_t mem = READ64(aligned);
            const uint32_t shift = offset << 3;
            const uint64_t keepMask = (offset == 0) ? 0ull : (0xFFFFFFFFFFFFFFFFull << ((8u - offset) << 3));
            SET_GPR_U64(ctx, f.rt, (GPR_U64(ctx, f.rt) & keepMask) | (mem >> shift));
            return Flow::Next;
        }
        case 0x1E: SET_GPR_VEC(ctx, f.rt, READ128(ADD32(GPR_U32(ctx, f.rs), f.simm) & ~0xFu)); return Flow::Next; // LQ
        case 0x1F: WRITE128(ADD32(GPR_U32(ctx, f.rs), f.simm) & ~0xFu, GPR_VEC(ctx, f.rt)); return Flow::Next;   // SQ
        case 0x20: SET_GPR_S32(ctx, f.rt, (int32_t)(int8_t)READ8(ADD32(GPR_U32(ctx, f.rs), f.simm))); return Flow::Next;
        case 0x21: SET_GPR_S32(ctx, f.rt, (int32_t)(int16_t)READ16(ADD32(GPR_U32(ctx, f.rs), f.simm))); return Flow::Next;
        case 0x22: // LWL
        {
            const uint32_t addr = ADD32(GPR_U32(ctx, f.rs), f.simm), aligned = addr & ~3u, offset = addr & 3u;
            const uint32_t mem = READ32(aligned);
            const uint32_t shift = (3u - offset) << 3;
            const uint32_t keepMask = (shift == 0) ? 0u : ((1u << shift) - 1u);
            SET_GPR_S32(ctx, f.rt, (int32_t)((GPR_U32(ctx, f.rt) & keepMask) | (mem << shift)));
            return Flow::Next;
        }
        case 0x23: SET_GPR_S32(ctx, f.rt, (int32_t)READ32(ADD32(GPR_U32(ctx, f.rs), f.simm))); return Flow::Next;
        case 0x24: SET_GPR_U32(ctx, f.rt, (uint32_t)READ8(ADD32(GPR_U32(ctx, f.rs), f.simm))); return Flow::Next;
        case 0x25: SET_GPR_U32(ctx, f.rt, (uint32_t)READ16(ADD32(GPR_U32(ctx, f.rs), f.simm))); return Flow::Next;
        case 0x26: // LWR
        {
            const uint32_t addr = ADD32(GPR_U32(ctx, f.rs), f.simm), aligned = addr & ~3u, offset = addr & 3u;
            const uint32_t mem = READ32(aligned);
            const uint32_t shift = offset << 3;
            const uint32_t keepMask = (offset == 0) ? 0u : (0xFFFFFFFFu << ((4u - offset) << 3));
            const uint32_t merged32 = (GPR_U32(ctx, f.rt) & keepMask) | (mem >> shift);
            uint64_t merged64 = (GPR_U64(ctx, f.rt) & 0xFFFFFFFF00000000ull) | (uint64_t)merged32;
            if (offset == 0)
                merged64 = (uint64_t)(int64_t)(int32_t)merged32;
            SET_GPR_U64(ctx, f.rt, merged64);
            return Flow::Next;
        }
        case 0x27: SET_GPR_ZE32(ctx, f.rt, READ32(ADD32(GPR_U32(ctx, f.rs), f.simm))); return Flow::Next; // LWU
        case 0x28: WRITE8(ADD32(GPR_U32(ctx, f.rs), f.simm), (uint8_t)GPR_U32(ctx, f.rt)); return Flow::Next;
        case 0x29: WRITE16(ADD32(GPR_U32(ctx, f.rs), f.simm), (uint16_t)GPR_U32(ctx, f.rt)); return Flow::Next;
        case 0x2A: // SWL
        {
            const uint32_t addr = ADD32(GPR_U32(ctx, f.rs), f.simm), aligned = addr & ~3u, offset = addr & 3u;
            const uint32_t shift = (3u - offset) << 3;
            const uint32_t mask = 0xFFFFFFFFu >> shift;
            const uint32_t oldData = READ32(aligned);
            const uint32_t newData = (oldData & ~mask) | ((GPR_U32(ctx, f.rt) >> shift) & mask);
            WRITE32(aligned, newData);
            return Flow::Next;
        }
        case 0x2B: WRITE32(ADD32(GPR_U32(ctx, f.rs), f.simm), GPR_U32(ctx, f.rt)); return Flow::Next;
        case 0x2C: // SDL
        {
            const uint32_t addr = ADD32(GPR_U32(ctx, f.rs), f.simm), aligned = addr & ~7u, offset = addr & 7u;
            const uint32_t shift = (7u - offset) << 3;
            const uint64_t mask = 0xFFFFFFFFFFFFFFFFull >> shift;
            const uint64_t oldData = READ64(aligned);
            const uint64_t newData = (oldData & ~mask) | ((GPR_U64(ctx, f.rt) >> shift) & mask);
            WRITE64(aligned, newData);
            return Flow::Next;
        }
        case 0x2D: // SDR
        {
            const uint32_t addr = ADD32(GPR_U32(ctx, f.rs), f.simm), aligned = addr & ~7u, offset = addr & 7u;
            const uint32_t shift = offset << 3;
            const uint64_t mask = 0xFFFFFFFFFFFFFFFFull << shift;
            const uint64_t oldData = READ64(aligned);
            const uint64_t newData = (oldData & ~mask) | ((GPR_U64(ctx, f.rt) << shift) & mask);
            WRITE64(aligned, newData);
            return Flow::Next;
        }
        case 0x2E: // SWR
        {
            const uint32_t addr = ADD32(GPR_U32(ctx, f.rs), f.simm), aligned = addr & ~3u, offset = addr & 3u;
            const uint32_t shift = offset << 3;
            const uint32_t mask = 0xFFFFFFFFu << shift;
            const uint32_t oldData = READ32(aligned);
            const uint32_t newData = (oldData & ~mask) | ((GPR_U32(ctx, f.rt) << shift) & mask);
            WRITE32(aligned, newData);
            return Flow::Next;
        }
        case 0x2F: return Flow::Next; // CACHE
        case 0x31: // LWC1
        {
            const uint32_t bits = READ32(ADD32(GPR_U32(ctx, f.rs), f.simm));
            std::memcpy(&ctx->f[f.rt], &bits, sizeof(bits));
            return Flow::Next;
        }
        case 0x33: return Flow::Next; // PREF
        case 0x37: SET_GPR_U64(ctx, f.rt, READ64(ADD32(GPR_U32(ctx, f.rs), f.simm))); return Flow::Next; // LD
        case 0x39: // SWC1
        {
            uint32_t bits;
            std::memcpy(&bits, &ctx->f[f.rt], sizeof(bits));
            WRITE32(ADD32(GPR_U32(ctx, f.rs), f.simm), bits);
            return Flow::Next;
        }
        case 0x3F: WRITE64(ADD32(GPR_U32(ctx, f.rs), f.simm), GPR_U64(ctx, f.rt)); return Flow::Next; // SD
        default:
            reportUnimplemented(pc, f.word, "opcode");
            return Flow::Next;
        }
        reportUnimplemented(pc, f.word, "instruction");
        return Flow::Next;
    }

    struct BranchInfo
    {
        bool taken = false;
        bool likely = false;   // delay slot only runs when taken
        bool link = false;
        uint32_t linkReg = 31;
        bool call = false;
        uint32_t target = 0;
    };

    // Decodes a control transfer; registers are read before the delay slot.
    BranchInfo evaluateBranch(R5900Context *ctx, uint32_t pc, const Fields &f)
    {
        BranchInfo b;
        const uint32_t op = f.word >> 26;
        const uint32_t branchTarget = pc + 4u + (static_cast<uint32_t>(f.simm) << 2);
        switch (op)
        {
        case 0x00:
            b.taken = true;
            b.target = GPR_U32(ctx, f.rs);
            if (f.funct == 0x09) // JALR
            {
                b.link = true;
                b.linkReg = f.rd;
                b.call = true;
            }
            return b;
        case 0x01:
        {
            const int64_t value = GPR_S64(ctx, f.rs);
            const bool lessThanZero = value < 0;
            b.target = branchTarget;
            switch (f.rt)
            {
            case 0x00: b.taken = lessThanZero; break;
            case 0x01: b.taken = !lessThanZero; break;
            case 0x02: b.taken = lessThanZero; b.likely = true; break;
            case 0x03: b.taken = !lessThanZero; b.likely = true; break;
            case 0x10: b.taken = lessThanZero; b.link = true; b.call = true; break;
            case 0x11: b.taken = !lessThanZero; b.link = true; b.call = true; break;
            case 0x12: b.taken = lessThanZero; b.link = true; b.call = true; b.likely = true; break;
            case 0x13: b.taken = !lessThanZero; b.link = true; b.call = true; b.likely = true; break;
            }
            return b;
        }
        case 0x02:
        case 0x03:
            b.taken = true;
            b.target = ((pc + 4u) & 0xF0000000u) | ((f.word & 0x03FFFFFFu) << 2);
            b.link = b.call = (op == 0x03);
            return b;
        case 0x04: case 0x14: b.taken = GPR_U64(ctx, f.rs) == GPR_U64(ctx, f.rt); break;
        case 0x05: case 0x15: b.taken = GPR_U64(ctx, f.rs) != GPR_U64(ctx, f.rt); break;
        case 0x06: case 0x16: b.taken = GPR_S64(ctx, f.rs) <= 0; break;
        case 0x07: case 0x17: b.taken = GPR_S64(ctx, f.rs) > 0; break;
        case 0x11: // BC1F/BC1T/BC1FL/BC1TL
        {
            const bool condition = (ctx->fcr31 & 0x800000) != 0;
            b.taken = (f.rt & 1u) ? condition : !condition;
            b.likely = (f.rt & 2u) != 0;
            b.target = branchTarget;
            return b;
        }
        }
        b.target = branchTarget;
        b.likely = op >= 0x14;
        return b;
    }
}

void ps2InterpretGuestCode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    uint32_t pc = ctx->pc;
    uint32_t sinceCheckpoint = 0;
    for (;;)
    {
        // Control reached code that has a recompiled function, or left the
        // RAM this interpreter runs: let the caller (or the scheduler)
        // continue there.
        if (ps2HasRecompiledFunction(pc) || !runtime->canRunGuestCode(pc))
        {
            ctx->pc = pc;
            return;
        }
        if (++sinceCheckpoint >= kCheckpointInterval)
        {
            sinceCheckpoint = 0;
            ctx->pc = pc;
            if (runtime->eeCheckpointDue(kCheckpointInterval))
                return;
        }

        ctx->pc = pc;
        const Fields f = decode(READ32(pc));
        if (!isControlTransfer(f.word))
        {
            if (executeSimple(rdram, ctx, runtime, pc, f) == Flow::Stop)
                return;
            pc += 4u;
            continue;
        }

        const BranchInfo b = evaluateBranch(ctx, pc, f);
        if (b.link)
            SET_GPR_U32(ctx, b.linkReg, pc + 8u);
        if (b.taken || !b.likely)
        {
            const uint32_t delaySlotPc = pc + 4u;
            const Fields slot = decode(READ32(delaySlotPc));
            ctx->pc = delaySlotPc;
            ctx->in_delay_slot = true;
            ctx->branch_pc = pc;
            if (isControlTransfer(slot.word))
                reportUnimplemented(delaySlotPc, slot.word, "branch in delay slot");
            else
                executeSimple(rdram, ctx, runtime, delaySlotPc, slot);
            ctx->in_delay_slot = false;
        }
        if (!b.taken)
        {
            pc += 8u;
            continue;
        }

        if (b.call && ps2HasRecompiledFunction(b.target))
        {
            const PS2Runtime::GuestBranchKind kind = (f.word >> 26) == 0x00 ? PS2Runtime::GuestBranchKind::IndirectCall
                                                                            : PS2Runtime::GuestBranchKind::DirectCall;
            if (!runtime->dispatchGuestBranch(rdram, ctx, b.target, pc, pc + 8u, kind, "interpreter"))
                return;
            pc = pc + 8u;
            continue;
        }
        pc = b.target;
    }
}
