cmake_minimum_required(VERSION 3.21)

project("PS2 Retro X")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(PS2X_RUNTIME OFF)

# ARM64 support using sse2neon
if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64|ARM64")
    message(STATUS "ARM64 detected, fetching sse2neon")

    include(FetchContent)
    FetchContent_Declare(
        sse2neon
        GIT_REPOSITORY https://github.com/DLTcollab/sse2neon.git
        GIT_TAG v1.9.1
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(sse2neon)

    include_directories(${sse2neon_SOURCE_DIR})
    add_compile_definitions(USE_SSE2NEON)
endif()

add_subdirectory("ps2xRecomp")

add_subdirectory("ps2xRuntime")

add_subdirectory("ps2xAnalyzer")
add_subdirectory("ps2xTest")
