#include "ps2recomp/code_generator.h"
#include "ps2recomp/instructions.h"
#include <fmt/format.h>
#include <sstream>
#include <algorithm>

namespace ps2recomp
{
    CodeGenerator::CodeGenerator(const std::vector<Symbol> &symbols)
        : m_symbols(symbols)
    {
    }

    std::string CodeGenerator::handleBranchDelaySlots(const Instruction &branchInst, const Instruction &delaySlot)
    {
        std::stringstream ss;

        if (branchInst.opcode == OPCODE_J || branchInst.opcode == OPCODE_JAL)
        {
            // J/JAL instruction
            if (branchInst.opcode == OPCODE_JAL)
            {
                // For JAL, set the return address
                ss << "    ctx->r[31] = 0x" << std::hex << (branchInst.address + 8) << ";\n"
                   << std::dec;
            }

            // Execute delay slot
            ss << "    " << translateInstruction(delaySlot) << "\n";

            // Jump to target
            uint32_t target = (branchInst.address & 0xF0000000) | (branchInst.target << 2);
            Symbol *sym = findSymbolByAddress(target);
            if (sym && sym->isFunction)
            {
                ss << "    " << sym->name << "(rdram, ctx);\n";
                ss << "    return;\n";
            }
            else
            {
                ss << "    // Jump to unknown target: 0x" << std::hex << target << std::dec << "\n";
                ss << "    return;\n";
            }
        }
        else if (branchInst.opcode == OPCODE_SPECIAL &&
                 (branchInst.function == SPECIAL_JR || branchInst.function == SPECIAL_JALR))
        {
            // JR/JALR instruction
            if (branchInst.function == SPECIAL_JALR)
            {
                // For JALR, set the return address
                ss << "    ctx->r[" << branchInst.rd << "] = 0x" << std::hex << (branchInst.address + 8) << ";\n"
                   << std::dec;
            }

            // Execute delay slot
            ss << "    " << translateInstruction(delaySlot) << "\n";

            // Jump to address in register
            if (branchInst.rs == 31 && branchInst.function == SPECIAL_JR)
            {
                // JR $ra - likely a return
                ss << "    return;\n";
            }
            else
            {
                ss << "    LOOKUP_FUNC(ctx->r[" << branchInst.rs << "])(rdram, ctx);\n";
                ss << "    return;\n";
            }
        }
        else if (branchInst.isBranch)
        {
            std::string conditionStr;

            // Generate condition based on branch type
            switch (branchInst.opcode)
            {
            case OPCODE_BEQ:
                conditionStr = fmt::format("ctx->r[{}] == ctx->r[{}]", branchInst.rs, branchInst.rt);
                break;

            case OPCODE_BNE:
                conditionStr = fmt::format("ctx->r[{}] != ctx->r[{}]", branchInst.rs, branchInst.rt);
                break;

            case OPCODE_BLEZ:
                conditionStr = fmt::format("(int32_t)ctx->r[{}] <= 0", branchInst.rs);
                break;

            case OPCODE_BGTZ:
                conditionStr = fmt::format("(int32_t)ctx->r[{}] > 0", branchInst.rs);
                break;

            case OPCODE_BEQL:
                conditionStr = fmt::format("ctx->r[{}] == ctx->r[{}]", branchInst.rs, branchInst.rt);
                break;

            case OPCODE_BNEL:
                conditionStr = fmt::format("ctx->r[{}] != ctx->r[{}]", branchInst.rs, branchInst.rt);
                break;

            case OPCODE_BLEZL:
                conditionStr = fmt::format("(int32_t)ctx->r[{}] <= 0", branchInst.rs);
                break;

            case OPCODE_BGTZL:
                conditionStr = fmt::format("(int32_t)ctx->r[{}] > 0", branchInst.rs);
                break;

            case OPCODE_REGIMM:
                switch (branchInst.rt)
                {
                case REGIMM_BLTZ:
                    conditionStr = fmt::format("(int32_t)ctx->r[{}] < 0", branchInst.rs);
                    break;

                case REGIMM_BGEZ:
                    conditionStr = fmt::format("(int32_t)ctx->r[{}] >= 0", branchInst.rs);
                    break;

                case REGIMM_BLTZL:
                    conditionStr = fmt::format("(int32_t)ctx->r[{}] < 0", branchInst.rs);
                    break;

                case REGIMM_BGEZL:
                    conditionStr = fmt::format("(int32_t)ctx->r[{}] >= 0", branchInst.rs);
                    break;

                case REGIMM_BLTZAL:
                    conditionStr = fmt::format("(int32_t)ctx->r[{}] < 0", branchInst.rs);
                    ss << "    ctx->r[31] = 0x" << std::hex << (branchInst.address + 8) << ";\n"
                       << std::dec;
                    break;

                case REGIMM_BGEZAL:
                    conditionStr = fmt::format("(int32_t)ctx->r[{}] >= 0", branchInst.rs);
                    ss << "    ctx->r[31] = 0x" << std::hex << (branchInst.address + 8) << ";\n"
                       << std::dec;
                    break;

                case REGIMM_BLTZALL:
                    conditionStr = fmt::format("(int32_t)ctx->r[{}] < 0", branchInst.rs);
                    ss << "    ctx->r[31] = 0x" << std::hex << (branchInst.address + 8) << ";\n"
                       << std::dec;
                    break;

                case REGIMM_BGEZALL:
                    conditionStr = fmt::format("(int32_t)ctx->r[{}] >= 0", branchInst.rs);
                    ss << "    ctx->r[31] = 0x" << std::hex << (branchInst.address + 8) << ";\n"
                       << std::dec;
                    break;

                default:
                    conditionStr = "false";
                    break;
                }
                break;

            default:
                conditionStr = "false";
                break;
            }

            int32_t offset = static_cast<int16_t>(branchInst.immediate) << 2;
            uint32_t target = branchInst.address + 4 + offset;

            Symbol *sym = findSymbolByAddress(target);
            std::string targetLabel;

            if (sym && sym->isFunction)
            {
                targetLabel = sym->name;
            }
            else
            {
                targetLabel = fmt::format("func_{:08X}", target);
            }

            bool isLikely = (branchInst.opcode == OPCODE_BEQL ||
                             branchInst.opcode == OPCODE_BNEL ||
                             branchInst.opcode == OPCODE_BLEZL ||
                             branchInst.opcode == OPCODE_BGTZL ||
                             branchInst.rt == REGIMM_BLTZL ||
                             branchInst.rt == REGIMM_BGEZL ||
                             branchInst.rt == REGIMM_BLTZALL ||
                             branchInst.rt == REGIMM_BGEZALL);

            if (isLikely)
            {
                // Likely branches only execute the delay slot if the branch is taken
                ss << "    if (" << conditionStr << ") {\n";
                ss << "        " << translateInstruction(delaySlot) << "\n";
                ss << "        " << targetLabel << "(rdram, ctx);\n";
                ss << "        return;\n";
                ss << "    }\n";
            }
            else
            {
                // Regular branches always execute the delay slot
                ss << "    " << translateInstruction(delaySlot) << "\n";
                ss << "    if (" << conditionStr << ") {\n";
                ss << "        " << targetLabel << "(rdram, ctx);\n";
                ss << "        return;\n";
                ss << "    }\n";
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
        ss << "#define READ8(addr) (*(uint8_t*)((rdram) + ((addr) & 0x1FFFFFF)))\n";
        ss << "#define READ16(addr) (*(uint16_t*)((rdram) + ((addr) & 0x1FFFFFF)))\n";
        ss << "#define READ32(addr) (*(uint32_t*)((rdram) + ((addr) & 0x1FFFFFF)))\n";
        ss << "#define READ64(addr) (*(uint64_t*)((rdram) + ((addr) & 0x1FFFFFF)))\n";
        ss << "#define READ128(addr) (*((__m128i*)((rdram) + ((addr) & 0x1FFFFFF))))\n";
        ss << "#define WRITE8(addr, val) (*(uint8_t*)((rdram) + ((addr) & 0x1FFFFFF)) = (val))\n";
        ss << "#define WRITE16(addr, val) (*(uint16_t*)((rdram) + ((addr) & 0x1FFFFFF)) = (val))\n";
        ss << "#define WRITE32(addr, val) (*(uint32_t*)((rdram) + ((addr) & 0x1FFFFFF)) = (val))\n";
        ss << "#define WRITE64(addr, val) (*(uint64_t*)((rdram) + ((addr) & 0x1FFFFFF)) = (val))\n";
        ss << "#define WRITE128(addr, val) (*((__m128i*)((rdram) + ((addr) & 0x1FFFFFF))) = (val))\n\n";

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
        else if (inst.isVU)
        {
            return translateVUInstruction(inst);
        }

        switch (inst.opcode)
        {
        case OPCODE_SPECIAL:
            return translateSpecialInstruction(inst);

        case OPCODE_ADDI:
        case OPCODE_ADDIU:
            if (inst.rt == 0)
            {
                return "// NOP (addiu $zero, ...)";
            }
            return fmt::format("ctx->r[{}] = ADD32(ctx->r[{}], 0x{:X});",
                               inst.rt, inst.rs, (int16_t)inst.immediate);

        case OPCODE_SLTI:
            return fmt::format("ctx->r[{}] = (int32_t)ctx->r[{}] < (int32_t)0x{:X} ? 1 : 0;",
                               inst.rt, inst.rs, (int16_t)inst.immediate);

        case OPCODE_SLTIU:
            return fmt::format("ctx->r[{}] = ctx->r[{}] < (uint32_t)0x{:X} ? 1 : 0;",
                               inst.rt, inst.rs, (uint32_t)(int16_t)inst.immediate);

        case OPCODE_ANDI:
            return fmt::format("ctx->r[{}] = AND32(ctx->r[{}], 0x{:X});",
                               inst.rt, inst.rs, inst.immediate);

        case OPCODE_ORI:
            return fmt::format("ctx->r[{}] = OR32(ctx->r[{}], 0x{:X});",
                               inst.rt, inst.rs, inst.immediate);

        case OPCODE_XORI:
            return fmt::format("ctx->r[{}] = XOR32(ctx->r[{}], 0x{:X});",
                               inst.rt, inst.rs, inst.immediate);

        case OPCODE_LUI:
            return fmt::format("ctx->r[{}] = 0x{:X} << 16;",
                               inst.rt, inst.immediate);

        case OPCODE_LB:
            return fmt::format("ctx->r[{}] = (int32_t)(int8_t)READ8(ADD32(ctx->r[{}], 0x{:X}));",
                               inst.rt, inst.rs, (int16_t)inst.immediate);

        case OPCODE_LH:
            return fmt::format("ctx->r[{}] = (int32_t)(int16_t)READ16(ADD32(ctx->r[{}], 0x{:X}));",
                               inst.rt, inst.rs, (int16_t)inst.immediate);

        case OPCODE_LW:
            return fmt::format("ctx->r[{}] = READ32(ADD32(ctx->r[{}], 0x{:X}));",
                               inst.rt, inst.rs, (int16_t)inst.immediate);

        case OPCODE_LBU:
            return fmt::format("ctx->r[{}] = (uint32_t)READ8(ADD32(ctx->r[{}], 0x{:X}));",
                               inst.rt, inst.rs, (int16_t)inst.immediate);

        case OPCODE_LHU:
            return fmt::format("ctx->r[{}] = (uint32_t)READ16(ADD32(ctx->r[{}], 0x{:X}));",
                               inst.rt, inst.rs, (int16_t)inst.immediate);

        case OPCODE_SB:
            return fmt::format("WRITE8(ADD32(ctx->r[{}], 0x{:X}), (uint8_t)ctx->r[{}]);",
                               inst.rs, (int16_t)inst.immediate, inst.rt);

        case OPCODE_SH:
            return fmt::format("WRITE16(ADD32(ctx->r[{}], 0x{:X}), (uint16_t)ctx->r[{}]);",
                               inst.rs, (int16_t)inst.immediate, inst.rt);

        case OPCODE_SW:
            return fmt::format("WRITE32(ADD32(ctx->r[{}], 0x{:X}), ctx->r[{}]);",
                               inst.rs, (int16_t)inst.immediate, inst.rt);

        // PS2-specific 128-bit load/store
        case OPCODE_LQ:
            return fmt::format("ctx->r[{}] = (__m128i)READ128(ADD32(ctx->r[{}], 0x{:X}));",
                               inst.rt, inst.rs, (int16_t)inst.immediate);

        case OPCODE_SQ:
            return fmt::format("WRITE128(ADD32(ctx->r[{}], 0x{:X}), (__m128i)ctx->r[{}]);",
                               inst.rs, (int16_t)inst.immediate, inst.rt);

        case OPCODE_LD:
            return fmt::format("{{ uint64_t val = READ64(ADD32(ctx->r[{}], 0x{:X})); ctx->r[{}].m128i_u32[0] = (uint32_t)val; ctx->r[{}].m128i_u32[1] = (uint32_t)(val >> 32); }}",
                               inst.rs, (int16_t)inst.immediate, inst.rt, inst.rt);

        case OPCODE_SD:
            return fmt::format("{{ uint64_t val = ((uint64_t)ctx->r[{}].m128i_u32[1] << 32) | ctx->r[{}].m128i_u32[0]; WRITE64(ADD32(ctx->r[{}], 0x{:X}), val); }}",
                               inst.rt, inst.rt, inst.rs, (int16_t)inst.immediate);

        case OPCODE_SWC1:
            return fmt::format("{{ float val = ctx->f[{}]; WRITE32(ADD32(ctx->r[{}], 0x{:X}), *(uint32_t*)&val); }}",
                               inst.rt, inst.rs, (int16_t)inst.immediate);

        case OPCODE_LQC2:
            return fmt::format("ctx->vu0_vf[{}] = (__m128)READ128(ADD32(ctx->r[{}], 0x{:X}));",
                               inst.rt, inst.rs, (int16_t)inst.immediate);

        case OPCODE_SQC2:
            return fmt::format("WRITE128(ADD32(ctx->r[{}], 0x{:X}), (__m128i)ctx->vu0_vf[{}]);",
                               inst.rs, (int16_t)inst.immediate, inst.rt);

        case OPCODE_LWC1:
            return fmt::format("{{ uint32_t val = READ32(ADD32(ctx->r[{}], 0x{:X})); ctx->f[{}] = *(float*)&val; }}",
                               inst.rs, (int16_t)inst.immediate, inst.rt);

        case OPCODE_LWU:
            return fmt::format("ctx->r[{}] = (uint32_t)READ32(ADD32(ctx->r[{}], 0x{:X}));",
                               inst.rt, inst.rs, (int16_t)inst.immediate);

        // Special case for R5900
        case OPCODE_CACHE:
            return "// CACHE instruction (ignored)";

        case OPCODE_PREF:
            return "// PREF instruction (ignored)";

        case OPCODE_COP1:
            return translateFPUInstruction(inst);

        case OPCODE_COP0:
            return translateCOP0Instruction(inst);

        // REGIMM special format (opcode=0x01)
        case OPCODE_REGIMM:
            switch (inst.rt)
            {
            case REGIMM_BLTZ:
                return fmt::format("// BLTZ r{}, 0x{:X} - Handled by branch logic",
                                   inst.rs, inst.address + 4 + ((int16_t)inst.immediate << 2));

            case REGIMM_BGEZ:
                return fmt::format("// BGEZ r{}, 0x{:X} - Handled by branch logic",
                                   inst.rs, inst.address + 4 + ((int16_t)inst.immediate << 2));

            case REGIMM_BLTZAL:
                return fmt::format("// BLTZAL r{}, 0x{:X} - Handled by branch logic",
                                   inst.rs, inst.address + 4 + ((int16_t)inst.immediate << 2));

            case REGIMM_BGEZAL:
                return fmt::format("// BGEZAL r{}, 0x{:X} - Handled by branch logic",
                                   inst.rs, inst.address + 4 + ((int16_t)inst.immediate << 2));

            case REGIMM_MTSAB:
                return fmt::format("ctx->sa = (ctx->r[{}] + 0x{:X}) & 0x0F;",
                                   inst.rs, inst.immediate);

            case REGIMM_MTSAH:
                return fmt::format("ctx->sa = ((ctx->r[{}] + 0x{:X}) & 0x07) << 1;",
                                   inst.rs, inst.immediate);

            case REGIMM_TGEI:
                return fmt::format("if ((int32_t)ctx->r[{}] >= (int32_t)0x{:X}) {{ /* Trap */ }}",
                                   inst.rs, (int16_t)inst.immediate);

            case REGIMM_TGEIU:
                return fmt::format("if (ctx->r[{}] >= (uint32_t)0x{:X}) {{ /* Trap */ }}",
                                   inst.rs, (uint32_t)(int16_t)inst.immediate);

            case REGIMM_TLTI:
                return fmt::format("if ((int32_t)ctx->r[{}] < (int32_t)0x{:X}) {{ /* Trap */ }}",
                                   inst.rs, (int16_t)inst.immediate);

            case REGIMM_TLTIU:
                return fmt::format("if (ctx->r[{}] < (uint32_t)0x{:X}) {{ /* Trap */ }}",
                                   inst.rs, (uint32_t)(int16_t)inst.immediate);

            case REGIMM_TEQI:
                return fmt::format("if ((int32_t)ctx->r[{}] == (int32_t)0x{:X}) {{ /* Trap */ }}",
                                   inst.rs, (int16_t)inst.immediate);

            case REGIMM_TNEI:
                return fmt::format("if ((int32_t)ctx->r[{}] != (int32_t)0x{:X}) {{ /* Trap */ }}",
                                   inst.rs, (int16_t)inst.immediate);

            default:
                return fmt::format("// Unhandled REGIMM instruction: 0x{:X}", inst.rt);
            }

        // MIPS-IV special format opcodes
        case OPCODE_BEQL:
        case OPCODE_BNEL:
        case OPCODE_BLEZL:
        case OPCODE_BGTZL:
            // Already handled in branch delay slot logic
            return fmt::format("// Likely branch instruction at 0x{:X} - Handled by branch logic", inst.address);

        case OPCODE_DADDI:
            return fmt::format("{{ int64_t a = ((int64_t)ctx->r[{}].m128i_i32[1] << 32) | ctx->r[{}].m128i_u32[0]; "
                               "int64_t res = a + (int64_t)(int16_t)0x{:X}; "
                               "ctx->r[{}].m128i_u32[0] = (uint32_t)res; ctx->r[{}].m128i_u32[1] = (uint32_t)(res >> 32); }}",
                               inst.rs, inst.rs, inst.immediate, inst.rt, inst.rt);

        case OPCODE_DADDIU:
            return fmt::format("{{ uint64_t a = ((uint64_t)ctx->r[{}].m128i_u32[1] << 32) | ctx->r[{}].m128i_u32[0]; "
                               "uint64_t res = a + (uint64_t)(int16_t)0x{:X}; "
                               "ctx->r[{}].m128i_u32[0] = (uint32_t)res; ctx->r[{}].m128i_u32[1] = (uint32_t)(res >> 32); }}",
                               inst.rs, inst.rs, inst.immediate, inst.rt, inst.rt);

        case OPCODE_LDL:
        case OPCODE_LDR:
        case OPCODE_SDL:
        case OPCODE_SDR:
        case OPCODE_LWL:
        case OPCODE_LWR:
        case OPCODE_SWL:
        case OPCODE_SWR:
            return fmt::format("// Unaligned load/store instruction 0x{:X} not implemented", inst.opcode);

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
            {
                return "// NOP";
            }
            return fmt::format("ctx->r[{}] = SLL32(ctx->r[{}], {});",
                               inst.rd, inst.rt, inst.sa);

        case SPECIAL_SRL:
            return fmt::format("ctx->r[{}] = SRL32(ctx->r[{}], {});",
                               inst.rd, inst.rt, inst.sa);

        case SPECIAL_SRA:
            return fmt::format("ctx->r[{}] = SRA32(ctx->r[{}], {});",
                               inst.rd, inst.rt, inst.sa);

        case SPECIAL_SLLV:
            return fmt::format("ctx->r[{}] = SLL32(ctx->r[{}], ctx->r[{}] & 0x1F);",
                               inst.rd, inst.rt, inst.rs);

        case SPECIAL_SRLV:
            return fmt::format("ctx->r[{}] = SRL32(ctx->r[{}], ctx->r[{}] & 0x1F);",
                               inst.rd, inst.rt, inst.rs);

        case SPECIAL_SRAV:
            return fmt::format("ctx->r[{}] = SRA32(ctx->r[{}], ctx->r[{}] & 0x1F);",
                               inst.rd, inst.rt, inst.rs);

        case SPECIAL_JR:
            // Handled by branch delay slots
            return fmt::format("// JR ${} - Handled by branch logic", inst.rs);

        case SPECIAL_JALR:
            // Handled by branch delay slots
            return fmt::format("// JALR ${}, ${} - Handled by branch logic", inst.rd, inst.rs);

        case SPECIAL_SYSCALL:
            return fmt::format("// SYSCALL 0x{:X}", (inst.raw & 0x03FFFFC0) >> 6);

        case SPECIAL_BREAK:
            return fmt::format("// BREAK 0x{:X}", (inst.raw & 0x03FFFFC0) >> 6);

        case SPECIAL_SYNC:
            return translateSYNC(inst);

        case SPECIAL_MFHI:
            return fmt::format("ctx->r[{}] = ctx->hi;", inst.rd);

        case SPECIAL_MTHI:
            return fmt::format("ctx->hi = ctx->r[{}];", inst.rs);

        case SPECIAL_MFLO:
            return fmt::format("ctx->r[{}] = ctx->lo;", inst.rd);

        case SPECIAL_MTLO:
            return fmt::format("ctx->lo = ctx->r[{}];", inst.rs);

        case SPECIAL_DSLLV:
            return translateDSLLV(inst);

        case SPECIAL_DSRLV:
            return translateDSRLV(inst);

        case SPECIAL_DSRAV:
            return translateDSRAV(inst);

        case SPECIAL_MULT:
            return fmt::format("{{ int64_t result = (int64_t)(int32_t)ctx->r[{}] * (int64_t)(int32_t)ctx->r[{}]; ctx->lo = (uint32_t)result; ctx->hi = (uint32_t)(result >> 32); }}",
                               inst.rs, inst.rt);

        case SPECIAL_MULTU:
            return fmt::format("{{ uint64_t result = (uint64_t)ctx->r[{}] * (uint64_t)ctx->r[{}]; ctx->lo = (uint32_t)result; ctx->hi = (uint32_t)(result >> 32); }}",
                               inst.rs, inst.rt);

        case SPECIAL_DIV:
            return fmt::format("{{ if (ctx->r[{}] != 0) {{ ctx->lo = (uint32_t)((int32_t)ctx->r[{}] / (int32_t)ctx->r[{}]); ctx->hi = (uint32_t)((int32_t)ctx->r[{}] % (int32_t)ctx->r[{}]); }} }}",
                               inst.rt, inst.rs, inst.rt, inst.rs, inst.rt);

        case SPECIAL_DIVU:
            return fmt::format("{{ if (ctx->r[{}] != 0) {{ ctx->lo = ctx->r[{}] / ctx->r[{}]; ctx->hi = ctx->r[{}] % ctx->r[{}]; }} }}",
                               inst.rt, inst.rs, inst.rt, inst.rs, inst.rt);

        case SPECIAL_ADD:
        case SPECIAL_ADDU:
            return fmt::format("ctx->r[{}] = ADD32(ctx->r[{}], ctx->r[{}]);",
                               inst.rd, inst.rs, inst.rt);

        case SPECIAL_SUB:
        case SPECIAL_SUBU:
            return fmt::format("ctx->r[{}] = SUB32(ctx->r[{}], ctx->r[{}]);",
                               inst.rd, inst.rs, inst.rt);

        case SPECIAL_AND:
            return fmt::format("ctx->r[{}] = AND32(ctx->r[{}], ctx->r[{}]);",
                               inst.rd, inst.rs, inst.rt);

        case SPECIAL_OR:
            return fmt::format("ctx->r[{}] = OR32(ctx->r[{}], ctx->r[{}]);",
                               inst.rd, inst.rs, inst.rt);

        case SPECIAL_XOR:
            return fmt::format("ctx->r[{}] = XOR32(ctx->r[{}], ctx->r[{}]);",
                               inst.rd, inst.rs, inst.rt);

        case SPECIAL_NOR:
            return fmt::format("ctx->r[{}] = NOR32(ctx->r[{}], ctx->r[{}]);",
                               inst.rd, inst.rs, inst.rt);

        case SPECIAL_SLT:
            return fmt::format("ctx->r[{}] = SLT32(ctx->r[{}], ctx->r[{}]);",
                               inst.rd, inst.rs, inst.rt);

        case SPECIAL_SLTU:
            return fmt::format("ctx->r[{}] = SLTU32(ctx->r[{}], ctx->r[{}]);",
                               inst.rd, inst.rs, inst.rt);

        // PS2-specific instructions
        case SPECIAL_MOVZ:
            return fmt::format("if (ctx->r[{}] == 0) ctx->r[{}] = ctx->r[{}];",
                               inst.rt, inst.rd, inst.rs);

        case SPECIAL_MOVN:
            return fmt::format("if (ctx->r[{}] != 0) ctx->r[{}] = ctx->r[{}];",
                               inst.rt, inst.rd, inst.rs);

        case SPECIAL_MFSA:
            return fmt::format("ctx->r[{}] = ctx->sa;", inst.rd);

        case SPECIAL_MTSA:
            return fmt::format("ctx->sa = ctx->r[{}] & 0x0F;", inst.rs);

        // Doubleword operations
        case SPECIAL_DADD:
            return translateDADD(inst);

        case SPECIAL_DADDU:
            return translateDADDU(inst);

        case SPECIAL_DSUB:
            return translateDSUB(inst);

        case SPECIAL_DSUBU:
            return translateDSUBU(inst);

        case SPECIAL_DSLL:
            return translateDSLL(inst);

        case SPECIAL_DSRL:
            return translateDSRL(inst);

        case SPECIAL_DSRA:
            return translateDSRA(inst);

        case SPECIAL_DSLL32:
            return translateDSLL32(inst);

        case SPECIAL_DSRL32:
            return translateDSRL32(inst);

        case SPECIAL_DSRA32:
            return translateDSRA32(inst);

        // Trap instructions
        case SPECIAL_TGE:
            return fmt::format("if ((int32_t)ctx->r[{}] >= (int32_t)ctx->r[{}]) {{ /* Trap */ }}",
                               inst.rs, inst.rt);

        case SPECIAL_TGEU:
            return fmt::format("if (ctx->r[{}] >= ctx->r[{}]) {{ /* Trap */ }}",
                               inst.rs, inst.rt);

        case SPECIAL_TLT:
            return fmt::format("if ((int32_t)ctx->r[{}] < (int32_t)ctx->r[{}]) {{ /* Trap */ }}",
                               inst.rs, inst.rt);

        case SPECIAL_TLTU:
            return fmt::format("if (ctx->r[{}] < ctx->r[{}]) {{ /* Trap */ }}",
                               inst.rs, inst.rt);

        case SPECIAL_TEQ:
            return fmt::format("if (ctx->r[{}] == ctx->r[{}]) {{ /* Trap */ }}",
                               inst.rs, inst.rt);

        case SPECIAL_TNE:
            return fmt::format("if (ctx->r[{}] != ctx->r[{}]) {{ /* Trap */ }}",
                               inst.rs, inst.rt);

        default:
            return fmt::format("// Unhandled SPECIAL instruction: 0x{:X}", inst.function);
        }
    }

    std::string CodeGenerator::translateCOP0Instruction(const Instruction &inst)
    {
        uint32_t format = inst.rs; // Format field
        uint32_t rt = inst.rt;     // GPR register
        uint32_t rd = inst.rd;     // COP0 register

        switch (format)
        {
        case COP0_MF: // MFC0 - Move From COP0
            switch (rd)
            {
            case COP0_REG_INDEX:
                return fmt::format("ctx->r[{}] = ctx->cop0_index;", rt);

            case COP0_REG_RANDOM:
                return fmt::format("ctx->r[{}] = ctx->cop0_random;", rt);

            case COP0_REG_ENTRYLO0:
                return fmt::format("ctx->r[{}] = ctx->cop0_entrylo0;", rt);

            case COP0_REG_ENTRYLO1:
                return fmt::format("ctx->r[{}] = ctx->cop0_entrylo1;", rt);

            case COP0_REG_CONTEXT:
                return fmt::format("ctx->r[{}] = ctx->cop0_context;", rt);

            case COP0_REG_PAGEMASK:
                return fmt::format("ctx->r[{}] = ctx->cop0_pagemask;", rt);

            case COP0_REG_WIRED:
                return fmt::format("ctx->r[{}] = ctx->cop0_wired;", rt);

            case COP0_REG_BADVADDR:
                return fmt::format("ctx->r[{}] = ctx->cop0_badvaddr;", rt);

            case COP0_REG_COUNT:
                return fmt::format("ctx->r[{}] = ctx->cop0_count;", rt);

            case COP0_REG_ENTRYHI:
                return fmt::format("ctx->r[{}] = ctx->cop0_entryhi;", rt);

            case COP0_REG_COMPARE:
                return fmt::format("ctx->r[{}] = ctx->cop0_compare;", rt);

            case COP0_REG_STATUS:
                return fmt::format("ctx->r[{}] = ctx->cop0_status;", rt);

            case COP0_REG_CAUSE:
                return fmt::format("ctx->r[{}] = ctx->cop0_cause;", rt);

            case COP0_REG_EPC:
                return fmt::format("ctx->r[{}] = ctx->cop0_epc;", rt);

            case COP0_REG_PRID:
                return fmt::format("ctx->r[{}] = ctx->cop0_prid;", rt);

            case COP0_REG_CONFIG:
                return fmt::format("ctx->r[{}] = ctx->cop0_config;", rt);

            case COP0_REG_BADPADDR:
                return fmt::format("ctx->r[{}] = ctx->cop0_badpaddr;", rt);

            case COP0_REG_DEBUG:
                return fmt::format("ctx->r[{}] = ctx->cop0_debug;", rt);

            case COP0_REG_PERF:
                return fmt::format("ctx->r[{}] = ctx->cop0_perf;", rt);

            case COP0_REG_TAGLO:
                return fmt::format("ctx->r[{}] = ctx->cop0_taglo;", rt);

            case COP0_REG_TAGHI:
                return fmt::format("ctx->r[{}] = ctx->cop0_taghi;", rt);

            case COP0_REG_ERROREPC:
                return fmt::format("ctx->r[{}] = ctx->cop0_errorepc;", rt);

            default:
                return fmt::format("ctx->r[{}] = 0; // Unimplemented COP0 register {}", rt, rd);
            }

        case COP0_MT: // MTC0 - Move To COP0
            switch (rd)
            {
            case COP0_REG_INDEX:
                return fmt::format("ctx->cop0_index = ctx->r[{}] & 0x3F;", rt); // 6-bit field

            case COP0_REG_RANDOM:
                return fmt::format("// MTC0 to RANDOM register ignored (read-only)");

            case COP0_REG_ENTRYLO0:
                return fmt::format("ctx->cop0_entrylo0 = ctx->r[{}] & 0x3FFFFFFF;", rt);

            case COP0_REG_ENTRYLO1:
                return fmt::format("ctx->cop0_entrylo1 = ctx->r[{}] & 0x3FFFFFFF;", rt);

            case COP0_REG_CONTEXT:
                return fmt::format("ctx->cop0_context = (ctx->cop0_context & 0x7) | (ctx->r[{}] & ~0x7);", rt);

            case COP0_REG_PAGEMASK:
                return fmt::format("ctx->cop0_pagemask = ctx->r[{}] & 0x1FFE000;", rt);

            case COP0_REG_WIRED:
                return fmt::format("ctx->cop0_wired = ctx->r[{}] & 0x3F; ctx->cop0_random = 47;", rt);

            case COP0_REG_BADVADDR:
                return fmt::format("// MTC0 to BADVADDR register ignored (read-only)");

            case COP0_REG_COUNT:
                return fmt::format("ctx->cop0_count = ctx->r[{}];", rt);

            case COP0_REG_ENTRYHI:
                return fmt::format("ctx->cop0_entryhi = ctx->r[{}] & 0xFFFFE0FF;", rt);

            case COP0_REG_COMPARE:
                return fmt::format("ctx->cop0_compare = ctx->r[{}]; ctx->cop0_cause &= ~0x8000; // Clear timer interrupt", rt);

            case COP0_REG_STATUS:
                return fmt::format("ctx->cop0_status = ctx->r[{}] & 0xFF57FFFF;", rt);

            case COP0_REG_CAUSE:
                return fmt::format("ctx->cop0_cause = (ctx->cop0_cause & ~0x300) | (ctx->r[{}] & 0x300);", rt);

            case COP0_REG_EPC:
                return fmt::format("ctx->cop0_epc = ctx->r[{}];", rt);

            case COP0_REG_PRID:
                return fmt::format("// MTC0 to PRID register ignored (read-only)");

            case COP0_REG_CONFIG:
                return fmt::format("ctx->cop0_config = (ctx->cop0_config & ~0x7) | (ctx->r[{}] & 0x7);", rt);

            case COP0_REG_BADPADDR:
                return fmt::format("// MTC0 to BADPADDR register ignored (read-only)");

            case COP0_REG_DEBUG:
                return fmt::format("ctx->cop0_debug = ctx->r[{}];", rt);

            case COP0_REG_PERF:
                return fmt::format("ctx->cop0_perf = ctx->r[{}];", rt);

            case COP0_REG_TAGLO:
                return fmt::format("ctx->cop0_taglo = ctx->r[{}];", rt);

            case COP0_REG_TAGHI:
                return fmt::format("ctx->cop0_taghi = ctx->r[{}];", rt);

            case COP0_REG_ERROREPC:
                return fmt::format("ctx->cop0_errorepc = ctx->r[{}];", rt);

            default:
                return fmt::format("// MTC0 to register {} ignored", rd);
            }

        case COP0_CO: // COP0 co-processor operations
        {
            uint32_t function = inst.function;

            switch (function)
            {
            case COP0_CO_TLBR:
                return fmt::format("// TLBR instruction - TLB Read\n"
                                   "    // Reads TLB entry specified by Index register\n"
                                   "    // Not fully implemented in recompiled code");

            case COP0_CO_TLBWI:
                return fmt::format("// TLBWI instruction - TLB Write Indexed\n"
                                   "    // Writes TLB entry specified by Index register\n"
                                   "    // Not fully implemented in recompiled code");

            case COP0_CO_TLBWR:
                return fmt::format("// TLBWR instruction - TLB Write Random\n"
                                   "    // Writes TLB entry specified by Random register\n"
                                   "    // Not fully implemented in recompiled code");

            case COP0_CO_TLBP:
                return fmt::format("// TLBP instruction - TLB Probe\n"
                                   "    // Searches TLB for matching entry to EntryHi, sets Index\n"
                                   "    // Not fully implemented in recompiled code");

            case COP0_CO_ERET:
                return fmt::format("// ERET instruction - Return from Exception\n"
                                   "    if (ctx->cop0_status & 0x4) {{\n"
                                   "        ctx->pc = ctx->cop0_errorepc;\n"
                                   "        ctx->cop0_status &= ~0x4; // Clear ERL\n"
                                   "    }} else {{\n"
                                   "        ctx->pc = ctx->cop0_epc;\n"
                                   "        ctx->cop0_status &= ~0x2; // Clear EXL\n"
                                   "    }}\n"
                                   "    return;");

            case COP0_CO_EI:
                return translateEI(inst);

            case COP0_CO_DI:
                return translateDI(inst);

            default:
                return fmt::format("// Unhandled COP0 CO-OP: 0x{:X}", function);
            }
            break;
        }

        default:
            return fmt::format("// Unhandled COP0 instruction format: 0x{:X}", format);
        }
    }

    std::string CodeGenerator::translateMMIInstruction(const Instruction &inst)
    {
        uint32_t function = inst.function;

        switch (function)
        {
        case MMI_MADD:
            return fmt::format("{{ int64_t result = (int64_t)(((int64_t)ctx->hi << 32) | ctx->lo) + (int64_t)(int32_t)ctx->r[{}] * (int64_t)(int32_t)ctx->r[{}]; ctx->lo = (uint32_t)result; ctx->hi = (uint32_t)(result >> 32); }}",
                               inst.rs, inst.rt);

        case MMI_MADDU:
            return fmt::format("{{ uint64_t result = (uint64_t)(((uint64_t)ctx->hi << 32) | ctx->lo) + (uint64_t)ctx->r[{}] * (uint64_t)ctx->r[{}]; ctx->lo = (uint32_t)result; ctx->hi = (uint32_t)(result >> 32); }}",
                               inst.rs, inst.rt);

        case MMI_PLZCW:
            return fmt::format("ctx->r[{}] = __builtin_clz(ctx->r[{}]);",
                               inst.rd, inst.rs);

        case MMI_MFHI1:
            return fmt::format("ctx->r[{}] = ctx->hi;", inst.rd);

        case MMI_MTHI1:
            return fmt::format("ctx->hi = ctx->r[{}];", inst.rs);

        case MMI_MFLO1:
            return fmt::format("ctx->r[{}] = ctx->lo;", inst.rd);

        case MMI_MTLO1:
            return fmt::format("ctx->lo = ctx->r[{}];", inst.rs);

        case MMI_MULT1:
            return fmt::format("{{ int64_t result = (int64_t)(int32_t)ctx->r[{}] * (int64_t)(int32_t)ctx->r[{}]; ctx->lo = (uint32_t)result; ctx->hi = (uint32_t)(result >> 32); }}",
                               inst.rs, inst.rt);

        case MMI_MULTU1:
            return fmt::format("{{ uint64_t result = (uint64_t)ctx->r[{}] * (uint64_t)ctx->r[{}]; ctx->lo = (uint32_t)result; ctx->hi = (uint32_t)(result >> 32); }}",
                               inst.rs, inst.rt);

        case MMI_DIV1:
            return fmt::format("{{ if (ctx->r[{}] != 0) {{ ctx->lo = (uint32_t)((int32_t)ctx->r[{}] / (int32_t)ctx->r[{}]); ctx->hi = (uint32_t)((int32_t)ctx->r[{}] % (int32_t)ctx->r[{}]); }} }}",
                               inst.rt, inst.rs, inst.rt, inst.rs, inst.rt);

        case MMI_DIVU1:
            return fmt::format("{{ if (ctx->r[{}] != 0) {{ ctx->lo = ctx->r[{}] / ctx->r[{}]; ctx->hi = ctx->r[{}] % ctx->r[{}]; }} }}",
                               inst.rt, inst.rs, inst.rt, inst.rs, inst.rt);

        case MMI_MADD1:
            return fmt::format("{{ int64_t result = (int64_t)(((int64_t)ctx->hi << 32) | ctx->lo) + (int64_t)(int32_t)ctx->r[{}] * (int64_t)(int32_t)ctx->r[{}]; ctx->lo = (uint32_t)result; ctx->hi = (uint32_t)(result >> 32); }}",
                               inst.rs, inst.rt);

        case MMI_MADDU1:
            return fmt::format("{{ uint64_t result = (uint64_t)(((uint64_t)ctx->hi << 32) | ctx->lo) + (uint64_t)ctx->r[{}] * (uint64_t)ctx->r[{}]; ctx->lo = (uint32_t)result; ctx->hi = (uint32_t)(result >> 32); }}",
                               inst.rs, inst.rt);

        // MMI0 functions (PADDW, PSUBW, etc.)
        case MMI_MMI0:
            switch (inst.sa)
            {
            case MMI0_PADDW:
                return fmt::format("ctx->r[{}] = PS2_PADDW(ctx->r[{}], ctx->r[{}]);",
                                   inst.rd, inst.rs, inst.rt);

            case MMI0_PSUBW:
                return fmt::format("ctx->r[{}] = PS2_PSUBW(ctx->r[{}], ctx->r[{}]);",
                                   inst.rd, inst.rs, inst.rt);

            case MMI0_PCGTW:
                return fmt::format("ctx->r[{}] = PS2_PCGTW(ctx->r[{}], ctx->r[{}]);",
                                   inst.rd, inst.rs, inst.rt);

            case MMI0_PMAXW:
                return fmt::format("ctx->r[{}] = PS2_PMAXW(ctx->r[{}], ctx->r[{}]);",
                                   inst.rd, inst.rs, inst.rt);

            case MMI0_PADDH:
                return fmt::format("ctx->r[{}] = PS2_PADDH(ctx->r[{}], ctx->r[{}]);",
                                   inst.rd, inst.rs, inst.rt);

            case MMI0_PSUBH:
                return fmt::format("ctx->r[{}] = PS2_PSUBH(ctx->r[{}], ctx->r[{}]);",
                                   inst.rd, inst.rs, inst.rt);

            case MMI0_PCGTH:
                return fmt::format("ctx->r[{}] = PS2_PCGTH(ctx->r[{}], ctx->r[{}]);",
                                   inst.rd, inst.rs, inst.rt);

            case MMI0_PMAXH:
                return fmt::format("ctx->r[{}] = PS2_PMAXH(ctx->r[{}], ctx->r[{}]);",
                                   inst.rd, inst.rs, inst.rt);

            case MMI0_PADDB:
                return fmt::format("ctx->r[{}] = PS2_PADDB(ctx->r[{}], ctx->r[{}]);",
                                   inst.rd, inst.rs, inst.rt);

            case MMI0_PSUBB:
                return fmt::format("ctx->r[{}] = PS2_PSUBB(ctx->r[{}], ctx->r[{}]);",
                                   inst.rd, inst.rs, inst.rt);

            case MMI0_PCGTB:
                return fmt::format("ctx->r[{}] = PS2_PCGTB(ctx->r[{}], ctx->r[{}]);",
                                   inst.rd, inst.rs, inst.rt);

            case MMI0_PEXTLW:
                return fmt::format("ctx->r[{}] = PS2_PEXTLW(ctx->r[{}], ctx->r[{}]);",
                                   inst.rd, inst.rs, inst.rt);

            case MMI0_PPACW:
                return fmt::format("ctx->r[{}] = PS2_PPACW(ctx->r[{}], ctx->r[{}]);",
                                   inst.rd, inst.rs, inst.rt);

            case MMI0_PEXTLH:
                return fmt::format("ctx->r[{}] = PS2_PEXTLH(ctx->r[{}], ctx->r[{}]);",
                                   inst.rd, inst.rs, inst.rt);

            case MMI0_PPACH:
                return fmt::format("ctx->r[{}] = PS2_PPACH(ctx->r[{}], ctx->r[{}]);",
                                   inst.rd, inst.rs, inst.rt);

            case MMI0_PEXTLB:
                return fmt::format("ctx->r[{}] = PS2_PEXTLB(ctx->r[{}], ctx->r[{}]);",
                                   inst.rd, inst.rs, inst.rt);

            case MMI0_PPACB:
                return fmt::format("// PS2_PPACB not implemented");

            default:
                return fmt::format("// Unhandled MMI0 function: 0x{:X}", inst.sa);
            }
            break;

        // MMI1 functions (PABSW, PCEQW, etc.)
        case MMI_MMI1:
            switch (inst.sa)
            {
            case MMI1_PADDUW:
                return fmt::format("ctx->r[{}] = PS2_PADDW(ctx->r[{}], ctx->r[{}]);",
                                   inst.rd, inst.rs, inst.rt);

            case MMI1_PSUBUW:
                return fmt::format("ctx->r[{}] = PS2_PSUBW(ctx->r[{}], ctx->r[{}]);",
                                   inst.rd, inst.rs, inst.rt);

            case MMI1_PEXTUW:
                return fmt::format("ctx->r[{}] = PS2_PEXTUW(ctx->r[{}], ctx->r[{}]);",
                                   inst.rd, inst.rs, inst.rt);

            case MMI1_PADDUH:
                return fmt::format("ctx->r[{}] = PS2_PADDH(ctx->r[{}], ctx->r[{}]);",
                                   inst.rd, inst.rs, inst.rt);

            case MMI1_PSUBUH:
                return fmt::format("ctx->r[{}] = PS2_PSUBH(ctx->r[{}], ctx->r[{}]);",
                                   inst.rd, inst.rs, inst.rt);

            case MMI1_PEXTUH:
                return fmt::format("ctx->r[{}] = PS2_PEXTUH(ctx->r[{}], ctx->r[{}]);",
                                   inst.rd, inst.rs, inst.rt);
            case MMI1_PABSW:
                return fmt::format("ctx->r[{}] = PS2_PABSW(ctx->r[{}]);",
                                   inst.rd, inst.rs);

            case MMI1_PABSH:
                return fmt::format("ctx->r[{}] = PS2_PABSH(ctx->r[{}]);",
                                   inst.rd, inst.rs);

            case MMI1_PCEQW:
                return fmt::format("ctx->r[{}] = PS2_PCEQW(ctx->r[{}], ctx->r[{}]);",
                                   inst.rd, inst.rs, inst.rt);

            case MMI1_PCEQH:
                return fmt::format("ctx->r[{}] = PS2_PCEQH(ctx->r[{}], ctx->r[{}]);",
                                   inst.rd, inst.rs, inst.rt);

            case MMI1_PCEQB:
                return fmt::format("ctx->r[{}] = PS2_PCEQB(ctx->r[{}], ctx->r[{}]);",
                                   inst.rd, inst.rs, inst.rt);

            case MMI1_PMINW:
                return fmt::format("ctx->r[{}] = PS2_PMINW(ctx->r[{}], ctx->r[{}]);",
                                   inst.rd, inst.rs, inst.rt);

            case MMI1_PMINH:
                return fmt::format("ctx->r[{}] = PS2_PMINH(ctx->r[{}], ctx->r[{}]);",
                                   inst.rd, inst.rs, inst.rt);

            case MMI1_PADDUB:
                return fmt::format("ctx->r[{}] = PS2_PADDB(ctx->r[{}], ctx->r[{}]);",
                                   inst.rd, inst.rs, inst.rt);

            case MMI1_PSUBUB:
                return fmt::format("ctx->r[{}] = PS2_PSUBB(ctx->r[{}], ctx->r[{}]);",
                                   inst.rd, inst.rs, inst.rt);

            case MMI1_PEXTUB:
                return fmt::format("ctx->r[{}] = PS2_PEXTUB(ctx->r[{}], ctx->r[{}]);",
                                   inst.rd, inst.rs, inst.rt);

            case MMI1_QFSRV:
                return translateQFSRV(inst);

            default:
                return fmt::format("// Unhandled MMI1 function: 0x{:X}", inst.sa);
            }
            break;

        // MMI2 functions (PMADDW, PSLLVW, etc.)
        case MMI_MMI2:
            switch (inst.sa)
            {
            case MMI2_PMADDW:
                return translatePMADDW(inst);

            case MMI2_PSLLVW:
                return fmt::format("ctx->r[{}] = PS2_PSLLVW(ctx->r[{}], ctx->r[{}]);",
                                   inst.rd, inst.rs, inst.rt);

            case MMI2_PSRLVW:
                return fmt::format("ctx->r[{}] = PS2_PSRLVW(ctx->r[{}], ctx->r[{}]);",
                                   inst.rd, inst.rs, inst.rt);

            case MMI2_PINTH:
                return fmt::format("ctx->r[{}] = PS2_PINTH(ctx->r[{}], ctx->r[{}]);",
                                   inst.rd, inst.rs, inst.rt);

            case MMI2_PAND:
                return fmt::format("ctx->r[{}] = PS2_PAND(ctx->r[{}], ctx->r[{}]);",
                                   inst.rd, inst.rs, inst.rt);

            case MMI2_PXOR:
                return fmt::format("ctx->r[{}] = PS2_PXOR(ctx->r[{}], ctx->r[{}]);",
                                   inst.rd, inst.rs, inst.rt);

            case MMI2_PMULTW:
                return fmt::format("// PS2_PMULTW - Packed Multiply Word");

            case MMI2_PDIVW:
                return translatePDIVW(inst);

            case MMI2_PCPYLD:
                return translatePCPYLD(inst);

            case MMI2_PMADDH:
                return translatePMADDH(inst);

            case MMI2_PHMADH:
                return translatePHMADH(inst);

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
                return fmt::format("// Unhandled MMI2 function: 0x{:X}", inst.sa);
            }
            break;

        // MMI3 functions (PMADDUW, PSRAVW, etc.)
        case MMI_MMI3:
            switch (inst.sa)
            {
            case MMI3_POR:
                return fmt::format("ctx->r[{}] = PS2_POR(ctx->r[{}], ctx->r[{}]);",
                                   inst.rd, inst.rs, inst.rt);

            case MMI3_PNOR:
                return fmt::format("ctx->r[{}] = PS2_PNOR(ctx->r[{}], ctx->r[{}]);",
                                   inst.rd, inst.rs, inst.rt);

            case MMI3_PMADDUW:
                return fmt::format("// PS2_PMADDUW - Packed Multiply-Add Unsigned Word");

            case MMI3_PSRAVW:
                return fmt::format("ctx->r[{}] = PS2_PSRAVW(ctx->r[{}], ctx->r[{}]);",
                                   inst.rd, inst.rs, inst.rt);

            case MMI3_PINTEH:
                return fmt::format("ctx->r[{}] = PS2_PINTEH(ctx->r[{}], ctx->r[{}]);",
                                   inst.rd, inst.rs, inst.rt);

            case MMI3_PMULTUW:
                return translatePMULTUW(inst);

            case MMI3_PDIVUW:
                return translatePDIVUW(inst);

            case MMI3_PCPYUD:
                return translatePCPYUD(inst);

            case MMI3_PEXCH:
                return translatePEXCH(inst);

            case MMI3_PCPYH:
                return translatePCPYH(inst);

            case MMI3_PEXCW:
                return translatePEXCW(inst);

            default:
                return fmt::format("// Unhandled MMI3 function: 0x{:X}", inst.sa);
            }
            break;

        // MMI_PMFHL functions
        case MMI_PMFHL:
            switch (inst.pmfhlVariation)
            {
            case PMFHL_LW:
                return fmt::format("ctx->r[{}] = PS2_PMFHL_LW(ctx->hi, ctx->lo);",
                                   inst.rd);

            case PMFHL_UW:
                return fmt::format("ctx->r[{}] = PS2_PMFHL_UW(ctx->hi, ctx->lo);",
                                   inst.rd);

            case PMFHL_SLW:
                return fmt::format("ctx->r[{}] = PS2_PMFHL_SLW(ctx->hi, ctx->lo);",
                                   inst.rd);

            case PMFHL_LH:
                return fmt::format("ctx->r[{}] = PS2_PMFHL_LH(ctx->hi, ctx->lo);",
                                   inst.rd);

            case PMFHL_SH:
                return fmt::format("ctx->r[{}] = PS2_PMFHL_SH(ctx->hi, ctx->lo);",
                                   inst.rd);

            default:
                return fmt::format("// Unknown PMFHL variation: 0x{:X}", inst.pmfhlVariation);
            }
            break;

        default:
            return fmt::format("// Unhandled MMI instruction: 0x{:X}", function);
        }
    }

    std::string CodeGenerator::translateVUInstruction(const Instruction &inst)
    {
        uint32_t rs = inst.rs;

        switch (rs)
        {
        case COP2_QMFC2:
            return fmt::format("ctx->r[{}] = (__m128i)ctx->vu0_vf[{}];",
                               inst.rt, inst.rd);

        case COP2_CFC2:
            switch (inst.rd)
            {
            case VU0_CR_STATUS:
                return fmt::format("ctx->r[{}] = ctx->vu0_status;", inst.rt);

            case VU0_CR_MAC:
                return fmt::format("ctx->r[{}] = ctx->vu0_mac_flags;", inst.rt);

            case VU0_CR_CLIP:
                return fmt::format("ctx->r[{}] = ctx->vu0_clip_flags;", inst.rt);

            case VU0_CR_CMSAR0:
                return fmt::format("ctx->r[{}] = ctx->vu0_cmsar0;", inst.rt);

            case VU0_CR_FBRST:
                return fmt::format("ctx->r[{}] = ctx->vu0_fbrst;", inst.rt);

            default:
                return fmt::format("// Unhandled CFC2 VU control register: {}", inst.rd);
            }

        case COP2_QMTC2:
            return fmt::format("ctx->vu0_vf[{}] = (__m128)ctx->r[{}];",
                               inst.rd, inst.rt);

        case COP2_CTC2:
            if (inst.rd == 0)
            {
                return fmt::format("ctx->vu0_status = ctx->r[{}] & 0xFFFF;", inst.rt);
            }
            else
            {
                return fmt::format("// Unhandled CTC2 VU control register: {}", inst.rd);
            }

        case COP2_BC2:
            return fmt::format("// VU branch instruction not implemented");

        case COP2_BC2F:
            switch (inst.rt)
            {
            case COP2_BCF: // BC2F - Branch on VU0 false
                return fmt::format("if ((ctx->vu0_status & 0x1) == 0) {{ /* branch logic handled elsewhere */ }}");

            case COP2_BCT: // BC2T - Branch on VU0 true
                return fmt::format("if ((ctx->vu0_status & 0x1) != 0) {{ /* branch logic handled elsewhere */ }}");

            case COP2_BCFL: // BC2FL - Branch on VU0 false likely
                return fmt::format("if ((ctx->vu0_status & 0x1) == 0) {{ /* branch logic handled elsewhere */ }}");

            case COP2_BCTL: // BC2TL - Branch on VU0 true likely
                return fmt::format("if ((ctx->vu0_status & 0x1) != 0) {{ /* branch logic handled elsewhere */ }}");

            case COP2_BCEF: // BC2EF - Branch on VU0 equal flag false (0x5)
                return fmt::format("if ((ctx->vu0_status & 0x2) == 0) {{ /* branch logic handled elsewhere */ }}");

            case COP2_BCET: // BC2ET - Branch on VU0 equal flag true (0x6)
                return fmt::format("if ((ctx->vu0_status & 0x2) != 0) {{ /* branch logic handled elsewhere */ }}");

            case COP2_BCEFL: // BC2EFL - Branch on VU0 equal flag false likely (0x6)
                return fmt::format("if ((ctx->vu0_status & 0x2) == 0) {{ /* branch logic handled elsewhere */ }}");

            case COP2_BCETL: // BC2ETL - Branch on VU0 equal flag true likely (0x7)
                return fmt::format("if ((ctx->vu0_status & 0x2) != 0) {{ /* branch logic handled elsewhere */ }}");

            default:
                return fmt::format("// Unhandled BC2 instruction: rt=0x{:X}", inst.rt);
            }

        case COP2_MTVUCF:
            return fmt::format("ctx->vu0_cf[{}] = ctx->r[{}];", inst.rd, inst.rt);

        case COP2_VMTIR:
            return fmt::format("ctx->vu0_i = (float)ctx->r[{}].m128_i32[0];", inst.rt);

        case COP2_VCLIP: // VU0 Clipping operation (0x1E)
            return fmt::format("{{ float x = ctx->vu0_vf[{}].m128_f32[0]; "
                               "float y = ctx->vu0_vf[{}].m128_f32[1]; "
                               "float z = ctx->vu0_vf[{}].m128_f32[2]; "
                               "float w = ctx->vu0_vf[{}].m128_f32[3]; "
                               "// Calculate VU0 clipping flags and store in ctx->vu0_clip_flags \n"
                               "ctx->vu0_clip_flags = 0; "
                               "if (w < -x) ctx->vu0_clip_flags |= 0x01; "
                               "if (w < x) ctx->vu0_clip_flags |= 0x02; "
                               "if (w < -y) ctx->vu0_clip_flags |= 0x04; "
                               "if (w < y) ctx->vu0_clip_flags |= 0x08; "
                               "if (w < -z) ctx->vu0_clip_flags |= 0x10; "
                               "if (w < z) ctx->vu0_clip_flags |= 0x20; }}",
                               inst.rt, inst.rt, inst.rt, inst.rt);

        case COP2_VLDQ:
            if (inst.function & 0x10)
            {
                return fmt::format("{{ uint32_t addr = (ctx->r[{}].m128_i32[0] - 16) & 0x3FF; "
                                   "WRITE128(addr, (__m128i)ctx->vu0_vf[{}]); "
                                   "ctx->r[{}].m128_i32[0] = addr; }}",
                                   inst.rs, inst.rt, inst.rs);
            }
            else
            {
                return fmt::format("{{ uint32_t addr = (ctx->r[{}].m128_i32[0] - 16) & 0x3FF; "
                                   "ctx->vu0_vf[{}] = (__m128)READ128(addr); "
                                   "ctx->r[{}].m128_i32[0] = addr; }}",
                                   inst.rs, inst.rt, inst.rs);
            }

        case COP2_CO:
            switch (inst.function)
            {
            case VU0_VADD:
                return fmt::format("ctx->vu0_vf[{}] = PS2_VADD(ctx->vu0_vf[{}], ctx->vu0_vf[{}]);",
                                   inst.rd, inst.rs, inst.rt);

            case VU0_VSUB:
                return fmt::format("ctx->vu0_vf[{}] = PS2_VSUB(ctx->vu0_vf[{}], ctx->vu0_vf[{}]);",
                                   inst.rd, inst.rs, inst.rt);

            case VU0_VMUL:
                return fmt::format("ctx->vu0_vf[{}] = PS2_VMUL(ctx->vu0_vf[{}], ctx->vu0_vf[{}]);",
                                   inst.rd, inst.rs, inst.rt);

            case VU0_VDIV:
                return fmt::format("{{ float q = 0.0f; "
                                   "if (ctx->vu0_vf[{}].m128_f32[{}] != 0.0f) {{ "
                                   "q = ctx->vu0_vf[{}].m128_f32[{}] / ctx->vu0_vf[{}].m128_f32[{}]; }} "
                                   "ctx->vu0_q = q; }}",
                                   inst.rt, inst.rd, inst.rs, inst.rd, inst.rt, inst.rd);

            case VU0_VSQRT:
                return fmt::format("{{ float fsrc = ctx->vu0_vf[{}].m128_f32[{}]; "
                                   "ctx->vu0_q = sqrtf(fsrc); }}",
                                   inst.rt, inst.rd);

            case VU0_VRSQRT:
                return fmt::format("{{ float fsrc = ctx->vu0_vf[{}].m128_f32[{}]; "
                                   "if (fsrc != 0.0f) {{ ctx->vu0_q = 1.0f / sqrtf(fsrc); }} "
                                   "else {{ ctx->vu0_q = INFINITY; }} }}",
                                   inst.rt, inst.rd);

            case VU0_VIADDI:
                return fmt::format("ctx->vu0_i += {};",
                                   (int16_t)inst.immediate);

            case VU0_VIAND:
                return fmt::format("ctx->vu0_i &= _mm_cvtss_f32(_mm_castsi128_ps(_mm_cvtsi32_si128(ctx->vu0_vf[{}].m128_i32[{}])));",
                                   inst.rt, inst.rd);

            case VU0_VIOR:
                return fmt::format("ctx->vu0_i |= _mm_cvtss_f32(_mm_castsi128_ps(_mm_cvtsi32_si128(ctx->vu0_vf[{}].m128_i32[{}])));",
                                   inst.rt, inst.rd);

            case VU0_VILWR:
                return fmt::format("{{ uint32_t addr = (_mm_extract_epi32(ctx->vu0_vf[{}], {}) + ctx->vu0_i) & 0xFFF; "
                                   "ctx->vu0_vf[{}].m128_i32[{}] = *(int32_t*)(rdram + addr); }}",
                                   inst.rt, inst.rd, inst.rt, inst.rd);

            case VU0_VISWR:
                return fmt::format("{{ uint32_t addr = (_mm_extract_epi32(ctx->vu0_vf[{}], {}) + ctx->vu0_i) & 0xFFF; "
                                   "*(int32_t*)(rdram + addr) = ctx->vu0_vf[{}].m128_i32[{}]; }}",
                                   inst.rt, inst.rd, inst.rt, inst.rd);

            case VU0_VCALLMS:
                return fmt::format("// Calls VU0 microprogram at address {} - not implemented in recompiled code",
                                   inst.immediate);

            case VU0_VRGET: // TODO - Check if this is correct
                return fmt::format("ctx->vu0_vf[{}] = ctx->vu0_r; // Get VU0 R register",
                                   inst.rd);

            case VU0_VMULQ:
                return fmt::format("ctx->vu0_vf[{}] = PS2_VMULQ(ctx->vu0_vf[{}], ctx->vu0_q);",
                                   inst.rd, inst.rs);

            case VU0_VIADD:
                return fmt::format("ctx->vu0_i = _mm_extract_ps(ctx->vu0_vf[{}], {}) + "
                                   "_mm_extract_ps(ctx->vu0_vf[{}], {});",
                                   inst.rs, inst.rd, inst.rt, inst.rd);

            case VU0_VISUB:
                return fmt::format("ctx->vu0_i = _mm_extract_ps(ctx->vu0_vf[{}], {}) - "
                                   "_mm_extract_ps(ctx->vu0_vf[{}], {});",
                                   inst.rs, inst.rd, inst.rt, inst.rd);

            default:
                return fmt::format("// Unhandled VU0 macro instruction: 0x{:X}", inst.function);
            }
            break;

        case COP2_CTCVU:
            switch (inst.rd)
            {
            case VU0_CR_STATUS:
                return fmt::format("ctx->vu0_control = ctx->r[{}] & 0xFFFF; // Set VU0 status/control register", inst.rt);

            case VU0_CR_MAC:
                return fmt::format("ctx->vu0_mac_flags = ctx->r[{}]; // Set VU0 MAC flags register", inst.rt);

            case VU0_CR_CLIP:
                return fmt::format("ctx->vu0_clip_flags = ctx->r[{}]; // Set VU0 clipping flags register", inst.rt);

            case VU0_CR_CMSAR0:
                return fmt::format("ctx->vu0_cmsar0 = ctx->r[{}]; // Set VU0 microprogram start address",
                                   inst.rt);

            case VU0_CR_FBRST:
                return fmt::format("// VU0 FBRST register - handles VU/VIF resets\n"
                                   "    // Bit 0: Reset VIF0, Bit 1: Reset VIF1\n"
                                   "    // Bit 8: Reset VU0, Bit 9: Reset VU1\n"
                                   "    ctx->vu0_fbrst = ctx->r[{}] & 0x0303;",
                                   inst.rt);

            default:
                return fmt::format("// Unhandled CTCVU VU control register: {}", inst.rd);
            }

        case COP2_VU0OPS:
            switch (inst.function)
            {
            case VU0OPS_QMFC2_NI: // 0x00 - Non-incrementing QMFC2
                return fmt::format("ctx->r[{}] = (__m128i)ctx->vu0_vf[{}]; // Non-incrementing QMFC2",
                                   inst.rt, inst.rd);

            case VU0OPS_QMFC2_I: // 0x01 - Incrementing QMFC2
                return fmt::format("{{ ctx->r[{}] = (__m128i)ctx->vu0_vf[{}]; "
                                   "ctx->vu0_vf[{}] = (__m128)_mm_setzero_si128(); }} // Incrementing QMFC2",
                                   inst.rt, inst.rd, inst.rd);

            case VU0OPS_QMTC2_NI: // 0x02 - Non-incrementing QMTC2
                return fmt::format("ctx->vu0_vf[{}] = (__m128)ctx->r[{}]; // Non-incrementing QMTC2",
                                   inst.rd, inst.rt);

            case VU0OPS_QMTC2_I: // 0x03 - Incrementing QMTC2
                return fmt::format("{{ ctx->vu0_vf[{}] = (__m128)ctx->r[{}]; "
                                   "ctx->r[{}] = _mm_setzero_si128(); }} // Incrementing QMTC2",
                                   inst.rd, inst.rt, inst.rt);

            case VU0OPS_VMFIR: // 0x04 - Move From Integer Register
                return fmt::format("{{ int val = ctx->r[{}].m128i_i32[0]; "
                                   "ctx->vu0_vf[{}] = (__m128)_mm_set1_epi32(val); }}",
                                   inst.rt, inst.rd);

            case VU0OPS_VXITOP: // 0x08 - Execute Interrupt on VU0
                return fmt::format("// VXITOP - VU0 Interrupt operation not implemented");

            case VU0OPS_VWAITQ: // 0x3C - Wait for Q register operations to complete TODO check this better
                return fmt::format("// VWAITQ - Wait for Q register operations to complete\n"
                                   "    // need proper implementation for this\n");

            default:
                return fmt::format("// Unhandled VU0OPS function: 0x{:X}", inst.function);
            }

        default:
            return fmt::format("// Unhandled VU instruction format: 0x{:X}", rs);
        }
    }

    std::string CodeGenerator::translateFPUInstruction(const Instruction &inst)
    {
        uint32_t rs = inst.rs; // Format field
        uint32_t ft = inst.rt; // FPU source register
        uint32_t fs = inst.rd; // FPU source register
        uint32_t fd = inst.sa; // FPU destination register
        uint32_t function = inst.function;

        // For MFC1/MTC1/CFC1/CTC1, the GPR is in rt and the FPR is in rd(fs)
        if (rs == COP1_MF)
        {
            return fmt::format("ctx->r[{}] = *(uint32_t*)&ctx->f[{}];",
                               ft, fs);
        }
        else if (rs == COP1_MT)
        {
            return fmt::format("*(uint32_t*)&ctx->f[{}] = ctx->r[{}];",
                               fs, ft);
        }
        else if (rs == COP1_CF)
        {
            // CFC1 - Move Control From FPU
            if (fs == 31) // FCR31 contains status/control
            {
                return fmt::format("ctx->r[{}] = ctx->fcr31;", ft);
            }
            else if (fs == 0) // FCR0 is the FPU implementation register
            {
                return fmt::format("ctx->r[{}] = 0x00000000; // Emulated FPU implementation", ft);
            }
            else
            {
                return fmt::format("ctx->r[{}] = 0; // Unimplemented FCR{}", ft, fs);
            }
        }
        else if (rs == COP1_CT)
        {
            // CTC1 - Move Control To FPU
            if (fs == 31) // FCR31 contains status/control
            {
                return fmt::format("ctx->fcr31 = ctx->r[{}] & 0x0183FFFF;", ft); // Apply bit mask for valid bits
            }
            else
            {
                return fmt::format("// CTC1 to FCR{} ignored", fs);
            }
        }
        else if (rs == COP1_BC)
        {
            // FPU Branch instructions - handled by delay slot code
            return fmt::format("// FPU branch instruction - handled elsewhere");
        }
        else if (rs == COP1_S)
        {
            // Single precision operations
            switch (function)
            {
            case 0x00: // ADD.S
                return fmt::format("ctx->f[{}] = FPU_ADD_S(ctx->f[{}], ctx->f[{}]);",
                                   fd, fs, ft);

            case 0x01: // SUB.S
                return fmt::format("ctx->f[{}] = FPU_SUB_S(ctx->f[{}], ctx->f[{}]);",
                                   fd, fs, ft);

            case 0x02: // MUL.S
                return fmt::format("ctx->f[{}] = FPU_MUL_S(ctx->f[{}], ctx->f[{}]);",
                                   fd, fs, ft);

            case 0x03: // DIV.S
                return fmt::format("ctx->f[{}] = FPU_DIV_S(ctx->f[{}], ctx->f[{}]);",
                                   fd, fs, ft);

            case 0x04: // SQRT.S
                return fmt::format("ctx->f[{}] = FPU_SQRT_S(ctx->f[{}]);",
                                   fd, fs);

            case 0x05: // ABS.S
                return fmt::format("ctx->f[{}] = FPU_ABS_S(ctx->f[{}]);",
                                   fd, fs);

            case 0x06: // MOV.S
                return fmt::format("ctx->f[{}] = FPU_MOV_S(ctx->f[{}]);",
                                   fd, fs);

            case 0x07: // NEG.S
                return fmt::format("ctx->f[{}] = FPU_NEG_S(ctx->f[{}]);",
                                   fd, fs);

            case 0x08: // ROUND.L.S
                return fmt::format("*(int64_t*)&ctx->f[{}] = FPU_ROUND_L_S(ctx->f[{}]);",
                                   fd, fs);

            case 0x09: // TRUNC.L.S
                return fmt::format("*(int64_t*)&ctx->f[{}] = FPU_TRUNC_L_S(ctx->f[{}]);",
                                   fd, fs);

            case 0x0A: // CEIL.L.S
                return fmt::format("*(int64_t*)&ctx->f[{}] = FPU_CEIL_L_S(ctx->f[{}]);",
                                   fd, fs);

            case 0x0B: // FLOOR.L.S
                return fmt::format("*(int64_t*)&ctx->f[{}] = FPU_FLOOR_L_S(ctx->f[{}]);",
                                   fd, fs);

            case 0x0C: // ROUND.W.S
                return fmt::format("*(int32_t*)&ctx->f[{}] = FPU_ROUND_W_S(ctx->f[{}]);",
                                   fd, fs);

            case 0x0D: // TRUNC.W.S
                return fmt::format("*(int32_t*)&ctx->f[{}] = FPU_TRUNC_W_S(ctx->f[{}]);",
                                   fd, fs);

            case 0x0E: // CEIL.W.S
                return fmt::format("*(int32_t*)&ctx->f[{}] = FPU_CEIL_W_S(ctx->f[{}]);",
                                   fd, fs);

            case 0x0F: // FLOOR.W.S
                return fmt::format("*(int32_t*)&ctx->f[{}] = FPU_FLOOR_W_S(ctx->f[{}]);",
                                   fd, fs);

            case 0x21: // CVT.D.S - Convert Single to Double (not commonly used on PS2)
                return fmt::format("// CVT.D.S not implemented (PS2 rarely uses double precision)");

            case 0x24: // CVT.W.S - Convert Single to Word
                return fmt::format("*(int32_t*)&ctx->f[{}] = FPU_CVT_W_S(ctx->f[{}]);",
                                   fd, fs);

            case 0x25: // CVT.L.S - Convert Single to Long
                return fmt::format("*(int64_t*)&ctx->f[{}] = FPU_CVT_L_S(ctx->f[{}]);",
                                   fd, fs);

            case 0x30: // C.F.S - Compare False
                return fmt::format("ctx->fcr31 = (ctx->fcr31 & ~0x800000); // Clear condition bit",
                                   fs, ft);

            case 0x31: // C.UN.S - Compare Unordered
                return fmt::format("ctx->fcr31 = (FPU_C_UN_S(ctx->f[{}], ctx->f[{}])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);",
                                   fs, ft);

            case 0x32: // C.EQ.S - Compare Equal
                return fmt::format("ctx->fcr31 = (FPU_C_EQ_S(ctx->f[{}], ctx->f[{}])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);",
                                   fs, ft);

            case 0x33: // C.UEQ.S - Compare Unordered or Equal
                return fmt::format("ctx->fcr31 = (FPU_C_UEQ_S(ctx->f[{}], ctx->f[{}])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);",
                                   fs, ft);

            case 0x34: // C.OLT.S - Compare Ordered Less Than
                return fmt::format("ctx->fcr31 = (FPU_C_OLT_S(ctx->f[{}], ctx->f[{}])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);",
                                   fs, ft);

            case 0x35: // C.ULT.S - Compare Unordered or Less Than
                return fmt::format("ctx->fcr31 = (FPU_C_ULT_S(ctx->f[{}], ctx->f[{}])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);",
                                   fs, ft);

            case 0x36: // C.OLE.S - Compare Ordered Less Than or Equal
                return fmt::format("ctx->fcr31 = (FPU_C_OLE_S(ctx->f[{}], ctx->f[{}])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);",
                                   fs, ft);

            case 0x37: // C.ULE.S - Compare Unordered or Less Than or Equal
                return fmt::format("ctx->fcr31 = (FPU_C_ULE_S(ctx->f[{}], ctx->f[{}])) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);",
                                   fs, ft);

            default:
                return fmt::format("// Unhandled FPU.S instruction: function 0x{:X}", function);
            }
        }
        else if (rs == COP1_W)
        {
            // Word format operations
            switch (function)
            {
            case 0x20: // CVT.S.W - Convert Word to Single
                return fmt::format("ctx->f[{}] = FPU_CVT_S_W(*(int32_t*)&ctx->f[{}]);",
                                   fd, fs);

            default:
                return fmt::format("// Unhandled FPU.W instruction: function 0x{:X}", function);
            }
        }

        return fmt::format("// Unhandled FPU instruction: format 0x{:X}, function 0x{:X}", rs, function);
    }

    std::string CodeGenerator::translateQFSRV(const Instruction &inst)
    {
        return fmt::format("{{ uint32_t shift = ctx->sa & 0x1F; "
                           "ctx->r[{}] = _mm_or_si128(_mm_srl_epi32(ctx->r[{}], _mm_cvtsi32_si128(shift)), "
                           "_mm_sll_epi32(ctx->r[{}], _mm_cvtsi32_si128(32 - shift))); }}",
                           inst.rd, inst.rs, inst.rt);
    }

    std::string CodeGenerator::translatePCPYLD(const Instruction &inst)
    {
        return fmt::format("ctx->r[{}] = _mm_shuffle_epi32(_mm_shuffle_epi32("
                           "ctx->r[{}], _MM_SHUFFLE(1, 0, 1, 0)), "
                           "_mm_shuffle_epi32(ctx->r[{}], _MM_SHUFFLE(1, 0, 1, 0)), "
                           "_MM_SHUFFLE(3, 2, 1, 0));",
                           inst.rd, inst.rt, inst.rs);
    }

    std::string CodeGenerator::translatePMADDW(const Instruction &inst)
    {
        return fmt::format("{{ __m128i product = _mm_mullo_epi32(ctx->r[{}], ctx->r[{}]); "
                           "__m128i acc = _mm_set_epi32(0, ctx->hi, 0, ctx->lo); "
                           "__m128i result = _mm_add_epi32(product, acc); "
                           "ctx->r[{}] = result; "
                           "ctx->lo = _mm_extract_epi32(result, 0); "
                           "ctx->hi = _mm_extract_epi32(result, 1); }}",
                           inst.rs, inst.rt, inst.rd);
    }

    std::string CodeGenerator::translatePEXEH(const Instruction &inst)
    {
        return fmt::format("ctx->r[{}] = _mm_shufflelo_epi16(_mm_shufflehi_epi16("
                           "ctx->r[{}], _MM_SHUFFLE(2, 3, 0, 1)), _MM_SHUFFLE(2, 3, 0, 1));",
                           inst.rd, inst.rs);
    }

    std::string CodeGenerator::translatePEXEW(const Instruction &inst)
    {
        return fmt::format("ctx->r[{}] = _mm_shuffle_epi32(ctx->r[{}], _MM_SHUFFLE(2, 3, 0, 1));",
                           inst.rd, inst.rs);
    }

    std::string CodeGenerator::translatePROT3W(const Instruction &inst)
    {
        return fmt::format("ctx->r[{}] = _mm_shuffle_epi32(ctx->r[{}], _MM_SHUFFLE(0, 3, 2, 1));",
                           inst.rd, inst.rs);
    }

    std::string CodeGenerator::translatePMADDH(const Instruction &inst)
    {
        return fmt::format("{{ __m128i product = _mm_mullo_epi16(ctx->r[{}], ctx->r[{}]); "
                           "// Convert 16-bit products to 32-bit\n"
                           "__m128i prod_lo = _mm_unpacklo_epi16(product, _mm_srai_epi16(product, 15));\n"
                           "__m128i prod_hi = _mm_unpackhi_epi16(product, _mm_srai_epi16(product, 15));\n"
                           "// Add to accumulator\n"
                           "__m128i acc = _mm_set_epi32(0, ctx->hi, 0, ctx->lo);\n"
                           "__m128i result = _mm_add_epi32(_mm_add_epi32(prod_lo, prod_hi), acc);\n"
                           "ctx->r[{}] = result;\n"
                           "ctx->lo = _mm_extract_epi32(result, 0);\n"
                           "ctx->hi = _mm_extract_epi32(result, 1); }}",
                           inst.rs, inst.rt, inst.rd);
    }

    std::string CodeGenerator::translatePHMADH(const Instruction &inst)
    {
        return fmt::format("{{ // Multiply horizontally adjacent halfwords\n"
                           "__m128i rtEven = _mm_shuffle_epi32(ctx->r[{}], _MM_SHUFFLE(2, 0, 2, 0));\n"
                           "__m128i rtOdd = _mm_shuffle_epi32(ctx->r[{}], _MM_SHUFFLE(3, 1, 3, 1));\n"
                           "__m128i rsEven = _mm_shuffle_epi32(ctx->r[{}], _MM_SHUFFLE(2, 0, 2, 0));\n"
                           "__m128i rsOdd = _mm_shuffle_epi32(ctx->r[{}], _MM_SHUFFLE(3, 1, 3, 1));\n"
                           "__m128i prod1 = _mm_mullo_epi16(rtEven, rsEven);\n"
                           "__m128i prod2 = _mm_mullo_epi16(rtOdd, rsOdd);\n"
                           "__m128i sum = _mm_add_epi16(prod1, prod2);\n"
                           "// Convert to 32-bit and add to accumulator\n"
                           "__m128i result = _mm_add_epi32(\n"
                           "    _mm_unpacklo_epi16(sum, _mm_srai_epi16(sum, 15)),\n"
                           "    _mm_set_epi32(0, ctx->hi, 0, ctx->lo));\n"
                           "ctx->r[{}] = result;\n"
                           "ctx->lo = _mm_extract_epi32(result, 0);\n"
                           "ctx->hi = _mm_extract_epi32(result, 1); }}",
                           inst.rt, inst.rt, inst.rs, inst.rs, inst.rd);
    }

    std::string CodeGenerator::translatePMULTH(const Instruction &inst)
    {
        return fmt::format("{{ __m128i product = _mm_mullo_epi16(ctx->r[{}], ctx->r[{}]);\n"
                           "__m128i prod_lo = _mm_unpacklo_epi16(product, _mm_srai_epi16(product, 15));\n"
                           "__m128i prod_hi = _mm_unpackhi_epi16(product, _mm_srai_epi16(product, 15));\n"
                           "__m128i result = _mm_add_epi32(prod_lo, prod_hi);\n"
                           "ctx->r[{}] = result;\n"
                           "ctx->lo = _mm_extract_epi32(result, 0);\n"
                           "ctx->hi = _mm_extract_epi32(result, 1); }}",
                           inst.rs, inst.rt, inst.rd);
    }

    std::string CodeGenerator::translatePDIVW(const Instruction &inst)
    {
        return fmt::format("{{ // Extract 32-bit integers\n"
                           "int32_t rs0 = _mm_extract_epi32(ctx->r[{}], 0);\n"
                           "int32_t rs1 = _mm_extract_epi32(ctx->r[{}], 1);\n"
                           "int32_t rs2 = _mm_extract_epi32(ctx->r[{}], 2);\n"
                           "int32_t rs3 = _mm_extract_epi32(ctx->r[{}], 3);\n"
                           "int32_t rt0 = _mm_extract_epi32(ctx->r[{}], 0);\n"
                           "int32_t rt1 = _mm_extract_epi32(ctx->r[{}], 1);\n"
                           "int32_t rt2 = _mm_extract_epi32(ctx->r[{}], 2);\n"
                           "int32_t rt3 = _mm_extract_epi32(ctx->r[{}], 3);\n"
                           "// Perform division, handling div by zero\n"
                           "int32_t lo = (rt0 != 0) ? rs0 / rt0 : 0;\n"
                           "int32_t hi = (rt0 != 0) ? rs0 % rt0 : 0;\n"
                           "// Store results\n"
                           "ctx->lo = lo;\n"
                           "ctx->hi = hi;\n"
                           "// Create result vector\n"
                           "ctx->r[{}] = _mm_set_epi32(0, 0, hi, lo); }}",
                           inst.rs, inst.rs, inst.rs, inst.rs,
                           inst.rt, inst.rt, inst.rt, inst.rt, inst.rd);
    }

    std::string CodeGenerator::translatePDIVBW(const Instruction &inst)
    {
        return fmt::format("{{ // Get the first element of rt as the divisor\n"
                           "int32_t divisor = _mm_extract_epi32(ctx->r[{}], 0);\n"
                           "// Perform division on all elements of rs if divisor is not zero\n"
                           "if (divisor != 0) {{\n"
                           "    int32_t rs0 = _mm_extract_epi32(ctx->r[{}], 0);\n"
                           "    int32_t rs1 = _mm_extract_epi32(ctx->r[{}], 1);\n"
                           "    int32_t rs2 = _mm_extract_epi32(ctx->r[{}], 2);\n"
                           "    int32_t rs3 = _mm_extract_epi32(ctx->r[{}], 3);\n"
                           "    // Store quotient in lo and remainder in hi\n"
                           "    ctx->lo = rs0 / divisor;\n"
                           "    ctx->hi = rs0 % divisor;\n"
                           "    // Create result vector\n"
                           "    ctx->r[{}] = _mm_set_epi32(0, 0, ctx->hi, ctx->lo);\n"
                           "}} }}",
                           inst.rt, inst.rs, inst.rs, inst.rs, inst.rs, inst.rd);
    }

    std::string CodeGenerator::translatePCPYUD(const Instruction &inst)
    {
        return fmt::format("ctx->r[{}] = _mm_unpackhi_epi64(ctx->r[{}], ctx->r[{}]);",
                           inst.rd, inst.rt, inst.rs);
    }

    std::string CodeGenerator::translatePREVH(const Instruction &inst)
    {
        return fmt::format("ctx->r[{}] = _mm_shufflelo_epi16(_mm_shufflehi_epi16("
                           "ctx->r[{}], _MM_SHUFFLE(2, 3, 0, 1)), _MM_SHUFFLE(2, 3, 0, 1));",
                           inst.rd, inst.rs);
    }

    std::string CodeGenerator::translateDSLLV(const Instruction &inst)
    {
        return fmt::format("{{ uint64_t val = ((uint64_t)ctx->r[{}].m128i_u32[1] << 32) | ctx->r[{}].m128i_u32[0]; "
                           "val = val << (ctx->r[{}] & 0x3F); "
                           "ctx->r[{}].m128i_u32[0] = (uint32_t)val; "
                           "ctx->r[{}].m128i_u32[1] = (uint32_t)(val >> 32); }}",
                           inst.rt, inst.rt, inst.rs, inst.rd, inst.rd);
    }

    std::string CodeGenerator::translateDSRLV(const Instruction &inst)
    {
        return fmt::format("{{ uint64_t val = ((uint64_t)ctx->r[{}].m128i_u32[1] << 32) | ctx->r[{}].m128i_u32[0]; "
                           "val = val >> (ctx->r[{}] & 0x3F); "
                           "ctx->r[{}].m128i_u32[0] = (uint32_t)val; "
                           "ctx->r[{}].m128i_u32[1] = (uint32_t)(val >> 32); }}",
                           inst.rt, inst.rt, inst.rs, inst.rd, inst.rd);
    }

    std::string CodeGenerator::translateDSRAV(const Instruction &inst)
    {
        return fmt::format("{{ int64_t val = ((int64_t)(int32_t)ctx->r[{}].m128i_i32[1] << 32) | (uint32_t)ctx->r[{}].m128i_u32[0]; "
                           "val = val >> (ctx->r[{}] & 0x3F); "
                           "ctx->r[{}].m128i_u32[0] = (uint32_t)val; "
                           "ctx->r[{}].m128i_u32[1] = (uint32_t)(val >> 32); }}",
                           inst.rt, inst.rt, inst.rs, inst.rd, inst.rd);
    }

    std::string CodeGenerator::translateDSLL(const Instruction &inst)
    {
        return fmt::format("{{ uint64_t val = ((uint64_t)ctx->r[{}].m128i_u32[1] << 32) | ctx->r[{}].m128i_u32[0]; "
                           "val = val << {}; "
                           "ctx->r[{}].m128i_u32[0] = (uint32_t)val; "
                           "ctx->r[{}].m128i_u32[1] = (uint32_t)(val >> 32); }}",
                           inst.rt, inst.rt, inst.sa, inst.rd, inst.rd);
    }

    std::string CodeGenerator::translateDSRL(const Instruction &inst)
    {
        return fmt::format("{{ uint64_t val = ((uint64_t)ctx->r[{}].m128i_u32[1] << 32) | ctx->r[{}].m128i_u32[0]; "
                           "val = val >> {}; "
                           "ctx->r[{}].m128i_u32[0] = (uint32_t)val; "
                           "ctx->r[{}].m128i_u32[1] = (uint32_t)(val >> 32); }}",
                           inst.rt, inst.rt, inst.sa, inst.rd, inst.rd);
    }

    std::string CodeGenerator::translateDSRA(const Instruction &inst)
    {
        return fmt::format("{{ int64_t val = ((int64_t)(int32_t)ctx->r[{}].m128i_i32[1] << 32) | (uint32_t)ctx->r[{}].m128i_u32[0]; "
                           "val = val >> {}; "
                           "ctx->r[{}].m128i_u32[0] = (uint32_t)val; "
                           "ctx->r[{}].m128i_u32[1] = (uint32_t)(val >> 32); }}",
                           inst.rt, inst.rt, inst.sa, inst.rd, inst.rd);
    }

    std::string CodeGenerator::translateDSLL32(const Instruction &inst)
    {
        return fmt::format("{{ uint64_t val = ((uint64_t)ctx->r[{}].m128i_u32[1] << 32) | ctx->r[{}].m128i_u32[0]; "
                           "val = val << (32 + {}); "
                           "ctx->r[{}].m128i_u32[0] = (uint32_t)val; "
                           "ctx->r[{}].m128i_u32[1] = (uint32_t)(val >> 32); }}",
                           inst.rt, inst.rt, inst.sa, inst.rd, inst.rd);
    }

    std::string CodeGenerator::translateDSRL32(const Instruction &inst)
    {
        return fmt::format("{{ uint64_t val = ((uint64_t)ctx->r[{}].m128i_u32[1] << 32) | ctx->r[{}].m128i_u32[0]; "
                           "val = val >> (32 + {}); "
                           "ctx->r[{}].m128i_u32[0] = (uint32_t)val; "
                           "ctx->r[{}].m128i_u32[1] = 0; }}",
                           inst.rt, inst.rt, inst.sa, inst.rd, inst.rd);
    }

    std::string CodeGenerator::translateDSRA32(const Instruction &inst)
    {
        return fmt::format("{{ int64_t val = ((int64_t)(int32_t)ctx->r[{}].m128i_i32[1] << 32) | (uint32_t)ctx->r[{}].m128i_u32[0]; "
                           "val = val >> (32 + {}); "
                           "ctx->r[{}].m128i_u32[0] = (uint32_t)val; "
                           "ctx->r[{}].m128i_u32[1] = (val < 0) ? 0xFFFFFFFF : 0; }}",
                           inst.rt, inst.rt, inst.sa, inst.rd, inst.rd);
    }

    std::string CodeGenerator::translateDADD(const Instruction &inst)
    {
        return fmt::format("{{ int64_t a = ((int64_t)(int32_t)ctx->r[{}].m128i_i32[1] << 32) | (uint32_t)ctx->r[{}].m128i_u32[0]; "
                           "int64_t b = ((int64_t)(int32_t)ctx->r[{}].m128i_i32[1] << 32) | (uint32_t)ctx->r[{}].m128i_u32[0]; "
                           "int64_t res = a + b; "
                           "ctx->r[{}].m128i_u32[0] = (uint32_t)res; "
                           "ctx->r[{}].m128i_u32[1] = (uint32_t)(res >> 32); }}",
                           inst.rs, inst.rs, inst.rt, inst.rt, inst.rd, inst.rd);
    }

    std::string CodeGenerator::translateDADDU(const Instruction &inst)
    {
        return fmt::format("{{ uint64_t a = ((uint64_t)ctx->r[{}].m128i_u32[1] << 32) | ctx->r[{}].m128i_u32[0]; "
                           "uint64_t b = ((uint64_t)ctx->r[{}].m128i_u32[1] << 32) | ctx->r[{}].m128i_u32[0]; "
                           "uint64_t res = a + b; "
                           "ctx->r[{}].m128i_u32[0] = (uint32_t)res; "
                           "ctx->r[{}].m128i_u32[1] = (uint32_t)(res >> 32); }}",
                           inst.rs, inst.rs, inst.rt, inst.rt, inst.rd, inst.rd);
    }

    std::string CodeGenerator::translateDSUB(const Instruction &inst)
    {
        return fmt::format("{{ int64_t a = ((int64_t)(int32_t)ctx->r[{}].m128i_i32[1] << 32) | (uint32_t)ctx->r[{}].m128i_u32[0]; "
                           "int64_t b = ((int64_t)(int32_t)ctx->r[{}].m128i_i32[1] << 32) | (uint32_t)ctx->r[{}].m128i_u32[0]; "
                           "int64_t res = a - b; "
                           "ctx->r[{}].m128i_u32[0] = (uint32_t)res; "
                           "ctx->r[{}].m128i_u32[1] = (uint32_t)(res >> 32); }}",
                           inst.rs, inst.rs, inst.rt, inst.rt, inst.rd, inst.rd);
    }

    std::string CodeGenerator::translateDSUBU(const Instruction &inst)
    {
        return fmt::format("{{ uint64_t a = ((uint64_t)ctx->r[{}].m128i_u32[1] << 32) | ctx->r[{}].m128i_u32[0]; "
                           "uint64_t b = ((uint64_t)ctx->r[{}].m128i_u32[1] << 32) | ctx->r[{}].m128i_u32[0]; "
                           "uint64_t res = a - b; "
                           "ctx->r[{}].m128i_u32[0] = (uint32_t)res; "
                           "ctx->r[{}].m128i_u32[1] = (uint32_t)(res >> 32); }}",
                           inst.rs, inst.rs, inst.rt, inst.rt, inst.rd, inst.rd);
    }

    std::string CodeGenerator::translateSYNC(const Instruction &inst)
    {
        return "// SYNC instruction - memory barrier\n"
               "    // In recompiled code, we don't need explicit memory barriers";
    }

    std::string CodeGenerator::translateEI(const Instruction &inst)
    {
        return "ctx->cop0_status |= 0x1; // Enable interrupts";
    }

    std::string CodeGenerator::translateDI(const Instruction &inst)
    {
        return "ctx->cop0_status &= ~0x1; // Disable interrupts";
    }

    std::string CodeGenerator::translatePMULTUW(const Instruction &inst)
    {
        return fmt::format("{{ // Packed multiply of unsigned 32-bit integers\n"
                           "    __m128i a = ctx->r[{}];\n"
                           "    __m128i b = ctx->r[{}];\n"
                           "    // Extract 32-bit integers\n"
                           "    uint32_t a0 = _mm_extract_epi32(a, 0);\n"
                           "    uint32_t a1 = _mm_extract_epi32(a, 1);\n"
                           "    uint32_t a2 = _mm_extract_epi32(a, 2);\n"
                           "    uint32_t a3 = _mm_extract_epi32(a, 3);\n"
                           "    uint32_t b0 = _mm_extract_epi32(b, 0);\n"
                           "    uint32_t b1 = _mm_extract_epi32(b, 1);\n"
                           "    uint32_t b2 = _mm_extract_epi32(b, 2);\n"
                           "    uint32_t b3 = _mm_extract_epi32(b, 3);\n"
                           "    // Compute products\n"
                           "    uint64_t product0 = (uint64_t)a0 * (uint64_t)b0;\n"
                           "    uint64_t product1 = (uint64_t)a1 * (uint64_t)b1;\n"
                           "    uint64_t product2 = (uint64_t)a2 * (uint64_t)b2;\n"
                           "    uint64_t product3 = (uint64_t)a3 * (uint64_t)b3;\n"
                           "    // Store results\n"
                           "    ctx->lo = (uint32_t)product0;\n"
                           "    ctx->hi = (uint32_t)(product0 >> 32);\n"
                           "    // Create result vector with the multiplication results\n"
                           "    ctx->r[{}] = _mm_set_epi32(\n"
                           "        (uint32_t)product3, (uint32_t)product2,\n"
                           "        (uint32_t)product1, (uint32_t)product0); }}",
                           inst.rs, inst.rt, inst.rd);
    }

    std::string CodeGenerator::translatePDIVUW(const Instruction &inst)
    {
        return fmt::format("{{ // Packed division of unsigned 32-bit integers\n"
                           "    __m128i a = ctx->r[{}];\n"
                           "    __m128i b = ctx->r[{}];\n"
                           "    // Extract 32-bit integers\n"
                           "    uint32_t a0 = _mm_extract_epi32(a, 0);\n"
                           "    uint32_t b0 = _mm_extract_epi32(b, 0);\n"
                           "    // PS2 PDIVUW only operates on the first word\n"
                           "    uint32_t quotient = 0;\n"
                           "    uint32_t remainder = 0;\n"
                           "    if (b0 != 0) {{ // Check to avoid division by zero\n"
                           "        quotient = a0 / b0;\n"
                           "        remainder = a0 % b0;\n"
                           "    }}\n"
                           "    // Store results\n"
                           "    ctx->lo = quotient;\n"
                           "    ctx->hi = remainder;\n"
                           "    // Create result vector with zeros except in lowest word\n"
                           "    ctx->r[{}] = _mm_set_epi32(0, 0, 0, quotient); }}",
                           inst.rs, inst.rt, inst.rd);
    }

    std::string CodeGenerator::translatePEXCH(const Instruction &inst)
    {
        return fmt::format("{{ // Parallel Exchange Center Halfword\n"
                           "    // For each 64-bit half, exchange the middle two 16-bit values\n"
                           "    // [A3|A2|A1|A0] => [A3|A1|A2|A0] for each 64-bit half\n"
                           "    __m128i src = ctx->r[{}];\n"
                           "    // Create a shuffle mask for _mm_shuffle_epi8 that exchanges the center halfwords\n"
                           "    // For low 64 bits: [0,1, 4,5, 2,3, 6,7] becomes [0,1, 4,5, 6,7, 2,3]\n"
                           "    // For high 64 bits: [8,9, 12,13, 10,11, 14,15] becomes [8,9, 12,13, 14,15, 10,11]\n"
                           "    __m128i shuffleMask = _mm_set_epi8(\n"
                           "        15, 14, 13, 12, 11, 10, 9, 8,    // High 64 bits: keep order\n"
                           "        7, 6, 3, 2, 5, 4, 1, 0);         // Low 64 bits: swap 2-3 and 6-7\n"
                           "    __m128i shuffled = _mm_shuffle_epi8(src, shuffleMask);\n"
                           "    // Now we need to swap the byte pairs within each 64-bit half\n"
                           "    // Using a 16-bit shuffle for each 64-bit half\n"
                           "    ctx->r[{}] = _mm_shufflelo_epi16(_mm_shufflehi_epi16(shuffled, \n"
                           "        _MM_SHUFFLE(3, 1, 2, 0)), _MM_SHUFFLE(3, 1, 2, 0)); }}",
                           inst.rs, inst.rd);
    }

    std::string CodeGenerator::translatePCPYH(const Instruction &inst)
    {
        return fmt::format("{{ // Parallel Copy Halfword\n"
                           "    // Copy the lowest 16-bit value in each 64-bit half to all halfwords in that half\n"
                           "    // [A3|A2|A1|A0] => [A0|A0|A0|A0] for the low 64 bits\n"
                           "    // [B3|B2|B1|B0] => [B0|B0|B0|B0] for the high 64 bits\n"
                           "    __m128i src = ctx->r[{}];\n"
                           "    // Extract the lowest 16-bit value from each 64-bit half\n"
                           "    uint16_t lowHalf = (uint16_t)_mm_extract_epi16(src, 0);\n"
                           "    uint16_t highHalf = (uint16_t)_mm_extract_epi16(src, 4);\n"
                           "    // Create a vector with these values repeated\n"
                           "    ctx->r[{}] = _mm_set_epi16(\n"
                           "        highHalf, highHalf, highHalf, highHalf,\n"
                           "        lowHalf, lowHalf, lowHalf, lowHalf); }}",
                           inst.rs, inst.rd);
    }

    std::string CodeGenerator::translatePEXCW(const Instruction &inst)
    {
        return fmt::format("{{ // Parallel Exchange Center Word\n"
                           "    // Exchange the two 32-bit words in each 64-bit half\n"
                           "    // [A1|A0|B1|B0] => [A0|A1|B0|B1]\n"
                           "    __m128i src = ctx->r[{}];\n"
                           "    // Exchange words in each 64-bit half using shuffle\n"
                           "    ctx->r[{}] = _mm_shuffle_epi32(src, _MM_SHUFFLE(2, 3, 0, 1)); }}",
                           inst.rs, inst.rd);
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