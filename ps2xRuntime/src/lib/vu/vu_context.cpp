#include "runtime/vu/vu_runtime.h"
#include "ps2_runtime.h"
namespace ps2vu
{
    void importContext(const R5900Context &context, VU1State &state) noexcept
    {


        for (uint32_t i = 0; i < 32u; ++i)
        {
            _mm_storeu_ps(state.vf[i], context.vu0_vf[i]);
        }
        for (uint32_t i = 0; i < 16u; ++i)
        {
            state.vi[i] = static_cast<int16_t>(context.vi[i]);
        }

        _mm_storeu_ps(state.acc, context.vu0_acc);
        state.q = context.vu0_q;
        state.p = context.vu0_p;
        state.i = context.vu0_i;
        alignas(16) uint32_t rWords[4]{};
        _mm_storeu_si128(reinterpret_cast<__m128i *>(rWords), _mm_castps_si128(context.vu0_r));
        state.r = 0x3F800000u | (rWords[0] & 0x007FFFFFu);
        state.pc = context.vu0_pc;
        state.mac = context.vu0_mac_flags;
        state.clip = context.vu0_clip_flags;
        state.status = context.vu0_status;
        state.itop = context.vu0_itop;
        state.dBitEnabled = (context.vu0_fbrst & (1u << 2)) != 0u;
        state.tBitEnabled = (context.vu0_fbrst & (1u << 3)) != 0u;

        state.vf[0][0] = 0.0f;
        state.vf[0][1] = 0.0f;
        state.vf[0][2] = 0.0f;
        state.vf[0][3] = 1.0f;
        state.vi[0] = 0;
    }

    void exportContext(const VU1State &state, R5900Context &context) noexcept
    {
        for (uint32_t i = 0; i < 32u; ++i)
        {
            context.vu0_vf[i] = _mm_loadu_ps(state.vf[i]);
        }
        for (uint32_t i = 0; i < 16u; ++i)
        {
            context.vi[i] = static_cast<uint16_t>(state.vi[i]);
        }

        context.vu0_acc = _mm_loadu_ps(state.acc);
        context.vu0_q = state.q;
        context.vu0_p = state.p;
        context.vu0_i = state.i;
        context.vu0_r = _mm_castsi128_ps(_mm_set1_epi32(static_cast<int32_t>(state.r)));
        context.vu0_mac_flags = state.mac;
        context.vu0_clip_flags = state.clip;
        context.vu0_clip_flags2 = state.clip;
        context.vu0_status = static_cast<uint16_t>(state.status);
        context.vu0_itop = state.itop;
        context.vu0_pc = state.pc;
        context.vu0_tpc = state.pc;
        context.vu0_vpu_stat = (context.vu0_vpu_stat & 0xFF00u) | (state.stoppedByD ? (1u << 1) : 0u) | (state.stoppedByT ? (1u << 2) : 0u);
        context.vu0_vpu_stat2 = 0;

        context.vu0_vf[0] = _mm_set_ps(1.0f, 0.0f, 0.0f, 0.0f);
        context.vi[0] = 0;
    }

}
