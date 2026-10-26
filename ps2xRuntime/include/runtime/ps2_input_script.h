// Vanguard M5: opt-in scripted input channel (PS2X_INPUT_SCRIPT).
//
// A script is a plain text file of pad-state events applied on a run-time
// timeline (elapsed ms since the first pad read). Each line is one state:
//
//     t=<ms> btns=<hex> lx=<-128..127> ly=<-128..127> rx=<..> ry=<..>
//
// `btns` uses the pressed-bit convention (bit set = button down, same bit
// order as the DualShock wire report: CROSS=0x4000, START=0x0008, ...).
// The state holds until the next event; missing stick fields default to
// center (128). Intended for bring-up verification without human hands;
// when the env is set, physical input is ignored for determinism.

#pragma once

#include <cstdint>
#include <vector>

namespace ps2x
{
    struct PadScriptEvent
    {
        uint32_t tMs = 0u;
        uint16_t btns = 0u; // pressed-bit convention (bit set = down)
        uint8_t lx = 128u;
        uint8_t ly = 128u;
        uint8_t rx = 128u;
        uint8_t ry = 128u;
    };

    // Parses a script file. Returns false if the file is missing or no line
    // parses. Events are returned sorted by tMs.
    bool LoadPadScript(const char *path, std::vector<PadScriptEvent> &out);

    // Writes the DualShock report (>= 32 bytes) for the given timeline
    // position: the last event with tMs <= elapsedMs holds. Wire bytes are
    // active-low (pressed bit cleared in the report).
    void ApplyPadScript(const std::vector<PadScriptEvent> &events, uint32_t elapsedMs, uint8_t *data);
}
