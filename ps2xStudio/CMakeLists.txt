cmake_minimum_required(VERSION 3.21)
project(ps2xStudio)

include(FetchContent)

# 1. Dear ImGui (Docking branch for multi-viewport support)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        docking
    GIT_SHALLOW    TRUE
)

# 2. ImGuiColorTextEdit
FetchContent_Declare(
    imgui_colortextedit
    GIT_REPOSITORY https://github.com/BalazsJako/ImGuiColorTextEdit.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)

# 3. imgui_club (Contains the Memory Editor)
FetchContent_Declare(
    imgui_club
    GIT_REPOSITORY https://github.com/ocornut/imgui_club.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(imgui imgui_colortextedit imgui_club)

# --- Build ImGui Library (SDL2 + OpenGL3) ---
set(IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

add_library(imgui_lib STATIC ${IMGUI_SOURCES})
target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

# --- Build ImGuiColorTextEdit ---
add_library(imgui_colortextedit_lib STATIC
    ${imgui_colortextedit_SOURCE_DIR}/ImGuiColorTextEdit.cpp
)
target_include_directories(imgui_colortextedit_lib PUBLIC ${imgui_colortextedit_SOURCE_DIR})
target_link_libraries(imgui_colortextedit_lib PUBLIC imgui_lib)

# --- Setup ImGuiMemoryEditor (Header-only) ---
add_library(imgui_memory_editor_lib INTERFACE)
target_include_directories(imgui_memory_editor_lib INTERFACE
    ${imgui_club_SOURCE_DIR}/imgui_memory_editor
)
target_link_libraries(imgui_memory_editor_lib INTERFACE imgui_lib)

# --- Main ps2xStudio Target ---
file(GLOB_RECURSE STUDIO_SOURCES "src/*.cpp")
file(GLOB_RECURSE STUDIO_HEADERS "include/*.hpp")

add_executable(ps2xStudio ${STUDIO_SOURCES} ${STUDIO_HEADERS})
target_include_directories(ps2xStudio PRIVATE include)

# Link everything together
target_link_libraries(ps2xStudio PRIVATE
    imgui_lib
    imgui_colortextedit_lib
    imgui_memory_editor_lib
)
