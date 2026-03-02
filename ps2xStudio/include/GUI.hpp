#pragma once
#include "StudioState.hpp"

namespace GUI {
    void DrawStudio(StudioState& state);
    void ApplySettings(StudioState& state);
    
    // Called from main loop BEFORE NewFrame when fonts need rebuilding
    void RebuildFontsIfNeeded(StudioState& state);
    
    // Notify GUI that config editor should resync
    void SyncConfigEditor(StudioState& state);

    // Returns true when user requested exit (File > Exit)
    bool WantsQuit();
}