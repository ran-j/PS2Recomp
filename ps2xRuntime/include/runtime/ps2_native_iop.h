#pragma once

#include <string>
#include <vector>

class PS2Runtime;

// Modules named here load onto an emulated IOP with an SPU2 instead of being
// stubbed: the game's RPCs run their real code, and what the SPU2 mixes plays
// on the host audio device.
namespace ps2_native_iop
{
    // File names as the game loads them, for example "LIBSD.IRX". Call this
    // before the game starts.
    void setModules(PS2Runtime &runtime, std::vector<std::string> names);
    // 0 mutes the output. The IOP keeps running in step with the device either
    // way, since games wait on their sound driver.
    void setVolume(float volume);

    // Called by the runtime; the stream starts with the first native module.
    void startAudio(PS2Runtime &runtime);
    void stopAudio();
}
