#ifndef PS2_VU1_DETAIL_H
#define PS2_VU1_DETAIL_H

#include <cstdint>

#if defined(_MSC_VER)
#define PS2_VU_FORCE_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define PS2_VU_FORCE_INLINE inline __attribute__((always_inline))
#else
#define PS2_VU_FORCE_INLINE inline
#endif

#if defined(__GNUC__) || defined(__clang__)
#define PS2_VU_INLINE_LAMBDA __attribute__((always_inline))
#else
#define PS2_VU_INLINE_LAMBDA
#endif

// Instruction field extraction helpers
inline constexpr uint8_t DEST(uint32_t i) { return (uint8_t)((i >> 21) & 0xF); }
inline constexpr uint8_t FT(uint32_t i) { return (uint8_t)((i >> 16) & 0x1F); }
inline constexpr uint8_t FS(uint32_t i) { return (uint8_t)((i >> 11) & 0x1F); }
inline constexpr uint8_t FD(uint32_t i) { return (uint8_t)((i >> 6) & 0x1F); }
inline constexpr uint8_t BC(uint32_t i) { return (uint8_t)(i & 0x3); }

// Lower instruction field helpers
inline constexpr uint8_t LIT(uint32_t i) { return (uint8_t)((i >> 16) & 0x1F); }
inline constexpr uint8_t LIS(uint32_t i) { return (uint8_t)((i >> 11) & 0x1F); }
inline constexpr uint8_t LID(uint32_t i) { return (uint8_t)((i >> 6) & 0x1F); }
inline constexpr uint8_t VIT(uint32_t i) { return (uint8_t)((i >> 16) & 0xF); }
inline constexpr uint8_t VIS(uint32_t i) { return (uint8_t)((i >> 11) & 0xF); }
inline constexpr uint8_t VID(uint32_t i) { return (uint8_t)((i >> 6) & 0xF); }
inline constexpr int16_t IMM11(uint32_t i) { return (int16_t)(int32_t)((int32_t)(i << 21) >> 21); }
inline constexpr int16_t IMM15(uint32_t i)
{
    uint32_t lo11 = i & 0x7FF;
    uint32_t hi4 = (i >> 21) & 0xF;
    uint32_t raw = (hi4 << 11) | lo11;
    return (int16_t)(int32_t)((int32_t)(raw << 17) >> 17);
}

#endif
