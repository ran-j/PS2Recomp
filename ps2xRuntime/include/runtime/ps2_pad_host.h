#ifndef PS2_PAD_HOST_H
#define PS2_PAD_HOST_H

// Render-thread half of the pad backend. Samples raylib input once per presented
// frame and latches press edges so a tap between two guest polls still registers.
// Declared here rather than on PSPadBackend so the recompiled corpus, which
// includes ps2_pad.h through ps2_runtime.h, does not rebuild for input changes.
void ps2PadPollHost();

#endif
