#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace GSMem
{
    class TexturePageCache
    {
    public:
        static constexpr uint32_t kPageSize = 8192u;
        static constexpr uint32_t kPageCount = 512u;

        void Invalidate() noexcept
        {
            m_pageBase = UINT32_MAX;
            m_hasCopy = false;
        }

        // byteAddress is the wrapped, swizzled VRAM address. The returned
        // pointer is valid only until the next miss or invalidation.
        //
        // The cached page shows VRAM as it was when the page was loaded. Copying it on every miss
        // dominated textured drawing, so a page is read in place until a writer is about to change it
        // (see LivePage/Snapshot); copyOnMiss restores the eager copy where reads and writes interleave.
        const uint8_t* Resolve(const uint8_t* vram, uint32_t byteAddress) noexcept
        {
            const uint32_t pageBase = byteAddress & ~(kPageSize - 1u);
            if (m_tracking)
                NoteRead(pageBase);
            if (m_pageBase != pageBase)
            {
                m_pageBase = pageBase;
                m_hasCopy = m_copyOnMiss && CopyRangeHas(pageBase / kPageSize);
                if (m_hasCopy)
                    std::memcpy(m_bytes.data(), vram + pageBase, kPageSize);
            }
            return (m_hasCopy ? m_bytes.data() : vram + pageBase) + (byteAddress & (kPageSize - 1u));
        }

        // True when a page is cached but still read in place, i.e. a write to it must snapshot it first.
        bool LivePage(uint32_t& pageBase) const noexcept
        {
            if (m_hasCopy || m_pageBase == UINT32_MAX)
                return false;
            pageBase = m_pageBase;
            return true;
        }

        void Snapshot(const uint8_t* vram) noexcept
        {
            std::memcpy(m_bytes.data(), vram + m_pageBase, kPageSize);
            m_hasCopy = true;
        }

        bool CachedPage(uint32_t& pageBase) const noexcept
        {
            if (m_pageBase == UINT32_MAX)
                return false;
            pageBase = m_pageBase;
            return true;
        }

        bool MatchesVram(const uint8_t* vram) const noexcept
        {
            return !m_hasCopy || std::memcmp(m_bytes.data(), vram + m_pageBase, kPageSize) == 0;
        }

        void MakeLive() noexcept
        {
            m_hasCopy = false;
        }

        void SetLivePage(uint32_t pageBase) noexcept
        {
            m_pageBase = pageBase;
            m_hasCopy = false;
        }

        // While set, pages in the two ranges (page indices, wrapping at kPageCount) are copied when loaded
        // because the current primitive writes them; other pages cannot change meanwhile and stay in place.
        void SetCopyOnMiss(bool copyOnMiss, uint32_t first0 = 0u, uint32_t count0 = kPageCount,
                           uint32_t first1 = 0u, uint32_t count1 = 0u) noexcept
        {
            m_copyOnMiss = copyOnMiss;
            m_copyFirst[0] = first0;
            m_copyCount[0] = count0;
            m_copyFirst[1] = first1;
            m_copyCount[1] = count1;
        }


        // Replaces the cached page with a copy, as if it had been loaded when VRAM held these bytes.
        void SetCopy(const uint8_t* bytes, uint32_t pageBase) noexcept
        {
            std::memcpy(m_bytes.data(), bytes, kPageSize);
            m_pageBase = pageBase;
            m_hasCopy = true;
        }

        // Read tracking for a rasterizer band: remembers the page of the latest texel read and the position of
        // the latest read of any other page, which is where a sequential cache last switched pages.
        struct ReadPosition
        {
            int y = -1;
            int x = -1;
            uint32_t texel = 0;
            bool valid = false;
        };
        struct ReadTrack
        {
            uint32_t lastPage = UINT32_MAX;
            ReadPosition last;
            ReadPosition other;
            bool switchedInsidePixel = false;
        };

        void EnableTracking() noexcept
        {
            m_tracking = true;
            m_track = {};
        }

        void BeginPixel(int y, int x) noexcept
        {
            m_tagY = y;
            m_tagX = x;
            m_tagTexel = 0u;
        }

        const ReadTrack& Track() const noexcept
        {
            return m_track;
        }

    private:
        void NoteRead(uint32_t page) noexcept
        {
            const ReadPosition position{m_tagY, m_tagX, m_tagTexel++, true};
            if (page != m_track.lastPage)
            {
                if (m_track.lastPage != UINT32_MAX)
                {
                    m_track.other = m_track.last;
                    m_track.switchedInsidePixel = m_track.last.y == m_tagY && m_track.last.x == m_tagX;
                }
                m_track.lastPage = page;
            }
            m_track.last = position;
        }

        bool CopyRangeHas(uint32_t page) const noexcept
        {
            for (int i = 0; i < 2; ++i)
                if (((page + kPageCount - m_copyFirst[i]) % kPageCount) < m_copyCount[i])
                    return true;
            return false;
        }

        alignas(64) std::array<uint8_t, kPageSize> m_bytes{};
        uint32_t m_pageBase = UINT32_MAX;
        bool m_hasCopy = false;
        bool m_copyOnMiss = false;
        bool m_tracking = false;
        int m_tagY = -1;
        int m_tagX = -1;
        uint32_t m_tagTexel = 0u;
        ReadTrack m_track;
        uint32_t m_copyFirst[2] = {0u, 0u};
        uint32_t m_copyCount[2] = {0u, 0u};
    };
}
