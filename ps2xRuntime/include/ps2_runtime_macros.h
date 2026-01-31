#ifndef PS2_RUNTIME_MACROS_H
#define PS2_RUNTIME_MACROS_H
#include <cstdint>
#include <immintrin.h> // For SSE/AVX intrinsics
#include <intrin.h>
inline uint32_t ps2_clz32(uint32_t val) {
#if defined(_MSC_VER)
    unsigned long idx;
    if (_BitScanReverse(&idx, val)) {
        return 31u - idx;
    }
    return 32u;
#else
    return val == 0 ? 32u : (uint32_t)__builtin_clz(val);
#endif
}

// Basic MIPS arithmetic operations
#define ADD32(a, b) ((uint32_t)((a) + (b)))
#define ADD32_OV(rs, rt, result32, overflow)         \
            do {                                                    \
                int32_t _a = (int32_t)(rs);                         \
                int32_t _b = (int32_t)(rt);                         \
                int32_t _r = _a + _b;                               \
                overflow = (((_a ^ _b) >= 0) && ((_a ^ _r) < 0));   \
                result32 = (uint32_t)_r;                            \
            } while (0);
#define SUB32(a, b) ((uint32_t)((a) - (b)))
#define SUB32_OV(rs, rt, result32, overflow)         \
            do {                                                    \
                int32_t _a = (int32_t)(rs);                         \
                int32_t _b = (int32_t)(rt);                         \
                int32_t _r = _a - _b;                               \
                overflow = (((_a ^ _b) < 0) && ((_a ^ _r) < 0));    \
                result32 = (uint32_t)_r;                            \
            } while (0);
#define MUL32(a, b) ((uint32_t)((a) * (b)))
#define DIV32(a, b) ((uint32_t)((a) / (b)))
#define AND32(a, b) ((uint32_t)((a) & (b)))
#define OR32(a, b) ((uint32_t)((a) | (b)))
#define XOR32(a, b) ((uint32_t)((a) ^ (b)))
#define NOR32(a, b) ((uint32_t)(~((a) | (b))))
#define SLL32(a, b) ((uint32_t)((a) << (b)))
#define SRL32(a, b) ((uint32_t)((a) >> (b)))
#define SRA32(a, b) ((uint32_t)((int32_t)(a) >> (b)))
#define SLT32(a, b) ((uint32_t)((int32_t)(a) < (int32_t)(b) ? 1 : 0))
#define SLTU32(a, b) ((uint32_t)((a) < (b) ? 1 : 0))

// PS2-specific 128-bit MMI operations
#define PS2_PEXTLW(a, b) _mm_unpacklo_epi32((__m128i)(b), (__m128i)(a))
#define PS2_PEXTUW(a, b) _mm_unpackhi_epi32((__m128i)(b), (__m128i)(a))
#define PS2_PEXTLH(a, b) _mm_unpacklo_epi16((__m128i)(b), (__m128i)(a))
#define PS2_PEXTUH(a, b) _mm_unpackhi_epi16((__m128i)(b), (__m128i)(a))
#define PS2_PEXTLB(a, b) _mm_unpacklo_epi8((__m128i)(b), (__m128i)(a))
#define PS2_PEXTUB(a, b) _mm_unpackhi_epi8((__m128i)(b), (__m128i)(a))
#define PS2_PADDW(a, b) _mm_add_epi32((__m128i)(a), (__m128i)(b))
#define PS2_PSUBW(a, b) _mm_sub_epi32((__m128i)(a), (__m128i)(b))
#define PS2_PMAXW(a, b) _mm_max_epi32((__m128i)(a), (__m128i)(b))
#define PS2_PMINW(a, b) _mm_min_epi32((__m128i)(a), (__m128i)(b))
#define PS2_PADDH(a, b) _mm_add_epi16((__m128i)(a), (__m128i)(b))
#define PS2_PSUBH(a, b) _mm_sub_epi16((__m128i)(a), (__m128i)(b))
#define PS2_PMAXH(a, b) _mm_max_epi16((__m128i)(a), (__m128i)(b))
#define PS2_PMINH(a, b) _mm_min_epi16((__m128i)(a), (__m128i)(b))
#define PS2_PADDB(a, b) _mm_add_epi8((__m128i)(a), (__m128i)(b))
#define PS2_PSUBB(a, b) _mm_sub_epi8((__m128i)(a), (__m128i)(b))
#define PS2_PAND(a, b) _mm_and_si128((__m128i)(a), (__m128i)(b))
#define PS2_POR(a, b) _mm_or_si128((__m128i)(a), (__m128i)(b))
#define PS2_PXOR(a, b) _mm_xor_si128((__m128i)(a), (__m128i)(b))
#define PS2_PNOR(a, b) _mm_xor_si128(_mm_or_si128((__m128i)(a), (__m128i)(b)), _mm_set1_epi32(0xFFFFFFFF))

// PS2 VU (Vector Unit) operations
#define PS2_VADD(a, b) _mm_add_ps((__m128)(a), (__m128)(b))
#define PS2_VSUB(a, b) _mm_sub_ps((__m128)(a), (__m128)(b))
#define PS2_VMUL(a, b) _mm_mul_ps((__m128)(a), (__m128)(b))
#define PS2_VDIV(a, b) _mm_div_ps((__m128)(a), (__m128)(b))
#define PS2_VMULQ(a, q) _mm_mul_ps((__m128)(a), _mm_set1_ps(q))

// Memory access helpers
#define READ8(addr) (*(uint8_t*)((rdram) + ((addr) & PS2_RAM_MASK)))
#define READ16(addr) (*(uint16_t*)((rdram) + ((addr) & PS2_RAM_MASK)))
#define READ32(addr) (*(uint32_t*)((rdram) + ((addr) & PS2_RAM_MASK)))
#define READ64(addr) (*(uint64_t*)((rdram) + ((addr) & PS2_RAM_MASK)))
#define READ128(addr) (*((__m128i*)((rdram) + ((addr) & PS2_RAM_MASK))))
#define WRITE8(addr, val) (*(uint8_t*)((rdram) + ((addr) & PS2_RAM_MASK)) = (val))
#define WRITE16(addr, val) (*(uint16_t*)((rdram) + ((addr) & PS2_RAM_MASK)) = (val))
#define WRITE32(addr, val) (*(uint32_t*)((rdram) + ((addr) & PS2_RAM_MASK)) = (val))
#define WRITE64(addr, val) (*(uint64_t*)((rdram) + ((addr) & PS2_RAM_MASK)) = (val))
#define WRITE128(addr, val) (*((__m128i*)((rdram) + ((addr) & PS2_RAM_MASK))) = (val))

// Packed Compare Greater Than (PCGT)
#define PS2_PCGTW(a, b) _mm_cmpgt_epi32((__m128i)(a), (__m128i)(b))
#define PS2_PCGTH(a, b) _mm_cmpgt_epi16((__m128i)(a), (__m128i)(b))
#define PS2_PCGTB(a, b) _mm_cmpgt_epi8((__m128i)(a), (__m128i)(b))

// Packed Compare Equal (PCEQ)
#define PS2_PCEQW(a, b) _mm_cmpeq_epi32((__m128i)(a), (__m128i)(b))
#define PS2_PCEQH(a, b) _mm_cmpeq_epi16((__m128i)(a), (__m128i)(b))
#define PS2_PCEQB(a, b) _mm_cmpeq_epi8((__m128i)(a), (__m128i)(b))

// Packed Absolute (PABS)
#define PS2_PABSW(a) _mm_abs_epi32((__m128i)(a))
#define PS2_PABSH(a) _mm_abs_epi16((__m128i)(a))
#define PS2_PABSB(a) _mm_abs_epi8((__m128i)(a))

// Packed Pack (PPAC) - Packs larger elements into smaller ones
#define PS2_PPACW(a, b) _mm_packs_epi32((__m128i)(b), (__m128i)(a))
#define PS2_PPACH(a, b) _mm_packs_epi16((__m128i)(b), (__m128i)(a))
#define PS2_PPACB(a, b) _mm_packus_epi16(_mm_packs_epi32((__m128i)(b), (__m128i)(a)), _mm_setzero_si128())

// Packed Interleave (PINT)
#define PS2_PINTH(a, b) _mm_unpacklo_epi16(_mm_shuffle_epi32((__m128i)(b), _MM_SHUFFLE(3,2,1,0)), _mm_shuffle_epi32((__m128i)(a), _MM_SHUFFLE(3,2,1,0)))
#define PS2_PINTEH(a, b) _mm_unpackhi_epi16(_mm_shuffle_epi32((__m128i)(b), _MM_SHUFFLE(3,2,1,0)), _mm_shuffle_epi32((__m128i)(a), _MM_SHUFFLE(3,2,1,0)))

// Packed Multiply-Add (PMADD)
#define PS2_PMADDW(a, b) _mm_add_epi32(_mm_mullo_epi32(_mm_shuffle_epi32((__m128i)(a), _MM_SHUFFLE(1,0,3,2)), _mm_shuffle_epi32((__m128i)(b), _MM_SHUFFLE(1,0,3,2))), _mm_mullo_epi32(_mm_shuffle_epi32((__m128i)(a), _MM_SHUFFLE(3,2,1,0)), _mm_shuffle_epi32((__m128i)(b), _MM_SHUFFLE(3,2,1,0))))

// Packed Variable Shifts
#define PS2_PSLLVW(a, b) _mm_custom_sllv_epi32((__m128i)(a), (__m128i)(b))
#define PS2_PSRLVW(a, b) _mm_custom_srlv_epi32((__m128i)(a), (__m128i)(b))
#define PS2_PSRAVW(a, b) _mm_custom_srav_epi32((__m128i)(a), (__m128i)(b))

// Helper function declarations for custom variable shifts
inline __m128i _mm_custom_sllv_epi32(__m128i a, __m128i count) {
    int32_t a_arr[4], count_arr[4], result[4];
    _mm_storeu_si128((__m128i*)a_arr, a);
    _mm_storeu_si128((__m128i*)count_arr, count);
    for (int i = 0; i < 4; i++) {
        result[i] = a_arr[i] << (count_arr[i] & 0x1F);
    }
    return _mm_loadu_si128((__m128i*)result);
}

inline __m128i _mm_custom_srlv_epi32(__m128i a, __m128i count) {
    int32_t a_arr[4], count_arr[4], result[4];
    _mm_storeu_si128((__m128i*)a_arr, a);
    _mm_storeu_si128((__m128i*)count_arr, count);
    for (int i = 0; i < 4; i++) {
        result[i] = (uint32_t)a_arr[i] >> (count_arr[i] & 0x1F);
    }
    return _mm_loadu_si128((__m128i*)result);
}

inline __m128i _mm_custom_srav_epi32(__m128i a, __m128i count) {
    int32_t a_arr[4], count_arr[4], result[4];
    _mm_storeu_si128((__m128i*)a_arr, a);
    _mm_storeu_si128((__m128i*)count_arr, count);
    for (int i = 0; i < 4; i++) {
        result[i] = a_arr[i] >> (count_arr[i] & 0x1F);
    }
    return _mm_loadu_si128((__m128i*)result);
}

// PMFHL function implementations
#define PS2_PMFHL_LW(hi, lo) _mm_unpacklo_epi64(lo, hi)
#define PS2_PMFHL_UW(hi, lo) _mm_unpackhi_epi64(lo, hi)
#define PS2_PMFHL_SLW(hi, lo) _mm_packs_epi32(lo, hi)
#define PS2_PMFHL_LH(hi, lo) _mm_shuffle_epi32(_mm_packs_epi32(lo, hi), _MM_SHUFFLE(3,1,2,0))
#define PS2_PMFHL_SH(hi, lo) _mm_shufflehi_epi16(_mm_shufflelo_epi16(_mm_packs_epi32(lo, hi), _MM_SHUFFLE(3,1,2,0)), _MM_SHUFFLE(3,1,2,0))

// FPU (COP1) operations
#define FPU_ADD_S(a, b) ((float)(a) + (float)(b))
#define FPU_SUB_S(a, b) ((float)(a) - (float)(b))
#define FPU_MUL_S(a, b) ((float)(a) * (float)(b))
#define FPU_DIV_S(a, b) ((float)(a) / (float)(b))
#define FPU_SQRT_S(a) sqrtf((float)(a))
#define FPU_ABS_S(a) fabsf((float)(a))
#define FPU_MOV_S(a) ((float)(a))
#define FPU_NEG_S(a) (-(float)(a))
#define FPU_ROUND_L_S(a) ((int64_t)roundf((float)(a)))
#define FPU_TRUNC_L_S(a) ((int64_t)(float)(a))
#define FPU_CEIL_L_S(a) ((int64_t)ceilf((float)(a)))
#define FPU_FLOOR_L_S(a) ((int64_t)floorf((float)(a)))
#define FPU_ROUND_W_S(a) ((int32_t)roundf((float)(a)))
#define FPU_TRUNC_W_S(a) ((int32_t)(float)(a))
#define FPU_CEIL_W_S(a) ((int32_t)ceilf((float)(a)))
#define FPU_FLOOR_W_S(a) ((int32_t)floorf((float)(a)))
#define FPU_CVT_S_W(a) ((float)(int32_t)(a))
#define FPU_CVT_S_L(a) ((float)(int64_t)(a))
#define FPU_CVT_W_S(a) ((int32_t)(float)(a))
#define FPU_CVT_L_S(a) ((int64_t)(float)(a))
#define FPU_C_F_S(a, b) (0)
#define FPU_C_UN_S(a, b) (isnan((float)(a)) || isnan((float)(b)))
#define FPU_C_EQ_S(a, b) ((float)(a) == (float)(b))
#define FPU_C_UEQ_S(a, b) ((float)(a) == (float)(b) || isnan((float)(a)) || isnan((float)(b)))
#define FPU_C_OLT_S(a, b) ((float)(a) < (float)(b))
#define FPU_C_ULT_S(a, b) ((float)(a) < (float)(b) || isnan((float)(a)) || isnan((float)(b)))
#define FPU_C_OLE_S(a, b) ((float)(a) <= (float)(b))
#define FPU_C_ULE_S(a, b) ((float)(a) <= (float)(b) || isnan((float)(a)) || isnan((float)(b)))
#define FPU_C_SF_S(a, b) (0)
#define FPU_C_NGLE_S(a, b) (isnan((float)(a)) || isnan((float)(b)))
#define FPU_C_SEQ_S(a, b) ((float)(a) == (float)(b))
#define FPU_C_NGL_S(a, b) ((float)(a) == (float)(b) || isnan((float)(a)) || isnan((float)(b)))
#define FPU_C_LT_S(a, b) ((float)(a) < (float)(b))
#define FPU_C_NGE_S(a, b) ((float)(a) < (float)(b) || isnan((float)(a)) || isnan((float)(b)))
#define FPU_C_LE_S(a, b) ((float)(a) <= (float)(b))
#define FPU_C_NGT_S(a, b) ((float)(a) <= (float)(b) || isnan((float)(a)) || isnan((float)(b)))

#define PS2_QFSRV(rs, rt, sa) _mm_or_si128(_mm_srl_epi32(rt, _mm_cvtsi32_si128(sa)), _mm_sll_epi32(rs, _mm_cvtsi32_si128(32 - sa)))
#define PS2_PCPYLD(rs, rt) _mm_unpacklo_epi64(rt, rs)
#define PS2_PEXEH(rs) _mm_shufflelo_epi16(_mm_shufflehi_epi16(rs, _MM_SHUFFLE(2, 3, 0, 1)), _MM_SHUFFLE(2, 3, 0, 1))
#define PS2_PEXEW(rs) _mm_shuffle_epi32(rs, _MM_SHUFFLE(2, 3, 0, 1))
#define PS2_PROT3W(rs) _mm_shuffle_epi32(rs, _MM_SHUFFLE(0, 3, 2, 1))

// Additional VU0 operations
#define PS2_VSQRT(x) sqrtf(x)
#define PS2_VRSQRT(x) (1.0f / sqrtf(x))
#define PS2_VCALLMS(addr) // VU0 microprogram calls not supported directly
#define PS2_VCALLMSR(reg) // VU0 microprogram calls not supported directly

#define GPR_U32(ctx_ptr, reg_idx) ((reg_idx == 0) ? 0U : ctx_ptr->r[reg_idx].m128i_u32[0])
#define GPR_S32(ctx_ptr, reg_idx) ((reg_idx == 0) ? 0 : ctx_ptr->r[reg_idx].m128i_i32[0])
#define GPR_U64(ctx_ptr, reg_idx) ((reg_idx == 0) ? 0ULL : ctx_ptr->r[reg_idx].m128i_u64[0])
#define GPR_S64(ctx_ptr, reg_idx) ((reg_idx == 0) ? 0LL : ctx_ptr->r[reg_idx].m128i_i64[0])
#define GPR_VEC(ctx_ptr, reg_idx) ((reg_idx == 0) ? _mm_setzero_si128() : ctx_ptr->r[reg_idx])

#define SET_GPR_U32(ctx_ptr, reg_idx, val) \
    do                                     \
    {                                      \
        if (reg_idx != 0)                  \
            ctx_ptr->r[reg_idx] = _mm_set_epi32(0, 0, 0, (val)); \
    } while (0)

#define SET_GPR_S32(ctx_ptr, reg_idx, val) \
    do                                     \
    {                                      \
        if (reg_idx != 0)                  \
            ctx_ptr->r[reg_idx] = _mm_set_epi32(0, 0, 0, (val)); \
    } while (0)

#define SET_GPR_U64(ctx_ptr, reg_idx, val) \
    do                                     \
    {                                      \
        if (reg_idx != 0)                  \
            ctx_ptr->r[reg_idx] = _mm_set_epi64x(0, (val)); \
    } while (0)

#define SET_GPR_S64(ctx_ptr, reg_idx, val) \
    do                                     \
    {                                      \
        if (reg_idx != 0)                  \
            ctx_ptr->r[reg_idx] = _mm_set_epi64x(0, (val)); \
    } while (0)

#define SET_GPR_VEC(ctx_ptr, reg_idx, val) \
    do                                     \
    {                                      \
        if (reg_idx != 0)                  \
            ctx_ptr->r[reg_idx] = (val); \
    } while (0)

#endif // PS2_RUNTIME_MACROS_H
