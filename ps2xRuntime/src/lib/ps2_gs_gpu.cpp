#include "runtime/ps2_gs_gpu.h"
#include "runtime/ps2_gs_common.h"
#include "runtime/ps2_gs_psmct16.h"
#include "runtime/ps2_gs_psmct32.h"
#include "runtime/ps2_gs_psmt4.h"
#include "runtime/ps2_gs_psmt8.h"
#include "ps2_log.h"
#include "ps2_syscalls.h"
#include "runtime/ps2_memory.h"
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <sstream>

namespace
{
    static constexpr uint32_t kDefaultDisplayWidth = 640u;
    static constexpr uint32_t kDefaultDisplayHeight = 448u;
    static constexpr uint32_t kHostFrameWidth = 640u;
    static constexpr uint32_t kHostFrameHeight = 512u;

    uint16_t encodeFramePixelPSMCT16(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        return static_cast<uint16_t>(((r >> 3) & 0x1Fu) |
                                     (((g >> 3) & 0x1Fu) << 5) |
                                     (((b >> 3) & 0x1Fu) << 10) |
                                     ((a >= 0x40u) ? 0x8000u : 0u));
    }

    uint32_t addrPSMCT16Family(uint32_t basePtr, uint32_t width, uint8_t psm, uint32_t x, uint32_t y)
    {
        switch (psm)
        {
        case GS_PSM_CT16:
            return GSPSMCT16::addrPSMCT16(basePtr, width, x, y);
        case GS_PSM_CT16S:
            return GSPSMCT16::addrPSMCT16S(basePtr, width, x, y);
        case GS_PSM_Z16:
            return GSPSMCT16::addrPSMZ16(basePtr, width, x, y);
        case GS_PSM_Z16S:
            return GSPSMCT16::addrPSMZ16S(basePtr, width, x, y);
        default:
            return 0u;
        }
    }

    static inline uint64_t loadLE64(const uint8_t *p)
    {
        uint64_t v;
        std::memcpy(&v, p, 8);
        return v;
    }

    void decodeDisplaySize(uint64_t display64, uint32_t &outWidth, uint32_t &outHeight)
    {
        const uint32_t dx = static_cast<uint32_t>((display64 >> 0) & 0x0FFFu);
        const uint32_t dy = static_cast<uint32_t>((display64 >> 12) & 0x07FFu);
        const uint32_t dw = static_cast<uint32_t>((display64 >> 32) & 0x0FFFu);
        const uint32_t dh = static_cast<uint32_t>((display64 >> 44) & 0x07FFu);
        const uint32_t magh = static_cast<uint32_t>((display64 >> 23) & 0x0Fu);

        outWidth = (dw + 1u) / (magh + 1u);
        outHeight = dh + 1u;
        
        if (outWidth < 64u || outHeight < 64u)
        {
            outWidth = kDefaultDisplayWidth;
            outHeight = kDefaultDisplayHeight;
        }

        outWidth = std::min<uint32_t>(outWidth, kHostFrameWidth);
        outHeight = std::min<uint32_t>(outHeight, kHostFrameHeight);
    }

    GSFrameReg decodeDisplayFrame(uint64_t dispfb64)
    {
        GSFrameReg frame{};
        frame.fbp = static_cast<uint32_t>(dispfb64 & 0x1FFu);
        frame.fbw = static_cast<uint32_t>((dispfb64 >> 9) & 0x3Fu);
        frame.psm = static_cast<uint8_t>((dispfb64 >> 15) & 0x1Fu);
        return frame;
    }

    struct GSDisplayReadOrigin
    {
        uint32_t x = 0u;
        uint32_t y = 0u;
    };

    GSDisplayReadOrigin decodeDisplayReadOrigin(uint64_t dispfb64)
    {
        GSDisplayReadOrigin origin{};
        origin.x = static_cast<uint32_t>((dispfb64 >> 32) & 0x7FFu);
        origin.y = static_cast<uint32_t>((dispfb64 >> 43) & 0x7FFu);
        return origin;
    }

    bool hasDisplaySetup(uint64_t display64, const GSFrameReg &frame)
    {
        const uint32_t dw = static_cast<uint32_t>((display64 >> 32) & 0x0FFFu);
        const uint32_t dh = static_cast<uint32_t>((display64 >> 44) & 0x07FFu);
        const uint32_t magh = static_cast<uint32_t>((display64 >> 23) & 0x0Fu);
        return frame.fbw != 0u || dw != 0u || dh != 0u || magh != 0u;
    }

    struct GSTransferTraversal
    {
        bool reverseX = false;
        bool reverseY = false;
    };

    GSTransferTraversal decodeTransferTraversal(uint8_t dir)
    {
        GSTransferTraversal traversal{};
        switch (dir & 0x3u)
        {
        case 1u:
            traversal.reverseY = true;
            break;
        case 2u:
            traversal.reverseX = true;
            break;
        case 3u:
            traversal.reverseX = true;
            traversal.reverseY = true;
            break;
        default:
            break;
        }
        return traversal;
    }

    uint32_t transferCoord(uint32_t start, uint32_t extent, uint32_t index, bool reverse)
    {
        if (reverse && extent != 0u)
        {
            return start + (extent - 1u - index);
        }
        return start + index;
    }

    struct GSPmodeState
    {
        bool enableCrt1 = false;
        bool enableCrt2 = false;
        bool mmod = false;
        bool amod = false;
        bool slbg = false;
        uint8_t alp = 0u;
    };

    GSPmodeState decodePmode(uint64_t pmode64)
    {
        GSPmodeState pmode{};
        pmode.enableCrt1 = (pmode64 & 0x1ull) != 0ull;
        pmode.enableCrt2 = (pmode64 & 0x2ull) != 0ull;
        pmode.mmod = ((pmode64 >> 5) & 0x1ull) != 0ull;
        pmode.amod = ((pmode64 >> 6) & 0x1ull) != 0ull;
        pmode.slbg = ((pmode64 >> 7) & 0x1ull) != 0ull;
        pmode.alp = static_cast<uint8_t>((pmode64 >> 8) & 0xFFu);
        return pmode;
    }

    struct GSSmode2State
    {
        bool interlaced = false;
        bool frameMode = true;
    };

    GSSmode2State decodeSMode2(uint64_t smode264)
    {
        GSSmode2State smode2{};
        smode2.interlaced = (smode264 & 0x1ull) != 0ull;
        smode2.frameMode = ((smode264 >> 1) & 0x1ull) != 0ull;
        return smode2;
    }

    void applyFieldPresentation(std::vector<uint8_t> &pixels, uint32_t width, uint32_t height, bool oddField)
    {
        if (pixels.empty() || width == 0u || height < 2u)
        {
            return;
        }

        const std::vector<uint8_t> source = pixels;
        for (uint32_t y = 0; y < height; ++y)
        {
            uint32_t sourceY = ((y >> 1u) << 1u) + (oddField ? 1u : 0u);
            if (sourceY >= height)
            {
                sourceY = height - 1u;
            }

            const uint8_t *srcRow = source.data() + (sourceY * kHostFrameWidth * 4u);
            uint8_t *dstRow = pixels.data() + (y * kHostFrameWidth * 4u);
            std::memcpy(dstRow, srcRow, width * 4u);
        }
    }

    void normalizePresentationAlpha(std::vector<uint8_t> &pixels, uint32_t width, uint32_t height)
    {
        if (pixels.empty() || width == 0u || height == 0u)
        {
            return;
        }

        for (uint32_t y = 0; y < height; ++y)
        {
            uint8_t *row = pixels.data() + (y * kHostFrameWidth * 4u);
            for (uint32_t x = 0; x < width; ++x)
            {
                row[x * 4u + 3u] = 255u;
            }
        }
    }

    uint8_t blendPresentationChannel(uint8_t src, uint8_t dst, uint32_t factor)
    {
        const int delta = static_cast<int>(src) - static_cast<int>(dst);
        return GSInternal::clampU8(static_cast<int>(dst) + ((delta * static_cast<int>(factor)) / 255));
    }

    uint32_t countNonBlackPixels(const std::vector<uint8_t> &pixels, uint32_t width, uint32_t height)
    {
        uint32_t count = 0u;
        for (uint32_t y = 0; y < height; ++y)
        {
            const uint8_t *row = pixels.data() + (y * kHostFrameWidth * 4u);
            for (uint32_t x = 0; x < width; ++x)
            {
                const uint8_t r = row[x * 4u + 0u];
                const uint8_t g = row[x * 4u + 1u];
                const uint8_t b = row[x * 4u + 2u];
                if (r != 0u || g != 0u || b != 0u)
                {
                    ++count;
                }
            }
        }
        return count;
    }

    bool clearFramebufferRect(uint8_t *vram,
                              uint32_t vramSize,
                              const GSContext &ctx,
                              uint32_t rgba)
    {
        if (!vram || vramSize == 0u || ctx.frame.fbw == 0u)
        {
            return false;
        }

        const uint32_t stride = GSInternal::fbStride(ctx.frame.fbw, ctx.frame.psm);
        if (stride == 0u)
        {
            return false;
        }

        const int x0 = std::max<int>(0, ctx.scissor.x0);
        const int x1 = std::max<int>(x0, ctx.scissor.x1);
        const int y0 = std::max<int>(0, ctx.scissor.y0);
        const int y1 = std::max<int>(y0, ctx.scissor.y1);
        const uint32_t base = ctx.frame.fbp * 8192u;

        uint8_t r = static_cast<uint8_t>(rgba & 0xFFu);
        uint8_t g = static_cast<uint8_t>((rgba >> 8) & 0xFFu);
        uint8_t b = static_cast<uint8_t>((rgba >> 16) & 0xFFu);
        uint8_t a = static_cast<uint8_t>((rgba >> 24) & 0xFFu);
        if ((ctx.fba & 0x1ull) != 0ull && ctx.frame.psm != GS_PSM_CT24)
        {
            a = static_cast<uint8_t>(a | 0x80u);
        }

        if (ctx.frame.psm == GS_PSM_CT32 || ctx.frame.psm == GS_PSM_CT24)
        {
            const uint32_t srcPixel =
                static_cast<uint32_t>(r) |
                (static_cast<uint32_t>(g) << 8) |
                (static_cast<uint32_t>(b) << 16) |
                (static_cast<uint32_t>(a) << 24);
            const uint32_t widthBlocks = (ctx.frame.fbw != 0u) ? ctx.frame.fbw : 1u;

            for (int y = y0; y <= y1; ++y)
            {
                for (int x = x0; x <= x1; ++x)
                {
                    const uint32_t off =
                        GSPSMCT32::addrPSMCT32(GSInternal::framePageBaseToBlock(ctx.frame.fbp),
                                               widthBlocks,
                                               static_cast<uint32_t>(x),
                                               static_cast<uint32_t>(y));
                    if (off + 4u > vramSize)
                    {
                        return true;
                    }

                    uint32_t pixel = srcPixel;
                    if (ctx.frame.fbmsk != 0u)
                    {
                        uint32_t existing = 0u;
                        std::memcpy(&existing, vram + off, sizeof(existing));
                        pixel = (pixel & ~ctx.frame.fbmsk) | (existing & ctx.frame.fbmsk);
                    }
                    std::memcpy(vram + off, &pixel, sizeof(pixel));
                }
            }
            return true;
        }

        if (ctx.frame.psm == GS_PSM_CT16 || ctx.frame.psm == GS_PSM_CT16S)
        {
            const uint16_t srcPixel = encodeFramePixelPSMCT16(r, g, b, a);
            const uint16_t mask = static_cast<uint16_t>(ctx.frame.fbmsk & 0xFFFFu);
            const uint32_t widthBlocks = (ctx.frame.fbw != 0u) ? ctx.frame.fbw : 1u;
            const uint32_t basePtr = GSInternal::framePageBaseToBlock(ctx.frame.fbp);

            for (int y = y0; y <= y1; ++y)
            {
                for (int x = x0; x <= x1; ++x)
                {
                    const uint32_t off = addrPSMCT16Family(basePtr,
                                                           widthBlocks,
                                                           ctx.frame.psm,
                                                           static_cast<uint32_t>(x),
                                                           static_cast<uint32_t>(y));
                    if (off + 2u > vramSize)
                    {
                        return true;
                    }

                    uint16_t pixel = srcPixel;
                    if (mask != 0u)
                    {
                        uint16_t existing = 0u;
                        std::memcpy(&existing, vram + off, sizeof(existing));
                        pixel = static_cast<uint16_t>((pixel & ~mask) | (existing & mask));
                    }
                    std::memcpy(vram + off, &pixel, sizeof(pixel));
                }
            }
            return true;
        }

        return false;
    }

    std::atomic<uint32_t> s_debugGifPacketCount{0};
    std::atomic<uint32_t> s_debugGsRegisterCount{0};
    std::atomic<uint32_t> s_debugGsPackedVertexCount{0};
    std::atomic<uint32_t> s_debugGsVertexKickCount{0};
    std::atomic<uint32_t> s_debugCopyRegCount{0};
    std::atomic<uint32_t> s_debugTexaWriteCount{0};
    std::atomic<uint32_t> s_debugCvFontUploadCount{0};
    std::atomic<uint32_t> s_debugLocalCopyCount{0};

    bool supportsFormatAwareLocalCopy(uint8_t psm)
    {
        switch (psm)
        {
        case GS_PSM_CT32:
        case GS_PSM_Z32:
        case GS_PSM_CT24:
        case GS_PSM_Z24:
        case GS_PSM_CT16:
        case GS_PSM_CT16S:
        case GS_PSM_Z16:
        case GS_PSM_Z16S:
        case GS_PSM_T8:
        case GS_PSM_T4:
            return true;
        default:
            return false;
        }
    }

    uint32_t readTransferPixel(const uint8_t *vram,
                               uint32_t vramSize,
                               uint32_t basePtr,
                               uint8_t widthBlocks,
                               uint8_t psm,
                               uint32_t x,
                               uint32_t y)
    {
        const uint32_t width = (widthBlocks != 0u) ? static_cast<uint32_t>(widthBlocks) : 1u;
        const uint32_t base = basePtr * 256u;

        switch (psm)
        {
        case GS_PSM_CT32:
        case GS_PSM_Z32:
        {
            const uint32_t off = GSPSMCT32::addrPSMCT32(basePtr, width, x, y);
            if (off + 4u > vramSize)
                return 0u;
            uint32_t value = 0u;
            std::memcpy(&value, vram + off, sizeof(value));
            return value;
        }
        case GS_PSM_CT24:
        case GS_PSM_Z24:
        {
            const uint32_t off = GSPSMCT32::addrPSMCT32(basePtr, width, x, y);
            if (off + 3u > vramSize)
                return 0u;
            return static_cast<uint32_t>(vram[off + 0u]) |
                   (static_cast<uint32_t>(vram[off + 1u]) << 8) |
                   (static_cast<uint32_t>(vram[off + 2u]) << 16);
        }
        case GS_PSM_CT16:
        case GS_PSM_CT16S:
        case GS_PSM_Z16:
        case GS_PSM_Z16S:
        {
            const uint32_t off = addrPSMCT16Family(basePtr, width, psm, x, y);
            if (off + 2u > vramSize)
                return 0u;
            uint16_t value = 0u;
            std::memcpy(&value, vram + off, sizeof(value));
            return value;
        }
        case GS_PSM_T8:
        {
            const uint32_t off = GSPSMT8::addrPSMT8(basePtr, width, x, y);
            return (off < vramSize) ? vram[off] : 0u;
        }
        case GS_PSM_T4:
        {
            const uint32_t nibbleAddr = GSPSMT4::addrPSMT4(basePtr, width, x, y);
            const uint32_t byteOff = nibbleAddr >> 1;
            if (byteOff >= vramSize)
                return 0u;
            const int shift = static_cast<int>((nibbleAddr & 1u) << 2);
            return static_cast<uint32_t>((vram[byteOff] >> shift) & 0x0Fu);
        }
        default:
            return 0u;
        }
    }

    void writeTransferPixel(uint8_t *vram,
                            uint32_t vramSize,
                            uint32_t basePtr,
                            uint8_t widthBlocks,
                            uint8_t psm,
                            uint32_t x,
                            uint32_t y,
                            uint32_t value)
    {
        const uint32_t width = (widthBlocks != 0u) ? static_cast<uint32_t>(widthBlocks) : 1u;
        const uint32_t base = basePtr * 256u;

        switch (psm)
        {
        case GS_PSM_CT32:
        case GS_PSM_Z32:
        {
            const uint32_t off = GSPSMCT32::addrPSMCT32(basePtr, width, x, y);
            if (off + 4u > vramSize)
                return;
            std::memcpy(vram + off, &value, sizeof(value));
            return;
        }
        case GS_PSM_CT24:
        case GS_PSM_Z24:
        {
            const uint32_t off = GSPSMCT32::addrPSMCT32(basePtr, width, x, y);
            if (off + 3u > vramSize)
                return;
            vram[off + 0u] = static_cast<uint8_t>(value & 0xFFu);
            vram[off + 1u] = static_cast<uint8_t>((value >> 8) & 0xFFu);
            vram[off + 2u] = static_cast<uint8_t>((value >> 16) & 0xFFu);
            return;
        }
        case GS_PSM_CT16:
        case GS_PSM_CT16S:
        case GS_PSM_Z16:
        case GS_PSM_Z16S:
        {
            const uint32_t off = addrPSMCT16Family(basePtr, width, psm, x, y);
            if (off + 2u > vramSize)
                return;
            const uint16_t value16 = static_cast<uint16_t>(value & 0xFFFFu);
            std::memcpy(vram + off, &value16, sizeof(value16));
            return;
        }
        case GS_PSM_T8:
        {
            const uint32_t off = GSPSMT8::addrPSMT8(basePtr, width, x, y);
            if (off < vramSize)
                vram[off] = static_cast<uint8_t>(value & 0xFFu);
            return;
        }
        case GS_PSM_T4:
        {
            const uint32_t nibbleAddr = GSPSMT4::addrPSMT4(basePtr, width, x, y);
            const uint32_t byteOff = nibbleAddr >> 1;
            if (byteOff >= vramSize)
                return;
            const uint8_t nibble = static_cast<uint8_t>(value & 0x0Fu);
            uint8_t &dst = vram[byteOff];
            if ((nibbleAddr & 1u) != 0u)
                dst = static_cast<uint8_t>((dst & 0x0Fu) | (nibble << 4));
            else
                dst = static_cast<uint8_t>((dst & 0xF0u) | nibble);
            return;
        }
        default:
            return;
        }
    }
}

using namespace GSInternal;

GS::GS()
{
    reset();
}

void GS::init(uint8_t *vram, uint32_t vramSize, GSRegisters *privRegs)
{
    m_vram = vram;
    m_vramSize = vramSize;
    m_privRegs = privRegs;
    reset();
}

void GS::reset()
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    std::memset(m_ctx, 0, sizeof(m_ctx));
    m_prim = {};
    m_curR = 0x80;
    m_curG = 0x80;
    m_curB = 0x80;
    m_curA = 0x80;
    m_curQ = 1.0f;
    m_curS = 0.0f;
    m_curT = 0.0f;
    m_curU = 0;
    m_curV = 0;
    m_curFog = 0;
    m_prmodecont = true;
    m_pabe = false;
    m_bitbltbuf = {};
    m_trxpos = {};
    m_trxreg = {};
    m_trxdir = 3;
    m_hwregX = 0;
    m_hwregY = 0;
    m_vtxCount = 0;
    m_vtxIndex = 0;
    m_localToHostBuffer.clear();
    m_localToHostReadPos = 0;
    m_preferredDisplaySourceFrame = {};
    m_preferredDisplayDestFbp = 0;
    m_hasPreferredDisplaySource = false;
    m_hostPresentationFrame.clear();
    m_hostPresentationWidth = 0u;
    m_hostPresentationHeight = 0u;
    m_hostPresentationDisplayFbp = 0u;
    m_hostPresentationSourceFbp = 0u;
    m_hostPresentationUsedPreferred = false;
    m_hasHostPresentationFrame = false;

    for (int i = 0; i < 2; ++i)
    {
        m_ctx[i].frame.fbw = 10;
        m_ctx[i].scissor = {0, 639, 0, 447};
        m_ctx[i].xyoffset = {0, 0};
    }
}

GSContext &GS::activeContext()
{
    return m_ctx[m_prim.ctxt ? 1 : 0];
}

void GS::snapshotVRAM()
{
    std::lock_guard<std::recursive_mutex> stateLock(m_stateMutex);
    if (!m_vram || m_vramSize == 0)
        return;
    std::lock_guard<std::mutex> lock(m_snapshotMutex);
    m_displaySnapshot.resize(m_vramSize);
    std::memcpy(m_displaySnapshot.data(), m_vram, m_vramSize);
}

const uint8_t *GS::lockDisplaySnapshot(uint32_t &outSize)
{
    m_snapshotMutex.lock();
    if (m_displaySnapshot.empty())
    {
        outSize = 0;
        return nullptr;
    }

    outSize = static_cast<uint32_t>(m_displaySnapshot.size());
    return m_displaySnapshot.data();
}

bool GS::getPreferredDisplaySource(GSFrameReg &outSource, uint32_t &outDestFbp) const
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    if (!m_hasPreferredDisplaySource)
    {
        outSource = {};
        outDestFbp = 0u;
        return false;
    }

    outSource = m_preferredDisplaySourceFrame;
    outDestFbp = m_preferredDisplayDestFbp;
    return true;
}

void GS::unlockDisplaySnapshot()
{
    m_snapshotMutex.unlock();
}

uint32_t GS::getLastDisplayBaseBytes() const
{
    return m_lastDisplayBaseBytes;
}

void GS::refreshDisplaySnapshot()
{
    snapshotVRAM();
}

bool GS::copyFrameToHostRgbaUnlocked(const GSFrameReg &frame,
                                     uint32_t width,
                                     uint32_t height,
                                     std::vector<uint8_t> &outPixels,
                                     bool preserveAlpha,
                                     bool useLocalMemoryLayout,
                                     bool frameBaseIsPages,
                                     uint32_t sourceOriginX,
                                     uint32_t sourceOriginY) const
{
    if (!m_vram || m_vramSize == 0u)
    {
        return false;
    }

    outPixels.assign(kHostFrameWidth * kHostFrameHeight * 4u, 0u);

    const uint32_t baseBytes = frameBaseIsPages ? (frame.fbp * 8192u) : (frame.fbp * 256u);
    const uint32_t basePtr = frameBaseIsPages ? GSInternal::framePageBaseToBlock(frame.fbp) : frame.fbp;
    const uint32_t fbwBlocks = frame.fbw ? frame.fbw : (kHostFrameWidth / 64u);
    const uint32_t bytesPerPixel = (frame.psm == GS_PSM_CT16 || frame.psm == GS_PSM_CT16S) ? 2u : 4u;
    const uint32_t strideBytes = fbwBlocks * 64u * bytesPerPixel;

    if (frame.psm == GS_PSM_CT32 || frame.psm == GS_PSM_CT24)
    {
        const uint32_t srcPixelBytes = (frame.psm == GS_PSM_CT24) ? 3u : 4u;
        if (useLocalMemoryLayout)
        {
            for (uint32_t y = 0; y < height; ++y)
            {
                uint8_t *dstRow = outPixels.data() + (y * kHostFrameWidth * 4u);
                for (uint32_t x = 0; x < width; ++x)
                {
                    const uint32_t srcX = sourceOriginX + x;
                    const uint32_t srcY = sourceOriginY + y;
                    const uint32_t srcOff = GSPSMCT32::addrPSMCT32(basePtr, fbwBlocks, srcX, srcY);
                    if (srcOff + srcPixelBytes > m_vramSize)
                    {
                        return false;
                    }

                    dstRow[x * 4u + 0u] = m_vram[srcOff + 0u];
                    dstRow[x * 4u + 1u] = m_vram[srcOff + 1u];
                    dstRow[x * 4u + 2u] = m_vram[srcOff + 2u];
                    dstRow[x * 4u + 3u] =
                        (preserveAlpha && frame.psm != GS_PSM_CT24) ? m_vram[srcOff + 3u] : 255u;
                }
            }
            return true;
        }

        for (uint32_t y = 0; y < height; ++y)
        {
            const uint32_t dstOff = y * kHostFrameWidth * 4u;
            uint8_t *dstRow = outPixels.data() + dstOff;
            for (uint32_t x = 0; x < width; ++x)
            {
                const uint32_t srcX = sourceOriginX + x;
                const uint32_t srcY = sourceOriginY + y;
                const uint32_t srcOff = baseBytes + (srcY * strideBytes) + (srcX * srcPixelBytes);
                if (srcOff + srcPixelBytes > m_vramSize)
                {
                    return false;
                }

                dstRow[x * 4u + 0u] = m_vram[srcOff + 0u];
                dstRow[x * 4u + 1u] = m_vram[srcOff + 1u];
                dstRow[x * 4u + 2u] = m_vram[srcOff + 2u];
                dstRow[x * 4u + 3u] =
                    (preserveAlpha && frame.psm != GS_PSM_CT24) ? m_vram[srcOff + 3u] : 255u;
            }
        }
        return true;
    }

    if (frame.psm == GS_PSM_CT16 || frame.psm == GS_PSM_CT16S)
    {
        if (useLocalMemoryLayout)
        {
            for (uint32_t y = 0; y < height; ++y)
            {
                const uint32_t dstOff = y * kHostFrameWidth * 4u;
                uint8_t *dst = outPixels.data() + dstOff;
                for (uint32_t x = 0; x < width; ++x)
                {
                    const uint32_t srcX = sourceOriginX + x;
                    const uint32_t srcY = sourceOriginY + y;
                    const uint32_t srcOff = addrPSMCT16Family(basePtr, fbwBlocks, frame.psm, srcX, srcY);
                    if (srcOff + sizeof(uint16_t) > m_vramSize)
                    {
                        return false;
                    }

                    uint16_t pixel = 0u;
                    std::memcpy(&pixel, m_vram + srcOff, sizeof(pixel));
                    const uint32_t r = pixel & 31u;
                    const uint32_t g = (pixel >> 5) & 31u;
                    const uint32_t b = (pixel >> 10) & 31u;
                    dst[x * 4u + 0u] = static_cast<uint8_t>((r << 3) | (r >> 2));
                    dst[x * 4u + 1u] = static_cast<uint8_t>((g << 3) | (g >> 2));
                    dst[x * 4u + 2u] = static_cast<uint8_t>((b << 3) | (b >> 2));
                    dst[x * 4u + 3u] = preserveAlpha ? ((pixel & 0x8000u) ? 0x80u : 0x00u) : 255u;
                }
            }
            return true;
        }

        for (uint32_t y = 0; y < height; ++y)
        {
            const uint32_t dstOff = y * kHostFrameWidth * 4u;
            uint8_t *dst = outPixels.data() + dstOff;
            for (uint32_t x = 0; x < width; ++x)
            {
                const uint32_t srcX = sourceOriginX + x;
                const uint32_t srcY = sourceOriginY + y;
                const uint32_t srcOff = baseBytes + (srcY * strideBytes) + (srcX * 2u);
                if (srcOff + sizeof(uint16_t) > m_vramSize)
                {
                    return false;
                }

                uint16_t pixel = 0u;
                std::memcpy(&pixel, m_vram + srcOff, sizeof(pixel));
                const uint32_t r = pixel & 31u;
                const uint32_t g = (pixel >> 5) & 31u;
                const uint32_t b = (pixel >> 10) & 31u;
                dst[x * 4u + 0u] = static_cast<uint8_t>((r << 3) | (r >> 2));
                dst[x * 4u + 1u] = static_cast<uint8_t>((g << 3) | (g >> 2));
                dst[x * 4u + 2u] = static_cast<uint8_t>((b << 3) | (b >> 2));
                dst[x * 4u + 3u] = preserveAlpha ? ((pixel & 0x8000u) ? 0x80u : 0x00u) : 255u;
            }
        }
        return true;
    }

    return false;
}

void GS::latchHostPresentationFrame()
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);

    if (!m_privRegs || !m_vram || m_vramSize == 0u)
    {
        m_hostPresentationFrame.clear();
        m_hostPresentationWidth = 0u;
        m_hostPresentationHeight = 0u;
        m_hostPresentationDisplayFbp = 0u;
        m_hostPresentationSourceFbp = 0u;
        m_hostPresentationUsedPreferred = false;
        m_hasHostPresentationFrame = false;
        return;
    }

    const GSPmodeState pmode = decodePmode(m_privRegs->pmode);
    const GSSmode2State smode2 = decodeSMode2(m_privRegs->smode2);
    const bool applyFieldMode = smode2.interlaced && !smode2.frameMode;
    const bool oddField = (ps2_syscalls::GetCurrentVSyncTick() & 1ull) != 0ull;
    const GSFrameReg displayFrame1 = decodeDisplayFrame(m_privRegs->dispfb1);
    const GSFrameReg displayFrame2 = decodeDisplayFrame(m_privRegs->dispfb2);
    const GSDisplayReadOrigin displayOrigin1 = decodeDisplayReadOrigin(m_privRegs->dispfb1);
    const GSDisplayReadOrigin displayOrigin2 = decodeDisplayReadOrigin(m_privRegs->dispfb2);

    uint32_t width1 = 0u;
    uint32_t height1 = 0u;
    uint32_t width2 = 0u;
    uint32_t height2 = 0u;
    decodeDisplaySize(m_privRegs->display1, width1, height1);
    decodeDisplaySize(m_privRegs->display2, width2, height2);

    const bool validCrt1 = pmode.enableCrt1 && hasDisplaySetup(m_privRegs->display1, displayFrame1);
    const bool validCrt2 = pmode.enableCrt2 && hasDisplaySetup(m_privRegs->display2, displayFrame2);

    auto copyDisplaySource = [&](const GSFrameReg &displayFrame,
                                 const GSDisplayReadOrigin &displayOrigin,
                                 uint32_t width,
                                 uint32_t height,
                                 bool allowPreferred,
                                 bool preserveAlpha,
                                 GSFrameReg &selectedFrame,
                                 std::vector<uint8_t> &scratch,
                                 bool &usedPreferred) -> bool
    {
        selectedFrame = displayFrame;
        scratch.clear();
        usedPreferred = false;

        if (allowPreferred &&
            m_hasPreferredDisplaySource &&
            m_preferredDisplayDestFbp == displayFrame.fbp &&
            (m_preferredDisplaySourceFrame.fbw != 0u || m_preferredDisplaySourceFrame.fbp != displayFrame.fbp))
        {
            if (copyFrameToHostRgbaUnlocked(m_preferredDisplaySourceFrame,
                                           width,
                                           height,
                                           scratch,
                                           preserveAlpha,
                                           true,
                                           false,
                                           0u,
                                           0u))
            {
                selectedFrame = m_preferredDisplaySourceFrame;
                usedPreferred = true;
            }
        }

        if (scratch.empty() &&
            !copyFrameToHostRgbaUnlocked(displayFrame,
                                         width,
                                         height,
                                         scratch,
                                         preserveAlpha,
                                         true,
                                         true,
                                         displayOrigin.x,
                                         displayOrigin.y))
        {
            return false;
        }

        if (!usedPreferred && displayFrame.fbp == 0u && countNonBlackPixels(scratch, width, height) == 0u)
        {
            for (int contextIndex = 0; contextIndex < 2; ++contextIndex)
            {
                const GSFrameReg &candidate = m_ctx[contextIndex].frame;
                if (candidate.fbp == selectedFrame.fbp &&
                    candidate.fbw == selectedFrame.fbw &&
                    candidate.psm == selectedFrame.psm)
                {
                    continue;
                }

                std::vector<uint8_t> candidatePixels;
                if (!copyFrameToHostRgbaUnlocked(candidate,
                                                width,
                                                height,
                                                candidatePixels,
                                                preserveAlpha,
                                                true,
                                                true,
                                                0u,
                                                0u))
                {
                    continue;
                }

                if (countNonBlackPixels(candidatePixels, width, height) == 0u)
                {
                    continue;
                }

                selectedFrame = candidate;
                scratch.swap(candidatePixels);
                break;
            }
        }

        return true;
    };

    if (!validCrt1 && !validCrt2)
    {
        m_hostPresentationFrame.clear();
        m_hostPresentationWidth = 0u;
        m_hostPresentationHeight = 0u;
        m_hostPresentationDisplayFbp = 0u;
        m_hostPresentationSourceFbp = 0u;
        m_hostPresentationUsedPreferred = false;
        m_hasHostPresentationFrame = false;
        return;
    }

    if (validCrt1 && validCrt2)
    {
        GSFrameReg selectedFrame1{};
        GSFrameReg selectedFrame2{};
        std::vector<uint8_t> rc1;
        std::vector<uint8_t> rc2;
        bool usedPreferred1 = false;
        bool usedPreferred2 = false;

        const bool copiedCrt1 = copyDisplaySource(displayFrame1, displayOrigin1, width1, height1, false, true, selectedFrame1, rc1, usedPreferred1);
        const bool copiedCrt2 = copyDisplaySource(displayFrame2, displayOrigin2, width2, height2, false, true, selectedFrame2, rc2, usedPreferred2);

        if (copiedCrt1 && copiedCrt2)
        {
            const uint32_t width = std::max(width1, width2);
            const uint32_t height = std::max(height1, height2);
            const uint8_t bgR = static_cast<uint8_t>(m_privRegs->bgcolor & 0xFFu);
            const uint8_t bgG = static_cast<uint8_t>((m_privRegs->bgcolor >> 8) & 0xFFu);
            const uint8_t bgB = static_cast<uint8_t>((m_privRegs->bgcolor >> 16) & 0xFFu);
            const uint8_t bgA = pmode.alp;

            std::vector<uint8_t> merged(kHostFrameWidth * kHostFrameHeight * 4u, 0u);
            for (uint32_t y = 0; y < height; ++y)
            {
                uint8_t *dstRow = merged.data() + (y * kHostFrameWidth * 4u);
                for (uint32_t x = 0; x < width; ++x)
                {
                    dstRow[x * 4u + 0u] = bgR;
                    dstRow[x * 4u + 1u] = bgG;
                    dstRow[x * 4u + 2u] = bgB;
                    dstRow[x * 4u + 3u] = bgA;
                }
            }

            if (!pmode.slbg)
            {
                for (uint32_t y = 0; y < height2; ++y)
                {
                    const uint8_t *srcRow = rc2.data() + (y * kHostFrameWidth * 4u);
                    uint8_t *dstRow = merged.data() + (y * kHostFrameWidth * 4u);
                    for (uint32_t x = 0; x < width2; ++x)
                    {
                        dstRow[x * 4u + 0u] = srcRow[x * 4u + 0u];
                        dstRow[x * 4u + 1u] = srcRow[x * 4u + 1u];
                        dstRow[x * 4u + 2u] = srcRow[x * 4u + 2u];
                        dstRow[x * 4u + 3u] = srcRow[x * 4u + 3u];
                    }
                }
            }

            for (uint32_t y = 0; y < height1; ++y)
            {
                const uint8_t *srcRow = rc1.data() + (y * kHostFrameWidth * 4u);
                uint8_t *dstRow = merged.data() + (y * kHostFrameWidth * 4u);
                for (uint32_t x = 0; x < width1; ++x)
                {
                    const uint8_t srcR = srcRow[x * 4u + 0u];
                    const uint8_t srcG = srcRow[x * 4u + 1u];
                    const uint8_t srcB = srcRow[x * 4u + 2u];
                    const uint8_t srcA = srcRow[x * 4u + 3u];
                    const uint8_t dstR = dstRow[x * 4u + 0u];
                    const uint8_t dstG = dstRow[x * 4u + 1u];
                    const uint8_t dstB = dstRow[x * 4u + 2u];
                    const uint8_t dstA = dstRow[x * 4u + 3u];
                    const uint32_t factor = pmode.mmod
                                                ? static_cast<uint32_t>(pmode.alp)
                                                : std::min<uint32_t>(255u, static_cast<uint32_t>(srcA) * 2u);

                    dstRow[x * 4u + 0u] = blendPresentationChannel(srcR, dstR, factor);
                    dstRow[x * 4u + 1u] = blendPresentationChannel(srcG, dstG, factor);
                    dstRow[x * 4u + 2u] = blendPresentationChannel(srcB, dstB, factor);
                    dstRow[x * 4u + 3u] = pmode.amod ? dstA : srcA;
                }
            }

            for (uint32_t y = 0; y < height; ++y)
            {
                uint8_t *row = merged.data() + (y * kHostFrameWidth * 4u);
                for (uint32_t x = 0; x < width; ++x)
                {
                    row[x * 4u + 3u] = 255u;
                }
            }

            if (applyFieldMode)
            {
                applyFieldPresentation(merged, width, height, oddField);
            }

            m_hostPresentationFrame.swap(merged);
            m_hostPresentationWidth = width;
            m_hostPresentationHeight = height;
            m_hostPresentationDisplayFbp = displayFrame1.fbp;
            m_hostPresentationSourceFbp = selectedFrame1.fbp;
            m_hostPresentationUsedPreferred = false;
            m_hasHostPresentationFrame = true;
            return;
        }
    }

    const GSFrameReg &displayFrame = validCrt1 ? displayFrame1 : displayFrame2;
    const uint32_t width = validCrt1 ? width1 : width2;
    const uint32_t height = validCrt1 ? height1 : height2;

    GSFrameReg selectedFrame = displayFrame;
    std::vector<uint8_t> scratch;
    bool usedPreferred = false;
    const GSDisplayReadOrigin &displayOrigin = validCrt1 ? displayOrigin1 : displayOrigin2;
    if (!copyDisplaySource(displayFrame, displayOrigin, width, height, true, false, selectedFrame, scratch, usedPreferred))
    {
        m_hostPresentationFrame.clear();
        m_hostPresentationWidth = 0u;
        m_hostPresentationHeight = 0u;
        m_hostPresentationDisplayFbp = displayFrame.fbp;
        m_hostPresentationSourceFbp = 0u;
        m_hostPresentationUsedPreferred = false;
        m_hasHostPresentationFrame = false;
        return;
    }

    if (applyFieldMode)
    {
        applyFieldPresentation(scratch, width, height, oddField);
    }

    normalizePresentationAlpha(scratch, width, height);

    m_hostPresentationFrame.swap(scratch);
    m_hostPresentationWidth = width;
    m_hostPresentationHeight = height;
    m_hostPresentationDisplayFbp = displayFrame.fbp;
    m_hostPresentationSourceFbp = selectedFrame.fbp;
    m_hostPresentationUsedPreferred = usedPreferred;
    m_hasHostPresentationFrame = true;
}

bool GS::copyLatchedHostPresentationFrame(std::vector<uint8_t> &outPixels,
                                          uint32_t &outWidth,
                                          uint32_t &outHeight,
                                          uint32_t *outDisplayFbp,
                                          uint32_t *outSourceFbp,
                                          bool *outUsedPreferred) const
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    if (!m_hasHostPresentationFrame || m_hostPresentationFrame.empty())
    {
        outPixels.clear();
        outWidth = 0u;
        outHeight = 0u;
        if (outDisplayFbp)
            *outDisplayFbp = 0u;
        if (outSourceFbp)
            *outSourceFbp = 0u;
        if (outUsedPreferred)
            *outUsedPreferred = false;
        return false;
    }

    outPixels = m_hostPresentationFrame;
    outWidth = m_hostPresentationWidth;
    outHeight = m_hostPresentationHeight;
    if (outDisplayFbp)
        *outDisplayFbp = m_hostPresentationDisplayFbp;
    if (outSourceFbp)
        *outSourceFbp = m_hostPresentationSourceFbp;
    if (outUsedPreferred)
        *outUsedPreferred = m_hostPresentationUsedPreferred;
    return true;
}

void GS::processGIFPacket(const uint8_t *data, uint32_t sizeBytes)
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    if (!data || sizeBytes < 16 || !m_vram)
        return;

    PS2_IF_AGRESSIVE_LOGS({
        const uint32_t packetIndex = s_debugGifPacketCount.fetch_add(1, std::memory_order_relaxed);
        if (packetIndex < 48u)
        {
            const uint64_t tagLo = loadLE64(data);
            const uint32_t nloop = static_cast<uint32_t>(tagLo & 0x7FFFu);
            const uint8_t flg = static_cast<uint8_t>((tagLo >> 58) & 0x3u);
            uint32_t nreg = static_cast<uint32_t>((tagLo >> 60) & 0xFu);
            if (nreg == 0u)
                nreg = 16u;
            RUNTIME_LOG("[gs:gif] idx=" << packetIndex
                      << " size=" << sizeBytes
                      << " nloop=" << nloop
                      << " flg=" << static_cast<uint32_t>(flg)
                      << " nreg=" << nreg
                      << " ctx0fbp=" << m_ctx[0].frame.fbp
                      << " ctx1fbp=" << m_ctx[1].frame.fbp
                      << std::endl);
        }
    });

    if (sizeBytes >= 16)
    {
        const uint64_t tagLo = loadLE64(data);
        const uint8_t flg = static_cast<uint8_t>((tagLo >> 58) & 0x3);
        if (flg == GIF_FMT_PACKED)
        {
            m_hwregX = 0;
            m_hwregY = 0;
        }
    }

    uint32_t offset = 0;
    while (offset + 16 <= sizeBytes)
    {
        uint64_t tagLo = loadLE64(data + offset);
        uint64_t tagHi = loadLE64(data + offset + 8);
        offset += 16;

        m_curQ = 1.0f;

        uint32_t nloop = static_cast<uint32_t>(tagLo & 0x7FFF);
        uint8_t flg = static_cast<uint8_t>((tagLo >> 58) & 0x3);
        uint32_t nreg = static_cast<uint32_t>((tagLo >> 60) & 0xF);
        if (nreg == 0)
            nreg = 16;

        bool pre = ((tagLo >> 46) & 1) != 0;
        if (pre)
        {
            writeRegister(GS_REG_PRIM, (tagLo >> 47) & 0x7FF);
        }

        uint8_t regs[16];
        for (uint32_t i = 0; i < nreg; ++i)
            regs[i] = static_cast<uint8_t>((tagHi >> (i * 4)) & 0xF);

        if (flg == GIF_FMT_PACKED)
        {
            for (uint32_t loop = 0; loop < nloop; ++loop)
            {
                for (uint32_t r = 0; r < nreg; ++r)
                {
                    if (offset + 16 > sizeBytes)
                        return;
                    uint64_t lo = loadLE64(data + offset);
                    uint64_t hi = loadLE64(data + offset + 8);
                    offset += 16;
                    writeRegisterPacked(regs[r], lo, hi);
                }
            }
        }
        else if (flg == GIF_FMT_REGLIST)
        {
            for (uint32_t loop = 0; loop < nloop; ++loop)
            {
                for (uint32_t r = 0; r < nreg; ++r)
                {
                    if (offset + 8 > sizeBytes)
                        return;
                    writeRegister(regs[r], loadLE64(data + offset));
                    offset += 8;
                }
            }
            if ((nloop * nreg) & 1)
                offset += 8;
        }
        else if (flg == GIF_FMT_IMAGE)
        {
            uint32_t imageBytes = nloop * 16;
            if (offset + imageBytes > sizeBytes)
                imageBytes = sizeBytes - offset;
            processImageData(data + offset, imageBytes);
            offset += imageBytes;
        }
    }
}

void GS::writeRegisterPacked(uint8_t regDesc, uint64_t lo, uint64_t hi)
{
    switch (regDesc)
    {
    case 0x00:
        writeRegister(GS_REG_PRIM, lo & 0x7FF);
        break;
    case 0x01:
        m_curR = static_cast<uint8_t>(lo & 0xFF);
        m_curG = static_cast<uint8_t>((lo >> 32) & 0xFF);
        m_curB = static_cast<uint8_t>(hi & 0xFF);
        m_curA = static_cast<uint8_t>((hi >> 32) & 0xFF);
        break;
    case 0x02:
    {
        uint32_t sBits = static_cast<uint32_t>(lo & 0xFFFFFFFF);
        uint32_t tBits = static_cast<uint32_t>((lo >> 32) & 0xFFFFFFFF);
        uint32_t qBits = static_cast<uint32_t>(hi & 0xFFFFFFFF);
        std::memcpy(&m_curS, &sBits, 4);
        std::memcpy(&m_curT, &tBits, 4);
        std::memcpy(&m_curQ, &qBits, 4);
        if (m_curQ == 0.0f)
            m_curQ = 1.0f;
        break;
    }
    case 0x03:
        m_curU = static_cast<uint16_t>(lo & 0xFFFFu);
        m_curV = static_cast<uint16_t>((lo >> 32) & 0xFFFFu);
        break;
    case 0x04:
    {
        uint16_t x = static_cast<uint16_t>(lo & 0xFFFF);
        uint16_t y = static_cast<uint16_t>((lo >> 32) & 0xFFFF);
        uint32_t z = static_cast<uint32_t>((hi >> 4) & 0xFFFFFF);
        uint8_t f = static_cast<uint8_t>((hi >> 36) & 0xFF);
        bool adk = ((hi >> 47) & 1) != 0;
        PS2_IF_AGRESSIVE_LOGS({
            const uint32_t debugIndex = s_debugGsPackedVertexCount.fetch_add(1, std::memory_order_relaxed);
            if (debugIndex < 64u)
            {
                RUNTIME_LOG("[gs:packed-xyzf] idx=" << debugIndex
                          << " x=" << x
                          << " y=" << y
                          << " z=0x" << std::hex << z
                          << std::dec
                          << " fog=" << static_cast<uint32_t>(f)
                          << " kick=" << static_cast<uint32_t>(!adk ? 1u : 0u)
                          << " prim=" << static_cast<uint32_t>(m_prim.type)
                          << std::endl);
            }
        });
        GSVertex &vtx = m_vtxQueue[m_vtxCount % kMaxVerts];
        vtx.x = static_cast<float>(x) / 16.0f;
        vtx.y = static_cast<float>(y) / 16.0f;
        vtx.z = static_cast<float>(z);
        vtx.r = m_curR;
        vtx.g = m_curG;
        vtx.b = m_curB;
        vtx.a = m_curA;
        vtx.q = m_curQ;
        vtx.s = m_curS;
        vtx.t = m_curT;
        vtx.u = m_curU;
        vtx.v = m_curV;
        vtx.fog = f;
        vertexKick(!adk);
        break;
    }
    case 0x05:
    {
        uint16_t x = static_cast<uint16_t>(lo & 0xFFFF);
        uint16_t y = static_cast<uint16_t>((lo >> 32) & 0xFFFF);
        uint32_t z = static_cast<uint32_t>(hi & 0xFFFFFFFF);
        bool adk = ((hi >> 47) & 1) != 0;
        PS2_IF_AGRESSIVE_LOGS({
            const uint32_t debugIndex = s_debugGsPackedVertexCount.fetch_add(1, std::memory_order_relaxed);
            if (debugIndex < 64u)
            {
                RUNTIME_LOG("[gs:packed-xyz] idx=" << debugIndex
                          << " x=" << x
                          << " y=" << y
                          << " z=0x" << std::hex << z
                          << std::dec
                          << " kick=" << static_cast<uint32_t>(!adk ? 1u : 0u)
                          << " prim=" << static_cast<uint32_t>(m_prim.type)
                          << std::endl);
            }
        });
        GSVertex &vtx = m_vtxQueue[m_vtxCount % kMaxVerts];
        vtx.x = static_cast<float>(x) / 16.0f;
        vtx.y = static_cast<float>(y) / 16.0f;
        vtx.z = static_cast<float>(z);
        vtx.r = m_curR;
        vtx.g = m_curG;
        vtx.b = m_curB;
        vtx.a = m_curA;
        vtx.q = m_curQ;
        vtx.s = m_curS;
        vtx.t = m_curT;
        vtx.u = m_curU;
        vtx.v = m_curV;
        vtx.fog = m_curFog;
        vertexKick(!adk);
        break;
    }
    case 0x0A:
        m_curFog = static_cast<uint8_t>((hi >> 36) & 0xFF);
        break;
    case 0x0C:
    {
        PS2_IF_AGRESSIVE_LOGS({
            const uint32_t debugIndex = s_debugGsPackedVertexCount.fetch_add(1, std::memory_order_relaxed);
            if (debugIndex < 64u)
            {
                RUNTIME_LOG("[gs:packed-xyzf3] idx=" << debugIndex
                          << " x=" << static_cast<uint32_t>(lo & 0xFFFFu)
                          << " y=" << static_cast<uint32_t>((lo >> 32) & 0xFFFFu)
                          << " kick=0"
                          << " prim=" << static_cast<uint32_t>(m_prim.type)
                          << std::endl);
            }
        });
        GSVertex &vtx = m_vtxQueue[m_vtxCount % kMaxVerts];
        vtx.x = static_cast<float>(lo & 0xFFFF) / 16.0f;
        vtx.y = static_cast<float>((lo >> 32) & 0xFFFF) / 16.0f;
        vtx.z = static_cast<float>((hi >> 4) & 0xFFFFFF);
        vtx.r = m_curR;
        vtx.g = m_curG;
        vtx.b = m_curB;
        vtx.a = m_curA;
        vtx.q = m_curQ;
        vtx.s = m_curS;
        vtx.t = m_curT;
        vtx.u = m_curU;
        vtx.v = m_curV;
        vtx.fog = static_cast<uint8_t>((hi >> 36) & 0xFF);
        vertexKick(false);
        break;
    }
    case 0x0D:
    {
        PS2_IF_AGRESSIVE_LOGS({
            const uint32_t debugIndex = s_debugGsPackedVertexCount.fetch_add(1, std::memory_order_relaxed);
            if (debugIndex < 64u)
            {
                RUNTIME_LOG("[gs:packed-xyz3] idx=" << debugIndex
                          << " x=" << static_cast<uint32_t>(lo & 0xFFFFu)
                          << " y=" << static_cast<uint32_t>((lo >> 32) & 0xFFFFu)
                          << " kick=0"
                          << " prim=" << static_cast<uint32_t>(m_prim.type)
                          << std::endl);
            }
        });
        GSVertex &vtx = m_vtxQueue[m_vtxCount % kMaxVerts];
        vtx.x = static_cast<float>(lo & 0xFFFF) / 16.0f;
        vtx.y = static_cast<float>((lo >> 32) & 0xFFFF) / 16.0f;
        vtx.z = static_cast<float>(hi & 0xFFFFFFFF);
        vtx.r = m_curR;
        vtx.g = m_curG;
        vtx.b = m_curB;
        vtx.a = m_curA;
        vtx.q = m_curQ;
        vtx.s = m_curS;
        vtx.t = m_curT;
        vtx.u = m_curU;
        vtx.v = m_curV;
        vtx.fog = m_curFog;
        vertexKick(false);
        break;
    }
    case 0x0E:
    {
        uint8_t addr = static_cast<uint8_t>(hi & 0xFF);
        writeRegister(addr, lo);
        break;
    }
    case 0x0F:
        break;
    default:
        writeRegister(regDesc, lo);
        break;
    }
}

void GS::writeRegister(uint8_t regAddr, uint64_t value)
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    const bool interestingReg =
        regAddr == GS_REG_PRIM ||
        regAddr == GS_REG_RGBAQ ||
        regAddr == GS_REG_ST ||
        regAddr == GS_REG_UV ||
        regAddr == GS_REG_XYZ2 ||
        regAddr == GS_REG_XYZ3 ||
        regAddr == GS_REG_XYZF2 ||
        regAddr == GS_REG_XYZF3 ||
        regAddr == GS_REG_TEX0_1 ||
        regAddr == GS_REG_TEX0_2 ||
        regAddr == GS_REG_XYOFFSET_1 ||
        regAddr == GS_REG_XYOFFSET_2 ||
        regAddr == GS_REG_SCISSOR_1 ||
        regAddr == GS_REG_SCISSOR_2 ||
        regAddr == GS_REG_FRAME_1 ||
        regAddr == GS_REG_FRAME_2 ||
        regAddr == GS_REG_ALPHA_1 ||
        regAddr == GS_REG_ALPHA_2 ||
        regAddr == GS_REG_TEST_1 ||
        regAddr == GS_REG_TEST_2 ||
        regAddr == GS_REG_BITBLTBUF ||
        regAddr == GS_REG_TRXPOS ||
        regAddr == GS_REG_TRXREG ||
        regAddr == GS_REG_TRXDIR;

    PS2_IF_AGRESSIVE_LOGS({
        if (interestingReg)
        {
            const uint32_t debugIndex = s_debugGsRegisterCount.fetch_add(1, std::memory_order_relaxed);
            if (debugIndex < 128u)
            {
                RUNTIME_LOG("[gs:reg] idx=" << debugIndex
                          << " reg=0x" << std::hex << static_cast<uint32_t>(regAddr)
                          << " value=0x" << value
                          << std::dec
                          << std::endl);
            }
        }
    });

    const bool isCopyRelevantReg =
        regAddr == GS_REG_PRIM ||
        regAddr == GS_REG_TEX0_2 ||
        regAddr == GS_REG_TEX1_2 ||
        regAddr == GS_REG_ALPHA_2 ||
        regAddr == GS_REG_TEST_2 ||
        regAddr == GS_REG_PABE ||
        regAddr == GS_REG_FRAME_2 ||
        regAddr == GS_REG_XYOFFSET_2 ||
        regAddr == GS_REG_SCISSOR_2;
    PS2_IF_AGRESSIVE_LOGS({
        if (isCopyRelevantReg &&
            s_debugCopyRegCount.fetch_add(1u, std::memory_order_relaxed) < 64u)
        {
            RUNTIME_LOG("[gs:copy-reg] reg=0x"
                      << std::hex << static_cast<uint32_t>(regAddr)
                      << " value=0x" << value
                      << std::dec
                      << " primCtxt=" << static_cast<uint32_t>(m_prim.ctxt)
                      << " ctx0fbp=" << m_ctx[0].frame.fbp
                      << " ctx1fbp=" << m_ctx[1].frame.fbp
                      << std::endl);
        }
    });

    switch (regAddr)
    {
    case GS_REG_PRIM:
    {
        m_prim.type = static_cast<GSPrimType>(value & 0x7);
        m_prim.iip = ((value >> 3) & 1) != 0;
        m_prim.tme = ((value >> 4) & 1) != 0;
        m_prim.fge = ((value >> 5) & 1) != 0;
        m_prim.abe = ((value >> 6) & 1) != 0;
        m_prim.aa1 = ((value >> 7) & 1) != 0;
        m_prim.fst = ((value >> 8) & 1) != 0;
        m_prim.ctxt = ((value >> 9) & 1) != 0;
        m_prim.fix = ((value >> 10) & 1) != 0;
        m_vtxCount = 0;
        m_vtxIndex = 0;
        break;
    }
    case GS_REG_RGBAQ:
    {
        m_curR = static_cast<uint8_t>(value & 0xFF);
        m_curG = static_cast<uint8_t>((value >> 8) & 0xFF);
        m_curB = static_cast<uint8_t>((value >> 16) & 0xFF);
        m_curA = static_cast<uint8_t>((value >> 24) & 0xFF);
        uint32_t qBits = static_cast<uint32_t>((value >> 32) & 0xFFFFFFFF);
        std::memcpy(&m_curQ, &qBits, 4);
        if (m_curQ == 0.0f)
            m_curQ = 1.0f;
        break;
    }
    case GS_REG_ST:
    {
        uint32_t sBits = static_cast<uint32_t>(value & 0xFFFFFFFF);
        uint32_t tBits = static_cast<uint32_t>((value >> 32) & 0xFFFFFFFF);
        std::memcpy(&m_curS, &sBits, 4);
        std::memcpy(&m_curT, &tBits, 4);
        break;
    }
    case GS_REG_UV:
    {
        m_curU = static_cast<uint16_t>(value & 0xFFFFu);
        m_curV = static_cast<uint16_t>((value >> 16) & 0xFFFFu);
        break;
    }
    case GS_REG_XYZF2:
    case GS_REG_XYZF3:
    {
        GSVertex &vtx = m_vtxQueue[m_vtxCount % kMaxVerts];
        vtx.x = static_cast<float>(value & 0xFFFF) / 16.0f;
        vtx.y = static_cast<float>((value >> 16) & 0xFFFF) / 16.0f;
        vtx.z = static_cast<float>((value >> 32) & 0xFFFFFF);
        vtx.fog = static_cast<uint8_t>((value >> 56) & 0xFF);
        vtx.r = m_curR;
        vtx.g = m_curG;
        vtx.b = m_curB;
        vtx.a = m_curA;
        vtx.q = m_curQ;
        vtx.s = m_curS;
        vtx.t = m_curT;
        vtx.u = m_curU;
        vtx.v = m_curV;
        vertexKick(regAddr == GS_REG_XYZF2);
        break;
    }
    case GS_REG_XYZ2:
    case GS_REG_XYZ3:
    {
        GSVertex &vtx = m_vtxQueue[m_vtxCount % kMaxVerts];
        vtx.x = static_cast<float>(value & 0xFFFF) / 16.0f;
        vtx.y = static_cast<float>((value >> 16) & 0xFFFF) / 16.0f;
        vtx.z = static_cast<float>((value >> 32) & 0xFFFFFFFF);
        vtx.r = m_curR;
        vtx.g = m_curG;
        vtx.b = m_curB;
        vtx.a = m_curA;
        vtx.q = m_curQ;
        vtx.s = m_curS;
        vtx.t = m_curT;
        vtx.u = m_curU;
        vtx.v = m_curV;
        vtx.fog = m_curFog;
        vertexKick(regAddr == GS_REG_XYZ2);
        break;
    }
    case GS_REG_TEX0_1:
    case GS_REG_TEX0_2:
    {
        int ci = (regAddr == GS_REG_TEX0_2) ? 1 : 0;
        auto &t = m_ctx[ci].tex0;
        t.tbp0 = static_cast<uint32_t>(value & 0x3FFF);
        t.tbw = static_cast<uint8_t>((value >> 14) & 0x3F);
        t.psm = static_cast<uint8_t>((value >> 20) & 0x3F);
        t.tw = static_cast<uint8_t>((value >> 26) & 0xF);
        t.th = static_cast<uint8_t>((value >> 30) & 0xF);
        t.tcc = static_cast<uint8_t>((value >> 34) & 0x1);
        t.tfx = static_cast<uint8_t>((value >> 35) & 0x3);
        t.cbp = static_cast<uint32_t>((value >> 37) & 0x3FFF);
        t.cpsm = static_cast<uint8_t>((value >> 51) & 0xF);
        t.csm = static_cast<uint8_t>((value >> 55) & 0x1);
        t.csa = static_cast<uint8_t>((value >> 56) & 0x1F);
        t.cld = static_cast<uint8_t>((value >> 61) & 0x7);
        break;
    }
    case GS_REG_CLAMP_1:
    case GS_REG_CLAMP_2:
    {
        int ci = (regAddr == GS_REG_CLAMP_2) ? 1 : 0;
        m_ctx[ci].clamp = value;
        break;
    }
    case GS_REG_FOG:
        m_curFog = static_cast<uint8_t>((value >> 56) & 0xFF);
        break;
    case GS_REG_TEX1_1:
    case GS_REG_TEX1_2:
    {
        int ci = (regAddr == GS_REG_TEX1_2) ? 1 : 0;
        m_ctx[ci].tex1 = value;
        break;
    }
    case GS_REG_TEX2_1:
    case GS_REG_TEX2_2:
        break;
    case GS_REG_XYOFFSET_1:
    case GS_REG_XYOFFSET_2:
    {
        int ci = (regAddr == GS_REG_XYOFFSET_2) ? 1 : 0;
        m_ctx[ci].xyoffset.ofx = static_cast<uint16_t>(value & 0xFFFF);
        m_ctx[ci].xyoffset.ofy = static_cast<uint16_t>((value >> 32) & 0xFFFF);
        break;
    }
    case GS_REG_PRMODECONT:
        m_prmodecont = (value & 1) != 0;
        break;
    case GS_REG_PRMODE:
        if (!m_prmodecont)
        {
            m_prim.iip = ((value >> 3) & 1) != 0;
            m_prim.tme = ((value >> 4) & 1) != 0;
            m_prim.fge = ((value >> 5) & 1) != 0;
            m_prim.abe = ((value >> 6) & 1) != 0;
            m_prim.aa1 = ((value >> 7) & 1) != 0;
            m_prim.fst = ((value >> 8) & 1) != 0;
            m_prim.ctxt = ((value >> 9) & 1) != 0;
            m_prim.fix = ((value >> 10) & 1) != 0;
        }
        break;
    case GS_REG_SCISSOR_1:
    case GS_REG_SCISSOR_2:
    {
        int ci = (regAddr == GS_REG_SCISSOR_2) ? 1 : 0;
        m_ctx[ci].scissor.x0 = static_cast<uint16_t>(value & 0x7FF);
        m_ctx[ci].scissor.x1 = static_cast<uint16_t>((value >> 16) & 0x7FF);
        m_ctx[ci].scissor.y0 = static_cast<uint16_t>((value >> 32) & 0x7FF);
        m_ctx[ci].scissor.y1 = static_cast<uint16_t>((value >> 48) & 0x7FF);
        break;
    }
    case GS_REG_ALPHA_1:
    case GS_REG_ALPHA_2:
    {
        int ci = (regAddr == GS_REG_ALPHA_2) ? 1 : 0;
        m_ctx[ci].alpha = value;
        break;
    }
    case GS_REG_TEST_1:
    case GS_REG_TEST_2:
    {
        int ci = (regAddr == GS_REG_TEST_2) ? 1 : 0;
        m_ctx[ci].test = value;
        break;
    }
    case GS_REG_FRAME_1:
    case GS_REG_FRAME_2:
    {
        int ci = (regAddr == GS_REG_FRAME_2) ? 1 : 0;
        m_ctx[ci].frame.fbp = static_cast<uint32_t>(value & 0x1FF);
        m_ctx[ci].frame.fbw = static_cast<uint32_t>((value >> 16) & 0x3F);
        m_ctx[ci].frame.psm = static_cast<uint8_t>((value >> 24) & 0x3F);
        m_ctx[ci].frame.fbmsk = static_cast<uint32_t>((value >> 32) & 0xFFFFFFFF);
        break;
    }
    case GS_REG_ZBUF_1:
    case GS_REG_ZBUF_2:
    {
        int ci = (regAddr == GS_REG_ZBUF_2) ? 1 : 0;
        m_ctx[ci].zbuf = value;
        break;
    }
    case GS_REG_FBA_1:
    case GS_REG_FBA_2:
    {
        int ci = (regAddr == GS_REG_FBA_2) ? 1 : 0;
        m_ctx[ci].fba = value;
        break;
    }
    case GS_REG_BITBLTBUF:
    {
        m_bitbltbuf.sbp = static_cast<uint32_t>(value & 0x3FFF);
        m_bitbltbuf.sbw = static_cast<uint8_t>((value >> 16) & 0x3F);
        m_bitbltbuf.spsm = static_cast<uint8_t>((value >> 24) & 0x3F);
        m_bitbltbuf.dbp = static_cast<uint32_t>((value >> 32) & 0x3FFF);
        m_bitbltbuf.dbw = static_cast<uint8_t>((value >> 48) & 0x3F);
        m_bitbltbuf.dpsm = static_cast<uint8_t>((value >> 56) & 0x3F);
        break;
    }
    case GS_REG_TRXPOS:
    {
        m_trxpos.ssax = static_cast<uint16_t>(value & 0x7FF);
        m_trxpos.ssay = static_cast<uint16_t>((value >> 16) & 0x7FF);
        m_trxpos.dsax = static_cast<uint16_t>((value >> 32) & 0x7FF);
        m_trxpos.dsay = static_cast<uint16_t>((value >> 48) & 0x7FF);
        m_trxpos.dir = static_cast<uint8_t>((value >> 59) & 0x3);
        break;
    }
    case GS_REG_TRXREG:
    {
        m_trxreg.rrw = static_cast<uint16_t>(value & 0xFFF);
        m_trxreg.rrh = static_cast<uint16_t>((value >> 32) & 0xFFF);
        break;
    }
    case GS_REG_TRXDIR:
    {
        m_trxdir = static_cast<uint32_t>(value & 0x3);
        m_hwregX = 0;
        m_hwregY = 0;

        if (m_trxdir == 2 && m_vram)
        {
            performLocalToLocalTransfer();
        }
        else if (m_trxdir == 1 && m_vram)
        {
            performLocalToHostToBuffer();
        }
        break;
    }
    case GS_REG_HWREG:
    {
        uint8_t buf[8];
        std::memcpy(buf, &value, 8);
        processImageData(buf, 8);
        break;
    }
    case GS_REG_PABE:
        m_pabe = (value & 1u) != 0u;
        break;
    case GS_REG_TEXFLUSH:
    case GS_REG_TEXCLUT:
    case GS_REG_SCANMSK:
    case GS_REG_FOGCOL:
    case GS_REG_DIMX:
    case GS_REG_DTHE:
    case GS_REG_COLCLAMP:
    case GS_REG_MIPTBP1_1:
    case GS_REG_MIPTBP1_2:
    case GS_REG_MIPTBP2_1:
    case GS_REG_MIPTBP2_2:
        break;
    case GS_REG_TEXA:
    {
        PS2_IF_AGRESSIVE_LOGS({
            const uint32_t texaIndex = s_debugTexaWriteCount.fetch_add(1u, std::memory_order_relaxed);
            if (texaIndex < 24u)
            {
                RUNTIME_LOG("[gs:texa] idx=" << texaIndex
                          << " value=0x" << std::hex << value
                          << " ta0=0x" << ((value >> 0) & 0xFFu)
                          << " aem=" << ((value >> 15) & 0x1u)
                          << " ta1=0x" << ((value >> 32) & 0xFFu)
                          << std::dec
                          << std::endl);
            }
        });
        break;
    }
    case GS_REG_SIGNAL:
    {
        if (m_privRegs)
        {
            uint32_t id = static_cast<uint32_t>(value & 0xFFFFFFFF);
            uint32_t mask = static_cast<uint32_t>(value >> 32);
            uint32_t lo = static_cast<uint32_t>(m_privRegs->siglblid & 0xFFFFFFFF);
            lo = (lo & ~mask) | (id & mask);
            m_privRegs->siglblid = (m_privRegs->siglblid & 0xFFFFFFFF00000000ULL) | lo;
            m_privRegs->csr |= 0x1;
        }
        break;
    }
    case GS_REG_FINISH:
    {
        if (m_privRegs)
            m_privRegs->csr |= 0x2;
        break;
    }
    case GS_REG_LABEL:
    {
        if (m_privRegs)
        {
            uint32_t id = static_cast<uint32_t>(value & 0xFFFFFFFF);
            uint32_t mask = static_cast<uint32_t>(value >> 32);
            uint32_t hi = static_cast<uint32_t>(m_privRegs->siglblid >> 32);
            hi = (hi & ~mask) | (id & mask);
            m_privRegs->siglblid = (static_cast<uint64_t>(hi) << 32) | (m_privRegs->siglblid & 0xFFFFFFFF);
        }
        break;
    }
    case 0x59:
        if (m_privRegs)
            m_privRegs->dispfb1 = value;
        break;
    case 0x5a:
        if (m_privRegs)
            m_privRegs->display1 = value;
        break;
    case 0x5b:
        if (m_privRegs)
            m_privRegs->dispfb2 = value;
        break;
    case 0x5c:
        if (m_privRegs)
            m_privRegs->display2 = value;
        break;
    case 0x5f:
        if (m_privRegs)
            m_privRegs->bgcolor = value;
        break;
    default:
        break;
    }
}

void GS::performLocalToLocalTransfer()
{
    if (!m_vram)
        return;

    uint32_t sbp = m_bitbltbuf.sbp;
    uint8_t sbw = m_bitbltbuf.sbw;
    uint8_t spsm = m_bitbltbuf.spsm;
    uint32_t dbp = m_bitbltbuf.dbp;
    uint8_t dbw = m_bitbltbuf.dbw;
    uint8_t dpsm = m_bitbltbuf.dpsm;

    if (sbw == 0)
        sbw = 1;
    if (dbw == 0)
        dbw = 1;

    const uint32_t rrw = m_trxreg.rrw;
    const uint32_t rrh = m_trxreg.rrh;
    const uint32_t ssax = m_trxpos.ssax;
    const uint32_t ssay = m_trxpos.ssay;
    const uint32_t dsax = m_trxpos.dsax;
    const uint32_t dsay = m_trxpos.dsay;
    const GSTransferTraversal traversal = decodeTransferTraversal(m_trxpos.dir);
    const bool formatAware = (spsm == dpsm) && supportsFormatAwareLocalCopy(spsm);

    if (rrw == 0u || rrh == 0u)
    {
        return;
    }

    PS2_IF_AGRESSIVE_LOGS({
        if ((spsm == GS_PSM_T4 || dpsm == GS_PSM_T4) &&
            s_debugLocalCopyCount.fetch_add(1u, std::memory_order_relaxed) < 96u)
        {
            RUNTIME_LOG("[gs:l2l] sbp=" << sbp
                      << " dbp=" << dbp
                      << " sbw=" << static_cast<uint32_t>(sbw)
                      << " dbw=" << static_cast<uint32_t>(dbw)
                      << " spsm=0x" << std::hex << static_cast<uint32_t>(spsm)
                      << " dpsm=0x" << static_cast<uint32_t>(dpsm) << std::dec
                      << " ss=(" << ssax << "," << ssay << ")"
                      << " ds=(" << dsax << "," << dsay << ")"
                      << " rr=(" << rrw << "," << rrh << ")"
                      << " dir=" << static_cast<uint32_t>(m_trxpos.dir)
                      << " formatAware=" << (formatAware ? 1 : 0) << std::endl);
        }
    });

    if (formatAware)
    {
        for (uint32_t row = 0; row < rrh; ++row)
        {
            const uint32_t srcY = transferCoord(ssay, rrh, row, traversal.reverseY);
            const uint32_t dstY = transferCoord(dsay, rrh, row, traversal.reverseY);
            for (uint32_t col = 0; col < rrw; ++col)
            {
                const uint32_t srcX = transferCoord(ssax, rrw, col, traversal.reverseX);
                const uint32_t dstX = transferCoord(dsax, rrw, col, traversal.reverseX);
                const uint32_t pixel =
                    readTransferPixel(m_vram, m_vramSize, sbp, sbw, spsm, srcX, srcY);
                writeTransferPixel(m_vram, m_vramSize, dbp, dbw, dpsm, dstX, dstY, pixel);
            }
        }
    }
    else
    {
        const uint32_t srcBase = sbp * 256u;
        const uint32_t dstBase = dbp * 256u;
        uint32_t srcBpp = bitsPerPixel(spsm) / 8u;
        uint32_t dstBpp = bitsPerPixel(dpsm) / 8u;
        if (srcBpp == 0)
            srcBpp = 4;
        if (dstBpp == 0)
            dstBpp = 4;
        const uint32_t srcStride = static_cast<uint32_t>(sbw) * 64u * srcBpp;
        const uint32_t dstStride = static_cast<uint32_t>(dbw) * 64u * dstBpp;
        const uint32_t copyBpp = (srcBpp < dstBpp) ? srcBpp : dstBpp;

        uint8_t pixelBytes[4] = {};
        for (uint32_t row = 0; row < rrh; ++row)
        {
            const uint32_t srcY = transferCoord(ssay, rrh, row, traversal.reverseY);
            const uint32_t dstY = transferCoord(dsay, rrh, row, traversal.reverseY);
            for (uint32_t col = 0; col < rrw; ++col)
            {
                const uint32_t srcX = transferCoord(ssax, rrw, col, traversal.reverseX);
                const uint32_t dstX = transferCoord(dsax, rrw, col, traversal.reverseX);
                const uint32_t srcOff = srcBase + srcY * srcStride + srcX * srcBpp;
                const uint32_t dstOff = dstBase + dstY * dstStride + dstX * dstBpp;
                if (srcOff + copyBpp > m_vramSize || dstOff + copyBpp > m_vramSize)
                {
                    continue;
                }

                std::memcpy(pixelBytes, m_vram + srcOff, copyBpp);
                std::memcpy(m_vram + dstOff, pixelBytes, copyBpp);
            }
        }
    }

    if (sbp == 0u && (dbp == 0u || dbp == 0x20u) && rrw >= 640u && rrh >= 512u)
    {
        m_lastDisplayBaseBytes = (dbp == 0x20u) ? 8192u : 0u;
        snapshotVRAM();
    }
}

void GS::vertexKick(bool drawing)
{
    ++m_vtxCount;
    ++m_vtxIndex;

    PS2_IF_AGRESSIVE_LOGS({
        const uint32_t debugIndex = s_debugGsVertexKickCount.fetch_add(1, std::memory_order_relaxed);
        if (debugIndex < 96u)
        {
            RUNTIME_LOG("[gs:kick] idx=" << debugIndex
                      << " drawing=" << static_cast<uint32_t>(drawing ? 1u : 0u)
                      << " prim=" << static_cast<uint32_t>(m_prim.type)
                      << " vtxCount=" << m_vtxCount
                      << std::endl);
        }
    });

    if (!drawing)
        return;

    int needed = 0;
    switch (m_prim.type)
    {
    case GS_PRIM_POINT:
        needed = 1;
        break;
    case GS_PRIM_LINE:
        needed = 2;
        break;
    case GS_PRIM_LINESTRIP:
        needed = 2;
        break;
    case GS_PRIM_TRIANGLE:
        needed = 3;
        break;
    case GS_PRIM_TRISTRIP:
        needed = 3;
        break;
    case GS_PRIM_TRIFAN:
        needed = 3;
        break;
    case GS_PRIM_SPRITE:
        needed = 2;
        break;
    default:
        return;
    }

    if (m_vtxCount < needed)
        return;

    m_rasterizer.drawPrimitive(this);

    switch (m_prim.type)
    {
    case GS_PRIM_LINE:
    case GS_PRIM_TRIANGLE:
    case GS_PRIM_SPRITE:
    case GS_PRIM_POINT:
        m_vtxCount = 0;
        break;
    case GS_PRIM_LINESTRIP:
        m_vtxQueue[0] = m_vtxQueue[1];
        m_vtxCount = 1;
        break;
    case GS_PRIM_TRISTRIP:
        m_vtxQueue[0] = m_vtxQueue[1];
        m_vtxQueue[1] = m_vtxQueue[2];
        m_vtxCount = 2;
        break;
    case GS_PRIM_TRIFAN:
        m_vtxQueue[1] = m_vtxQueue[2];
        m_vtxCount = 2;
        break;
    default:
        m_vtxCount = 0;
        break;
    }
}

void GS::processImageData(const uint8_t *data, uint32_t sizeBytes)
{
    if (m_trxdir != 0 || !m_vram)
        return;

    uint32_t dbp = m_bitbltbuf.dbp;
    uint8_t dbw = m_bitbltbuf.dbw;
    uint8_t dpsm = m_bitbltbuf.dpsm;

    if (dbw == 0)
        dbw = 1;
    uint32_t base = dbp * 256u;
    uint32_t bpp = bitsPerPixel(dpsm);
    uint32_t stridePixels = static_cast<uint32_t>(dbw) * 64u;

    uint32_t rrw = m_trxreg.rrw;
    uint32_t rrh = m_trxreg.rrh;
    uint32_t dsax = m_trxpos.dsax;
    uint32_t dsay = m_trxpos.dsay;

    if (bpp == 4)
    {
        uint32_t widthBlocks = (dbw != 0) ? static_cast<uint32_t>(dbw) : 1u;
        uint32_t offset = 0;

        // T4 image uploads can be split across multiple GIF IMAGE packets.
        // Keep advancing from the previous HWREG position instead of restarting at (0, 0).
        auto writeT4Nibble = [&](uint8_t nibble, uint8_t srcByte, uint32_t srcLo, uint32_t srcHi)
        {
            if (m_hwregY >= rrh)
                return;

            const uint32_t vx = dsax + m_hwregX;
            const uint32_t vy = dsay + m_hwregY;
            const uint32_t nibbleAddr = GSPSMT4::addrPSMT4(dbp, widthBlocks, vx, vy);
            const uint32_t byteOff = nibbleAddr >> 1;

            if (byteOff < m_vramSize)
            {
                const int shift = static_cast<int>((nibbleAddr & 1u) << 2);
                uint8_t &b = m_vram[byteOff];
                b = static_cast<uint8_t>((b & (0xF0u >> shift)) | ((nibble & 0x0Fu) << shift));
            }
            ++m_hwregX;
            if (m_hwregX >= rrw)
            {
                m_hwregX = 0;
                ++m_hwregY;
            }
        };

        while (offset < sizeBytes && m_hwregY < rrh)
        {
            const uint8_t srcByte = data[offset++];
            const uint32_t srcLo = srcByte & 0x0Fu;
            const uint32_t srcHi = (srcByte >> 4) & 0x0Fu;
            const uint32_t xBefore = m_hwregX;

            writeT4Nibble(static_cast<uint8_t>(srcLo), srcByte, srcLo, srcHi);
            if ((xBefore + 1u) < rrw && m_hwregY < rrh)
            {
                writeT4Nibble(static_cast<uint8_t>(srcHi), srcByte, srcLo, srcHi);
            }
        }
    }
    else if (dpsm == GS_PSM_T8)
    {
        uint32_t offset = 0;
        while (offset < sizeBytes && m_hwregY < rrh)
        {
            uint32_t pixelsLeft = rrw - m_hwregX;
            uint32_t pixelsToCopy = std::min<uint32_t>(pixelsLeft, sizeBytes - offset);
            if (pixelsToCopy == 0)
            {
                break;
            }

            for (uint32_t i = 0; i < pixelsToCopy; ++i)
            {
                const uint32_t vx = dsax + m_hwregX + i;
                const uint32_t vy = dsay + m_hwregY;
                const uint32_t dst = GSPSMT8::addrPSMT8(dbp, dbw, vx, vy);
                if (dst < m_vramSize)
                {
                    m_vram[dst] = data[offset + i];
                }
            }

            offset += pixelsToCopy;
            m_hwregX += pixelsToCopy;
            if (m_hwregX >= rrw)
            {
                m_hwregX = 0;
                ++m_hwregY;
            }
        }
    }
    else if (dpsm == GS_PSM_CT24 || dpsm == GS_PSM_Z24)
    {
        uint32_t transferBpp = 3;

        uint32_t offset = 0;
        while (offset < sizeBytes && m_hwregY < rrh)
        {
            uint32_t pixelsLeft = rrw - m_hwregX;
            uint32_t srcBytesLeft = pixelsLeft * transferBpp;
            uint32_t bytesAvail = sizeBytes - offset;
            uint32_t pixelsToCopy = pixelsLeft;
            if (srcBytesLeft > bytesAvail)
                pixelsToCopy = bytesAvail / transferBpp;

            if (pixelsToCopy == 0)
                break;

            if (pixelsToCopy > 0)
            {
                for (uint32_t p = 0; p < pixelsToCopy; ++p)
                {
                    const uint32_t vx = dsax + m_hwregX + p;
                    const uint32_t vy = dsay + m_hwregY;
                    const uint32_t dstOff = GSPSMCT32::addrPSMCT32(dbp, dbw, vx, vy);
                    if (dstOff + 4u <= m_vramSize)
                    {
                        m_vram[dstOff + 0u] = data[offset + p * 3u + 0u];
                        m_vram[dstOff + 1u] = data[offset + p * 3u + 1u];
                        m_vram[dstOff + 2u] = data[offset + p * 3u + 2u];
                        m_vram[dstOff + 3u] = 0x80u;
                    }
                }
            }

            offset += pixelsToCopy * transferBpp;
            m_hwregX += pixelsToCopy;
            if (m_hwregX >= rrw)
            {
                m_hwregX = 0;
                ++m_hwregY;
            }
        }
    }
    else
    {
        uint32_t bytesPerPixel = bpp / 8u;
        if (bytesPerPixel == 0)
            bytesPerPixel = 4;
        uint32_t strideBytes = stridePixels * bytesPerPixel;
        uint32_t rowBytes = rrw * bytesPerPixel;

        uint32_t offset = 0;
        while (offset < sizeBytes && m_hwregY < rrh)
        {
            uint32_t dstY = dsay + m_hwregY;
            uint32_t pixelsLeft = rrw - m_hwregX;
            uint32_t bytesLeft = pixelsLeft * bytesPerPixel;
            uint32_t bytesAvail = sizeBytes - offset;
            if (bytesLeft > bytesAvail)
                bytesLeft = (bytesAvail / bytesPerPixel) * bytesPerPixel;

            uint32_t pixelsCopied = bytesLeft / bytesPerPixel;
            if ((dpsm == GS_PSM_CT32 || dpsm == GS_PSM_Z32) && pixelsCopied > 0)
            {
                for (uint32_t p = 0; p < pixelsCopied; ++p)
                {
                    const uint32_t vx = dsax + m_hwregX + p;
                    const uint32_t vy = dstY;
                    const uint32_t dstOff = GSPSMCT32::addrPSMCT32(dbp, dbw, vx, vy);
                    if (dstOff + 4u <= m_vramSize)
                        std::memcpy(m_vram + dstOff, data + offset + p * 4u, 4u);
                }
            }
            else
            {
                uint32_t dstOff = base + dstY * strideBytes + (dsax + m_hwregX) * bytesPerPixel;
                if (dstOff + bytesLeft <= m_vramSize && bytesLeft > 0)
                    std::memcpy(m_vram + dstOff, data + offset, bytesLeft);
            }

            offset += bytesLeft;
            m_hwregX += pixelsCopied;
            if (m_hwregX >= rrw)
            {
                m_hwregX = 0;
                ++m_hwregY;
            }
        }
    }
}

void GS::performLocalToHostToBuffer()
{
    m_localToHostBuffer.clear();
    m_localToHostReadPos = 0;
    if (!m_vram)
        return;

    uint32_t sbp = m_bitbltbuf.sbp;
    uint8_t sbw = m_bitbltbuf.sbw;
    uint8_t spsm = m_bitbltbuf.spsm;

    if (sbw == 0)
        sbw = 1;
    uint32_t base = sbp * 256u;
    uint32_t bpp = bitsPerPixel(spsm);
    uint32_t stridePixels = static_cast<uint32_t>(sbw) * 64u;

    uint32_t rrw = m_trxreg.rrw;
    uint32_t rrh = m_trxreg.rrh;
    uint32_t ssax = m_trxpos.ssax;
    uint32_t ssay = m_trxpos.ssay;

    if (bpp == 4)
    {
        uint32_t rowBytes = (rrw + 1u) / 2u;
        if (rowBytes == 0)
            rowBytes = 1;
        m_localToHostBuffer.reserve(rowBytes * rrh);
        uint32_t widthBlocks = static_cast<uint32_t>(sbw);
        for (uint32_t y = 0; y < rrh; ++y)
        {
            for (uint32_t x = 0; x < rrw; ++x)
            {
                uint32_t vx = ssax + x;
                uint32_t vy = ssay + y;
                uint32_t nibbleAddr = GSPSMT4::addrPSMT4(sbp, widthBlocks, vx, vy);
                uint32_t byteOff = nibbleAddr >> 1;
                uint8_t nibble = 0;
                if (byteOff < m_vramSize)
                {
                    int shift = static_cast<int>((nibbleAddr & 1u) << 2);
                    nibble = static_cast<uint8_t>((m_vram[byteOff] >> shift) & 0x0Fu);
                }
                if (x & 1u)
                    m_localToHostBuffer.back() = static_cast<uint8_t>((m_localToHostBuffer.back() & 0x0Fu) | (nibble << 4));
                else
                    m_localToHostBuffer.push_back(nibble);
            }
        }
    }
    else if (spsm == GS_PSM_T8)
    {
        m_localToHostBuffer.reserve(rrw * rrh);
        for (uint32_t y = 0; y < rrh; ++y)
        {
            for (uint32_t x = 0; x < rrw; ++x)
            {
                const uint32_t src = GSPSMT8::addrPSMT8(sbp, sbw, ssax + x, ssay + y);
                m_localToHostBuffer.push_back((src < m_vramSize) ? m_vram[src] : 0u);
            }
        }
    }
    else if (spsm == GS_PSM_CT24 || spsm == GS_PSM_Z24)
    {
        uint32_t transferBpp = 3;
        m_localToHostBuffer.reserve(rrw * rrh * transferBpp);

        for (uint32_t y = 0; y < rrh; ++y)
        {
            for (uint32_t x = 0; x < rrw; ++x)
            {
                uint32_t srcOff = GSPSMCT32::addrPSMCT32(sbp, sbw, ssax + x, ssay + y);
                if (srcOff + 4 <= m_vramSize)
                {
                    m_localToHostBuffer.push_back(m_vram[srcOff + 0]);
                    m_localToHostBuffer.push_back(m_vram[srcOff + 1]);
                    m_localToHostBuffer.push_back(m_vram[srcOff + 2]);
                }
            }
        }
    }
    else
    {
        uint32_t bytesPerPixel = bpp / 8u;
        if (bytesPerPixel == 0)
            bytesPerPixel = 4;
        uint32_t strideBytes = stridePixels * bytesPerPixel;
        uint32_t rowBytes = rrw * bytesPerPixel;
        m_localToHostBuffer.reserve(rowBytes * rrh);

        for (uint32_t y = 0; y < rrh; ++y)
        {
            if (spsm == GS_PSM_CT32 || spsm == GS_PSM_Z32)
            {
                for (uint32_t x = 0; x < rrw; ++x)
                {
                    const uint32_t srcOff = GSPSMCT32::addrPSMCT32(sbp, sbw, ssax + x, ssay + y);
                    if (srcOff + 4u <= m_vramSize)
                    {
                        m_localToHostBuffer.push_back(m_vram[srcOff + 0u]);
                        m_localToHostBuffer.push_back(m_vram[srcOff + 1u]);
                        m_localToHostBuffer.push_back(m_vram[srcOff + 2u]);
                        m_localToHostBuffer.push_back(m_vram[srcOff + 3u]);
                    }
                }
            }
            else
            {
                uint32_t srcOff = base + (ssay + y) * strideBytes + ssax * bytesPerPixel;
                if (srcOff + rowBytes <= m_vramSize)
                {
                    for (uint32_t i = 0; i < rowBytes; ++i)
                        m_localToHostBuffer.push_back(m_vram[srcOff + i]);
                }
            }
        }
    }
}

bool GS::clearFramebufferContext(uint32_t contextIndex, uint32_t rgba)
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    return clearFramebufferRect(m_vram, m_vramSize, m_ctx[(contextIndex != 0u) ? 1 : 0], rgba);
}

bool GS::clearActiveFramebuffer(uint32_t rgba)
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    return clearFramebufferRect(m_vram, m_vramSize, activeContext(), rgba);
}

uint32_t GS::consumeLocalToHostBytes(uint8_t *dst, uint32_t maxBytes)
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    if (!dst || maxBytes == 0)
        return 0;
    size_t avail = m_localToHostBuffer.size() - m_localToHostReadPos;
    if (avail == 0)
        return 0;
    size_t toCopy = (avail < maxBytes) ? avail : static_cast<size_t>(maxBytes);
    std::memcpy(dst, m_localToHostBuffer.data() + m_localToHostReadPos, toCopy);
    m_localToHostReadPos += toCopy;
    return static_cast<uint32_t>(toCopy);
}
