#pragma once
#include "vu_inline.h"
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>

inline float VUExecutor::normalizeResult(float value, uint32_t &laneFlags) const
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

inline void VUExecutor::applyDest(float *dst, const float *result, uint8_t dest)
{
    if (dest & 0x8u)
        dst[0] = result[0];
    if (dest & 0x4u)
        dst[1] = result[1];
    if (dest & 0x2u)
        dst[2] = result[2];
    if (dest & 0x1u)
        dst[3] = result[3];
}

template <class Word>
inline void VUExecutor::normalizeFmacResult(Word instruction, float *result, uint8_t dest, uint8_t laneFlags[4])
{
    for (uint32_t component = 0; component < 4u; ++component)
    {
        laneFlags[component] = 0u;
        if ((dest & ps2vu::laneForComponent(component)) == 0u)
            continue;

        long double exactResult = 0.0L;
        if (calculateFmacExactResult(instruction, component, exactResult))
        {
            laneFlags[component] = normalizeFmacExactResult(result[component], exactResult);
            continue;
        }

        uint32_t flags = 0u;
        result[component] = normalizeResult(result[component], flags);
        laneFlags[component] = static_cast<uint8_t>(flags);
    }
}

template <class Word>
inline bool VUExecutor::calculateFmacExactResult(Word instruction, uint32_t component, long double &result) const
{
    const uint32_t upper = instruction;
    const uint8_t op = static_cast<uint8_t>(upper & 0x3Fu);
    const uint8_t special = op >= 0x3Cu ? static_cast<uint8_t>((upper & 3u) | ((upper >> 4) & 0x7Cu)) : 0xFFu;
    const auto vs = [this](uint32_t lane) { return static_cast<long double>(m_upperOperands.vs[lane]); };

    const auto vt = [this](uint32_t lane) { return static_cast<long double>(m_upperOperands.vt[lane]); };

    const auto acc = [this](uint32_t lane) { return static_cast<long double>(m_upperOperands.acc[lane]); };

    const long double q = m_upperOperands.q;
    const long double i = m_upperOperands.i;

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
            case 0x2Eu: {
                static constexpr uint8_t left[4] = {1u, 2u, 0u, 3u};
                static constexpr uint8_t right[4] = {2u, 0u, 1u, 3u};
                result = component == 3u ? 0.0L : acc(component) - vs(left[component]) * vt(right[component]);
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
        case 0x2Eu: {
            static constexpr uint8_t left[4] = {1u, 2u, 0u, 3u};
            static constexpr uint8_t right[4] = {2u, 0u, 1u, 3u};
            result = component == 3u ? 0.0L : vs(left[component]) * vt(right[component]);
            break;
        }
        default:
            return false;
        }
    }
    return true;
}

inline uint8_t VUExecutor::normalizeFmacExactResult(float &value, long double exactResult) const
{
    const uint64_t exactBits = std::bit_cast<uint64_t>(static_cast<double>(exactResult));
    const bool negative = (exactBits >> 63) != 0;
    const long double magnitude = negative ? -exactResult : exactResult;
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

template <class Word> inline uint32_t VUExecutor::calculateFmacProductSticky(Word instruction, uint8_t dest) const
{
    uint32_t extraSticky = 0u;
    const uint32_t upper = instruction;
    const uint8_t op = static_cast<uint8_t>(upper & 0x3Fu);
    const uint8_t special = op >= 0x3Cu ? static_cast<uint8_t>((upper & 3u) | ((upper >> 4) & 0x7Cu)) : 0xFFu;
    const bool productSum = (op >= 0x08u && op <= 0x0Fu) || op == 0x21u || op == 0x23u || op == 0x25u || op == 0x27u ||
                            op == 0x29u || op == 0x2Du || op == 0x2Eu || (special >= 0x08u && special <= 0x0Fu) ||
                            special == 0x21u || special == 0x23u || special == 0x25u || special == 0x27u ||
                            special == 0x29u || special == 0x2Du;

    if (!productSum)
        return 0u;

    for (uint32_t component = 0; component < 4u; ++component)
    {
        if ((dest & ps2vu::laneForComponent(component)) == 0u)
            continue;

        static constexpr uint8_t crossLeft[4] = {1u, 2u, 0u, 3u};
        static constexpr uint8_t crossRight[4] = {2u, 0u, 1u, 3u};

        const uint8_t leftComponent = op == 0x2Eu ? crossLeft[component] : static_cast<uint8_t>(component);
        const float left = m_upperOperands.vs[leftComponent];
        float right = 0.0f;
        if ((op >= 0x08u && op <= 0x0Fu) || (special >= 0x08u && special <= 0x0Fu))
        {
            right = m_upperOperands.vt[(op >= 0x08u && op <= 0x0Fu ? op : special) & 3u];
        }
        else if (op == 0x21u || op == 0x25u || special == 0x21u || special == 0x25u)
        {
            right = m_upperOperands.q;
        }
        else if (op == 0x23u || op == 0x27u || special == 0x23u || special == 0x27u)
        {
            right = m_upperOperands.i;
        }
        else if (op == 0x2Eu)
        {
            right = m_upperOperands.vt[crossRight[component]];
        }
        else
        {
            right = m_upperOperands.vt[component];
        }

        float product = left * right;
        const long double exactProduct = static_cast<long double>(left) * static_cast<long double>(right);
        const uint8_t productFlags = normalizeFmacExactResult(product, exactProduct);
        extraSticky |= productFlags & 0xFu;
    }
    return extraSticky;
}

inline void VUExecutor::updateFmacFlags(const uint8_t laneFlags[4], uint8_t dest, uint32_t extraSticky)
{
    if (dest == 0u)
        return;

    static constexpr auto flagPlanes = [] {
        std::array<uint16_t, 16> table{};
        for (uint32_t flags = 0; flags < table.size(); ++flags)
            for (uint32_t condition = 0; condition < 4u; ++condition)
                table[flags] |= ((flags >> condition) & 1u) << (condition * 4u);
        return table;
    }();

    const uint32_t x = (dest & 8u) ? laneFlags[0] : 0u;
    const uint32_t y = (dest & 4u) ? laneFlags[1] : 0u;
    const uint32_t z = (dest & 2u) ? laneFlags[2] : 0u;
    const uint32_t w = (dest & 1u) ? laneFlags[3] : 0u;
    const uint32_t mac = (flagPlanes[x] << 3) | (flagPlanes[y] << 2) | (flagPlanes[z] << 1) | flagPlanes[w];
    const uint32_t status = x | y | z | w;

    auto *entry = allocatePipelineEntry<ps2vu::WritebackKind::Flag>(m_flagPipeline, m_flagActive, m_cycle + kFmacLatency);
    if (!entry)
    {
        reportReservedInstruction(true, 0xFFFFFFFFu);
        return;
    }

    entry->issueCycle = m_cycle;
    entry->mac = mac;
    entry->status = status;
    entry->extraSticky = extraSticky;
    entry->writesMac = true;
    entry->writesStatus = true;
}

template <class Word> inline void VUExecutor::applyFmacDest(Word instruction, float *dst, float *result, uint8_t dest)
{
    uint8_t laneFlags[4]{};
    normalizeFmacResult(instruction, result, dest, laneFlags);
    updateFmacFlags(laneFlags, dest, calculateFmacProductSticky(instruction, dest));
    applyDest(dst, result, dest);
}
