#ifndef PS2_VU1_FMAC_H
#define PS2_VU1_FMAC_H

#include "ps2_vu1_detail.h"

#include <bit>
#include <cstdint>

namespace ps2_vu_detail {

PS2_VU_FORCE_INLINE bool normalProductSumFlags(float result, float left, float right)
{
    const uint32_t resultExponent = (std::bit_cast<uint32_t>(result) >> 23u) & 0xFFu;
    if (resultExponent < 2u || resultExponent > 253u)
        return false;

    const uint32_t leftExponent = (std::bit_cast<uint32_t>(left) >> 23u) & 0xFFu;
    const uint32_t rightExponent = (std::bit_cast<uint32_t>(right) >> 23u) & 0xFFu;
    if (leftExponent == 0u || rightExponent == 0u)
        return true;

    // Inputs are normalized. Keep the product finite and its rounding error
    // below one eighth of the result, leaving room at both normal boundaries.
    const uint32_t productBound = leftExponent + rightExponent;
    return productBound <= 379u && productBound <= resultExponent + 146u;
}

}

#endif
