#include "imgui.h"
#include "rlImGui.h"
#include "raylib.h"
#include "GUI.hpp"
#include "StudioState.hpp"
#include <fstream>
#include <string>

int main(int argc, char** argv) {
    // Basic initialization for reading monitor size
    InitWindow(800, 600, "PS2Recomp Studio Initializing");
    int screenWidth = GetMonitorWidth(GetCurrentMonitor());
    int screenHeight = GetMonitorHeight(GetCurrentMonitor());
    CloseWindow();

    int windowWidth = (int)(screenWidth * 0.8f);
    int windowHeight = (int)(screenHeight * 0.8f);
    bool maximized = false;

    // Pre-load window settings before StudioState construction
    AppSettings tempSettings;
    try {
        std::ifstream file("studio_settings.ini");
        if (file) {
            std::string line;
            while (std::getline(file, line)) {
                size_t pos = line.find('=');
                if (pos != std::string::npos) {
                    std::string key = line.substr(0, pos);
                    std::string value = line.substr(pos + 1);

                    if (key == "maximized") {
                        tempSettings.maximized = (value == "1");
                    } else if (key == "windowWidth") {
                        tempSettings.windowWidth = std::stoi(value);
                    } else if (key == "windowHeight") {
                        tempSettings.windowHeight = std::stoi(value);
                    }
                }
            }
        }
    } catch(...) {}

    if (tempSettings.maximized) {
        maximized = true;
    } else {
        windowWidth = tempSettings.windowWidth;
        windowHeight = tempSettings.windowHeight;
    }

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    if (maximized) {
        SetConfigFlags(FLAG_WINDOW_MAXIMIZED);
    }
    
    InitWindow(windowWidth, windowHeight, "PS2Recomp Studio");
    SetTargetFPS(60);

    // Setup rlImGui
    rlImGuiSetup(true);

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // Create StudioState AFTER ImGui context
    StudioState state;

    // Initial font and theme setup
    GUI::ApplySettings(state);

    if (argc > 1) {
        state.StartAnalysis(argv[1]);
    }

    while (!WindowShouldClose()) {
        // Track window events
        if (IsWindowResized()) {
            state.settings.windowWidth = GetScreenWidth();
            state.settings.windowHeight = GetScreenHeight();
        }
        if (IsWindowMaximized()) {
            state.settings.maximized = true;
        }
        if (!IsWindowMaximized() && maximized) { // simplified transition tracking
            state.settings.maximized = false;
        }
        maximized = IsWindowMaximized();

        // Check GUI quit request (File > Exit)
        if (GUI::WantsQuit()) {
            break;
        }

        // CRITICAL: Handle deferred font rebuild BEFORE NewFrame
        if (state.pendingFontRebuild.load()) {
            GUI::RebuildFontsIfNeeded(state);
            // The rlImGui backend automatically syncs the font texture to the GPU
            // because ImGui automatically flags the font atlas as needing an update
            // when we call io.Fonts->Build() in SetupFonts.
        }

        BeginDrawing();
        ClearBackground(Color{25, 25, 25, 255}); // #191919 hex

        rlImGuiBegin();

        GUI::DrawStudio(state);

        rlImGuiEnd();
        
        EndDrawing();
    }

    // Save settings before shutdown
    state.SaveSettings();

    if (state.workerThread.valid()) {
        state.workerThread.wait();
    }

    rlImGuiShutdown();
    CloseWindow();

    return 0;
}
