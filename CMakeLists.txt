cmake_minimum_required(VERSION 3.21)

project("PS2 Retro X")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory("ps2xRecomp")
add_subdirectory("ps2xRuntime")
add_subdirectory("ps2xAnalyzer")
add_subdirectory("ps2xTest")
