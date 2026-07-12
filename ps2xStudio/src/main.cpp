#include "imgui.h"
#include "raylib.h"
#include "rlImGui.h"
#include "GUI.hpp"
#include "StudioState.hpp"
#include <fstream>
#include <string>

int main(int argc, char** argv) {
    int screenWidth = 1280;
    int screenHeight = 720;

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

    if (!tempSettings.maximized && tempSettings.windowWidth > 0 && tempSettings.windowHeight > 0) {
        screenWidth = tempSettings.windowWidth;
        screenHeight = tempSettings.windowHeight;
    }

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIGHDPI);
    if (tempSettings.maximized) {
        SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIGHDPI | FLAG_WINDOW_MAXIMIZED);
    }

    InitWindow(screenWidth, screenHeight, "PS2Recomp Studio");
    SetTargetFPS(60);

    rlImGuiSetup(true);

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    StudioState state;
    GUI::ApplySettings(state);
    ImGui::GetIO().Fonts->Build();

    if (argc > 1) {
        state.StartAnalysis(argv[1]);
    }

    while (!WindowShouldClose()) {
        if (IsWindowResized()) {
            state.settings.windowWidth = GetScreenWidth();
            state.settings.windowHeight = GetScreenHeight();
        }
        
        if (IsWindowMaximized()) {
            state.settings.maximized = true;
        } else if (!IsWindowMaximized() && state.settings.maximized) {
            state.settings.maximized = false;
        }

        if (GUI::WantsQuit()) {
            break;
        }

        if (state.pendingFontRebuild.load()) {
            GUI::RebuildFontsIfNeeded(state);
            ImGui::GetIO().Fonts->Build();
        }

        BeginDrawing();
        ClearBackground(Color{25, 25, 25, 255});

        rlImGuiBegin();
        
        GUI::DrawStudio(state);

        rlImGuiEnd();
        EndDrawing();
    }

    state.SaveSettings();

    if (state.workerThread.valid()) {
        state.workerThread.wait();
    }

    rlImGuiShutdown();
    CloseWindow();

    return 0;
}
