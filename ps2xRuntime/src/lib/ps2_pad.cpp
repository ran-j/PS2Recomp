#include "runtime/ps2_pad.h"
#include "runtime/ps2_pad_host.h"
#include "ps2_host_backend.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
    constexpr uint8_t kPadAnalogMarker = 0x73;
    constexpr uint8_t kPadStickCenter = 0x80;
    constexpr uint32_t kPadStickNeutral = 0x80808080u;
    constexpr int kGamepad = 0;

    // Drop a latch the guest never came back for, so a tap during a long
    // non-polling stretch (loading, cutscene) does not fire much later.
    constexpr uint64_t kLatchTimeoutMs = 1000u;

    constexpr uint16_t PAD_LEFT = 0x0080u;
    constexpr uint16_t PAD_DOWN = 0x0040u;
    constexpr uint16_t PAD_RIGHT = 0x0020u;
    constexpr uint16_t PAD_UP = 0x0010u;
    constexpr uint16_t PAD_START = 0x0008u;
    constexpr uint16_t PAD_R3 = 0x0004u;
    constexpr uint16_t PAD_L3 = 0x0002u;
    constexpr uint16_t PAD_SELECT = 0x0001u;
    constexpr uint16_t PAD_SQUARE = 0x8000u;
    constexpr uint16_t PAD_CROSS = 0x4000u;
    constexpr uint16_t PAD_CIRCLE = 0x2000u;
    constexpr uint16_t PAD_TRIANGLE = 0x1000u;
    constexpr uint16_t PAD_R1 = 0x0800u;
    constexpr uint16_t PAD_L1 = 0x0400u;
    constexpr uint16_t PAD_R2 = 0x0200u;
    constexpr uint16_t PAD_L2 = 0x0100u;

    // Published by the render thread in ps2PadPollHost(), consumed on the EE thread.
    std::atomic<bool> g_hostPolled{false};
    std::atomic<uint32_t> g_held{0u};              // active-high PAD_* mask
    std::atomic<uint32_t> g_latched{0u};           // press edges not consumed yet
    std::atomic<uint32_t> g_sticks{kPadStickNeutral}; // packed rx,ry,lx,ly
    std::atomic<uint64_t> g_latchStampMs{0u};

    struct KeyBinding
    {
        int key;
        uint16_t mask;
    };

    constexpr KeyBinding kKeyBindings[] = {
        {KEY_UP, PAD_UP},
        {KEY_DOWN, PAD_DOWN},
        {KEY_LEFT, PAD_LEFT},
        {KEY_RIGHT, PAD_RIGHT},
        {KEY_X, PAD_CROSS}, {KEY_SPACE, PAD_CROSS},
        {KEY_C, PAD_CIRCLE}, {KEY_ESCAPE, PAD_CIRCLE},
        {KEY_Z, PAD_SQUARE}, {KEY_KP_0, PAD_SQUARE},
        {KEY_V, PAD_TRIANGLE}, {KEY_KP_1, PAD_TRIANGLE},
        {KEY_Q, PAD_L1},
        {KEY_E, PAD_R1},
        {KEY_LEFT_SHIFT, PAD_L2},
        {KEY_RIGHT_SHIFT, PAD_R2},
        {KEY_ENTER, PAD_START},
        {KEY_TAB, PAD_SELECT},
        {KEY_R, PAD_L3},
        {KEY_F, PAD_R3},
    };

    struct PadBinding
    {
        int button;
        uint16_t mask;
    };

    constexpr PadBinding kPadBindings[] = {
        {GAMEPAD_BUTTON_LEFT_FACE_UP, PAD_UP},
        {GAMEPAD_BUTTON_LEFT_FACE_DOWN, PAD_DOWN},
        {GAMEPAD_BUTTON_LEFT_FACE_LEFT, PAD_LEFT},
        {GAMEPAD_BUTTON_LEFT_FACE_RIGHT, PAD_RIGHT},
        {GAMEPAD_BUTTON_RIGHT_FACE_DOWN, PAD_CROSS},
        {GAMEPAD_BUTTON_RIGHT_FACE_RIGHT, PAD_CIRCLE},
        {GAMEPAD_BUTTON_RIGHT_FACE_LEFT, PAD_SQUARE},
        {GAMEPAD_BUTTON_RIGHT_FACE_UP, PAD_TRIANGLE},
        {GAMEPAD_BUTTON_LEFT_TRIGGER_1, PAD_L1},
        {GAMEPAD_BUTTON_RIGHT_TRIGGER_1, PAD_R1},
        {GAMEPAD_BUTTON_LEFT_TRIGGER_2, PAD_L2},
        {GAMEPAD_BUTTON_RIGHT_TRIGGER_2, PAD_R2},
        {GAMEPAD_BUTTON_MIDDLE_RIGHT, PAD_START},
        {GAMEPAD_BUTTON_MIDDLE_LEFT, PAD_SELECT},
        {GAMEPAD_BUTTON_LEFT_THUMB, PAD_L3},
        {GAMEPAD_BUTTON_RIGHT_THUMB, PAD_R3},
    };

    uint8_t axisToByte(float axis)
    {
        if (axis > -0.125f && axis < 0.125f)
            return kPadStickCenter;
        const float mapped = 128.0f + axis * (axis < 0.0f ? 128.0f : 127.0f);
        return static_cast<uint8_t>(mapped < 0.0f ? 0.0f : (mapped > 255.0f ? 255.0f : mapped));
    }

    uint8_t keyboardAxis(int negative, int positive, uint8_t fallback)
    {
        const bool low = IsKeyDown(negative);
        const bool high = IsKeyDown(positive);
        return low || high ? (low == high ? kPadStickCenter : low ? 0u : 255u) : fallback;
    }

    uint64_t nowMs()
    {
        using namespace std::chrono;
        return static_cast<uint64_t>(
            duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
    }

    uint32_t keyMask(int key)
    {
        uint32_t mask = 0u;
        for (const KeyBinding &binding : kKeyBindings)
        {
            if (binding.key == key)
            {
                mask |= binding.mask;
            }
        }
        return mask;
    }

    // drainQueue must only be true on the render thread: GetKeyPressed() mutates
    // raylib's queue, while every other call here is a plain read.
    void sampleHost(bool drainQueue, uint32_t &held, uint32_t &pressed, uint32_t &sticks)
    {
        held = 0u;
        pressed = 0u;
        sticks = kPadStickNeutral;

        if (IsGamepadAvailable(kGamepad))
        {
            for (const PadBinding &binding : kPadBindings)
            {
                if (IsGamepadButtonDown(kGamepad, binding.button))
                {
                    held |= binding.mask;
                }
                if (IsGamepadButtonPressed(kGamepad, binding.button))
                {
                    pressed |= binding.mask;
                }
            }

            const uint8_t rx = axisToByte(GetGamepadAxisMovement(kGamepad, GAMEPAD_AXIS_RIGHT_X));
            const uint8_t ry = axisToByte(GetGamepadAxisMovement(kGamepad, GAMEPAD_AXIS_RIGHT_Y));
            const uint8_t lx = axisToByte(GetGamepadAxisMovement(kGamepad, GAMEPAD_AXIS_LEFT_X));
            const uint8_t ly = axisToByte(GetGamepadAxisMovement(kGamepad, GAMEPAD_AXIS_LEFT_Y));
            sticks = (static_cast<uint32_t>(rx) << 24) | (static_cast<uint32_t>(ry) << 16) |
                     (static_cast<uint32_t>(lx) << 8) | static_cast<uint32_t>(ly);
        }

        for (const KeyBinding &binding : kKeyBindings)
        {
            if (IsKeyDown(binding.key))
            {
                held |= binding.mask;
            }
        }

        const uint8_t rx = keyboardAxis(KEY_J, KEY_L, static_cast<uint8_t>(sticks >> 24u));
        const uint8_t ry = keyboardAxis(KEY_I, KEY_K, static_cast<uint8_t>(sticks >> 16u));
        const uint8_t lx = keyboardAxis(KEY_A, KEY_D, static_cast<uint8_t>(sticks >> 8u));
        const uint8_t ly = keyboardAxis(KEY_W, KEY_S, static_cast<uint8_t>(sticks));
        sticks = (uint32_t(rx) << 24u) | (uint32_t(ry) << 16u) | (uint32_t(lx) << 8u) | ly;

        if (drainQueue)
        {
            // raylib queues every GLFW press, including one released again inside
            // the same poll, which IsKeyPressed() would already have missed.
            for (int key = GetKeyPressed(); key != 0; key = GetKeyPressed())
            {
                pressed |= keyMask(key);
            }
        }
    }
}

namespace
{
    // Scripted input. DQ8_PAD_SCRIPT is either a file path or the script text
    // itself, entries separated by ';' or newlines:
    //
    //     <guest-frame> <BUTTON>[+<BUTTON>...] [hold-frames]
    //
    // Timed in guest vsync ticks so a script replays the same way whether the
    // game is running at 3 fps or 60. Lines starting with '#' are comments.
    struct PadScriptEvent
    {
        uint64_t frame = 0u;
        uint64_t holdFrames = 4u;
        uint32_t mask = 0u;
        bool active = false;
    };

    std::atomic<uint64_t> g_guestFrame{0u};
    std::vector<PadScriptEvent> g_padScript;
    bool g_padScriptVerbose = false;

    uint32_t padScriptButtonMask(const std::string &name)
    {
        static const std::unordered_map<std::string, uint32_t> kNames = {
            {"UP", PAD_UP}, {"DOWN", PAD_DOWN}, {"LEFT", PAD_LEFT}, {"RIGHT", PAD_RIGHT},
            {"CROSS", PAD_CROSS}, {"CIRCLE", PAD_CIRCLE}, {"SQUARE", PAD_SQUARE},
            {"TRIANGLE", PAD_TRIANGLE}, {"START", PAD_START}, {"SELECT", PAD_SELECT},
            {"L1", PAD_L1}, {"R1", PAD_R1}, {"L2", PAD_L2}, {"R2", PAD_R2},
            {"L3", PAD_L3}, {"R3", PAD_R3},
        };
        std::string upper;
        upper.reserve(name.size());
        for (const char c : name)
        {
            upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
        }
        const auto it = kNames.find(upper);
        return it == kNames.end() ? 0u : it->second;
    }

    void parsePadScript(const std::string &text)
    {
        std::string entry;
        std::istringstream stream(text);
        while (std::getline(stream, entry, '\n'))
        {
            std::string chunk;
            std::istringstream entries(entry);
            while (std::getline(entries, chunk, ';'))
            {
                const size_t hash = chunk.find('#');
                if (hash != std::string::npos)
                {
                    chunk.erase(hash);
                }
                std::istringstream fields(chunk);
                uint64_t frame = 0u;
                std::string buttons;
                if (!(fields >> frame >> buttons))
                {
                    continue;
                }
                PadScriptEvent event{};
                event.frame = frame;
                uint64_t hold = 0u;
                if (fields >> hold && hold > 0u)
                {
                    event.holdFrames = hold;
                }
                size_t start = 0u;
                while (start <= buttons.size())
                {
                    const size_t plus = buttons.find('+', start);
                    const std::string name = buttons.substr(
                        start, plus == std::string::npos ? std::string::npos : plus - start);
                    const uint32_t mask = padScriptButtonMask(name);
                    if (mask == 0u && !name.empty())
                    {
                        std::fprintf(stderr, "[pad] unknown button in DQ8_PAD_SCRIPT: '%s'\n",
                                     name.c_str());
                    }
                    event.mask |= mask;
                    if (plus == std::string::npos)
                    {
                        break;
                    }
                    start = plus + 1u;
                }
                if (event.mask != 0u)
                {
                    g_padScript.push_back(event);
                }
            }
        }
        std::sort(g_padScript.begin(), g_padScript.end(),
                  [](const PadScriptEvent &l, const PadScriptEvent &r) { return l.frame < r.frame; });
    }

    void ensurePadScriptLoaded()
    {
        static const bool loaded = [] {
            const char *value = std::getenv("DQ8_PAD_SCRIPT");
            g_padScriptVerbose = std::getenv("DQ8_PAD_SCRIPT_VERBOSE") != nullptr;
            if (value == nullptr || *value == '\0')
            {
                return true;
            }
            std::ifstream file(value);
            if (file)
            {
                std::ostringstream contents;
                contents << file.rdbuf();
                parsePadScript(contents.str());
            }
            else
            {
                parsePadScript(value);
            }
            std::fprintf(stderr, "[pad] DQ8_PAD_SCRIPT: %zu events\n", g_padScript.size());
            return true;
        }();
        (void)loaded;
    }

    // Merges scripted buttons into whatever the host sampled, so a human can
    // still take over while a script is running.
    void applyPadScript(uint32_t &held, uint32_t &pressed)
    {
        ensurePadScriptLoaded();
        if (g_padScript.empty())
        {
            return;
        }
        const uint64_t frame = g_guestFrame.load(std::memory_order_relaxed);
        for (PadScriptEvent &event : g_padScript)
        {
            const bool on = frame >= event.frame && frame < event.frame + event.holdFrames;
            if (on)
            {
                held |= event.mask;
                if (!event.active)
                {
                    pressed |= event.mask;
                    event.active = true;
                    if (g_padScriptVerbose)
                    {
                        std::fprintf(stderr, "[pad] frame %llu: press 0x%04x\n",
                                     static_cast<unsigned long long>(frame), event.mask);
                    }
                }
            }
            else
            {
                event.active = false;
            }
        }
    }

    // Shared tail of both host paths: publish held state and latch edges long
    // enough that a tap between two guest polls is not lost.
    void publishHostState(uint32_t held, uint32_t pressed, uint32_t sticks)
    {
        applyPadScript(held, pressed);
        g_held.store(held);
        g_sticks.store(sticks);

        const uint64_t now = nowMs();
        if (pressed != 0u)
        {
            if (g_latched.fetch_or(pressed) == 0u)
            {
                g_latchStampMs.store(now);
            }
        }
        else
        {
            const uint32_t stale = g_latched.load();
            if (stale != 0u && (now - g_latchStampMs.load()) > kLatchTimeoutMs)
            {
                g_latched.fetch_and(~stale);
            }
        }

        g_hostPolled.store(true);
    }
}

void ps2PadPublishHostState(uint32_t held, uint32_t pressed, uint32_t sticks)
{
    publishHostState(held, pressed, sticks);
}

void ps2PadSetGuestFrame(uint64_t frame)
{
    g_guestFrame.store(frame, std::memory_order_relaxed);
}

uint64_t ps2PadCurrentGuestFrame()
{
    return g_guestFrame.load(std::memory_order_relaxed);
}

void ps2PadPollHost()
{
    if (!IsWindowReady())
    {
        return;
    }

    uint32_t held = 0u;
    uint32_t pressed = 0u;
    uint32_t sticks = kPadStickNeutral;
    sampleHost(true, held, pressed, sticks);
    publishHostState(held, pressed, sticks);
}

bool PSPadBackend::readState(int /*port*/, int /*slot*/, uint8_t *data, size_t size)
{
    if (!data || size < 32)
        return false;

    std::memset(data, 0, 32);
    data[0] = 0x01;
    data[1] = kPadAnalogMarker;
    data[2] = 0xFF;
    data[3] = 0xFF;
    data[4] = data[5] = data[6] = data[7] = kPadStickCenter;

    uint32_t active = 0u;
    uint32_t sticks = kPadStickNeutral;
    if (g_hostPolled.load())
    {
        // exchange() so each latched press is handed to the guest exactly once.
        active = g_held.load() | g_latched.exchange(0u);
        sticks = g_sticks.load();
    }
    else
    {
        // No host present loop (embedders that never call ps2PadPollHost).
        uint32_t pressed = 0u;
        sampleHost(false, active, pressed, sticks);
    }

    data[4] = static_cast<uint8_t>(sticks >> 24);
    data[5] = static_cast<uint8_t>(sticks >> 16);
    data[6] = static_cast<uint8_t>(sticks >> 8);
    data[7] = static_cast<uint8_t>(sticks);

    const uint16_t btns = static_cast<uint16_t>(0xFFFFu & ~active);
    data[2] = static_cast<uint8_t>(btns & 0xFF);
    data[3] = static_cast<uint8_t>(btns >> 8);
    return true;
}
