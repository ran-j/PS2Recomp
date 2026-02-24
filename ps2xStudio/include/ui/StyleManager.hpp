#pragma once
#include "imgui.h"
#include "../StudioState.hpp"

namespace StyleManager {
    void ApplyTheme(ThemeMode mode, AppSettings& settings);
    void ApplyDarkTheme();
    void ApplyLightTheme();
    void ApplyCustomTheme(AppSettings& settings);
    void SetupFonts(AppSettings& settings);
}