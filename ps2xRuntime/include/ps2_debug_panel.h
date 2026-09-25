#ifndef PS2_DEBUG_PANEL_H
#define PS2_DEBUG_PANEL_H

#include <array>
#include <chrono>
#include <cstdint>

class PS2Runtime;

class PS2DebugPanel
{
public:
    void initialize();
    void shutdown();
    void draw(PS2Runtime &runtime);

    bool isVisible() const { return m_visible; }
    void setVisible(bool visible) { m_visible = visible; }
    void toggleVisible() { m_visible = !m_visible; }

private:
    bool m_initialized = false;
    bool m_visible = true;
    bool m_showRegisters = true;
    unsigned int m_memoryAddress = 0x00100000u;
    unsigned int m_memoryBytes = 0x100u;
    std::chrono::steady_clock::time_point m_fpsSampleStart{};
    std::array<uint64_t, 2> m_lastDisplayFlips{};
    uint64_t m_lastSdkPresents = 0;
    uint64_t m_hostFramesInSample = 0;
    double m_gameFps = 0.0;
    double m_hostFps = 0.0;
};

#endif // PS2_DEBUG_PANEL_H
