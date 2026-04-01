#include "Common.h"
#include "VU.h"

namespace ps2_stubs
{
void sceVu0ecossin(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceVu0ecossin", rdram, ctx, runtime);
}


namespace
{
    bool readVuVec4f(uint8_t *rdram, uint32_t addr, float (&out)[4])
    {
        const uint8_t *ptr = getConstMemPtr(rdram, addr);
        if (!ptr)
        {
            return false;
        }
        std::memcpy(out, ptr, sizeof(out));
        return true;
    }

    bool writeVuVec4f(uint8_t *rdram, uint32_t addr, const float (&in)[4])
    {
        uint8_t *ptr = getMemPtr(rdram, addr);
        if (!ptr)
        {
            return false;
        }
        std::memcpy(ptr, in, sizeof(in));
        return true;
    }

    bool readVuVec4i(uint8_t *rdram, uint32_t addr, int32_t (&out)[4])
    {
        const uint8_t *ptr = getConstMemPtr(rdram, addr);
        if (!ptr)
        {
            return false;
        }
        std::memcpy(out, ptr, sizeof(out));
        return true;
    }

    bool writeVuVec4i(uint8_t *rdram, uint32_t addr, const int32_t (&in)[4])
    {
        uint8_t *ptr = getMemPtr(rdram, addr);
        if (!ptr)
        {
            return false;
        }
        std::memcpy(ptr, in, sizeof(in));
        return true;
    }
}

void sceVpu0Reset(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    setReturnS32(ctx, 0);
}

void sceVu0AddVector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t dstAddr = getRegU32(ctx, 4);
    const uint32_t lhsAddr = getRegU32(ctx, 5);
    const uint32_t rhsAddr = getRegU32(ctx, 6);
    float lhs[4]{}, rhs[4]{}, out[4]{};
    if (readVuVec4f(rdram, lhsAddr, lhs) && readVuVec4f(rdram, rhsAddr, rhs))
    {
        for (int i = 0; i < 4; ++i)
        {
            out[i] = lhs[i] + rhs[i];
        }
        (void)writeVuVec4f(rdram, dstAddr, out);
    }
    setReturnS32(ctx, 0);
}

void sceVu0ApplyMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceVu0ApplyMatrix", rdram, ctx, runtime);
}

void sceVu0CameraMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceVu0CameraMatrix", rdram, ctx, runtime);
}

void sceVu0ClampVector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceVu0ClampVector", rdram, ctx, runtime);
}

void sceVu0ClipAll(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceVu0ClipAll", rdram, ctx, runtime);
}

void sceVu0ClipScreen(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceVu0ClipScreen", rdram, ctx, runtime);
}

void sceVu0ClipScreen3(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceVu0ClipScreen3", rdram, ctx, runtime);
}

void sceVu0CopyMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceVu0CopyMatrix", rdram, ctx, runtime);
}

void sceVu0CopyVector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceVu0CopyVector", rdram, ctx, runtime);
}

void sceVu0CopyVectorXYZ(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceVu0CopyVectorXYZ", rdram, ctx, runtime);
}

void sceVu0DivVector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceVu0DivVector", rdram, ctx, runtime);
}

void sceVu0DivVectorXYZ(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceVu0DivVectorXYZ", rdram, ctx, runtime);
}

void sceVu0DropShadowMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceVu0DropShadowMatrix", rdram, ctx, runtime);
}

void sceVu0FTOI0Vector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t dstAddr = getRegU32(ctx, 4);
    const uint32_t srcAddr = getRegU32(ctx, 5);
    float src[4]{};
    int32_t out[4]{};
    if (readVuVec4f(rdram, srcAddr, src))
    {
        for (int i = 0; i < 4; ++i)
        {
            out[i] = static_cast<int32_t>(src[i]);
        }
        (void)writeVuVec4i(rdram, dstAddr, out);
    }
    setReturnS32(ctx, 0);
}

void sceVu0FTOI4Vector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t dstAddr = getRegU32(ctx, 4);
    const uint32_t srcAddr = getRegU32(ctx, 5);
    float src[4]{};
    int32_t out[4]{};
    if (readVuVec4f(rdram, srcAddr, src))
    {
        for (int i = 0; i < 4; ++i)
        {
            out[i] = static_cast<int32_t>(src[i] * 16.0f);
        }
        (void)writeVuVec4i(rdram, dstAddr, out);
    }
    setReturnS32(ctx, 0);
}

void sceVu0InnerProduct(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t lhsAddr = getRegU32(ctx, 4);
    const uint32_t rhsAddr = getRegU32(ctx, 5);
    float lhs[4]{}, rhs[4]{};
    float dot = 0.0f;
    if (readVuVec4f(rdram, lhsAddr, lhs) && readVuVec4f(rdram, rhsAddr, rhs))
    {
        dot = (lhs[0] * rhs[0]) + (lhs[1] * rhs[1]) + (lhs[2] * rhs[2]) + (lhs[3] * rhs[3]);
    }

    if (ctx)
    {
        ctx->f[0] = dot;
    }
    uint32_t raw = 0u;
    std::memcpy(&raw, &dot, sizeof(raw));
    setReturnU32(ctx, raw);
}

void sceVu0InterVector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceVu0InterVector", rdram, ctx, runtime);
}

void sceVu0InterVectorXYZ(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceVu0InterVectorXYZ", rdram, ctx, runtime);
}

void sceVu0InversMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceVu0InversMatrix", rdram, ctx, runtime);
}

void sceVu0ITOF0Vector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t dstAddr = getRegU32(ctx, 4);
    const uint32_t srcAddr = getRegU32(ctx, 5);
    int32_t src[4]{};
    float out[4]{};
    if (readVuVec4i(rdram, srcAddr, src))
    {
        for (int i = 0; i < 4; ++i)
        {
            out[i] = static_cast<float>(src[i]);
        }
        (void)writeVuVec4f(rdram, dstAddr, out);
    }
    setReturnS32(ctx, 0);
}

void sceVu0ITOF12Vector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t dstAddr = getRegU32(ctx, 4);
    const uint32_t srcAddr = getRegU32(ctx, 5);
    int32_t src[4]{};
    float out[4]{};
    if (readVuVec4i(rdram, srcAddr, src))
    {
        for (int i = 0; i < 4; ++i)
        {
            out[i] = static_cast<float>(src[i]) / 4096.0f;
        }
        (void)writeVuVec4f(rdram, dstAddr, out);
    }
    setReturnS32(ctx, 0);
}

void sceVu0ITOF4Vector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t dstAddr = getRegU32(ctx, 4);
    const uint32_t srcAddr = getRegU32(ctx, 5);
    int32_t src[4]{};
    float out[4]{};
    if (readVuVec4i(rdram, srcAddr, src))
    {
        for (int i = 0; i < 4; ++i)
        {
            out[i] = static_cast<float>(src[i]) / 16.0f;
        }
        (void)writeVuVec4f(rdram, dstAddr, out);
    }
    setReturnS32(ctx, 0);
}

void sceVu0LightColorMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceVu0LightColorMatrix", rdram, ctx, runtime);
}

void sceVu0MulMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceVu0MulMatrix", rdram, ctx, runtime);
}

void sceVu0MulVector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceVu0MulVector", rdram, ctx, runtime);
}

void sceVu0Normalize(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t dstAddr = getRegU32(ctx, 4);
    const uint32_t srcAddr = getRegU32(ctx, 5);
    float src[4]{}, out[4]{};
    if (readVuVec4f(rdram, srcAddr, src))
    {
        const float len = std::sqrt((src[0] * src[0]) + (src[1] * src[1]) + (src[2] * src[2]) + (src[3] * src[3]));
        if (len > 1.0e-6f)
        {
            const float invLen = 1.0f / len;
            for (int i = 0; i < 4; ++i)
            {
                out[i] = src[i] * invLen;
            }
        }
        (void)writeVuVec4f(rdram, dstAddr, out);
    }
    setReturnS32(ctx, 0);
}

void sceVu0NormalLightMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceVu0NormalLightMatrix", rdram, ctx, runtime);
}

void sceVu0OuterProduct(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t dstAddr = getRegU32(ctx, 4);
    const uint32_t lhsAddr = getRegU32(ctx, 5);
    const uint32_t rhsAddr = getRegU32(ctx, 6);
    float lhs[4]{}, rhs[4]{}, out[4]{};
    if (readVuVec4f(rdram, lhsAddr, lhs) && readVuVec4f(rdram, rhsAddr, rhs))
    {
        out[0] = (lhs[1] * rhs[2]) - (lhs[2] * rhs[1]);
        out[1] = (lhs[2] * rhs[0]) - (lhs[0] * rhs[2]);
        out[2] = (lhs[0] * rhs[1]) - (lhs[1] * rhs[0]);
        out[3] = 0.0f;
        (void)writeVuVec4f(rdram, dstAddr, out);
    }
    setReturnS32(ctx, 0);
}

void sceVu0RotMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceVu0RotMatrix", rdram, ctx, runtime);
}

void sceVu0RotMatrixX(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceVu0RotMatrixX", rdram, ctx, runtime);
}

void sceVu0RotMatrixY(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceVu0RotMatrixY", rdram, ctx, runtime);
}

void sceVu0RotMatrixZ(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceVu0RotMatrixZ", rdram, ctx, runtime);
}

void sceVu0RotTransPers(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceVu0RotTransPers", rdram, ctx, runtime);
}

void sceVu0RotTransPersN(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceVu0RotTransPersN", rdram, ctx, runtime);
}

void sceVu0ScaleVector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t dstAddr = getRegU32(ctx, 4);
    const uint32_t srcAddr = getRegU32(ctx, 5);
    float src[4]{}, out[4]{};
    float scale = ctx ? ctx->f[12] : 0.0f;
    if (scale == 0.0f)
    {
        uint32_t raw = getRegU32(ctx, 6);
        std::memcpy(&scale, &raw, sizeof(scale));
        if (scale == 0.0f)
        {
            scale = static_cast<float>(getRegU32(ctx, 6));
        }
    }

    if (readVuVec4f(rdram, srcAddr, src))
    {
        for (int i = 0; i < 4; ++i)
        {
            out[i] = src[i] * scale;
        }
        (void)writeVuVec4f(rdram, dstAddr, out);
    }
    setReturnS32(ctx, 0);
}

void sceVu0ScaleVectorXYZ(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceVu0ScaleVectorXYZ", rdram, ctx, runtime);
}

void sceVu0SubVector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t dstAddr = getRegU32(ctx, 4);
    const uint32_t lhsAddr = getRegU32(ctx, 5);
    const uint32_t rhsAddr = getRegU32(ctx, 6);
    float lhs[4]{}, rhs[4]{}, out[4]{};
    if (readVuVec4f(rdram, lhsAddr, lhs) && readVuVec4f(rdram, rhsAddr, rhs))
    {
        for (int i = 0; i < 4; ++i)
        {
            out[i] = lhs[i] - rhs[i];
        }
        (void)writeVuVec4f(rdram, dstAddr, out);
    }
    setReturnS32(ctx, 0);
}

void sceVu0TransMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceVu0TransMatrix", rdram, ctx, runtime);
}

void sceVu0TransposeMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceVu0TransposeMatrix", rdram, ctx, runtime);
}

void sceVu0UnitMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t dstAddr = getRegU32(ctx, 4); // sceVu0FMATRIX dst
    alignas(16) const float identity[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f};

    if (!writeGuestBytes(rdram, runtime, dstAddr, reinterpret_cast<const uint8_t *>(identity), sizeof(identity)))
    {
        static uint32_t warnCount = 0;
        if (warnCount < 8)
        {
            std::cerr << "sceVu0UnitMatrix: failed to write matrix at 0x"
                      << std::hex << dstAddr << std::dec << std::endl;
            ++warnCount;
        }
    }

    setReturnS32(ctx, 0);
}

void sceVu0ViewScreenMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    TODO_NAMED("sceVu0ViewScreenMatrix", rdram, ctx, runtime);
}
}
