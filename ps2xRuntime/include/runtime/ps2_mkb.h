// Vanguard M5-T19: opt-in mouse+keyboard (MKB) mouse-look layer
// (PS2X_MKB=1).
//
// The goal: the mouse aims like any native PC FPS. Mouse deltas feed a
// VIRTUAL RIGHT STICK (camera look - the console FPS standard): each host
// frame the delta is accumulated (scaled by `sens`), clamped to [-1,1] and
// then decayed toward center, so the stick re-centers shortly after the
// mouse stops (a flick turns, then the camera settles - no stuck turn).
// Mouse buttons ride the PS2 FPS standard: LMB=CROSS (fire),
// RMB=L1 (aim down sights). Keyboard->button mapping and the gamepad path
// are unchanged; the scripted input channel (PS2X_INPUT_SCRIPT) still
// takes precedence over everything.
//
// Configuration lives in a plain INI file (default mkb.ini next to the
// runner, override with PS2X_MKB_CONFIG):
//
//     [mkb]
//     sens=0.05        # mouse px -> stick deflection multiplier
//     deadzone=0.1     # radial deadzone of the virtual stick, 0..1
//     invert_y=0       # 1 flips the look Y axis
//
// Tuning constants (documented, hand-tuned): the per-frame decay below and
// the shipped sens=0.05 in vanguard/mkb.ini (a ~10 px/frame drag reads
// ~0.5 stick; a 20+ px/frame flick saturates - see the T19 diary).

#pragma once

#include <cstdint>

namespace ps2x
{
    // Re-center factor of the virtual right stick, applied once per host
    // frame after the delta accumulates. 0.80: a full deflection decays
    // under the default 0.1 deadzone in ~10 frames (~165 ms at 60 Hz),
    // which reads as "the turn stops right after the flick stops".
    // Brief band 0.7-0.85; lower = floatier camera, higher = snappier.
    constexpr float kMkbStickDecayPerFrame = 0.80f;

    struct MkbConfig
    {
        float sens = 1.0f;     // mouse px -> stick units multiplier
        float deadzone = 0.1f; // radial, normalized [0..1]
        int invertY = 0;       // 1 = flip the look Y axis
    };

    // Parses a [mkb] INI section. Returns false when the file is missing,
    // unreadable or has no [mkb] section; on false (and for keys absent
    // from the section) `out` keeps whatever the caller initialized it
    // with, so `MkbConfig cfg; MkbLoadConfig(p, cfg);` always yields a
    // usable configuration. Unknown keys/lines and other sections are
    // ignored; deadzone is clamped to [0,1]; sens <= 0 is ignored.
    bool MkbLoadConfig(const char *path, MkbConfig &out);

    // One host frame of the virtual right stick: accumulate the mouse
    // delta (scaled by sens, Y sign flipped when invertY), clamp to
    // [-1,1], then decay toward center by kMkbStickDecayPerFrame. The
    // caller owns the stick state across frames (start at 0,0).
    void MkbApplyMouseDelta(float &rx, float &ry, float dx, float dy,
                            const MkbConfig &cfg);

    // Normalized stick vector -> DualShock right-stick bytes (128 =
    // center, same 128 + v*127 mapping as the gamepad path). Radial
    // deadzone with rescale: below the deadzone the stick reads center;
    // above it the deflection magnitude is remapped from [dz..1] to
    // [0..1] so there is no jump at the deadzone edge.
    void MkbStickToBytes(float rx, float ry, const MkbConfig &cfg,
                         uint8_t &outRx, uint8_t &outRy);

    // Mouse button mask (bit0 = left, bit1 = right, bit2 = middle - the
    // raylib MOUSE_BUTTON_* numbering) -> pressed-bit pad mask (bit set =
    // button down): LMB=CROSS (fire), RMB=L1 (aim down sights). Other
    // buttons unmapped for now.
    uint16_t MkbMouseButtonsToPad(uint32_t mouseButtons);

    // Prints the one-shot "[mkb] ..." startup status line (enabled or
    // disabled, the resolved config path and values) so run logs evidence
    // the layer regardless of whether the game ever polls the pad. Pure
    // environment/config work - no raylib. Idempotent (first call wins).
    void MkbLogStartupStatus();
}
