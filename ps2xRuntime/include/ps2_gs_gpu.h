#ifndef PS2_GS_GPU_H
#define PS2_GS_GPU_H

#include <cstdint>
#include <vector>
#include <mutex>
#include <atomic>

enum GsGpuPrimType : uint8_t
{
    GS_GPU_POINT = 0,
    GS_GPU_LINE = 1,
    GS_GPU_TRIANGLE = 2,
    GS_GPU_QUAD = 3,
};

struct GsGpuVertex
{
    float x, y, z;      // screen-space position (after PS2 12.4 fixed → float)
    uint8_t r, g, b, a; // vertex color
    float u, v;         // texture coords (for future use)
};

struct GsGpuPrimitive
{
    GsGpuPrimType type;
    uint8_t vertexCount; // 1 (point), 2 (line), 3 (tri), 4 (quad)
    GsGpuVertex verts[4];
};

class GsGpuFrameData
{
public:
    GsGpuFrameData();

    void pushPrimitive(const GsGpuPrimitive &prim);

    const std::vector<GsGpuPrimitive> &swapAndGetFront();

    bool hasGpuPrimitives() const;

    void setScreenSize(uint32_t w, uint32_t h)
    {
        m_screenW = w;
        m_screenH = h;
    }
    uint32_t screenWidth() const { return m_screenW; }
    uint32_t screenHeight() const { return m_screenH; }

private:
    std::vector<GsGpuPrimitive> m_buffers[2];
    int m_backIdx = 0; // index into m_buffers for the current write target
    mutable std::mutex m_mutex;
    std::atomic<bool> m_hasData{false};
    uint32_t m_screenW = 640;
    uint32_t m_screenH = 448;
};

GsGpuFrameData &gsGpuGetFrameData();
bool gsGpuRenderFrame();

#endif // PS2_GS_GPU_H
