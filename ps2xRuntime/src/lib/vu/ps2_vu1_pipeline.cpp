#include "runtime/ps2_vu1.h"

#include <bit>
#include <cstring>
#if defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace {
constexpr uint8_t laneForComponent(uint32_t component) {
    return static_cast<uint8_t>(1u << (3u - component));
}
template <size_t Size>
uint32_t pipelineCycleMask(uint64_t cycle) {
    static_assert(Size >= 8u && (Size & (Size - 1u)) == 0u);
    return 3u << (2u * (cycle & (Size / 2u - 1u)));
}
}

void VU1Interpreter::commitReadyPipelines()
{
    // The two slots in each bucket preserve issue order for simultaneous flags.
    for (uint32_t active = m_activeFlags & pipelineCycleMask<kMaxFlagEntries>(m_cycle);
         active != 0u; active &= active - 1u)
    {
        const unsigned index = std::countr_zero(active);
        auto &entry = m_flagPipeline[index];
        if (entry.readyCycle > m_cycle)
            continue;

        if (entry.writesMac)
            m_state.mac = entry.mac;
        if (entry.writesStatus)
        {
            const uint32_t current = entry.status & 0xFu;
            m_state.status = (m_state.status & 0xFF0u) | current | ((current | entry.extraSticky) << 6);
        }
        if (entry.writesSticky)
        {
            m_state.status = (m_state.status & 0x03Fu) | (entry.status & 0xFC0u);
        }
        if (entry.writesClip)
            m_state.clip = entry.clip;
        m_activeFlags &= ~(1u << index);
    }

    if (m_fdiv.valid && m_fdiv.readyCycle <= m_cycle)
    {
        m_state.q = m_fdiv.value;
        const uint32_t currentDi = m_fdiv.statusDi & 0x30u;
        m_state.status = (m_state.status & 0xFCFu) | currentDi | (currentDi << 6);
        m_fdiv = {};
    }

    for (ScalarPipelineEntry &entry : m_efu)
    {
        if (entry.valid && entry.readyCycle <= m_cycle)
        {
            m_state.p = entry.value;
            entry = {};
        }
    }

    for (uint32_t active = m_activeStores; active != 0u; active &= active - 1u)
    {
        const unsigned index = std::countr_zero(active);
        auto &store = m_storePipeline[index];
        if (store.readyCycle > m_cycle)
            continue;
        if (m_activeVuData && store.address + 16u <= m_activeVuDataSize)
        {
            uint32_t oldWords[4]{};
            std::memcpy(oldWords, m_activeVuData + store.address, sizeof(oldWords));
            for (uint32_t component = 0; component < 4u; ++component)
            {
                if ((store.laneMask & laneForComponent(component)) != 0u)
                    oldWords[component] = store.words[component];
            }
            std::memcpy(m_activeVuData + store.address, oldWords, sizeof(oldWords));
        }
        m_activeStores &= ~(1u << index);
    }

    for (uint32_t active = m_activeVfWrites & pipelineCycleMask<kMaxPendingVfWrites>(m_cycle);
         active != 0u; active &= active - 1u)
    {
        const unsigned index = std::countr_zero(active);
        auto &write = m_vfWritePipeline[index];
        if (write.readyCycle > m_cycle)
            continue;
#if defined(__aarch64__)
        const auto sequence = vdupq_n_u64(write.sequence);
        const auto *latest = m_vfLatestWrite[write.reg].data();
        const auto matches = vcombine_u32(vmovn_u64(vceqq_u64(vld1q_u64(latest), sequence)),
                                         vmovn_u64(vceqq_u64(vld1q_u64(latest + 2), sequence)));
        const uint32x4_t laneBits = {8u, 4u, 2u, 1u};
        const auto lanes = vtstq_u32(vdupq_n_u32(write.laneMask), laneBits);
        const auto mask = vandq_u32(matches, lanes);
        vst1q_f32(m_state.vf[write.reg], vbslq_f32(mask, vld1q_f32(write.value.data()),
                                                vld1q_f32(m_state.vf[write.reg])));
#else
        for (uint32_t component = 0; component < 4u; ++component)
        {
            if ((write.laneMask & laneForComponent(component)) != 0u &&
                m_vfLatestWrite[write.reg][component] == write.sequence)
            {
                m_state.vf[write.reg][component] = write.value[component];
            }
        }
#endif
        m_activeVfWrites &= ~(1u << index);
    }

    for (uint32_t active = m_activeViWrites & pipelineCycleMask<kMaxPendingViWrites>(m_cycle);
         active != 0u; active &= active - 1u)
    {
        const unsigned index = std::countr_zero(active);
        auto &write = m_viWritePipeline[index];
        if (write.readyCycle > m_cycle)
            continue;
        if (m_viLatestWrite[write.reg] == write.sequence)
            m_state.vi[write.reg] = static_cast<int16_t>(write.value);
        m_activeViWrites &= ~(1u << index);
    }

    for (uint32_t active = m_activeAccWrites; active != 0u; active &= active - 1u)
    {
        const unsigned index = std::countr_zero(active);
        auto &write = m_accWritePipeline[index];
        if (write.readyCycle > m_cycle)
            continue;
        for (uint32_t component = 0; component < 4u; ++component)
        {
            if ((write.laneMask & laneForComponent(component)) != 0u &&
                m_accLatestWrite[component] == write.sequence)
            {
                m_state.acc[component] = write.value[component];
            }
        }
        m_activeAccWrites &= ~(1u << index);
    }
}
