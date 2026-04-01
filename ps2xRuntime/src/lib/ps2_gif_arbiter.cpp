#include "runtime/ps2_gif_arbiter.h"
#include "ps2_log.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <iostream>

namespace
{
    std::atomic<uint32_t> s_debugGifArbiterSubmitCount{0};
    std::atomic<uint32_t> s_debugGifArbiterDrainCount{0};

    const char *pathName(GifPathId id)
    {
        switch (id)
        {
        case GifPathId::Path1:
            return "path1";
        case GifPathId::Path2:
            return "path2";
        case GifPathId::Path3:
            return "path3";
        default:
            return "path?";
        }
    }
}

GifArbiter::GifArbiter(ProcessPacketFn processFn)
    : m_processFn(std::move(processFn))
{
}

bool GifArbiter::isImagePacket(const uint8_t *data, uint32_t sizeBytes)
{
    if (!data || sizeBytes < 16u)
        return false;

    uint64_t tagLo = 0;
    std::memcpy(&tagLo, data, sizeof(tagLo));
    const uint8_t flg = static_cast<uint8_t>((tagLo >> 58) & 0x3u);
    return flg == 2u;
}

void GifArbiter::submit(GifPathId pathId, const uint8_t *data, uint32_t sizeBytes, bool path2DirectHl)
{
    if (!data || sizeBytes < 16 || !m_processFn)
        return;

    const uint32_t debugIndex = s_debugGifArbiterSubmitCount.fetch_add(1, std::memory_order_relaxed);
    if (debugIndex < 96u)
    {
        uint64_t tagLo = 0;
        std::memcpy(&tagLo, data, sizeof(tagLo));
        const uint32_t nloop = static_cast<uint32_t>(tagLo & 0x7FFFu);
        const uint8_t flg = static_cast<uint8_t>((tagLo >> 58) & 0x3u);
        uint32_t nreg = static_cast<uint32_t>((tagLo >> 60) & 0xFu);
        if (nreg == 0u)
            nreg = 16u;
        RUNTIME_LOG("[gif:submit] idx=" << debugIndex
                                        << " path=" << pathName(pathId)
                                        << " size=" << sizeBytes
                                        << " nloop=" << nloop
                                        << " flg=" << static_cast<uint32_t>(flg)
                                        << " nreg=" << nreg
                                        << " directhl=" << static_cast<uint32_t>(path2DirectHl ? 1u : 0u)
                                        << std::endl);
    }

    GifArbiterPacket pkt;
    pkt.pathId = pathId;
    pkt.path2DirectHl = (pathId == GifPathId::Path2) && path2DirectHl;
    pkt.path3Image = (pathId == GifPathId::Path3) && isImagePacket(data, sizeBytes);
    pkt.data.resize(sizeBytes);
    std::memcpy(pkt.data.data(), data, sizeBytes);
    m_queue.push_back(std::move(pkt));
}

void GifArbiter::drain()
{
    if (!m_processFn)
        return;

    std::stable_sort(m_queue.begin(), m_queue.end(),
                     [](const GifArbiterPacket &a, const GifArbiterPacket &b)
                     {
                         // DIRECTHL cannot preempt PATH3 IMAGE transfers.
                         if (a.path2DirectHl != b.path2DirectHl || a.path3Image != b.path3Image)
                         {
                             if (a.path3Image && b.path2DirectHl)
                                 return true;
                             if (a.path2DirectHl && b.path3Image)
                                 return false;
                         }
                         return pathPriority(a.pathId) < pathPriority(b.pathId);
                     });

    for (size_t i = 0; i < m_queue.size(); ++i)
    {
        auto &pkt = m_queue[i];
        if (!pkt.data.empty())
        {
            const uint32_t debugIndex = s_debugGifArbiterDrainCount.fetch_add(1, std::memory_order_relaxed);
            if (debugIndex < 96u)
            {
                uint64_t tagLo = 0;
                std::memcpy(&tagLo, pkt.data.data(), sizeof(tagLo));
                const uint32_t nloop = static_cast<uint32_t>(tagLo & 0x7FFFu);
                const uint8_t flg = static_cast<uint8_t>((tagLo >> 58) & 0x3u);
                uint32_t nreg = static_cast<uint32_t>((tagLo >> 60) & 0xFu);
                if (nreg == 0u)
                    nreg = 16u;
                RUNTIME_LOG("[gif:drain] idx=" << debugIndex
                                               << " path=" << pathName(pkt.pathId)
                                               << " size=" << pkt.data.size()
                                               << " nloop=" << nloop
                                               << " flg=" << static_cast<uint32_t>(flg)
                                               << " nreg=" << nreg
                                               << " directhl=" << static_cast<uint32_t>(pkt.path2DirectHl ? 1u : 0u)
                                               << " path3image=" << static_cast<uint32_t>(pkt.path3Image ? 1u : 0u)
                                               << std::endl);
            }
            m_processFn(pkt.data.data(), static_cast<uint32_t>(pkt.data.size()));
        }
    }
    m_queue.clear();
}

uint8_t GifArbiter::pathPriority(GifPathId id)
{
    return static_cast<uint8_t>(id);
}
