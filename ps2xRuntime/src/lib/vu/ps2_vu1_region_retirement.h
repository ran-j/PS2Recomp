#ifndef PS2_VU1_REGION_RETIREMENT_H
#define PS2_VU1_REGION_RETIREMENT_H

#include <cstdint>
#include <cstring>
#if defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace ps2_vu_detail {

inline void retireRegionVector(float *destination, const float *value,
                               const uint64_t *latest, uint64_t sequence,
                               uint8_t laneMask, const uint8_t *firstWrite,
                               uint32_t readyOffset)
{
#if defined(__aarch64__)
    const auto expected = vdupq_n_u64(sequence);
    const auto matches = vcombine_u32(vmovn_u64(vceqq_u64(vld1q_u64(latest), expected)),
                                     vmovn_u64(vceqq_u64(vld1q_u64(latest + 2), expected)));
    uint32_t offsets;
    std::memcpy(&offsets, firstWrite, sizeof(offsets));
    const auto expanded = vmovl_u16(vget_low_u16(vmovl_u8(vcreate_u8(offsets))));
    const auto visible = vcgeq_u32(expanded, vdupq_n_u32(readyOffset));
    const uint32x4_t laneBits = {8u, 4u, 2u, 1u};
    const auto lanes = vtstq_u32(vdupq_n_u32(laneMask), laneBits);
    const auto mask = vandq_u32(vandq_u32(matches, visible), lanes);
    vst1q_f32(destination, vbslq_f32(mask, vld1q_f32(value), vld1q_f32(destination)));
#else
    for (unsigned lane = 0; lane < 4; ++lane)
        if ((laneMask & (8u >> lane)) != 0u && latest[lane] == sequence &&
            readyOffset <= firstWrite[lane])
            destination[lane] = value[lane];
#endif
}

}
#endif
