#include "runtime/ps2_pad.h"
#include "runtime/ps2_input_script.h"
#include "runtime/ps2_mkb.h"
#include "ps2_host_backend.h"
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <vector>

namespace
{
    constexpr uint8_t kPadAnalogMarker = 0x73;
    constexpr uint8_t kPadStickCenter = 0x80;

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

    // Opt-in mouse-look (PS2X_MKB=1). Mouse deltas feed a
    // virtual right stick (accumulate -> clamp -> decay toward center once
    // per host frame); LMB=CROSS (fire), RMB=L1 (aim down sights). Config
    // (sens/deadzone/invert_y) from PS2X_MKB_CONFIG or mkb.ini next to the
    // runner; a missing config keeps the defaults. The input script above
    // still takes precedence, and the gamepad path is unchanged when the
    // env is unset. With a gamepad connected AND MKB on, the mouse owns
    // the right stick (look) while the gamepad keeps sticks-left/buttons.
    // The one-shot "[mkb]" status line is printed by MkbLogStartupStatus
    // from PS2Runtime::initialize (this path is lazy: it only runs when
    // the game actually polls the pad).
    bool mkbEnabled()
    {
        const char *env = std::getenv("PS2X_MKB");
        return env != nullptr && env[0] == '1' && env[1] == '\0';
    }

    ps2x::MkbConfig mkbStartupConfig()
    {
        ps2x::MkbConfig cfg;
        const char *env = std::getenv("PS2X_MKB_CONFIG");
        ps2x::MkbLoadConfig((env != nullptr && env[0] != '\0') ? env : "mkb.ini", cfg);
        return cfg;
    }
}

bool PSPadBackend::readState(int /*port*/, int /*slot*/, uint8_t *data, size_t size)
{
    if (!data || size < 32)
        return false;

    // Vanguard M5: opt-in scripted input (PS2X_INPUT_SCRIPT=<path>). When
    // set, the pad report comes from the script timeline (elapsed ms since
    // the first read) and physical input is ignored, so bring-up
    // verification is deterministic and needs no human hands.
    static const bool useInputScript = []()
    {
        const char *env = std::getenv("PS2X_INPUT_SCRIPT");
        return env != nullptr && env[0] != '\0';
    }();
    static const std::vector<ps2x::PadScriptEvent> inputScript = []()
    {
        std::vector<ps2x::PadScriptEvent> events;
        if (useInputScript)
        {
            const char *env = std::getenv("PS2X_INPUT_SCRIPT");
            ps2x::LoadPadScript(env, events);
        }
        return events;
    }();
    static const std::chrono::steady_clock::time_point inputScriptStart =
        std::chrono::steady_clock::now();
    if (useInputScript && !inputScript.empty())
    {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - inputScriptStart)
                                 .count();
        ps2x::ApplyPadScript(inputScript, static_cast<uint32_t>(elapsed), data);
        return true;
    }

    // The MKB mouse-look layer (script input above still
    // wins; nothing below runs when PS2X_MKB is unset).
    static const bool useMkb = mkbEnabled();
    static const ps2x::MkbConfig mkbConfig = useMkb ? mkbStartupConfig() : ps2x::MkbConfig{};
    static float mkbRx = 0.0f;
    static float mkbRy = 0.0f;
    // Consume the mouse once per host frame: GetMouseDelta() accumulates
    // per frame, so a second pad poll in the same frame must not re-add it
    // (and the decay must run exactly once per frame). raylib updates
    // GetTime() once per frame, making it a frame token.
    static double mkbLastFrameTime = -1.0;

    std::memset(data, 0, 32);
    data[0] = 0x01;
    data[1] = kPadAnalogMarker;
    data[2] = 0xFF;
    data[3] = 0xFF;
    data[4] = data[5] = data[6] = data[7] = kPadStickCenter;

    uint16_t btns = 0xFFFFu;
    constexpr int kGamepad = 0;
    const bool useGamepad = IsGamepadAvailable(kGamepad);
    auto clearBit = [&btns](uint16_t mask)
    { btns &= ~mask; };

    if (useGamepad)
    {
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_LEFT_FACE_UP))
            clearBit(PAD_UP);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_LEFT_FACE_DOWN))
            clearBit(PAD_DOWN);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_LEFT_FACE_LEFT))
            clearBit(PAD_LEFT);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_LEFT_FACE_RIGHT))
            clearBit(PAD_RIGHT);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_RIGHT_FACE_DOWN))
            clearBit(PAD_CROSS);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT))
            clearBit(PAD_CIRCLE);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_RIGHT_FACE_LEFT))
            clearBit(PAD_SQUARE);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_RIGHT_FACE_UP))
            clearBit(PAD_TRIANGLE);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_LEFT_TRIGGER_1))
            clearBit(PAD_L1);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_RIGHT_TRIGGER_1))
            clearBit(PAD_R1);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_LEFT_TRIGGER_2))
            clearBit(PAD_L2);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_RIGHT_TRIGGER_2))
            clearBit(PAD_R2);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_MIDDLE_RIGHT))
            clearBit(PAD_START);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_MIDDLE_LEFT))
            clearBit(PAD_SELECT);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_LEFT_THUMB))
            clearBit(PAD_L3);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_RIGHT_THUMB))
            clearBit(PAD_R3);

        float lx = GetGamepadAxisMovement(kGamepad, GAMEPAD_AXIS_LEFT_X);
        float ly = GetGamepadAxisMovement(kGamepad, GAMEPAD_AXIS_LEFT_Y);
        float rx = GetGamepadAxisMovement(kGamepad, GAMEPAD_AXIS_RIGHT_X);
        float ry = GetGamepadAxisMovement(kGamepad, GAMEPAD_AXIS_RIGHT_Y);
        data[6] = static_cast<uint8_t>(128 + lx * 127);
        data[7] = static_cast<uint8_t>(128 + ly * 127);
        data[4] = static_cast<uint8_t>(128 + rx * 127);
        data[5] = static_cast<uint8_t>(128 + ry * 127);
    }
    else
    {
        if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W))
            clearBit(PAD_UP);
        if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S))
            clearBit(PAD_DOWN);
        if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A))
            clearBit(PAD_LEFT);
        if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D))
            clearBit(PAD_RIGHT);
        if (IsKeyDown(KEY_X) || IsKeyDown(KEY_SPACE))
            clearBit(PAD_CROSS);
        if (IsKeyDown(KEY_C) || IsKeyDown(KEY_ESCAPE))
            clearBit(PAD_CIRCLE);
        if (IsKeyDown(KEY_Z) || IsKeyDown(KEY_KP_0))
            clearBit(PAD_SQUARE);
        if (IsKeyDown(KEY_V) || IsKeyDown(KEY_KP_1))
            clearBit(PAD_TRIANGLE);
        if (IsKeyDown(KEY_Q))
            clearBit(PAD_L1);
        if (IsKeyDown(KEY_E))
            clearBit(PAD_R1);
        if (IsKeyDown(KEY_LEFT_SHIFT))
            clearBit(PAD_L2);
        if (IsKeyDown(KEY_RIGHT_SHIFT))
            clearBit(PAD_R2);
        if (IsKeyDown(KEY_ENTER))
            clearBit(PAD_START);
        if (IsKeyDown(KEY_TAB))
            clearBit(PAD_SELECT);
    }

    if (useMkb)
    {
        const double frameTime = GetTime();
        if (frameTime != mkbLastFrameTime)
        {
            mkbLastFrameTime = frameTime;
            const Vector2 mouseDelta = GetMouseDelta();
            ps2x::MkbApplyMouseDelta(mkbRx, mkbRy, mouseDelta.x, mouseDelta.y,
                                     mkbConfig);
        }
        // The mouse owns the right stick (look); whatever the gamepad or
        // keyboard branches wrote above still feeds sticks-left and the
        // rest of the buttons.
        ps2x::MkbStickToBytes(mkbRx, mkbRy, mkbConfig, data[4], data[5]);
        const uint32_t mouseButtons =
            (IsMouseButtonDown(MOUSE_BUTTON_LEFT) ? 1u : 0u) |
            (IsMouseButtonDown(MOUSE_BUTTON_RIGHT) ? 2u : 0u);
        btns &= static_cast<uint16_t>(~ps2x::MkbMouseButtonsToPad(mouseButtons));
    }

    data[2] = static_cast<uint8_t>(btns & 0xFF);
    data[3] = static_cast<uint8_t>(btns >> 8);
    return true;
}
