#pragma once

#include "runtime/gs/gs_backend.h"

class GSOpenGLBackend final : public GSRasterBackend
{
public:
    bool Initialize(uint8_t* memory, size_t size) override;
    void Reset() override;
    void Submit(const GSPrimitiveBatch& batch) override;
    PresentationFrame Present() override;

private:
    bool CreateShaders();
    bool CreateFramebuffer();
    bool UploadTextures();
};
