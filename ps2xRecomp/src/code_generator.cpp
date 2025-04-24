#include "ps2recomp/code_generator.h"
#include "ps2recomp/instructions.h"
#include <fmt/format.h>
#include <sstream>
#include <algorithm>
#include <unordered_set>

namespace ps2recomp
{
    CodeGenerator::CodeGenerator(const std::vector<Symbol> &symbols)
        : m_symbols(symbols)
    {
    }

    std::string CodeGenerator::handleBranchDelaySlots(const Instruction &branchInst, const Instruction &delaySlot)
    {
        std::stringstream ss;
        bool hasValidDelaySlot = (delaySlot.raw != 0);
        std::string delaySlotCode = hasValidDelaySlot ? translateInstruction(delaySlot) : "";
        uint8_t rs_reg = branchInst.rs;
        uint8_t rt_reg = branchInst.rt;
        uint8_t rd_reg = branchInst.rd;

        if (branchInst.opcode == OPCODE_J || branchInst.opcode == OPCODE_JAL)
        {
            if (branchInst.opcode == OPCODE_JAL)
            {
                ss << "    SET_GPR_U32(ctx, 31, 0x" << std::hex << (branchInst.address + 8) << ");\n"
                   << std::dec;
            }
            if (hasValidDelaySlot)
            {
                ss << "    " << delaySlotCode << "\n";
            }
            uint32_t target = (branchInst.address & 0xF0000000) | (branchInst.target << 2);
            Symbol *sym = findSymbolByAddress(target);
            if (sym && sym->isFunction)
            {
                ss << "    " << sym->name << "(rdram, ctx); return;\n";
            }
            else
            {
                ss << "    ctx->pc = 0x" << std::hex << target << "; return;\n"
                   << std::dec;
            }
        }
        else if (branchInst.opcode == OPCODE_SPECIAL &&
                 (branchInst.function == SPECIAL_JR || branchInst.function == SPECIAL_JALR))
        {
            uint8_t link_reg = (branchInst.function == SPECIAL_JALR) ? ((rd_reg == 0) ? 31 : rd_reg) : 0;
            if (link_reg != 0)
            {
                ss << "    SET_GPR_U32(ctx, " << (int)link_reg << ", 0x" << std::hex << (branchInst.address + 8) << ");\n"
                   << std::dec;
            }
            if (hasValidDelaySlot)
            {
                ss << "    " << delaySlotCode << "\n";
            }
            if (rs_reg == 31 && branchInst.function == SPECIAL_JR)
            {
                ss << "    return;\n";
            }
            else
            {
                ss << "    ctx->pc = GPR_U32(ctx, " << (int)rs_reg << "); return;\n";
            }
        }
        else if (branchInst.isBranch)
        {
            std::string conditionStr = "false";
            std::string linkCode = "";

            switch (branchInst.opcode)
            {
            case OPCODE_BEQ:
                conditionStr = fmt::format("GPR_U32(ctx, {}) == GPR_U32(ctx, {})", rs_reg, rt_reg);
                break;
            case OPCODE_BNE:
                conditionStr = fmt::format("GPR_U32(ctx, {}) != GPR_U32(ctx, {})", rs_reg, rt_reg);
                break;
            case OPCODE_BLEZ:
                conditionStr = fmt::format("GPR_S32(ctx, {}) <= 0", rs_reg);
                break;
            case OPCODE_BGTZ:
                conditionStr = fmt::format("GPR_S32(ctx, {}) > 0", rs_reg);
                break;
            case OPCODE_BEQL:
                conditionStr = fmt::format("GPR_U32(ctx, {}) == GPR_U32(ctx, {})", rs_reg, rt_reg);
                break;
            case OPCODE_BNEL:
                conditionStr = fmt::format("GPR_U32(ctx, {}) != GPR_U32(ctx, {})", rs_reg, rt_reg);
                break;
            case OPCODE_BLEZL:
                conditionStr = fmt::format("GPR_S32(ctx, {}) <= 0", rs_reg);
                break;
            case OPCODE_BGTZL:
                conditionStr = fmt::format("GPR_S32(ctx, {}) > 0", rs_reg);
                break;
            case OPCODE_REGIMM:
                switch (rt_reg)
                {
                case REGIMM_BLTZ:
                    conditionStr = fmt::format("GPR_S32(ctx, {}) < 0", rs_reg);
                    break;
                case REGIMM_BGEZ:
                    conditionStr = fmt::format("GPR_S32(ctx, {}) >= 0", rs_reg);
                    break;
                case REGIMM_BLTZL:
                    conditionStr = fmt::format("GPR_S32(ctx, {}) < 0", rs_reg);
                    break;
                case REGIMM_BGEZL:
                    conditionStr = fmt::format("GPR_S32(ctx, {}) >= 0", rs_reg);
                    break;
                case REGIMM_BLTZAL:
                    conditionStr = fmt::format("GPR_S32(ctx, {}) < 0", rs_reg);
                    linkCode = fmt::format("SET_GPR_U32(ctx, 31, 0x{:X});", branchInst.address + 8);
                    break;
                case REGIMM_BGEZAL:
                    conditionStr = fmt::format("GPR_S32(ctx, {}) >= 0", rs_reg);
                    linkCode = fmt::format("SET_GPR_U32(ctx, 31, 0x{:X});", branchInst.address + 8);
                    break;
                case REGIMM_BLTZALL:
                    conditionStr = fmt::format("GPR_S32(ctx, {}) < 0", rs_reg);
                    linkCode = fmt::format("SET_GPR_U32(ctx, 31, 0x{:X});", branchInst.address + 8);
                    break;
                case REGIMM_BGEZALL:
                    conditionStr = fmt::format("GPR_S32(ctx, {}) >= 0", rs_reg);
                    linkCode = fmt::format("SET_GPR_U32(ctx, 31, 0x{:X});", branchInst.address + 8);
                    break;
                }
                break;
            case OPCODE_COP1:
                if (branchInst.rs == COP1_BC)
                {
                    uint8_t bc_cond = branchInst.rt;
                    if (bc_cond == COP1_BC_BCF || bc_cond == COP1_BC_BCFL)
                    {
                        conditionStr = "!(ctx->fcr31 & 0x800000)";
                    }
                    else
                    {
                        conditionStr = "(ctx->fcr31 & 0x800000)";
                    }
                }
                break;
            case OPCODE_COP2:
                if (branchInst.rs == COP2_BC)
                {
                    uint8_t bc_cond = branchInst.rt;
                    if (bc_cond == COP2_BC_BCF || bc_cond == COP2_BC_BCFL)
                    {
                        conditionStr = "!(ctx->vu0_status & 0x1)";
                    }
                    else
                    {
                        conditionStr = "(ctx->vu0_status & 0x1)";
                    }
                }
                break;
            }

            int32_t offset = branchInst.simmediate << 2;
            uint32_t target = branchInst.address + 4 + offset;

            Symbol *sym = findSymbolByAddress(target);
            std::string targetAction;

            if (sym && sym->isFunction)
            {
                targetAction = fmt::format("{}(rdram, ctx); return;", sym->name);
            }
            else
            {
                targetAction = fmt::format("ctx->pc = 0x{:X}; return;", target);
            }

            bool isLikely = (branchInst.opcode == OPCODE_BEQL || branchInst.opcode == OPCODE_BNEL ||
                             branchInst.opcode == OPCODE_BLEZL || branchInst.opcode == OPCODE_BGTZL ||
                             (branchInst.opcode == OPCODE_REGIMM && (branchInst.rt == REGIMM_BLTZL || branchInst.rt == REGIMM_BGEZL || branchInst.rt == REGIMM_BLTZALL || branchInst.rt == REGIMM_BGEZALL)) ||
                             (branchInst.opcode == OPCODE_COP1 && branchInst.rs == COP1_BC && (branchInst.rt == COP1_BC_BCFL || branchInst.rt == COP1_BC_BCTL)) ||
                             (branchInst.opcode == OPCODE_COP2 && branchInst.rs == COP2_BC && (branchInst.rt == COP2_BC_BCFL || branchInst.rt == COP2_BC_BCTL)));

            if (linkCode != "")
            {
                ss << "    " << linkCode << "\n";
            }

            if (isLikely)
            {
                ss << "    if (" << conditionStr << ") {\n";
                if (hasValidDelaySlot)
                {
                    ss << "        " << delaySlotCode << "\n";
                }
                ss << "        " << targetAction << "\n";
                ss << "    }\n";
            }
            else
            {
                if (hasValidDelaySlot)
                {
                    ss << "    " << delaySlotCode << "\n";
                }
                ss << "    if (" << conditionStr << ") {\n";
                ss << "        " << targetAction << "\n";
                ss << "    }\n";
            }
        }
        else
        {
            ss << "    " << translateInstruction(branchInst) << "\n";
            if (hasValidDelaySlot)
            {
                ss << "    " << delaySlotCode << "\n";
            }
        }
        return ss.str();
    }

    CodeGenerator::~CodeGenerator() = default;

    std::string CodeGenerator::generateMacroHeader()
    {
        std::stringstream ss;

        ss << "#ifndef PS2_RUNTIME_MACROS_H\n";
        ss << "#define PS2_RUNTIME_MACROS_H\n\n";
        ss << "#include <cstdint>\n";
        ss << "#include <immintrin.h> // For SSE/AVX intrinsics\n\n";

        ss << "// Basic MIPS arithmetic operations\n";
        ss << "#define ADD32(a, b) ((uint32_t)((a) + (b)))\n";
        ss << "#define SUB32(a, b) ((uint32_t)((a) - (b)))\n";
        ss << "#define MUL32(a, b) ((uint32_t)((a) * (b)))\n";
        ss << "#define DIV32(a, b) ((uint32_t)((a) / (b)))\n";
        ss << "#define AND32(a, b) ((uint32_t)((a) & (b)))\n";
        ss << "#define OR32(a, b) ((uint32_t)((a) | (b)))\n";
        ss << "#define XOR32(a, b) ((uint32_t)((a) ^ (b)))\n";
        ss << "#define NOR32(a, b) ((uint32_t)(~((a) | (b))))\n";
        ss << "#define SLL32(a, b) ((uint32_t)((a) << (b)))\n";
        ss << "#define SRL32(a, b) ((uint32_t)((a) >> (b)))\n";
        ss << "#define SRA32(a, b) ((uint32_t)((int32_t)(a) >> (b)))\n";
        ss << "#define SLT32(a, b) ((uint32_t)((int32_t)(a) < (int32_t)(b) ? 1 : 0))\n";
        ss << "#define SLTU32(a, b) ((uint32_t)((a) < (b) ? 1 : 0))\n\n";

        ss << "// PS2-specific 128-bit MMI operations\n";
        ss << "#define PS2_PEXTLW(a, b) _mm_unpacklo_epi32((__m128i)(b), (__m128i)(a))\n";
        ss << "#define PS2_PEXTUW(a, b) _mm_unpackhi_epi32((__m128i)(b), (__m128i)(a))\n";
        ss << "#define PS2_PEXTLH(a, b) _mm_unpacklo_epi16((__m128i)(b), (__m128i)(a))\n";
        ss << "#define PS2_PEXTUH(a, b) _mm_unpackhi_epi16((__m128i)(b), (__m128i)(a))\n";
        ss << "#define PS2_PEXTLB(a, b) _mm_unpacklo_epi8((__m128i)(b), (__m128i)(a))\n";
        ss << "#define PS2_PEXTUB(a, b) _mm_unpackhi_epi8((__m128i)(b), (__m128i)(a))\n";
        ss << "#define PS2_PADDW(a, b) _mm_add_epi32((__m128i)(a), (__m128i)(b))\n";
        ss << "#define PS2_PSUBW(a, b) _mm_sub_epi32((__m128i)(a), (__m128i)(b))\n";
        ss << "#define PS2_PMAXW(a, b) _mm_max_epi32((__m128i)(a), (__m128i)(b))\n";
        ss << "#define PS2_PMINW(a, b) _mm_min_epi32((__m128i)(a), (__m128i)(b))\n";
        ss << "#define PS2_PADDH(a, b) _mm_add_epi16((__m128i)(a), (__m128i)(b))\n";
        ss << "#define PS2_PSUBH(a, b) _mm_sub_epi16((__m128i)(a), (__m128i)(b))\n";
        ss << "#define PS2_PMAXH(a, b) _mm_max_epi16((__m128i)(a), (__m128i)(b))\n";
        ss << "#define PS2_PMINH(a, b) _mm_min_epi16((__m128i)(a), (__m128i)(b))\n";
        ss << "#define PS2_PADDB(a, b) _mm_add_epi8((__m128i)(a), (__m128i)(b))\n";
        ss << "#define PS2_PSUBB(a, b) _mm_sub_epi8((__m128i)(a), (__m128i)(b))\n";
        ss << "#define PS2_PAND(a, b) _mm_and_si128((__m128i)(a), (__m128i)(b))\n";
        ss << "#define PS2_POR(a, b) _mm_or_si128((__m128i)(a), (__m128i)(b))\n";
        ss << "#define PS2_PXOR(a, b) _mm_xor_si128((__m128i)(a), (__m128i)(b))\n";
        ss << "#define PS2_PNOR(a, b) _mm_xor_si128(_mm_or_si128((__m128i)(a), (__m128i)(b)), _mm_set1_epi32(0xFFFFFFFF))\n\n";

        ss << "// PS2 VU (Vector Unit) operations\n";
        ss << "#define PS2_VADD(a, b) _mm_add_ps((__m128)(a), (__m128)(b))\n";
        ss << "#define PS2_VSUB(a, b) _mm_sub_ps((__m128)(a), (__m128)(b))\n";
        ss << "#define PS2_VMUL(a, b) _mm_mul_ps((__m128)(a), (__m128)(b))\n";
        ss << "#define PS2_VDIV(a, b) _mm_div_ps((__m128)(a), (__m128)(b))\n";
        ss << "#define PS2_VMULQ(a, q) _mm_mul_ps((__m128)(a), _mm_set1_ps(q))\n\n";

        ss << "// Memory access helpers\n";
        ss << "#define READ8(addr) (*(uint8_t*)((rdram) + ((addr) & PS2_RAM_MASK)))\n";
        ss << "#define READ16(addr) (*(uint16_t*)((rdram) + ((addr) & PS2_RAM_MASK)))\n";
        ss << "#define READ32(addr) (*(uint32_t*)((rdram) + ((addr) & PS2_RAM_MASK)))\n";
        ss << "#define READ64(addr) (*(uint64_t*)((rdram) + ((addr) & PS2_RAM_MASK)))\n";
        ss << "#define READ128(addr) (*((__m128i*)((rdram) + ((addr) & PS2_RAM_MASK))))\n";
        ss << "#define WRITE8(addr, val) (*(uint8_t*)((rdram) + ((addr) & PS2_RAM_MASK)) = (val))\n";
        ss << "#define WRITE16(addr, val) (*(uint16_t*)((rdram) + ((addr) & PS2_RAM_MASK)) = (val))\n";
        ss << "#define WRITE32(addr, val) (*(uint32_t*)((rdram) + ((addr) & PS2_RAM_MASK)) = (val))\n";
        ss << "#define WRITE64(addr, val) (*(uint64_t*)((rdram) + ((addr) & PS2_RAM_MASK)) = (val))\n";
        ss << "#define WRITE128(addr, val) (*((__m128i*)((rdram) + ((addr) & PS2_RAM_MASK))) = (val))\n\n";

        ss << "// Function lookup for indirect calls\n";
        ss << "#define LOOKUP_FUNC(addr) runtime->lookupFunction(addr)\n\n";

        // Packed Compare Greater Than (PCGT)
        ss << "#define PS2_PCGTW(a, b) _mm_cmpgt_epi32((__m128i)(a), (__m128i)(b))\n";
        ss << "#define PS2_PCGTH(a, b) _mm_cmpgt_epi16((__m128i)(a), (__m128i)(b))\n";
        ss << "#define PS2_PCGTB(a, b) _mm_cmpgt_epi8((__m128i)(a), (__m128i)(b))\n";

        // Packed Compare Equal (PCEQ)
        ss << "#define PS2_PCEQW(a, b) _mm_cmpeq_epi32((__m128i)(a), (__m128i)(b))\n";
        ss << "#define PS2_PCEQH(a, b) _mm_cmpeq_epi16((__m128i)(a), (__m128i)(b))\n";
        ss << "#define PS2_PCEQB(a, b) _mm_cmpeq_epi8((__m128i)(a), (__m128i)(b))\n";

        // Packed Absolute (PABS)
        ss << "#define PS2_PABSW(a) _mm_abs_epi32((__m128i)(a))\n";
        ss << "#define PS2_PABSH(a) _mm_abs_epi16((__m128i)(a))\n";
        ss << "#define PS2_PABSB(a) _mm_abs_epi8((__m128i)(a))\n";

        // Packed Pack (PPAC) - Packs larger elements into smaller ones
        ss << "#define PS2_PPACW(a, b) _mm_packs_epi32((__m128i)(b), (__m128i)(a))\n";
        ss << "#define PS2_PPACH(a, b) _mm_packs_epi16((__m128i)(b), (__m128i)(a))\n";
        ss << "#define PS2_PPACB(a, b) _mm_packus_epi16(_mm_packs_epi32((__m128i)(b), (__m128i)(a)), _mm_setzero_si128())\n";

        // Packed Interleave (PINT)
        ss << "#define PS2_PINTH(a, b) _mm_unpacklo_epi16(_mm_shuffle_epi32((__m128i)(b), _MM_SHUFFLE(3,2,1,0)), _mm_shuffle_epi32((__m128i)(a), _MM_SHUFFLE(3,2,1,0)))\n";
        ss << "#define PS2_PINTEH(a, b) _mm_unpackhi_epi16(_mm_shuffle_epi32((__m128i)(b), _MM_SHUFFLE(3,2,1,0)), _mm_shuffle_epi32((__m128i)(a), _MM_SHUFFLE(3,2,1,0)))\n";

        // Packed Multiply-Add (PMADD)
        ss << "#define PS2_PMADDW(a, b) _mm_add_epi32(_mm_mullo_epi32(_mm_shuffle_epi32((__m128i)(a), _MM_SHUFFLE(1,0,3,2)), _mm_shuffle_epi32((__m128i)(b), _MM_SHUFFLE(1,0,3,2))), _mm_mullo_epi32(_mm_shuffle_epi32((__m128i)(a), _MM_SHUFFLE(3,2,1,0)), _mm_shuffle_epi32((__m128i)(b), _MM_SHUFFLE(3,2,1,0))))\n";

        // Packed Variable Shifts
        ss << "#define PS2_PSLLVW(a, b) _mm_custom_sllv_epi32((__m128i)(a), (__m128i)(b))\n";
        ss << "#define PS2_PSRLVW(a, b) _mm_custom_srlv_epi32((__m128i)(a), (__m128i)(b))\n";
        ss << "#define PS2_PSRAVW(a, b) _mm_custom_srav_epi32((__m128i)(a), (__m128i)(b))\n";

        // Helper function declarations for custom variable shifts
        ss << "inline __m128i _mm_custom_sllv_epi32(__m128i a, __m128i count) {\n";
        ss << "    int32_t a_arr[4], count_arr[4], result[4];\n";
        ss << "    _mm_storeu_si128((__m128i*)a_arr, a);\n";
        ss << "    _mm_storeu_si128((__m128i*)count_arr, count);\n";
        ss << "    for (int i = 0; i < 4; i++) {\n";
        ss << "        result[i] = a_arr[i] << (count_arr[i] & 0x1F);\n";
        ss << "    }\n";
        ss << "    return _mm_loadu_si128((__m128i*)result);\n";
        ss << "}\n\n";

        ss << "inline __m128i _mm_custom_srlv_epi32(__m128i a, __m128i count) {\n";
        ss << "    int32_t a_arr[4], count_arr[4], result[4];\n";
        ss << "    _mm_storeu_si128((__m128i*)a_arr, a);\n";
        ss << "    _mm_storeu_si128((__m128i*)count_arr, count);\n";
        ss << "    for (int i = 0; i < 4; i++) {\n";
        ss << "        result[i] = (uint32_t)a_arr[i] >> (count_arr[i] & 0x1F);\n";
        ss << "    }\n";
        ss << "    return _mm_loadu_si128((__m128i*)result);\n";
        ss << "}\n\n";

        ss << "inline __m128i _mm_custom_srav_epi32(__m128i a, __m128i count) {\n";
        ss << "    int32_t a_arr[4], count_arr[4], result[4];\n";
        ss << "    _mm_storeu_si128((__m128i*)a_arr, a);\n";
        ss << "    _mm_storeu_si128((__m128i*)count_arr, count);\n";
        ss << "    for (int i = 0; i < 4; i++) {\n";
        ss << "        result[i] = a_arr[i] >> (count_arr[i] & 0x1F);\n";
        ss << "    }\n";
        ss << "    return _mm_loadu_si128((__m128i*)result);\n";
        ss << "}\n\n";

        // PMFHL function implementations
        ss << "#define PS2_PMFHL_LW(hi, lo) _mm_unpacklo_epi64(lo, hi)\n";
        ss << "#define PS2_PMFHL_UW(hi, lo) _mm_unpackhi_epi64(lo, hi)\n";
        ss << "#define PS2_PMFHL_SLW(hi, lo) _mm_packs_epi32(lo, hi)\n";
        ss << "#define PS2_PMFHL_LH(hi, lo) _mm_shuffle_epi32(_mm_packs_epi32(lo, hi), _MM_SHUFFLE(3,1,2,0))\n";
        ss << "#define PS2_PMFHL_SH(hi, lo) _mm_shufflehi_epi16(_mm_shufflelo_epi16(_mm_packs_epi32(lo, hi), _MM_SHUFFLE(3,1,2,0)), _MM_SHUFFLE(3,1,2,0))\n";

        ss << "// FPU (COP1) operations\n";
        ss << "#define FPU_ADD_S(a, b) ((float)(a) + (float)(b))\n";
        ss << "#define FPU_SUB_S(a, b) ((float)(a) - (float)(b))\n";
        ss << "#define FPU_MUL_S(a, b) ((float)(a) * (float)(b))\n";
        ss << "#define FPU_DIV_S(a, b) ((float)(a) / (float)(b))\n";
        ss << "#define FPU_SQRT_S(a) sqrtf((float)(a))\n";
        ss << "#define FPU_ABS_S(a) fabsf((float)(a))\n";
        ss << "#define FPU_MOV_S(a) ((float)(a))\n";
        ss << "#define FPU_NEG_S(a) (-(float)(a))\n";
        ss << "#define FPU_ROUND_L_S(a) ((int64_t)roundf((float)(a)))\n";
        ss << "#define FPU_TRUNC_L_S(a) ((int64_t)(float)(a))\n";
        ss << "#define FPU_CEIL_L_S(a) ((int64_t)ceilf((float)(a)))\n";
        ss << "#define FPU_FLOOR_L_S(a) ((int64_t)floorf((float)(a)))\n";
        ss << "#define FPU_ROUND_W_S(a) ((int32_t)roundf((float)(a)))\n";
        ss << "#define FPU_TRUNC_W_S(a) ((int32_t)(float)(a))\n";
        ss << "#define FPU_CEIL_W_S(a) ((int32_t)ceilf((float)(a)))\n";
        ss << "#define FPU_FLOOR_W_S(a) ((int32_t)floorf((float)(a)))\n";
        ss << "#define FPU_CVT_S_W(a) ((float)(int32_t)(a))\n";
        ss << "#define FPU_CVT_S_L(a) ((float)(int64_t)(a))\n";
        ss << "#define FPU_CVT_W_S(a) ((int32_t)(float)(a))\n";
        ss << "#define FPU_CVT_L_S(a) ((int64_t)(float)(a))\n";
        ss << "#define FPU_C_F_S(a, b) (0)\n";
        ss << "#define FPU_C_UN_S(a, b) (isnan((float)(a)) || isnan((float)(b)))\n";
        ss << "#define FPU_C_EQ_S(a, b) ((float)(a) == (float)(b))\n";
        ss << "#define FPU_C_UEQ_S(a, b) ((float)(a) == (float)(b) || isnan((float)(a)) || isnan((float)(b)))\n";
        ss << "#define FPU_C_OLT_S(a, b) ((float)(a) < (float)(b))\n";
        ss << "#define FPU_C_ULT_S(a, b) ((float)(a) < (float)(b) || isnan((float)(a)) || isnan((float)(b)))\n";
        ss << "#define FPU_C_OLE_S(a, b) ((float)(a) <= (float)(b))\n";
        ss << "#define FPU_C_ULE_S(a, b) ((float)(a) <= (float)(b) || isnan((float)(a)) || isnan((float)(b)))\n";
        ss << "#define FPU_C_SF_S(a, b) (0)\n";
        ss << "#define FPU_C_NGLE_S(a, b) (isnan((float)(a)) || isnan((float)(b)))\n";
        ss << "#define FPU_C_SEQ_S(a, b) ((float)(a) == (float)(b))\n";
        ss << "#define FPU_C_NGL_S(a, b) ((float)(a) == (float)(b) || isnan((float)(a)) || isnan((float)(b)))\n";
        ss << "#define FPU_C_LT_S(a, b) ((float)(a) < (float)(b))\n";
        ss << "#define FPU_C_NGE_S(a, b) ((float)(a) < (float)(b) || isnan((float)(a)) || isnan((float)(b)))\n";
        ss << "#define FPU_C_LE_S(a, b) ((float)(a) <= (float)(b))\n";
        ss << "#define FPU_C_NGT_S(a, b) ((float)(a) <= (float)(b) || isnan((float)(a)) || isnan((float)(b)))\n\n";

        ss << "#define PS2_QFSRV(rs, rt, sa) _mm_or_si128(_mm_srl_epi32(rt, _mm_cvtsi32_si128(sa)), _mm_sll_epi32(rs, _mm_cvtsi32_si128(32 - sa)))\n";
        ss << "#define PS2_PCPYLD(rs, rt) _mm_unpacklo_epi64(rt, rs)\n";
        ss << "#define PS2_PEXEH(rs) _mm_shufflelo_epi16(_mm_shufflehi_epi16(rs, _MM_SHUFFLE(2, 3, 0, 1)), _MM_SHUFFLE(2, 3, 0, 1))\n";
        ss << "#define PS2_PEXEW(rs) _mm_shuffle_epi32(rs, _MM_SHUFFLE(2, 3, 0, 1))\n";
        ss << "#define PS2_PROT3W(rs) _mm_shuffle_epi32(rs, _MM_SHUFFLE(0, 3, 2, 1))\n";

        ss << "// Additional VU0 operations\n";
        ss << "#define PS2_VSQRT(x) sqrtf(x)\n";
        ss << "#define PS2_VRSQRT(x) (1.0f / sqrtf(x))\n";
        ss << "#define PS2_VCALLMS(addr) // VU0 microprogram calls not supported directly\n";
        ss << "#define PS2_VCALLMSR(reg) // VU0 microprogram calls not supported directly\n";

        ss << "#define GPR_U32(ctx_ptr, reg_idx) ((reg_idx == 0) ? 0U : ctx_ptr->r[reg_idx].m128i_u32[0])";
        ss << "#define GPR_S32(ctx_ptr, reg_idx) ((reg_idx == 0) ? 0 : ctx_ptr->r[reg_idx].m128i_i32[0])";
        ss << "#define GPR_U64(ctx_ptr, reg_idx) ((reg_idx == 0) ? 0ULL : ctx_ptr->r[reg_idx].m128i_u64[0])";
        ss << "#define GPR_S64(ctx_ptr, reg_idx) ((reg_idx == 0) ? 0LL : ctx_ptr->r[reg_idx].m128i_i64[0])";
        ss << "#define GPR_VEC(ctx_ptr, reg_idx) ((reg_idx == 0) ? _mm_setzero_si128() : ctx_ptr->r[reg_idx])";

        ss << "#define SET_GPR_U32(ctx_ptr, reg_idx, val) \\\n";
        ss << "    do                                     \\\n";
        ss << "    {                                      \\\n";
        ss << "        if (reg_idx != 0)                  \\\n";
        ss << "            ctx_ptr->r[reg_idx] = _mm_set_epi32(0, 0, 0, (val)); \\\n";
        ss << "    } while (0)\n";

        ss << "#define SET_GPR_S32(ctx_ptr, reg_idx, val) \\\n";
        ss << "    do                                     \\\n";
        ss << "    {                                      \\\n";
        ss << "        if (reg_idx != 0)                  \\\n";
        ss << "            ctx_ptr->r[reg_idx] = _mm_set_epi32(0, 0, 0, (val)); \\\n";
        ss << "    } while (0)\n";

        ss << "#define SET_GPR_U64(ctx_ptr, reg_idx, val) \\\n";
        ss << "    do                                     \\\n";
        ss << "    {                                      \\\n";
        ss << "        if (reg_idx != 0)                  \\\n";
        ss << "            ctx_ptr->r[reg_idx] = _mm_set_epi64x(0, (val)); \\\n";
        ss << "    } while (0)\n";

        ss << "#define SET_GPR_S64(ctx_ptr, reg_idx, val) \\\n";
        ss << "    do                                     \\\n";
        ss << "    {                                      \\\n";
        ss << "        if (reg_idx != 0)                  \\\n";
        ss << "            ctx_ptr->r[reg_idx] = _mm_set_epi64x(0, (val)); \\\n";
        ss << "    } while (0)\n";

        ss << "#define SET_GPR_VEC(ctx_ptr, reg_idx, val) \\\n";
        ss << "    do                                     \\\n";
        ss << "    {                                      \\\n";
        ss << "        if (reg_idx != 0)                  \\\n";
        ss << "            ctx_ptr->r[reg_idx] = (val); \\\n";
        ss << "    } while (0)\n";

        ss << "#endif // PS2_RUNTIME_MACROS_H\n";

        return ss.str();
    }

    std::string CodeGenerator::generateFunction(const Function &function, const std::vector<Instruction> &instructions, const bool &useHeaders)
    {
        std::stringstream ss;

        if (useHeaders)
        {
            ss << "#include \"ps2_runtime_macros.h\"\n";
            ss << "#include \"ps2_runtime.h\"\n";
            ss << "#include \"ps2_recompiled_functions.h\"\n\n";
        }

        ss << "// Function: " << function.name << "\n";
        ss << "// Address: 0x" << std::hex << function.start << " - 0x" << function.end << std::dec << "\n";
        ss << "void " << function.name << "(uint8_t* rdram, R5900Context* ctx) {\n\n";

        for (size_t i = 0; i < instructions.size(); ++i)
        {
            const Instruction &inst = instructions[i];

            ss << "    // 0x" << std::hex << inst.address << ": 0x" << inst.raw << std::dec << "\n";

            if (inst.hasDelaySlot && i + 1 < instructions.size())
            {
                const Instruction &delaySlot = instructions[i + 1];
                ss << handleBranchDelaySlots(inst, delaySlot);

                // Skip the delay slot instruction as we've already handled it
                ++i;
            }
            else
            {
                ss << "    " << translateInstruction(inst) << "\n";
            }
        }

        ss << "}\n";

        return ss.str();
    }

    std::string CodeGenerator::translateInstruction(const Instruction &inst)
    {
        if (inst.isMMI)
        {
            return translateMMIInstruction(inst);
        }

        switch (inst.opcode)
        {
        case OPCODE_SPECIAL:
            return translateSpecialInstruction(inst);
        case OPCODE_REGIMM:
            return translateRegimmInstruction(inst);
        case OPCODE_COP0:
            return translateCOP0Instruction(inst);
        case OPCODE_COP1:
            return translateFPUInstruction(inst);
        case OPCODE_COP2:
            return translateVUInstruction(inst);
        case OPCODE_ADDI:
        case OPCODE_ADDIU:
            if (inst.rt == 0)
                return "// NOP (addiu $zero, ...)";
            return fmt::format("SET_GPR_S32(ctx, {}, ADD32(GPR_U32(ctx, {}), {}));", inst.rt, inst.rs, inst.simmediate);
        case OPCODE_SLTI:
            return fmt::format("SET_GPR_U32(ctx, {}, SLT32(GPR_S32(ctx, {}), {}));", inst.rt, inst.rs, inst.simmediate);
        case OPCODE_SLTIU:
            return fmt::format("SET_GPR_U32(ctx, {}, SLTU32(GPR_U32(ctx, {}), {}));", inst.rt, inst.rs, inst.immediate);
        case OPCODE_ANDI:
            return fmt::format("SET_GPR_U32(ctx, {}, AND32(GPR_U32(ctx, {}), {}));", inst.rt, inst.rs, inst.immediate);
        case OPCODE_ORI:
            return fmt::format("SET_GPR_U32(ctx, {}, OR32(GPR_U32(ctx, {}), {}));", inst.rt, inst.rs, inst.immediate);
        case OPCODE_XORI:
            return fmt::format("SET_GPR_U32(ctx, {}, XOR32(GPR_U32(ctx, {}), {}));", inst.rt, inst.rs, inst.immediate);
        case OPCODE_LUI:
            return fmt::format("SET_GPR_U32(ctx, {}, ((uint32_t){} << 16));", inst.rt, inst.immediate);
        case OPCODE_LB:
            return fmt::format("SET_GPR_S32(ctx, {}, (int8_t)READ8(ADD32(GPR_U32(ctx, {}), {})));", inst.rt, inst.rs, inst.simmediate);
        case OPCODE_LH:
            return fmt::format("SET_GPR_S32(ctx, {}, (int16_t)READ16(ADD32(GPR_U32(ctx, {}), {})));", inst.rt, inst.rs, inst.simmediate);
        case OPCODE_LW:
            return fmt::format("SET_GPR_U32(ctx, {}, READ32(ADD32(GPR_U32(ctx, {}), {})));", inst.rt, inst.rs, inst.simmediate);
        case OPCODE_LBU:
            return fmt::format("SET_GPR_U32(ctx, {}, (uint8_t)READ8(ADD32(GPR_U32(ctx, {}), {})));", inst.rt, inst.rs, inst.simmediate);
        case OPCODE_LHU:
            return fmt::format("SET_GPR_U32(ctx, {}, (uint16_t)READ16(ADD32(GPR_U32(ctx, {}), {})));", inst.rt, inst.rs, inst.simmediate);
        case OPCODE_LWU:
            return fmt::format("SET_GPR_U32(ctx, {}, READ32(ADD32(GPR_U32(ctx, {}), {})));", inst.rt, inst.rs, inst.simmediate);
        case OPCODE_SB:
            return fmt::format("WRITE8(ADD32(GPR_U32(ctx, {}), {}), (uint8_t)GPR_U32(ctx, {}));", inst.rs, inst.simmediate, inst.rt);
        case OPCODE_SH:
            return fmt::format("WRITE16(ADD32(GPR_U32(ctx, {}), {}), (uint16_t)GPR_U32(ctx, {}));", inst.rs, inst.simmediate, inst.rt);
        case OPCODE_SW:
            return fmt::format("WRITE32(ADD32(GPR_U32(ctx, {}), {}), GPR_U32(ctx, {}));", inst.rs, inst.simmediate, inst.rt);
        case OPCODE_LQ:
            return fmt::format("SET_GPR_VEC(ctx, {}, READ128(ADD32(GPR_U32(ctx, {}), {})));", inst.rt, inst.rs, inst.simmediate);
        case OPCODE_SQ:
            return fmt::format("WRITE128(ADD32(GPR_U32(ctx, {}), {}), GPR_VEC(ctx, {}));", inst.rs, inst.simmediate, inst.rt);
        case OPCODE_LD:
            return fmt::format("SET_GPR_U64(ctx, {}, READ64(ADD32(GPR_U32(ctx, {}), {})));", inst.rt, inst.rs, inst.simmediate);
        case OPCODE_SD:
            return fmt::format("WRITE64(ADD32(GPR_U32(ctx, {}), {}), GPR_U64(ctx, {}));", inst.rs, inst.simmediate, inst.rt);
        case OPCODE_LWC1:
            return fmt::format("{{ uint32_t val = READ32(ADD32(GPR_U32(ctx, {}), {})); ctx->f[{}] = *(float*)&val; }}", inst.rs, inst.simmediate, inst.rt);
        case OPCODE_SWC1:
            return fmt::format("{{ float val = ctx->f[{}]; WRITE32(ADD32(GPR_U32(ctx, {}), {}), *(uint32_t*)&val); }}", inst.rt, inst.rs, inst.simmediate);
        case OPCODE_LDC2: // was OPCODE_LQC2 need to check
            return fmt::format("ctx->vu0_vf[{}] = (__m128)READ128(ADD32(GPR_U32(ctx, {}), {}));", inst.rt, inst.rs, inst.simmediate);
        case OPCODE_SDC2: // was OPCODE_SQC2 need to check
            return fmt::format("WRITE128(ADD32(GPR_U32(ctx, {}), {}), (__m128i)ctx->vu0_vf[{}]);", inst.rs, inst.simmediate, inst.rt);
        case OPCODE_DADDI:
        case OPCODE_DADDIU:
            return fmt::format("SET_GPR_U64(ctx, {}, GPR_U64(ctx, {}) + (uint64_t)(int64_t){});", inst.rt, inst.rs, inst.simmediate);
        case OPCODE_J:
            return fmt::format("// JAL 0x{:X} - Handled by branch logic", (inst.address & 0xF0000000) | (inst.target << 2));
        case OPCODE_JAL:
            return fmt::format("// JAL 0x{:X} - Handled by branch logic", (inst.address & 0xF0000000) | (inst.target << 2));
        case OPCODE_BEQ:
        case OPCODE_BNE:
        case OPCODE_BLEZ:
        case OPCODE_BGTZ:
        case OPCODE_BEQL:
        case OPCODE_BNEL:
        case OPCODE_BLEZL:
        case OPCODE_BGTZL:
            return fmt::format("// Likely branch instruction at 0x{:X} - Handled by branch logic", inst.address);
        case OPCODE_LDL:
        case OPCODE_LDR:
        case OPCODE_SDL:
        case OPCODE_SDR:
        case OPCODE_LWL:
        case OPCODE_LWR:
        case OPCODE_SWL:
        case OPCODE_SWR:
            return fmt::format("//Unhandled Unaligned load/store instruction 0x{:X} not implemented", inst.opcode);
        case OPCODE_CACHE:
            return "// CACHE instruction (ignored)";
        case OPCODE_PREF:
            return "// PREF instruction (ignored)";
        default:
            return fmt::format("// Unhandled opcode: 0x{:X}", inst.opcode);
        }
    }

    std::string CodeGenerator::translateSpecialInstruction(const Instruction &inst)
    {
        switch (inst.function)
        {
        case SPECIAL_SLL:
            if (inst.rd == 0 && inst.rt == 0 && inst.sa == 0)
                return "// NOP";
            if (inst.rd == 0)
                return "";
            return fmt::format("SET_GPR_U32(ctx, {}, SLL32(GPR_U32(ctx, {}), {}));", inst.rd, inst.rt, inst.sa);
        case SPECIAL_SRL:
            return fmt::format("SET_GPR_U32(ctx, {}, SRL32(GPR_U32(ctx, {}), {}));", inst.rd, inst.rt, inst.sa);
        case SPECIAL_SRA:
            return fmt::format("SET_GPR_S32(ctx, {}, SRA32(GPR_S32(ctx, {}), {}));", inst.rd, inst.rt, inst.sa);
        case SPECIAL_SLLV:
            return fmt::format("SET_GPR_U32(ctx, {}, SLL32(GPR_U32(ctx, {}), GPR_U32(ctx, {}) & 0x1F));", inst.rd, inst.rt, inst.rs);
        case SPECIAL_SRLV:
            return fmt::format("SET_GPR_U32(ctx, {}, SRL32(GPR_U32(ctx, {}), GPR_U32(ctx, {}) & 0x1F));", inst.rd, inst.rt, inst.rs);
        case SPECIAL_SRAV:
            return fmt::format("SET_GPR_S32(ctx, {}, SRA32(GPR_S32(ctx, {}), GPR_U32(ctx, {}) & 0x1F));", inst.rd, inst.rt, inst.rs);
        case SPECIAL_JR:
            return fmt::format("// JR ${} - Handled by branch logic", inst.rs);
        case SPECIAL_JALR:
            return fmt::format("// JALR ${}, ${} - Handled by branch logic", inst.rd, inst.rs);
        case SPECIAL_SYSCALL:
            return fmt::format("runtime->handleSyscall(rdram, ctx);");
        case SPECIAL_BREAK:
            return fmt::format("runtime->handleBreak(rdram, ctx);");
        case SPECIAL_SYNC:
            return "// SYNC instruction - memory barrier\n// In recompiled code, we don't need explicit memory barriers";
        case SPECIAL_MFHI:
            return fmt::format("SET_GPR_U32(ctx, {}, ctx->hi);", inst.rd);
        case SPECIAL_MTHI:
            return fmt::format("ctx->hi = GPR_U32(ctx, {});", inst.rs);
        case SPECIAL_MFLO:
            return fmt::format("SET_GPR_U32(ctx, {}, ctx->lo);", inst.rd);
        case SPECIAL_MTLO:
            return fmt::format("ctx->lo = GPR_U32(ctx, {});", inst.rs);
        case SPECIAL_MULT:
            return fmt::format("{{ int64_t result = (int64_t)GPR_S32(ctx, {}) * (int64_t)GPR_S32(ctx, {}); ctx->lo = (uint32_t)result; ctx->hi = (uint32_t)(result >> 32); }}", inst.rs, inst.rt);
        case SPECIAL_MULTU:
            return fmt::format("{{ uint64_t result = (uint64_t)GPR_U32(ctx, {}) * (uint64_t)GPR_U32(ctx, {}); ctx->lo = (uint32_t)result; ctx->hi = (uint32_t)(result >> 32); }}", inst.rs, inst.rt);
        case SPECIAL_DIV:
            return fmt::format("{{ int32_t divisor = GPR_S32(ctx, {}); if (divisor != 0) {{ ctx->lo = (uint32_t)(GPR_S32(ctx, {}) / divisor); ctx->hi = (uint32_t)(GPR_S32(ctx, {}) % divisor); }} else {{ ctx->lo = (GPR_S32(ctx,{}) < 0) ? 1 : -1; ctx->hi = GPR_S32(ctx,{}); }} }}", inst.rt, inst.rs, inst.rt, inst.rs, inst.rt);
        case SPECIAL_DIVU:
            return fmt::format("{{ uint32_t divisor = GPR_U32(ctx, {}); if (divisor != 0) {{ ctx->lo = GPR_U32(ctx, {}) / divisor; ctx->hi = GPR_U32(ctx, {}) % divisor; }} else {{ ctx->lo = 0xFFFFFFFF; ctx->hi = GPR_U32(ctx,{}); }} }}", inst.rt, inst.rs, inst.rt, inst.rs, inst.rt);
        case SPECIAL_ADD:
            return fmt::format("SET_GPR_S32(ctx, {}, ADD32(GPR_U32(ctx, {}), GPR_U32(ctx, {})));", inst.rd, inst.rs, inst.rt);
        case SPECIAL_ADDU:
            return fmt::format("SET_GPR_U32(ctx, {}, ADD32(GPR_U32(ctx, {}), GPR_U32(ctx, {})));", inst.rd, inst.rs, inst.rt);
        case SPECIAL_SUB:
            return fmt::format("SET_GPR_S32(ctx, {}, SUB32(GPR_U32(ctx, {}), GPR_U32(ctx, {})));", inst.rd, inst.rs, inst.rt);
        case SPECIAL_SUBU:
            return fmt::format("SET_GPR_U32(ctx, {}, SUB32(GPR_U32(ctx, {}), GPR_U32(ctx, {})));", inst.rd, inst.rs, inst.rt);
        case SPECIAL_AND:
            return fmt::format("SET_GPR_U32(ctx, {}, AND32(GPR_U32(ctx, {}), GPR_U32(ctx, {})));", inst.rd, inst.rs, inst.rt);
        case SPECIAL_OR:
            return fmt::format("SET_GPR_U32(ctx, {}, OR32(GPR_U32(ctx, {}), GPR_U32(ctx, {})));", inst.rd, inst.rs, inst.rt);
        case SPECIAL_XOR:
            return fmt::format("SET_GPR_U32(ctx, {}, XOR32(GPR_U32(ctx, {}), GPR_U32(ctx, {})));", inst.rd, inst.rs, inst.rt);
        case SPECIAL_NOR:
            return fmt::format("SET_GPR_U32(ctx, {}, NOR32(GPR_U32(ctx, {}), GPR_U32(ctx, {})));", inst.rd, inst.rs, inst.rt);
        case SPECIAL_SLT:
            return fmt::format("SET_GPR_U32(ctx, {}, SLT32(GPR_S32(ctx, {}), GPR_S32(ctx, {})));", inst.rd, inst.rs, inst.rt);
        case SPECIAL_SLTU:
            return fmt::format("SET_GPR_U32(ctx, {}, SLTU32(GPR_U32(ctx, {}), GPR_U32(ctx, {})));", inst.rd, inst.rs, inst.rt);
        case SPECIAL_MOVZ:
            return fmt::format("if (GPR_U32(ctx, {}) == 0) SET_GPR_U32(ctx, {}, GPR_U32(ctx, {}));", inst.rt, inst.rd, inst.rs);
        case SPECIAL_MOVN:
            return fmt::format("if (GPR_U32(ctx, {}) != 0) SET_GPR_U32(ctx, {}, GPR_U32(ctx, {}));", inst.rt, inst.rd, inst.rs);
        case SPECIAL_MFSA:
            return fmt::format("SET_GPR_U32(ctx, {}, ctx->sa);", inst.rd);
        case SPECIAL_MTSA:
            return fmt::format("ctx->sa = GPR_U32(ctx, {}) & 0x1F;", inst.rs);
        case SPECIAL_DADD:
        case SPECIAL_DADDU:
            return fmt::format("SET_GPR_U64(ctx, {}, GPR_U64(ctx, {}) + GPR_U64(ctx, {}));", inst.rd, inst.rs, inst.rt);
        case SPECIAL_DSUB:
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
            return fmt::format("if (GPR_S32(ctx, {}) >= GPR_S32(ctx, {})) {{ runtime->handleTrap(rdram, ctx); }}", inst.rs, inst.rt);
        case SPECIAL_TGEU:
            return fmt::format("if (GPR_U32(ctx, {}) >= GPR_U32(ctx, {})) {{ runtime->handleTrap(rdram, ctx); }}", inst.rs, inst.rt);
        case SPECIAL_TLT:
            return fmt::format("if (GPR_S32(ctx, {}) < GPR_S32(ctx, {})) {{ runtime->handleTrap(rdram, ctx); }}", inst.rs, inst.rt);
        case SPECIAL_TLTU:
            return fmt::format("if (GPR_U32(ctx, {}) < GPR_U32(ctx, {})) {{ runtime->handleTrap(rdram, ctx); }}", inst.rs, inst.rt);
        case SPECIAL_TEQ:
            return fmt::format("if (GPR_U32(ctx, {}) == GPR_U32(ctx, {})) {{ runtime->handleTrap(rdram, ctx); }}", inst.rs, inst.rt);
        case SPECIAL_TNE:
            return fmt::format("if (GPR_U32(ctx, {}) != GPR_U32(ctx, {})) {{ runtime->handleTrap(rdram, ctx); }}", inst.rs, inst.rt);
        default:
            return fmt::format("// Unhandled SPECIAL instruction: 0x{:X}", inst.function);
        }
    }

    std::string CodeGenerator::translateRegimmInstruction(const Instruction &inst)
    {
        switch (inst.rt)
        {
        case REGIMM_BLTZ:
        case REGIMM_BGEZ:
        case REGIMM_BLTZL:
        case REGIMM_BGEZL:
        case REGIMM_BLTZAL:
        case REGIMM_BGEZAL:
        case REGIMM_BLTZALL:
        case REGIMM_BGEZALL:
        {
            uint32_t target = inst.address + 4 + (inst.simmediate << 2);
            return fmt::format("// REGIMM branch instruction to 0x{:X} - Handled by branch logic", target);
        }
        case REGIMM_MTSAB:
            return fmt::format("ctx->sa = (GPR_U32(ctx, {}) + {}) & 0xF;", inst.rs, inst.simmediate);
        case REGIMM_MTSAH:
            return fmt::format("ctx->sa = ((GPR_U32(ctx, {}) + {}) & 0x7) << 1;", inst.rs, inst.simmediate);
        case REGIMM_TGEI:
            return fmt::format("if (GPR_S32(ctx, {}) >= {}) {{ runtime->handleTrap(rdram, ctx); }}", inst.rs, inst.simmediate);
        case REGIMM_TGEIU:
            return fmt::format("if (GPR_U32(ctx, {}) >= (uint32_t){}) {{ runtime->handleTrap(rdram, ctx); }}", inst.rs, inst.simmediate);
        case REGIMM_TLTI:
            return fmt::format("if (GPR_S32(ctx, {}) < {}) {{ runtime->handleTrap(rdram, ctx); }}", inst.rs, inst.simmediate);
        case REGIMM_TLTIU:
            return fmt::format("if (GPR_U32(ctx, {}) < (uint32_t){}) {{ runtime->handleTrap(rdram, ctx); }}", inst.rs, inst.simmediate);
        case REGIMM_TEQI:
            return fmt::format("if (GPR_S32(ctx, {}) == {}) {{ runtime->handleTrap(rdram, ctx); }}", inst.rs, inst.simmediate);
        case REGIMM_TNEI:
            return fmt::format("if (GPR_S32(ctx, {}) != {}) {{ runtime->handleTrap(rdram, ctx); }}", inst.rs, inst.simmediate);
        default:
            return fmt::format("// Unhandled REGIMM instruction: 0x{:X}", inst.rt);
        }
    }

    std::string CodeGenerator::translateCOP0Instruction(const Instruction &inst)
    {
        uint32_t format = inst.rs; // Format field
        uint32_t rt = inst.rt;     // GPR register
        uint32_t rd = inst.rd;     // COP0 register

        switch (format)
        {
        case COP0_MF:
            switch (rd)
            {
            case COP0_REG_INDEX:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->cop0_index);", rt);
            case COP0_REG_RANDOM:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->cop0_random);", rt);
            case COP0_REG_ENTRYLO0:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->cop0_entrylo0);", rt);
            case COP0_REG_ENTRYLO1:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->cop0_entrylo1);", rt);
            case COP0_REG_CONTEXT:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->cop0_context);", rt);
            case COP0_REG_PAGEMASK:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->cop0_pagemask);", rt);
            case COP0_REG_WIRED:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->cop0_wired);", rt);
            case COP0_REG_BADVADDR:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->cop0_badvaddr);", rt);
            case COP0_REG_COUNT:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->cop0_count);", rt);
            case COP0_REG_ENTRYHI:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->cop0_entryhi);", rt);
            case COP0_REG_COMPARE:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->cop0_compare);", rt);
            case COP0_REG_STATUS:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->cop0_status);", rt);
            case COP0_REG_CAUSE:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->cop0_cause);", rt);
            case COP0_REG_EPC:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->cop0_epc);", rt);
            case COP0_REG_PRID:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->cop0_prid);", rt);
            case COP0_REG_CONFIG:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->cop0_config);", rt);
            case COP0_REG_BADPADDR:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->cop0_badpaddr);", rt);
            case COP0_REG_DEBUG:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->cop0_debug);", rt);
            case COP0_REG_PERF:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->cop0_perf);", rt);
            case COP0_REG_TAGLO:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->cop0_taglo);", rt);
            case COP0_REG_TAGHI:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->cop0_taghi);", rt);
            case COP0_REG_ERROREPC:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->cop0_errorepc);", rt);
            default:
                return fmt::format("SET_GPR_U32(ctx, {}, 0);  // Unimplemented COP0 register {}", rt, rd);
            }
        case COP0_MT:
            switch (rd)
            {
            case COP0_REG_INDEX:
                return fmt::format("ctx->cop0_index = GPR_U32(ctx, {}) & 0x3F;", rt);
            case COP0_REG_RANDOM:
                return "// MTC0 to RANDOM register ignored (read-only)";
            case COP0_REG_ENTRYLO0:
                return fmt::format("ctx->cop0_entrylo0 = GPR_U32(ctx, {}) & 0x3FFFFFFF;", rt);
            case COP0_REG_ENTRYLO1:
                return fmt::format("ctx->cop0_entrylo1 = GPR_U32(ctx, {}) & 0x3FFFFFFF;", rt);
            case COP0_REG_CONTEXT:
                return fmt::format("ctx->cop0_context = (ctx->cop0_context & 0xFF800000) | (GPR_U32(ctx, {}) & 0x7FFFFF);", rt);
            case COP0_REG_PAGEMASK:
                return fmt::format("ctx->cop0_pagemask = GPR_U32(ctx, {}) & 0x01FFE000;", rt);
            case COP0_REG_WIRED:
                return fmt::format("ctx->cop0_wired = GPR_U32(ctx, {}) & 0x3F; ctx->cop0_random = 47;", rt);
            case COP0_REG_BADVADDR:
                return "// MTC0 to BADVADDR register ignored (read-only)";
            case COP0_REG_COUNT:
                return fmt::format("ctx->cop0_count = GPR_U32(ctx, {});", rt);
            case COP0_REG_ENTRYHI:
                return fmt::format("ctx->cop0_entryhi = GPR_U32(ctx, {}) & 0xC00000FF;", rt);
            case COP0_REG_COMPARE:
                return fmt::format("ctx->cop0_compare = GPR_U32(ctx, {}); ctx->cop0_cause &= ~0x8000;", rt);
            case COP0_REG_STATUS:
                return fmt::format("ctx->cop0_status = GPR_U32(ctx, {}) & 0xFF57FFFF;", rt);
            case COP0_REG_CAUSE:
                return fmt::format("ctx->cop0_cause = (ctx->cop0_cause & ~0x00000300) | (GPR_U32(ctx, {}) & 0x00000300);", rt);
            case COP0_REG_EPC:
                return fmt::format("ctx->cop0_epc = GPR_U32(ctx, {});", rt);
            case COP0_REG_PRID:
                return "// MTC0 to PRID register ignored (read-only)";
            case COP0_REG_CONFIG:
                return fmt::format("ctx->cop0_config = (ctx->cop0_config & ~0x7) | (GPR_U32(ctx, {}) & 0x7);", rt);
            case COP0_REG_BADPADDR:
                return "// MTC0 to BADPADDR register ignored (read-only)";
            case COP0_REG_DEBUG:
                return fmt::format("ctx->cop0_debug = GPR_U32(ctx, {});", rt);
            case COP0_REG_PERF:
                return fmt::format("ctx->cop0_perf = GPR_U32(ctx, {});", rt);
            case COP0_REG_TAGLO:
                return fmt::format("ctx->cop0_taglo = GPR_U32(ctx, {});", rt);
            case COP0_REG_TAGHI:
                return fmt::format("ctx->cop0_taghi = GPR_U32(ctx, {});", rt);
            case COP0_REG_ERROREPC:
                return fmt::format("ctx->cop0_errorepc = GPR_U32(ctx, {});", rt);
            default:
                return fmt::format("// Unimplemented MTC0 to COP0 {}", rd);
            }
        case COP0_BC:
            return fmt::format("// BC0 (Condition: 0x{:X}) - Handled by branch logic", rt);
        case COP0_CO:
        {
            uint8_t function = FUNCTION(inst.raw);
            switch (function)
            {
            case COP0_CO_TLBR:
                return fmt::format("runtime->handleTLBR(rdram, ctx);");
            case COP0_CO_TLBWI:
                return fmt::format("runtime->handleTLBWI(rdram, ctx);");
            case COP0_CO_TLBWR:
                return fmt::format("runtime->handleTLBWR(rdram, ctx);");
            case COP0_CO_TLBP:
                return fmt::format("runtime->handleTLBP(rdram, ctx);");
            case COP0_CO_ERET:
                return fmt::format(
                    "if (ctx->cop0_status & 0x4) {{ \\\n" // Check ERL bit (bit 2)
                    "    ctx->pc = ctx->cop0_errorepc; \\\n"
                    "    ctx->cop0_status &= ~0x4; \\\n" // Clear ERL bit
                    "}} else {{ \\\n"                    // If ERL is not set, use EPC and clear EXL (bit 1)
                    "    ctx->pc = ctx->cop0_epc; \\\n"  // Note: If neither ERL/EXL set, behavior is undefined; using EPC is common.
                    "    ctx->cop0_status &= ~0x2; \\\n" // Clear EXL bit
                    "}} \\\n"
                    "runtime->clearLLBit(ctx); \\\n" // Essential: Clear Load-Linked bit
                    "return;"                        // Stop execution in this recompiled block
                );
            case COP0_CO_EI:
                return fmt::format("ctx->cop0_status |= 0x1; // Enable interrupts");
            case COP0_CO_DI:
                return fmt::format("ctx->cop0_status &= ~0x1; // Disable interrupts");
            default:
                return fmt::format("// Unhandled COP0 CO-OP: 0x{:X}", function);
            }
        }
        default:
            return fmt::format("// Unhandled COP0 instruction format: 0x{:X}", format);
        }
    }

    std::string CodeGenerator::translateFPUInstruction(const Instruction &inst)
    {
        uint8_t format = inst.rs; // Format field
        uint32_t ft = inst.rt;    // FPU source register
        uint32_t fs = inst.rd;    // FPU source register
        uint32_t fd = inst.sa;    // FPU destination register
        uint32_t function = inst.function;

        switch (format)
        {
        case COP1_MF:
            return fmt::format("SET_GPR_U32(ctx, {}, *(uint32_t*)&ctx->f[{}]);", ft, fs);
        case COP1_MT:
            return fmt::format("*(uint32_t*)&ctx->f[{}] = GPR_U32(ctx, {});", fs, ft);
        case COP1_CF:
            if (fs == 31)
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->fcr31);", ft); // FCR31 contains status/control
            if (fs == 0)
                return fmt::format("SET_GPR_U32(ctx, {}, 0x00000000);", ft); // FCR0 is the FPU implementation register
            return fmt::format("SET_GPR_U32(ctx, {}, 0); // Unimplemented FCR{}", ft, fs);
        case COP1_CT:
            if (fs == 31)
                return fmt::format("ctx->fcr31 = GPR_U32(ctx, {}) & 0x0183FFFF;", ft);
            else
                return fmt::format("// CTC1 to FCR{} ignored", fs);
            return "";
        case COP1_BC:
            return "// FPU branch instruction - handled elsewhere";
        case COP1_S:
            switch (function)
            {
            case COP1_S_ADD:
                return fmt::format("ctx->f[{}] = FPU_ADD_S(ctx->f[{}], ctx->f[{}]);", fd, fs, ft);
            case COP1_S_SUB:
                return fmt::format("ctx->f[{}] = FPU_SUB_S(ctx->f[{}], ctx->f[{}]);", fd, fs, ft);
            case COP1_S_MUL:
                return fmt::format("ctx->f[{}] = FPU_MUL_S(ctx->f[{}], ctx->f[{}]);", fd, fs, ft);
            case COP1_S_DIV:
                return fmt::format("ctx->f[{}] = FPU_DIV_S(ctx->f[{}], ctx->f[{}]);", fd, fs, ft);
            case COP1_S_SQRT:
                return fmt::format("ctx->f[{}] = FPU_SQRT_S(ctx->f[{}]);", fd, fs);
            case COP1_S_ABS:
                return fmt::format("ctx->f[{}] = FPU_ABS_S(ctx->f[{}]);", fd, fs);
            case COP1_S_MOV:
                return fmt::format("ctx->f[{}] = FPU_MOV_S(ctx->f[{}]);", fd, fs);
            case COP1_S_NEG:
                return fmt::format("ctx->f[{}] = FPU_NEG_S(ctx->f[{}]);", fd, fs);
            case COP1_S_ROUND_W:
                return fmt::format("*(int32_t*)&ctx->f[{}] = FPU_ROUND_W_S(ctx->f[{}]);", fd, fs);
            case COP1_S_TRUNC_W:
                return fmt::format("*(int32_t*)&ctx->f[{}] = FPU_TRUNC_W_S(ctx->f[{}]);", fd, fs);
            case COP1_S_CEIL_W:
                return fmt::format("*(int32_t*)&ctx->f[{}] = FPU_CEIL_W_S(ctx->f[{}]);", fd, fs);
            case COP1_S_FLOOR_W:
                return fmt::format("*(int32_t*)&ctx->f[{}] = FPU_FLOOR_W_S(ctx->f[{}]);", fd, fs);
            case COP1_S_CVT_W:
                return fmt::format("*(int32_t*)&ctx->f[{}] = FPU_CVT_W_S(ctx->f[{}]);", fd, fs);
            case COP1_S_RSQRT:
                return fmt::format("ctx->f[{}] = 1.0f / sqrtf(ctx->f[{}]);", fd, fs);
            case COP1_S_ADDA:
                return fmt::format("ctx->f[31] = FPU_ADD_S(ctx->f[{}], ctx->f[{}]);", fs, ft);
            case COP1_S_SUBA:
                return fmt::format("ctx->f[31] = FPU_SUB_S(ctx->f[{}], ctx->f[{}]);", fs, ft);
            case COP1_S_MULA:
                return fmt::format("ctx->f[31] = FPU_MUL_S(ctx->f[{}], ctx->f[{}]);", fs, ft);
            case COP1_S_MADD:
                return fmt::format("ctx->f[{}] = FPU_ADD_S(ctx->f[31], FPU_MUL_S(ctx->f[{}], ctx->f[{}]));", fd, fs, ft);
            case COP1_S_MSUB:
                return fmt::format("ctx->f[{}] = FPU_SUB_S(ctx->f[31], FPU_MUL_S(ctx->f[{}], ctx->f[{}]));", fd, fs, ft);
            case COP1_S_MADDA:
                return fmt::format("ctx->f[31] = FPU_ADD_S(ctx->f[31], FPU_MUL_S(ctx->f[{}], ctx->f[{}]));", fs, ft);
            case COP1_S_MSUBA:
                return fmt::format("ctx->f[31] = FPU_SUB_S(ctx->f[31], FPU_MUL_S(ctx->f[{}], ctx->f[{}]));", fs, ft);
            case COP1_S_MAX:
                return fmt::format("ctx->f[{}] = std::max(ctx->f[{}], ctx->f[{}]);", fd, fs, ft);
            case COP1_S_MIN:
                return fmt::format("ctx->f[{}] = std::min(ctx->f[{}], ctx->f[{}]);", fd, fs, ft);
            case COP1_S_C_F:
                return fmt::format("ctx->fcr31 &= ~0x800000;");
            case COP1_S_C_UN:
                return fmt::format("ctx->fcr31 = (FPU_C_UN_S(ctx->f[{}], ctx->f[{}])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);", fs, ft);
            case COP1_S_C_EQ:
                return fmt::format("ctx->fcr31 = (FPU_C_EQ_S(ctx->f[{}], ctx->f[{}])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);", fs, ft);
            case COP1_S_C_UEQ:
                return fmt::format("ctx->fcr31 = (FPU_C_UEQ_S(ctx->f[{}], ctx->f[{}])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);", fs, ft);
            case COP1_S_C_OLT:
                return fmt::format("ctx->fcr31 = (FPU_C_OLT_S(ctx->f[{}], ctx->f[{}])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);", fs, ft);
            case COP1_S_C_ULT:
                return fmt::format("ctx->fcr31 = (FPU_C_ULT_S(ctx->f[{}], ctx->f[{}])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);", fs, ft);
            case COP1_S_C_OLE:
                return fmt::format("ctx->fcr31 = (FPU_C_OLE_S(ctx->f[{}], ctx->f[{}])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);", fs, ft);
            case COP1_S_C_ULE:
                return fmt::format("ctx->fcr31 = (FPU_C_ULE_S(ctx->f[{}], ctx->f[{}])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);", fs, ft);
            case COP1_S_C_SF:
                return fmt::format("ctx->fcr31 &= ~0x800000;");
            case COP1_S_C_NGLE:
                return fmt::format("ctx->fcr31 = (FPU_C_NGLE_S(ctx->f[{}], ctx->f[{}])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);", fs, ft);
            case COP1_S_C_SEQ:
                return fmt::format("ctx->fcr31 = (FPU_C_SEQ_S(ctx->f[{}], ctx->f[{}])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);", fs, ft);
            case COP1_S_C_NGL:
                return fmt::format("ctx->fcr31 = (FPU_C_NGL_S(ctx->f[{}], ctx->f[{}])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);", fs, ft);
            case COP1_S_C_LT:
                return fmt::format("ctx->fcr31 = (FPU_C_LT_S(ctx->f[{}], ctx->f[{}])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);", fs, ft);
            case COP1_S_C_NGE:
                return fmt::format("ctx->fcr31 = (FPU_C_NGE_S(ctx->f[{}], ctx->f[{}])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);", fs, ft);
            case COP1_S_C_LE:
                return fmt::format("ctx->fcr31 = (FPU_C_LE_S(ctx->f[{}], ctx->f[{}])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);", fs, ft);
            case COP1_S_C_NGT:
                return fmt::format("ctx->fcr31 = (FPU_C_NGT_S(ctx->f[{}], ctx->f[{}])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);", fs, ft);
            default:
                return fmt::format("// Unhandled FPU.S instruction: function 0x{:X}", function);
            }
        case COP1_W:
            switch (function)
            {
            case COP1_W_CVT_S:
                return fmt::format("ctx->f[{}] = FPU_CVT_S_W(*(int32_t*)&ctx->f[{}]);", fd, fs);
            default:
                return fmt::format("// Unhandled FPU.W instruction: function 0x{:X}", function);
            }
        default:
            return fmt::format("// Unhandled FPU instruction: format 0x{:X}, function 0x{:X}", format, function);
        }
    }

    std::string CodeGenerator::translateMMIInstruction(const Instruction &inst)
    {
        uint32_t function = inst.function;
        uint8_t rs = inst.rs;
        uint8_t rt = inst.rt;
        uint8_t rd = inst.rd;
        uint8_t sa = inst.sa;
        switch (function)
        {
        case MMI_MFHI1:
            return fmt::format("SET_GPR_U32(ctx, {}, ctx->hi1);", rd);
        case MMI_MTHI1:
            return fmt::format("ctx->hi1 = GPR_U32(ctx, {});", rs);
        case MMI_MFLO1:
            return fmt::format("SET_GPR_U32(ctx, {}, ctx->lo1);", rd);
        case MMI_MTLO1:
            return fmt::format("ctx->lo1 = GPR_U32(ctx, {});", rs);
        case MMI_MULT1:
            return fmt::format("{{ int64_t result = (int64_t)GPR_S32(ctx, {}) * (int64_t)GPR_S32(ctx, {}); ctx->lo1 = (uint32_t)result; ctx->hi1 = (uint32_t)(result >> 32); }}", rs, rt);
        case MMI_MULTU1:
            return fmt::format("{{ uint64_t result = (uint64_t)GPR_U32(ctx, {}) * (uint64_t)GPR_U32(ctx, {}); ctx->lo1 = (uint32_t)result; ctx->hi1 = (uint32_t)(result >> 32); }}", rs, rt);
        case MMI_DIV1:
            return fmt::format("{{ int32_t divisor = GPR_S32(ctx, {}); if (divisor != 0) {{ ctx->lo1 = (uint32_t)(GPR_S32(ctx, {}) / divisor); ctx->hi1 = (uint32_t)(GPR_S32(ctx, {}) % divisor); }} else {{ ctx->lo1= (GPR_S32(ctx,{}) < 0) ? 1 : -1; ctx->hi1=GPR_S32(ctx,{}); }} }}", rt, rs, rt, rs, rt);
        case MMI_DIVU1:
            return fmt::format("{{ uint32_t divisor = GPR_U32(ctx, {}); if (divisor != 0) {{ ctx->lo1 = GPR_U32(ctx, {}) / divisor; ctx->hi1 = GPR_U32(ctx, {}) % divisor; }} else {{ ctx->lo1=0xFFFFFFFF; ctx->hi1=GPR_U32(ctx,{}); }} }}", rt, rs, rt, rs, rt);
        case MMI_MADD:
            return fmt::format("{{ int64_t acc = ((int64_t)ctx->hi << 32) | ctx->lo; int64_t prod = (int64_t)GPR_S32(ctx, {}) * (int64_t)GPR_S32(ctx, {}); int64_t result = acc + prod; ctx->lo = (uint32_t)result; ctx->hi = (uint32_t)(result >> 32); }}", rs, rt);
        case MMI_MADDU:
            return fmt::format("{{ uint64_t acc = ((uint64_t)ctx->hi << 32) | ctx->lo; uint64_t prod = (uint64_t)GPR_U32(ctx, {}) * (uint64_t)GPR_U32(ctx, {}); uint64_t result = acc + prod; ctx->lo = (uint32_t)result; ctx->hi = (uint32_t)(result >> 32); }}", rs, rt);
        case MMI_MSUB:
            return fmt::format("{{ int64_t acc = ((int64_t)ctx->hi << 32) | ctx->lo; int64_t prod = (int64_t)GPR_S32(ctx, {}) * (int64_t)GPR_S32(ctx, {}); int64_t result = acc - prod; ctx->lo = (uint32_t)result; ctx->hi = (uint32_t)(result >> 32); }}", rs, rt);
        case MMI_MSUBU:
            return fmt::format("{{ uint64_t acc = ((uint64_t)ctx->hi << 32) | ctx->lo; uint64_t prod = (uint64_t)GPR_U32(ctx, {}) * (uint64_t)GPR_U32(ctx, {}); uint64_t result = acc - prod; ctx->lo = (uint32_t)result; ctx->hi = (uint32_t)(result >> 32); }}", rs, rt);
        case MMI_MADD1:
            return fmt::format("{{ int64_t acc = ((int64_t)ctx->hi1 << 32) | ctx->lo1; int64_t prod = (int64_t)GPR_S32(ctx, {}) * (int64_t)GPR_S32(ctx, {}); int64_t result = acc + prod; ctx->lo1 = (uint32_t)result; ctx->hi1 = (uint32_t)(result >> 32); }}", rs, rt);
        case MMI_MADDU1:
            return fmt::format("{{ uint64_t acc = ((uint64_t)ctx->hi1 << 32) | ctx->lo1; uint64_t prod = (uint64_t)GPR_U32(ctx, {}) * (uint64_t)GPR_U32(ctx, {}); uint64_t result = acc + prod; ctx->lo1 = (uint32_t)result; ctx->hi1 = (uint32_t)(result >> 32); }}", rs, rt);
        case MMI_PLZCW:
            return fmt::format("{{ uint32_t val = GPR_U32(ctx, {}); SET_GPR_U32(ctx, {}, val == 0 ? 32 : __builtin_clz(val)); }}", rs, rd);
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
            return translateMMI0Instruction(inst);
        case MMI_MMI1:
            return translateMMI1Instruction(inst);
        case MMI_MMI2:
            return translateMMI2Instruction(inst);
        case MMI_MMI3:
            return translateMMI3Instruction(inst);
        case MMI_PMFHL:
            return translatePMFHLInstruction(inst);
        case MMI_PMTHL:
            return translatePMTHLInstruction(inst);
        default:
            return fmt::format("// Unhandled MMI instruction: function 0x{:X}", function);
        }
    }

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
            return fmt::format("SET_GPR_VEC(ctx, {}, _mm_adds_epi32(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PSUBSW:
            return fmt::format("SET_GPR_VEC(ctx, {}, _mm_subs_epi32(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
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
            return fmt::format("// Unhandled PEXT5 instruction: function 0x{:X}", subfunc);
        case MMI0_PPAC5:
            return fmt::format("// Unhandled PPAC5 instruction: function 0x{:X}", subfunc);
        default:
            return fmt::format("// Unhandled MMI0 instruction: function 0x{:X}", subfunc);
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
            return fmt::format("// Unhandled PADSBH instruction: function 0x{:X}", subfunc);
        case MMI1_PABSH:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PABSH(GPR_VEC(ctx, {})));", rd, rs);
        case MMI1_PCEQH:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PCEQH(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI1_PMINH:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PMINH(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI1_PCEQB:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PCEQB(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI1_PADDUW:
            return fmt::format("SET_GPR_VEC(ctx, {}, _mm_add_epi32(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI1_PSUBUW:
            return fmt::format("SET_GPR_VEC(ctx, {}, _mm_sub_epi32(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
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
            return fmt::format("// Unhandled MMI1 instruction: function 0x{:X}", subfunc);
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
            return fmt::format("// Unhandled PMSUBW instruction: function 0x{:X}", subfunc);
        case MMI2_PMFHI:
            return fmt::format("SET_GPR_U32(ctx, {}, ctx->hi);", rd);
        case MMI2_PMFLO:
            return fmt::format("SET_GPR_U32(ctx, {}, ctx->lo);", rd);
        case MMI2_PINTH:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PINTH(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI2_PMULTW:
            return fmt::format("// Unhandled PMULTW instruction: function 0x{:X}", subfunc);
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
            return fmt::format("// Unhandled PMSUBH instruction: function 0x{:X}", subfunc);
        case MMI2_PHMSBH:
            return fmt::format("// Unhandled PHMSBH instruction: function 0x{:X}", subfunc);
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
            return fmt::format("// Unhandled MMI2 instruction: function 0x{:X}", subfunc);
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
            return fmt::format("Unhandled PMADDUW instruction: function 0x{:X}", subfunc);
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
            return fmt::format("// Unhandled MMI3 instruction: function 0x{:X}", subfunc);
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
            return fmt::format("// Unhandled PMFHL instruction: function 0x{:X}", subfunc);
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
            return fmt::format("// Unhandled PMTHL instruction: function 0x{:X}", subfunc);
        }
    }

    std::string CodeGenerator::translateVUInstruction(const Instruction &inst)
    {
        uint8_t format = inst.rs; // Use parsed rs field for COP2 format
        uint8_t rt = inst.rt;
        uint8_t rd = inst.rd;

        switch (format)
        {
        case COP2_QMFC2:
            return fmt::format("SET_GPR_VEC(ctx, {}, (__m128i)ctx->vu0_vf[{}]);", rt, rd);
        case COP2_CFC2:
        {
            switch (rd) // Control register number is in rd
            {
            case VU0_CR_STATUS:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->vu0_status);", rt);
            case VU0_CR_MAC:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->vu0_mac_flags);", rt);
            case VU0_CR_CLIP:
                return fmt::format("SET_GPR_U32(ctx, {}, ctx->vu0_clip_flags);", rt);
            case VU0_CR_R:
                return fmt::format("SET_GPR_VEC(ctx, {}, (__m128i)ctx->vu0_r);", rt);
            case VU0_CR_I:
                return fmt::format("SET_GPR_U32(ctx, {}, *(uint32_t*)&ctx->vu0_i);", rt);
            default:
                return fmt::format("// Unhandled CFC2 VU CReg: {}", rd);
            }
        }
        case COP2_QMTC2:
            return fmt::format("ctx->vu0_vf[{}] = (__m128)GPR_VEC(ctx, {});", rd, rt);
        case COP2_CTC2:
        {
            switch (rd) // Control register number is in rd
            {
            case VU0_CR_STATUS:
                return fmt::format("ctx->vu0_status = GPR_U32(ctx, {}) & 0xFFFF;", rt);
            case VU0_CR_MAC:
                return fmt::format("ctx->vu0_mac_flags = GPR_U32(ctx, {});", rt);
            case VU0_CR_CLIP:
                return fmt::format("ctx->vu0_clip_flags = GPR_U32(ctx, {});", rt);
            case VU0_CR_R:
                return fmt::format("ctx->vu0_r = (__m128)GPR_VEC(ctx, {});", rt);
            case VU0_CR_I:
                return fmt::format("ctx->vu0_i = *(float*)&GPR_U32(ctx, {});", rt);
            default:
                return fmt::format("// Unhandled CTC2 VU CReg: {}", rd);
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
            uint8_t vu_func = inst.function;
            if (vu_func >= 0x3C) // Special2 Table
            {
                switch (vu_func)
                {
                case VU0_S2_VDIV:
                    return translateVU_VDIV(inst);
                case VU0_S2_VSQRT:
                    return translateVU_VSQRT(inst);
                case VU0_S2_VRSQRT:
                    return translateVU_VRSQRT(inst);
                case VU0_S2_VWAITQ:
                    return fmt::format("// Unhandled VU0 VWAITQ instruction: 0x{:X}", vu_func);
                case VU0_S2_VMTIR:
                    return translateVU_VMTIR(inst);
                case VU0_S2_VMFIR:
                    return translateVU_VMFIR(inst);
                case VU0_S2_VILWR:
                    return translateVU_VILWR(inst);
                case VU0_S2_VISWR:
                    return translateVU_VISWR(inst);
                case VU0_S2_VRNEXT:
                    return translateVU_VRNEXT(inst);
                case VU0_S2_VRGET:
                    return translateVU_VRGET(inst);
                case VU0_S2_VRINIT:
                    return translateVU_VRINIT(inst);
                case VU0_S2_VRXOR:
                    return translateVU_VRXOR(inst);
                case VU0_S2_VABS:
                    return fmt::format("ctx->vu0_vf[{}] = _mm_andnot_ps(_mm_set1_ps(-0.0f), ctx->vu0_vf[{}]);", inst.rt, inst.rs); // FT, FS
                case VU0_S2_VNOP:
                    return fmt::format("// NOP operation, no action needed for VU0"); // No operation
                case VU0_S2_VMOVE:
                    return fmt::format("ctx->vu0_vf[{}] = ctx->vu0_vf[{}];", inst.rt, inst.rs); // FT, FS
                case VU0_S2_VMR32:
                    return fmt::format("ctx->vu0_vf[{}] = _mm_shuffle_ps(ctx->vu0_vf[{}], ctx->vu0_vf[{}], _MM_SHUFFLE(0,0,0,1));", inst.rt, inst.rs, inst.rs); // FT, FS
                default:
                    return fmt::format("// Unhandled VU0 Special2 function: 0x{:X}", vu_func);
                }
            }
            else // Special1 Table
            {
                switch (vu_func)
                {
                case VU0_S1_VADDx:
                case VU0_S1_VADDy:
                case VU0_S1_VADDz:
                case VU0_S1_VADDw:
                    return translateVU_VADD_Field(inst);
                case VU0_S1_VSUBx:
                case VU0_S1_VSUBy:
                case VU0_S1_VSUBz:
                case VU0_S1_VSUBw:
                    return translateVU_VSUB_Field(inst);
                case VU0_S1_VMULx:
                case VU0_S1_VMULy:
                case VU0_S1_VMULz:
                case VU0_S1_VMULw:
                    return translateVU_VMUL_Field(inst);
                case VU0_S1_VADD:
                    return translateVU_VADD(inst);
                case VU0_S1_VSUB:
                    return translateVU_VSUB(inst);
                case VU0_S1_VMUL:
                    return translateVU_VMUL(inst);
                case VU0_S1_VIADD:
                    return translateVU_VIADD(inst);
                case VU0_S1_VISUB:
                    return translateVU_VISUB(inst);
                case VU0_S1_VIADDI:
                    return translateVU_VIADDI(inst);
                case VU0_S1_VIAND:
                    return translateVU_VIAND(inst);
                case VU0_S1_VIOR:
                    return translateVU_VIOR(inst);
                case VU0_S1_VCALLMS:
                    return translateVU_VCALLMS(inst);
                case VU0_S1_VCALLMSR:
                    return translateVU_VCALLMSR(inst);
                case VU0_S1_VADDq:
                    return fmt::format("ctx->vu0_vf[{}] = PS2_VADD(ctx->vu0_vf[{}], _mm_set1_ps(ctx->vu0_q));", inst.rd, inst.rs);
                case VU0_S1_VSUBq:
                    return fmt::format("ctx->vu0_vf[{}] = PS2_VSUB(ctx->vu0_vf[{}], _mm_set1_ps(ctx->vu0_q));", inst.rd, inst.rs);
                case VU0_S1_VMULq:
                    return fmt::format("ctx->vu0_vf[{}] = PS2_VMUL(ctx->vu0_vf[{}], _mm_set1_ps(ctx->vu0_q));", inst.rd, inst.rs);
                case VU0_S1_VADDi:
                    return fmt::format("ctx->vu0_vf[{}] = PS2_VADD(ctx->vu0_vf[{}], _mm_set1_ps(ctx->vu0_i));", inst.rd, inst.rs);
                case VU0_S1_VSUBi:
                    return fmt::format("ctx->vu0_vf[{}] = PS2_VSUB(ctx->vu0_vf[{}], _mm_set1_ps(ctx->vu0_i));", inst.rd, inst.rs);
                case VU0_S1_VMULi:
                    return fmt::format("ctx->vu0_vf[{}] = PS2_VMUL(ctx->vu0_vf[{}], _mm_set1_ps(ctx->vu0_i));", inst.rd, inst.rs);
                default:
                    return fmt::format("// Unhandled VU0 Special1 function: 0x{:X}", vu_func);
                }
            }
        }
        default:
            return fmt::format("// Unhandled COP2 format: 0x{:X}", format);
        }
    }

    std::string CodeGenerator::translateVU_VADD_Field(const Instruction &inst)
    {
        uint8_t dest_mask = inst.vectorInfo.vectorField;
        return fmt::format("{{ __m128 res = PS2_VADD(ctx->vu0_vf[{}], ctx->vu0_vf[{}]); __m128i mask = _mm_set_epi32({}, {}, {}, {}); ctx->vu0_vf[{}] = _mm_blendv_ps(ctx->vu0_vf[{}], res, _mm_castsi128_ps(mask)); }}", inst.rs, inst.rt, (dest_mask & 0x8) ? -1 : 0, (dest_mask & 0x4) ? -1 : 0, (dest_mask & 0x2) ? -1 : 0, (dest_mask & 0x1) ? -1 : 0, inst.rd, inst.rd);
    }

    std::string CodeGenerator::translateVU_VSUB_Field(const Instruction &inst)
    {
        uint8_t dest_mask = inst.vectorInfo.vectorField;
        return fmt::format("{{ __m128 res = PS2_VSUB(ctx->vu0_vf[{}], ctx->vu0_vf[{}]); __m128i mask = _mm_set_epi32({}, {}, {}, {}); ctx->vu0_vf[{}] = _mm_blendv_ps(ctx->vu0_vf[{}], res, _mm_castsi128_ps(mask)); }}", inst.rs, inst.rt, (dest_mask & 0x8) ? -1 : 0, (dest_mask & 0x4) ? -1 : 0, (dest_mask & 0x2) ? -1 : 0, (dest_mask & 0x1) ? -1 : 0, inst.rd, inst.rd);
    }

    std::string CodeGenerator::translateVU_VMUL_Field(const Instruction &inst)
    {
        uint8_t dest_mask = inst.vectorInfo.vectorField;
        return fmt::format("{{ __m128 res = PS2_VMUL(ctx->vu0_vf[{}], ctx->vu0_vf[{}]); __m128i mask = _mm_set_epi32({}, {}, {}, {}); ctx->vu0_vf[{}] = _mm_blendv_ps(ctx->vu0_vf[{}], res, _mm_castsi128_ps(mask)); }}", inst.rs, inst.rt, (dest_mask & 0x8) ? -1 : 0, (dest_mask & 0x4) ? -1 : 0, (dest_mask & 0x2) ? -1 : 0, (dest_mask & 0x1) ? -1 : 0, inst.rd, inst.rd);
    }

    std::string CodeGenerator::translateVU_VADD(const Instruction &inst)
    {
        uint8_t dest_mask = inst.vectorInfo.vectorField;
        return fmt::format("{{ __m128 res = PS2_VADD(ctx->vu0_vf[{}], ctx->vu0_vf[{}]); __m128i mask = _mm_set_epi32({}, {}, {}, {}); ctx->vu0_vf[{}] = _mm_blendv_ps(ctx->vu0_vf[{}], res, _mm_castsi128_ps(mask)); }}", inst.rs, inst.rt, (dest_mask & 0x8) ? -1 : 0, (dest_mask & 0x4) ? -1 : 0, (dest_mask & 0x2) ? -1 : 0, (dest_mask & 0x1) ? -1 : 0, inst.rd, inst.rd);
    }

    std::string CodeGenerator::translateVU_VSUB(const Instruction &inst)
    {
        uint8_t dest_mask = inst.vectorInfo.vectorField;
        return fmt::format("{{ __m128 res = PS2_VSUB(ctx->vu0_vf[{}], ctx->vu0_vf[{}]); __m128i mask = _mm_set_epi32({}, {}, {}, {}); ctx->vu0_vf[{}] = _mm_blendv_ps(ctx->vu0_vf[{}], res, _mm_castsi128_ps(mask)); }}", inst.rs, inst.rt, (dest_mask & 0x8) ? -1 : 0, (dest_mask & 0x4) ? -1 : 0, (dest_mask & 0x2) ? -1 : 0, (dest_mask & 0x1) ? -1 : 0, inst.rd, inst.rd);
    }

    std::string CodeGenerator::translateVU_VMUL(const Instruction &inst)
    {
        uint8_t dest_mask = inst.vectorInfo.vectorField;
        return fmt::format("{{ __m128 res = PS2_VMUL(ctx->vu0_vf[{}], ctx->vu0_vf[{}]); __m128i mask = _mm_set_epi32({}, {}, {}, {}); ctx->vu0_vf[{}] = _mm_blendv_ps(ctx->vu0_vf[{}], res, _mm_castsi128_ps(mask)); }}", inst.rs, inst.rt, (dest_mask & 0x8) ? -1 : 0, (dest_mask & 0x4) ? -1 : 0, (dest_mask & 0x2) ? -1 : 0, (dest_mask & 0x1) ? -1 : 0, inst.rd, inst.rd);
    }

    std::string CodeGenerator::translatePMADDW(const Instruction &inst)
    {
        return fmt::format("{{ __m128i p01 = _mm_mul_epu32(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})); \n"                                       // [p1, p0] 64b each
                           "   __m128i p23 = _mm_mul_epu32(_mm_srli_si128(GPR_VEC(ctx, {}), 8), _mm_srli_si128(GPR_VEC(ctx, {}), 8)); \n" // [p3, p2] 64b each
                           "   uint64_t acc = ((uint64_t)ctx->hi << 32) | ctx->lo; \n"
                           "   acc += _mm_cvtsi128_si64(p01); \n"                    // Add product 0
                           "   acc += _mm_cvtsi128_si64(_mm_srli_si128(p01, 8)); \n" // Add product 1
                           "   acc += _mm_cvtsi128_si64(p23); \n"                    // Add product 2
                           "   acc += _mm_cvtsi128_si64(_mm_srli_si128(p23, 8)); \n" // Add product 3
                           "   ctx->lo = (uint32_t)acc; ctx->hi = (uint32_t)(acc >> 32); \n"
                           "   SET_GPR_U64(ctx, {}, acc); }}", // Store 64-bit acc result in rd
                           inst.rs, inst.rt, inst.rs, inst.rt, inst.rd);
    }

    std::string CodeGenerator::translatePDIVW(const Instruction &inst)
    {
        // Only divides the first word element rs[0] / rt[0]
        return fmt::format("{{ int32_t rs0 = GPR_S32(ctx, {}); int32_t rt0 = GPR_S32(ctx, {}); \n"
                           "   if (rt0 != 0) {{ ctx->lo = (uint32_t)(rs0 / rt0); ctx->hi = (uint32_t)(rs0 % rt0); }} \n"
                           "   else {{ ctx->lo = (rs0 < 0) ? 1 : -1; ctx->hi = rs0; }} \n" // Div by zero behavior
                           "   SET_GPR_U32(ctx, {}, ctx->lo); }}",                         // Store quotient in rd[0]
                           inst.rs, inst.rt, inst.rd);
    }

    std::string CodeGenerator::translatePCPYLD(const Instruction &inst)
    {
        // Copies lower 64 of rs to lower 64 of rd, lower 64 of rt to upper 64 of rd
        return fmt::format("SET_GPR_VEC(ctx, {}, _mm_unpacklo_epi64(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));",
                           inst.rd, inst.rs, inst.rt); // Order matters for unpack
    }

    std::string CodeGenerator::translatePMADDH(const Instruction &inst)
    {
        // Parallel multiply add halfword -> results to HI/LO and rd
        return fmt::format("{{ __m128i prod = _mm_madd_epi16(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})); \n" // Packed multiply and add adjacent pairs
                           "   int32_t p0 = _mm_cvtsi128_si32(prod); \n"
                           "   int32_t p1 = _mm_cvtsi128_si32(_mm_srli_si128(prod, 4)); \n"
                           "   int32_t p2 = _mm_cvtsi128_si32(_mm_srli_si128(prod, 8)); \n"
                           "   int32_t p3 = _mm_cvtsi128_si32(_mm_srli_si128(prod, 12)); \n"
                           "   int64_t acc = ((int64_t)ctx->hi << 32) | ctx->lo; \n"
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
                           "   int64_t acc = ((int64_t)ctx->hi << 32) | ctx->lo; \n"
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
        return fmt::format("{{ __m128i mask = _mm_set_epi8(0,1, 2,3, 4,5, 6,7, 8,9, 10,11, 12,13, 14,15); "
                           "SET_GPR_VEC(ctx, {}, _mm_shuffle_epi8(GPR_VEC(ctx, {}), mask)); }}",
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
        // Divide each element of rs by the first element of rt
        return fmt::format("{{ int32_t div = GPR_S32(ctx, {}); \n"
                           "   int32_t r0 = GPR_S32(ctx, {}); int32_t r1 = GPR_S32(ctx, {}); \n"
                           "   int32_t r2 = GPR_S32(ctx, {}); int32_t r3 = GPR_S32(ctx, {}); \n"
                           "   int32_t q0=0, q1=0, q2=0, q3=0; \n"
                           "   if (div != 0) {{ \n"
                           "       q0 = r0 / div; ctx->lo = q0; ctx->hi = r0 % div; \n" // HI/LO only from first element
                           "       q1 = r1 / div; q2 = r2 / div; q3 = r3 / div; \n"
                           "   }} else {{ ctx->lo = (r0 < 0) ? 1 : -1; ctx->hi = r0; }} \n"
                           "   SET_GPR_VEC(ctx, {}, _mm_set_epi32(q3, q2, q1, q0)); }}",
                           inst.rt, inst.rs + 0, inst.rs + 1, inst.rs + 2, inst.rs + 3, // TODO check if GPR_S32 allows offset indexing
                           inst.rd);
    }

    std::string CodeGenerator::translatePEXEW(const Instruction &inst)
    {
        // Swaps words 0<->2 and 1<->3
        return fmt::format("SET_GPR_VEC(ctx, {}, _mm_shuffle_epi32(GPR_VEC(ctx, {}), _MM_SHUFFLE(1,0,3,2)));",
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

    std::string CodeGenerator::translateVU_VDIV(const Instruction &inst)
    {
        uint8_t fsf = inst.vectorInfo.fsf;
        uint8_t ftf = inst.vectorInfo.ftf;
        uint8_t fs_reg = inst.rs;
        uint8_t ft_reg = inst.rt;

        return fmt::format("{{ float fs = _mm_cvtss_f32(_mm_shuffle_ps(ctx->vu0_vf[{}], ctx->vu0_vf[{}], _MM_SHUFFLE(0,0,0,{}))); float ft = _mm_cvtss_f32(_mm_shuffle_ps(ctx->vu0_vf[{}], ctx->vu0_vf[{}], _MM_SHUFFLE(0,0,0,{}))); ctx->vu0_q = (ft != 0.0f) ? (fs / ft) : 0.0f; }}", fs_reg, fs_reg, fsf, ft_reg, ft_reg, ftf);
    }

    std::string CodeGenerator::translateVU_VSQRT(const Instruction &inst)
    {
        uint8_t ftf = inst.vectorInfo.ftf;
        uint8_t ft_reg = inst.rt;
        return fmt::format("{{ float ft = _mm_cvtss_f32(_mm_shuffle_ps(ctx->vu0_vf[{}], ctx->vu0_vf[{}], _MM_SHUFFLE(0,0,0,{}))); ctx->vu0_q = sqrtf(std::max(0.0f, ft)); }}", ft_reg, ft_reg, ftf);
    }

    std::string CodeGenerator::translateVU_VRSQRT(const Instruction &inst)
    {
        uint8_t ftf = inst.vectorInfo.ftf;
        uint8_t ft_reg = inst.rt;
        return fmt::format("{{ float ft = _mm_cvtss_f32(_mm_shuffle_ps(ctx->vu0_vf[{}], ctx->vu0_vf[{}], _MM_SHUFFLE(0,0,0,{}))); ctx->vu0_q = (ft > 0.0f) ? (1.0f / sqrtf(ft)) : 0.0f; }}", ft_reg, ft_reg, ftf);
    }

    std::string CodeGenerator::translateVU_VMTIR(const Instruction &inst)
    {
        return fmt::format("ctx->vu0_i = (float)ctx->vi[{}];", inst.rt); // rt = IT
    }

    std::string CodeGenerator::translateVU_VMFIR(const Instruction &inst)
    {
        uint8_t dest_mask = inst.vectorInfo.vectorField;                                                                                                                                                                                                                                                                                                                    // Use parsed field
        return fmt::format("{{ float val = (float)ctx->vi[{}]; __m128 res = _mm_set1_ps(val); __m128i mask = _mm_set_epi32({}, {}, {}, {}); ctx->vu0_vf[{}] = _mm_blendv_ps(ctx->vu0_vf[{}], res, _mm_castsi128_ps(mask)); }}", inst.rs, (dest_mask & 0x8) ? -1 : 0, (dest_mask & 0x4) ? -1 : 0, (dest_mask & 0x2) ? -1 : 0, (dest_mask & 0x1) ? -1 : 0, inst.rt, inst.rt); // rs=IS, rt=FT
    }

    std::string CodeGenerator::translateVU_VILWR(const Instruction &inst)
    {
        uint8_t field_idx = inst.vectorInfo.ftf;                                                                                                                                                                                                 // Use parsed ftf field
        return fmt::format("{{ uint32_t addr = (uint32_t)(_mm_cvtss_f32(_mm_shuffle_ps(ctx->vu0_vf[{}], ctx->vu0_vf[{}], _MM_SHUFFLE(0,0,0,{}))) + ctx->vu0_i) & 0x3FFC; ctx->vi[{}] = READ32(addr); }}", inst.rs, inst.rs, field_idx, inst.rt); // rs=IS, rt=IT
    }

    std::string CodeGenerator::translateVU_VISWR(const Instruction &inst)
    {
        uint8_t field_idx = inst.vectorInfo.ftf;                                                                                                                                                                                                 // Use parsed ftf field
        return fmt::format("{{ uint32_t addr = (uint32_t)(_mm_cvtss_f32(_mm_shuffle_ps(ctx->vu0_vf[{}], ctx->vu0_vf[{}], _MM_SHUFFLE(0,0,0,{}))) + ctx->vu0_i) & 0x3FFC; WRITE32(addr, ctx->vi[{}]); }}", inst.rs, inst.rs, field_idx, inst.rt); // rs=IS, rt=IT
    }

    std::string CodeGenerator::translateVU_VIADD(const Instruction &inst)
    {
        return fmt::format("ctx->vi[{}] = ctx->vi[{}] + ctx->vi[{}];", inst.rd, inst.rs, inst.rt); // rd=ID, rs=IS, rt=IT
    }

    std::string CodeGenerator::translateVU_VISUB(const Instruction &inst)
    {
        return fmt::format("ctx->vi[{}] = ctx->vi[{}] - ctx->vi[{}];", inst.rd, inst.rs, inst.rt); // rd=ID, rs=IS, rt=IT
    }

    std::string CodeGenerator::translateVU_VIADDI(const Instruction &inst)
    {
        return fmt::format("ctx->vi[{}] = ctx->vi[{}] + {};", inst.rt, inst.rs, inst.sa); // rt=IT, rs=IS, sa=Imm5
    }

    std::string CodeGenerator::translateVU_VIAND(const Instruction &inst)
    {
        return fmt::format("ctx->vi[{}] = ctx->vi[{}] & ctx->vi[{}];", inst.rd, inst.rs, inst.rt); // rd=ID, rs=IS, rt=IT
    }

    std::string CodeGenerator::translateVU_VIOR(const Instruction &inst)
    {
        return fmt::format("ctx->vi[{}] = ctx->vi[{}] | ctx->vi[{}];", inst.rd, inst.rs, inst.rt); // rd=ID, rs=IS, rt=IT
    }

    std::string CodeGenerator::translateVU_VCALLMS(const Instruction &inst)
    {
        return fmt::format("// Calls VU0 microprogram at address {} - not implemented in recompiled code", inst.immediate);
    }

    std::string CodeGenerator::translateVU_VCALLMSR(const Instruction &inst)
    {
        return fmt::format("// Calls VU0 microprogram at address {} - not implemented in recompiled code", inst.immediate);
    }

    std::string CodeGenerator::translateVU_VRNEXT(const Instruction &inst)
    {
        return fmt::format("// Unhandled VU0 VRNEXT instruction: 0x{:X}", inst.function);
    }

    std::string CodeGenerator::translateVU_VRGET(const Instruction &inst)
    {
        uint8_t dest_mask = inst.vectorInfo.vectorField;
        uint8_t ft_reg = inst.rt;
        return fmt::format("{{ __m128 res = ctx->vu0_r; __m128i mask = _mm_set_epi32({}, {}, {}, {}); ctx->vu0_vf[{}] = _mm_blendv_ps(ctx->vu0_vf[{}], res, _mm_castsi128_ps(mask)); }}", (dest_mask & 0x8) ? -1 : 0, (dest_mask & 0x4) ? -1 : 0, (dest_mask & 0x2) ? -1 : 0, (dest_mask & 0x1) ? -1 : 0, ft_reg, ft_reg);
    }

    std::string CodeGenerator::translateVU_VRINIT(const Instruction &inst)
    {
        return fmt::format("// Unhandled VU0 VRINIT instruction: 0x{:X}", inst.function);
    }

    std::string CodeGenerator::translateVU_VRXOR(const Instruction &inst)
    {
        return fmt::format("// Unhandled VU0 VRXOR instruction: 0x{:X}", inst.function);
    }

    std::string CodeGenerator::translateQFSRV(const Instruction &inst)
    {
        uint8_t rd = inst.rd;
        uint8_t rs = inst.rs;
        uint8_t rt = inst.rt;
        // PS2 MMI QFSRV uses the lower 7 bits of the SA register.
        return fmt::format(
            "{{ \n"
            "    __m128i val_rt = GPR_VEC(ctx, {});\n"       // Get rt (higher bits of the 256-bit value)
            "    __m128i val_rs = GPR_VEC(ctx, {});\n"       // Get rs (lower bits of the 256-bit value)
            "    uint32_t shift_amount = ctx->sa & 0x7F; \n" // Get shift amount (0-127) from SA reg

            // Perform the shift using 64-bit parts for easier SSE2 implementation
            "    uint64_t rt_hi = _mm_cvtsi128_si64(_mm_srli_si128(val_rt, 8));\n"
            "    uint64_t rt_lo = _mm_cvtsi128_si64(val_rt);\n"
            "    uint64_t rs_hi = _mm_cvtsi128_si64(_mm_srli_si128(val_rs, 8));\n"
            "    uint64_t rs_lo = _mm_cvtsi128_si64(val_rs);\n"

            "    __m128i result; \n"
            "    if (shift_amount == 0) {{ \n"
            "        result = val_rs; \n" // No shift, result is just rs
            "    }} else if (shift_amount < 64) {{ \n"
            "        uint64_t res_lo = (rs_lo >> shift_amount) | (rs_hi << (64 - shift_amount)); \n"
            "        uint64_t res_hi = (rs_hi >> shift_amount) | (rt_lo << (64 - shift_amount)); \n"
            "        result = _mm_set_epi64x(res_hi, res_lo); \n"
            "    }} else if (shift_amount == 64) {{ \n"
            "        result = _mm_set_epi64x(rt_lo, rs_hi); \n" // Shift exactly 64 bits
            "    }} else if (shift_amount < 128) {{ \n"         // shift_amount > 64
            "        uint32_t sub_shift = shift_amount - 64; \n"
            "        uint64_t res_lo = (rs_hi >> sub_shift) | (rt_lo << (64 - sub_shift)); \n"
            "        uint64_t res_hi = (rt_lo >> sub_shift) | (rt_hi << (64 - sub_shift)); \n"
            "        result = _mm_set_epi64x(res_hi, res_lo); \n"
            "    }} else {{ // shift_amount >= 128 \n"
            "         uint32_t sub_shift = shift_amount - 128; \n"
            "         uint64_t res_lo = (rt_lo >> sub_shift) | (rt_hi << (64 - sub_shift)); \n" // Shift rt into result
            "         uint64_t res_hi = (rt_hi >> sub_shift); \n"                               // Shift hi part of rt
            "         result = _mm_set_epi64x(res_hi, res_lo); \n"
            "    }} \n"
            "    SET_GPR_VEC(ctx, {}, result); \n"
            "}}",
            rt, rs, rd);
    }

    std::string CodeGenerator::generateFunctionRegistration(const std::vector<Function> &functions,
                                                            const std::map<uint32_t, std::string> &stubs)
    {
        static const std::unordered_map<std::string, std::pair<uint32_t, std::string>> systemCalls = {
            // Memory management
            {"FlushCache", {0x0040, "ps2_syscalls::FlushCache"}},
            {"ResetEE", {0x0042, "ps2_syscalls::ResetEE"}},
            {"SetMemoryMode", {0x0043, "ps2_syscalls::SetMemoryMode"}},

            // Thread management
            {"CreateThread", {0x0055, "ps2_syscalls::CreateThread"}},
            {"DeleteThread", {0x0056, "ps2_syscalls::DeleteThread"}},
            {"StartThread", {0x0057, "ps2_syscalls::StartThread"}},
            {"ExitThread", {0x003C, "ps2_syscalls::ExitThread"}},
            {"ExitDeleteThread", {0x003D, "ps2_syscalls::ExitDeleteThread"}},
            {"TerminateThread", {0x0058, "ps2_syscalls::TerminateThread"}},
            {"SuspendThread", {0x0059, "ps2_syscalls::SuspendThread"}},
            {"ResumeThread", {0x005A, "ps2_syscalls::ResumeThread"}},
            {"GetThreadId", {0x0047, "ps2_syscalls::GetThreadId"}},
            {"ReferThreadStatus", {0x005B, "ps2_syscalls::ReferThreadStatus"}},
            {"SleepThread", {0x005C, "ps2_syscalls::SleepThread"}},
            {"WakeupThread", {0x005D, "ps2_syscalls::WakeupThread"}},
            {"iWakeupThread", {0x005E, "ps2_syscalls::iWakeupThread"}},
            {"ChangeThreadPriority", {0x005F, "ps2_syscalls::ChangeThreadPriority"}},
            {"RotateThreadReadyQueue", {0x0060, "ps2_syscalls::RotateThreadReadyQueue"}},
            {"ReleaseWaitThread", {0x0061, "ps2_syscalls::ReleaseWaitThread"}},
            {"iReleaseWaitThread", {0x0062, "ps2_syscalls::iReleaseWaitThread"}},

            // Semaphores
            {"CreateSema", {0x0064, "ps2_syscalls::CreateSema"}},
            {"DeleteSema", {0x0065, "ps2_syscalls::DeleteSema"}},
            {"SignalSema", {0x0066, "ps2_syscalls::SignalSema"}},
            {"iSignalSema", {0x0067, "ps2_syscalls::iSignalSema"}},
            {"WaitSema", {0x0068, "ps2_syscalls::WaitSema"}},
            {"PollSema", {0x0069, "ps2_syscalls::PollSema"}},
            {"iPollSema", {0x006A, "ps2_syscalls::iPollSema"}},
            {"ReferSemaStatus", {0x006B, "ps2_syscalls::ReferSemaStatus"}},
            {"iReferSemaStatus", {0x006C, "ps2_syscalls::iReferSemaStatus"}},

            // Event flags
            {"CreateEventFlag", {0x006D, "ps2_syscalls::CreateEventFlag"}},
            {"DeleteEventFlag", {0x006E, "ps2_syscalls::DeleteEventFlag"}},
            {"SetEventFlag", {0x006F, "ps2_syscalls::SetEventFlag"}},
            {"iSetEventFlag", {0x0070, "ps2_syscalls::iSetEventFlag"}},
            {"ClearEventFlag", {0x0071, "ps2_syscalls::ClearEventFlag"}},
            {"iClearEventFlag", {0x0072, "ps2_syscalls::iClearEventFlag"}},
            {"WaitEventFlag", {0x0073, "ps2_syscalls::WaitEventFlag"}},
            {"PollEventFlag", {0x0074, "ps2_syscalls::PollEventFlag"}},
            {"iPollEventFlag", {0x0075, "ps2_syscalls::iPollEventFlag"}},
            {"ReferEventFlagStatus", {0x0076, "ps2_syscalls::ReferEventFlagStatus"}},
            {"iReferEventFlagStatus", {0x0077, "ps2_syscalls::iReferEventFlagStatus"}},

            // Alarm
            {"SetAlarm", {0x0078, "ps2_syscalls::SetAlarm"}},
            {"iSetAlarm", {0x0079, "ps2_syscalls::iSetAlarm"}},
            {"CancelAlarm", {0x007A, "ps2_syscalls::CancelAlarm"}},
            {"iCancelAlarm", {0x007B, "ps2_syscalls::iCancelAlarm"}},

            // Intr handlers
            {"EnableIntc", {0x0080, "ps2_syscalls::EnableIntc"}},
            {"DisableIntc", {0x0081, "ps2_syscalls::DisableIntc"}},
            {"EnableDmac", {0x0082, "ps2_syscalls::EnableDmac"}},
            {"DisableDmac", {0x0083, "ps2_syscalls::DisableDmac"}},

            // RPC and IOP
            {"SifStopModule", {0x0085, "ps2_syscalls::SifStopModule"}},
            {"SifLoadModule", {0x0086, "ps2_syscalls::SifLoadModule"}},
            {"SifInitRpc", {0x00A5, "ps2_syscalls::SifInitRpc"}},
            {"SifBindRpc", {0x00A6, "ps2_syscalls::SifBindRpc"}},
            {"SifCallRpc", {0x00A7, "ps2_syscalls::SifCallRpc"}},
            {"SifRegisterRpc", {0x00A8, "ps2_syscalls::SifRegisterRpc"}},
            {"SifCheckStatRpc", {0x00A9, "ps2_syscalls::SifCheckStatRpc"}},
            {"SifSetRpcQueue", {0x00AA, "ps2_syscalls::SifSetRpcQueue"}},
            {"SifRemoveRpcQueue", {0x00AB, "ps2_syscalls::SifRemoveRpcQueue"}},
            {"SifRemoveRpc", {0x00AC, "ps2_syscalls::SifRemoveRpc"}},

            // IO system calls
            {"fioOpen", {0x00B0, "ps2_syscalls::fioOpen"}},
            {"fioClose", {0x00B1, "ps2_syscalls::fioClose"}},
            {"fioRead", {0x00B2, "ps2_syscalls::fioRead"}},
            {"fioWrite", {0x00B3, "ps2_syscalls::fioWrite"}},
            {"fioLseek", {0x00B4, "ps2_syscalls::fioLseek"}},
            {"fioMkdir", {0x00B5, "ps2_syscalls::fioMkdir"}},
            {"fioChdir", {0x00B6, "ps2_syscalls::fioChdir"}},
            {"fioRmdir", {0x00B7, "ps2_syscalls::fioRmdir"}},
            {"fioGetstat", {0x00B8, "ps2_syscalls::fioGetstat"}},
            {"fioRemove", {0x00B9, "ps2_syscalls::fioRemove"}},

            // Graphics
            {"GsSetCrt", {0x00C0, "ps2_syscalls::GsSetCrt"}},
            {"GsGetIMR", {0x00C1, "ps2_syscalls::GsGetIMR"}},
            {"GsPutIMR", {0x00C2, "ps2_syscalls::GsPutIMR"}},
            {"GsSetVideoMode", {0x00C3, "ps2_syscalls::GsSetVideoMode"}},

            // Miscellaneous
            {"GetOsdConfigParam", {0x00F0, "ps2_syscalls::GetOsdConfigParam"}},
            {"SetOsdConfigParam", {0x00F1, "ps2_syscalls::SetOsdConfigParam"}},
            {"GetRomName", {0x00F2, "ps2_syscalls::GetRomName"}},
            {"SifLoadElfPart", {0x00F6, "ps2_syscalls::SifLoadElfPart"}},
            {"sceSifLoadModule", {0x0122, "ps2_syscalls::sceSifLoadModule"}},

            {"TODO", {0x0000, "ps2_syscalls::TODO"}}};

        static const std::unordered_map<std::string, std::string> libraryStubs = {
            // Memory operations
            {"malloc", "ps2_stubs::malloc"},
            {"free", "ps2_stubs::free"},
            {"calloc", "ps2_stubs::calloc"},
            {"realloc", "ps2_stubs::realloc"},
            {"memcpy", "ps2_stubs::memcpy"},
            {"memset", "ps2_stubs::memset"},
            {"memmove", "ps2_stubs::memmove"},
            {"memcmp", "ps2_stubs::memcmp"},

            // String operations
            {"strcpy", "ps2_stubs::strcpy"},
            {"strncpy", "ps2_stubs::strncpy"},
            {"strlen", "ps2_stubs::strlen"},
            {"strcmp", "ps2_stubs::strcmp"},
            {"strncmp", "ps2_stubs::strncmp"},
            {"strcat", "ps2_stubs::strcat"},
            {"strncat", "ps2_stubs::strncat"},
            {"strchr", "ps2_stubs::strchr"},
            {"strrchr", "ps2_stubs::strrchr"},
            {"strstr", "ps2_stubs::strstr"},

            // I/O operations
            {"printf", "ps2_stubs::printf"},
            {"sprintf", "ps2_stubs::sprintf"},
            {"snprintf", "ps2_stubs::snprintf"},
            {"puts", "ps2_stubs::puts"},
            {"fopen", "ps2_stubs::fopen"},
            {"fclose", "ps2_stubs::fclose"},
            {"fread", "ps2_stubs::fread"},
            {"fwrite", "ps2_stubs::fwrite"},
            {"fprintf", "ps2_stubs::fprintf"},
            {"fseek", "ps2_stubs::fseek"},
            {"ftell", "ps2_stubs::ftell"},
            {"fflush", "ps2_stubs::fflush"},

            // Math functions
            {"sqrt", "ps2_stubs::sqrt"},
            {"sin", "ps2_stubs::sin"},
            {"cos", "ps2_stubs::cos"},
            {"tan", "ps2_stubs::tan"},
            {"atan2", "ps2_stubs::atan2"},
            {"pow", "ps2_stubs::pow"},
            {"exp", "ps2_stubs::exp"},
            {"log", "ps2_stubs::log"},
            {"log10", "ps2_stubs::log10"},
            {"ceil", "ps2_stubs::ceil"},
            {"floor", "ps2_stubs::floor"},
            {"fabs", "ps2_stubs::fabs"},

            {"TODO", "ps2_stubs::TODO"}};

        std::stringstream ss;

        std::unordered_set<uint32_t> registeredAddresses;

        // Begin function
        ss << "#include \"ps2_runtime.h\"\n";
        ss << "#include \"ps2_recompiled_functions.h\"\n";
        ss << "#include \"ps2_stubs.h\"\n\n";

        ss << "// Default handler for unimplemented syscalls/functions\n";
        ss << "void ps2_syscalls::TODO(uint8_t* rdram, R5900Context* ctx) {\n";
        ss << "    std::cout << \"Unimplemented syscall/function called at PC=0x\" << std::hex << ctx->pc << std::dec << std::endl;\n";
        ss << "    ctx->r[2] = 0; // Return 0 by default\n";
        ss << "}\n\n";

        ss << "void ps2_stubs::TODO(uint8_t* rdram, R5900Context* ctx) {\n";
        ss << "    std::cout << \"Unimplemented library function called at PC=0x\" << std::hex << ctx->pc << std::dec << std::endl;\n";
        ss << "    ctx->r[2] = 0; // Return 0 by default\n";
        ss << "}\n\n";

        // Registration function
        ss << "void registerAllFunctions(PS2Runtime& runtime) {\n";

        std::vector<std::pair<uint32_t, std::string>> normalFunctions;
        std::vector<std::pair<uint32_t, std::string>> stubFunctions;
        std::vector<std::pair<uint32_t, std::string>> systemCallFunctions;
        std::vector<std::pair<uint32_t, std::string>> libraryFunctions;

        uint32_t libBaseAddr = 0x00110000;
        uint32_t libOffset = 0;

        for (const auto &function : functions)
        {
            if (!function.isRecompiled)
                continue;

            bool isSystemCall = systemCalls.find(function.name) != systemCalls.end();
            bool isLibCall = libraryStubs.find(function.name) != libraryStubs.end();

            if (isSystemCall)
            {
                const auto &syscallInfo = systemCalls.at(function.name);
                systemCallFunctions.push_back({syscallInfo.first, syscallInfo.second});
                continue;
            }

            if (isLibCall)
            {
                uint32_t libAddr = libBaseAddr + (libOffset++ * 4);
                libraryFunctions.push_back({libAddr, libraryStubs.at(function.name)});
                continue;
            }

            if (function.isStub)
            {
                stubFunctions.push_back({function.start, function.name});
            }
            else
            {
                normalFunctions.push_back({function.start, function.name});
            }
        }

        ss << "    // Register recompiled functions\n";
        for (const auto &function : normalFunctions)
        {
            ss << "    runtime.registerFunction(0x" << std::hex << function.first << std::dec
               << ", " << function.second << ");\n";
        }

        ss << "\n    // Register stub functions\n";
        for (const auto &function : stubFunctions)
        {
            ss << "    runtime.registerFunction(0x" << std::hex << function.first << std::dec
               << ", " << function.second << ");\n";
        }

        ss << "\n    // Register system call stubs\n";
        for (const auto &function : systemCallFunctions)
        {
            ss << "    runtime.registerFunction(0x" << std::hex << function.first << std::dec
               << ", " << function.second << ");\n";
        }

        ss << "\n    // Register library stubs\n";
        for (const auto &function : libraryFunctions)
        {
            ss << "    runtime.registerFunction(0x" << std::hex << function.first << std::dec
               << ", " << function.second << ");\n";
        }

        ss << "}\n";

        return ss.str();
    }

    std::string CodeGenerator::generateJumpTableSwitch(const Instruction &inst, uint32_t tableAddress,
                                                       const std::vector<JumpTableEntry> &entries)
    {
        std::stringstream ss;

        uint32_t indexReg = inst.rs;

        ss << "switch (ctx->r[" << indexReg << "]) {\n";

        for (const auto &entry : entries)
        {
            ss << "    case " << entry.index << ": {\n";

            Symbol *sym = findSymbolByAddress(entry.target);
            if (sym && sym->isFunction)
            {
                ss << "        " << sym->name << "(rdram, ctx);\n";
            }
            else
            {
                ss << "        func_" << std::hex << entry.target << std::dec << "(rdram, ctx);\n";
            }

            ss << "        return;\n";
            ss << "    }\n";
        }

        ss << "    default:\n";
        ss << "        // Unknown jump table target\n";
        ss << "        return;\n";
        ss << "}\n";

        return ss.str();
    }

    Symbol *CodeGenerator::findSymbolByAddress(uint32_t address)
    {
        for (auto &symbol : m_symbols)
        {
            if (symbol.address == address)
            {
                return &symbol;
            }
        }

        return nullptr;
    }
};