# Blackline Interactive

cmake_minimum_required(VERSION 3.21)
project(ps2xStudio)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

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

FetchContent_MakeAvailable(imgui_colortextedit imgui_club)

FetchContent_GetProperties(imgui_file_dialog)
if(NOT imgui_file_dialog_POPULATED)
    FetchContent_Populate(imgui_file_dialog)
endif()

add_library(imgui_colortextedit_lib STATIC
    ${imgui_colortextedit_SOURCE_DIR}/TextEditor.cpp
)

target_include_directories(imgui_colortextedit_lib PUBLIC ${imgui_colortextedit_SOURCE_DIR})

target_link_libraries(imgui_colortextedit_lib PUBLIC imgui)

add_library(imgui_memory_editor_lib INTERFACE)

target_include_directories(imgui_memory_editor_lib INTERFACE
    ${imgui_club_SOURCE_DIR}/imgui_memory_editor
)

target_link_libraries(imgui_memory_editor_lib INTERFACE imgui)

add_library(imgui_filedialog_lib STATIC
    ${imgui_file_dialog_SOURCE_DIR}/ImGuiFileDialog.cpp
)

target_include_directories(imgui_filedialog_lib PUBLIC 
    ${imgui_file_dialog_SOURCE_DIR}
)

target_link_libraries(imgui_filedialog_lib PUBLIC imgui)

file(GLOB_RECURSE STUDIO_SOURCES "src/*.cpp")
file(GLOB_RECURSE STUDIO_HEADERS "include/*.hpp")

add_executable(ps2xStudio ${STUDIO_SOURCES} ${STUDIO_HEADERS})

if(TARGET ps2_test_function_table)
    target_sources(ps2xStudio PRIVATE $<TARGET_OBJECTS:ps2_test_function_table>)
endif()

target_include_directories(ps2xStudio PRIVATE 
    include
    ${CMAKE_CURRENT_SOURCE_DIR}/../ps2xAnalyzer/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../ps2xRecomp/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../ps2xTest/include
)

target_link_libraries(ps2xStudio PRIVATE
    imgui_colortextedit_lib
    imgui_memory_editor_lib
    imgui_filedialog_lib
    ps2_recomp_lib
    ps2_analyzer_lib
    ps2_test_lib
    raylib
    rlImGui
    imgui
)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external")
    add_custom_command(TARGET ps2xStudio POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/external"
        "$<TARGET_FILE_DIR:ps2xStudio>/external"
    )
endif()

if(COMMAND ps2x_stage_ffmpeg_runtime_dlls)
    ps2x_stage_ffmpeg_runtime_dlls(ps2xStudio)
endif()

include("${CMAKE_SOURCE_DIR}/ps2xRuntime/cmake/ReleaseMode.cmake")

if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    EnableFastReleaseMode(imgui_colortextedit_lib)
    EnableFastReleaseMode(imgui_filedialog_lib)
    EnableFastReleaseMode(ps2xStudio)
    
    if(WIN32 AND NOT PS2X_SHOW_WINDOWS_CONSOLE)
        if(MSVC)
            target_link_options(ps2xStudio PRIVATE
                $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:/SUBSYSTEM:WINDOWS>
                $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:/ENTRY:mainCRTStartup>
            )
        elseif(MINGW)
            target_link_options(ps2xStudio PRIVATE
                $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-mwindows>
            )
        endif()
    endif()
endif()
