#include "ps2recomp/Translators/vu_translator.h"
#include "ps2recomp/code_generator.h"
#include "ps2recomp/codegen_helpers.h"
#include "ps2recomp/instructions.h"
#include "ps2recomp/types.h"

#include <fmt/format.h>
#include <sstream>
#include <cmath>


namespace ps2recomp
{
    VuTranslator::VuTranslator(CodeGenerator &codeGenerator)
        : m_codeGenerator(codeGenerator)
    {
    }

    std::string VuTranslator::translate(const Instruction &inst)
    {
        uint8_t format = inst.rs; // Use parsed rs field for COP2 format
        uint8_t rt = inst.rt;
        uint8_t rd = inst.rd;
        uint8_t sa = inst.sa;

        switch (format)
        {
        case COP2_QMFC2:
            return fmt::format("SET_GPR_VEC(ctx, {}, _mm_castps_si128(ctx->vu0_vf[{}]));", rt, rd);
        case COP2_CFC2:
        {
            switch (rd) // Control register number is in rd
            {
            case VU0_CR_STATUS:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->vu0_status);", rt);
            case VU0_CR_MAC:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->vu0_mac_flags);", rt);
            case VU0_CR_VPU_STAT:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->vu0_vpu_stat);", rt);
            case VU0_CR_R:
                return fmt::format("SET_GPR_VEC(ctx, {}, _mm_castps_si128(ctx->vu0_r));", rt);
            case VU0_CR_I:
                return fmt::format("{{ uint32_t bits; std::memcpy(&bits, &ctx->vu0_i, sizeof(bits)); SET_GPR_U32(ctx, {}, bits); }}", rt);
            case VU0_CR_CLIP:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->vu0_clip_flags);", rt);
            case VU0_CR_TPC:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->vu0_tpc);", rt);
            case VU0_CR_CMSAR0:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->vu0_cmsar0);", rt);
            case VU0_CR_FBRST:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->vu0_fbrst);", rt);
            case VU0_CR_VPU_STAT2:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->vu0_vpu_stat2);", rt);
            case VU0_CR_TPC2:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->vu0_tpc2);", rt);
            case VU0_CR_CMSAR1:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->vu0_cmsar1);", rt);
            case VU0_CR_FBRST2:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->vu0_fbrst2);", rt);
            case VU0_CR_VPU_STAT3:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->vu0_vpu_stat3);", rt);
            case VU0_CR_CMSAR2:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->vu0_cmsar2);", rt);
            case VU0_CR_FBRST3:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->vu0_fbrst3);", rt);
            case VU0_CR_VPU_STAT4:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->vu0_vpu_stat4);", rt);
            case VU0_CR_CMSAR3:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->vu0_cmsar3);", rt);
            case VU0_CR_FBRST4:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->vu0_fbrst4);", rt);
            case VU0_CR_ACC:
                return fmt::format("SET_GPR_VEC(ctx, {}, _mm_castps_si128(ctx->vu0_acc));", rt);
            case VU0_CR_INFO: // I dd found on offical docs but ok
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->vu0_info);", rt);
            case VU0_CR_CLIP2:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->vu0_clip_flags2);", rt);
            case VU0_CR_P:
                return fmt::format("{{ uint32_t bits; std::memcpy(&bits, &ctx->vu0_p, sizeof(bits)); SET_GPR_U32(ctx, {}, bits); }}", rt);
            case VU0_CR_XITOP: // Maybe this does not exist, maybe we handle to vu0_itop
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->vu0_xitop);", rt);
            case VU0_CR_ITOP:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->vu0_itop);", rt);
            case VU0_CR_TOP:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->vu0_top);", rt);
            default:
                return fmt::format("// Unimplemented CFC2 VU CReg: {}", rt);
            }
        }
        case COP2_QMTC2:
            return fmt::format("ctx->vu0_vf[{}] = _mm_castsi128_ps(GPR_VEC(ctx, {}));", rd, rt);
        case COP2_CTC2:
        {
            switch (rd) // Control register number is in rd
            {
            case VU0_CR_STATUS:
                return fmt::format("ctx->vu0_status = GPR_U32(ctx, {}) & 0xFFFF;", rt);
            case VU0_CR_MAC:
                return fmt::format("ctx->vu0_mac_flags = GPR_U32(ctx, {});", rt);
            case VU0_CR_VPU_STAT:
                return fmt::format("ctx->vu0_vpu_stat = GPR_U32(ctx, {});", rt);
            case VU0_CR_CLIP:
                return fmt::format("ctx->vu0_clip_flags = GPR_U32(ctx, {});", rt);
            case VU0_CR_R:
                return fmt::format("ctx->vu0_r = _mm_castsi128_ps(GPR_VEC(ctx, {}));", rt);
            case VU0_CR_I:
                return fmt::format("{{ uint32_t tmp = GPR_U32(ctx, {}); std::memcpy(&ctx->vu0_i, &tmp, sizeof(tmp)); }}", rt);
            case VU0_CR_TPC:
                return fmt::format("ctx->vu0_tpc = GPR_U32(ctx, {});", rt);
            case VU0_CR_CMSAR0:
                return fmt::format("ctx->vu0_cmsar0 = GPR_U32(ctx, {});", rt);
            case VU0_CR_FBRST:
                return fmt::format("ctx->vu0_fbrst = GPR_U32(ctx, {});", rt);
            case VU0_CR_VPU_STAT2:
                return fmt::format("ctx->vu0_vpu_stat2 = GPR_U32(ctx, {});", rt);
            case VU0_CR_TPC2:
                return fmt::format("ctx->vu0_tpc2 = GPR_U32(ctx, {});", rt);
            case VU0_CR_CMSAR1:
                return fmt::format("ctx->vu0_cmsar1 = GPR_U32(ctx, {});", rt);
            case VU0_CR_FBRST2:
                return fmt::format("ctx->vu0_fbrst2 = GPR_U32(ctx, {});", rt);
            case VU0_CR_VPU_STAT3:
                return fmt::format("ctx->vu0_vpu_stat3 = GPR_U32(ctx, {});", rt);
            case VU0_CR_CMSAR2:
                return fmt::format("ctx->vu0_cmsar2 = GPR_U32(ctx, {});", rt);
            case VU0_CR_FBRST3:
                return fmt::format("ctx->vu0_fbrst3 = GPR_U32(ctx, {});", rt);
            case VU0_CR_VPU_STAT4:
                return fmt::format("ctx->vu0_vpu_stat4 = GPR_U32(ctx, {});", rt);
            case VU0_CR_CMSAR3:
                return fmt::format("ctx->vu0_cmsar3 = GPR_U32(ctx, {});", rt);
            case VU0_CR_FBRST4:
                return fmt::format("ctx->vu0_fbrst4 = GPR_U32(ctx, {});", rt);
            case VU0_CR_ACC:
                return fmt::format("ctx->vu0_acc = _mm_castsi128_ps(GPR_VEC(ctx, {}));", rt);
            case VU0_CR_INFO:
                return fmt::format("ctx->vu0_info = GPR_U32(ctx, {});", rt);
            case VU0_CR_CLIP2:
                return fmt::format("ctx->vu0_clip_flags2 = GPR_U32(ctx, {});", rt);
            case VU0_CR_P:
                return fmt::format("{{ uint32_t tmp = GPR_U32(ctx, {}); std::memcpy(&ctx->vu0_p, &tmp, sizeof(tmp)); }}", rt);
            case VU0_CR_XITOP:
                return fmt::format("ctx->vu0_xitop = GPR_U32(ctx, {}) & 0x3FF;", rt);
            case VU0_CR_ITOP:
                return fmt::format("ctx->vu0_itop = GPR_U32(ctx, {}) & 0x3FF;", rt);
            case VU0_CR_TOP:
                return fmt::format("ctx->vu0_top = GPR_U32(ctx, {}) & 0x3FF;", rt);
            default:
                return fmt::format("// Unimplemented CTC2 VU CReg: {}", rd);
            }
        }
        case COP2_BC:
            return fmt::format("// BC2 (Condition: 0x{:X}) - Handled by branch logic", rt);
        case COP2_CO:
        case COP2_CO + 1:
        case COP2_CO + 2:
        case COP2_CO + 3:
        case COP2_CO + 4:
        case COP2_CO + 5:
        case COP2_CO + 6:
        case COP2_CO + 7:
        case COP2_CO + 8:
        case COP2_CO + 9:
        case COP2_CO + 10:
        case COP2_CO + 11:
        case COP2_CO + 12:
        case COP2_CO + 13:
        case COP2_CO + 14:
        case COP2_CO + 15:
        {
            const uint8_t special1_func = static_cast<uint8_t>(inst.function & 0x3F);
            if (special1_func >= 0x3C) // Special2 Table
            {
                const uint8_t vu_func = static_cast<uint8_t>((((inst.raw >> 6) & 0x1F) << 2) | (inst.raw & 0x3));
                switch (vu_func)
                {
                case VU0_S2_VADDAx:
                case VU0_S2_VADDAy:
                case VU0_S2_VADDAz:
                case VU0_S2_VADDAw:
                    return m_codeGenerator.translateVU_VADDA_Field(inst);
                case VU0_S2_VSUBAx:
                case VU0_S2_VSUBAy:
                case VU0_S2_VSUBAz:
                case VU0_S2_VSUBAw:
                    return m_codeGenerator.translateVU_VSUBA_Field(inst);
                case VU0_S2_VMADDAx:
                case VU0_S2_VMADDAy:
                case VU0_S2_VMADDAz:
                case VU0_S2_VMADDAw:
                    return m_codeGenerator.translateVU_VMADDA_Field(inst);
                case VU0_S2_VMSUBAx:
                case VU0_S2_VMSUBAy:
                case VU0_S2_VMSUBAz:
                case VU0_S2_VMSUBAw:
                    return m_codeGenerator.translateVU_VMSUBA_Field(inst);
                case VU0_S2_VMULAx:
                case VU0_S2_VMULAy:
                case VU0_S2_VMULAz:
                case VU0_S2_VMULAw:
                    return m_codeGenerator.translateVU_VMULA_Field(inst);
                case VU0_S2_VADDA:
                    return m_codeGenerator.translateVU_VADDA(inst);
                case VU0_S2_VADDAq:
                    return m_codeGenerator.translateVU_VADDAq(inst);
                case VU0_S2_VADDAi:
                    return m_codeGenerator.translateVU_VADDAi(inst);
                case VU0_S2_VMADDA:
                    return m_codeGenerator.translateVU_VMADDA(inst);
                case VU0_S2_VMADDAq:
                    return m_codeGenerator.translateVU_VMADDAq(inst);
                case VU0_S2_VMADDAi:
                    return m_codeGenerator.translateVU_VMADDAi(inst);
                case VU0_S2_VSUBA:
                    return m_codeGenerator.translateVU_VSUBA(inst);
                case VU0_S2_VSUBAq:
                    return m_codeGenerator.translateVU_VSUBAq(inst);
                case VU0_S2_VSUBAi:
                    return m_codeGenerator.translateVU_VSUBAi(inst);
                case VU0_S2_VMSUBA:
                    return m_codeGenerator.translateVU_VMSUBA(inst);
                case VU0_S2_VMSUBAq:
                    return m_codeGenerator.translateVU_VMSUBAq(inst);
                case VU0_S2_VMSUBAi:
                    return m_codeGenerator.translateVU_VMSUBAi(inst);
                case VU0_S2_VMULA:
                    return m_codeGenerator.translateVU_VMULA(inst);
                case VU0_S2_VMULAq:
                    return m_codeGenerator.translateVU_VMULAq(inst);
                case VU0_S2_VMULAi:
                    return m_codeGenerator.translateVU_VMULAi(inst);
                case VU0_S2_VOPMULA:
                    return m_codeGenerator.translateVU_VOPMULA(inst);
                case VU0_S2_VITOF0:
                    return m_codeGenerator.translateVU_VITOF(inst, 0);
                case VU0_S2_VITOF4:
                    return m_codeGenerator.translateVU_VITOF(inst, 4);
                case VU0_S2_VITOF12:
                    return m_codeGenerator.translateVU_VITOF(inst, 12);
                case VU0_S2_VITOF15:
                    return m_codeGenerator.translateVU_VITOF(inst, 15);
                case VU0_S2_VFTOI0:
                    return m_codeGenerator.translateVU_VFTOI(inst, 0);
                case VU0_S2_VFTOI4:
                    return m_codeGenerator.translateVU_VFTOI(inst, 4);
                case VU0_S2_VFTOI12:
                    return m_codeGenerator.translateVU_VFTOI(inst, 12);
                case VU0_S2_VFTOI15:
                    return m_codeGenerator.translateVU_VFTOI(inst, 15);
                case VU0_S2_VLQI:
                    return m_codeGenerator.translateVU_VLQI(inst);
                case VU0_S2_VSQI:
                    return m_codeGenerator.translateVU_VSQI(inst);
                case VU0_S2_VLQD:
                    return m_codeGenerator.translateVU_VLQD(inst);
                case VU0_S2_VSQD:
                    return m_codeGenerator.translateVU_VSQD(inst);
                case VU0_S2_VDIV:
                    return m_codeGenerator.translateVU_VDIV(inst);
                case VU0_S2_VSQRT:
                    return m_codeGenerator.translateVU_VSQRT(inst);
                case VU0_S2_VRSQRT:
                    return m_codeGenerator.translateVU_VRSQRT(inst);
                case VU0_S2_VWAITQ:
                    return fmt::format("// VWAITQ (Q already resolved in this runtime)");
                case VU0_S2_VMTIR:
                    return m_codeGenerator.translateVU_VMTIR(inst);
                case VU0_S2_VMFIR:
                    return m_codeGenerator.translateVU_VMFIR(inst);
                case VU0_S2_VILWR:
                    return m_codeGenerator.translateVU_VILWR(inst);
                case VU0_S2_VISWR:
                    return m_codeGenerator.translateVU_VISWR(inst);
                case VU0_S2_VABS:
                {
                    uint8_t dest_mask = inst.vectorInfo.vectorField;
                    return fmt::format("{{ __m128 res = _mm_and_ps(ctx->vu0_vf[{}], _mm_castsi128_ps(_mm_set1_epi32(0x7FFFFFFF))); "
                                       "__m128i mask = _mm_set_epi32({}, {}, {}, {}); "
                                       "ctx->vu0_vf[{}] = _mm_blendv_ps(ctx->vu0_vf[{}], res, _mm_castsi128_ps(mask)); }}",
                                       inst.rd,
                                       (dest_mask & 0x1) ? -1 : 0, (dest_mask & 0x2) ? -1 : 0,
                                       (dest_mask & 0x4) ? -1 : 0, (dest_mask & 0x8) ? -1 : 0,
                                       inst.rt, inst.rt);
                }
                case VU0_S2_VMOVE:
                {
                    uint8_t dest_mask = inst.vectorInfo.vectorField;
                    return fmt::format(
                        "{{ __m128i mask = _mm_set_epi32({}, {}, {}, {}); "
                        "ctx->vu0_vf[{}] = _mm_blendv_ps(ctx->vu0_vf[{}], ctx->vu0_vf[{}], _mm_castsi128_ps(mask)); }}",
                        (dest_mask & 0x1) ? -1 : 0,
                        (dest_mask & 0x2) ? -1 : 0,
                        (dest_mask & 0x4) ? -1 : 0,
                        (dest_mask & 0x8) ? -1 : 0,
                        inst.rt, inst.rt, inst.rd);
                }
                case VU0_S2_VMR32:
                {
                    uint8_t dest_mask = inst.vectorInfo.vectorField;
                    return fmt::format(
                        "{{ __m128 res = _mm_shuffle_ps(ctx->vu0_vf[{}], ctx->vu0_vf[{}], _MM_SHUFFLE(0,3,2,1)); "
                        "__m128i mask = _mm_set_epi32({}, {}, {}, {}); "
                        "ctx->vu0_vf[{}] = _mm_blendv_ps(ctx->vu0_vf[{}], res, _mm_castsi128_ps(mask)); }}",
                        inst.rd, inst.rd,
                        (dest_mask & 0x1) ? -1 : 0,
                        (dest_mask & 0x2) ? -1 : 0,
                        (dest_mask & 0x4) ? -1 : 0,
                        (dest_mask & 0x8) ? -1 : 0,
                        inst.rt, inst.rt);
                }
                case VU0_S2_VCLIPw:
                {
                    uint8_t field = inst.function & 0x3;
                    std::string shuffle_pattern = fmt::format("_MM_SHUFFLE({},{},{},{})", field, field, field, field);

                    return fmt::format(
                        "{{ __m128 fs = ctx->vu0_vf[{}]; "
                        "__m128 ft = _mm_shuffle_ps(ctx->vu0_vf[{}], ctx->vu0_vf[{}], {}); "
                        "__m128 neg_ft = _mm_xor_ps(ft, _mm_castsi128_ps(_mm_set1_epi32(0x80000000))); "
                        "__m128 gt = _mm_cmpgt_ps(fs, ft); "
                        "__m128 lt = _mm_cmplt_ps(fs, neg_ft); "
                        "uint32_t gt_mask = (uint32_t)_mm_movemask_ps(gt); "
                        "uint32_t lt_mask = (uint32_t)_mm_movemask_ps(lt); "
                        "uint32_t flags = ((lt_mask & 0x1) << 0) | ((gt_mask & 0x1) << 1) | "
                        "((lt_mask & 0x2) << 1) | ((gt_mask & 0x2) << 2) | "
                        "((lt_mask & 0x4) << 2) | ((gt_mask & 0x4) << 3); "
                        "ctx->vu0_clip_flags = ((ctx->vu0_clip_flags << 6) | (flags & 0x3F)) & 0xFFFFFF; }}",
                        inst.rd, inst.rt, inst.rt, shuffle_pattern);
                }
                case VU0_S2_VNOP:
                    return fmt::format("// NOP operation, no action needed for VU0");
                case VU0_S2_VRNEXT:
                    return m_codeGenerator.translateVU_VRNEXT(inst);
                case VU0_S2_VRGET:
                    return m_codeGenerator.translateVU_VRGET(inst);
                case VU0_S2_VRINIT:
                    return m_codeGenerator.translateVU_VRINIT(inst);
                case VU0_S2_VRXOR:
                    return m_codeGenerator.translateVU_VRXOR(inst);
                default:
                    return m_codeGenerator.emitUnhandledInstruction(inst, fmt::format("Unhandled VU0 Special2 function: 0x{:X}", vu_func));
                }
            }

            // Special1 Table (function-based)
            switch (special1_func)
            {
            case VU0_S1_VADDx:
            case VU0_S1_VADDy:
            case VU0_S1_VADDz:
            case VU0_S1_VADDw:
                return m_codeGenerator.translateVU_VADD_Field(inst);
            case VU0_S1_VSUBx:
            case VU0_S1_VSUBy:
            case VU0_S1_VSUBz:
            case VU0_S1_VSUBw:
                return m_codeGenerator.translateVU_VSUB_Field(inst);
            case VU0_S1_VMULx:
            case VU0_S1_VMULy:
            case VU0_S1_VMULz:
            case VU0_S1_VMULw:
                return m_codeGenerator.translateVU_VMUL_Field(inst);
            case VU0_S1_VADD:
                return m_codeGenerator.translateVU_VADD(inst);
            case VU0_S1_VSUB:
                return m_codeGenerator.translateVU_VSUB(inst);
            case VU0_S1_VMUL:
                return m_codeGenerator.translateVU_VMUL(inst);
            case VU0_S1_VIADD:
                return m_codeGenerator.translateVU_VIADD(inst);
            case VU0_S1_VISUB:
                return m_codeGenerator.translateVU_VISUB(inst);
            case VU0_S1_VIADDI:
                return m_codeGenerator.translateVU_VIADDI(inst);
            case VU0_S1_VIAND:
                return m_codeGenerator.translateVU_VIAND(inst);
            case VU0_S1_VIOR:
                return m_codeGenerator.translateVU_VIOR(inst);
            case VU0_S1_VCALLMS:
                return m_codeGenerator.translateVU_VCALLMS(inst);
            case VU0_S1_VCALLMSR:
                return m_codeGenerator.translateVU_VCALLMSR(inst);
            case VU0_S1_VADDq:
                return m_codeGenerator.translateVU_VADDq(inst);
            case VU0_S1_VSUBq:
                return m_codeGenerator.translateVU_VSUBq(inst);
            case VU0_S1_VMULq:
                return m_codeGenerator.translateVU_VMULq(inst);
            case VU0_S1_VADDi:
                return m_codeGenerator.translateVU_VADDi(inst);
            case VU0_S1_VSUBi:
                return m_codeGenerator.translateVU_VSUBi(inst);
            case VU0_S1_VMULi:
                return m_codeGenerator.translateVU_VMULi(inst);
            case VU0_S1_VMADDx:
            case VU0_S1_VMADDy:
            case VU0_S1_VMADDz:
            case VU0_S1_VMADDw:
                return m_codeGenerator.translateVU_VMADD_Field(inst);
            case VU0_S1_VMSUBx:
            case VU0_S1_VMSUBy:
            case VU0_S1_VMSUBz:
            case VU0_S1_VMSUBw:
                return m_codeGenerator.translateVU_VMSUB_Field(inst);
            case VU0_S1_VMAXx:
            case VU0_S1_VMAXy:
            case VU0_S1_VMAXz:
            case VU0_S1_VMAXw:
                return m_codeGenerator.translateVU_VMAX_Field(inst);
            case VU0_S1_VMINIx:
            case VU0_S1_VMINIy:
            case VU0_S1_VMINIz:
            case VU0_S1_VMINIw:
                return m_codeGenerator.translateVU_VMINI_Field(inst);
            case VU0_S1_VMAXi:
                return m_codeGenerator.translateVU_VMAXi(inst);
            case VU0_S1_VMINIi:
                return m_codeGenerator.translateVU_VMINIi(inst);
            case VU0_S1_VMADD:
                return m_codeGenerator.translateVU_VMADD(inst);
            case VU0_S1_VMADDq:
                return m_codeGenerator.translateVU_VMADDq(inst);
            case VU0_S1_VMADDi:
                return m_codeGenerator.translateVU_VMADDi(inst);
            case VU0_S1_VMAX:
                return m_codeGenerator.translateVU_VMAX(inst);
            case VU0_S1_VOPMSUB:
                return m_codeGenerator.translateVU_VOPMSUB(inst);
            case VU0_S1_VMINI:
                return m_codeGenerator.translateVU_VMINI(inst);
            case VU0_S1_VMSUB:
                return m_codeGenerator.translateVU_VMSUB(inst);
            case VU0_S1_VMSUBq:
                return m_codeGenerator.translateVU_VMSUBq(inst);
            case VU0_S1_VMSUBi:
                return m_codeGenerator.translateVU_VMSUBi(inst);
            default:
                return m_codeGenerator.emitUnhandledInstruction(inst, fmt::format("Unhandled VU0 Special1 function: 0x{:X}", special1_func));
            }
        }
        default:
            return m_codeGenerator.emitUnhandledInstruction(inst, fmt::format("Unhandled COP2 format: 0x{:X}", format));
        }
    }

}
