#include "gs_opengl_backend.h"

#include <cstring>

GSOpenGLBackend::GSOpenGLBackend() = default;

GSOpenGLBackend::~GSOpenGLBackend()
{
    Reset();
}

bool GSOpenGLBackend::Initialize(
    uint8_t* vram,
    uint32_t vramSize)
{
#ifdef __ANDROID__
    const char* version =
        reinterpret_cast<const char*>(
            glGetString(GL_VERSION)
        );

    if (!version)
        return false;

    m_vram = vram;
    m_vramSize = vramSize;

    GLint major = 0;
    GLint minor = 0;

    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);

    if (major < 3 || (major == 3 && minor < 2))
        return false;

    m_initialized = InitializeShaders()
                 && InitializeBuffers()
                 && InitializeTextures();

    return m_initialized;
#else
    return false;
#endif
}

bool GSOpenGLBackend::IsAvailable() const
{
    return m_initialized;
}
