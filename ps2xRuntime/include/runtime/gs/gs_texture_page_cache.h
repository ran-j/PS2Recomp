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
        // Positions order as a serial draw reads: by primitive, row, column, then texel within the pixel.
        struct ReadPosition
        {
            uint32_t primitive = 0;
            int y = -1;
            int x = -1;
            uint32_t texel = 0;
            bool valid = false;

            bool samePixel(const ReadPosition& other) const noexcept
            {
                return primitive == other.primitive && y == other.y && x == other.x;
            }
            bool after(const ReadPosition& other) const noexcept
            {
                if (!other.valid)
                    return valid;
                if (!valid)
                    return false;
                if (primitive != other.primitive)
                    return primitive > other.primitive;
                if (y != other.y)
                    return y > other.y;
                if (x != other.x)
                    return x > other.x;
                return texel > other.texel;
            }
        };
        struct ReadTrack
        {
            uint32_t lastPage = UINT32_MAX;
            ReadPosition last;
            ReadPosition other;
            bool switchedInsidePixel = false;
        };

        // Starts a band's pass with nothing cached, reading in place, and optionally tracking reads.
        void ResetForBand(bool tracking) noexcept
        {
            Invalidate();
            m_copyOnMiss = false;
            m_tracking = tracking;
            m_track = {};
        }

        void BeginPixel(uint32_t primitive, int y, int x) noexcept
        {
            m_tagPrimitive = primitive;
            m_tagY = y;
            m_tagX = x;
            m_tagTexel = 0u;
        }

        const ReadTrack& Track() const noexcept
        {
            return m_track;
        }

        // What Resolve does for further reads of the page it just resolved: they hit, and are recorded.
        void ResolveAgain(uint32_t count) noexcept
        {
            if (m_tracking)
                for (uint32_t i = 0; i < count; ++i)
                    NoteRead(m_pageBase);
        }

    private:
        void NoteRead(uint32_t page) noexcept
        {
            const ReadPosition position{m_tagPrimitive, m_tagY, m_tagX, m_tagTexel++, true};
            if (page != m_track.lastPage)
            {
                if (m_track.lastPage != UINT32_MAX)
                {
                    m_track.other = m_track.last;
                    m_track.switchedInsidePixel = m_track.last.samePixel(position);
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
        uint32_t m_tagPrimitive = 0u;
        int m_tagY = -1;
        int m_tagX = -1;
        uint32_t m_tagTexel = 0u;
        ReadTrack m_track;
        uint32_t m_copyFirst[2] = {0u, 0u};
        uint32_t m_copyCount[2] = {0u, 0u};
    };

    using TextureFetchFn = uint32_t (*)(TexturePageCache& cache, const uint8_t* data, uint32_t bp, uint32_t bw, uint32_t x, uint32_t y);
    // Reads the 2x2 texels of a bilinear sample in the order (u0,v0) (u1,v0) (u0,v1) (u1,v1).
    using TextureQuadFetchFn = void (*)(TexturePageCache& cache, const uint8_t* data, uint32_t bp, uint32_t bw, uint32_t u0, uint32_t u1,
                                        uint32_t v0, uint32_t v1, uint32_t* out);
}
