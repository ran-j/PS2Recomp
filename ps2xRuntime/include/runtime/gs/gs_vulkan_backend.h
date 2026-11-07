#pragma once

#include "runtime/gs/gs_backend.h"

class GSVulkanBackend final : public GSRasterBackend
{
public:
    bool Initialize(uint8_t* memory, size_t size) override;
    void Reset() override;
    void Submit(const GSPrimitiveBatch& batch) override;
    PresentationFrame Present() override;

private:
    bool CreateInstance();
    bool CreateDevice();
    bool CreateSwapchain();
    bool CreateRenderPass();
    bool CreatePipeline();
};
