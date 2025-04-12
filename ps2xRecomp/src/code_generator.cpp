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
                ss << "    ctx->r31 = 0x" << std::hex << (branchInst.address + 8) << ";\n"
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
                ss << "    ctx->r" << branchInst.rd << " = 0x" << std::hex << (branchInst.address + 8) << ";\n"
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
                ss << "    LOOKUP_FUNC(ctx->r" << branchInst.rs << ")(rdram, ctx);\n";
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
                conditionStr = fmt::format("ctx->r{} == ctx->r{}", branchInst.rs, branchInst.rt);
                break;

            case OPCODE_BNE:
                conditionStr = fmt::format("ctx->r{} != ctx->r{}", branchInst.rs, branchInst.rt);
                break;

            case OPCODE_BLEZ:
                conditionStr = fmt::format("(int32_t)ctx->r{} <= 0", branchInst.rs);
                break;

            case OPCODE_BGTZ:
                conditionStr = fmt::format("(int32_t)ctx->r{} > 0", branchInst.rs);
                break;

            case OPCODE_BEQL:
                conditionStr = fmt::format("ctx->r{} == ctx->r{}", branchInst.rs, branchInst.rt);
                break;

            case OPCODE_BNEL:
                conditionStr = fmt::format("ctx->r{} != ctx->r{}", branchInst.rs, branchInst.rt);
                break;

            case OPCODE_BLEZL:
                conditionStr = fmt::format("(int32_t)ctx->r{} <= 0", branchInst.rs);
                break;

            case OPCODE_BGTZL:
                conditionStr = fmt::format("(int32_t)ctx->r{} > 0", branchInst.rs);
                break;

            case OPCODE_REGIMM:
                switch (branchInst.rt)
                {
                case REGIMM_BLTZ:
                    conditionStr = fmt::format("(int32_t)ctx->r{} < 0", branchInst.rs);
                    break;

                case REGIMM_BGEZ:
                    conditionStr = fmt::format("(int32_t)ctx->r{} >= 0", branchInst.rs);
                    break;

                case REGIMM_BLTZL:
                    conditionStr = fmt::format("(int32_t)ctx->r{} < 0", branchInst.rs);
                    break;

                case REGIMM_BGEZL:
                    conditionStr = fmt::format("(int32_t)ctx->r{} >= 0", branchInst.rs);
                    break;

                case REGIMM_BLTZAL:
                    conditionStr = fmt::format("(int32_t)ctx->r{} < 0", branchInst.rs);
                    ss << "    ctx->r31 = 0x" << std::hex << (branchInst.address + 8) << ";\n"
                       << std::dec;
                    break;

                case REGIMM_BGEZAL:
                    conditionStr = fmt::format("(int32_t)ctx->r{} >= 0", branchInst.rs);
                    ss << "    ctx->r31 = 0x" << std::hex << (branchInst.address + 8) << ";\n"
                       << std::dec;
                    break;

                case REGIMM_BLTZALL:
                    conditionStr = fmt::format("(int32_t)ctx->r{} < 0", branchInst.rs);
                    ss << "    ctx->r31 = 0x" << std::hex << (branchInst.address + 8) << ";\n"
                       << std::dec;
                    break;

                case REGIMM_BGEZALL:
                    conditionStr = fmt::format("(int32_t)ctx->r{} >= 0", branchInst.rs);
                    ss << "    ctx->r31 = 0x" << std::hex << (branchInst.address + 8) << ";\n"
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

        ss << "#endif // PS2_RUNTIME_MACROS_H\n";

        return ss.str();
    }

    std::string CodeGenerator::generateFunction(const Function &function, const std::vector<Instruction> &instructions)
    {
        std::stringstream ss;

        ss << "#include \"ps2_runtime_macros.h\"\n";
        ss << "#include \"ps2_runtime.h\"\n\n";

        ss << "// Function: " << function.name << "\n";
        ss << "// Address: 0x" << std::hex << function.start << " - 0x" << function.end << std::dec << "\n";
        ss << "void " << function.name << "(uint8_t* rdram, R5900Context* ctx) {\n";

        ss << "    // Local variables\n";
        ss << "    uint32_t temp;\n";
        ss << "    uint32_t branch_target;\n\n";

        for (size_t i = 0; i < instructions.size(); ++i)
        {
            const Instruction &inst = instructions[i];

            ss << "    // 0x" << std::hex << inst.address << ": 0x" << inst.raw << std::dec << "\n";

            // Check if this is a branch or jump with a delay slot
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
            switch (inst.function)
            {
            case SPECIAL_SLL:
                if (inst.rd == 0 && inst.rt == 0 && inst.sa == 0)
                {
                    return "// NOP";
                }
                return fmt::format("ctx->r{} = SLL32(ctx->r{}, {});",
                                   inst.rd, inst.rt, inst.sa);

            case SPECIAL_SRL:
                return fmt::format("ctx->r{} = SRL32(ctx->r{}, {});",
                                   inst.rd, inst.rt, inst.sa);

            case SPECIAL_SRA:
                return fmt::format("ctx->r{} = SRA32(ctx->r{}, {});",
                                   inst.rd, inst.rt, inst.sa);

            case SPECIAL_SLLV:
                return fmt::format("ctx->r{} = SLL32(ctx->r{}, ctx->r{} & 0x1F);",
                                   inst.rd, inst.rt, inst.rs);

            case SPECIAL_SRLV:
                return fmt::format("ctx->r{} = SRL32(ctx->r{}, ctx->r{} & 0x1F);",
                                   inst.rd, inst.rt, inst.rs);

            case SPECIAL_SRAV:
                return fmt::format("ctx->r{} = SRA32(ctx->r{}, ctx->r{} & 0x1F);",
                                   inst.rd, inst.rt, inst.rs);

            case SPECIAL_MFHI:
                return fmt::format("ctx->r{} = ctx->hi;", inst.rd);

            case SPECIAL_MTHI:
                return fmt::format("ctx->hi = ctx->r{};", inst.rs);

            case SPECIAL_MFLO:
                return fmt::format("ctx->r{} = ctx->lo;", inst.rd);

            case SPECIAL_MTLO:
                return fmt::format("ctx->lo = ctx->r{};", inst.rs);

            case SPECIAL_MULT:
                return fmt::format("{{ int64_t result = (int64_t)(int32_t)ctx->r{} * (int64_t)(int32_t)ctx->r{}; ctx->lo = (uint32_t)result; ctx->hi = (uint32_t)(result >> 32); }}",
                                   inst.rs, inst.rt);

            case SPECIAL_MULTU:
                return fmt::format("{{ uint64_t result = (uint64_t)ctx->r{} * (uint64_t)ctx->r{}; ctx->lo = (uint32_t)result; ctx->hi = (uint32_t)(result >> 32); }}",
                                   inst.rs, inst.rt);

            case SPECIAL_DIV:
                return fmt::format("{{ if (ctx->r{} != 0) {{ ctx->lo = (uint32_t)((int32_t)ctx->r{} / (int32_t)ctx->r{}); ctx->hi = (uint32_t)((int32_t)ctx->r{} % (int32_t)ctx->r{}); }} }}",
                                   inst.rt, inst.rs, inst.rt, inst.rs, inst.rt);

            case SPECIAL_DIVU:
                return fmt::format("{{ if (ctx->r{} != 0) {{ ctx->lo = ctx->r{} / ctx->r{}; ctx->hi = ctx->r{} % ctx->r{}; }} }}",
                                   inst.rt, inst.rs, inst.rt, inst.rs, inst.rt);

            case SPECIAL_ADD:
            case SPECIAL_ADDU:
                return fmt::format("ctx->r{} = ADD32(ctx->r{}, ctx->r{});",
                                   inst.rd, inst.rs, inst.rt);

            case SPECIAL_SUB:
            case SPECIAL_SUBU:
                return fmt::format("ctx->r{} = SUB32(ctx->r{}, ctx->r{});",
                                   inst.rd, inst.rs, inst.rt);

            case SPECIAL_AND:
                return fmt::format("ctx->r{} = AND32(ctx->r{}, ctx->r{});",
                                   inst.rd, inst.rs, inst.rt);

            case SPECIAL_OR:
                return fmt::format("ctx->r{} = OR32(ctx->r{}, ctx->r{});",
                                   inst.rd, inst.rs, inst.rt);

            case SPECIAL_XOR:
                return fmt::format("ctx->r{} = XOR32(ctx->r{}, ctx->r{});",
                                   inst.rd, inst.rs, inst.rt);

            case SPECIAL_NOR:
                return fmt::format("ctx->r{} = NOR32(ctx->r{}, ctx->r{});",
                                   inst.rd, inst.rs, inst.rt);

            case SPECIAL_SLT:
                return fmt::format("ctx->r{} = SLT32(ctx->r{}, ctx->r{});",
                                   inst.rd, inst.rs, inst.rt);

            case SPECIAL_SLTU:
                return fmt::format("ctx->r{} = SLTU32(ctx->r{}, ctx->r{});",
                                   inst.rd, inst.rs, inst.rt);

            // PS2-specific instructions
            case SPECIAL_MOVZ:
                return fmt::format("if (ctx->r{} == 0) ctx->r{} = ctx->r{};",
                                   inst.rt, inst.rd, inst.rs);

            case SPECIAL_MOVN:
                return fmt::format("if (ctx->r{} != 0) ctx->r{} = ctx->r{};",
                                   inst.rt, inst.rd, inst.rs);

            default:
                return fmt::format("// Unhandled SPECIAL instruction: 0x{:X}", inst.function);
            }
            break;

        case OPCODE_ADDI:
        case OPCODE_ADDIU:
            if (inst.rt == 0)
            {
                return "// NOP (addiu $zero, ...)";
            }
            return fmt::format("ctx->r{} = ADD32(ctx->r{}, 0x{:X});",
                               inst.rt, inst.rs, (int16_t)inst.immediate);

        case OPCODE_SLTI:
            return fmt::format("ctx->r{} = (int32_t)ctx->r{} < (int32_t)0x{:X} ? 1 : 0;",
                               inst.rt, inst.rs, (int16_t)inst.immediate);

        case OPCODE_SLTIU:
            return fmt::format("ctx->r{} = ctx->r{} < (uint32_t)0x{:X} ? 1 : 0;",
                               inst.rt, inst.rs, (uint32_t)(int16_t)inst.immediate);

        case OPCODE_ANDI:
            return fmt::format("ctx->r{} = AND32(ctx->r{}, 0x{:X});",
                               inst.rt, inst.rs, inst.immediate);

        case OPCODE_ORI:
            return fmt::format("ctx->r{} = OR32(ctx->r{}, 0x{:X});",
                               inst.rt, inst.rs, inst.immediate);

        case OPCODE_XORI:
            return fmt::format("ctx->r{} = XOR32(ctx->r{}, 0x{:X});",
                               inst.rt, inst.rs, inst.immediate);

        case OPCODE_LUI:
            return fmt::format("ctx->r{} = 0x{:X} << 16;",
                               inst.rt, inst.immediate);

        case OPCODE_LB:
            return fmt::format("ctx->r{} = (int32_t)(int8_t)READ8(ADD32(ctx->r{}, 0x{:X}));",
                               inst.rt, inst.rs, (int16_t)inst.immediate);

        case OPCODE_LH:
            return fmt::format("ctx->r{} = (int32_t)(int16_t)READ16(ADD32(ctx->r{}, 0x{:X}));",
                               inst.rt, inst.rs, (int16_t)inst.immediate);

        case OPCODE_LW:
            return fmt::format("ctx->r{} = READ32(ADD32(ctx->r{}, 0x{:X}));",
                               inst.rt, inst.rs, (int16_t)inst.immediate);

        case OPCODE_LBU:
            return fmt::format("ctx->r{} = (uint32_t)READ8(ADD32(ctx->r{}, 0x{:X}));",
                               inst.rt, inst.rs, (int16_t)inst.immediate);

        case OPCODE_LHU:
            return fmt::format("ctx->r{} = (uint32_t)READ16(ADD32(ctx->r{}, 0x{:X}));",
                               inst.rt, inst.rs, (int16_t)inst.immediate);

        case OPCODE_SB:
            return fmt::format("WRITE8(ADD32(ctx->r{}, 0x{:X}), (uint8_t)ctx->r{});",
                               inst.rs, (int16_t)inst.immediate, inst.rt);

        case OPCODE_SH:
            return fmt::format("WRITE16(ADD32(ctx->r{}, 0x{:X}), (uint16_t)ctx->r{});",
                               inst.rs, (int16_t)inst.immediate, inst.rt);

        case OPCODE_SW:
            return fmt::format("WRITE32(ADD32(ctx->r{}, 0x{:X}), ctx->r{});",
                               inst.rs, (int16_t)inst.immediate, inst.rt);

        // PS2-specific 128-bit load/store
        case OPCODE_LQ:
            return fmt::format("ctx->r{} = (__m128i)READ128(ADD32(ctx->r{}, 0x{:X}));",
                               inst.rt, inst.rs, (int16_t)inst.immediate);

        case OPCODE_SQ:
            return fmt::format("WRITE128(ADD32(ctx->r{}, 0x{:X}), (__m128i)ctx->r{});",
                               inst.rs, (int16_t)inst.immediate, inst.rt);

        // Special case for R5900
        case OPCODE_CACHE:
            return "// CACHE instruction (ignored)";

        case OPCODE_PREF:
            return "// PREF instruction (ignored)";

        case OPCODE_COP1:
            return translateFPUInstruction(inst);

        case OPCODE_COP0:
            return translateCOP0Instruction(inst);

        default:
            return fmt::format("// Unhandled opcode: 0x{:X}", inst.opcode);
        }
    }

    std::string CodeGenerator::translateMMIInstruction(const Instruction &inst)
    {
        uint32_t function = inst.function;

        switch (function)
        {
        case MMI_MADD:
            return fmt::format("{{ int64_t result = (int64_t)(((int64_t)ctx->hi << 32) | ctx->lo) + (int64_t)(int32_t)ctx->r{} * (int64_t)(int32_t)ctx->r{}; ctx->lo = (uint32_t)result; ctx->hi = (uint32_t)(result >> 32); }}",
                               inst.rs, inst.rt);

        case MMI_MADDU:
            return fmt::format("{{ uint64_t result = (uint64_t)(((uint64_t)ctx->hi << 32) | ctx->lo) + (uint64_t)ctx->r{} * (uint64_t)ctx->r{}; ctx->lo = (uint32_t)result; ctx->hi = (uint32_t)(result >> 32); }}",
                               inst.rs, inst.rt);

        case MMI_PLZCW:
            return fmt::format("ctx->r{} = __builtin_clz(ctx->r{});",
                               inst.rd, inst.rs);

        case MMI_MFHI1:
            return fmt::format("ctx->r{} = ctx->hi;", inst.rd);

        case MMI_MTHI1:
            return fmt::format("ctx->hi = ctx->r{};", inst.rs);

        case MMI_MFLO1:
            return fmt::format("ctx->r{} = ctx->lo;", inst.rd);

        case MMI_MTLO1:
            return fmt::format("ctx->lo = ctx->r{};", inst.rs);

        case MMI_MULT1:
            return fmt::format("{{ int64_t result = (int64_t)(int32_t)ctx->r{} * (int64_t)(int32_t)ctx->r{}; ctx->lo = (uint32_t)result; ctx->hi = (uint32_t)(result >> 32); }}",
                               inst.rs, inst.rt);

        case MMI_MULTU1:
            return fmt::format("{{ uint64_t result = (uint64_t)ctx->r{} * (uint64_t)ctx->r{}; ctx->lo = (uint32_t)result; ctx->hi = (uint32_t)(result >> 32); }}",
                               inst.rs, inst.rt);

        case MMI_DIV1:
            return fmt::format("{{ if (ctx->r{} != 0) {{ ctx->lo = (uint32_t)((int32_t)ctx->r{} / (int32_t)ctx->r{}); ctx->hi = (uint32_t)((int32_t)ctx->r{} % (int32_t)ctx->r{}); }} }}",
                               inst.rt, inst.rs, inst.rt, inst.rs, inst.rt);

        case MMI_DIVU1:
            return fmt::format("{{ if (ctx->r{} != 0) {{ ctx->lo = ctx->r{} / ctx->r{}; ctx->hi = ctx->r{} % ctx->r{}; }} }}",
                               inst.rt, inst.rs, inst.rt, inst.rs, inst.rt);

        case MMI_MADD1:
            return fmt::format("{{ int64_t result = (int64_t)(((int64_t)ctx->hi << 32) | ctx->lo) + (int64_t)(int32_t)ctx->r{} * (int64_t)(int32_t)ctx->r{}; ctx->lo = (uint32_t)result; ctx->hi = (uint32_t)(result >> 32); }}",
                               inst.rs, inst.rt);

        case MMI_MADDU1:
            return fmt::format("{{ uint64_t result = (uint64_t)(((uint64_t)ctx->hi << 32) | ctx->lo) + (uint64_t)ctx->r{} * (uint64_t)ctx->r{}; ctx->lo = (uint32_t)result; ctx->hi = (uint32_t)(result >> 32); }}",
                               inst.rs, inst.rt);

        // MMI0 functions (PADDW, PSUBW, etc.)
        case MMI_MMI0:
            switch (inst.sa)
            {
            case MMI0_PADDW:
                return fmt::format("ctx->r{} = PS2_PADDW(ctx->r{}, ctx->r{});",
                                   inst.rd, inst.rs, inst.rt);

            case MMI0_PSUBW:
                return fmt::format("ctx->r{} = PS2_PSUBW(ctx->r{}, ctx->r{});",
                                   inst.rd, inst.rs, inst.rt);

            case MMI0_PCGTW:
                return fmt::format("ctx->r{} = PS2_PCGTW(ctx->r{}, ctx->r{});",
                                   inst.rd, inst.rs, inst.rt);

            case MMI0_PMAXW:
                return fmt::format("ctx->r{} = PS2_PMAXW(ctx->r{}, ctx->r{});",
                                   inst.rd, inst.rs, inst.rt);

            case MMI0_PADDH:
                return fmt::format("ctx->r{} = PS2_PADDH(ctx->r{}, ctx->r{});",
                                   inst.rd, inst.rs, inst.rt);

            case MMI0_PSUBH:
                return fmt::format("ctx->r{} = PS2_PSUBH(ctx->r{}, ctx->r{});",
                                   inst.rd, inst.rs, inst.rt);

            case MMI0_PCGTH:
                return fmt::format("ctx->r{} = PS2_PCGTH(ctx->r{}, ctx->r{});",
                                   inst.rd, inst.rs, inst.rt);

            case MMI0_PMAXH:
                return fmt::format("ctx->r{} = PS2_PMAXH(ctx->r{}, ctx->r{});",
                                   inst.rd, inst.rs, inst.rt);

            case MMI0_PADDB:
                return fmt::format("ctx->r{} = PS2_PADDB(ctx->r{}, ctx->r{});",
                                   inst.rd, inst.rs, inst.rt);

            case MMI0_PSUBB:
                return fmt::format("ctx->r{} = PS2_PSUBB(ctx->r{}, ctx->r{});",
                                   inst.rd, inst.rs, inst.rt);

            case MMI0_PCGTB:
                return fmt::format("ctx->r{} = PS2_PCGTB(ctx->r{}, ctx->r{});",
                                   inst.rd, inst.rs, inst.rt);

            case MMI0_PEXTLW:
                return fmt::format("ctx->r{} = PS2_PEXTLW(ctx->r{}, ctx->r{});",
                                   inst.rd, inst.rs, inst.rt);

            case MMI0_PPACW:
                return fmt::format("ctx->r{} = PS2_PPACW(ctx->r{}, ctx->r{});",
                                   inst.rd, inst.rs, inst.rt);

            case MMI0_PEXTLH:
                return fmt::format("ctx->r{} = PS2_PEXTLH(ctx->r{}, ctx->r{});",
                                   inst.rd, inst.rs, inst.rt);

            case MMI0_PPACH:
                return fmt::format("ctx->r{} = PS2_PPACH(ctx->r{}, ctx->r{});",
                                   inst.rd, inst.rs, inst.rt);

            case MMI0_PEXTLB:
                return fmt::format("ctx->r{} = PS2_PEXTLB(ctx->r{}, ctx->r{});",
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
                return fmt::format("ctx->r{} = PS2_PADDW(ctx->r{}, ctx->r{});",
                                   inst.rd, inst.rs, inst.rt);

            case MMI1_PSUBUW:
                return fmt::format("ctx->r{} = PS2_PSUBW(ctx->r{}, ctx->r{});",
                                   inst.rd, inst.rs, inst.rt);

            case MMI1_PEXTUW:
                return fmt::format("ctx->r{} = PS2_PEXTUW(ctx->r{}, ctx->r{});",
                                   inst.rd, inst.rs, inst.rt);

            case MMI1_PADDUH:
                return fmt::format("ctx->r{} = PS2_PADDH(ctx->r{}, ctx->r{});",
                                   inst.rd, inst.rs, inst.rt);

            case MMI1_PSUBUH:
                return fmt::format("ctx->r{} = PS2_PSUBH(ctx->r{}, ctx->r{});",
                                   inst.rd, inst.rs, inst.rt);

            case MMI1_PEXTUH:
                return fmt::format("ctx->r{} = PS2_PEXTUH(ctx->r{}, ctx->r{});",
                                   inst.rd, inst.rs, inst.rt);
            case MMI1_PABSW:
                return fmt::format("ctx->r{} = PS2_PABSW(ctx->r{});",
                                   inst.rd, inst.rs);

            case MMI1_PABSH:
                return fmt::format("ctx->r{} = PS2_PABSH(ctx->r{});",
                                   inst.rd, inst.rs);

            case MMI1_PCEQW:
                return fmt::format("ctx->r{} = PS2_PCEQW(ctx->r{}, ctx->r{});",
                                   inst.rd, inst.rs, inst.rt);

            case MMI1_PCEQH:
                return fmt::format("ctx->r{} = PS2_PCEQH(ctx->r{}, ctx->r{});",
                                   inst.rd, inst.rs, inst.rt);

            case MMI1_PCEQB:
                return fmt::format("ctx->r{} = PS2_PCEQB(ctx->r{}, ctx->r{});",
                                   inst.rd, inst.rs, inst.rt);

            case MMI1_PMINW:
                return fmt::format("ctx->r{} = PS2_PMINW(ctx->r{}, ctx->r{});",
                                   inst.rd, inst.rs, inst.rt);

            case MMI1_PMINH:
                return fmt::format("ctx->r{} = PS2_PMINH(ctx->r{}, ctx->r{});",
                                   inst.rd, inst.rs, inst.rt);

            case MMI1_PADDUB:
                return fmt::format("ctx->r{} = PS2_PADDB(ctx->r{}, ctx->r{});",
                                   inst.rd, inst.rs, inst.rt);

            case MMI1_PSUBUB:
                return fmt::format("ctx->r{} = PS2_PSUBB(ctx->r{}, ctx->r{});",
                                   inst.rd, inst.rs, inst.rt);

            case MMI1_PEXTUB:
                return fmt::format("ctx->r{} = PS2_PEXTUB(ctx->r{}, ctx->r{});",
                                   inst.rd, inst.rs, inst.rt);

            case MMI1_QFSRV:
                return fmt::format("// PS2_QFSRV - Quadword Funnel Shift Right Variable");

            default:
                return fmt::format("// Unhandled MMI1 function: 0x{:X}", inst.sa);
            }
            break;

        // MMI2 functions (PMADDW, PSLLVW, etc.)
        case MMI_MMI2:
            switch (inst.sa)
            {
            case MMI2_PMADDW:
                return fmt::format("ctx->r{} = PS2_PMADDW(ctx->r{}, ctx->r{});",
                                   inst.rd, inst.rs, inst.rt);

            case MMI2_PSLLVW:
                return fmt::format("ctx->r{} = PS2_PSLLVW(ctx->r{}, ctx->r{});",
                                   inst.rd, inst.rs, inst.rt);

            case MMI2_PSRLVW:
                return fmt::format("ctx->r{} = PS2_PSRLVW(ctx->r{}, ctx->r{});",
                                   inst.rd, inst.rs, inst.rt);

            case MMI2_PINTH:
                return fmt::format("ctx->r{} = PS2_PINTH(ctx->r{}, ctx->r{});",
                                   inst.rd, inst.rs, inst.rt);

            case MMI2_PAND:
                return fmt::format("ctx->r{} = PS2_PAND(ctx->r{}, ctx->r{});",
                                   inst.rd, inst.rs, inst.rt);

            case MMI2_PXOR:
                return fmt::format("ctx->r{} = PS2_PXOR(ctx->r{}, ctx->r{});",
                                   inst.rd, inst.rs, inst.rt);

            case MMI2_PMULTW:
                return fmt::format("// PS2_PMULTW - Packed Multiply Word");

            case MMI2_PDIVW:
                return fmt::format("// PS2_PDIVW - Packed Divide Word");

            case MMI2_PCPYLD:
                return fmt::format("// PS2_PCPYLD - Parallel Copy Lower Doubleword");

            case MMI2_PMADDH:
                return fmt::format("// PS2_PMADDH - Packed Multiply-Add Halfword");

            case MMI2_PHMADH:
                return fmt::format("// PS2_PHMADH - Packed Horizontal Multiply-Add Halfword");

            case MMI2_PEXEH:
                return fmt::format("// PS2_PEXEH - Parallel Exchange Even Halfword");

            case MMI2_PREVH:
                return fmt::format("// PS2_PREVH - Parallel Reverse Halfword");

            case MMI2_PMULTH:
                return fmt::format("// PS2_PMULTH - Packed Multiply Halfword");

            case MMI2_PDIVBW:
                return fmt::format("// PS2_PDIVBW - Packed Divide Broadcast Word");

            case MMI2_PEXEW:
                return fmt::format("// PS2_PEXEW - Parallel Exchange Even Word");

            case MMI2_PROT3W:
                return fmt::format("// PS2_PROT3W - Parallel Rotate 3 Words");

            default:
                return fmt::format("// Unhandled MMI2 function: 0x{:X}", inst.sa);
            }
            break;

        // MMI3 functions (PMADDUW, PSRAVW, etc.)
        case MMI_MMI3:
            switch (inst.sa)
            {
            case MMI3_POR:
                return fmt::format("ctx->r{} = PS2_POR(ctx->r{}, ctx->r{});",
                                   inst.rd, inst.rs, inst.rt);

            case MMI3_PNOR:
                return fmt::format("ctx->r{} = PS2_PNOR(ctx->r{}, ctx->r{});",
                                   inst.rd, inst.rs, inst.rt);

            case MMI3_PMADDUW:
                return fmt::format("// PS2_PMADDUW - Packed Multiply-Add Unsigned Word");

            case MMI3_PSRAVW:
                return fmt::format("ctx->r{} = PS2_PSRAVW(ctx->r{}, ctx->r{});",
                                   inst.rd, inst.rs, inst.rt);

            case MMI3_PINTEH:
                return fmt::format("ctx->r{} = PS2_PINTEH(ctx->r{}, ctx->r{});",
                                   inst.rd, inst.rs, inst.rt);

            case MMI3_PMULTUW:
                return fmt::format("// PS2_PMULTUW - Packed Multiply Unsigned Word");

            case MMI3_PDIVUW:
                return fmt::format("// PS2_PDIVUW - Packed Divide Unsigned Word");

            case MMI3_PCPYUD:
                return fmt::format("// PS2_PCPYUD - Parallel Copy Upper Doubleword");

            case MMI3_PEXCH:
                return fmt::format("// PS2_PEXCH - Parallel Exchange Center Halfword");

            case MMI3_PCPYH:
                return fmt::format("// PS2_PCPYH - Parallel Copy Halfword");

            case MMI3_PEXCW:
                return fmt::format("// PS2_PEXCW - Parallel Exchange Center Word");

            default:
                return fmt::format("// Unhandled MMI3 function: 0x{:X}", inst.sa);
            }
            break;

        // MMI_PMFHL functions
        case MMI_PMFHL:
            switch (inst.pmfhlVariation)
            {
            case PMFHL_LW:
                return fmt::format("ctx->r{} = PS2_PMFHL_LW(ctx->hi, ctx->lo);",
                                   inst.rd);

            case PMFHL_UW:
                return fmt::format("ctx->r{} = PS2_PMFHL_UW(ctx->hi, ctx->lo);",
                                   inst.rd);

            case PMFHL_SLW:
                return fmt::format("ctx->r{} = PS2_PMFHL_SLW(ctx->hi, ctx->lo);",
                                   inst.rd);

            case PMFHL_LH:
                return fmt::format("ctx->r{} = PS2_PMFHL_LH(ctx->hi, ctx->lo);",
                                   inst.rd);

            case PMFHL_SH:
                return fmt::format("ctx->r{} = PS2_PMFHL_SH(ctx->hi, ctx->lo);",
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
            return fmt::format("ctx->r{} = (__m128i)ctx->vu0_vf{};",
                               inst.rt, inst.rd);

        case COP2_CFC2:
            if (inst.rd == 0)
            {
                return fmt::format("ctx->r{} = ctx->vu0_status;", inst.rt);
            }
            else
            {
                return fmt::format("// Unhandled CFC2 VU control register: {}", inst.rd);
            }

        case COP2_QMTC2:
            return fmt::format("ctx->vu0_vf{} = (__m128)ctx->r{};",
                               inst.rd, inst.rt);

        case COP2_CTC2:
            if (inst.rd == 0)
            {
                return fmt::format("ctx->vu0_status = ctx->r{} & 0xFFFF;", inst.rt);
            }
            else
            {
                return fmt::format("// Unhandled CTC2 VU control register: {}", inst.rd);
            }

        case COP2_BC2:
            return fmt::format("// VU branch instruction not implemented");

        case COP2_CO:
            // VU0 macro instructions
            switch (inst.function)
            {
            case VU0_VADD:
                return fmt::format("ctx->vu0_vf{} = PS2_VADD(ctx->vu0_vf{}, ctx->vu0_vf{});",
                                   inst.rd, inst.rs, inst.rt);

            case VU0_VSUB:
                return fmt::format("ctx->vu0_vf{} = PS2_VSUB(ctx->vu0_vf{}, ctx->vu0_vf{});",
                                   inst.rd, inst.rs, inst.rt);

            case VU0_VMUL:
                return fmt::format("ctx->vu0_vf{} = PS2_VMUL(ctx->vu0_vf{}, ctx->vu0_vf{});",
                                   inst.rd, inst.rs, inst.rt);

            case VU0_VDIV:
                return fmt::format("ctx->vu0_vf{} = PS2_VDIV(ctx->vu0_vf{}, ctx->vu0_vf{});",
                                   inst.rd, inst.rs, inst.rt);

            case VU0_VMULQ:
                return fmt::format("ctx->vu0_vf{} = PS2_VMULQ(ctx->vu0_vf{}, ctx->vu0_q);",
                                   inst.rd, inst.rs);

            default:
                return fmt::format("// Unhandled VU0 macro instruction: 0x{:X}", inst.function);
            }
            break;

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
            return fmt::format("ctx->r{} = *(uint32_t*)&ctx->f{};",
                               ft, fs);
        }
        else if (rs == COP1_MT)
        {
            return fmt::format("*(uint32_t*)&ctx->f{} = ctx->r{};",
                               fs, ft);
        }
        else if (rs == COP1_CF)
        {
            // CFC1 - Move Control From FPU
            if (fs == 31) // FCR31 contains status/control
            {
                return fmt::format("ctx->r{} = ctx->fcr31;", ft);
            }
            else if (fs == 0) // FCR0 is the FPU implementation register
            {
                return fmt::format("ctx->r{} = 0x00000000; // Emulated FPU implementation", ft);
            }
            else
            {
                return fmt::format("ctx->r{} = 0; // Unimplemented FCR{}", ft, fs);
            }
        }
        else if (rs == COP1_CT)
        {
            // CTC1 - Move Control To FPU
            if (fs == 31) // FCR31 contains status/control
            {
                return fmt::format("ctx->fcr31 = ctx->r{} & 0x0183FFFF;", ft); // Apply bit mask for valid bits
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
                return fmt::format("ctx->f{} = FPU_ADD_S(ctx->f{}, ctx->f{});",
                                   fd, fs, ft);

            case 0x01: // SUB.S
                return fmt::format("ctx->f{} = FPU_SUB_S(ctx->f{}, ctx->f{});",
                                   fd, fs, ft);

            case 0x02: // MUL.S
                return fmt::format("ctx->f{} = FPU_MUL_S(ctx->f{}, ctx->f{});",
                                   fd, fs, ft);

            case 0x03: // DIV.S
                return fmt::format("ctx->f{} = FPU_DIV_S(ctx->f{}, ctx->f{});",
                                   fd, fs, ft);

            case 0x04: // SQRT.S
                return fmt::format("ctx->f{} = FPU_SQRT_S(ctx->f{});",
                                   fd, fs);

            case 0x05: // ABS.S
                return fmt::format("ctx->f{} = FPU_ABS_S(ctx->f{});",
                                   fd, fs);

            case 0x06: // MOV.S
                return fmt::format("ctx->f{} = FPU_MOV_S(ctx->f{});",
                                   fd, fs);

            case 0x07: // NEG.S
                return fmt::format("ctx->f{} = FPU_NEG_S(ctx->f{});",
                                   fd, fs);

            case 0x08: // ROUND.L.S
                return fmt::format("*(int64_t*)&ctx->f{} = FPU_ROUND_L_S(ctx->f{});",
                                   fd, fs);

            case 0x09: // TRUNC.L.S
                return fmt::format("*(int64_t*)&ctx->f{} = FPU_TRUNC_L_S(ctx->f{});",
                                   fd, fs);

            case 0x0A: // CEIL.L.S
                return fmt::format("*(int64_t*)&ctx->f{} = FPU_CEIL_L_S(ctx->f{});",
                                   fd, fs);

            case 0x0B: // FLOOR.L.S
                return fmt::format("*(int64_t*)&ctx->f{} = FPU_FLOOR_L_S(ctx->f{});",
                                   fd, fs);

            case 0x0C: // ROUND.W.S
                return fmt::format("*(int32_t*)&ctx->f{} = FPU_ROUND_W_S(ctx->f{});",
                                   fd, fs);

            case 0x0D: // TRUNC.W.S
                return fmt::format("*(int32_t*)&ctx->f{} = FPU_TRUNC_W_S(ctx->f{});",
                                   fd, fs);

            case 0x0E: // CEIL.W.S
                return fmt::format("*(int32_t*)&ctx->f{} = FPU_CEIL_W_S(ctx->f{});",
                                   fd, fs);

            case 0x0F: // FLOOR.W.S
                return fmt::format("*(int32_t*)&ctx->f{} = FPU_FLOOR_W_S(ctx->f{});",
                                   fd, fs);

                // Continuing the COP1_S switch case from the previous code:

            case 0x21: // CVT.D.S - Convert Single to Double (not commonly used on PS2)
                return fmt::format("// CVT.D.S not implemented (PS2 rarely uses double precision)");

            case 0x24: // CVT.W.S - Convert Single to Word
                return fmt::format("*(int32_t*)&ctx->f{} = FPU_CVT_W_S(ctx->f{});",
                                   fd, fs);

            case 0x25: // CVT.L.S - Convert Single to Long
                return fmt::format("*(int64_t*)&ctx->f{} = FPU_CVT_L_S(ctx->f{});",
                                   fd, fs);

            case 0x30: // C.F.S - Compare False
                return fmt::format("ctx->fcr31 = (ctx->fcr31 & ~0x800000); // Clear condition bit",
                                   fs, ft);

            case 0x31: // C.UN.S - Compare Unordered
                return fmt::format("ctx->fcr31 = (FPU_C_UN_S(ctx->f{}, ctx->f{})) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);",
                                   fs, ft);

            case 0x32: // C.EQ.S - Compare Equal
                return fmt::format("ctx->fcr31 = (FPU_C_EQ_S(ctx->f{}, ctx->f{})) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);",
                                   fs, ft);

            case 0x33: // C.UEQ.S - Compare Unordered or Equal
                return fmt::format("ctx->fcr31 = (FPU_C_UEQ_S(ctx->f{}, ctx->f{})) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);",
                                   fs, ft);

            case 0x34: // C.OLT.S - Compare Ordered Less Than
                return fmt::format("ctx->fcr31 = (FPU_C_OLT_S(ctx->f{}, ctx->f{})) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);",
                                   fs, ft);

            case 0x35: // C.ULT.S - Compare Unordered or Less Than
                return fmt::format("ctx->fcr31 = (FPU_C_ULT_S(ctx->f{}, ctx->f{})) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);",
                                   fs, ft);

            case 0x36: // C.OLE.S - Compare Ordered Less Than or Equal
                return fmt::format("ctx->fcr31 = (FPU_C_OLE_S(ctx->f{}, ctx->f{})) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);",
                                   fs, ft);

            case 0x37: // C.ULE.S - Compare Unordered or Less Than or Equal
                return fmt::format("ctx->fcr31 = (FPU_C_ULE_S(ctx->f{}, ctx->f{})) ? (ctx->fcr31 | 0x800000) : (ctx->fcr31 & ~0x800000);",
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
                return fmt::format("ctx->f{} = FPU_CVT_S_W(*(int32_t*)&ctx->f{});",
                                   fd, fs);

            default:
                return fmt::format("// Unhandled FPU.W instruction: function 0x{:X}", function);
            }
        }

        return fmt::format("// Unhandled FPU instruction: format 0x{:X}, function 0x{:X}", rs, function);
    }

    std::string CodeGenerator::translateCOP0Instruction(const Instruction &inst)
    {
        uint32_t rs = inst.rs; // Format field
        uint32_t rt = inst.rt; // GPR register
        uint32_t rd = inst.rd; // COP0 register

        if (rs == COP0_MF)
        {
            // MFC0 - Move From COP0
            switch (rd)
            {
            case 12: // Status register
                return fmt::format("ctx->r{} = ctx->cop0_status;", rt);

            case 13: // Cause register
                return fmt::format("ctx->r{} = ctx->cop0_cause;", rt);

            case 14: // EPC register
                return fmt::format("ctx->r{} = ctx->cop0_epc;", rt);

            default:
                return fmt::format("ctx->r{} = 0; // Unimplemented COP0 register {}", rt, rd);
            }
        }
        else if (rs == COP0_MT)
        {
            // MTC0 - Move To COP0
            switch (rd)
            {
            case 12: // Status register
                return fmt::format("ctx->cop0_status = ctx->r{};", rt);

            case 13: // Cause register
                return fmt::format("ctx->cop0_cause = ctx->r{};", rt);

            case 14: // EPC register
                return fmt::format("ctx->cop0_epc = ctx->r{};", rt);

            default:
                return fmt::format("// MTC0 to register {} ignored", rd);
            }
        }
        else if (rs == COP0_CO)
        {
            // COP0 co-processor operations
            uint32_t function = inst.function;

            switch (function)
            {
            case COP0_CO_ERET:
                return fmt::format("// ERET instruction - Return from exception\n    ctx->pc = ctx->cop0_epc;\n    return;");

            case COP0_CO_TLBR:
                return fmt::format("// TLBR instruction - TLB Read (ignored)");

            case COP0_CO_TLBWI:
                return fmt::format("// TLBWI instruction - TLB Write Indexed (ignored)");

            case COP0_CO_TLBWR:
                return fmt::format("// TLBWR instruction - TLB Write Random (ignored)");

            case COP0_CO_TLBP:
                return fmt::format("// TLBP instruction - TLB Probe (ignored)");

            case COP0_CO_EI:
                return fmt::format("// EI instruction - Enable Interrupts\n    ctx->cop0_status |= 0x1;");

            case COP0_CO_DI:
                return fmt::format("// DI instruction - Disable Interrupts\n    ctx->cop0_status &= ~0x1;");

            default:
                return fmt::format("// Unhandled COP0 CO-OP: 0x{:X}", function);
            }
        }

        return fmt::format("// Unhandled COP0 instruction: format 0x{:X}", rs);
    }

    std::string CodeGenerator::generateJumpTableSwitch(const Instruction &inst, uint32_t tableAddress,
                                                       const std::vector<JumpTableEntry> &entries)
    {
        std::stringstream ss;

        uint32_t indexReg = inst.rs;

        ss << "switch (ctx->r" << indexReg << ") {\n";

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