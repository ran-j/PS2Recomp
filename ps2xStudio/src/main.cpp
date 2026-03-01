#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"
#include <SDL.h>
#include <SDL_opengl.h>
#include "GUI.hpp"
#include "StudioState.hpp"

int main(int argc, char** argv) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) return -1;

    SDL_DisplayMode displayMode;
    SDL_GetCurrentDisplayMode(0, &displayMode);

    int screenWidth = displayMode.w;
    int screenHeight = displayMode.h;

    int windowWidth = (int)(screenWidth * 0.8f);
    int windowHeight = (int)(screenHeight * 0.8f);

    SDL_WindowFlags window_flags = (SDL_WindowFlags)(
        SDL_WINDOW_OPENGL |
        SDL_WINDOW_RESIZABLE
    );

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
        window_flags = (SDL_WindowFlags)(window_flags | SDL_WINDOW_MAXIMIZED);
    } else {
        windowWidth = tempSettings.windowWidth;
        windowHeight = tempSettings.windowHeight;
    }

    SDL_Window* window = SDL_CreateWindow(
        "PS2Recomp Studio",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        windowWidth,
        windowHeight,
        window_flags
    );

    if (!window) {
        SDL_Quit();
        return -1;
    }

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // Create StudioState AFTER ImGui context
    StudioState state;

    // Initial font and theme setup
    GUI::ApplySettings(state);

    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 130");

    if (argc > 1) {
        state.StartAnalysis(argv[1]);
    }

    bool done = false;
    while (!done) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) {
                done = true;
            }
            if (event.type == SDL_WINDOWEVENT) {
                if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
                    state.settings.windowWidth = event.window.data1;
                    state.settings.windowHeight = event.window.data2;
                }
                if (event.window.event == SDL_WINDOWEVENT_MAXIMIZED) {
                    state.settings.maximized = true;
                }
                if (event.window.event == SDL_WINDOWEVENT_RESTORED) {
                    state.settings.maximized = false;
                }
            }
        }

        // Check GUI quit request (File > Exit)
        if (GUI::WantsQuit()) {
            done = true;
        }

        // CRITICAL: Handle deferred font rebuild BEFORE NewFrame
        // This is the safe place to modify fonts - outside of the rendering frame
        if (state.pendingFontRebuild.load()) {
            // Rebuild fonts (clears atlas, adds fonts with new size)
            GUI::RebuildFontsIfNeeded(state);
            // Build the font atlas; the OpenGL3 backend will detect the
            // change and upload the new texture automatically on NewFrame.
            io.Fonts->Build();
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        GUI::DrawStudio(state);

        ImGui::Render();
        glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    // Save settings before shutdown
    state.SaveSettings();

    if (state.workerThread.valid()) {
        state.workerThread.wait();
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
