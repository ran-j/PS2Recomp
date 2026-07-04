#include "ps2recomp/Translators/mmi_translator.h"
#include "ps2recomp/code_generator.h"
#include "ps2recomp/codegen_helpers.h"
#include "ps2recomp/instructions.h"
#include "ps2recomp/types.h"

#include <fmt/format.h>
#include <sstream>
#include <cmath>


namespace ps2recomp
{
    MmiTranslator::MmiTranslator(CodeGenerator &codeGenerator)
        : m_codeGenerator(codeGenerator)
    {
    }

    std::string MmiTranslator::translate(const Instruction &inst)
    {
        uint32_t function = inst.function;
        uint8_t rs = inst.rs;
        uint8_t rt = inst.rt;
        uint8_t rd = inst.rd;
        uint8_t sa = inst.sa;
        switch (function)
        {
        case MMI_MFHI1:
            return fmt::format("SET_GPR_U64(ctx, {}, ctx->hi1);", rd);
        case MMI_MTHI1:
            return fmt::format("ctx->hi1 = GPR_U64(ctx, {});", rs);
        case MMI_MFLO1:
            return fmt::format("SET_GPR_U64(ctx, {}, ctx->lo1);", rd);
        case MMI_MTLO1:
            return fmt::format("ctx->lo1 = GPR_U64(ctx, {});", rs);
        case MMI_MULT1:
            if (rd != 0)
            {
                return fmt::format("{{ int64_t result = (int64_t)GPR_S32(ctx, {}) * (int64_t)GPR_S32(ctx, {}); ctx->lo1 = (uint64_t)(int64_t)(int32_t)result; ctx->hi1 = (uint64_t)(int64_t)(int32_t)(result >> 32); SET_GPR_S32(ctx, {}, (int32_t)result); }}", rs, rt, rd);
            }
            return fmt::format("{{ int64_t result = (int64_t)GPR_S32(ctx, {}) * (int64_t)GPR_S32(ctx, {}); ctx->lo1 = (uint64_t)(int64_t)(int32_t)result; ctx->hi1 = (uint64_t)(int64_t)(int32_t)(result >> 32); }}", rs, rt);
        case MMI_MULTU1:
            if (rd != 0)
            {
                return fmt::format("{{ uint64_t result = (uint64_t)GPR_U32(ctx, {}) * (uint64_t)GPR_U32(ctx, {}); ctx->lo1 = (uint64_t)(int64_t)(int32_t)result; ctx->hi1 = (uint64_t)(int64_t)(int32_t)(result >> 32); SET_GPR_S32(ctx, {}, (int32_t)result); }}", rs, rt, rd);
            }
            return fmt::format("{{ uint64_t result = (uint64_t)GPR_U32(ctx, {}) * (uint64_t)GPR_U32(ctx, {}); ctx->lo1 = (uint64_t)(int64_t)(int32_t)result; ctx->hi1 = (uint64_t)(int64_t)(int32_t)(result >> 32); }}", rs, rt);
        case MMI_DIV1:
            return fmt::format("{{ int32_t divisor = GPR_S32(ctx, {}); "
                               "int32_t dividend = GPR_S32(ctx, {}); "
                               "if (divisor != 0) {{ "
                               "    if (divisor == -1 && dividend == INT32_MIN) {{ "
                               "        ctx->lo1 = (uint64_t)(int64_t)INT32_MIN; ctx->hi1 = 0; "
                               "    }} else {{ "
                               "        ctx->lo1 = (uint64_t)(int64_t)(dividend / divisor); "
                               "        ctx->hi1 = (uint64_t)(int64_t)(dividend % divisor); "
                               "    }} "
                               "}} else {{ "
                               "    ctx->lo1 = (dividend < 0) ? 1ull : 0xFFFFFFFFFFFFFFFFull; ctx->hi1 = (uint64_t)(int64_t)dividend; "
                               "}} }}",
                               inst.rt, inst.rs);
        case MMI_DIVU1:
            return fmt::format("{{ uint32_t divisor = GPR_U32(ctx, {}); if (divisor != 0) {{ ctx->lo1 = (uint64_t)(int64_t)(int32_t)(GPR_U32(ctx, {}) / divisor); ctx->hi1 = (uint64_t)(int64_t)(int32_t)(GPR_U32(ctx, {}) % divisor); }} else {{ ctx->lo1=0xFFFFFFFFFFFFFFFFull; ctx->hi1=(uint64_t)(int64_t)(int32_t)GPR_U32(ctx,{}); }} }}", rt, rs, rs, rs);
        case MMI_MADD:
            if (rd != 0)
            {
                return fmt::format("{{ uint64_t acc = Ps2HiLoToU64(ctx->hi, ctx->lo); int64_t prod = (int64_t)GPR_S32(ctx, {}) * (int64_t)GPR_S32(ctx, {}); int64_t result = acc + prod; ctx->lo = Ps2SignExt32ToU64((uint32_t)result); ctx->hi = Ps2SignExt32ToU64((uint32_t)(result >> 32)); SET_GPR_S32(ctx, {}, (int32_t)result); }}", rs, rt, rd);
            }
            return fmt::format("{{ uint64_t acc = Ps2HiLoToU64(ctx->hi, ctx->lo); int64_t prod = (int64_t)GPR_S32(ctx, {}) * (int64_t)GPR_S32(ctx, {}); int64_t result = acc + prod; ctx->lo = Ps2SignExt32ToU64((uint32_t)result); ctx->hi = Ps2SignExt32ToU64((uint32_t)(result >> 32)); }}", rs, rt);
        case MMI_MADDU:
            if (rd != 0)
            {
                return fmt::format("{{ uint64_t acc = Ps2HiLoToU64(ctx->hi, ctx->lo); uint64_t prod = (uint64_t)GPR_U32(ctx, {}) * (uint64_t)GPR_U32(ctx, {}); uint64_t result = acc + prod; ctx->lo = Ps2SignExt32ToU64((uint32_t)result); ctx->hi = Ps2SignExt32ToU64((uint32_t)(result >> 32)); SET_GPR_S32(ctx, {}, (int32_t)result); }}", rs, rt, rd);
            }
            return fmt::format("{{ uint64_t acc = Ps2HiLoToU64(ctx->hi, ctx->lo); uint64_t prod = (uint64_t)GPR_U32(ctx, {}) * (uint64_t)GPR_U32(ctx, {}); uint64_t result = acc + prod; ctx->lo = Ps2SignExt32ToU64((uint32_t)result); ctx->hi = Ps2SignExt32ToU64((uint32_t)(result >> 32)); }}", rs, rt);
        case MMI_MSUB:
            if (rd != 0)
            {
                return fmt::format("{{ uint64_t acc = Ps2HiLoToU64(ctx->hi, ctx->lo); int64_t prod = (int64_t)GPR_S32(ctx, {}) * (int64_t)GPR_S32(ctx, {}); int64_t result = acc - prod; ctx->lo = Ps2SignExt32ToU64((uint32_t)result); ctx->hi = Ps2SignExt32ToU64((uint32_t)(result >> 32)); SET_GPR_S32(ctx, {}, (int32_t)result); }}", rs, rt, rd);
            }
            return fmt::format("{{ uint64_t acc = Ps2HiLoToU64(ctx->hi, ctx->lo); int64_t prod = (int64_t)GPR_S32(ctx, {}) * (int64_t)GPR_S32(ctx, {}); int64_t result = acc - prod; ctx->lo = Ps2SignExt32ToU64((uint32_t)result); ctx->hi = Ps2SignExt32ToU64((uint32_t)(result >> 32)); }}", rs, rt);
        case MMI_MSUBU:
            if (rd != 0)
            {
                return fmt::format("{{ uint64_t acc = Ps2HiLoToU64(ctx->hi, ctx->lo); uint64_t prod = (uint64_t)GPR_U32(ctx, {}) * (uint64_t)GPR_U32(ctx, {}); uint64_t result = acc - prod; ctx->lo = Ps2SignExt32ToU64((uint32_t)result); ctx->hi = Ps2SignExt32ToU64((uint32_t)(result >> 32)); SET_GPR_S32(ctx, {}, (int32_t)result); }}", rs, rt, rd);
            }
            return fmt::format("{{ uint64_t acc = Ps2HiLoToU64(ctx->hi, ctx->lo); uint64_t prod = (uint64_t)GPR_U32(ctx, {}) * (uint64_t)GPR_U32(ctx, {}); uint64_t result = acc - prod; ctx->lo = Ps2SignExt32ToU64((uint32_t)result); ctx->hi = Ps2SignExt32ToU64((uint32_t)(result >> 32)); }}", rs, rt);
        case MMI_MADD1:
            if (rd != 0)
            {
                return fmt::format("{{ uint64_t acc = Ps2HiLoToU64(ctx->hi1, ctx->lo1); int64_t prod = (int64_t)GPR_S32(ctx, {}) * (int64_t)GPR_S32(ctx, {}); int64_t result = acc + prod; ctx->lo1 = Ps2SignExt32ToU64((uint32_t)result); ctx->hi1 = Ps2SignExt32ToU64((uint32_t)(result >> 32)); SET_GPR_S32(ctx, {}, (int32_t)result); }}", rs, rt, rd);
            }
            return fmt::format("{{ uint64_t acc = Ps2HiLoToU64(ctx->hi1, ctx->lo1); int64_t prod = (int64_t)GPR_S32(ctx, {}) * (int64_t)GPR_S32(ctx, {}); int64_t result = acc + prod; ctx->lo1 = Ps2SignExt32ToU64((uint32_t)result); ctx->hi1 = Ps2SignExt32ToU64((uint32_t)(result >> 32)); }}", rs, rt);
        case MMI_MADDU1:
            if (rd != 0)
            {
                return fmt::format("{{ uint64_t acc = Ps2HiLoToU64(ctx->hi1, ctx->lo1); uint64_t prod = (uint64_t)GPR_U32(ctx, {}) * (uint64_t)GPR_U32(ctx, {}); uint64_t result = acc + prod; ctx->lo1 = Ps2SignExt32ToU64((uint32_t)result); ctx->hi1 = Ps2SignExt32ToU64((uint32_t)(result >> 32)); SET_GPR_S32(ctx, {}, (int32_t)result); }}", rs, rt, rd);
            }
            return fmt::format("{{ uint64_t acc = Ps2HiLoToU64(ctx->hi1, ctx->lo1); uint64_t prod = (uint64_t)GPR_U32(ctx, {}) * (uint64_t)GPR_U32(ctx, {}); uint64_t result = acc + prod; ctx->lo1 = Ps2SignExt32ToU64((uint32_t)result); ctx->hi1 = Ps2SignExt32ToU64((uint32_t)(result >> 32)); }}", rs, rt);
        case MMI_PLZCW:
            return fmt::format(
                "{{ "
                "uint64_t v = GPR_U64(ctx, {}); "
                "uint32_t lo = (uint32_t)(v & 0xFFFFFFFFu); "
                "uint32_t hi = (uint32_t)(v >> 32); "
                "uint64_t out = ((uint64_t)ps2_plzcw32(hi) << 32) | (uint64_t)ps2_plzcw32(lo); "
                "SET_GPR_U64(ctx, {}, out); "
                "}}",
                rs, rd);
        case MMI_PSLLH:
            return fmt::format("SET_GPR_VEC(ctx, {}, _mm_slli_epi16(GPR_VEC(ctx, {}), {}));", rd, rt, sa);
        case MMI_PSRLH:
            return fmt::format("SET_GPR_VEC(ctx, {}, _mm_srli_epi16(GPR_VEC(ctx, {}), {}));", rd, rt, sa);
        case MMI_PSRAH:
            return fmt::format("SET_GPR_VEC(ctx, {}, _mm_srai_epi16(GPR_VEC(ctx, {}), {}));", rd, rt, sa);
        case MMI_PSLLW:
            return fmt::format("SET_GPR_VEC(ctx, {}, _mm_slli_epi32(GPR_VEC(ctx, {}), {}));", rd, rt, sa);
        case MMI_PSRLW:
            return fmt::format("SET_GPR_VEC(ctx, {}, _mm_srli_epi32(GPR_VEC(ctx, {}), {}));", rd, rt, sa);
        case MMI_PSRAW:
            return fmt::format("SET_GPR_VEC(ctx, {}, _mm_srai_epi32(GPR_VEC(ctx, {}), {}));", rd, rt, sa);
        case MMI_MMI0:
            return m_codeGenerator.translateMMI0Instruction(inst);
        case MMI_MMI1:
            return m_codeGenerator.translateMMI1Instruction(inst);
        case MMI_MMI2:
            return m_codeGenerator.translateMMI2Instruction(inst);
        case MMI_MMI3:
            return m_codeGenerator.translateMMI3Instruction(inst);
        case MMI_PMFHL:
            return m_codeGenerator.translatePMFHLInstruction(inst);
        case MMI_PMTHL:
            return m_codeGenerator.translatePMTHLInstruction(inst);
        default:
            return m_codeGenerator.emitUnhandledInstruction(inst, fmt::format("Unhandled MMI instruction: function 0x{:X}", function));
        }
    }

}
