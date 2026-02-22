#include "ps2_gs_gpu.h"
#include "raylib.h"
#include "rlgl.h"

GsGpuFrameData::GsGpuFrameData()
{
    m_buffers[0].reserve(8192);
    m_buffers[1].reserve(8192);
}

void GsGpuFrameData::pushPrimitive(const GsGpuPrimitive &prim)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_buffers[m_backIdx].push_back(prim);
    m_hasData.store(true, std::memory_order_relaxed);
}

const std::vector<GsGpuPrimitive> &GsGpuFrameData::swapAndGetFront()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    int frontIdx = m_backIdx;
    m_backIdx = 1 - m_backIdx;
    m_buffers[m_backIdx].clear();
    m_hasData.store(false, std::memory_order_relaxed);
    return m_buffers[frontIdx];
}

bool GsGpuFrameData::hasGpuPrimitives() const
{
    return m_hasData.load(std::memory_order_relaxed);
}

GsGpuFrameData &gsGpuGetFrameData()
{
    static GsGpuFrameData instance;
    return instance;
}

bool gsGpuRenderFrame()
{
    GsGpuFrameData &fd = gsGpuGetFrameData();
    const std::vector<GsGpuPrimitive> &prims = fd.swapAndGetFront();

    if (prims.empty())
    {
        return false;
    }

    const float screenW = static_cast<float>(fd.screenWidth());
    const float screenH = static_cast<float>(fd.screenHeight());

    // Set up 2D orthographic projection matching PS2 screen coords
    rlMatrixMode(RL_PROJECTION);
    rlPushMatrix();
    rlLoadIdentity();
    rlOrtho(0.0, static_cast<double>(screenW),
            static_cast<double>(screenH), 0.0,
            -1.0, 1.0);

    rlMatrixMode(RL_MODELVIEW);
    rlPushMatrix();
    rlLoadIdentity();

    // Disable depth test for 2D rendering (PS2 GS handles Z separately)
    rlDisableDepthTest();

    // Disable backface culling — PS2 games rely on both winding orders
    rlDisableBackfaceCulling();

    // Render each primitive
    for (const GsGpuPrimitive &prim : prims)
    {
        switch (prim.type)
        {
        case GS_GPU_TRIANGLE:
        {
            rlBegin(RL_TRIANGLES);
            for (int i = 0; i < 3; ++i)
            {
                const GsGpuVertex &v = prim.verts[i];
                rlColor4ub(v.r, v.g, v.b, v.a);
                rlVertex3f(v.x, v.y, v.z);
            }
            rlEnd();
            break;
        }

        case GS_GPU_QUAD:
        {
            // QUAD: v0=top-left, v1=top-right, v2=bottom-left, v3=bottom-right
            // Raylib RL_QUADS expects: v0, v1, v2, v3 in order
            rlBegin(RL_QUADS);
            for (int i = 0; i < 4; ++i)
            {
                const GsGpuVertex &v = prim.verts[i];
                rlColor4ub(v.r, v.g, v.b, v.a);
                rlVertex3f(v.x, v.y, v.z);
            }
            rlEnd();
            break;
        }

        case GS_GPU_LINE:
        {
            rlBegin(RL_LINES);
            for (int i = 0; i < 2; ++i)
            {
                const GsGpuVertex &v = prim.verts[i];
                rlColor4ub(v.r, v.g, v.b, v.a);
                rlVertex3f(v.x, v.y, v.z);
            }
            rlEnd();
            break;
        }

        case GS_GPU_POINT:
        {
            const GsGpuVertex &v = prim.verts[0];
            rlBegin(RL_TRIANGLES);
            rlColor4ub(v.r, v.g, v.b, v.a);
            rlVertex3f(v.x - 0.5f, v.y - 0.5f, v.z);
            rlVertex3f(v.x + 0.5f, v.y - 0.5f, v.z);
            rlVertex3f(v.x, v.y + 0.5f, v.z);
            rlEnd();
            break;
        }
        }
    }

    rlDrawRenderBatchActive();

    rlEnableBackfaceCulling();
    rlEnableDepthTest();

    rlMatrixMode(RL_MODELVIEW);
    rlPopMatrix();
    rlMatrixMode(RL_PROJECTION);
    rlPopMatrix();

    return true;
}
