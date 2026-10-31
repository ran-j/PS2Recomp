#pragma once

#include "runtime/gs/gs_backend.h"
#include "runtime/gs/gs_texture_page_cache.h"

#include <array>
#include <bitset>
#include <memory>
#include <mutex>
#include <vector>

class GSCpuBackend final : public GSRasterBackend
{
public:
    GSCpuBackend();
    ~GSCpuBackend() override;

    // Threads (including the caller) that rasterize batched primitives; 0 picks the default
    // (PS2X_GS_THREADS, else half the cores). 1 draws every primitive immediately.
    void SetRasterThreads(uint32_t participants);

    void Initialize(uint8_t *vram, uint32_t vramSize) override;
    void Reset() override;

    void Submit(const GSPrimitiveBatch &batch) override;
    void LoadClut(const GSTex0Reg &tex0, const GSTexClutReg &texclut) override;
    void BeginTransfer(const GSTransferCommand &command) override;
    void UploadImage(const uint8_t *data, uint32_t sizeBytes) override;

    void Flush() override;
    void TextureFlush() override;
    void Sync(GSSyncReason reason) override;
    PresentationFrame Present(const GSPresentationRequest &request) override;

    bool ClearFramebuffer(const GSContext &context, uint32_t rgba) override;
    uint32_t ConsumeLocalToHostBytes(uint8_t *dst, uint32_t maxBytes) override;

    uint32_t ReadVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y) const override;
    void WriteVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y, uint32_t value) override;
    void SnapshotVram(std::vector<uint8_t> &out) const override;
    GSTransferSnapshot GetTransferSnapshot() const override;

private:
    void ResetUnlocked();
    void LoadClutUnlocked(const GSTex0Reg &tex0, const GSTexClutReg &texclut);
    uint32_t ReadVramUnlocked(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y) const;
    uint32_t ReadTextureVramUnlocked(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y);
    void WriteVramUnlocked(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y, uint32_t value);

    // Where a rasterizer pass writes and samples: all rows, or every Nth band of 8 rows on a worker.
    struct RasterTrace
    {
        uint32_t primitive = 0;
        int y = -1;
        int x = -1;
        bool sampled = false;
    };
    struct RasterContext
    {
        uint32_t workers = 1;
        uint32_t index = 0;
        GSMem::TexturePageCache *cache = nullptr;
        RasterTrace *trace = nullptr;
        uint32_t primitive = 0;
        // The palette as it was when a queued primitive was submitted; null uses the current CLUT.
        const uint32_t *clut = nullptr;
        // Set when the band's cache records read positions (see RasterizeTopByteAliasInParallel).
        bool trackReads = false;

        bool ownsRow(int y) const
        {
            return workers == 1u || ((static_cast<uint32_t>(y) >> 3u) % workers) == index;
        }
        void beginSample(int y, int x) const
        {
            if (trackReads)
                cache->BeginPixel(y, x);
        }
        void sampled(int y, int x) const
        {
            if (trace)
            {
                trace->primitive = primitive;
                trace->y = y;
                trace->x = x;
                trace->sampled = true;
            }
        }
    };
    class RasterWorkers;

    void DrawPrimitive(const GSPrimitiveBatch &batch, const uint32_t *clut = nullptr);
    void DrawPrimitiveRows(const GSPrimitiveBatch &batch, const RasterContext &raster);
    void PrepareTexturePageCache(const GSDrawState &state);
    void QueuePrimitive(const GSPrimitiveBatch &batch);
    void FlushPendingUnlocked();
    void RasterizePendingInParallel();
    bool RasterizeTopByteAliasInParallel(const GSPrimitiveBatch &batch);
    uint32_t RasterParticipants() const;
    void DrawSprite(const GSPrimitiveBatch &batch, const RasterContext &raster);
    void DrawTriangle(const GSPrimitiveBatch &batch, const RasterContext &raster);
    void DrawLine(const GSPrimitiveBatch &batch, const RasterContext &raster);
    // Per-primitive register decoding hoisted out of the per-pixel path.
    struct PixelPipeline;
    // A wrapped texel coordinate along one axis (plus its neighbour and weight when filtering).
    struct TextureAxis
    {
        int c0 = 0;
        int c1 = 0;
        float frac = 0.0f;
    };

    static PixelPipeline MakePixelPipeline(const GSDrawState &state);
    void WritePixel(const GSDrawState &state, int x, int y, int z, uint8_t r, uint8_t g, uint8_t b, uint8_t a, uint8_t fog);
    void WritePixel(const PixelPipeline &pipe, int x, int y, int z, uint8_t r, uint8_t g, uint8_t b, uint8_t a, uint8_t fog);
    // Texture registers decoded once per primitive, with the CLUT expanded to RGBA for indexed formats.
    struct TextureSampler
    {
        const GSDrawState *state = nullptr;
        uint32_t psm = 0;
        uint32_t tbp0 = 0;
        uint32_t tbw = 0;
        const uint32_t *clut = nullptr;
        GSMem::TexturePageCache *cache = nullptr;
        GSMem::TextureFetchFn fetch = nullptr;
        int texW = 1;
        int texH = 1;
        uint8_t wrapU = 0;
        uint8_t wrapV = 0;
        uint16_t minU = 0;
        uint16_t maxU = 0;
        uint16_t minV = 0;
        uint16_t maxV = 0;
        bool linear = false;
    };
    TextureSampler MakeTextureSampler(const GSDrawState &state, GSMem::TexturePageCache &cache, const uint32_t *clut = nullptr);
    void ExpandClut(const GSDrawState &state, uint32_t *rgba);
    uint32_t SampleTexture(const TextureSampler &sampler, float s, float t, float q, uint16_t u, uint16_t v);
    static TextureAxis PrepareTextureAxis(float coordinate, int size, uint8_t mode, uint16_t regionMin, uint16_t regionMax, bool linear);
    uint32_t SampleTexel(const TextureSampler &sampler, int u, int v);
    uint32_t SampleTextureAxes(const TextureSampler &sampler, const TextureAxis &u, const TextureAxis &v);
    uint32_t LookupCLUT(const GSDrawState &state, uint8_t index, uint8_t cpsm, uint8_t csm, uint8_t csa, uint8_t sourcePsm);

    void PerformLocalToLocalTransfer();
    void PerformLocalToHostTransfer();
    PresentationFrame PresentFromLocalMemory(const GSPresentationRequest &request);
    bool CopyFrameToHostRgba(const GSFrameReg &frame,
                             uint32_t width,
                             uint32_t height,
                             std::vector<uint8_t> &outPixels,
                             bool preserveAlpha,
                             bool useLocalMemoryLayout,
                             bool frameBaseIsPages,
                             uint32_t sourceOriginX,
                             uint32_t sourceOriginY) const;

    using WriteVramFunc = void (*)(uint8_t *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    using ReadVramFunc = uint32_t (*)(uint8_t *, uint32_t, uint32_t, uint32_t, uint32_t);

    static constexpr size_t kPsmHandlerCount = 1u << 6u;
    mutable std::mutex m_mutex;
    uint8_t *m_vram = nullptr;
    uint32_t m_vramSize = 0;
    std::array<ReadVramFunc, kPsmHandlerCount> m_readVramFuncs{};
    std::array<WriteVramFunc, kPsmHandlerCount> m_writeVramFuncs{};
    std::array<uint16_t, 512> m_clut{};
    std::array<uint32_t, 2> m_clutCbp{};
    // Bumped whenever m_clut changes so expanded palettes cached per thread are rebuilt.
    uint64_t m_clutGeneration = 0;
    GSMem::TexturePageCache m_texturePageCache;

    GSTransferCommand m_transfer{};
    GSTransferSnapshot m_transferState{};
    std::vector<uint8_t> m_localToHostBuffer;
    size_t m_localToHostReadPos = 0;
    // Host->local data is one byte stream across IMAGE packets and HWREG writes; a CT24/CT16 pixel can
    // straddle two of them, so the leading bytes of a split pixel wait here for the rest.
    std::array<uint8_t, 4> m_uploadCarry{};
    uint32_t m_uploadCarryBytes = 0;

    // Primitives that touch disjoint memory from each other's texture reads are batched and rasterized by
    // row bands on worker threads; anything that reads or writes VRAM otherwise flushes the batch first.
    std::vector<GSPrimitiveBatch> m_pending;
    // Indexed textures keep the palette they were queued with: CLUT loads run immediately.
    std::vector<std::array<uint32_t, 256>> m_pendingCluts;
    std::vector<int32_t> m_pendingClutIndex;
    uint64_t m_pendingClutGeneration = UINT64_MAX;
    uint64_t m_pendingClutKey = 0;
    // First queued primitive after the latest CLUT load; earlier samples no longer decide the cached page.
    size_t m_pendingClutReadIndex = 0;
    std::bitset<512> m_pendingWritePages;
    std::bitset<512> m_pendingTexturePages;
    uint64_t m_pendingTargetKey = 0;
    uint64_t m_pendingPixels = 0;
    std::unique_ptr<RasterWorkers> m_workers;
    uint32_t m_rasterParticipants = 0;
    // Rebuilds the texture cache's copied page after a parallel draw into the page it holds.
    std::vector<uint8_t> m_pageRebuildScratch;
};
