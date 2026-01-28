cmake_minimum_required(VERSION 3.21)

project("PS2 Retro X")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(PS2X_RUNTIME OFF)

add_subdirectory("ps2xRecomp")

if (PS2X_RUNTIME)
    add_subdirectory("ps2xRuntime")
endif()

add_subdirectory("ps2xAnalyzer")
add_subdirectory("ps2xTest")
