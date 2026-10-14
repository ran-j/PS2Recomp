#pragma once
#include <cmath>
#include <cstring>
#include <limits>

namespace ps2vu
{
    inline int32_t vuFloatToInt(float value, float scale)
    {
        const double scaled = static_cast<double>(value) * static_cast<double>(scale);
        if (scaled >= static_cast<double>(std::numeric_limits<int32_t>::max()))
            return std::numeric_limits<int32_t>::max();
        if (scaled <= static_cast<double>(std::numeric_limits<int32_t>::min()))
            return std::numeric_limits<int32_t>::min();
        return static_cast<int32_t>(scaled);
    }
}

template <class Word> inline void VUExecutor::execUpper(Word instr, float *vfResult, float *accResult)
{
    using namespace ps2vu;
    uint8_t dest = destMask(instr);
    uint8_t ft = vfTarget(instr);
    uint8_t fs = vfSource(instr);
    uint8_t op = instr & 0x3F;

    const uint8_t special = static_cast<uint8_t>((instr & 3u) | ((instr >> 4) & 0x7Cu));
    if (op >= 0x3Cu && (special == 0x2Fu || special == 0x30u))
        return;

    float *vd = vfResult;
    auto &operands = m_upperOperands;
    for (uint32_t component = 0; component < 4u; ++component)
    {
        operands.vs[component] = normalizeOperand(m_state.vf[fs][component]);
        operands.vt[component] = normalizeOperand(m_state.vf[ft][component]);
        operands.acc[component] = normalizeOperand(m_state.acc[component]);
    }
    const float *vs = operands.vs;
    const float *vt = operands.vt;
    const float *acc = operands.acc;
    const float q = operands.q = normalizeOperand(m_state.q);
    const float i = operands.i = normalizeOperand(m_state.i);
    float result[4];

    switch (op)
    {
    case 0x00:
    case 0x01:
    case 0x02:
    case 0x03: {
        float bc = vt[op & 3];
        for (int c = 0; c < 4; c++)
            result[c] = vs[c] + bc;
        applyFmacDest(instr, vd, result, dest);
        return;
    }
    case 0x04:
    case 0x05:
    case 0x06:
    case 0x07: {
        float bc = vt[op & 3];
        for (int c = 0; c < 4; c++)
            result[c] = vs[c] - bc;
        applyFmacDest(instr, vd, result, dest);
        return;
    }
    case 0x08:
    case 0x09:
    case 0x0A:
    case 0x0B: {
        float bc = vt[op & 3];
        for (int c = 0; c < 4; c++)
            result[c] = acc[c] + vs[c] * bc;
        applyFmacDest(instr, vd, result, dest);
        return;
    }
    case 0x0C:
    case 0x0D:
    case 0x0E:
    case 0x0F: {
        float bc = vt[op & 3];
        for (int c = 0; c < 4; c++)
            result[c] = acc[c] - vs[c] * bc;
        applyFmacDest(instr, vd, result, dest);
        return;
    }
    case 0x10:
    case 0x11:
    case 0x12:
    case 0x13: {
        float bc = vt[op & 3];
        for (int c = 0; c < 4; c++)
            result[c] = (vs[c] > bc) ? vs[c] : bc;
        applyDest(vd, result, dest);
        return;
    }
    case 0x14:
    case 0x15:
    case 0x16:
    case 0x17: {
        float bc = vt[op & 3];
        for (int c = 0; c < 4; c++)
            result[c] = (vs[c] < bc) ? vs[c] : bc;
        applyDest(vd, result, dest);
        return;
    }
    case 0x18:
    case 0x19:
    case 0x1A:
    case 0x1B: {
        float bc = vt[op & 3];
        for (int c = 0; c < 4; c++)
            result[c] = vs[c] * bc;
        applyFmacDest(instr, vd, result, dest);
        return;
    }
    case 0x1C:
        for (int c = 0; c < 4; c++)
            result[c] = vs[c] * q;
        applyFmacDest(instr, vd, result, dest);
        return;
    case 0x1D:
        for (int c = 0; c < 4; c++)
            result[c] = (vs[c] > i) ? vs[c] : i;
        applyDest(vd, result, dest);
        return;
    case 0x1E:
        for (int c = 0; c < 4; c++)
            result[c] = vs[c] * i;
        applyFmacDest(instr, vd, result, dest);
        return;
    case 0x1F:
        for (int c = 0; c < 4; c++)
            result[c] = (vs[c] < i) ? vs[c] : i;
        applyDest(vd, result, dest);
        return;
    case 0x20:
        for (int c = 0; c < 4; c++)
            result[c] = vs[c] + q;
        applyFmacDest(instr, vd, result, dest);
        return;
    case 0x21:
        for (int c = 0; c < 4; c++)
            result[c] = acc[c] + vs[c] * q;
        applyFmacDest(instr, vd, result, dest);
        return;
    case 0x22:
        for (int c = 0; c < 4; c++)
            result[c] = vs[c] + i;
        applyFmacDest(instr, vd, result, dest);
        return;
    case 0x23:
        for (int c = 0; c < 4; c++)
            result[c] = acc[c] + vs[c] * i;
        applyFmacDest(instr, vd, result, dest);
        return;
    case 0x24:
        for (int c = 0; c < 4; c++)
            result[c] = vs[c] - q;
        applyFmacDest(instr, vd, result, dest);
        return;
    case 0x25:
        for (int c = 0; c < 4; c++)
            result[c] = acc[c] - vs[c] * q;
        applyFmacDest(instr, vd, result, dest);
        return;
    case 0x26:
        for (int c = 0; c < 4; c++)
            result[c] = vs[c] - i;
        applyFmacDest(instr, vd, result, dest);
        return;
    case 0x27:
        for (int c = 0; c < 4; c++)
            result[c] = acc[c] - vs[c] * i;
        applyFmacDest(instr, vd, result, dest);
        return;
    case 0x28:
        for (int c = 0; c < 4; c++)
            result[c] = vs[c] + vt[c];
        applyFmacDest(instr, vd, result, dest);
        return;
    case 0x29:
        for (int c = 0; c < 4; c++)
            result[c] = acc[c] + vs[c] * vt[c];
        applyFmacDest(instr, vd, result, dest);
        return;
    case 0x2A:
        for (int c = 0; c < 4; c++)
            result[c] = vs[c] * vt[c];
        applyFmacDest(instr, vd, result, dest);
        return;
    case 0x2B:
        for (int c = 0; c < 4; c++)
            result[c] = (vs[c] > vt[c]) ? vs[c] : vt[c];
        applyDest(vd, result, dest);
        return;
    case 0x2C:
        for (int c = 0; c < 4; c++)
            result[c] = vs[c] - vt[c];
        applyFmacDest(instr, vd, result, dest);
        return;
    case 0x2D:
        for (int c = 0; c < 4; c++)
            result[c] = acc[c] - vs[c] * vt[c];
        applyFmacDest(instr, vd, result, dest);
        return;
    case 0x2E:
        result[0] = acc[0] - vs[1] * vt[2];
        result[1] = acc[1] - vs[2] * vt[0];
        result[2] = acc[2] - vs[0] * vt[1];
        result[3] = 0.0f;
        applyFmacDest(instr, vd, result, dest);
        return;
    case 0x2F:
        for (int c = 0; c < 4; c++)
            result[c] = (vs[c] < vt[c]) ? vs[c] : vt[c];
        applyDest(vd, result, dest);
        return;

    case 0x3C:
    case 0x3D:
    case 0x3E:
    case 0x3F: {
        const uint8_t specialOp = static_cast<uint8_t>((instr & 0x3u) | ((instr >> 4) & 0x7Cu));
        float *vtDest = vfResult;

        switch (specialOp)
        {
        case 0x00:
        case 0x01:
        case 0x02:
        case 0x03: {
            float bc = vt[specialOp & 3];
            for (int c = 0; c < 4; c++)
                result[c] = vs[c] + bc;
            applyFmacDest(instr, accResult, result, dest);
            return;
        }
        case 0x04:
        case 0x05:
        case 0x06:
        case 0x07: {
            float bc = vt[specialOp & 3];
            for (int c = 0; c < 4; c++)
                result[c] = vs[c] - bc;
            applyFmacDest(instr, accResult, result, dest);
            return;
        }
        case 0x08:
        case 0x09:
        case 0x0A:
        case 0x0B: {
            float bc = vt[specialOp & 3];
            for (int c = 0; c < 4; c++)
                result[c] = acc[c] + vs[c] * bc;
            applyFmacDest(instr, accResult, result, dest);
            return;
        }
        case 0x0C:
        case 0x0D:
        case 0x0E:
        case 0x0F: {
            float bc = vt[specialOp & 3];
            for (int c = 0; c < 4; c++)
                result[c] = acc[c] - vs[c] * bc;
            applyFmacDest(instr, accResult, result, dest);
            return;
        }
        case 0x10:
            for (int c = 0; c < 4; c++)
            {
                int32_t iv;
                std::memcpy(&iv, &m_state.vf[fs][c], 4);
                result[c] = static_cast<float>(iv);
            }
            applyDest(vtDest, result, dest);
            return;
        case 0x11:
            for (int c = 0; c < 4; c++)
            {
                int32_t iv;
                std::memcpy(&iv, &m_state.vf[fs][c], 4);
                result[c] = static_cast<float>(iv) / 16.0f;
            }
            applyDest(vtDest, result, dest);
            return;
        case 0x12:
            for (int c = 0; c < 4; c++)
            {
                int32_t iv;
                std::memcpy(&iv, &m_state.vf[fs][c], 4);
                result[c] = static_cast<float>(iv) / 4096.0f;
            }
            applyDest(vtDest, result, dest);
            return;
        case 0x13:
            for (int c = 0; c < 4; c++)
            {
                int32_t iv;
                std::memcpy(&iv, &m_state.vf[fs][c], 4);
                result[c] = static_cast<float>(iv) / 32768.0f;
            }
            applyDest(vtDest, result, dest);
            return;
        case 0x14:
            for (int c = 0; c < 4; c++)
            {
                int32_t iv = vuFloatToInt(vs[c], 1.0f);
                std::memcpy(&result[c], &iv, 4);
            }
            applyDest(vtDest, result, dest);
            return;
        case 0x15:
            for (int c = 0; c < 4; c++)
            {
                int32_t iv = vuFloatToInt(vs[c], 16.0f);
                std::memcpy(&result[c], &iv, 4);
            }
            applyDest(vtDest, result, dest);
            return;
        case 0x16:
            for (int c = 0; c < 4; c++)
            {
                int32_t iv = vuFloatToInt(vs[c], 4096.0f);
                std::memcpy(&result[c], &iv, 4);
            }
            applyDest(vtDest, result, dest);
            return;
        case 0x17:
            for (int c = 0; c < 4; c++)
            {
                int32_t iv = vuFloatToInt(vs[c], 32768.0f);
                std::memcpy(&result[c], &iv, 4);
            }
            applyDest(vtDest, result, dest);
            return;
        case 0x18:
        case 0x19:
        case 0x1A:
        case 0x1B: {
            float bc = vt[specialOp & 3];
            for (int c = 0; c < 4; c++)
                result[c] = vs[c] * bc;
            applyFmacDest(instr, accResult, result, dest);
            return;
        }
        case 0x1C:
            for (int c = 0; c < 4; c++)
                result[c] = vs[c] * q;
            applyFmacDest(instr, accResult, result, dest);
            return;
        case 0x1D:
            for (int c = 0; c < 4; c++)
                result[c] = std::fabs(vs[c]);
            applyDest(vtDest, result, dest);
            return;
        case 0x1E:
            for (int c = 0; c < 4; c++)
                result[c] = vs[c] * i;
            applyFmacDest(instr, accResult, result, dest);
            return;
        case 0x1F: {
            uint32_t wBits = 0u;
            std::memcpy(&wBits, &m_state.vf[ft][3], sizeof(wBits));
            const int32_t limit = (wBits & 0x7F800000u) != 0u ? static_cast<int32_t>(wBits & 0x7FFFFFFFu) : 0x007FFFFF;

            const auto exceedsClipPlane = [limit](float value, uint32_t signMask) {
                uint32_t bits = 0u;
                std::memcpy(&bits, &value, sizeof(bits));
                bits ^= signMask;
                int32_t orderedBits = 0;
                std::memcpy(&orderedBits, &bits, sizeof(orderedBits));
                return orderedBits > limit;
            };

            uint32_t flags = 0u;
            if (exceedsClipPlane(m_state.vf[fs][0], 0x00000000u))
                flags |= 0x01u;
            if (exceedsClipPlane(m_state.vf[fs][0], 0x80000000u))
                flags |= 0x02u;
            if (exceedsClipPlane(m_state.vf[fs][1], 0x00000000u))
                flags |= 0x04u;
            if (exceedsClipPlane(m_state.vf[fs][1], 0x80000000u))
                flags |= 0x08u;
            if (exceedsClipPlane(m_state.vf[fs][2], 0x00000000u))
                flags |= 0x10u;
            if (exceedsClipPlane(m_state.vf[fs][2], 0x80000000u))
                flags |= 0x20u;
            queueClip(flags);
            return;
        }
        case 0x20:
            for (int c = 0; c < 4; c++)
                result[c] = vs[c] + q;
            applyFmacDest(instr, accResult, result, dest);
            return;
        case 0x21:
            for (int c = 0; c < 4; c++)
                result[c] = acc[c] + vs[c] * q;
            applyFmacDest(instr, accResult, result, dest);
            return;
        case 0x22:
            for (int c = 0; c < 4; c++)
                result[c] = vs[c] + i;
            applyFmacDest(instr, accResult, result, dest);
            return;
        case 0x23:
            for (int c = 0; c < 4; c++)
                result[c] = acc[c] + vs[c] * i;
            applyFmacDest(instr, accResult, result, dest);
            return;
        case 0x24:
            for (int c = 0; c < 4; c++)
                result[c] = vs[c] - q;
            applyFmacDest(instr, accResult, result, dest);
            return;
        case 0x25:
            for (int c = 0; c < 4; c++)
                result[c] = acc[c] - vs[c] * q;
            applyFmacDest(instr, accResult, result, dest);
            return;
        case 0x26:
            for (int c = 0; c < 4; c++)
                result[c] = vs[c] - i;
            applyFmacDest(instr, accResult, result, dest);
            return;
        case 0x27:
            for (int c = 0; c < 4; c++)
                result[c] = acc[c] - vs[c] * i;
            applyFmacDest(instr, accResult, result, dest);
            return;
        case 0x28:
            for (int c = 0; c < 4; c++)
                result[c] = vs[c] + vt[c];
            applyFmacDest(instr, accResult, result, dest);
            return;
        case 0x29:
            for (int c = 0; c < 4; c++)
                result[c] = acc[c] + vs[c] * vt[c];
            applyFmacDest(instr, accResult, result, dest);
            return;
        case 0x2A:
            for (int c = 0; c < 4; c++)
                result[c] = vs[c] * vt[c];
            applyFmacDest(instr, accResult, result, dest);
            return;
        case 0x2C:
            for (int c = 0; c < 4; c++)
                result[c] = vs[c] - vt[c];
            applyFmacDest(instr, accResult, result, dest);
            return;
        case 0x2D:
            for (int c = 0; c < 4; c++)
                result[c] = acc[c] - vs[c] * vt[c];
            applyFmacDest(instr, accResult, result, dest);
            return;
        case 0x2E:
            result[0] = vs[1] * vt[2];
            result[1] = vs[2] * vt[0];
            result[2] = vs[0] * vt[1];
            result[3] = 0.0f;
            applyFmacDest(instr, accResult, result, dest);
            return;
        case 0x2F:
        case 0x30:
            return;
        default:
            reportReservedInstruction(true, instr);
            return;
        }
    }

    case 0x30:
    case 0x31:
    case 0x32:
    case 0x33:
    default:
        reportReservedInstruction(true, instr);
        return;
    }
}
