#ifndef PS2_VU1_FMAC_NEON_H
#define PS2_VU1_FMAC_NEON_H

#include "ps2_vu1_detail.h"

#if defined(__aarch64__)
#include <arm_neon.h>
#include <cstring>

namespace ps2_vu_detail {

PS2_VU_FORCE_INLINE float32x4_t normalizeFmacOperands(float32x4_t value)
{
    const auto bits = vreinterpretq_u32_f32(value);
    const auto sign = vandq_u32(bits, vdupq_n_u32(0x80000000u));
    const auto exponent = vandq_u32(bits, vdupq_n_u32(0x7f800000u));
    const auto zero = vceqq_u32(exponent, vdupq_n_u32(0u));
    const auto special = vceqq_u32(exponent, vdupq_n_u32(0x7f800000u));
    const auto finite = vbslq_u32(special, vorrq_u32(sign, vdupq_n_u32(0x7f7fffffu)), bits);
    return vreinterpretq_f32_u32(vbslq_u32(zero, sign, finite));
}

template <bool subtract>
PS2_VU_FORCE_INLINE bool tryProductSumVector(
    const float *leftInput, const float *rightInput, const float *accInput,
    uint8_t destination, float *result, uint8_t laneFlags[4], uint32_t &extraSticky)
{
    const auto left = normalizeFmacOperands(vld1q_f32(leftInput));
    const auto right = normalizeFmacOperands(vld1q_f32(rightInput));
    const auto acc = normalizeFmacOperands(vld1q_f32(accInput));
    const auto value = subtract ? vfmsq_f32(acc, left, right) : vfmaq_f32(acc, left, right);
    const auto valueBits = vreinterpretq_u32_f32(value);
    const auto leftBits = vreinterpretq_u32_f32(left);
    const auto rightBits = vreinterpretq_u32_f32(right);
    const auto exponentMask = vdupq_n_u32(0xffu);
    const auto resultExponent = vandq_u32(vshrq_n_u32(valueBits, 23), exponentMask);
    const auto leftExponent = vandq_u32(vshrq_n_u32(leftBits, 23), exponentMask);
    const auto rightExponent = vandq_u32(vshrq_n_u32(rightBits, 23), exponentMask);
    const auto productBound = vaddq_u32(leftExponent, rightExponent);
    const auto zeroProduct = vorrq_u32(vceqq_u32(leftExponent, vdupq_n_u32(0u)),
                                      vceqq_u32(rightExponent, vdupq_n_u32(0u)));
    const auto productSafe = vandq_u32(vcleq_u32(productBound, vdupq_n_u32(379u)),
        vcleq_u32(productBound, vaddq_u32(resultExponent, vdupq_n_u32(146u))));
    const auto resultSafe = vandq_u32(vcgeq_u32(resultExponent, vdupq_n_u32(2u)),
                                     vcleq_u32(resultExponent, vdupq_n_u32(253u)));
    const auto zeroResult = vceqq_u32(vandq_u32(valueBits, vdupq_n_u32(0x7fffffffu)), vdupq_n_u32(0u));
    const uint32x4_t laneBits = {8u, 4u, 2u, 1u};
    const auto active = vtstq_u32(vdupq_n_u32(destination), laneBits);
    const auto safe = vorrq_u32(vandq_u32(resultSafe, vorrq_u32(zeroProduct, productSafe)),
                                vandq_u32(zeroResult, zeroProduct));
    if (vminvq_u32(vorrq_u32(safe, vmvnq_u32(active))) == 0u)
        return false;

    // The accepted exponent bound excludes product overflow. A tiny product
    // is exact zero only when one normalized multiplicand is zero.
    const auto product = vmulq_f32(left, right);
    const auto productMagnitude = vandq_u32(vreinterpretq_u32_f32(product), vdupq_n_u32(0x7fffffffu));
    const auto tiny = vcltq_u32(productMagnitude, vdupq_n_u32(0x00800000u));
    const auto sign = vshlq_n_u32(vshrq_n_u32(veorq_u32(leftBits, rightBits), 31), 1);
    const auto zero = vandq_u32(tiny, vdupq_n_u32(1u));
    const auto underflow = vandq_u32(vbicq_u32(tiny, zeroProduct), vdupq_n_u32(4u));
    const auto sticky = vandq_u32(vorrq_u32(sign, vorrq_u32(zero, underflow)), active);
    const auto foldedSticky = vorr_u32(vget_low_u32(sticky), vget_high_u32(sticky));
    extraSticky = vget_lane_u32(foldedSticky, 0) | vget_lane_u32(foldedSticky, 1);

    const auto current = vandq_u32(vorrq_u32(vshlq_n_u32(vshrq_n_u32(valueBits, 31), 1),
        vandq_u32(zeroResult, vdupq_n_u32(1u))), active);
    const auto packed = vmovn_u16(vcombine_u16(vmovn_u32(current), vdup_n_u16(0u)));
    const uint32_t flags = vget_lane_u32(vreinterpret_u32_u8(packed), 0);
    std::memcpy(laneFlags, &flags, sizeof(flags));
    vst1q_f32(result, value);
    return true;
}

}
#endif

#endif
