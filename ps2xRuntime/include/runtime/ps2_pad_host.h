#ifndef PS2_PAD_HOST_H
#define PS2_PAD_HOST_H

#include <cstdint>

// Render-thread half of the pad backend. Samples raylib input once per presented
// frame and latches press edges so a tap between two guest polls still registers.
// Declared here rather than on PSPadBackend so the recompiled corpus, which
// includes ps2_pad.h through ps2_runtime.h, does not rebuild for input changes.
void ps2PadPollHost();

// The same latching, driven by a host that is not raylib. A backend owning its
// own window samples input itself and publishes it here, in the pad report's
// button encoding, with `pressed` carrying edges seen since the last call.
void ps2PadPublishHostState(uint32_t held, uint32_t pressed, uint32_t sticks);

// Clock for DQ8_PAD_SCRIPT, in guest vsync ticks rather than host frames: the
// game runs well under 60 fps, so a script timed in host frames would fire at
// the wrong point in the game and replay differently on every run.
void ps2PadSetGuestFrame(uint64_t frame);

// The same clock, for anything that wants to name its output in script time.
uint64_t ps2PadCurrentGuestFrame();

#endif
