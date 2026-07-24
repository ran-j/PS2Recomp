#include "Common.h"
#include "VU.h"

namespace ps2_stubs
{

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

        bool readVuMatrix4f(uint8_t *rdram, uint32_t addr, float (&out)[16])
        {
            const uint8_t *ptr = getConstMemPtr(rdram, addr);
            if (!ptr)
            {
                return false;
            }
            std::memcpy(out, ptr, sizeof(out));
            return true;
        }

        bool writeVuMatrix4f(uint8_t *rdram, uint32_t addr, const float (&in)[16])
        {
            uint8_t *ptr = getMemPtr(rdram, addr);
            if (!ptr)
            {
                return false;
            }
            std::memcpy(ptr, in, sizeof(in));
            return true;
        }

        // Row-major matrix product: out = lhs * rhs (lhs is the left factor).
        void mulVuMatrix(const float (&lhs)[16], const float (&rhs)[16], float (&out)[16])
        {
            std::fill(std::begin(out), std::end(out), 0.0f);
            for (int i = 0; i < 4; ++i)
            {
                for (int j = 0; j < 4; ++j)
                {
                    for (int k = 0; k < 4; ++k)
                    {
                        out[4 * i + j] += rhs[4 * k + j] * lhs[4 * i + k];
                    }
                }
            }
        }

        void makeIdentityMatrix(float (&out)[16])
        {
            std::fill(std::begin(out), std::end(out), 0.0f);
            out[0] = 1.0f;
            out[5] = 1.0f;
            out[10] = 1.0f;
            out[15] = 1.0f;
        }

        // Rigid-transform inverse under the file's row-vector convention
        // (translation in row 3, ApplyMatrix computes v*M): transpose the 3x3
        // rotation block, zero its 4th column, and set the translation row to
        // -t*R^T -- each lane dots t with a ROW of R: out[12+col] =
        // -(t . R[col]). This makes M * rigidInverse(M) == I. Copy [15].
        void rigidInverse(const float (&in)[16], float (&out)[16])
        {
            for (int row = 0; row < 3; ++row)
            {
                for (int col = 0; col < 3; ++col)
                    out[4 * row + col] = in[4 * col + row];
                out[4 * row + 3] = 0.0f;
            }
            const float tx = in[12], ty = in[13], tz = in[14];
            for (int col = 0; col < 3; ++col)
                out[12 + col] = -((tx * in[4 * col]) + (ty * in[4 * col + 1]) + (tz * in[4 * col + 2]));
            out[15] = in[15];
        }

        void axisRotateMatrix(const float (&in)[16], float angle, int axis, float (&out)[16])
        {
            float rot[16]{};
            makeIdentityMatrix(rot);
            const float cs = std::cos(angle);
            const float sn = std::sin(angle);
            if (axis == 2)
            {
                rot[0] = cs;
                rot[1] = sn;
                rot[4] = -sn;
                rot[5] = cs;
            }
            else if (axis == 1)
            {
                rot[0] = cs;
                rot[2] = -sn;
                rot[8] = sn;
                rot[10] = cs;
            }
            else
            {
                rot[5] = cs;
                rot[6] = sn;
                rot[9] = -sn;
                rot[10] = cs;
            }
            mulVuMatrix(in, rot, out);
        }

        // Applies the matrix to the vertex, perspective-divides xyz by w
        // (w==0 maps to a zero divide rather than Inf/NaN), then converts
        // x/y to 12.4 fixed point (x16) unconditionally. z/w take the same
        // x16 conversion when fullFtoi4 is set; otherwise they are plain
        // integer-truncated (FTOI0) after the divide instead.
        void rotTransPersOne(const float (&m)[16], const float (&v)[4], bool fullFtoi4, int32_t (&out)[4])
        {
            float t[4];
            t[0] = (m[0] * v[0]) + (m[4] * v[1]) + (m[8] * v[2]) + (m[12] * v[3]);
            t[1] = (m[1] * v[0]) + (m[5] * v[1]) + (m[9] * v[2]) + (m[13] * v[3]);
            t[2] = (m[2] * v[0]) + (m[6] * v[1]) + (m[10] * v[2]) + (m[14] * v[3]);
            t[3] = (m[3] * v[0]) + (m[7] * v[1]) + (m[11] * v[2]) + (m[15] * v[3]);
            const float q = (t[3] != 0.0f) ? (1.0f / t[3]) : 0.0f;
            t[0] *= q;
            t[1] *= q;
            t[2] *= q;
            out[0] = static_cast<int32_t>(t[0] * 16.0f);
            out[1] = static_cast<int32_t>(t[1] * 16.0f);
            out[2] = fullFtoi4 ? static_cast<int32_t>(t[2] * 16.0f) : static_cast<int32_t>(t[2]);
            out[3] = fullFtoi4 ? static_cast<int32_t>(t[3] * 16.0f) : static_cast<int32_t>(t[3]);
        }

        // Guard-band proxy for the COP2 sticky clip flags: nonzero => the
        // vertex is offscreen. Not the hardware per-plane flag layout.
        constexpr float kScreenClipGuard = 4096.0f;
        int32_t screenClipCode(const float (&v)[4])
        {
            int32_t code = 0;
            if (v[0] > kScreenClipGuard)
                code |= 0x1;
            if (v[0] < -kScreenClipGuard)
                code |= 0x2;
            if (v[1] > kScreenClipGuard)
                code |= 0x4;
            if (v[1] < -kScreenClipGuard)
                code |= 0x8;
            return code;
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
        const uint32_t dstAddr = getRegU32(ctx, 4);
        const uint32_t matrixAddr = getRegU32(ctx, 5);
        const uint32_t srcAddr = getRegU32(ctx, 6);
        float matrix[16]{};
        float src[4]{};
        float out[4]{};
        if (readVuMatrix4f(rdram, matrixAddr, matrix) && readVuVec4f(rdram, srcAddr, src))
        {
            // Match libvux VuxApplyMatrix math while honoring the imported EE ABI:
            // a0=out, a1=matrix, a2=vector.
            out[0] = (matrix[0] * src[0]) + (matrix[4] * src[1]) + (matrix[8] * src[2]) + (matrix[12] * src[3]);
            out[1] = (matrix[1] * src[0]) + (matrix[5] * src[1]) + (matrix[9] * src[2]) + (matrix[13] * src[3]);
            out[2] = (matrix[2] * src[0]) + (matrix[6] * src[1]) + (matrix[10] * src[2]) + (matrix[14] * src[3]);
            out[3] = (matrix[3] * src[0]) + (matrix[7] * src[1]) + (matrix[11] * src[2]) + (matrix[15] * src[3]);
            (void)writeVuVec4f(rdram, dstAddr, out);
        }
        setReturnS32(ctx, 0);
    }

    void sceVu0CameraMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t dstAddr = getRegU32(ctx, 4);
        const uint32_t eyeAddr = getRegU32(ctx, 5);
        const uint32_t fwdAddr = getRegU32(ctx, 6);
        const uint32_t upAddr = getRegU32(ctx, 7);
        float eye[4]{}, fwd[4]{}, up[4]{};
        if (readVuVec4f(rdram, eyeAddr, eye) && readVuVec4f(rdram, fwdAddr, fwd) && readVuVec4f(rdram, upAddr, up))
        {
            auto cross = [](const float (&l)[4], const float (&r)[4], float (&o)[4])
            {
                o[0] = (l[1] * r[2]) - (l[2] * r[1]);
                o[1] = (l[2] * r[0]) - (l[0] * r[2]);
                o[2] = (l[0] * r[1]) - (l[1] * r[0]);
                o[3] = 0.0f;
            };
            auto normalize = [](const float (&s)[4], float (&o)[4])
            {
                const float len = std::sqrt((s[0] * s[0]) + (s[1] * s[1]) + (s[2] * s[2]) + (s[3] * s[3]));
                const float inv = (len > 1.0e-6f) ? (1.0f / len) : 0.0f;
                for (int i = 0; i < 4; ++i)
                    o[i] = s[i] * inv;
            };
            float rawCross[4]{}, row0[4]{}, row1[4]{}, row2[4]{};
            cross(up, fwd, rawCross);
            normalize(rawCross, row0);
            normalize(fwd, row2);
            cross(row2, row0, row1);

            float m[16]{};
            for (int i = 0; i < 4; ++i)
            {
                m[i] = row0[i];
                m[4 + i] = row1[i];
                m[8 + i] = row2[i];
            }
            m[12] = eye[0];
            m[13] = eye[1];
            m[14] = eye[2];
            m[15] = 1.0f;

            float out[16]{};
            rigidInverse(m, out);

            (void)writeVuMatrix4f(rdram, dstAddr, out);
        }
        setReturnS32(ctx, 0);
    }

    void sceVu0ClampVector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t dstAddr = getRegU32(ctx, 4);
        const uint32_t srcAddr = getRegU32(ctx, 5);
        const float lo = ctx ? ctx->f[12] : 0.0f;
        const float hi = ctx ? ctx->f[13] : 0.0f;
        float src[4]{}, out[4]{};
        if (readVuVec4f(rdram, srcAddr, src))
        {
            for (int i = 0; i < 4; ++i)
            {
                float v = src[i];
                v = (v < lo) ? lo : v;
                v = (v > hi) ? hi : v;
                out[i] = v;
            }
            (void)writeVuVec4f(rdram, dstAddr, out);
        }
        setReturnS32(ctx, 0);
    }

    void sceVu0ClipAll(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t loAddr = getRegU32(ctx, 4);
        const uint32_t hiAddr = getRegU32(ctx, 5);
        const uint32_t matAddr = getRegU32(ctx, 6);
        uint32_t vAddr = getRegU32(ctx, 7);
        const int32_t count = static_cast<int32_t>(getRegU32(ctx, 8));
        float lo[4]{}, hi[4]{}, m[16]{};
        int32_t result = 0;
        if (readVuVec4f(rdram, loAddr, lo) && readVuVec4f(rdram, hiAddr, hi) && readVuMatrix4f(rdram, matAddr, m))
        {
            result = 1;
            for (int32_t i = 0; i < count; ++i)
            {
                float v[4]{};
                if (!readVuVec4f(rdram, vAddr, v))
                    break;
                const float tx = (m[0] * v[0]) + (m[4] * v[1]) + (m[8] * v[2]) + (m[12] * v[3]);
                const float ty = (m[1] * v[0]) + (m[5] * v[1]) + (m[9] * v[2]) + (m[13] * v[3]);
                const float tw = (m[3] * v[0]) + (m[7] * v[1]) + (m[11] * v[2]) + (m[15] * v[3]);
                const bool outsideX = (tx < lo[0] * tw) || (tx > hi[0] * tw);
                const bool outsideY = (ty < lo[1] * tw) || (ty > hi[1] * tw);
                if (!outsideX && !outsideY)
                {
                    result = 0;
                    break;
                }
                vAddr += 16u;
            }
        }
        setReturnS32(ctx, result);
    }

    void sceVu0ClipScreen(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t vAddr = getRegU32(ctx, 4);
        float v[4]{};
        int32_t code = 0;
        if (readVuVec4f(rdram, vAddr, v))
        {
            code = screenClipCode(v);
        }
        setReturnS32(ctx, code);
    }

    void sceVu0ClipScreen3(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t v0Addr = getRegU32(ctx, 4);
        const uint32_t v1Addr = getRegU32(ctx, 5);
        const uint32_t v2Addr = getRegU32(ctx, 6);
        float v0[4]{}, v1[4]{}, v2[4]{};
        int32_t code = 0;
        if (readVuVec4f(rdram, v0Addr, v0))
        {
            code |= screenClipCode(v0);
        }
        if (readVuVec4f(rdram, v1Addr, v1))
        {
            code |= screenClipCode(v1);
        }
        if (readVuVec4f(rdram, v2Addr, v2))
        {
            code |= screenClipCode(v2);
        }
        setReturnS32(ctx, code);
    }

    void sceVu0CopyMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t dstAddr = getRegU32(ctx, 4);
        const uint32_t srcAddr = getRegU32(ctx, 5);
        uint8_t *dst = getMemPtr(rdram, dstAddr);
        const uint8_t *src = getConstMemPtr(rdram, srcAddr);
        if (dst && src)
        {
            std::memcpy(dst, src, sizeof(float) * 16u);
        }
        setReturnS32(ctx, 0);
    }

    void sceVu0CopyVector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t dstAddr = getRegU32(ctx, 4);
        const uint32_t srcAddr = getRegU32(ctx, 5);
        uint8_t *dst = getMemPtr(rdram, dstAddr);
        const uint8_t *src = getConstMemPtr(rdram, srcAddr);
        if (dst && src)
        {
            std::memcpy(dst, src, sizeof(float) * 4u);
        }
        setReturnS32(ctx, 0);
    }

    void sceVu0CopyVectorXYZ(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t dstAddr = getRegU32(ctx, 4);
        const uint32_t srcAddr = getRegU32(ctx, 5);
        uint8_t *dst = getMemPtr(rdram, dstAddr);
        const uint8_t *src = getConstMemPtr(rdram, srcAddr);
        if (dst && src)
        {
            std::memcpy(dst, src, sizeof(float) * 3u);
        }
        setReturnS32(ctx, 0);
    }

    void sceVu0DivVector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t dstAddr = getRegU32(ctx, 4);
        const uint32_t srcAddr = getRegU32(ctx, 5);
        const float divisor = ctx ? ctx->f[12] : 1.0f;
        float src[4]{}, out[4]{};
        if (readVuVec4f(rdram, srcAddr, src))
        {
            const float q = (divisor != 0.0f) ? (1.0f / divisor) : 0.0f;
            for (int i = 0; i < 4; ++i)
            {
                out[i] = src[i] * q;
            }
            (void)writeVuVec4f(rdram, dstAddr, out);
        }
        setReturnS32(ctx, 0);
    }

    void sceVu0DivVectorXYZ(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t dstAddr = getRegU32(ctx, 4);
        const uint32_t srcAddr = getRegU32(ctx, 5);
        const float divisor = ctx ? ctx->f[12] : 1.0f;
        float src[4]{}, out[4]{};
        if (readVuVec4f(rdram, srcAddr, src))
        {
            const float q = (divisor != 0.0f) ? (1.0f / divisor) : 0.0f;
            out[0] = src[0] * q;
            out[1] = src[1] * q;
            out[2] = src[2] * q;
            out[3] = src[3];
            (void)writeVuVec4f(rdram, dstAddr, out);
        }
        setReturnS32(ctx, 0);
    }

    void sceVu0DropShadowMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        // Two documented drop-shadow modes reconstructed from behavior; not
        // claimed bit-exact to a specific SDK build.
        const uint32_t dstAddr = getRegU32(ctx, 4);
        const uint32_t nAddr = getRegU32(ctx, 5);
        const uint32_t mode = getRegU32(ctx, 6);
        const float lx = ctx ? ctx->f[12] : 0.0f;
        const float ly = ctx ? ctx->f[13] : 0.0f;
        const float lz = ctx ? ctx->f[14] : 0.0f;
        float n[4]{};
        if (readVuVec4f(rdram, nAddr, n))
        {
            const float nx = n[0], ny = n[1], nz = n[2];
            const float d = (lx * nx) + (ly * ny) + (lz * nz);
            float out[16]{};
            if (mode != 0)
            {
                const float k = 1.0f - d;
                out[0] = (lx * nx) + k;
                out[1] = lx * ny;
                out[2] = lx * nz;
                out[3] = lx;
                out[4] = ly * nx;
                out[5] = (ly * ny) + k;
                out[6] = ly * nz;
                out[7] = ly;
                out[8] = lz * nx;
                out[9] = lz * ny;
                out[10] = (lz * nz) + k;
                out[11] = lz;
                out[12] = -nx;
                out[13] = -ny;
                out[14] = -nz;
                out[15] = -d;
            }
            else
            {
                const float k = (d != 0.0f) ? (-1.0f / d) : 0.0f;
                out[0] = k * ((lx * nx) - d);
                out[1] = k * (lx * ny);
                out[2] = k * (lx * nz);
                out[3] = 0.0f;
                out[4] = k * (ly * nx);
                out[5] = k * ((ly * ny) - d);
                out[6] = k * (ly * nz);
                out[7] = 0.0f;
                out[8] = k * (lz * nx);
                out[9] = k * (lz * ny);
                out[10] = k * ((lz * nz) - d);
                out[11] = 0.0f;
                out[12] = k * -nx;
                out[13] = k * -ny;
                out[14] = k * -nz;
                out[15] = 1.0f;
            }
            (void)writeVuMatrix4f(rdram, dstAddr, out);
        }
        setReturnS32(ctx, 0);
    }

    void sceVu0ecossin(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t dstAddr = getRegU32(ctx, 4);
        const float angle = ctx ? ctx->f[12] : 0.0f;
        float out[4] = {std::cos(angle), std::sin(angle), 0.0f, 0.0f};
        (void)writeVuVec4f(rdram, dstAddr, out);
        setReturnS32(ctx, 0);
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
        const uint32_t dstAddr = getRegU32(ctx, 4);
        const uint32_t aAddr = getRegU32(ctx, 5);
        const uint32_t bAddr = getRegU32(ctx, 6);
        const float t = ctx ? ctx->f[12] : 0.0f;
        float a[4]{}, b[4]{}, out[4]{};
        if (readVuVec4f(rdram, aAddr, a) && readVuVec4f(rdram, bAddr, b))
        {
            const float invT = 1.0f - t;
            for (int i = 0; i < 4; ++i)
            {
                out[i] = (a[i] * t) + (b[i] * invT);
            }
            (void)writeVuVec4f(rdram, dstAddr, out);
        }
        setReturnS32(ctx, 0);
    }

    void sceVu0InterVectorXYZ(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t dstAddr = getRegU32(ctx, 4);
        const uint32_t aAddr = getRegU32(ctx, 5);
        const uint32_t bAddr = getRegU32(ctx, 6);
        const float t = ctx ? ctx->f[12] : 0.0f;
        float a[4]{}, b[4]{}, out[4]{};
        if (readVuVec4f(rdram, aAddr, a) && readVuVec4f(rdram, bAddr, b))
        {
            const float invT = 1.0f - t;
            out[0] = (a[0] * t) + (b[0] * invT);
            out[1] = (a[1] * t) + (b[1] * invT);
            out[2] = (a[2] * t) + (b[2] * invT);
            out[3] = a[3];
            (void)writeVuVec4f(rdram, dstAddr, out);
        }
        setReturnS32(ctx, 0);
    }

    void sceVu0InversMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t dstAddr = getRegU32(ctx, 4);
        const uint32_t srcAddr = getRegU32(ctx, 5);
        float in[16]{}, out[16]{};
        if (readVuMatrix4f(rdram, srcAddr, in))
        {
            rigidInverse(in, out);
            (void)writeVuMatrix4f(rdram, dstAddr, out);
        }
        setReturnS32(ctx, 0);
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
        const uint32_t dstAddr = getRegU32(ctx, 4);
        const uint32_t c0Addr = getRegU32(ctx, 5);
        const uint32_t c1Addr = getRegU32(ctx, 6);
        const uint32_t c2Addr = getRegU32(ctx, 7);
        const uint32_t c3Addr = getRegU32(ctx, 8);
        float c0[4]{}, c1[4]{}, c2[4]{}, c3[4]{};
        if (readVuVec4f(rdram, c0Addr, c0) && readVuVec4f(rdram, c1Addr, c1) &&
            readVuVec4f(rdram, c2Addr, c2) && readVuVec4f(rdram, c3Addr, c3))
        {
            float out[16]{};
            for (int i = 0; i < 4; ++i)
            {
                out[i] = c0[i];
                out[4 + i] = c1[i];
                out[8 + i] = c2[i];
                out[12 + i] = c3[i];
            }
            (void)writeVuMatrix4f(rdram, dstAddr, out);
        }
        setReturnS32(ctx, 0);
    }

    void sceVu0MulMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t dstAddr = getRegU32(ctx, 4);
        const uint32_t m0Addr = getRegU32(ctx, 5);
        const uint32_t m1Addr = getRegU32(ctx, 6);
        float m0[16]{}, m1[16]{}, out[16]{};
        if (readVuMatrix4f(rdram, m0Addr, m0) && readVuMatrix4f(rdram, m1Addr, m1))
        {
            // out = m0 * m1 (first source . second source), matching the
            // file's mulVuMatrix(lhs,rhs)=lhs.rhs convention and the RotMatrix
            // / ViewScreenMatrix siblings (first operand on the left).
            mulVuMatrix(m0, m1, out);
            (void)writeVuMatrix4f(rdram, dstAddr, out);
        }
        setReturnS32(ctx, 0);
    }

    void sceVu0MulVector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t dstAddr = getRegU32(ctx, 4);
        const uint32_t lhsAddr = getRegU32(ctx, 5);
        const uint32_t rhsAddr = getRegU32(ctx, 6);
        float lhs[4]{}, rhs[4]{}, out[4]{};
        if (readVuVec4f(rdram, lhsAddr, lhs) && readVuVec4f(rdram, rhsAddr, rhs))
        {
            for (int i = 0; i < 4; ++i)
            {
                out[i] = lhs[i] * rhs[i];
            }
            (void)writeVuVec4f(rdram, dstAddr, out);
        }
        setReturnS32(ctx, 0);
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
        // Rows = normalize(-light); the 4x4 is transposed so directions occupy
        // columns (one ApplyMatrix then yields per-light N.L).
        const uint32_t dstAddr = getRegU32(ctx, 4);
        const uint32_t l0Addr = getRegU32(ctx, 5);
        const uint32_t l1Addr = getRegU32(ctx, 6);
        const uint32_t l2Addr = getRegU32(ctx, 7);
        float l0[4]{}, l1[4]{}, l2[4]{};
        if (readVuVec4f(rdram, l0Addr, l0) && readVuVec4f(rdram, l1Addr, l1) && readVuVec4f(rdram, l2Addr, l2))
        {
            auto negNormalize = [](const float (&s)[4], float (&o)[4])
            {
                const float len = std::sqrt((s[0] * s[0]) + (s[1] * s[1]) + (s[2] * s[2]) + (s[3] * s[3]));
                const float inv = (len > 1.0e-6f) ? (1.0f / len) : 0.0f;
                for (int i = 0; i < 4; ++i)
                    o[i] = -s[i] * inv;
            };
            float r0[4]{}, r1[4]{}, r2[4]{};
            negNormalize(l0, r0);
            negNormalize(l1, r1);
            negNormalize(l2, r2);
            float m[16]{};
            for (int i = 0; i < 4; ++i)
            {
                m[i] = r0[i];
                m[4 + i] = r1[i];
                m[8 + i] = r2[i];
                m[12 + i] = 0.0f;
            }
            m[15] = 1.0f;
            float out[16]{};
            for (int row = 0; row < 4; ++row)
                for (int col = 0; col < 4; ++col)
                    out[4 * row + col] = m[4 * col + row];
            (void)writeVuMatrix4f(rdram, dstAddr, out);
        }
        setReturnS32(ctx, 0);
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
        const uint32_t dstAddr = getRegU32(ctx, 4);
        const uint32_t srcAddr = getRegU32(ctx, 5);
        const uint32_t rotAddr = getRegU32(ctx, 6);
        float src[16]{}, rotVec[4]{};
        if (readVuMatrix4f(rdram, srcAddr, src) && readVuVec4f(rdram, rotAddr, rotVec))
        {
            float afterZ[16]{}, afterY[16]{}, afterX[16]{};
            axisRotateMatrix(src, rotVec[2], 2, afterZ);
            axisRotateMatrix(afterZ, rotVec[1], 1, afterY);
            axisRotateMatrix(afterY, rotVec[0], 0, afterX);
            (void)writeVuMatrix4f(rdram, dstAddr, afterX);
        }
        setReturnS32(ctx, 0);
    }

    void sceVu0RotMatrixX(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t dstAddr = getRegU32(ctx, 4);
        const uint32_t srcAddr = getRegU32(ctx, 5);
        const float angle = ctx ? ctx->f[12] : 0.0f;
        float src[16]{}, rot[16]{}, out[16]{};
        if (readVuMatrix4f(rdram, srcAddr, src))
        {
            makeIdentityMatrix(rot);
            const float cs = std::cos(angle);
            const float sn = std::sin(angle);
            rot[5] = cs;
            rot[6] = sn;
            rot[9] = -sn;
            rot[10] = cs;
            mulVuMatrix(src, rot, out);
            (void)writeVuMatrix4f(rdram, dstAddr, out);
        }
        setReturnS32(ctx, 0);
    }

    void sceVu0RotMatrixY(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t dstAddr = getRegU32(ctx, 4);
        const uint32_t srcAddr = getRegU32(ctx, 5);
        const float angle = ctx ? ctx->f[12] : 0.0f;
        float src[16]{}, rot[16]{}, out[16]{};
        if (readVuMatrix4f(rdram, srcAddr, src))
        {
            makeIdentityMatrix(rot);
            const float cs = std::cos(angle);
            const float sn = std::sin(angle);
            rot[0] = cs;
            rot[2] = -sn;
            rot[8] = sn;
            rot[10] = cs;
            mulVuMatrix(src, rot, out);
            (void)writeVuMatrix4f(rdram, dstAddr, out);
        }
        setReturnS32(ctx, 0);
    }

    void sceVu0RotMatrixZ(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t dstAddr = getRegU32(ctx, 4);
        const uint32_t srcAddr = getRegU32(ctx, 5);
        const float angle = ctx ? ctx->f[12] : 0.0f;
        float src[16]{}, rot[16]{}, out[16]{};
        if (readVuMatrix4f(rdram, srcAddr, src))
        {
            makeIdentityMatrix(rot);
            const float cs = std::cos(angle);
            const float sn = std::sin(angle);
            rot[0] = cs;
            rot[1] = sn;
            rot[4] = -sn;
            rot[5] = cs;
            mulVuMatrix(src, rot, out);
            (void)writeVuMatrix4f(rdram, dstAddr, out);
        }
        setReturnS32(ctx, 0);
    }

    void sceVu0RotTransPers(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t dstAddr = getRegU32(ctx, 4);
        const uint32_t matAddr = getRegU32(ctx, 5);
        const uint32_t vAddr = getRegU32(ctx, 6);
        const bool fullFtoi4 = (getRegU32(ctx, 7) != 0);
        float m[16]{}, v[4]{};
        if (readVuMatrix4f(rdram, matAddr, m) && readVuVec4f(rdram, vAddr, v))
        {
            int32_t out[4]{};
            rotTransPersOne(m, v, fullFtoi4, out);
            (void)writeVuVec4i(rdram, dstAddr, out);
        }
        setReturnS32(ctx, 0);
    }

    void sceVu0RotTransPersN(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t dstAddr = getRegU32(ctx, 4);
        const uint32_t matAddr = getRegU32(ctx, 5);
        uint32_t vAddr = getRegU32(ctx, 6);
        const int32_t count = static_cast<int32_t>(getRegU32(ctx, 7));
        const bool fullFtoi4 = (getRegU32(ctx, 8) != 0);
        float m[16]{};
        if (readVuMatrix4f(rdram, matAddr, m))
        {
            uint32_t outAddr = dstAddr;
            for (int32_t i = 0; i < count; ++i)
            {
                float v[4]{};
                if (!readVuVec4f(rdram, vAddr, v))
                    break;
                int32_t out[4]{};
                rotTransPersOne(m, v, fullFtoi4, out);
                (void)writeVuVec4i(rdram, outAddr, out);
                vAddr += 16u;
                outAddr += 16u;
            }
        }
        setReturnS32(ctx, 0);
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
        const uint32_t dstAddr = getRegU32(ctx, 4);
        const uint32_t srcAddr = getRegU32(ctx, 5);
        const float scale = ctx ? ctx->f[12] : 0.0f;
        float src[4]{}, out[4]{};
        if (readVuVec4f(rdram, srcAddr, src))
        {
            out[0] = src[0] * scale;
            out[1] = src[1] * scale;
            out[2] = src[2] * scale;
            out[3] = src[3];
            (void)writeVuVec4f(rdram, dstAddr, out);
        }
        setReturnS32(ctx, 0);
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
        const uint32_t dstAddr = getRegU32(ctx, 4);
        const uint32_t srcAddr = getRegU32(ctx, 5);
        const uint32_t vAddr = getRegU32(ctx, 6);
        float src[16]{}, v[4]{};
        if (readVuMatrix4f(rdram, srcAddr, src) && readVuVec4f(rdram, vAddr, v))
        {
            float out[16]{};
            std::memcpy(out, src, sizeof(out));
            out[12] = src[12] + v[0];
            out[13] = src[13] + v[1];
            out[14] = src[14] + v[2];
            (void)writeVuMatrix4f(rdram, dstAddr, out);
        }
        setReturnS32(ctx, 0);
    }

    void sceVu0TransposeMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t dstAddr = getRegU32(ctx, 4);
        const uint32_t srcAddr = getRegU32(ctx, 5);
        float src[16]{};
        float out[16]{};
        if (readVuMatrix4f(rdram, srcAddr, src))
        {
            for (int row = 0; row < 4; ++row)
            {
                for (int col = 0; col < 4; ++col)
                {
                    out[4 * row + col] = src[4 * col + row];
                }
            }
            (void)writeVuMatrix4f(rdram, dstAddr, out);
        }
        setReturnS32(ctx, 0);
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
        // Near/far params handled by formula shape; SDK parameter names
        // not pinned. Args follow the out-of-line libvu0 register convention
        // used throughout this file: eight scalar floats in f12..f19 (p0..p7)
        // and the ninth (p8) as the first stack-passed argument. That
        // eight-FP-arg-register layout is the n32/EABI convention the EE
        // toolchain emits (an o32 layout would carry only two FP args in
        // f12/f14 and spill p2..p7 too), and under it no GPR home/save area is
        // reserved, so the ninth float is at 0(sp) -- read below. A target ABI
        // that reserves a home area would shift only that read by its size.
        const uint32_t dstAddr = getRegU32(ctx, 4);
        const float p0 = ctx ? ctx->f[12] : 0.0f;
        const float p1 = ctx ? ctx->f[13] : 0.0f;
        const float p2 = ctx ? ctx->f[14] : 0.0f;
        const float p3 = ctx ? ctx->f[15] : 0.0f;
        const float p4 = ctx ? ctx->f[16] : 0.0f;
        const float p5 = ctx ? ctx->f[17] : 0.0f;
        const float p6 = ctx ? ctx->f[18] : 0.0f;
        const float p7 = ctx ? ctx->f[19] : 0.0f;
        float p8 = 0.0f;
        if (const uint8_t *sp = getConstMemPtr(rdram, getRegU32(ctx, 29)))
        {
            std::memcpy(&p8, sp, sizeof(p8));
        }

        const float denom = p8 - p7;
        const float zScale = (denom != 0.0f) ? ((p8 * p7 * (p6 - p5)) / denom) : 0.0f;
        const float zOffset = (denom != 0.0f) ? (((p5 * p8) - (p6 * p7)) / denom) : 0.0f;

        float scaleMat[16]{};
        makeIdentityMatrix(scaleMat);
        scaleMat[0] = p0;
        scaleMat[5] = p0;
        scaleMat[10] = 0.0f;
        scaleMat[11] = 1.0f;
        scaleMat[14] = 1.0f;
        scaleMat[15] = 0.0f;

        float projMat[16]{};
        makeIdentityMatrix(projMat);
        projMat[0] = p1;
        projMat[5] = p2;
        projMat[10] = zScale;
        projMat[12] = p3;
        projMat[13] = p4;
        projMat[14] = zOffset;

        float out[16]{};
        mulVuMatrix(scaleMat, projMat, out);
        (void)writeVuMatrix4f(rdram, dstAddr, out);
        setReturnS32(ctx, 0);
    }
}
