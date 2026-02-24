cmake_minimum_required(VERSION 3.21)
project(ps2xStudio)

include(FetchContent)

# 1. ImGui (docking branch)
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

# 3. ImGuiMemoryEditor
FetchContent_Declare(
    imgui_memory_editor
    GIT_REPOSITORY https://github.com/ocornut/imgui_memory_editor.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(imgui imgui_colortextedit imgui_memory_editor)

#--- ImGui Customisation for SDL2 + OpenGL3 ---

# We specify only the necessary backends so that there are no conflicts with Vulkan/DX
set(IMGUI_BACKEND_SOURCES
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

add_library(imgui_lib STATIC 
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${IMGUI_BACKEND_SOURCES}
)

# Important: ImGui backends need SDL and OpenGL headers
target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

# --- ImGuiColorTextEdit ---
add_library(imgui_colortextedit_lib STATIC 
    ${imgui_colortextedit_SOURCE_DIR}/ImGuiColorTextEdit.cpp
)
target_include_directories(imgui_colortextedit_lib PUBLIC ${imgui_colortextedit_SOURCE_DIR})
target_link_libraries(imgui_colortextedit_lib PUBLIC imgui_lib)

# --- ImGuiMemoryEditor (Header-only) ---
add_library(imgui_memory_editor_lib INTERFACE)
target_include_directories(imgui_memory_editor_lib INTERFACE ${imgui_memory_editor_SOURCE_DIR})
target_link_libraries(imgui_memory_editor_lib INTERFACE imgui_lib)
