cmake_minimum_required(VERSION 3.20)

project(PS2Runtime VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
set(FETCHCONTENT_QUIET FALSE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    raylib
    GIT_REPOSITORY "https://github.com/raysan5/raylib.git"
    GIT_TAG "5.5" # we will migrate to 4.2.0 later, trust me it will be better
    GIT_PROGRESS TRUE
)
FetchContent_MakeAvailable(raylib)

add_library(ps2_runtime STATIC
    src/ps2_memory.cpp
    src/ps2_runtime.cpp
    src/ps2_stubs.cpp
    src/ps2_syscalls.cpp
)

add_executable(ps2EntryRunner
    src/register_functions.cpp
    src/main.cpp
)

target_include_directories(ps2_runtime PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ps2_runtime PRIVATE raylib)
target_link_libraries(ps2EntryRunner PRIVATE raylib)
target_link_libraries(ps2EntryRunner PRIVATE ps2_runtime)

install(TARGETS ps2_runtime
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(DIRECTORY include/
    DESTINATION include
)