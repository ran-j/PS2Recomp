#pragma once

#include "runtime/gs/gs_backend.h"
#include <memory>

class GSCpuBackend;

// Fase de integración: comprueba que existe un contexto OpenGL ES 3.2
// y conserva el comportamiento GS actual delegando las operaciones al backend CPU.
// IMPORTANTE: todavía no rasteriza las primitivas GS en la GPU.
class GSOpenGLBackend final : public GSRasterBackend
{
public:
    GSOpenGLBackend();
    ~GSOpenGLBackend() override;

    bool IsOpenGLES32Available() const;
    const char* GetOpenGLVersion() const;

    void Initialize(uint8_t* vram, uint32_t vramSize) override;
    void Reset() override;
    void Submit(const GSPrimitiveBatch& batch) override;
    void LoadClut(const GSTex0Reg& tex0, const GSTexClutReg& texclut) override;
    void BeginTransfer(const GSTransferCommand& command) override;
    void UploadImage(const uint8_t* data, uint32_t sizeBytes) override;
    void Flush() override;
    void TextureFlush() override;
    void Sync(GSSyncReason reason) override;
    PresentationFrame Present(const GSPresentationRequest& request) override;
    bool ClearFramebuffer(const GSContext& context, uint32_t rgba) override;
    uint32_t ConsumeLocalToHostBytes(uint8_t* dst, uint32_t maxBytes) override;
    uint32_t ReadVram(uint32_t psm, uint32_t base, uint32_t bw,
                      uint32_t x, uint32_t y) const override;
    void WriteVram(uint32_t psm, uint32_t base, uint32_t bw,
                   uint32_t x, uint32_t y, uint32_t value) override;
    void SnapshotVram(std::vector<uint8_t>& out) const override;
    GSTransferSnapshot GetTransferSnapshot() const override;

private:
    std::unique_ptr<GSCpuBackend> m_cpu;
    bool m_gles32Available = false;
    char m_glVersion[256]{};
};
