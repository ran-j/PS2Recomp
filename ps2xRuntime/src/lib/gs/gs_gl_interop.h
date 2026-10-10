#pragma once
#include "runtime/gs/gs_types.h"
#include "gs_renderer.hpp"

class GSGLInterop
{
public:
    GSGLInterop();
    ~GSGLInterop();
    Vulkan::Device &device();
    PresentationFrame present(const ParallelGS::ScanoutResult &scanout);
private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};
