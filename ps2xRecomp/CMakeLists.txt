cmake_minimum_required(VERSION 3.20)

project(PS2Recomp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
    elfio
    GIT_REPOSITORY https://github.com/serge1/ELFIO.git
    GIT_TAG main
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
    GIT_TAG master
)
FetchContent_MakeAvailable(fmt)

file(GLOB_RECURSE PS2RECOMP_LIB_SOURCES CONFIGURE_DEPENDS
    src/lib/*.cpp
)

file(GLOB_RECURSE PS2RECOMP_HEADERS CONFIGURE_DEPENDS
    include/*.h
    include/*.hpp
)

add_library(ps2_recomp_lib STATIC ${PS2RECOMP_LIB_SOURCES} ${PS2RECOMP_HEADERS})

target_include_directories(ps2_recomp_lib
PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${elfio_SOURCE_DIR}

PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../ps2xRuntime/include
)

target_link_libraries(ps2_recomp_lib
PUBLIC
    fmt::fmt
    toml11::toml11
)

file(GLOB_RECURSE PS2RECOMP_EXE_SOURCES CONFIGURE_DEPENDS
    src/runner/*.cpp
)

add_executable(ps2_recomp ${PS2RECOMP_EXE_SOURCES})

target_link_libraries(ps2_recomp
PRIVATE
    ps2_recomp_lib
)

install(TARGETS ps2_recomp ps2_recomp_lib
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(DIRECTORY include/
    DESTINATION include
)