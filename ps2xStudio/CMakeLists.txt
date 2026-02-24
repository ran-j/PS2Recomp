cmake_minimum_required(VERSION 3.21)

project(ps2xStudio)

include(FetchContent)

# ImGui з гілки docking
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_BRANCH docking
    GIT_SHALLOW TRUE
)

# ImGuiColorTextEdit
FetchContent_Declare(
    imgui_colortextedit
    GIT_REPOSITORY https://github.com/BalazsJako/ImGuiColorTextEdit.git
    GIT_BRANCH master
    GIT_SHALLOW TRUE
)

# ImGuiMemoryEditor
FetchContent_Declare(
    imgui_memory_editor
    GIT_REPOSITORY https://github.com/ocornut/imgui_memory_editor.git
    GIT_BRANCH master
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(imgui imgui_colortextedit imgui_memory_editor)

# ImGui
file(GLOB IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/*.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_*.cpp
)

add_library(imgui_lib STATIC ${IMGUI_SOURCES})
target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

#ImGuiColorTextEdit
file(GLOB COLORTEXTEDIT_SOURCES
    ${imgui_colortextedit_SOURCE_DIR}/*.cpp
)

add_library(imgui_colortextedit_lib STATIC ${COLORTEXTEDIT_SOURCES})
target_include_directories(imgui_colortextedit_lib PUBLIC
    ${imgui_colortextedit_SOURCE_DIR}
)
target_link_libraries(imgui_colortextedit_lib PUBLIC imgui_lib)

# ImGuiMemoryEditor - header-only
add_library(imgui_memory_editor_lib INTERFACE)
target_include_directories(imgui_memory_editor_lib INTERFACE
    ${imgui_memory_editor_SOURCE_DIR}
)
target_link_libraries(imgui_memory_editor_lib INTERFACE imgui_lib)
