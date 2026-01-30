cmake_minimum_required(VERSION 3.20)

project(PS2Recomp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
    elfio
    GIT_REPOSITORY https://github.com/serge1/ELFIO.git
    GIT_TAG 7d30a22fc5aac06adfe7887ae57f3701b6b5f913
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(elfio)

FetchContent_Declare(
    toml11
    GIT_REPOSITORY https://github.com/ToruNiina/toml11.git
    GIT_TAG master
)
FetchContent_MakeAvailable(toml11)

FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG 12.1.0
)
FetchContent_MakeAvailable(fmt)

file(GLOB_RECURSE PS2RECOMP_SOURCES
    "src/*.cpp"
)

file(GLOB_RECURSE PS2RECOMP_HEADERS
    "include/*.h"
    "include/*.hpp"
)

add_executable(ps2recomp ${PS2RECOMP_SOURCES})

add_library(ps2_recomp STATIC ${PS2RECOMP_SOURCES})

target_include_directories(ps2recomp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../ps2xRuntime/include
    ${elfio_SOURCE_DIR}
)

target_include_directories(ps2_recomp PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${elfio_SOURCE_DIR}
)

target_include_directories(ps2_recomp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../ps2xRuntime/include
)

target_link_libraries(ps2recomp PRIVATE
    fmt::fmt
    toml11::toml11
)

target_link_libraries(ps2_recomp PUBLIC
    fmt::fmt
    toml11::toml11
)

install(TARGETS ps2recomp ps2_recomp
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(DIRECTORY include/
    DESTINATION include
)