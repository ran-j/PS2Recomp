#include "ps2recomp/Translators/special_translator.h"
#include "ps2recomp/code_generator.h"
#include "ps2recomp/codegen_helpers.h"
#include "ps2recomp/instructions.h"
#include "ps2recomp/types.h"

#include <fmt/format.h>
#include <sstream>
#include <cmath>


namespace ps2recomp
{
    SpecialTranslator::SpecialTranslator(CodeGenerator &codeGenerator)
        : m_codeGenerator(codeGenerator)
    {
    }

    std::string SpecialTranslator::translate(const Instruction &inst)
    {
        switch (inst.function)
        {
        case SPECIAL_SLL:
            if (inst.rd == 0 && inst.rt == 0 && inst.sa == 0)
                return "// NOP";
            if (inst.rd == 0)
                return "";
            return fmt::format("SET_GPR_S32(ctx, {}, (int32_t)SLL32(GPR_U32(ctx, {}), {}));", inst.rd, inst.rt, inst.sa);
        case SPECIAL_SRL:
            return fmt::format("SET_GPR_S32(ctx, {}, (int32_t)SRL32(GPR_U32(ctx, {}), {}));", inst.rd, inst.rt, inst.sa);
        case SPECIAL_SRA:
            return fmt::format("SET_GPR_S32(ctx, {}, SRA32(GPR_S32(ctx, {}), {}));", inst.rd, inst.rt, inst.sa);
        case SPECIAL_SLLV:
            return fmt::format("SET_GPR_S32(ctx, {}, (int32_t)SLL32(GPR_U32(ctx, {}), GPR_U32(ctx, {}) & 0x1F));", inst.rd, inst.rt, inst.rs);
        case SPECIAL_SRLV:
            return fmt::format("SET_GPR_S32(ctx, {}, (int32_t)SRL32(GPR_U32(ctx, {}), GPR_U32(ctx, {}) & 0x1F));", inst.rd, inst.rt, inst.rs);
        case SPECIAL_SRAV:
            return fmt::format("SET_GPR_S32(ctx, {}, SRA32(GPR_S32(ctx, {}), GPR_U32(ctx, {}) & 0x1F));", inst.rd, inst.rt, inst.rs);
        case SPECIAL_JR:
            return fmt::format("// JR ${} - Handled by branch logic", inst.rs);
        case SPECIAL_JALR:
            return fmt::format("// JALR ${}, ${} - Handled by branch logic", inst.rd, inst.rs);
        case SPECIAL_SYSCALL:
            return fmt::format("runtime->handleSyscall(rdram, ctx, 0x{:X}u);", (inst.raw >> 6) & 0xFFFFFu);
        case SPECIAL_BREAK:
            return fmt::format("runtime->handleBreak(rdram, ctx);");
        case SPECIAL_SYNC:
            return "// SYNC instruction - memory barrier\n// In recompiled code, we don't need explicit memory barriers";
        case SPECIAL_MFHI:
            return fmt::format("SET_GPR_U64(ctx, {}, ctx->hi);", inst.rd);
        case SPECIAL_MTHI:
            return fmt::format("ctx->hi = GPR_U64(ctx, {});", inst.rs);
        case SPECIAL_MFLO:
            return fmt::format("SET_GPR_U64(ctx, {}, ctx->lo);", inst.rd);
        case SPECIAL_MTLO:
            return fmt::format("ctx->lo = GPR_U64(ctx, {});", inst.rs);
        case SPECIAL_MULT:
            if (inst.rd != 0)
            {
                return fmt::format("{{ int64_t result = (int64_t)GPR_S32(ctx, {}) * (int64_t)GPR_S32(ctx, {}); ctx->lo = (uint64_t)(int64_t)(int32_t)result; ctx->hi = (uint64_t)(int64_t)(int32_t)(result >> 32); SET_GPR_S32(ctx, {}, (int32_t)result); }}", inst.rs, inst.rt, inst.rd);
            }
            return fmt::format("{{ int64_t result = (int64_t)GPR_S32(ctx, {}) * (int64_t)GPR_S32(ctx, {}); ctx->lo = (uint64_t)(int64_t)(int32_t)result; ctx->hi = (uint64_t)(int64_t)(int32_t)(result >> 32); }}", inst.rs, inst.rt);
        case SPECIAL_MULTU:
            if (inst.rd != 0)
            {
                return fmt::format("{{ uint64_t result = (uint64_t)GPR_U32(ctx, {}) * (uint64_t)GPR_U32(ctx, {}); ctx->lo = (uint64_t)(int64_t)(int32_t)result; ctx->hi = (uint64_t)(int64_t)(int32_t)(result >> 32); SET_GPR_S32(ctx, {}, (int32_t)result); }}", inst.rs, inst.rt, inst.rd);
            }
            return fmt::format("{{ uint64_t result = (uint64_t)GPR_U32(ctx, {}) * (uint64_t)GPR_U32(ctx, {}); ctx->lo = (uint64_t)(int64_t)(int32_t)result; ctx->hi = (uint64_t)(int64_t)(int32_t)(result >> 32); }}", inst.rs, inst.rt);
        case SPECIAL_DIV:
            return fmt::format("{{ int32_t divisor = GPR_S32(ctx, {}); "
                               "   int32_t dividend = GPR_S32(ctx, {}); "
                               "   if (divisor != 0) {{ "
                               "       if (divisor == -1 && dividend == INT32_MIN) {{ "
                               "           ctx->lo = (uint64_t)(int64_t)INT32_MIN; ctx->hi = 0; "
                               "       }} else {{ "
                               "           ctx->lo = (uint64_t)(int64_t)(dividend / divisor); "
                               "           ctx->hi = (uint64_t)(int64_t)(dividend % divisor); "
                               "       }} "
                               "   }} else {{ "
                               "       ctx->lo = (dividend < 0) ? 1ull : 0xFFFFFFFFFFFFFFFFull; ctx->hi = (uint64_t)(int64_t)dividend; "
                               "   }} }}",
                               inst.rt, inst.rs);
        case SPECIAL_DIVU:
            return fmt::format("{{ uint32_t divisor = GPR_U32(ctx, {}); if (divisor != 0) {{ ctx->lo = (uint64_t)(int64_t)(int32_t)(GPR_U32(ctx, {}) / divisor); ctx->hi = (uint64_t)(int64_t)(int32_t)(GPR_U32(ctx, {}) % divisor); }} else {{ ctx->lo = 0xFFFFFFFFFFFFFFFFull; ctx->hi = (uint64_t)(int64_t)(int32_t)GPR_U32(ctx,{}); }} }}", inst.rt, inst.rs, inst.rs, inst.rs);
        case SPECIAL_ADD:
            return fmt::format(
                "{{ "
                "    int32_t rs_val = GPR_S32(ctx, {}); "
                "    int32_t rt_val = GPR_S32(ctx, {}); "
                "    int64_t result = (int64_t)rs_val + (int64_t)rt_val; "
                "    if (result > INT32_MAX || result < INT32_MIN) {{ "
                "        runtime->SignalException(ctx, EXCEPTION_INTEGER_OVERFLOW); "
                "    }} else {{ "
                "        SET_GPR_S32(ctx, {}, (int32_t)result); "
                "    }} "
                "}}",
                inst.rs, inst.rt, inst.rd);
        case SPECIAL_ADDU:
            return fmt::format("SET_GPR_S32(ctx, {}, (int32_t)ADD32(GPR_U32(ctx, {}), GPR_U32(ctx, {})));", inst.rd, inst.rs, inst.rt);
        case SPECIAL_SUB:
            return fmt::format(
                "{{ uint32_t tmp; bool ov; "
                "SUB32_OV(GPR_U32(ctx, {}), GPR_U32(ctx, {}), tmp, ov); "
                "if (ov) runtime->SignalException(ctx, EXCEPTION_INTEGER_OVERFLOW); "
                "else SET_GPR_S32(ctx, {}, (int32_t)tmp); }}",
                inst.rs, inst.rt, inst.rd);
        case SPECIAL_SUBU:
            return fmt::format("SET_GPR_S32(ctx, {}, (int32_t)SUB32(GPR_U32(ctx, {}), GPR_U32(ctx, {})));", inst.rd, inst.rs, inst.rt);
        case SPECIAL_AND:
            return fmt::format("SET_GPR_U64(ctx, {}, GPR_U64(ctx, {}) & GPR_U64(ctx, {}));", inst.rd, inst.rs, inst.rt);
        case SPECIAL_OR:
            return fmt::format("SET_GPR_U64(ctx, {}, GPR_U64(ctx, {}) | GPR_U64(ctx, {}));", inst.rd, inst.rs, inst.rt);
        case SPECIAL_XOR:
            return fmt::format("SET_GPR_U64(ctx, {}, GPR_U64(ctx, {}) ^ GPR_U64(ctx, {}));", inst.rd, inst.rs, inst.rt);
        case SPECIAL_NOR:
            return fmt::format("SET_GPR_U64(ctx, {}, ~(GPR_U64(ctx, {}) | GPR_U64(ctx, {})));", inst.rd, inst.rs, inst.rt);
        case SPECIAL_SLT:
            return fmt::format("SET_GPR_U64(ctx, {}, ((int64_t)GPR_S64(ctx, {}) < (int64_t)GPR_S64(ctx, {})) ? 1 : 0);", inst.rd, inst.rs, inst.rt);
        case SPECIAL_SLTU:
            return fmt::format("SET_GPR_U64(ctx, {}, ((uint64_t)GPR_U64(ctx, {}) < (uint64_t)GPR_U64(ctx, {})) ? 1 : 0);", inst.rd, inst.rs, inst.rt);
        case SPECIAL_MOVZ:
            return fmt::format("if (GPR_U64(ctx, {}) == 0) SET_GPR_VEC(ctx, {}, GPR_VEC(ctx, {}));", inst.rt, inst.rd, inst.rs);
        case SPECIAL_MOVN:
            return fmt::format("if (GPR_U64(ctx, {}) != 0) SET_GPR_VEC(ctx, {}, GPR_VEC(ctx, {}));", inst.rt, inst.rd, inst.rs);
        case SPECIAL_MFSA:
            return fmt::format("SET_GPR_U32(ctx, {}, ctx->sa);", inst.rd);
        case SPECIAL_MTSA:
            return fmt::format("ctx->sa = GPR_U32(ctx, {}) & 0x7F;", inst.rs);
        case SPECIAL_DADD:
            return fmt::format(
                "{{ int64_t a = (int64_t)GPR_S64(ctx, {}); "
                "int64_t b = (int64_t)GPR_S64(ctx, {}); "
                "int64_t r = a + b; "
                "if (((a ^ b) >= 0) && ((a ^ r) < 0)) runtime->SignalException(ctx, EXCEPTION_INTEGER_OVERFLOW); "
                "else SET_GPR_S64(ctx, {}, r); }}",
                inst.rs, inst.rt, inst.rd);
        case SPECIAL_DADDU:
            return fmt::format(
                "SET_GPR_U64(ctx, {}, (uint64_t)GPR_U64(ctx, {}) + (uint64_t)GPR_U64(ctx, {}));",
                inst.rd, inst.rs, inst.rt);
        case SPECIAL_DSUB:
            return fmt::format(
                "{{ int64_t a = (int64_t)GPR_S64(ctx, {}); "
                "int64_t b = (int64_t)GPR_S64(ctx, {}); "
                "int64_t r = a - b; "
                "if (((a ^ b) < 0) && ((a ^ r) < 0)) runtime->SignalException(ctx, EXCEPTION_INTEGER_OVERFLOW); "
                "else SET_GPR_S64(ctx, {}, r); }}",
                inst.rs, inst.rt, inst.rd);
        case SPECIAL_DSUBU:
            return fmt::format("SET_GPR_U64(ctx, {}, GPR_U64(ctx, {}) - GPR_U64(ctx, {}));", inst.rd, inst.rs, inst.rt);
        case SPECIAL_DSLL:
            return fmt::format("SET_GPR_U64(ctx, {}, GPR_U64(ctx, {}) << {});", inst.rd, inst.rt, inst.sa);
        case SPECIAL_DSRL:
            return fmt::format("SET_GPR_U64(ctx, {}, GPR_U64(ctx, {}) >> {});", inst.rd, inst.rt, inst.sa);
        case SPECIAL_DSRA:
            return fmt::format("SET_GPR_S64(ctx, {}, GPR_S64(ctx, {}) >> {});", inst.rd, inst.rt, inst.sa);
        case SPECIAL_DSLLV:
            return fmt::format("SET_GPR_U64(ctx, {}, GPR_U64(ctx, {}) << (GPR_U32(ctx, {}) & 0x3F));", inst.rd, inst.rt, inst.rs);
        case SPECIAL_DSRLV:
            return fmt::format("SET_GPR_U64(ctx, {}, GPR_U64(ctx, {}) >> (GPR_U32(ctx, {}) & 0x3F));", inst.rd, inst.rt, inst.rs);
        case SPECIAL_DSRAV:
            return fmt::format("SET_GPR_S64(ctx, {}, GPR_S64(ctx, {}) >> (GPR_U32(ctx, {}) & 0x3F));", inst.rd, inst.rt, inst.rs);
        case SPECIAL_DSLL32:
            return fmt::format("SET_GPR_U64(ctx, {}, GPR_U64(ctx, {}) << (32 + {}));", inst.rd, inst.rt, inst.sa);
        case SPECIAL_DSRL32:
            return fmt::format("SET_GPR_U64(ctx, {}, GPR_U64(ctx, {}) >> (32 + {}));", inst.rd, inst.rt, inst.sa);
        case SPECIAL_DSRA32:
            return fmt::format("SET_GPR_S64(ctx, {}, GPR_S64(ctx, {}) >> (32 + {}));", inst.rd, inst.rt, inst.sa);
        case SPECIAL_TGE:
            return fmt::format("if (GPR_S64(ctx, {}) >= GPR_S64(ctx, {})) {{ runtime->handleTrap(rdram, ctx); }}", inst.rs, inst.rt);
        case SPECIAL_TGEU:
            return fmt::format("if (GPR_U64(ctx, {}) >= GPR_U64(ctx, {})) {{ runtime->handleTrap(rdram, ctx); }}", inst.rs, inst.rt);
        case SPECIAL_TLT:
            return fmt::format("if (GPR_S64(ctx, {}) < GPR_S64(ctx, {})) {{ runtime->handleTrap(rdram, ctx); }}", inst.rs, inst.rt);
        case SPECIAL_TLTU:
            return fmt::format("if (GPR_U64(ctx, {}) < GPR_U64(ctx, {})) {{ runtime->handleTrap(rdram, ctx); }}", inst.rs, inst.rt);
        case SPECIAL_TEQ:
            return fmt::format("if (GPR_U64(ctx, {}) == GPR_U64(ctx, {})) {{ runtime->handleTrap(rdram, ctx); }}", inst.rs, inst.rt);
        case SPECIAL_TNE:
            return fmt::format("if (GPR_U64(ctx, {}) != GPR_U64(ctx, {})) {{ runtime->handleTrap(rdram, ctx); }}", inst.rs, inst.rt);
        default:
            return m_codeGenerator.emitUnhandledInstruction(inst, fmt::format("Unhandled SPECIAL instruction: 0x{:X}", inst.function));
        }
    }

}
