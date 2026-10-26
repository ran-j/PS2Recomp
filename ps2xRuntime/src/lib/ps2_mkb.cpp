#include "runtime/ps2_mkb.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

namespace ps2x
{
    namespace
    {
        bool isSpace(char c)
        {
            return c == ' ' || c == '\t' || c == '\r';
        }

        std::string trim(const std::string &s)
        {
            size_t b = 0;
            size_t e = s.size();
            while (b < e && isSpace(s[b]))
                ++b;
            while (e > b && isSpace(s[e - 1]))
                --e;
            return s.substr(b, e - b);
        }

        // Strip a trailing ';' comment (INI convention) from a value.
        std::string stripComment(const std::string &s)
        {
            const size_t pos = s.find(';');
            return pos == std::string::npos ? s : s.substr(0, pos);
        }

        // DualShock stick byte for a normalized component in [-1,1]:
        // 128 = center, 128 + v*127 rounded half-away-from-zero (the
        // gamepad path uses the same 128 + v*127 mapping).
        uint8_t stickByte(float v)
        {
            v = std::clamp(v, -1.0f, 1.0f);
            const float rounded = std::lround(v * 127.0f);
            return static_cast<uint8_t>(128 + static_cast<int>(rounded));
        }
    }

    bool MkbLoadConfig(const char *path, MkbConfig &out)
    {
        if (path == nullptr || path[0] == '\0')
            return false;

        std::ifstream in(path);
        if (!in)
            return false;

        bool inSection = false;
        bool sawSection = false;
        std::string line;
        while (std::getline(in, line))
        {
            const std::string s = trim(line);
            if (s.empty() || s[0] == '#' || s[0] == ';')
                continue;

            if (s[0] == '[')
            {
                const size_t close = s.rfind(']');
                inSection = close != std::string::npos && trim(s.substr(1, close - 1)) == "mkb";
                if (inSection)
                    sawSection = true;
                continue;
            }

            if (!inSection)
                continue;

            const size_t eq = s.find('=');
            if (eq == std::string::npos)
                continue;

            const std::string key = trim(s.substr(0, eq));
            const std::string value = trim(stripComment(s.substr(eq + 1)));
            if (key == "sens")
            {
                const float v = std::strtof(value.c_str(), nullptr);
                if (v > 0.0f)
                    out.sens = v;
            }
            else if (key == "deadzone")
            {
                const float v = std::strtof(value.c_str(), nullptr);
                out.deadzone = std::clamp(v, 0.0f, 1.0f);
            }
            else if (key == "invert_y")
            {
                out.invertY = std::atoi(value.c_str()) != 0 ? 1 : 0;
            }
        }
        return sawSection;
    }

    void MkbApplyMouseDelta(float &rx, float &ry, float dx, float dy,
                            const MkbConfig &cfg)
    {
        rx += dx * cfg.sens;
        ry += (cfg.invertY != 0 ? -dy : dy) * cfg.sens;
        rx = std::clamp(rx, -1.0f, 1.0f);
        ry = std::clamp(ry, -1.0f, 1.0f);
        rx *= kMkbStickDecayPerFrame;
        ry *= kMkbStickDecayPerFrame;
    }

    void MkbStickToBytes(float rx, float ry, const MkbConfig &cfg,
                         uint8_t &outRx, uint8_t &outRy)
    {
        const float dz = std::clamp(cfg.deadzone, 0.0f, 1.0f);
        const float mag = std::sqrt(rx * rx + ry * ry);
        if (mag < dz || mag <= 0.0f)
        {
            outRx = 128u;
            outRy = 128u;
            return;
        }

        // Radial rescale: deflection magnitude from [dz..1] -> [0..1], so
        // the stick output is continuous at the deadzone edge.
        const float scale = (mag - dz) / (1.0f - dz);
        const float ux = rx / mag;
        const float uy = ry / mag;
        outRx = stickByte(ux * scale);
        outRy = stickByte(uy * scale);
    }

    uint16_t MkbMouseButtonsToPad(uint32_t mouseButtons)
    {
        uint16_t pad = 0u;
        if (mouseButtons & 1u) // MOUSE_BUTTON_LEFT
            pad |= 0x4000u;    // CROSS - fire
        if (mouseButtons & 2u) // MOUSE_BUTTON_RIGHT
            pad |= 0x0400u;    // L1 - aim down sights
        return pad;
    }

    void MkbLogStartupStatus()
    {
        static const bool logged = []()
        {
            const char *enable = std::getenv("PS2X_MKB");
            const bool enabled = enable != nullptr && enable[0] == '1' && enable[1] == '\0';
            const char *env = std::getenv("PS2X_MKB_CONFIG");
            const char *path = (env != nullptr && env[0] != '\0') ? env : "mkb.ini";
            MkbConfig cfg;
            const bool loaded = MkbLoadConfig(path, cfg);
            if (!enabled)
            {
                std::fprintf(stderr, "[mkb] mouse-look disabled (PS2X_MKB unset)\n");
                return true;
            }
            if (loaded)
                std::fprintf(stderr,
                             "[mkb] mouse-look enabled: config=%s sens=%.3f "
                             "deadzone=%.3f invert_y=%d decay=%.2f\n",
                             path, static_cast<double>(cfg.sens),
                             static_cast<double>(cfg.deadzone), cfg.invertY,
                             static_cast<double>(kMkbStickDecayPerFrame));
            else
                std::fprintf(stderr,
                             "[mkb] mouse-look enabled: config=%s not found, "
                             "defaults sens=%.3f deadzone=%.3f invert_y=%d "
                             "decay=%.2f\n",
                             path, static_cast<double>(cfg.sens),
                             static_cast<double>(cfg.deadzone), cfg.invertY,
                             static_cast<double>(kMkbStickDecayPerFrame));
            return true;
        }();
        (void)logged;
    }
}
