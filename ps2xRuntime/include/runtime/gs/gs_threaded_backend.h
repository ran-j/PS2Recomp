#pragma once

#include "runtime/gs/gs_backend.h"

#include <cstddef>
#include <memory>

// Owns submitted data until the worker consumes it. Observations and GS
// completion barriers drain the ordered queue; presentation stays on its caller.
class GSThreadedBackend final : public GSRasterBackend
{
public:
    explicit GSThreadedBackend(std::unique_ptr<GSRasterBackend> backend,
                               size_t queueBytes = 4u * 1024u * 1024u);
    ~GSThreadedBackend() override;

    void Initialize(uint8_t *vram, uint32_t size) override;
    void Reset() override;
    void Submit(const GSPrimitiveBatch &batch) override;
    void BeginTransfer(const GSTransferCommand &command) override;
    void UploadImage(const uint8_t *data, uint32_t size) override;
    void Flush() override;
    void TextureFlush() override;
    void Sync(GSSyncReason reason) override;
    PresentationFrame Present(const GSPresentationRequest &request) override;
    bool SupportsPreparedPresentation() const override;
    bool QueuesPreparedPresentation() const override { return SupportsPreparedPresentation(); }
    GSPresentationTicket PreparePresentation(const GSPresentationRequest &request) override;
    PresentationFrame DisplayPreparedPresentation(const GSPresentationTicket &ticket) override;
    void CancelPreparedPresentations() noexcept override;
    bool ClearFramebuffer(const GSContext &context, uint32_t rgba) override;
    uint32_t ConsumeLocalToHostBytes(uint8_t *dst, uint32_t size) override;
    uint32_t ReadVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y) const override;
    void WriteVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y, uint32_t value) override;
    void SnapshotVram(std::vector<uint8_t> &out) const override;
    GSTransferSnapshot GetTransferSnapshot() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
