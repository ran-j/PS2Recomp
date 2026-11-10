#pragma once

#include "runtime/gs/gs_backend.h"
#include "runtime/gs/gs_texture_page_cache.h"

#include <array>
#include <bitset>
#include <condition_variable>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

struct GSRegisters;

class GSCpuBackend final : public GSRasterBackend
{
public:
    GSCpuBackend();
    ~GSCpuBackend() override;

    // Threads (including the caller) that rasterize batched primitives; 0 picks the default
    // (PS2X_GS_THREADS, else all cores but two). 1 draws every primitive immediately.
    void SetRasterThreads(uint32_t participants);

    // Runs commands that only change local memory on a GS thread, in submission order, while the caller
    // continues; anything that reads local memory or transfer state first waits for the queue to drain,
    // except presentation, which shows the frame captured at the latest vblank the GS thread has reached.
    void SetAsync(bool enabled);

    // The guest's privileged GS registers. While the GS thread lags behind the EE, DISPFB already names a
    // buffer it is still clearing or drawing; local memory and the display registers are captured in
    // stream order at each vblank so presentation shows a frame the guest had finished.
    void SetDisplayRegisters(const GSRegisters *regs);

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
    struct Command
    {
        enum class Kind : uint8_t
        {
            Submit,
            LoadClut,
            BeginTransfer,
            UploadImage,
            TextureFlush,
            ClearFramebuffer,
            WriteVram,
            CaptureFrame,
        };
        Kind kind = Kind::Submit;
        GSPrimitiveBatch batch{};
        GSTex0Reg tex0{};
        GSTexClutReg texclut{};
        GSTransferCommand transfer{};
        GSContext context{};
        uint32_t args[6]{};
        GSPresentationRequest display{};
        std::vector<uint8_t> data;
    };

    void Enqueue(Command &&command);
    void WaitForQueue() const;
    void RunQueue();
    void Execute(Command &command);

    void InitializeNow(uint8_t *vram, uint32_t vramSize);
    void ResetNow();
    void SubmitNow(const GSPrimitiveBatch &batch);
    void LoadClutNow(const GSTex0Reg &tex0, const GSTexClutReg &texclut);
    void BeginTransferNow(const GSTransferCommand &command);
    void UploadImageNow(const uint8_t *data, uint32_t sizeBytes);
    void FlushNow();
    void TextureFlushNow();
    void SyncNow(GSSyncReason reason);
    bool ClearFramebufferNow(const GSContext &context, uint32_t rgba);
    uint32_t ConsumeLocalToHostBytesNow(uint8_t *dst, uint32_t maxBytes);
    uint32_t ReadVramNow(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y) const;
    void WriteVramNow(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y, uint32_t value);
    void SnapshotVramNow(std::vector<uint8_t> &out) const;
    void CaptureFrameNow(const GSPresentationRequest &display);
    GSTransferSnapshot GetTransferSnapshotNow() const;

    void ResetUnlocked();
    void LoadClutUnlocked(const GSTex0Reg &tex0, const GSTexClutReg &texclut);
    uint32_t ReadVramUnlocked(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y) const;
    uint32_t ReadTextureVramUnlocked(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y);
    void WriteVramUnlocked(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y, uint32_t value);

    // Where a rasterizer pass writes and samples: all rows, or one band (a contiguous range of rows).
    struct RasterTrace
    {
        uint32_t primitive = 0;
        int y = -1;
        int x = -1;
        bool sampled = false;
    };
    // A word a tracked batch overwrote in a page it also samples, with the pixel that overwrote it.
    struct UndoEntry
    {
        uint32_t primitive = 0;
        int y = 0;
        int x = 0;
        uint32_t offset = 0;
        uint32_t old = 0;
    };
    // PS2X_GS_STATS counters a rasterizer pass keeps locally and adds to the totals once it ends.
    struct RasterStats
    {
        uint64_t boxPixels = 0;
        uint64_t insidePixels = 0;
        uint64_t writtenPixels = 0;
        uint64_t texSamples = 0;
        // Triangles whose clipped bounding box is at least 64K pixels.
        uint64_t bigBoxPixels = 0;
        uint64_t bigInsidePixels = 0;
    };
    static void AddRasterStats(const RasterStats &stats);
    // Why queued primitives were rasterized; indexes the PS2X_GS_STATS flush counters.
    enum class FlushReason : uint8_t
    {
        Full,
        TargetChange,
        TargetOverlap,
        ReadsPendingWrites,
        WritesPendingReads,
        TrackMismatch,
        DrawAlone,
        Clut,
        Transfer,
        TextureFlush,
        Capture,
        Idle,
        Readback,
        Other,
        Count,
    };
    struct RasterContext
    {
        int rowBegin = std::numeric_limits<int>::min();
        int rowEnd = std::numeric_limits<int>::max();
        GSMem::TexturePageCache *cache = nullptr;
        RasterTrace *trace = nullptr;
        uint32_t primitive = 0;
        // The palette as it was when a queued primitive was submitted; null uses the current CLUT.
        const uint32_t *clut = nullptr;
        // Set when the band's cache records read positions (see RasterizeTrackedInParallel).
        bool trackReads = false;
        // Where a tracked batch logs what it overwrites in undoPages, so a cached copy can be rebuilt.
        std::vector<UndoEntry> *undo = nullptr;
        const std::bitset<512> *undoPages = nullptr;
        RasterStats *stats = nullptr;

        bool ownsRow(int y) const
        {
            return y >= rowBegin && y < rowEnd;
        }
        int firstRow(int y) const
        {
            return y > rowBegin ? y : rowBegin;
        }
        int lastRow(int y) const
        {
            return y < rowEnd - 1 ? y : rowEnd - 1;
        }
        void beginSample(int y, int x) const
        {
            if (trackReads)
                cache->BeginPixel(primitive, y, x);
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
    // Estimated pixels per group of 8 rows, which is the unit row ranges are split at.
    static constexpr uint32_t kRowGroups = 256u;
    using RowWeights = std::array<uint64_t, kRowGroups>;
    struct RowRange
    {
        int begin = 0;
        int end = 0;
    };
    static void BalanceRows(const RowWeights &weights, uint32_t participants, std::vector<RowRange> &ranges);

    void DrawPrimitive(const GSPrimitiveBatch &batch, const uint32_t *clut = nullptr);
    void DrawPrimitiveRows(const GSPrimitiveBatch &batch, const RasterContext &raster);
    void PrepareTexturePageCache(const GSDrawState &state);
    void QueuePrimitive(const GSPrimitiveBatch &batch);
    void FlushPendingUnlocked(FlushReason reason = FlushReason::Other);
    void RasterizePendingInParallel();
    void RasterizeTrackedInParallel();
    void ClearPendingUnlocked();
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
    static PixelPipeline MakePixelPipeline(const GSDrawState &state, const RasterContext &raster);
    void RecordUndo(const PixelPipeline &pipe, const uint8_t *pixel, int x, int y);
    void WritePixel(const GSDrawState &state, int x, int y, int z, uint8_t r, uint8_t g, uint8_t b, uint8_t a, uint8_t fog);
    // True when the pixel passed every test and something was stored.
    bool WritePixel(const PixelPipeline &pipe, int x, int y, int z, uint8_t r, uint8_t g, uint8_t b, uint8_t a, uint8_t fog);
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
        GSMem::TextureQuadFetchFn fetchQuad = nullptr;
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
    static uint32_t ConvertTexel(const TextureSampler &sampler, uint32_t raw);
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
    std::bitset<512> m_pendingFramePages;
    std::bitset<512> m_pendingDepthPages;
    // Pages sampled whole (not just the top byte of 32-bit words), and pages written in formats that
    // change that top byte.
    std::bitset<512> m_pendingFullTexturePages;
    std::bitset<512> m_pendingTopWritePages;
    // A tracked batch samples pages it also draws (top bytes under RGB24/Z24 writes only), so it records
    // reads and overwritten words to leave the texture page cache exactly as a serial draw would.
    bool m_pendingTracked = false;
    // Every queued primitive could join a tracked batch: textured sprites or triangles writing the same pages.
    bool m_pendingTrackable = true;
    uint64_t m_pendingSpanKey = 0;
    uint64_t m_pendingTargetKey = 0;
    uint64_t m_pendingPixels = 0;
    RowWeights m_pendingRowWeights{};
    std::unique_ptr<RasterWorkers> m_workers;

    bool m_async = false;
    std::thread m_gsThread;
    mutable std::mutex m_queueMutex;
    mutable std::condition_variable m_queueReady;
    mutable std::condition_variable m_queueProgress;
    std::deque<Command> m_queue;
    uint64_t m_queueSubmitted = 0;
    uint64_t m_queueCompleted = 0;
    bool m_queueStop = false;
    const GSRegisters *m_displayRegs = nullptr;
    uint64_t m_lastQueuedVSync = 0;
    // Written by the GS thread at each vblank in the stream, read by presentation.
    mutable std::mutex m_captureMutex;
    std::vector<uint8_t> m_capturedVram;
    GSPresentationRequest m_capturedDisplay{};
    std::vector<uint8_t> m_captureScratch;
    uint32_t m_rasterParticipants = 0;
    // Rebuilds the texture cache's copied page after a parallel draw into the page it holds.
    std::vector<uint8_t> m_pageRebuildScratch;
    std::vector<std::vector<UndoEntry>> m_undoLogs;
};
