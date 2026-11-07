#pragma once

#include "gs_backend.h"

#ifdef __ANDROID__
#include <EGL/egl.h>
#include <GLES3/gl32.h>
#endif

class GSOpenGLBackend : public GSBackend
{
public:
    GSOpenGLBackend();

    ~GSOpenGLBackend() override;

    bool Initialize(
        uint8_t* vram,
        uint32_t vramSize
    ) override;

    void Reset() override;

    void Flush() override;

    void Sync() override;

    bool IsAvailable() const override;

private:
    bool InitializeShaders();
    bool InitializeBuffers();
    bool InitializeTextures();

    GLuint m_program = 0;
    GLuint m_vertexBuffer = 0;
    GLuint m_vertexArray = 0;
    GLuint m_framebuffer = 0;
    GLuint m_texture = 0;

    uint8_t* m_vram = nullptr;
    uint32_t m_vramSize = 0;

    bool m_initialized = false;
};
