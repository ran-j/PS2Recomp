cmake_minimum_required(VERSION 3.20)
project(ps2xStudio)

set(CMAKE_CXX_STANDARD 20)

find_package(SDL2 REQUIRED)
find_package(Threads REQUIRED)

set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/imgui")
set(MEMEDIT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/imgui_memory_editor")
set(TEXTEDIT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/ImGuiColorTextEdit")

include(FetchContent)
FetchContent_Declare(
    ImGuiFileDialog
    GIT_REPOSITORY https://github.com/aiekick/ImGuiFileDialog.git
    GIT_TAG v0.6.7
)
FetchContent_MakeAvailable(ImGuiFileDialog)
target_include_directories(ImGuiFileDialog PUBLIC ${IMGUI_DIR})

file(GLOB IMGUI_SOURCES
    "${IMGUI_DIR}/*.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp"
)
file(GLOB TEXTEDIT_SOURCES "${TEXTEDIT_DIR}/*.cpp")

include_directories(
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/ps2xAnalyzer/include"
    "${CMAKE_SOURCE_DIR}/ps2xRecomp/include"
    "${CMAKE_SOURCE_DIR}/ps2xTest/include"
)

set(ANALYZER_SOURCES "${CMAKE_SOURCE_DIR}/ps2xAnalyzer/src/elf_analyzer.cpp")

set(TEST_SOURCES
    "${CMAKE_SOURCE_DIR}/ps2xTest/src/code_generator_tests.cpp"
    "${CMAKE_SOURCE_DIR}/ps2xTest/src/r5900_decoder_tests.cpp"
)

add_executable(ps2xStudio
    src/main.cpp
    src/GUI.cpp
    src/ui/StyleManager.cpp
    ${IMGUI_SOURCES}
    ${TEXTEDIT_SOURCES}
    ${ANALYZER_SOURCES}
    ${TEST_SOURCES}
)

target_include_directories(ps2xStudio PRIVATE 
    src 
    include
    ${IMGUI_DIR} 
    ${IMGUI_DIR}/backends 
    ${MEMEDIT_DIR} 
    ${TEXTEDIT_DIR} 
    ${SDL2_INCLUDE_DIRS}
)

target_link_libraries(ps2xStudio PRIVATE 
    ${SDL2_LIBRARIES} 
    Threads::Threads 
    ps2_recomp_lib 
    elfio 
    toml11::toml11 
    ImGuiFileDialog
)

if(WIN32)
    target_link_libraries(ps2xStudio PRIVATE opengl32)
else()
    target_link_libraries(ps2xStudio PRIVATE GL)
endif()