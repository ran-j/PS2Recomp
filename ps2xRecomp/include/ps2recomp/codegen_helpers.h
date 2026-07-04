#ifndef PS2RECOMP_CODEGEN_HELPERS_H
#define PS2RECOMP_CODEGEN_HELPERS_H

#include <cmath>
#include <cstdint>
#include <string>
#include <fmt/format.h>

namespace ps2recomp::codegen
{
    inline std::string formatFloatLiteral(float value)
    {
        if (!std::isfinite(value))
        {
            return (value < 0.0f) ? "-INFINITY" : "INFINITY";
        }

        std::string literal = fmt::format("{:.9g}", value);
        if (literal.find_first_of(".eE") == std::string::npos)
        {
            literal += ".0";
        }
        literal += 'f';
        return literal;
    }

    inline std::string vuMaskExpr(uint8_t dest_mask)
    {
        return fmt::format("_mm_castsi128_ps(_mm_set_epi32({}, {}, {}, {}))",
                           (dest_mask & 0x1) ? -1 : 0,
                           (dest_mask & 0x2) ? -1 : 0,
                           (dest_mask & 0x4) ? -1 : 0,
                           (dest_mask & 0x8) ? -1 : 0);
    }
}

#endif // PS2RECOMP_CODEGEN_HELPERS_H
