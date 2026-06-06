# Blackline Interactive

cmake_minimum_required(VERSION 3.21)
project(ps2xStudio)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        docking
    GIT_SHALLOW    TRUE
)

FetchContent_Declare(
    imgui_colortextedit
    GIT_REPOSITORY https://github.com/BalazsJako/ImGuiColorTextEdit.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)

FetchContent_Declare(
    imgui_club
    GIT_REPOSITORY https://github.com/ocornut/imgui_club.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
)

FetchContent_Declare(
    imgui_file_dialog
    GIT_REPOSITORY https://github.com/aiekick/ImGuiFileDialog.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)

FetchContent_Declare(
    rlImGui
    GIT_REPOSITORY https://github.com/raylib-extras/rlImGui.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(imgui imgui_colortextedit imgui_club rlImGui)

FetchContent_GetProperties(imgui_file_dialog)
if(NOT imgui_file_dialog_POPULATED)
    FetchContent_Populate(imgui_file_dialog)
endif()

set(IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${rlimgui_SOURCE_DIR}/rlImGui.cpp
)

add_library(imgui_lib STATIC ${IMGUI_SOURCES})

target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${rlimgui_SOURCE_DIR}
)

target_link_libraries(imgui_lib PUBLIC raylib)

add_library(imgui_colortextedit_lib STATIC
    ${imgui_colortextedit_SOURCE_DIR}/TextEditor.cpp
)

target_include_directories(imgui_colortextedit_lib PUBLIC ${imgui_colortextedit_SOURCE_DIR})

target_link_libraries(imgui_colortextedit_lib PUBLIC imgui_lib)

add_library(imgui_memory_editor_lib INTERFACE)

target_include_directories(imgui_memory_editor_lib INTERFACE
    ${imgui_club_SOURCE_DIR}/imgui_memory_editor
)

target_link_libraries(imgui_memory_editor_lib INTERFACE imgui_lib)

add_library(imgui_filedialog_lib STATIC
    ${imgui_file_dialog_SOURCE_DIR}/ImGuiFileDialog.cpp
)

target_include_directories(imgui_filedialog_lib PUBLIC 
    ${imgui_file_dialog_SOURCE_DIR}
    ${imgui_SOURCE_DIR}
)

target_link_libraries(imgui_filedialog_lib PUBLIC imgui_lib)

file(GLOB_RECURSE STUDIO_SOURCES "src/*.cpp")
file(GLOB_RECURSE STUDIO_HEADERS "include/*.hpp")

add_executable(ps2xStudio ${STUDIO_SOURCES} ${STUDIO_HEADERS})

target_include_directories(ps2xStudio PRIVATE 
    include
    ${CMAKE_CURRENT_SOURCE_DIR}/../ps2xAnalyzer/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../ps2xRecomp/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../ps2xTest/include
    ${rlimgui_SOURCE_DIR}
)

target_link_libraries(ps2xStudio PRIVATE
    imgui_lib
    imgui_colortextedit_lib
    imgui_memory_editor_lib
    imgui_filedialog_lib
    ps2_recomp_lib
    ps2_analyzer_lib
    ps2_test_lib
    raylib
)

include("${CMAKE_SOURCE_DIR}/ps2xRuntime/cmake/ReleaseMode.cmake")

if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    EnableFastReleaseMode(imgui_lib)
    EnableFastReleaseMode(imgui_colortextedit_lib)
    EnableFastReleaseMode(imgui_filedialog_lib)
    EnableFastReleaseMode(ps2xStudio)
endif()
