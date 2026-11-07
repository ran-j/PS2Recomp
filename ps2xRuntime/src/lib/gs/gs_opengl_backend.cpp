#include "runtime/gs/gs_opengl_backend.h"
#include "runtime/gs/gs_cpu_backend.h"

#if defined(__ANDROID__)
#include <GLES3/gl32.h>
#include <EGL/egl.h>
#endif

#include <algorithm>
#include <cstring>

GSOpenGLBackend::GSOpenGLBackend()
    : m_cpu(std::make_unique<GSCpuBackend>())
{
#if defined(__ANDROID__)
    // Esto solo es válido después de crear y hacer current el contexto EGL
    // en el hilo que vaya a utilizar OpenGL.
    const GLubyte* version = glGetString(GL_VERSION);
    if (version != nullptr)
    {
        std::strncpy(m_glVersion, reinterpret_cast<const char*>(version),
                     sizeof(m_glVersion) - 1);
        GLint major = 0;
        GLint minor = 0;
        glGetIntegerv(GL_MAJOR_VERSION, &major);
        glGetIntegerv(GL_MINOR_VERSION, &minor);
        m_gles32Available = (major > 3) || (major == 3 && minor >= 2);
    }
#endif
}

GSOpenGLBackend::~GSOpenGLBackend() = default;

bool GSOpenGLBackend::IsOpenGLES32Available() const
{
    return m_gles32Available;
}

const char* GSOpenGLBackend::GetOpenGLVersion() const
{
    return m_glVersion;
}

void GSOpenGLBackend::Initialize(uint8_t* vram, uint32_t vramSize)
{
    m_cpu->Initialize(vram, vramSize);
}
void GSOpenGLBackend::Reset() { m_cpu->Reset(); }
void GSOpenGLBackend::Submit(const GSPrimitiveBatch& batch) { m_cpu->Submit(batch); }
void GSOpenGLBackend::LoadClut(const GSTex0Reg& a, const GSTexClutReg& b) { m_cpu->LoadClut(a, b); }
void GSOpenGLBackend::BeginTransfer(const GSTransferCommand& command) { m_cpu->BeginTransfer(command); }
void GSOpenGLBackend::UploadImage(const uint8_t* data, uint32_t sizeBytes) { m_cpu->UploadImage(data, sizeBytes); }
void GSOpenGLBackend::Flush() { m_cpu->Flush(); }
void GSOpenGLBackend::TextureFlush() { m_cpu->TextureFlush(); }
void GSOpenGLBackend::Sync(GSSyncReason reason) { m_cpu->Sync(reason); }
PresentationFrame GSOpenGLBackend::Present(const GSPresentationRequest& request) { return m_cpu->Present(request); }
bool GSOpenGLBackend::ClearFramebuffer(const GSContext& context, uint32_t rgba) { return m_cpu->ClearFramebuffer(context, rgba); }
uint32_t GSOpenGLBackend::ConsumeLocalToHostBytes(uint8_t* dst, uint32_t maxBytes) { return m_cpu->ConsumeLocalToHostBytes(dst, maxBytes); }
uint32_t GSOpenGLBackend::ReadVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y) const { return m_cpu->ReadVram(psm, base, bw, x, y); }
void GSOpenGLBackend::WriteVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y, uint32_t value) { m_cpu->WriteVram(psm, base, bw, x, y, value); }
void GSOpenGLBackend::SnapshotVram(std::vector<uint8_t>& out) const { m_cpu->SnapshotVram(out); }
GSTransferSnapshot GSOpenGLBackend::GetTransferSnapshot() const { return m_cpu->GetTransferSnapshot(); }
