#include "ps2_pad.h"
#include "raylib.h"
#include <cstring>

namespace
{
constexpr uint8_t kPadAnalogMarker = 0x73;
constexpr uint8_t kPadStickCenter = 0x80;

constexpr uint16_t PAD_LEFT    = 0x0080u;
constexpr uint16_t PAD_DOWN    = 0x0040u;
constexpr uint16_t PAD_RIGHT   = 0x0020u;
constexpr uint16_t PAD_UP      = 0x0010u;
constexpr uint16_t PAD_START   = 0x0008u;
constexpr uint16_t PAD_R3      = 0x0004u;
constexpr uint16_t PAD_L3      = 0x0002u;
constexpr uint16_t PAD_SELECT  = 0x0001u;
constexpr uint16_t PAD_SQUARE  = 0x8000u;
constexpr uint16_t PAD_CROSS   = 0x4000u;
constexpr uint16_t PAD_CIRCLE  = 0x2000u;
constexpr uint16_t PAD_TRIANGLE = 0x1000u;
constexpr uint16_t PAD_R1      = 0x0800u;
constexpr uint16_t PAD_L1      = 0x0400u;
constexpr uint16_t PAD_R2      = 0x0200u;
constexpr uint16_t PAD_L2      = 0x0100u;
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

    uint16_t btns = 0xFFFFu;
    constexpr int kGamepad = 0;
    const bool useGamepad = IsGamepadAvailable(kGamepad);
    auto clearBit = [&btns](uint16_t mask) { btns &= ~mask; };

    if (useGamepad)
    {
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_LEFT_FACE_UP))    clearBit(PAD_UP);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_LEFT_FACE_DOWN))  clearBit(PAD_DOWN);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_LEFT_FACE_LEFT))  clearBit(PAD_LEFT);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_LEFT_FACE_RIGHT)) clearBit(PAD_RIGHT);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) clearBit(PAD_CROSS);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT)) clearBit(PAD_CIRCLE);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_RIGHT_FACE_LEFT)) clearBit(PAD_SQUARE);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_RIGHT_FACE_UP))   clearBit(PAD_TRIANGLE);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_LEFT_TRIGGER_1))  clearBit(PAD_L1);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_RIGHT_TRIGGER_1)) clearBit(PAD_R1);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_LEFT_TRIGGER_2))  clearBit(PAD_L2);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_RIGHT_TRIGGER_2)) clearBit(PAD_R2);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_MIDDLE_RIGHT))   clearBit(PAD_START);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_MIDDLE_LEFT))    clearBit(PAD_SELECT);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_LEFT_THUMB))     clearBit(PAD_L3);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_RIGHT_THUMB))    clearBit(PAD_R3);

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
        if (IsKeyDown(KEY_UP)    || IsKeyDown(KEY_W)) clearBit(PAD_UP);
        if (IsKeyDown(KEY_DOWN)  || IsKeyDown(KEY_S)) clearBit(PAD_DOWN);
        if (IsKeyDown(KEY_LEFT)  || IsKeyDown(KEY_A)) clearBit(PAD_LEFT);
        if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) clearBit(PAD_RIGHT);
        if (IsKeyDown(KEY_ENTER) || IsKeyDown(KEY_SPACE)) clearBit(PAD_CROSS);
        if (IsKeyDown(KEY_ESCAPE)) clearBit(PAD_CIRCLE);
        if (IsKeyDown(KEY_KP_0)  || IsKeyDown(KEY_Z)) clearBit(PAD_SQUARE);
        if (IsKeyDown(KEY_KP_1)  || IsKeyDown(KEY_X)) clearBit(PAD_TRIANGLE);
        if (IsKeyDown(KEY_Q)) clearBit(PAD_L1);
        if (IsKeyDown(KEY_E)) clearBit(PAD_R1);
        if (IsKeyDown(KEY_LEFT_SHIFT)) clearBit(PAD_L2);
        if (IsKeyDown(KEY_RIGHT_SHIFT)) clearBit(PAD_R2);
        if (IsKeyDown(KEY_ENTER)) clearBit(PAD_START);
        if (IsKeyDown(KEY_TAB)) clearBit(PAD_SELECT);
    }

    data[2] = static_cast<uint8_t>(btns & 0xFF);
    data[3] = static_cast<uint8_t>(btns >> 8);
    return true;
}
