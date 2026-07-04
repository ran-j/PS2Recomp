#include "ps2recomp/code_generator.h"
#include "ps2recomp/codegen_helpers.h"
#include "ps2recomp/instructions.h"
#include "ps2recomp/types.h"
#include <fmt/format.h>
#include <sstream>
#include <cmath>

namespace ps2recomp
{
    std::string CodeGenerator::translateMMI0Instruction(const Instruction &inst)
    {
        uint8_t subfunc = inst.sa;
        uint8_t rs = inst.rs;
        uint8_t rt = inst.rt;
        uint8_t rd = inst.rd;
        switch (subfunc)
        {
        case MMI0_PADDW:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PADDW(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PSUBW:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PSUBW(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PCGTW:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PCGTW(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PMAXW:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PMAXW(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PADDH:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PADDH(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PSUBH:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PSUBH(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PCGTH:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PCGTH(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PMAXH:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PMAXH(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PADDB:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PADDB(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PSUBB:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PSUBB(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PCGTB:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PCGTB(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PADDSW:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PADDSW(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PSUBSW:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PSUBSW(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PEXTLW:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PEXTLW(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PPACW:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PPACW(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PADDSH:
            return fmt::format("SET_GPR_VEC(ctx, {}, _mm_adds_epi16(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PSUBSH:
            return fmt::format("SET_GPR_VEC(ctx, {}, _mm_subs_epi16(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PEXTLH:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PEXTLH(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PPACH:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PPACH(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PADDSB:
            return fmt::format("SET_GPR_VEC(ctx, {}, _mm_adds_epi8(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PSUBSB:
            return fmt::format("SET_GPR_VEC(ctx, {}, _mm_subs_epi8(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PEXTLB:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PEXTLB(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PPACB:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PPACB(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PEXT5:
            return translatePEXT5(inst);
        case MMI0_PPAC5:
            return translatePPAC5(inst);
        default:
            return emitUnhandledInstruction(inst, fmt::format("Unhandled MMI0 instruction: function 0x{:X}", subfunc));
        }
    }


    std::string CodeGenerator::translateMMI1Instruction(const Instruction &inst)
    {
        uint8_t subfunc = inst.sa;
        uint8_t rs = inst.rs;
        uint8_t rt = inst.rt;
        uint8_t rd = inst.rd;
        switch (subfunc)
        {
        case MMI1_PABSW:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PABSW(GPR_VEC(ctx, {})));", rd, rs);
        case MMI1_PCEQW:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PCEQW(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI1_PMINW:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PMINW(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI1_PADSBH:
            return translatePADSBH(inst);
        case MMI1_PABSH:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PABSH(GPR_VEC(ctx, {})));", rd, rs);
        case MMI1_PCEQH:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PCEQH(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI1_PMINH:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PMINH(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI1_PCEQB:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PCEQB(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI1_PADDUW:
            return fmt::format(
                "SET_GPR_VEC(ctx, {}, ps2_paddu32(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI1_PSUBUW:
            return fmt::format(
                "SET_GPR_VEC(ctx, {}, ps2_psubu32(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI1_PEXTUW:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PEXTUW(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI1_PADDUH:
            return fmt::format("SET_GPR_VEC(ctx, {}, _mm_add_epi16(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI1_PSUBUH:
            return fmt::format("SET_GPR_VEC(ctx, {}, _mm_sub_epi16(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI1_PEXTUH:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PEXTUH(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI1_PADDUB:
            return fmt::format("SET_GPR_VEC(ctx, {}, _mm_adds_epu8(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI1_PSUBUB:
            return fmt::format("SET_GPR_VEC(ctx, {}, _mm_subs_epu8(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI1_PEXTUB:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PEXTUB(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI1_QFSRV:
            return translateQFSRV(inst);
        default:
            return emitUnhandledInstruction(inst, fmt::format("Unhandled MMI1 instruction: function 0x{:X}", subfunc));
        }
    }


    std::string CodeGenerator::translateMMI2Instruction(const Instruction &inst)
    {
        uint8_t subfunc = inst.sa;
        uint8_t rs = inst.rs;
        uint8_t rt = inst.rt;
        uint8_t rd = inst.rd;
        switch (subfunc)
        {
        case MMI2_PMADDW:
            return translatePMADDW(inst);
        case MMI2_PSLLVW:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PSLLVW(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI2_PSRLVW:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PSRLVW(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI2_PMSUBW:
            return translatePMSUBW(inst);
        case MMI2_PMFHI:
            return fmt::format("SET_GPR_U64(ctx, {}, ctx->hi);", rd);
        case MMI2_PMFLO:
            return fmt::format("SET_GPR_U64(ctx, {}, ctx->lo);", rd);
        case MMI2_PINTH:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PINTH(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI2_PMULTW:
            return translatePMULTW(inst);
        case MMI2_PDIVW:
            return translatePDIVW(inst);
        case MMI2_PCPYLD:
            return translatePCPYLD(inst);
        case MMI2_PAND:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PAND(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI2_PXOR:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PXOR(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI2_PMADDH:
            return translatePMADDH(inst);
        case MMI2_PHMADH:
            return translatePHMADH(inst);
        case MMI2_PMSUBH:
            return translatePMSUBH(inst);
        case MMI2_PHMSBH:
            return translatePHMSBH(inst);
        case MMI2_PEXEH:
            return translatePEXEH(inst);
        case MMI2_PREVH:
            return translatePREVH(inst);
        case MMI2_PMULTH:
            return translatePMULTH(inst);
        case MMI2_PDIVBW:
            return translatePDIVBW(inst);
        case MMI2_PEXEW:
            return translatePEXEW(inst);
        case MMI2_PROT3W:
            return translatePROT3W(inst);
        default:
            return emitUnhandledInstruction(inst, fmt::format("Unhandled MMI2 instruction: function 0x{:X}", subfunc));
        }
    }


    std::string CodeGenerator::translateMMI3Instruction(const Instruction &inst)
    {
        uint8_t subfunc = inst.sa;
        uint8_t rs = inst.rs;
        uint8_t rt = inst.rt;
        uint8_t rd = inst.rd;
        switch (subfunc)
        {
        case MMI3_PMADDUW:
            return translatePMADDUW(inst);
        case MMI3_PSRAVW:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PSRAVW(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI3_PMTHI:
            return translatePMTHI(inst);
        case MMI3_PMTLO:
            return translatePMTLO(inst);
        case MMI3_PINTEH:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PINTEH(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI3_PMULTUW:
            return translatePMULTUW(inst);
        case MMI3_PDIVUW:
            return translatePDIVUW(inst);
        case MMI3_PCPYUD:
            return translatePCPYUD(inst);
        case MMI3_POR:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_POR(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI3_PNOR:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PNOR(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI3_PEXCH:
            return translatePEXCH(inst);
        case MMI3_PCPYH:
            return translatePCPYH(inst);
        case MMI3_PEXCW:
            return translatePEXCW(inst);
        default:
            return emitUnhandledInstruction(inst, fmt::format("Unhandled MMI3 instruction: function 0x{:X}", subfunc));
        }
    }


    std::string CodeGenerator::translatePMFHLInstruction(const Instruction &inst)
    {
        uint8_t subfunc = inst.sa;
        switch (subfunc)
        {
        case PMFHL_LW:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PMFHL_LW(ctx->hi, ctx->lo));", inst.rd);
        case PMFHL_UW:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PMFHL_UW(ctx->hi, ctx->lo));", inst.rd);
        case PMFHL_SLW:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PMFHL_SLW(ctx->hi, ctx->lo));", inst.rd);
        case PMFHL_LH:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PMFHL_LH(ctx->hi, ctx->lo));", inst.rd);
        case PMFHL_SH:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PMFHL_SH(ctx->hi, ctx->lo));", inst.rd);
        default:
            return emitUnhandledInstruction(inst, fmt::format("Unhandled PMFHL instruction: function 0x{:X}", subfunc));
        }
    }


    std::string CodeGenerator::translatePMTHLInstruction(const Instruction &inst)
    {
        uint8_t subfunc = inst.sa;
        switch (subfunc)
        {
        case PMFHL_LW:
            return fmt::format("{{ __m128i val = GPR_VEC(ctx, {}); ctx->lo = _mm_extract_epi32(val, 0); ctx->hi = _mm_extract_epi32(val, 1); }}", inst.rs);
        default:
            return emitUnhandledInstruction(inst, fmt::format("Unhandled PMTHL instruction: function 0x{:X}", subfunc));
        }
    }


    std::string CodeGenerator::translatePEXT5(const Instruction &inst)
    {
        return fmt::format(
            "{{ __m128i rt = GPR_VEC(ctx, {}); \n"
            "   __m128i m1 = _mm_set1_epi32(0x0000001F); \n"
            "   __m128i m2 = _mm_set1_epi32(0x000003E0); \n"
            "   __m128i m3 = _mm_set1_epi32(0x00007C00); \n"
            "   __m128i m4 = _mm_set1_epi32(0x00008000); \n"
            "   __m128i a1 = _mm_slli_epi32(_mm_and_si128(rt, m1), 3); \n"
            "   __m128i a2 = _mm_slli_epi32(_mm_and_si128(rt, m2), 6); \n"
            "   __m128i a3 = _mm_slli_epi32(_mm_and_si128(rt, m3), 9); \n"
            "   __m128i a4 = _mm_slli_epi32(_mm_and_si128(rt, m4), 16); \n"
            "   SET_GPR_VEC(ctx, {}, _mm_or_si128(_mm_or_si128(a1, a2), _mm_or_si128(a3, a4))); }}",
            inst.rt, inst.rd);
    }


    std::string CodeGenerator::translatePPAC5(const Instruction &inst)
    {
        return fmt::format(
            "{{ __m128i rt = GPR_VEC(ctx, {}); \n"
            "   __m128i m1 = _mm_set1_epi32(0x0000001F); \n"
            "   __m128i m2 = _mm_set1_epi32(0x000003E0); \n"
            "   __m128i m3 = _mm_set1_epi32(0x00007C00); \n"
            "   __m128i m4 = _mm_set1_epi32(0x00008000); \n"
            "   __m128i a1 = _mm_and_si128(_mm_srli_epi32(rt, 3), m1); \n"
            "   __m128i a2 = _mm_and_si128(_mm_srli_epi32(rt, 6), m2); \n"
            "   __m128i a3 = _mm_and_si128(_mm_srli_epi32(rt, 9), m3); \n"
            "   __m128i a4 = _mm_and_si128(_mm_srli_epi32(rt, 16), m4); \n"
            "   SET_GPR_VEC(ctx, {}, _mm_or_si128(_mm_or_si128(a1, a2), _mm_or_si128(a3, a4))); }}",
            inst.rt, inst.rd);
    }


    std::string CodeGenerator::translatePADSBH(const Instruction &inst)
    {
        return fmt::format(
            "{{ __m128i rs = GPR_VEC(ctx, {}); __m128i rt = GPR_VEC(ctx, {}); \n"
            "   __m128i sub = _mm_sub_epi16(rs, rt); \n"
            "   __m128i add = _mm_add_epi16(rs, rt); \n"
            "   SET_GPR_VEC(ctx, {}, _mm_unpacklo_epi64(sub, _mm_unpackhi_epi64(add, add))); }}",
            inst.rs, inst.rt, inst.rd);
    }


    std::string CodeGenerator::translatePMADDW(const Instruction &inst)
    {
        return fmt::format("{{ __m128i p01 = _mm_mul_epu32(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})); \n"
                           "   __m128i p23 = _mm_mul_epu32(_mm_srli_si128(GPR_VEC(ctx, {}), 8), _mm_srli_si128(GPR_VEC(ctx, {}), 8)); \n"
                           "   uint64_t acc = Ps2HiLoToU64(ctx->hi, ctx->lo); \n"
                           "   acc += _mm_cvtsi128_si64(p01); \n"
                           "   acc += _mm_cvtsi128_si64(_mm_srli_si128(p01, 8)); \n"
                           "   acc += _mm_cvtsi128_si64(p23); \n"
                           "   acc += _mm_cvtsi128_si64(_mm_srli_si128(p23, 8)); \n"
                           "   ctx->lo = (uint32_t)acc; ctx->hi = (uint32_t)(acc >> 32); \n"
                           "   SET_GPR_U64(ctx, {}, acc); }}",
                           inst.rs, inst.rt, inst.rs, inst.rt, inst.rd);
    }


    std::string CodeGenerator::translatePMSUBW(const Instruction &inst)
    {
        return fmt::format("{{ __m128i p01 = _mm_mul_epu32(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})); \n"
                           "   __m128i p23 = _mm_mul_epu32(_mm_srli_si128(GPR_VEC(ctx, {}), 8), _mm_srli_si128(GPR_VEC(ctx, {}), 8)); \n"
                           "   uint64_t acc = Ps2HiLoToU64(ctx->hi, ctx->lo); \n"
                           "   acc -= _mm_cvtsi128_si64(p01); \n"
                           "   acc -= _mm_cvtsi128_si64(_mm_srli_si128(p01, 8)); \n"
                           "   acc -= _mm_cvtsi128_si64(p23); \n"
                           "   acc -= _mm_cvtsi128_si64(_mm_srli_si128(p23, 8)); \n"
                           "   ctx->lo = (uint32_t)acc; ctx->hi = (uint32_t)(acc >> 32); \n"
                           "   SET_GPR_U64(ctx, {}, acc); }}",
                           inst.rs, inst.rt, inst.rs, inst.rt, inst.rd);
    }


    std::string CodeGenerator::translatePMULTW(const Instruction &inst)
    {
        return fmt::format("{{ __m128i p01 = _mm_mul_epu32(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})); \n"
                           "   __m128i p23 = _mm_mul_epu32(_mm_srli_si128(GPR_VEC(ctx, {}), 8), _mm_srli_si128(GPR_VEC(ctx, {}), 8)); \n"
                           "   uint64_t acc = 0; \n"
                           "   acc += _mm_cvtsi128_si64(p01); \n"
                           "   acc += _mm_cvtsi128_si64(_mm_srli_si128(p01, 8)); \n"
                           "   acc += _mm_cvtsi128_si64(p23); \n"
                           "   acc += _mm_cvtsi128_si64(_mm_srli_si128(p23, 8)); \n"
                           "   ctx->lo = (uint32_t)acc; ctx->hi = (uint32_t)(acc >> 32); \n"
                           "   SET_GPR_U64(ctx, {}, acc); }}",
                           inst.rs, inst.rt, inst.rs, inst.rt, inst.rd);
    }


    std::string CodeGenerator::translatePMADDUW(const Instruction &inst)
    {
        return fmt::format("{{ __m128i p01 = _mm_mul_epu32(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})); \n"
                           "   __m128i p23 = _mm_mul_epu32(_mm_srli_si128(GPR_VEC(ctx, {}), 8), _mm_srli_si128(GPR_VEC(ctx, {}), 8)); \n"
                           "   uint64_t acc = Ps2HiLoToU64(ctx->hi, ctx->lo); \n"
                           "   acc += _mm_cvtsi128_si64(p01); \n"
                           "   acc += _mm_cvtsi128_si64(_mm_srli_si128(p01, 8)); \n"
                           "   acc += _mm_cvtsi128_si64(p23); \n"
                           "   acc += _mm_cvtsi128_si64(_mm_srli_si128(p23, 8)); \n"
                           "   ctx->lo = (uint32_t)acc; ctx->hi = (uint32_t)(acc >> 32); \n"
                           "   SET_GPR_U64(ctx, {}, acc); }}",
                           inst.rs, inst.rt, inst.rs, inst.rt, inst.rd);
    }


    std::string CodeGenerator::translatePDIVW(const Instruction &inst)
    {
        // Only divides the first word element rs[0] / rt[0]
        return fmt::format("{{ int32_t rs0 = GPR_S32(ctx, {}); int32_t rt0 = GPR_S32(ctx, {}); \n"
                           "   if (rt0 != 0) {{ \n"
                           "       if (rt0 == -1 && rs0 == INT32_MIN) {{ ctx->lo = (uint32_t)INT32_MIN; ctx->hi = 0; }} \n"
                           "       else {{ ctx->lo = (uint32_t)(rs0 / rt0); ctx->hi = (uint32_t)(rs0 % rt0); }} \n"
                           "   }} else {{ ctx->lo = (rs0 < 0) ? 1 : -1; ctx->hi = (uint32_t)rs0; }} \n"
                           "   SET_GPR_U32(ctx, {}, ctx->lo); }}",
                           inst.rs, inst.rt, inst.rd);
    }


    std::string CodeGenerator::translatePCPYLD(const Instruction &inst)
    {
        // PCPYLD uses rs as the upper source and rt as the lower source.
        return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PCPYLD(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));",
                           inst.rd, inst.rs, inst.rt);
    }


    std::string CodeGenerator::translatePMADDH(const Instruction &inst)
    {
        // Parallel multiply add halfword -> results to HI/LO and rd
        return fmt::format("{{ __m128i prod = _mm_madd_epi16(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})); \n" // Packed multiply and add adjacent pairs
                           "   int32_t p0 = _mm_cvtsi128_si32(prod); \n"
                           "   int32_t p1 = _mm_cvtsi128_si32(_mm_srli_si128(prod, 4)); \n"
                           "   int32_t p2 = _mm_cvtsi128_si32(_mm_srli_si128(prod, 8)); \n"
                           "   int32_t p3 = _mm_cvtsi128_si32(_mm_srli_si128(prod, 12)); \n"
                           "   int64_t acc = Ps2HiLoToU64(ctx->hi, ctx->lo); \n"
                           "   acc += (int64_t)p0 + (int64_t)p1 + (int64_t)p2 + (int64_t)p3; \n"
                           "   ctx->lo = (uint32_t)acc; ctx->hi = (uint32_t)(acc >> 32); \n"
                           "   SET_GPR_U64(ctx, {}, acc); }}",
                           inst.rs, inst.rt, inst.rd);
    }


    std::string CodeGenerator::translatePHMADH(const Instruction &inst)
    {
        // Parallel Horizontal Multiply Add Halfword -> results to HI/LO and rd
        return fmt::format("{{ __m128i evens = _mm_shuffle_epi32(GPR_VEC(ctx, {}), _MM_SHUFFLE(2,0,2,0)); \n" // Select even halfwords
                           "   __m128i odds  = _mm_shuffle_epi32(GPR_VEC(ctx, {}), _MM_SHUFFLE(3,1,3,1)); \n" // Select odd halfwords
                           "   __m128i prod_ev = _mm_mullo_epi16(evens, _mm_shuffle_epi32(GPR_VEC(ctx, {}), _MM_SHUFFLE(2,0,2,0))); \n"
                           "   __m128i prod_od = _mm_mullo_epi16(odds,  _mm_shuffle_epi32(GPR_VEC(ctx, {}), _MM_SHUFFLE(3,1,3,1))); \n"
                           "   __m128i sum_pairs = _mm_add_epi16(prod_ev, prod_od); \n"                            // Add rs[0]*rt[0] + rs[1]*rt[1], etc.
                           "   int32_t h0 = _mm_extract_epi16(sum_pairs, 0) + _mm_extract_epi16(sum_pairs, 1); \n" // Horizontal add within low 32b
                           "   int32_t h1 = _mm_extract_epi16(sum_pairs, 2) + _mm_extract_epi16(sum_pairs, 3); \n" // Horizontal add within next 32b
                           "   int32_t h2 = _mm_extract_epi16(sum_pairs, 4) + _mm_extract_epi16(sum_pairs, 5); \n"
                           "   int32_t h3 = _mm_extract_epi16(sum_pairs, 6) + _mm_extract_epi16(sum_pairs, 7); \n"
                           "   int64_t acc = Ps2HiLoToU64(ctx->hi, ctx->lo); \n"
                           "   acc += (int64_t)h0 + (int64_t)h1 + (int64_t)h2 + (int64_t)h3; \n"
                           "   ctx->lo = (uint32_t)acc; ctx->hi = (uint32_t)(acc >> 32); \n"
                           "   SET_GPR_U64(ctx, {}, acc); }}",
                           inst.rs, inst.rt, inst.rs, inst.rt, inst.rd);
    }


    std::string CodeGenerator::translatePMSUBH(const Instruction &inst)
    {
        return fmt::format("{{ __m128i prod = _mm_madd_epi16(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})); \n"
                           "   int32_t p0 = _mm_cvtsi128_si32(prod); \n"
                           "   int32_t p1 = _mm_cvtsi128_si32(_mm_srli_si128(prod, 4)); \n"
                           "   int32_t p2 = _mm_cvtsi128_si32(_mm_srli_si128(prod, 8)); \n"
                           "   int32_t p3 = _mm_cvtsi128_si32(_mm_srli_si128(prod, 12)); \n"
                           "   int64_t acc = Ps2HiLoToU64(ctx->hi, ctx->lo); \n"
                           "   acc -= (int64_t)p0 + (int64_t)p1 + (int64_t)p2 + (int64_t)p3; \n"
                           "   ctx->lo = (uint32_t)acc; ctx->hi = (uint32_t)(acc >> 32); \n"
                           "   SET_GPR_U64(ctx, {}, acc); }}",
                           inst.rs, inst.rt, inst.rd);
    }


    std::string CodeGenerator::translatePHMSBH(const Instruction &inst)
    {
        return fmt::format("{{ __m128i evens = _mm_shuffle_epi32(GPR_VEC(ctx, {}), _MM_SHUFFLE(2,0,2,0)); \n"
                           "   __m128i odds  = _mm_shuffle_epi32(GPR_VEC(ctx, {}), _MM_SHUFFLE(3,1,3,1)); \n"
                           "   __m128i prod_ev = _mm_mullo_epi16(evens, _mm_shuffle_epi32(GPR_VEC(ctx, {}), _MM_SHUFFLE(2,0,2,0))); \n"
                           "   __m128i prod_od = _mm_mullo_epi16(odds,  _mm_shuffle_epi32(GPR_VEC(ctx, {}), _MM_SHUFFLE(3,1,3,1))); \n"
                           "   __m128i sub_pairs = _mm_sub_epi16(prod_od, prod_ev); \n"
                           "   int32_t h0 = _mm_extract_epi16(sub_pairs, 0) + _mm_extract_epi16(sub_pairs, 1); \n"
                           "   int32_t h1 = _mm_extract_epi16(sub_pairs, 2) + _mm_extract_epi16(sub_pairs, 3); \n"
                           "   int32_t h2 = _mm_extract_epi16(sub_pairs, 4) + _mm_extract_epi16(sub_pairs, 5); \n"
                           "   int32_t h3 = _mm_extract_epi16(sub_pairs, 6) + _mm_extract_epi16(sub_pairs, 7); \n"
                           "   int64_t acc = Ps2HiLoToU64(ctx->hi, ctx->lo); \n"
                           "   acc += (int64_t)h0 + (int64_t)h1 + (int64_t)h2 + (int64_t)h3; \n"
                           "   ctx->lo = (uint32_t)acc; ctx->hi = (uint32_t)(acc >> 32); \n"
                           "   SET_GPR_U64(ctx, {}, acc); }}",
                           inst.rs, inst.rt, inst.rs, inst.rt, inst.rd);
    }


    std::string CodeGenerator::translatePEXEH(const Instruction &inst)
    {
        // Swaps halfwords 1<->3 and 5<->7 within the 128-bit register
        return fmt::format("SET_GPR_VEC(ctx, {}, _mm_shufflelo_epi16(_mm_shufflehi_epi16(GPR_VEC(ctx, {}), _MM_SHUFFLE(2,3,0,1)), _MM_SHUFFLE(2,3,0,1)));",
                           inst.rd, inst.rs);
    }


    std::string CodeGenerator::translatePREVH(const Instruction &inst)
    {
        // Reverses the order of the 8 halfwords
        return fmt::format("{{ __m128i mask = _mm_setr_epi8(14,15, 12,13, 10,11, 8,9, 6,7, 4,5, 2,3, 0,1); "
                           "SET_GPR_VEC(ctx, {}, PS2_SHUFFLE_EPI8(GPR_VEC(ctx, {}), mask)); }}",
                           inst.rd, inst.rs);
    }


    std::string CodeGenerator::translatePMULTH(const Instruction &inst)
    {
        // Parallel multiply halfword, results sum to HI/LO and rd
        return fmt::format("{{ __m128i prod = _mm_madd_epi16(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})); \n"
                           "   int32_t p0 = _mm_cvtsi128_si32(prod); \n"
                           "   int32_t p1 = _mm_cvtsi128_si32(_mm_srli_si128(prod, 4)); \n"
                           "   int32_t p2 = _mm_cvtsi128_si32(_mm_srli_si128(prod, 8)); \n"
                           "   int32_t p3 = _mm_cvtsi128_si32(_mm_srli_si128(prod, 12)); \n"
                           "   int64_t result = (int64_t)p0 + (int64_t)p1 + (int64_t)p2 + (int64_t)p3; \n"
                           "   ctx->lo = (uint32_t)result; ctx->hi = (uint32_t)(result >> 32); \n"
                           "   SET_GPR_U64(ctx, {}, result); }}",
                           inst.rs, inst.rt, inst.rd);
    }


    std::string CodeGenerator::translatePDIVBW(const Instruction &inst)
    {
        return fmt::format(
            "{{\n"
            "    __m128i rsVec = GPR_VEC(ctx, {});\n"
            "    __m128i rtVec = GPR_VEC(ctx, {});\n"
            "    alignas(16) int32_t rsWords[4];\n"
            "    alignas(16) int32_t rtWords[4];\n"
            "    _mm_store_si128((__m128i*)rsWords, rsVec);\n"
            "    _mm_store_si128((__m128i*)rtWords, rtVec);\n"
            "    int32_t div = rtWords[0];\n"
            "    int32_t q0 = 0, q1 = 0, q2 = 0, q3 = 0;\n"
            "    if (div != 0) {{\n"
            "        q0 = rsWords[0] / div; ctx->lo = (uint32_t)q0; ctx->hi = (uint32_t)(rsWords[0] % div);\n"
            "        q1 = rsWords[1] / div;\n"
            "        q2 = rsWords[2] / div;\n"
            "        q3 = rsWords[3] / div;\n"
            "    }} else {{\n"
            "        ctx->lo = (rsWords[0] < 0) ? 1 : -1;\n"
            "        ctx->hi = (uint32_t)rsWords[0];\n"
            "    }}\n"
            "    SET_GPR_VEC(ctx, {}, _mm_set_epi32(q3, q2, q1, q0));\n"
            "}}",
            inst.rs, inst.rt, inst.rd);
    }


    std::string CodeGenerator::translatePEXEW(const Instruction &inst)
    {
        return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PEXEW(GPR_VEC(ctx, {})));",
                           inst.rd, inst.rs);
    }


    std::string CodeGenerator::translatePROT3W(const Instruction &inst)
    {
        // Rotates words left by 3: [d,c,b,a] -> [a,d,c,b]
        return fmt::format("SET_GPR_VEC(ctx, {}, _mm_shuffle_epi32(GPR_VEC(ctx, {}), _MM_SHUFFLE(0,3,2,1)));",
                           inst.rd, inst.rs);
    }


    std::string CodeGenerator::translatePMULTUW(const Instruction &inst)
    {
        // Parallel multiply unsigned word -> results to HI/LO and rd (lower 32 bits)
        return fmt::format("{{ __m128i p01 = _mm_mul_epu32(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})); \n"
                           "   __m128i p23 = _mm_mul_epu32(_mm_srli_si128(GPR_VEC(ctx, {}), 8), _mm_srli_si128(GPR_VEC(ctx, {}), 8)); \n"
                           "   uint64_t res0 = _mm_cvtsi128_si64(p01); uint64_t res1 = _mm_cvtsi128_si64(_mm_srli_si128(p01, 8)); \n"
                           "   uint64_t res2 = _mm_cvtsi128_si64(p23); uint64_t res3 = _mm_cvtsi128_si64(_mm_srli_si128(p23, 8)); \n"
                           "   ctx->lo = (uint32_t)res0; ctx->hi = (uint32_t)(res0 >> 32); \n" // HI/LO from first product only
                           "   SET_GPR_VEC(ctx, {}, _mm_set_epi32((uint32_t)res3, (uint32_t)res2, (uint32_t)res1, (uint32_t)res0)); }}",
                           inst.rs, inst.rt, inst.rs, inst.rt, inst.rd);
    }


    std::string CodeGenerator::translatePDIVUW(const Instruction &inst)
    {
        // Parallel divide unsigned word (only first element) -> results to HI/LO and rd (quotient)
        return fmt::format("{{ uint32_t rs0 = GPR_U32(ctx, {}); uint32_t rt0 = GPR_U32(ctx, {}); \n"
                           "   if (rt0 != 0) {{ ctx->lo = rs0 / rt0; ctx->hi = rs0 % rt0; }} \n"
                           "   else {{ ctx->lo = 0xFFFFFFFF; ctx->hi = rs0; }} \n" // Div by zero behavior
                           "   SET_GPR_U32(ctx, {}, ctx->lo); }}",
                           inst.rs, inst.rt, inst.rd);
    }


    std::string CodeGenerator::translatePCPYUD(const Instruction &inst)
    {
        // Copies upper 64 of rs to lower 64 of rd, upper 64 of rt to upper 64 of rd
        return fmt::format("SET_GPR_VEC(ctx, {}, _mm_unpackhi_epi64(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));",
                           inst.rd, inst.rs, inst.rt); // Order matters
    }


    std::string CodeGenerator::translatePEXCH(const Instruction &inst)
    {
        // Parallel Exchange Center Halfword (same as MMI2 PEXEH)
        return fmt::format("SET_GPR_VEC(ctx, {}, _mm_shufflelo_epi16(_mm_shufflehi_epi16(GPR_VEC(ctx, {}), _MM_SHUFFLE(2,3,0,1)), _MM_SHUFFLE(2,3,0,1)));",
                           inst.rd, inst.rs);
    }


    std::string CodeGenerator::translatePCPYH(const Instruction &inst)
    {
        // Parallel Copy Halfword (Broadcast lower 16 bits of each 64-bit half)
        return fmt::format("{{ __m128i src = GPR_VEC(ctx, {}); uint16_t l = _mm_extract_epi16(src, 0); uint16_t h = _mm_extract_epi16(src, 4); \n"
                           "   SET_GPR_VEC(ctx, {}, _mm_set_epi16(h,h,h,h, l,l,l,l)); }}",
                           inst.rs, inst.rd);
    }


    std::string CodeGenerator::translatePEXCW(const Instruction &inst)
    {
        // Parallel Exchange Center Word (Swaps words 0<>2, 1<>3)
        return fmt::format("SET_GPR_VEC(ctx, {}, _mm_shuffle_epi32(GPR_VEC(ctx, {}), _MM_SHUFFLE(1,0,3,2)));",
                           inst.rd, inst.rs);
    }


    std::string CodeGenerator::translatePMTHI(const Instruction &inst)
    {
        return fmt::format("ctx->hi = GPR_U32(ctx, {});", inst.rs); // PMTHI uses standard HI/LO
    }


    std::string CodeGenerator::translatePMTLO(const Instruction &inst)
    {
        return fmt::format("ctx->lo = GPR_U32(ctx, {});", inst.rs); // PMTLO uses standard HI/LO
    }


    std::string CodeGenerator::translateQFSRV(const Instruction &inst)
    {
        uint8_t rd = inst.rd;
        uint8_t rs = inst.rs;
        uint8_t rt = inst.rt;
        // QFSRV semantics are centralized in runtime macro helpers.
        return fmt::format("SET_GPR_VEC(ctx, {}, PS2_QFSRV(GPR_VEC(ctx, {}), GPR_VEC(ctx, {}), ctx->sa & 0x7F));",
                           rd, rs, rt);
    }

}
