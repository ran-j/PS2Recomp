#include "MiniTest.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "runtime/ps2_memory.h"
#include "Kernel/Stubs/VU.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace
{
    constexpr float kEps = 1e-4f;

    bool nearlyEqual(float a, float b, float eps = kEps)
    {
        return std::fabs(a - b) <= eps;
    }

    constexpr uint32_t kDst = 0x1000u;
    constexpr uint32_t kA = 0x1100u;
    constexpr uint32_t kB = 0x1200u;
    constexpr uint32_t kC = 0x1300u;
    constexpr uint32_t kD = 0x1400u;
    constexpr uint32_t kDst2 = 0x1500u;
    constexpr uint32_t kArr = 0x1600u;
    constexpr uint32_t kOut = 0x1700u;
    constexpr uint32_t kOut2 = 0x1800u;
    constexpr uint32_t kSp = 0x1900u;

    struct VuEnv
    {
        std::vector<uint8_t> rdram;
        R5900Context ctx{};
        PS2Runtime runtime;

        VuEnv() : rdram(PS2_RAM_SIZE, 0)
        {
            std::memset(&ctx, 0, sizeof(ctx));
        }
    };

    void writeVec4(VuEnv &env, uint32_t addr, float x, float y, float z, float w)
    {
        float v[4] = {x, y, z, w};
        std::memcpy(getMemPtr(env.rdram.data(), addr), v, sizeof(v));
    }

    void readVec4f(VuEnv &env, uint32_t addr, float (&out)[4])
    {
        std::memcpy(out, getConstMemPtr(env.rdram.data(), addr), sizeof(out));
    }

    void readVec4i(VuEnv &env, uint32_t addr, int32_t (&out)[4])
    {
        std::memcpy(out, getConstMemPtr(env.rdram.data(), addr), sizeof(out));
    }

    void writeMat4(VuEnv &env, uint32_t addr, const float (&m)[16])
    {
        std::memcpy(getMemPtr(env.rdram.data(), addr), m, sizeof(m));
    }

    void readMat4(VuEnv &env, uint32_t addr, float (&out)[16])
    {
        std::memcpy(out, getConstMemPtr(env.rdram.data(), addr), sizeof(out));
    }

    void makeIdentity(float (&m)[16])
    {
        std::fill(std::begin(m), std::end(m), 0.0f);
        m[0] = 1.0f;
        m[5] = 1.0f;
        m[10] = 1.0f;
        m[15] = 1.0f;
    }
}

void register_ps2_vu_tests()
{
    MiniTest::Case("PS2VU0Math", [](TestCase &tc)
    {
        tc.Run("MulVector_componentwise", [](TestCase &t)
        {
            VuEnv env;
            writeVec4(env, kA, 2.0f, 3.0f, 4.0f, 5.0f);
            writeVec4(env, kB, 10.0f, 10.0f, 10.0f, 10.0f);
            SET_GPR_U32(&env.ctx, 4, kDst);
            SET_GPR_U32(&env.ctx, 5, kA);
            SET_GPR_U32(&env.ctx, 6, kB);
            ps2_stubs::sceVu0MulVector(env.rdram.data(), &env.ctx, &env.runtime);
            float out[4]{};
            readVec4f(env, kDst, out);
            t.IsTrue(nearlyEqual(out[0], 20.0f) && nearlyEqual(out[1], 30.0f) &&
                         nearlyEqual(out[2], 40.0f) && nearlyEqual(out[3], 50.0f),
                     "MulVector should multiply lanes component-wise");
        });

        tc.Run("ScaleVectorXYZ_scales_xyz", [](TestCase &t)
        {
            VuEnv env;
            writeVec4(env, kA, 2.0f, 4.0f, 6.0f, 9.0f);
            env.ctx.f[12] = 3.0f;
            SET_GPR_U32(&env.ctx, 4, kDst);
            SET_GPR_U32(&env.ctx, 5, kA);
            ps2_stubs::sceVu0ScaleVectorXYZ(env.rdram.data(), &env.ctx, &env.runtime);
            float out[4]{};
            readVec4f(env, kDst, out);
            t.IsTrue(nearlyEqual(out[0], 6.0f) && nearlyEqual(out[1], 12.0f) && nearlyEqual(out[2], 18.0f),
                     "ScaleVectorXYZ should scale xyz by f12");
        });

        tc.Run("ScaleVectorXYZ_w_passthrough", [](TestCase &t)
        {
            VuEnv env;
            writeVec4(env, kA, 2.0f, 4.0f, 6.0f, 9.0f);
            env.ctx.f[12] = 3.0f;
            SET_GPR_U32(&env.ctx, 4, kDst);
            SET_GPR_U32(&env.ctx, 5, kA);
            ps2_stubs::sceVu0ScaleVectorXYZ(env.rdram.data(), &env.ctx, &env.runtime);
            float out[4]{};
            readVec4f(env, kDst, out);
            t.IsTrue(nearlyEqual(out[3], 9.0f), "ScaleVectorXYZ should pass w through unscaled");
        });

        tc.Run("ClampVector_low", [](TestCase &t)
        {
            VuEnv env;
            writeVec4(env, kA, -5.0f, -10.0f, 50.0f, 3.0f);
            env.ctx.f[12] = 0.0f;
            env.ctx.f[13] = 100.0f;
            SET_GPR_U32(&env.ctx, 4, kDst);
            SET_GPR_U32(&env.ctx, 5, kA);
            ps2_stubs::sceVu0ClampVector(env.rdram.data(), &env.ctx, &env.runtime);
            float out[4]{};
            readVec4f(env, kDst, out);
            t.IsTrue(nearlyEqual(out[0], 0.0f) && nearlyEqual(out[1], 0.0f), "ClampVector should clamp below lo");
        });

        tc.Run("ClampVector_high", [](TestCase &t)
        {
            VuEnv env;
            writeVec4(env, kA, -5.0f, 20.0f, 50.0f, 3.0f);
            env.ctx.f[12] = -100.0f;
            env.ctx.f[13] = 10.0f;
            SET_GPR_U32(&env.ctx, 4, kDst);
            SET_GPR_U32(&env.ctx, 5, kA);
            ps2_stubs::sceVu0ClampVector(env.rdram.data(), &env.ctx, &env.runtime);
            float out[4]{};
            readVec4f(env, kDst, out);
            t.IsTrue(nearlyEqual(out[1], 10.0f) && nearlyEqual(out[2], 10.0f), "ClampVector should clamp above hi");
        });

        tc.Run("DivVector_reciprocal", [](TestCase &t)
        {
            VuEnv env;
            writeVec4(env, kA, 2.0f, 4.0f, 6.0f, 8.0f);
            env.ctx.f[12] = 2.0f;
            SET_GPR_U32(&env.ctx, 4, kDst);
            SET_GPR_U32(&env.ctx, 5, kA);
            ps2_stubs::sceVu0DivVector(env.rdram.data(), &env.ctx, &env.runtime);
            float out[4]{};
            readVec4f(env, kDst, out);
            t.IsTrue(nearlyEqual(out[0], 1.0f) && nearlyEqual(out[1], 2.0f) &&
                         nearlyEqual(out[2], 3.0f) && nearlyEqual(out[3], 4.0f),
                     "DivVector should multiply by the reciprocal of the divisor");
        });

        tc.Run("DivVector_divzero_zero", [](TestCase &t)
        {
            VuEnv env;
            writeVec4(env, kA, 5.0f, 5.0f, 5.0f, 5.0f);
            env.ctx.f[12] = 0.0f;
            SET_GPR_U32(&env.ctx, 4, kDst);
            SET_GPR_U32(&env.ctx, 5, kA);
            ps2_stubs::sceVu0DivVector(env.rdram.data(), &env.ctx, &env.runtime);
            float out[4]{};
            readVec4f(env, kDst, out);
            t.IsTrue(nearlyEqual(out[0], 0.0f) && nearlyEqual(out[1], 0.0f) &&
                         nearlyEqual(out[2], 0.0f) && nearlyEqual(out[3], 0.0f),
                     "DivVector should produce finite zero, not Inf/NaN, when the divisor is zero");
        });

        tc.Run("DivVectorXYZ_w_passthrough", [](TestCase &t)
        {
            VuEnv env;
            writeVec4(env, kA, 2.0f, 4.0f, 6.0f, 9.0f);
            env.ctx.f[12] = 2.0f;
            SET_GPR_U32(&env.ctx, 4, kDst);
            SET_GPR_U32(&env.ctx, 5, kA);
            ps2_stubs::sceVu0DivVectorXYZ(env.rdram.data(), &env.ctx, &env.runtime);
            float out[4]{};
            readVec4f(env, kDst, out);
            t.IsTrue(nearlyEqual(out[3], 9.0f), "DivVectorXYZ should pass w through unchanged");
        });

        tc.Run("InterVector_lerp", [](TestCase &t)
        {
            VuEnv env;
            writeVec4(env, kA, 10.0f, 10.0f, 10.0f, 10.0f);
            writeVec4(env, kB, 0.0f, 0.0f, 0.0f, 0.0f);
            env.ctx.f[12] = 0.25f;
            SET_GPR_U32(&env.ctx, 4, kDst);
            SET_GPR_U32(&env.ctx, 5, kA);
            SET_GPR_U32(&env.ctx, 6, kB);
            ps2_stubs::sceVu0InterVector(env.rdram.data(), &env.ctx, &env.runtime);
            float out[4]{};
            readVec4f(env, kDst, out);
            t.IsTrue(nearlyEqual(out[0], 2.5f) && nearlyEqual(out[3], 2.5f),
                     "InterVector should lerp as a*t + b*(1-t)");
        });

        tc.Run("InterVectorXYZ_w_from_a", [](TestCase &t)
        {
            VuEnv env;
            writeVec4(env, kA, 10.0f, 10.0f, 10.0f, 99.0f);
            writeVec4(env, kB, 0.0f, 0.0f, 0.0f, 0.0f);
            env.ctx.f[12] = 0.25f;
            SET_GPR_U32(&env.ctx, 4, kDst);
            SET_GPR_U32(&env.ctx, 5, kA);
            SET_GPR_U32(&env.ctx, 6, kB);
            ps2_stubs::sceVu0InterVectorXYZ(env.rdram.data(), &env.ctx, &env.runtime);
            float out[4]{};
            readVec4f(env, kDst, out);
            t.IsTrue(nearlyEqual(out[3], 99.0f), "InterVectorXYZ should take w unmodified from a");
        });

        tc.Run("TransMatrix_adds_translation", [](TestCase &t)
        {
            VuEnv env;
            float src[16] = {
                1, 0, 0, 0,
                0, 1, 0, 0,
                0, 0, 1, 0,
                100, 200, 300, 1};
            writeMat4(env, kA, src);
            writeVec4(env, kB, 5.0f, 6.0f, 7.0f, 0.0f);
            SET_GPR_U32(&env.ctx, 4, kDst);
            SET_GPR_U32(&env.ctx, 5, kA);
            SET_GPR_U32(&env.ctx, 6, kB);
            ps2_stubs::sceVu0TransMatrix(env.rdram.data(), &env.ctx, &env.runtime);
            float out[16]{};
            readMat4(env, kDst, out);
            t.IsTrue(nearlyEqual(out[12], 105.0f) && nearlyEqual(out[13], 206.0f) && nearlyEqual(out[14], 307.0f),
                     "TransMatrix should add the translation vector onto row3.xyz");
        });

        tc.Run("TransMatrix_rows_unchanged", [](TestCase &t)
        {
            VuEnv env;
            float src[16] = {
                1, 2, 3, 4,
                5, 6, 7, 8,
                9, 10, 11, 12,
                100, 200, 300, 1};
            writeMat4(env, kA, src);
            writeVec4(env, kB, 1.0f, 1.0f, 1.0f, 0.0f);
            SET_GPR_U32(&env.ctx, 4, kDst);
            SET_GPR_U32(&env.ctx, 5, kA);
            SET_GPR_U32(&env.ctx, 6, kB);
            ps2_stubs::sceVu0TransMatrix(env.rdram.data(), &env.ctx, &env.runtime);
            float out[16]{};
            readMat4(env, kDst, out);
            t.IsTrue(nearlyEqual(out[0], 1.0f) && nearlyEqual(out[5], 6.0f) && nearlyEqual(out[10], 11.0f) &&
                         nearlyEqual(out[15], 1.0f),
                     "TransMatrix should leave rows 0-2 and row3.w copied verbatim from src");
        });

        tc.Run("MulMatrix_identity_left", [](TestCase &t)
        {
            VuEnv env;
            float ident[16]{};
            makeIdentity(ident);
            float a[16] = {
                1, 2, 3, 4,
                5, 6, 7, 8,
                9, 10, 11, 12,
                13, 14, 15, 16};
            writeMat4(env, kA, ident);
            writeMat4(env, kB, a);
            SET_GPR_U32(&env.ctx, 4, kDst);
            SET_GPR_U32(&env.ctx, 5, kA);
            SET_GPR_U32(&env.ctx, 6, kB);
            ps2_stubs::sceVu0MulMatrix(env.rdram.data(), &env.ctx, &env.runtime);
            float out[16]{};
            readMat4(env, kDst, out);
            bool allMatch = true;
            for (int i = 0; i < 16; ++i)
            {
                if (!nearlyEqual(out[i], a[i]))
                {
                    allMatch = false;
                }
            }
            t.IsTrue(allMatch, "MulMatrix(dst, I, A) should equal A");
        });

        tc.Run("MulMatrix_equals_arg1_times_arg2", [](TestCase &t)
        {
            VuEnv env;
            float m0[16] = {
                1, 3, 2, 4,
                5, 2, 6, 1,
                3, 7, 1, 5,
                2, 4, 3, 1};
            float m1[16] = {
                1, 2, 3, 4,
                5, 6, 7, 8,
                9, 10, 11, 12,
                13, 14, 15, 16};
            float expected[16]{};
            for (int i = 0; i < 4; ++i)
            {
                for (int j = 0; j < 4; ++j)
                {
                    float sum = 0.0f;
                    for (int k = 0; k < 4; ++k)
                    {
                        // expected = mulVuMatrix(m0, m1) = m0 * m1 (arg1 * arg2);
                        // mulVuMatrix is file-local to VU.cpp, so mirror its formula here.
                        sum += m1[4 * k + j] * m0[4 * i + k];
                    }
                    expected[4 * i + j] = sum;
                }
            }
            writeMat4(env, kA, m0);
            writeMat4(env, kB, m1);
            SET_GPR_U32(&env.ctx, 4, kDst);
            SET_GPR_U32(&env.ctx, 5, kA);
            SET_GPR_U32(&env.ctx, 6, kB);
            ps2_stubs::sceVu0MulMatrix(env.rdram.data(), &env.ctx, &env.runtime);
            float out[16]{};
            readMat4(env, kDst, out);
            bool allMatch = true;
            for (int i = 0; i < 16; ++i)
            {
                if (!nearlyEqual(out[i], expected[i]))
                {
                    allMatch = false;
                }
            }
            t.IsTrue(allMatch, "MulMatrix(dst, m0, m1) should equal m0*m1 (mulVuMatrix(m0,m1), arg1*arg2)");
        });

        tc.Run("RotMatrix_Z_matches_RotMatrixZ", [](TestCase &t)
        {
            VuEnv env;
            float ident[16]{};
            makeIdentity(ident);
            const float angle = 0.3f;
            writeMat4(env, kA, ident);
            writeVec4(env, kB, 0.0f, 0.0f, angle, 0.0f);
            SET_GPR_U32(&env.ctx, 4, kDst);
            SET_GPR_U32(&env.ctx, 5, kA);
            SET_GPR_U32(&env.ctx, 6, kB);
            ps2_stubs::sceVu0RotMatrix(env.rdram.data(), &env.ctx, &env.runtime);
            float rotOut[16]{};
            readMat4(env, kDst, rotOut);

            VuEnv env2;
            writeMat4(env2, kA, ident);
            env2.ctx.f[12] = angle;
            SET_GPR_U32(&env2.ctx, 4, kDst2);
            SET_GPR_U32(&env2.ctx, 5, kA);
            ps2_stubs::sceVu0RotMatrixZ(env2.rdram.data(), &env2.ctx, &env2.runtime);
            float zOut[16]{};
            readMat4(env2, kDst2, zOut);

            bool allMatch = true;
            for (int i = 0; i < 16; ++i)
            {
                if (!nearlyEqual(rotOut[i], zOut[i]))
                {
                    allMatch = false;
                }
            }
            t.IsTrue(allMatch, "RotMatrix with rotVec=(0,0,angle) should match RotMatrixZ(angle)");
        });

        tc.Run("RotMatrix_X_matches_RotMatrixX", [](TestCase &t)
        {
            VuEnv env;
            float ident[16]{};
            makeIdentity(ident);
            const float angle = 0.4f;
            writeMat4(env, kA, ident);
            writeVec4(env, kB, angle, 0.0f, 0.0f, 0.0f);
            SET_GPR_U32(&env.ctx, 4, kDst);
            SET_GPR_U32(&env.ctx, 5, kA);
            SET_GPR_U32(&env.ctx, 6, kB);
            ps2_stubs::sceVu0RotMatrix(env.rdram.data(), &env.ctx, &env.runtime);
            float rotOut[16]{};
            readMat4(env, kDst, rotOut);

            VuEnv env2;
            writeMat4(env2, kA, ident);
            env2.ctx.f[12] = angle;
            SET_GPR_U32(&env2.ctx, 4, kDst2);
            SET_GPR_U32(&env2.ctx, 5, kA);
            ps2_stubs::sceVu0RotMatrixX(env2.rdram.data(), &env2.ctx, &env2.runtime);
            float xOut[16]{};
            readMat4(env2, kDst2, xOut);

            bool allMatch = true;
            for (int i = 0; i < 16; ++i)
            {
                if (!nearlyEqual(rotOut[i], xOut[i]))
                {
                    allMatch = false;
                }
            }
            t.IsTrue(allMatch, "RotMatrix with rotVec=(angle,0,0) should match RotMatrixX(angle)");
        });

        tc.Run("RotMatrix_Y_matches_RotMatrixY", [](TestCase &t)
        {
            VuEnv env;
            float ident[16]{};
            makeIdentity(ident);
            const float angle = 0.5f;
            writeMat4(env, kA, ident);
            writeVec4(env, kB, 0.0f, angle, 0.0f, 0.0f);
            SET_GPR_U32(&env.ctx, 4, kDst);
            SET_GPR_U32(&env.ctx, 5, kA);
            SET_GPR_U32(&env.ctx, 6, kB);
            ps2_stubs::sceVu0RotMatrix(env.rdram.data(), &env.ctx, &env.runtime);
            float rotOut[16]{};
            readMat4(env, kDst, rotOut);

            VuEnv env2;
            writeMat4(env2, kA, ident);
            env2.ctx.f[12] = angle;
            SET_GPR_U32(&env2.ctx, 4, kDst2);
            SET_GPR_U32(&env2.ctx, 5, kA);
            ps2_stubs::sceVu0RotMatrixY(env2.rdram.data(), &env2.ctx, &env2.runtime);
            float yOut[16]{};
            readMat4(env2, kDst2, yOut);

            bool allMatch = true;
            for (int i = 0; i < 16; ++i)
            {
                if (!nearlyEqual(rotOut[i], yOut[i]))
                {
                    allMatch = false;
                }
            }
            t.IsTrue(allMatch, "RotMatrix with rotVec=(0,angle,0) should match RotMatrixY(angle)");
        });

        tc.Run("RotMatrix_multi_axis_order", [](TestCase &t)
        {
            // RotMatrix composes dst = src * Rz * Ry * Rx (Z first, then Y, then X).
            // Cross-check against chaining the trusted single-axis siblings in the
            // same order. Distinct nonzero angles about non-commuting axes make the
            // ordering observable: reordering the three composition steps diverges.
            VuEnv env;
            float ident[16]{};
            makeIdentity(ident);
            const float ax = 0.3f, ay = 0.5f, az = 0.7f;

            writeMat4(env, kA, ident);
            writeVec4(env, kB, ax, ay, az, 0.0f);
            SET_GPR_U32(&env.ctx, 4, kDst);
            SET_GPR_U32(&env.ctx, 5, kA);
            SET_GPR_U32(&env.ctx, 6, kB);
            ps2_stubs::sceVu0RotMatrix(env.rdram.data(), &env.ctx, &env.runtime);
            float rotOut[16]{};
            readMat4(env, kDst, rotOut);

            // Reference: RotMatrixX(RotMatrixY(RotMatrixZ(src, az), ay), ax).
            VuEnv env2;
            writeMat4(env2, kA, ident);
            env2.ctx.f[12] = az;
            SET_GPR_U32(&env2.ctx, 4, kB); // kB <- src * Rz
            SET_GPR_U32(&env2.ctx, 5, kA);
            ps2_stubs::sceVu0RotMatrixZ(env2.rdram.data(), &env2.ctx, &env2.runtime);
            env2.ctx.f[12] = ay;
            SET_GPR_U32(&env2.ctx, 4, kC); // kC <- (src*Rz) * Ry
            SET_GPR_U32(&env2.ctx, 5, kB);
            ps2_stubs::sceVu0RotMatrixY(env2.rdram.data(), &env2.ctx, &env2.runtime);
            env2.ctx.f[12] = ax;
            SET_GPR_U32(&env2.ctx, 4, kD); // kD <- (src*Rz*Ry) * Rx
            SET_GPR_U32(&env2.ctx, 5, kC);
            ps2_stubs::sceVu0RotMatrixX(env2.rdram.data(), &env2.ctx, &env2.runtime);
            float refOut[16]{};
            readMat4(env2, kD, refOut);

            bool allMatch = true;
            for (int i = 0; i < 16; ++i)
            {
                if (!nearlyEqual(rotOut[i], refOut[i], 1e-3f))
                {
                    allMatch = false;
                }
            }
            t.IsTrue(allMatch,
                     "RotMatrix(src,(ax,ay,az)) should equal RotMatrixX(RotMatrixY(RotMatrixZ(src,az),ay),ax)");
        });

        tc.Run("InversMatrix_transposes_rotation", [](TestCase &t)
        {
            VuEnv env;
            float in[16] = {
                1, 2, 3, 0,
                4, 5, 6, 0,
                7, 8, 9, 0,
                100, 200, 300, 1};
            writeMat4(env, kA, in);
            SET_GPR_U32(&env.ctx, 4, kDst);
            SET_GPR_U32(&env.ctx, 5, kA);
            ps2_stubs::sceVu0InversMatrix(env.rdram.data(), &env.ctx, &env.runtime);
            float out[16]{};
            readMat4(env, kDst, out);
            t.IsTrue(nearlyEqual(out[1], 4.0f), "InversMatrix should transpose the 3x3 rotation block");
        });

        tc.Run("InversMatrix_negRt_translation", [](TestCase &t)
        {
            VuEnv env;
            float in[16] = {
                1, 2, 3, 0,
                4, 5, 6, 0,
                7, 8, 9, 0,
                100, 200, 300, 1};
            writeMat4(env, kA, in);
            SET_GPR_U32(&env.ctx, 4, kDst);
            SET_GPR_U32(&env.ctx, 5, kA);
            ps2_stubs::sceVu0InversMatrix(env.rdram.data(), &env.ctx, &env.runtime);
            float out[16]{};
            readMat4(env, kDst, out);
            // Corrected rigid-inverse translation = -t*R^T (t dotted with rotation
            // ROWS). t=(100,200,300), R rows (1,2,3)/(4,5,6)/(7,8,9):
            // out[12]=-(100+400+900)=-1400, out[13]=-(400+1000+1800)=-3200,
            // out[14]=-(700+1600+2700)=-5000.
            t.IsTrue(nearlyEqual(out[12], -1400.0f) && nearlyEqual(out[13], -3200.0f) &&
                         nearlyEqual(out[14], -5000.0f),
                     "InversMatrix translation row should be -t*R^T (t dotted with rotation rows)");
        });

        tc.Run("InversMatrix_roundtrip_identity", [](TestCase &t)
        {
            VuEnv env;
            float ident[16]{};
            makeIdentity(ident);
            writeMat4(env, kA, ident);
            env.ctx.f[12] = 0.5f;
            SET_GPR_U32(&env.ctx, 4, kDst);
            SET_GPR_U32(&env.ctx, 5, kA);
            ps2_stubs::sceVu0RotMatrixZ(env.rdram.data(), &env.ctx, &env.runtime);
            float rot[16]{};
            readMat4(env, kDst, rot);

            writeMat4(env, kB, rot);
            SET_GPR_U32(&env.ctx, 4, kDst2);
            SET_GPR_U32(&env.ctx, 5, kB);
            ps2_stubs::sceVu0InversMatrix(env.rdram.data(), &env.ctx, &env.runtime);
            float inv[16]{};
            readMat4(env, kDst2, inv);

            writeMat4(env, kC, inv);
            writeMat4(env, kD, rot);
            SET_GPR_U32(&env.ctx, 4, kOut);
            SET_GPR_U32(&env.ctx, 5, kC);
            SET_GPR_U32(&env.ctx, 6, kD);
            ps2_stubs::sceVu0MulMatrix(env.rdram.data(), &env.ctx, &env.runtime);
            float product[16]{};
            readMat4(env, kOut, product);

            float ident2[16]{};
            makeIdentity(ident2);
            bool allMatch = true;
            for (int i = 0; i < 16; ++i)
            {
                if (!nearlyEqual(product[i], ident2[i], 1e-3f))
                {
                    allMatch = false;
                }
            }
            t.IsTrue(allMatch, "R * InversMatrix(R) should be the identity for a pure rotation");
        });

        tc.Run("InversMatrix_roundtrip_rigid", [](TestCase &t)
        {
            // M = rotation (Z by 0.5) with a NONZERO translation row. Its rigid
            // inverse must satisfy M * InversMatrix(M) == I, which exercises the
            // translation term (the pure-rotation roundtrip above leaves it zero).
            VuEnv env;
            float ident[16]{};
            makeIdentity(ident);
            writeMat4(env, kA, ident);
            env.ctx.f[12] = 0.5f;
            SET_GPR_U32(&env.ctx, 4, kDst);
            SET_GPR_U32(&env.ctx, 5, kA);
            ps2_stubs::sceVu0RotMatrixZ(env.rdram.data(), &env.ctx, &env.runtime);
            float M[16]{};
            readMat4(env, kDst, M);
            M[12] = 3.0f;
            M[13] = 7.0f;
            M[14] = -2.0f; // nonzero translation
            writeMat4(env, kB, M);

            SET_GPR_U32(&env.ctx, 4, kDst2);
            SET_GPR_U32(&env.ctx, 5, kB);
            ps2_stubs::sceVu0InversMatrix(env.rdram.data(), &env.ctx, &env.runtime);
            float inv[16]{};
            readMat4(env, kDst2, inv);

            writeMat4(env, kC, M);
            writeMat4(env, kD, inv);
            SET_GPR_U32(&env.ctx, 4, kOut);
            SET_GPR_U32(&env.ctx, 5, kC); // M
            SET_GPR_U32(&env.ctx, 6, kD); // InversMatrix(M)
            ps2_stubs::sceVu0MulMatrix(env.rdram.data(), &env.ctx, &env.runtime);
            float product[16]{};
            readMat4(env, kOut, product);

            float ident2[16]{};
            makeIdentity(ident2);
            bool allMatch = true;
            for (int i = 0; i < 16; ++i)
            {
                if (!nearlyEqual(product[i], ident2[i], 1e-3f))
                {
                    allMatch = false;
                }
            }
            t.IsTrue(allMatch, "M * InversMatrix(M) should be identity for a rotation+translation");
        });

        tc.Run("CameraMatrix_orthonormal_basis", [](TestCase &t)
        {
            VuEnv env;
            writeVec4(env, kA, 0.0f, 0.0f, 0.0f, 1.0f); // eye
            writeVec4(env, kB, 0.0f, 0.0f, 1.0f, 0.0f); // fwd
            writeVec4(env, kC, 0.0f, 1.0f, 0.0f, 0.0f); // up
            SET_GPR_U32(&env.ctx, 4, kDst);
            SET_GPR_U32(&env.ctx, 5, kA);
            SET_GPR_U32(&env.ctx, 6, kB);
            SET_GPR_U32(&env.ctx, 7, kC);
            ps2_stubs::sceVu0CameraMatrix(env.rdram.data(), &env.ctx, &env.runtime);
            float out[16]{};
            readMat4(env, kDst, out);
            float ident[16]{};
            makeIdentity(ident);
            bool allMatch = true;
            for (int i = 0; i < 16; ++i)
            {
                if (!nearlyEqual(out[i], ident[i], 1e-3f))
                {
                    allMatch = false;
                }
            }
            t.IsTrue(allMatch, "CameraMatrix at the origin with a standard basis should be the identity");
        });

        tc.Run("CameraMatrix_nonorigin_eye_maps_to_origin", [](TestCase &t)
        {
            // The world-to-view matrix must map the eye point to the view-space
            // origin. A diagonal fwd gives a NON-symmetric (45-deg Y) rotation, so
            // the translation-row formula is actually exercised: with the wrong
            // (column-dotted) term the eye does NOT map to the origin.
            VuEnv env;
            writeVec4(env, kA, 10.0f, 20.0f, 30.0f, 1.0f); // eye (w=1)
            writeVec4(env, kB, 1.0f, 0.0f, 1.0f, 0.0f);    // fwd (diagonal)
            writeVec4(env, kC, 0.0f, 1.0f, 0.0f, 0.0f);    // up
            SET_GPR_U32(&env.ctx, 4, kDst);
            SET_GPR_U32(&env.ctx, 5, kA);
            SET_GPR_U32(&env.ctx, 6, kB);
            SET_GPR_U32(&env.ctx, 7, kC);
            ps2_stubs::sceVu0CameraMatrix(env.rdram.data(), &env.ctx, &env.runtime);

            // ApplyMatrix(dst, matrix, src) computes src*matrix; eye*view xyz ~ 0.
            SET_GPR_U32(&env.ctx, 4, kOut);
            SET_GPR_U32(&env.ctx, 5, kDst); // the camera (world-to-view) matrix
            SET_GPR_U32(&env.ctx, 6, kA);   // eye
            ps2_stubs::sceVu0ApplyMatrix(env.rdram.data(), &env.ctx, &env.runtime);
            float mapped[4]{};
            readVec4f(env, kOut, mapped);
            t.IsTrue(nearlyEqual(mapped[0], 0.0f, 1e-3f) && nearlyEqual(mapped[1], 0.0f, 1e-3f) &&
                         nearlyEqual(mapped[2], 0.0f, 1e-3f),
                     "CameraMatrix should map the eye point to the view-space origin");
        });

        tc.Run("NormalLightMatrix_neg_normalized", [](TestCase &t)
        {
            VuEnv env;
            writeVec4(env, kA, 1.0f, 0.0f, 0.0f, 0.0f);
            writeVec4(env, kB, 0.0f, 1.0f, 0.0f, 0.0f);
            writeVec4(env, kC, 0.0f, 0.0f, 1.0f, 0.0f);
            SET_GPR_U32(&env.ctx, 4, kDst);
            SET_GPR_U32(&env.ctx, 5, kA);
            SET_GPR_U32(&env.ctx, 6, kB);
            SET_GPR_U32(&env.ctx, 7, kC);
            ps2_stubs::sceVu0NormalLightMatrix(env.rdram.data(), &env.ctx, &env.runtime);
            float out[16]{};
            readMat4(env, kDst, out);
            t.IsTrue(nearlyEqual(out[0], -1.0f), "NormalLightMatrix rows should be normalize(-light)");
        });

        tc.Run("NormalLightMatrix_transposed", [](TestCase &t)
        {
            // Magnitudes (not signs) distinguish rows 6/9 here so this test
            // stays independent of NormalLightMatrix_neg_normalized's sign
            // convention: l1's direction has a 0.8-magnitude z component,
            // l2's a 1.0-magnitude y component. Only a genuine row<->column
            // transpose swaps which magnitude lands at out[6] vs out[9].
            VuEnv env;
            writeVec4(env, kA, 1.0f, 0.0f, 0.0f, 0.0f);
            writeVec4(env, kB, 0.0f, 3.0f, 4.0f, 0.0f);
            writeVec4(env, kC, 0.0f, -1.0f, 0.0f, 0.0f);
            SET_GPR_U32(&env.ctx, 4, kDst);
            SET_GPR_U32(&env.ctx, 5, kA);
            SET_GPR_U32(&env.ctx, 6, kB);
            SET_GPR_U32(&env.ctx, 7, kC);
            ps2_stubs::sceVu0NormalLightMatrix(env.rdram.data(), &env.ctx, &env.runtime);
            float out[16]{};
            readMat4(env, kDst, out);
            t.IsTrue(nearlyEqual(std::fabs(out[6]), 1.0f) && nearlyEqual(std::fabs(out[9]), 0.8f),
                     "NormalLightMatrix should transpose the light-direction rows into columns");
        });

        tc.Run("LightColorMatrix_verbatim_rows", [](TestCase &t)
        {
            VuEnv env;
            writeVec4(env, kA, 1.0f, 2.0f, 3.0f, 4.0f);
            writeVec4(env, kB, 5.0f, 6.0f, 7.0f, 8.0f);
            writeVec4(env, kC, 9.0f, 10.0f, 11.0f, 12.0f);
            writeVec4(env, kD, 13.0f, 14.0f, 15.0f, 16.0f);
            SET_GPR_U32(&env.ctx, 4, kDst);
            SET_GPR_U32(&env.ctx, 5, kA);
            SET_GPR_U32(&env.ctx, 6, kB);
            SET_GPR_U32(&env.ctx, 7, kC);
            SET_GPR_U32(&env.ctx, 8, kD);
            ps2_stubs::sceVu0LightColorMatrix(env.rdram.data(), &env.ctx, &env.runtime);
            float out[16]{};
            readMat4(env, kDst, out);
            t.IsTrue(nearlyEqual(out[4], 5.0f) && nearlyEqual(out[8], 9.0f) && nearlyEqual(out[12], 13.0f),
                     "LightColorMatrix should copy the 4 color vectors verbatim into the 4 rows");
        });

        tc.Run("RotTransPers_apply_persp_ftoi4", [](TestCase &t)
        {
            VuEnv env;
            float ident[16]{};
            makeIdentity(ident);
            writeMat4(env, kA, ident);
            writeVec4(env, kB, 32.0f, 64.0f, 16.0f, 2.0f);
            SET_GPR_U32(&env.ctx, 4, kDst);
            SET_GPR_U32(&env.ctx, 5, kA);
            SET_GPR_U32(&env.ctx, 6, kB);
            SET_GPR_U32(&env.ctx, 7, 1u); // fullFtoi4 = true
            ps2_stubs::sceVu0RotTransPers(env.rdram.data(), &env.ctx, &env.runtime);
            int32_t out[4]{};
            readVec4i(env, kDst, out);
            t.IsTrue(out[0] == 256 && out[1] == 512 && out[2] == 128 && out[3] == 32,
                     "RotTransPers should perspective-divide x/y/z then FTOI4, with w taking the un-divided FTOI4 value");
        });

        tc.Run("RotTransPers_ftoi0_z_when_flag0", [](TestCase &t)
        {
            VuEnv env;
            float ident[16]{};
            makeIdentity(ident);
            writeMat4(env, kA, ident);
            writeVec4(env, kB, 32.0f, 64.0f, 16.0f, 2.0f);
            SET_GPR_U32(&env.ctx, 4, kDst);
            SET_GPR_U32(&env.ctx, 5, kA);
            SET_GPR_U32(&env.ctx, 6, kB);
            SET_GPR_U32(&env.ctx, 7, 0u); // fullFtoi4 = false
            ps2_stubs::sceVu0RotTransPers(env.rdram.data(), &env.ctx, &env.runtime);
            int32_t out[4]{};
            readVec4i(env, kDst, out);
            t.IsTrue(out[2] == 8 && out[3] == 2,
                     "RotTransPers should FTOI0-truncate z/w instead of FTOI4 when fullFtoi4 is clear");
        });

        tc.Run("RotTransPers_persp_wzero_zero", [](TestCase &t)
        {
            VuEnv env;
            float ident[16]{};
            makeIdentity(ident);
            writeMat4(env, kA, ident);
            writeVec4(env, kB, 5.0f, 5.0f, 5.0f, 0.0f);
            SET_GPR_U32(&env.ctx, 4, kDst);
            SET_GPR_U32(&env.ctx, 5, kA);
            SET_GPR_U32(&env.ctx, 6, kB);
            SET_GPR_U32(&env.ctx, 7, 1u);
            ps2_stubs::sceVu0RotTransPers(env.rdram.data(), &env.ctx, &env.runtime);
            int32_t out[4]{};
            readVec4i(env, kDst, out);
            t.IsTrue(out[0] == 0 && out[1] == 0 && out[2] == 0,
                     "RotTransPers should produce finite zero (not Inf/NaN) when w is zero");
        });

        tc.Run("RotTransPers_nonidentity_matrix_apply", [](TestCase &t)
        {
            // The other RotTransPers/RotTransPersN cases feed an identity matrix,
            // which is symmetric and hides the matrix-apply lane indexing. This
            // case uses a NON-symmetric rotation+translation matrix so the per-lane
            // m[4*i+j] dot (v*M, translation in row 3) is actually exercised. The
            // 4th matrix column is (0,0,0,1) so w stays 1 and the perspective divide
            // is exact (q = 1/1 = 1, xyz unchanged). v = (1,2,3,1); hand-derived:
            //   t0 = m0*1 + m4*2 + m8*3  + m12*1 = 2 + 0  + 15 + 10 = 27
            //   t1 = m1*1 + m5*2 + m9*3  + m13*1 = 3 + 2  + 0  + 20 = 25
            //   t2 = m2*1 + m6*2 + m10*3 + m14*1 = 0 + 8  + 3  + 30 = 41
            //   t3 = m3*1 + m7*2 + m11*3 + m15*1 = 1
            // fullFtoi4=true => every lane x16: (432, 400, 656, 16).
            VuEnv env;
            float m[16] = {
                2.0f, 3.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 4.0f, 0.0f,
                5.0f, 0.0f, 1.0f, 0.0f,
                10.0f, 20.0f, 30.0f, 1.0f,
            };
            writeMat4(env, kA, m);
            writeVec4(env, kB, 1.0f, 2.0f, 3.0f, 1.0f);
            SET_GPR_U32(&env.ctx, 4, kDst);
            SET_GPR_U32(&env.ctx, 5, kA);
            SET_GPR_U32(&env.ctx, 6, kB);
            SET_GPR_U32(&env.ctx, 7, 1u); // fullFtoi4 = true
            ps2_stubs::sceVu0RotTransPers(env.rdram.data(), &env.ctx, &env.runtime);
            int32_t out[4]{};
            readVec4i(env, kDst, out);
            t.IsTrue(out[0] == 432 && out[1] == 400 && out[2] == 656 && out[3] == 16,
                     "RotTransPers matrix-apply should dot the vertex with matrix rows (v*M, translation in row 3)");
        });

        tc.Run("RotTransPersN_iterates", [](TestCase &t)
        {
            VuEnv env;
            float ident[16]{};
            makeIdentity(ident);
            writeMat4(env, kA, ident);
            writeVec4(env, kArr, 1.0f, 2.0f, 3.0f, 4.0f);
            writeVec4(env, kArr + 16u, 10.0f, 20.0f, 30.0f, 40.0f);
            SET_GPR_U32(&env.ctx, 4, kDst);
            SET_GPR_U32(&env.ctx, 5, kA);
            SET_GPR_U32(&env.ctx, 6, kArr);
            SET_GPR_U32(&env.ctx, 7, 2u);
            SET_GPR_U32(&env.ctx, 8, 1u);
            ps2_stubs::sceVu0RotTransPersN(env.rdram.data(), &env.ctx, &env.runtime);
            int32_t out0[4]{}, out1[4]{};
            readVec4i(env, kDst, out0);
            readVec4i(env, kDst + 16u, out1);
            t.IsTrue(out0[3] == 64, "RotTransPersN first vertex should use w=4");
            t.IsTrue(out1[3] == 640, "RotTransPersN second vertex should advance the input/output stride by 16 bytes");
        });

        tc.Run("ecossin_cos_sin", [](TestCase &t)
        {
            VuEnv env;
            const float angle = 1.0f;
            env.ctx.f[12] = angle;
            SET_GPR_U32(&env.ctx, 4, kDst);
            ps2_stubs::sceVu0ecossin(env.rdram.data(), &env.ctx, &env.runtime);
            float out[4]{};
            readVec4f(env, kDst, out);
            t.IsTrue(nearlyEqual(out[0], std::cos(angle)) && nearlyEqual(out[1], std::sin(angle)) &&
                         nearlyEqual(out[2], 0.0f) && nearlyEqual(out[3], 0.0f),
                     "ecossin should write {cos(angle), sin(angle), 0, 0}");
        });

        tc.Run("ClipScreen_inside_zero", [](TestCase &t)
        {
            VuEnv env;
            writeVec4(env, kA, 100.0f, 100.0f, 0.0f, 0.0f);
            SET_GPR_U32(&env.ctx, 4, kA);
            ps2_stubs::sceVu0ClipScreen(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegU32(&env.ctx, 2), 0u, "ClipScreen should return 0 for an in-bounds vertex");
        });

        tc.Run("ClipScreen_offscreen_nonzero", [](TestCase &t)
        {
            VuEnv env;
            writeVec4(env, kA, 5000.0f, 100.0f, 0.0f, 0.0f);
            SET_GPR_U32(&env.ctx, 4, kA);
            ps2_stubs::sceVu0ClipScreen(env.rdram.data(), &env.ctx, &env.runtime);
            t.IsTrue(getRegU32(&env.ctx, 2) != 0u, "ClipScreen should return nonzero when x exceeds the guard band");
        });

        tc.Run("ClipScreen_negx_nonzero", [](TestCase &t)
        {
            // Pins the negative-x guard-band clause (v[0] < -kScreenClipGuard => 0x2),
            // the mirror of ClipScreen_offscreen_nonzero's +x clause. Without this
            // case, deleting the negative-x clause leaves the suite green.
            VuEnv env;
            writeVec4(env, kA, -5000.0f, 100.0f, 0.0f, 0.0f);
            SET_GPR_U32(&env.ctx, 4, kA);
            ps2_stubs::sceVu0ClipScreen(env.rdram.data(), &env.ctx, &env.runtime);
            t.IsTrue(getRegU32(&env.ctx, 2) != 0u, "ClipScreen should return nonzero when x is below the negative guard band");
        });

        tc.Run("ClipScreen3_ors_three", [](TestCase &t)
        {
            // Both offscreen vertices trip the y-guard clauses (not x) so this
            // test does not overlap ClipScreen_offscreen_nonzero's x-guard mutation.
            VuEnv env;
            writeVec4(env, kA, 0.0f, 5000.0f, 0.0f, 0.0f);
            writeVec4(env, kB, 0.0f, 0.0f, 0.0f, 0.0f);
            writeVec4(env, kC, 0.0f, -5000.0f, 0.0f, 0.0f);
            SET_GPR_U32(&env.ctx, 4, kA);
            SET_GPR_U32(&env.ctx, 5, kB);
            SET_GPR_U32(&env.ctx, 6, kC);
            ps2_stubs::sceVu0ClipScreen3(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(getRegU32(&env.ctx, 2), 0xCu, "ClipScreen3 should OR the guard-band codes across all 3 vertices");
        });

        tc.Run("ClipAll_all_out_returns_1", [](TestCase &t)
        {
            VuEnv env;
            float ident[16]{};
            makeIdentity(ident);
            writeVec4(env, kA, -1.0f, -1.0f, 0.0f, 0.0f); // lo
            writeVec4(env, kB, 1.0f, 1.0f, 0.0f, 0.0f);   // hi
            writeMat4(env, kC, ident);
            writeVec4(env, kArr, 5.0f, 5.0f, 0.0f, 1.0f);
            writeVec4(env, kArr + 16u, -5.0f, -5.0f, 0.0f, 1.0f);
            SET_GPR_U32(&env.ctx, 4, kA);
            SET_GPR_U32(&env.ctx, 5, kB);
            SET_GPR_U32(&env.ctx, 6, kC);
            SET_GPR_U32(&env.ctx, 7, kArr);
            SET_GPR_U32(&env.ctx, 8, 2u);
            ps2_stubs::sceVu0ClipAll(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(static_cast<int32_t>(getRegU32(&env.ctx, 2)), 1, "ClipAll should return 1 (cull) when every vertex is outside");
        });

        tc.Run("ClipAll_one_in_returns_0", [](TestCase &t)
        {
            VuEnv env;
            float ident[16]{};
            makeIdentity(ident);
            writeVec4(env, kA, -1.0f, -1.0f, 0.0f, 0.0f); // lo
            writeVec4(env, kB, 1.0f, 1.0f, 0.0f, 0.0f);   // hi
            writeMat4(env, kC, ident);
            writeVec4(env, kArr, 5.0f, 5.0f, 0.0f, 1.0f);
            writeVec4(env, kArr + 16u, 0.0f, 0.0f, 0.0f, 1.0f);
            SET_GPR_U32(&env.ctx, 4, kA);
            SET_GPR_U32(&env.ctx, 5, kB);
            SET_GPR_U32(&env.ctx, 6, kC);
            SET_GPR_U32(&env.ctx, 7, kArr);
            SET_GPR_U32(&env.ctx, 8, 2u);
            ps2_stubs::sceVu0ClipAll(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(static_cast<int32_t>(getRegU32(&env.ctx, 2)), 0, "ClipAll should return 0 as soon as one vertex is inside");
        });

        tc.Run("ClipAll_w_scaling_homogeneous", [](TestCase &t)
        {
            VuEnv env;
            float ident[16]{};
            makeIdentity(ident);
            writeVec4(env, kA, -1.0f, -1.0f, 0.0f, 0.0f); // lo
            writeVec4(env, kB, 1.0f, 1.0f, 0.0f, 0.0f);   // hi
            writeMat4(env, kC, ident);
            // Vertex x=2 sits OUTSIDE the raw [-1,1] band but INSIDE the
            // homogeneous band [-w,w] = [-4,4] once scaled by w=4. With the
            // * tw factor this vertex is judged inside -> ClipAll returns 0;
            // without it the vertex is judged outside -> ClipAll returns 1.
            writeVec4(env, kArr, 2.0f, 0.0f, 0.0f, 4.0f);
            SET_GPR_U32(&env.ctx, 4, kA);
            SET_GPR_U32(&env.ctx, 5, kB);
            SET_GPR_U32(&env.ctx, 6, kC);
            SET_GPR_U32(&env.ctx, 7, kArr);
            SET_GPR_U32(&env.ctx, 8, 1u);
            ps2_stubs::sceVu0ClipAll(env.rdram.data(), &env.ctx, &env.runtime);
            t.Equals(static_cast<int32_t>(getRegU32(&env.ctx, 2)), 0,
                     "ClipAll must scale the lo/hi bounds by the vertex w (homogeneous guard planes)");
        });

        tc.Run("ViewScreenMatrix_zscale", [](TestCase &t)
        {
            VuEnv env;
            env.ctx.f[12] = 1.0f; // p0
            env.ctx.f[13] = 1.0f; // p1
            env.ctx.f[14] = 1.0f; // p2
            env.ctx.f[15] = 0.0f; // p3
            env.ctx.f[16] = 0.0f; // p4
            env.ctx.f[17] = 1.0f; // p5
            env.ctx.f[18] = 2.0f; // p6
            env.ctx.f[19] = 100.0f; // p7
            const float p8 = 200.0f;
            std::memcpy(getMemPtr(env.rdram.data(), kSp), &p8, sizeof(p8));
            SET_GPR_U32(&env.ctx, 4, kDst);
            SET_GPR_U32(&env.ctx, 29, kSp);
            ps2_stubs::sceVu0ViewScreenMatrix(env.rdram.data(), &env.ctx, &env.runtime);
            float out[16]{};
            readMat4(env, kDst, out);
            t.IsTrue(nearlyEqual(out[14], 200.0f, 1e-2f), "ViewScreenMatrix zScale should follow the documented formula");
        });

        tc.Run("ViewScreenMatrix_denom_zero", [](TestCase &t)
        {
            VuEnv env;
            env.ctx.f[12] = 1.0f;
            env.ctx.f[13] = 1.0f;
            env.ctx.f[14] = 1.0f;
            env.ctx.f[15] = 0.0f;
            env.ctx.f[16] = 0.0f;
            env.ctx.f[17] = 1.0f;
            env.ctx.f[18] = 2.0f;
            env.ctx.f[19] = 50.0f; // p7 == p8 -> denom == 0
            const float p8 = 50.0f;
            std::memcpy(getMemPtr(env.rdram.data(), kSp), &p8, sizeof(p8));
            SET_GPR_U32(&env.ctx, 4, kDst);
            SET_GPR_U32(&env.ctx, 29, kSp);
            ps2_stubs::sceVu0ViewScreenMatrix(env.rdram.data(), &env.ctx, &env.runtime);
            float out[16]{};
            readMat4(env, kDst, out);
            t.IsTrue(nearlyEqual(out[14], 0.0f) && nearlyEqual(out[10] /*unused row*/, 0.0f),
                     "ViewScreenMatrix should guard the near/far denominator against divide-by-zero");
        });

        tc.Run("DropShadow_directional_mode", [](TestCase &t)
        {
            VuEnv env;
            writeVec4(env, kA, 0.0f, 1.0f, 0.0f, 0.0f); // n
            env.ctx.f[12] = 1.0f; // lx
            env.ctx.f[13] = 0.0f; // ly
            env.ctx.f[14] = 0.0f; // lz
            SET_GPR_U32(&env.ctx, 4, kDst);
            SET_GPR_U32(&env.ctx, 5, kA);
            SET_GPR_U32(&env.ctx, 6, 1u); // mode != 0
            ps2_stubs::sceVu0DropShadowMatrix(env.rdram.data(), &env.ctx, &env.runtime);
            float out[16]{};
            readMat4(env, kDst, out);
            t.IsTrue(nearlyEqual(out[0], 1.0f), "DropShadowMatrix directional mode should include the diagonal (1-d) term");
        });

        tc.Run("DropShadow_point_mode", [](TestCase &t)
        {
            VuEnv env;
            writeVec4(env, kA, 1.0f, 0.0f, 0.0f, 0.0f); // n
            env.ctx.f[12] = 1.0f; // lx
            env.ctx.f[13] = 0.0f; // ly
            env.ctx.f[14] = 0.0f; // lz
            SET_GPR_U32(&env.ctx, 4, kDst);
            SET_GPR_U32(&env.ctx, 5, kA);
            SET_GPR_U32(&env.ctx, 6, 0u); // mode == 0
            ps2_stubs::sceVu0DropShadowMatrix(env.rdram.data(), &env.ctx, &env.runtime);
            float out[16]{};
            readMat4(env, kDst, out);
            t.IsTrue(nearlyEqual(out[0], 0.0f), "DropShadowMatrix point mode should subtract d before scaling by k");
        });
    });
}
