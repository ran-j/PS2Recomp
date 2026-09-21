#ifndef PS2_VU1_UPPER_ENGINE_H
#define PS2_VU1_UPPER_ENGINE_H

#include "ps2_vu1_detail.h"
#include "ps2_vu1_fmac_flags.h"
#include "ps2_vu1_fmac_neon.h"
#include <cmath>
#include <cstring>
#include <limits>

namespace ps2_vu_detail::upper {

// Inputs provide raw FS/FT/ACC lanes, I/Q and the compiled SIMD preference.
// Sink receives VF/ACC values, packed FMAC flags, CLIP bits or a reserved opcode.
// The caller supplies VU rounding; this engine never accesses pipeline state.

inline constexpr uint8_t laneForComponent(uint32_t component)
{
    return static_cast<uint8_t>(8u >> component);
}

PS2_VU_FORCE_INLINE int32_t floatToInt(float value, float scale)
{
    const double scaled = static_cast<double>(value) * static_cast<double>(scale);
    if (scaled >= static_cast<double>(std::numeric_limits<int32_t>::max()))
        return std::numeric_limits<int32_t>::max();
    if (scaled <= static_cast<double>(std::numeric_limits<int32_t>::min()))
        return std::numeric_limits<int32_t>::min();
    return static_cast<int32_t>(scaled);
}

PS2_VU_FORCE_INLINE float normalizeOperand(float value)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    const uint32_t exponent = (bits >> 23) & 0xFFu;
    if (exponent == 0u)
    {
        bits &= 0x80000000u;
    }
    else if (exponent == 0xFFu)
    {
        bits = (bits & 0x80000000u) | 0x7F7FFFFFu;
    }
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

PS2_VU_FORCE_INLINE float normalizeResult(float value, uint32_t &laneFlags)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    const uint32_t sign = bits & 0x80000000u;
    const uint32_t magnitude = bits & 0x7FFFFFFFu;
    const uint32_t exponent = (bits >> 23) & 0xFFu;

    laneFlags = sign != 0u ? 0x2u : 0u;
    if (magnitude == 0u)
    {
        laneFlags |= 0x1u;
    }
    else if (exponent == 0u)
    {
        laneFlags |= 0x5u;
        bits = sign;
    }
    else if (exponent == 0xFFu)
    {
        laneFlags |= 0x8u;
        bits = sign | 0x7F7FFFFFu;
    }

    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

PS2_VU_FORCE_INLINE uint8_t normalizeFmacExactResult(float &value, long double exactResult)
{
    const bool negative = std::signbit(exactResult);
    const long double magnitude = std::fabs(exactResult);
    const long double maximum = static_cast<long double>(std::numeric_limits<float>::max());
    const long double minimum = static_cast<long double>(std::numeric_limits<float>::min());
    uint8_t flags = negative ? 0x2u : 0u;

    uint32_t bits = negative ? 0x80000000u : 0u;
    if (magnitude == 0.0L)
    {
        flags |= 0x1u;
        std::memcpy(&value, &bits, sizeof(value));
    }
    else if (magnitude > maximum)
    {
        flags |= 0x8u;
        bits |= 0x7F7FFFFFu;
        std::memcpy(&value, &bits, sizeof(value));
    }
    else if (magnitude < minimum)
    {
        flags |= 0x5u;
        std::memcpy(&value, &bits, sizeof(value));
    }

    return flags;
}

template <class Inputs>
PS2_VU_FORCE_INLINE bool calculateFmacExactResult(uint32_t instr, const Inputs &in, uint32_t component, long double &result)
{
    const uint32_t upper = instr;
    const uint8_t op = static_cast<uint8_t>(upper & 0x3Fu);
    const uint8_t special = op >= 0x3Cu
                                ? static_cast<uint8_t>((upper & 3u) | ((upper >> 4) & 0x7Cu))
                                : 0xFFu;

    const auto operand = [&](float value) PS2_VU_INLINE_LAMBDA
    {
        return static_cast<long double>(normalizeOperand(value));
    };
    const auto vs = [&](uint32_t lane) PS2_VU_INLINE_LAMBDA
    {
        return operand(in.fs(lane));
    };
    const auto vt = [&](uint32_t lane) PS2_VU_INLINE_LAMBDA
    {
        return operand(in.ft(lane));
    };
    const auto acc = [&](uint32_t lane) PS2_VU_INLINE_LAMBDA
    {
        return operand(in.acc(lane));
    };

    const long double q = operand(in.q());
    const long double i = operand(in.i());

    if (op < 0x3Cu)
    {
        if (op <= 0x03u)
            result = vs(component) + vt(op & 3u);
        else if (op <= 0x07u)
            result = vs(component) - vt(op & 3u);
        else if (op <= 0x0Bu)
            result = acc(component) + vs(component) * vt(op & 3u);
        else if (op <= 0x0Fu)
            result = acc(component) - vs(component) * vt(op & 3u);
        else if (op >= 0x18u && op <= 0x1Bu)
            result = vs(component) * vt(op & 3u);
        else
        {
            switch (op)
            {
            case 0x1Cu:
                result = vs(component) * q;
                break;
            case 0x1Eu:
                result = vs(component) * i;
                break;
            case 0x20u:
                result = vs(component) + q;
                break;
            case 0x21u:
                result = acc(component) + vs(component) * q;
                break;
            case 0x22u:
                result = vs(component) + i;
                break;
            case 0x23u:
                result = acc(component) + vs(component) * i;
                break;
            case 0x24u:
                result = vs(component) - q;
                break;
            case 0x25u:
                result = acc(component) - vs(component) * q;
                break;
            case 0x26u:
                result = vs(component) - i;
                break;
            case 0x27u:
                result = acc(component) - vs(component) * i;
                break;
            case 0x28u:
                result = vs(component) + vt(component);
                break;
            case 0x29u:
                result = acc(component) + vs(component) * vt(component);
                break;
            case 0x2Au:
                result = vs(component) * vt(component);
                break;
            case 0x2Cu:
                result = vs(component) - vt(component);
                break;
            case 0x2Du:
                result = acc(component) - vs(component) * vt(component);
                break;
            case 0x2Eu:
            {
                static constexpr uint8_t left[4] = {1u, 2u, 0u, 3u};
                static constexpr uint8_t right[4] = {2u, 0u, 1u, 3u};
                result = component == 3u
                             ? 0.0L
                             : acc(component) - vs(left[component]) * vt(right[component]);
                break;
            }
            default:
                return false;
            }
        }
        return true;
    }

    if (special <= 0x03u)
        result = vs(component) + vt(special & 3u);
    else if (special <= 0x07u)
        result = vs(component) - vt(special & 3u);
    else if (special <= 0x0Bu)
        result = acc(component) + vs(component) * vt(special & 3u);
    else if (special <= 0x0Fu)
        result = acc(component) - vs(component) * vt(special & 3u);
    else if (special >= 0x18u && special <= 0x1Bu)
        result = vs(component) * vt(special & 3u);
    else
    {
        switch (special)
        {
        case 0x1Cu:
            result = vs(component) * q;
            break;
        case 0x1Eu:
            result = vs(component) * i;
            break;
        case 0x20u:
            result = vs(component) + q;
            break;
        case 0x21u:
            result = acc(component) + vs(component) * q;
            break;
        case 0x22u:
            result = vs(component) + i;
            break;
        case 0x23u:
            result = acc(component) + vs(component) * i;
            break;
        case 0x24u:
            result = vs(component) - q;
            break;
        case 0x25u:
            result = acc(component) - vs(component) * q;
            break;
        case 0x26u:
            result = vs(component) - i;
            break;
        case 0x27u:
            result = acc(component) - vs(component) * i;
            break;
        case 0x28u:
            result = vs(component) + vt(component);
            break;
        case 0x29u:
            result = acc(component) + vs(component) * vt(component);
            break;
        case 0x2Au:
            result = vs(component) * vt(component);
            break;
        case 0x2Cu:
            result = vs(component) - vt(component);
            break;
        case 0x2Du:
            result = acc(component) - vs(component) * vt(component);
            break;
        case 0x2Eu:
        {
            static constexpr uint8_t left[4] = {1u, 2u, 0u, 3u};
            static constexpr uint8_t right[4] = {2u, 0u, 1u, 3u};
            result = component == 3u
                         ? 0.0L
                         : vs(left[component]) * vt(right[component]);
            break;
        }
        default:
            return false;
        }
    }
    return true;
}

template <class Inputs>
PS2_VU_FORCE_INLINE void normalizeFmacResult(uint32_t instr, const Inputs &in, float *result, uint8_t dest, uint8_t laneFlags[4])
{
    const uint32_t upper = instr;
    const uint32_t op = (upper & 0x3fu) < 0x3cu
        ? upper & 0x3fu : (upper & 3u) | ((upper >> 4u) & 0x7cu);
    const bool simpleArithmetic = op <= 7u || (op >= 0x18u && op <= 0x1cu) ||
        op == 0x1eu || op == 0x20u || op == 0x22u || op == 0x24u ||
        op == 0x26u || op == 0x28u || op == 0x2au || op == 0x2cu;
    for (uint32_t component = 0; component < 4u; ++component)
    {
        laneFlags[component] = 0u;
        if ((dest & laneForComponent(component)) == 0u)
            continue;

        // A normal ADD/SUB/MUL result has only a sign flag. Keep exact checks
        // at both range boundaries, including overflow rounded to FLT_MAX.
        uint32_t bits;
        std::memcpy(&bits, &result[component], sizeof(bits));
        const uint32_t magnitude = bits & 0x7fffffffu;
        if (simpleArithmetic && magnitude >= 0x00800000u && magnitude < 0x7f7fffffu)
        {
            laneFlags[component] = static_cast<uint8_t>((bits >> 30u) & 2u);
            continue;
        }

        long double exactResult = 0.0L;
        if (calculateFmacExactResult(instr, in, component, exactResult))
        {
            laneFlags[component] = normalizeFmacExactResult(result[component], exactResult);
            continue;
        }

        uint32_t flags = 0u;
        result[component] = normalizeResult(result[component], flags);
        laneFlags[component] = static_cast<uint8_t>(flags);
    }
}

template <class Inputs>
PS2_VU_FORCE_INLINE uint32_t calculateFmacProductSticky(uint32_t instr, const Inputs &in, uint8_t dest)
{
    uint32_t extraSticky = 0u;
    const uint32_t upper = instr;
    const uint8_t op = static_cast<uint8_t>(upper & 0x3Fu);
    const uint8_t special = op >= 0x3Cu ? static_cast<uint8_t>((upper & 3u) | ((upper >> 4) & 0x7Cu)) : 0xFFu;
    const bool productSum =
        (op >= 0x08u && op <= 0x0Fu) ||
        op == 0x21u || op == 0x23u || op == 0x25u || op == 0x27u ||
        op == 0x29u || op == 0x2Du || op == 0x2Eu ||
        (special >= 0x08u && special <= 0x0Fu) ||
        special == 0x21u || special == 0x23u || special == 0x25u ||
        special == 0x27u || special == 0x29u || special == 0x2Du;
    if (!productSum)
        return 0u;

    for (uint32_t component = 0; component < 4u; ++component)
    {
        if ((dest & laneForComponent(component)) == 0u)
            continue;
        static constexpr uint8_t crossLeft[4] = {1u, 2u, 0u, 3u};
        static constexpr uint8_t crossRight[4] = {2u, 0u, 1u, 3u};
        const uint8_t leftComponent = op == 0x2Eu ? crossLeft[component] : static_cast<uint8_t>(component);
        const float left = normalizeOperand(in.fs(leftComponent));
        float right = 0.0f;
        if ((op >= 0x08u && op <= 0x0Fu) || (special >= 0x08u && special <= 0x0Fu))
        {
            right = normalizeOperand(in.ft((op >= 0x08u && op <= 0x0Fu ? op : special) & 3u));
        }
        else if (op == 0x21u || op == 0x25u || special == 0x21u || special == 0x25u)
        {
            right = normalizeOperand(in.q());
        }
        else if (op == 0x23u || op == 0x27u || special == 0x23u || special == 0x27u)
        {
            right = normalizeOperand(in.i());
        }
        else if (op == 0x2Eu)
        {
            right = normalizeOperand(in.ft(crossRight[component]));
        }
        else
        {
            right = normalizeOperand(in.ft(component));
        }

        float product = left * right;
        uint32_t productBits;
        std::memcpy(&productBits, &product, sizeof(productBits));
        const uint32_t magnitude = productBits & 0x7fffffffu;
        if (magnitude >= 0x00800000u && magnitude < 0x7f7fffffu)
        {
            extraSticky |= (productBits >> 30u) & 2u;
            continue;
        }
        const long double exactProduct = static_cast<long double>(left) * static_cast<long double>(right);
        const uint8_t productFlags = normalizeFmacExactResult(product, exactProduct);
        // Product-sum instructions report Z/S/U/O from the add/subtract result
        // as current flags, while every product condition accumulates into the
        // corresponding sticky flag.
        extraSticky |= productFlags & 0xFu;
    }
    return extraSticky;
}

template <class Sink>
PS2_VU_FORCE_INLINE void publishFmacFlags(Sink &sink, const uint8_t laneFlags[4], uint8_t dest, uint32_t extraSticky)
{
    if (dest == 0u)
        return;

    const auto packed = ps2_vu_detail::packFmacFlags(laneFlags, dest);
    sink.fmac(packed.mac, packed.status, extraSticky);
}

// Inputs expose raw operands. Compute flags before publishing values so
// destination aliases cannot change operands used by exact checks.
template <class Inputs, class Sink>
PS2_VU_FORCE_INLINE void computeUpper(uint32_t instr, const Inputs &in, Sink &sink)
{
    uint8_t dest = DEST(instr);
    uint8_t ft = FT(instr);
    uint8_t fd = FD(instr);
    uint8_t op = instr & 0x3F;

    // Lower-pipeline work often pairs with an upper NOP; it has no operands.
    if (op >= 0x3Cu)
    {
        const uint8_t special = (instr & 3u) | ((instr >> 4) & 0x7Cu);
        if (special == 0x2Fu || special == 0x30u)
            return;
    }

    float result[4];
#if defined(__aarch64__)
    const uint8_t arithmeticOp = op < 0x3cu ? op : (instr & 3u) | ((instr >> 4u) & 0x7cu);
    const bool simpleAdd = arithmeticOp <= 3u || arithmeticOp == 0x20u || arithmeticOp == 0x22u || arithmeticOp == 0x28u;
    const bool simpleSub = (arithmeticOp >= 4u && arithmeticOp <= 7u) || arithmeticOp == 0x24u || arithmeticOp == 0x26u || arithmeticOp == 0x2cu;
    const bool simpleMul = (arithmeticOp >= 0x18u && arithmeticOp <= 0x1cu) || arithmeticOp == 0x1eu || arithmeticOp == 0x2au;
    if (in.compiledFastPath() && (simpleAdd || simpleSub || simpleMul))
    {
        float left[4], right[4];
        for (unsigned lane = 0u; lane < 4u; ++lane)
        {
            left[lane] = in.fs(lane);
            if (arithmeticOp <= 7u || (arithmeticOp >= 0x18u && arithmeticOp <= 0x1bu))
                right[lane] = in.ft(arithmeticOp & 3u);
            else if (arithmeticOp == 0x1cu || arithmeticOp == 0x20u || arithmeticOp == 0x24u)
                right[lane] = in.q();
            else if (arithmeticOp == 0x1eu || arithmeticOp == 0x22u || arithmeticOp == 0x26u)
                right[lane] = in.i();
            else
                right[lane] = in.ft(lane);
        }
        uint8_t laneFlags[4];
        using Arithmetic = ps2_vu_detail::SimpleArithmetic;
        const bool fast = simpleAdd ? ps2_vu_detail::trySimpleArithmeticVector<Arithmetic::Add>(left, right, dest, result, laneFlags)
            : simpleSub ? ps2_vu_detail::trySimpleArithmeticVector<Arithmetic::Subtract>(left, right, dest, result, laneFlags)
                        : ps2_vu_detail::trySimpleArithmeticVector<Arithmetic::Multiply>(left, right, dest, result, laneFlags);
        if (fast)
        {
            publishFmacFlags(sink, laneFlags, dest, 0u);
            if (op >= 0x3cu)
                sink.acc(dest, result);
            else
                sink.vf(fd, dest, result);
            return;
        }
    }
    const bool productSum = (arithmeticOp >= 8u && arithmeticOp <= 0xfu) ||
        arithmeticOp == 0x21u || arithmeticOp == 0x23u || arithmeticOp == 0x25u ||
        arithmeticOp == 0x27u || arithmeticOp == 0x29u || arithmeticOp == 0x2du;
    // Keep the scalar execution mode independent for differential replays.
    if (in.compiledFastPath() && productSum)
    {
        float leftInput[4], rightInput[4], accInput[4];
        for (unsigned lane = 0; lane < 4u; ++lane)
        {
            leftInput[lane] = in.fs(lane);
            rightInput[lane] = in.ft(lane);
            accInput[lane] = in.acc(lane);
        }
        float rightBroadcast[4];
        const float *right = rightInput;
        if (arithmeticOp <= 0xfu || arithmeticOp == 0x21u || arithmeticOp == 0x25u ||
            arithmeticOp == 0x23u || arithmeticOp == 0x27u)
        {
            const float scalar = arithmeticOp <= 0xfu ? in.ft(arithmeticOp & 3u) :
                (arithmeticOp == 0x21u || arithmeticOp == 0x25u ? in.q() : in.i());
            for (auto &lane : rightBroadcast)
                lane = scalar;
            right = rightBroadcast;
        }
        uint8_t laneFlags[4];
        uint32_t extraSticky;
        const bool subtract = (arithmeticOp >= 0xcu && arithmeticOp <= 0xfu) ||
            arithmeticOp == 0x25u || arithmeticOp == 0x27u || arithmeticOp == 0x2du;
        const bool fast = subtract
            ? ps2_vu_detail::tryProductSumVector<true>(leftInput, right, accInput, dest, result, laneFlags, extraSticky)
            : ps2_vu_detail::tryProductSumVector<false>(leftInput, right, accInput, dest, result, laneFlags, extraSticky);
        if (fast)
        {
            publishFmacFlags(sink, laneFlags, dest, extraSticky);
            if (op >= 0x3cu)
                sink.acc(dest, result);
            else
                sink.vf(fd, dest, result);
            return;
        }
    }
#endif
    // Normalize only operands used by this opcode, after dispatch selects it.
    const auto vs = [&](unsigned c) PS2_VU_INLINE_LAMBDA { return normalizeOperand(in.fs(c)); };
    const auto vt = [&](unsigned c) PS2_VU_INLINE_LAMBDA { return normalizeOperand(in.ft(c)); };
    const auto acc = [&](unsigned c) PS2_VU_INLINE_LAMBDA { return normalizeOperand(in.acc(c)); };
    const auto q = [&]() PS2_VU_INLINE_LAMBDA { return normalizeOperand(in.q()); };
    const auto i = [&]() PS2_VU_INLINE_LAMBDA { return normalizeOperand(in.i()); };

    const auto publishFmac = [&](float *values, uint8_t mask) PS2_VU_INLINE_LAMBDA {
        uint8_t laneFlags[4]{};
        normalizeFmacResult(instr, in, values, mask, laneFlags);
        const uint32_t extraSticky = calculateFmacProductSticky(instr, in, mask);
        publishFmacFlags(sink, laneFlags, mask, extraSticky);
    };

    // Upper opcode decoding (bits 5:0 of upper word)
    switch (op)
    {
    case 0x00:
    case 0x01:
    case 0x02:
    case 0x03: // ADDbc
    {
        float bc = vt(op & 3);
        for (int c = 0; c < 4; c++)
            result[c] = vs(c) + bc;
        publishFmac(result, dest);
        sink.vf(fd, dest, result);
        return;
    }
    case 0x04:
    case 0x05:
    case 0x06:
    case 0x07: // SUBbc
    {
        float bc = vt(op & 3);
        for (int c = 0; c < 4; c++)
            result[c] = vs(c) - bc;
        publishFmac(result, dest);
        sink.vf(fd, dest, result);
        return;
    }
    case 0x08:
    case 0x09:
    case 0x0A:
    case 0x0B: // MADDbc
    {
        float bc = vt(op & 3);
        for (int c = 0; c < 4; c++)
            result[c] = acc(c) + vs(c) * bc;
        publishFmac(result, dest);
        sink.vf(fd, dest, result);
        return;
    }
    case 0x0C:
    case 0x0D:
    case 0x0E:
    case 0x0F: // MSUBbc
    {
        float bc = vt(op & 3);
        for (int c = 0; c < 4; c++)
            result[c] = acc(c) - vs(c) * bc;
        publishFmac(result, dest);
        sink.vf(fd, dest, result);
        return;
    }
    case 0x10:
    case 0x11:
    case 0x12:
    case 0x13: // MAXbc
    {
        float bc = vt(op & 3);
        for (int c = 0; c < 4; c++)
            result[c] = (vs(c) > bc) ? vs(c) : bc;
        sink.vf(fd, dest, result);
        return;
    }
    case 0x14:
    case 0x15:
    case 0x16:
    case 0x17: // MINIbc
    {
        float bc = vt(op & 3);
        for (int c = 0; c < 4; c++)
            result[c] = (vs(c) < bc) ? vs(c) : bc;
        sink.vf(fd, dest, result);
        return;
    }
    case 0x18:
    case 0x19:
    case 0x1A:
    case 0x1B: // MULbc
    {
        float bc = vt(op & 3);
        for (int c = 0; c < 4; c++)
            result[c] = vs(c) * bc;
        publishFmac(result, dest);
        sink.vf(fd, dest, result);
        return;
    }
    case 0x1C: // MULq
        for (int c = 0; c < 4; c++)
            result[c] = vs(c) * q();
        publishFmac(result, dest);
        sink.vf(fd, dest, result);
        return;
    case 0x1D: // MAXi
        for (int c = 0; c < 4; c++)
            result[c] = (vs(c) > i()) ? vs(c) : i();
        sink.vf(fd, dest, result);
        return;
    case 0x1E: // MULi
        for (int c = 0; c < 4; c++)
            result[c] = vs(c) * i();
        publishFmac(result, dest);
        sink.vf(fd, dest, result);
        return;
    case 0x1F: // MINIi
        for (int c = 0; c < 4; c++)
            result[c] = (vs(c) < i()) ? vs(c) : i();
        sink.vf(fd, dest, result);
        return;
    case 0x20: // ADDq
        for (int c = 0; c < 4; c++)
            result[c] = vs(c) + q();
        publishFmac(result, dest);
        sink.vf(fd, dest, result);
        return;
    case 0x21: // MADDq
        for (int c = 0; c < 4; c++)
            result[c] = acc(c) + vs(c) * q();
        publishFmac(result, dest);
        sink.vf(fd, dest, result);
        return;
    case 0x22: // ADDi
        for (int c = 0; c < 4; c++)
            result[c] = vs(c) + i();
        publishFmac(result, dest);
        sink.vf(fd, dest, result);
        return;
    case 0x23: // MADDi
        for (int c = 0; c < 4; c++)
            result[c] = acc(c) + vs(c) * i();
        publishFmac(result, dest);
        sink.vf(fd, dest, result);
        return;
    case 0x24: // SUBq
        for (int c = 0; c < 4; c++)
            result[c] = vs(c) - q();
        publishFmac(result, dest);
        sink.vf(fd, dest, result);
        return;
    case 0x25: // MSUBq
        for (int c = 0; c < 4; c++)
            result[c] = acc(c) - vs(c) * q();
        publishFmac(result, dest);
        sink.vf(fd, dest, result);
        return;
    case 0x26: // SUBi
        for (int c = 0; c < 4; c++)
            result[c] = vs(c) - i();
        publishFmac(result, dest);
        sink.vf(fd, dest, result);
        return;
    case 0x27: // MSUBi
        for (int c = 0; c < 4; c++)
            result[c] = acc(c) - vs(c) * i();
        publishFmac(result, dest);
        sink.vf(fd, dest, result);
        return;
    case 0x28: // ADD
        for (int c = 0; c < 4; c++)
            result[c] = vs(c) + vt(c);
        publishFmac(result, dest);
        sink.vf(fd, dest, result);
        return;
    case 0x29: // MADD
        for (int c = 0; c < 4; c++)
            result[c] = acc(c) + vs(c) * vt(c);
        publishFmac(result, dest);
        sink.vf(fd, dest, result);
        return;
    case 0x2A: // MUL
        for (int c = 0; c < 4; c++)
            result[c] = vs(c) * vt(c);
        publishFmac(result, dest);
        sink.vf(fd, dest, result);
        return;
    case 0x2B: // MAX
        for (int c = 0; c < 4; c++)
            result[c] = (vs(c) > vt(c)) ? vs(c) : vt(c);
        sink.vf(fd, dest, result);
        return;
    case 0x2C: // SUB
        for (int c = 0; c < 4; c++)
            result[c] = vs(c) - vt(c);
        publishFmac(result, dest);
        sink.vf(fd, dest, result);
        return;
    case 0x2D: // MSUB
        for (int c = 0; c < 4; c++)
            result[c] = acc(c) - vs(c) * vt(c);
        publishFmac(result, dest);
        sink.vf(fd, dest, result);
        return;
    case 0x2E: // OPMSUB
        result[0] = acc(0) - vs(1) * vt(2);
        result[1] = acc(1) - vs(2) * vt(0);
        result[2] = acc(2) - vs(0) * vt(1);
        result[3] = 0.0f;
        publishFmac(result, dest);
        sink.vf(fd, dest, result);
        return;
    case 0x2F: // MINI
        for (int c = 0; c < 4; c++)
            result[c] = (vs(c) < vt(c)) ? vs(c) : vt(c);
        sink.vf(fd, dest, result);
        return;

    // Upper special group (low op 0x3C..0x3F).
    // Like lower1 special, the real selector is not just bits 5:0.  Dobie decodes:
    //   op = (instr & 0x3) | ((instr >> 4) & 0x7C)
    // Several instructions in this group also use FT as the destination, not FD.
    case 0x3C:
    case 0x3D:
    case 0x3E:
    case 0x3F:
    {
        const uint8_t specialOp = static_cast<uint8_t>((instr & 0x3u) | ((instr >> 4) & 0x7Cu));

        switch (specialOp)
        {
        case 0x00:
        case 0x01:
        case 0x02:
        case 0x03: // ADDAbc
        {
            float bc = vt(specialOp & 3);
            for (int c = 0; c < 4; c++)
                result[c] = vs(c) + bc;
            publishFmac(result, dest);
            sink.acc(dest, result);
            return;
        }
        case 0x04:
        case 0x05:
        case 0x06:
        case 0x07: // SUBAbc
        {
            float bc = vt(specialOp & 3);
            for (int c = 0; c < 4; c++)
                result[c] = vs(c) - bc;
            publishFmac(result, dest);
            sink.acc(dest, result);
            return;
        }
        case 0x08:
        case 0x09:
        case 0x0A:
        case 0x0B: // MADDAbc
        {
            float bc = vt(specialOp & 3);
            for (int c = 0; c < 4; c++)
                result[c] = acc(c) + vs(c) * bc;
            publishFmac(result, dest);
            sink.acc(dest, result);
            return;
        }
        case 0x0C:
        case 0x0D:
        case 0x0E:
        case 0x0F: // MSUBAbc
        {
            float bc = vt(specialOp & 3);
            for (int c = 0; c < 4; c++)
                result[c] = acc(c) - vs(c) * bc;
            publishFmac(result, dest);
            sink.acc(dest, result);
            return;
        }
        case 0x10: // ITOF0
            for (int c = 0; c < 4; c++)
            {
                int32_t iv;
                const float raw = in.fs(c);
                std::memcpy(&iv, &raw, 4);
                result[c] = static_cast<float>(iv);
            }
            sink.vf(ft, dest, result);
            return;
        case 0x11: // ITOF4
            for (int c = 0; c < 4; c++)
            {
                int32_t iv;
                const float raw = in.fs(c);
                std::memcpy(&iv, &raw, 4);
                result[c] = static_cast<float>(iv) / 16.0f;
            }
            sink.vf(ft, dest, result);
            return;
        case 0x12: // ITOF12
            for (int c = 0; c < 4; c++)
            {
                int32_t iv;
                const float raw = in.fs(c);
                std::memcpy(&iv, &raw, 4);
                result[c] = static_cast<float>(iv) / 4096.0f;
            }
            sink.vf(ft, dest, result);
            return;
        case 0x13: // ITOF15
            for (int c = 0; c < 4; c++)
            {
                int32_t iv;
                const float raw = in.fs(c);
                std::memcpy(&iv, &raw, 4);
                result[c] = static_cast<float>(iv) / 32768.0f;
            }
            sink.vf(ft, dest, result);
            return;
        case 0x14: // FTOI0
            for (int c = 0; c < 4; c++)
            {
                int32_t iv = floatToInt(vs(c), 1.0f);
                std::memcpy(&result[c], &iv, 4);
            }
            sink.vf(ft, dest, result);
            return;
        case 0x15: // FTOI4
            for (int c = 0; c < 4; c++)
            {
                int32_t iv = floatToInt(vs(c), 16.0f);
                std::memcpy(&result[c], &iv, 4);
            }
            sink.vf(ft, dest, result);
            return;
        case 0x16: // FTOI12
            for (int c = 0; c < 4; c++)
            {
                int32_t iv = floatToInt(vs(c), 4096.0f);
                std::memcpy(&result[c], &iv, 4);
            }
            sink.vf(ft, dest, result);
            return;
        case 0x17: // FTOI15
            for (int c = 0; c < 4; c++)
            {
                int32_t iv = floatToInt(vs(c), 32768.0f);
                std::memcpy(&result[c], &iv, 4);
            }
            sink.vf(ft, dest, result);
            return;
        case 0x18:
        case 0x19:
        case 0x1A:
        case 0x1B: // MULAbc
        {
            float bc = vt(specialOp & 3);
            for (int c = 0; c < 4; c++)
                result[c] = vs(c) * bc;
            publishFmac(result, dest);
            sink.acc(dest, result);
            return;
        }
        case 0x1C: // MULAq
            for (int c = 0; c < 4; c++)
                result[c] = vs(c) * q();
            publishFmac(result, dest);
            sink.acc(dest, result);
            return;
        case 0x1D: // ABS
            for (int c = 0; c < 4; c++)
                result[c] = std::fabs(vs(c));
            sink.vf(ft, dest, result);
            return;
        case 0x1E: // MULAi
            for (int c = 0; c < 4; c++)
                result[c] = vs(c) * i();
            publishFmac(result, dest);
            sink.acc(dest, result);
            return;
        case 0x1F: // CLIP
        {
            uint32_t wBits = 0u;
            const float rawW = in.ft(3u);
            std::memcpy(&wBits, &rawW, sizeof(wBits));
            const int32_t limit = (wBits & 0x7F800000u) != 0u ? static_cast<int32_t>(wBits & 0x7FFFFFFFu) : 0x007FFFFF;

            const auto exceedsClipPlane = [limit](float value, uint32_t signMask) PS2_VU_INLINE_LAMBDA
            {
                uint32_t bits = 0u;
                std::memcpy(&bits, &value, sizeof(bits));
                bits ^= signMask;
                int32_t orderedBits = 0;
                std::memcpy(&orderedBits, &bits, sizeof(orderedBits));
                return orderedBits > limit;
            };

            uint32_t flags = 0u;
            if (exceedsClipPlane(in.fs(0), 0x00000000u))
                flags |= 0x01u;
            if (exceedsClipPlane(in.fs(0), 0x80000000u))
                flags |= 0x02u;
            if (exceedsClipPlane(in.fs(1), 0x00000000u))
                flags |= 0x04u;
            if (exceedsClipPlane(in.fs(1), 0x80000000u))
                flags |= 0x08u;
            if (exceedsClipPlane(in.fs(2), 0x00000000u))
                flags |= 0x10u;
            if (exceedsClipPlane(in.fs(2), 0x80000000u))
                flags |= 0x20u;
            sink.clip(flags);
            return;
        }
        case 0x20: // ADDAq
            for (int c = 0; c < 4; c++)
                result[c] = vs(c) + q();
            publishFmac(result, dest);
            sink.acc(dest, result);
            return;
        case 0x21: // MADDAq
            for (int c = 0; c < 4; c++)
                result[c] = acc(c) + vs(c) * q();
            publishFmac(result, dest);
            sink.acc(dest, result);
            return;
        case 0x22: // ADDAi
            for (int c = 0; c < 4; c++)
                result[c] = vs(c) + i();
            publishFmac(result, dest);
            sink.acc(dest, result);
            return;
        case 0x23: // MADDAi
            for (int c = 0; c < 4; c++)
                result[c] = acc(c) + vs(c) * i();
            publishFmac(result, dest);
            sink.acc(dest, result);
            return;
        case 0x24: // SUBAq
            for (int c = 0; c < 4; c++)
                result[c] = vs(c) - q();
            publishFmac(result, dest);
            sink.acc(dest, result);
            return;
        case 0x25: // MSUBAq
            for (int c = 0; c < 4; c++)
                result[c] = acc(c) - vs(c) * q();
            publishFmac(result, dest);
            sink.acc(dest, result);
            return;
        case 0x26: // SUBAi
            for (int c = 0; c < 4; c++)
                result[c] = vs(c) - i();
            publishFmac(result, dest);
            sink.acc(dest, result);
            return;
        case 0x27: // MSUBAi
            for (int c = 0; c < 4; c++)
                result[c] = acc(c) - vs(c) * i();
            publishFmac(result, dest);
            sink.acc(dest, result);
            return;
        case 0x28: // ADDA
            for (int c = 0; c < 4; c++)
                result[c] = vs(c) + vt(c);
            publishFmac(result, dest);
            sink.acc(dest, result);
            return;
        case 0x29: // MADDA
            for (int c = 0; c < 4; c++)
                result[c] = acc(c) + vs(c) * vt(c);
            publishFmac(result, dest);
            sink.acc(dest, result);
            return;
        case 0x2A: // MULA
            for (int c = 0; c < 4; c++)
                result[c] = vs(c) * vt(c);
            publishFmac(result, dest);
            sink.acc(dest, result);
            return;
        case 0x2C: // SUBA
            for (int c = 0; c < 4; c++)
                result[c] = vs(c) - vt(c);
            publishFmac(result, dest);
            sink.acc(dest, result);
            return;
        case 0x2D: // MSUBA
            for (int c = 0; c < 4; c++)
                result[c] = acc(c) - vs(c) * vt(c);
            publishFmac(result, dest);
            sink.acc(dest, result);
            return;
        case 0x2E: // OPMULA
            result[0] = vs(1) * vt(2);
            result[1] = vs(2) * vt(0);
            result[2] = vs(0) * vt(1);
            result[3] = 0.0f;
            publishFmac(result, dest);
            sink.acc(dest, result);
            return;
        case 0x2F:
        case 0x30: // NOP
            return;
        default:
            sink.reserved(instr);
            return;
        }
    }

    case 0x30:
    case 0x31:
    case 0x32:
    case 0x33:
    default:
        sink.reserved(instr);
        return;
    }
}

} // namespace ps2_vu_detail::upper
#endif
