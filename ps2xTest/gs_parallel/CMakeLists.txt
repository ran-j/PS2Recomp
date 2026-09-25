cmake_minimum_required(VERSION 3.21)
project(PS2ParallelGSTests LANGUAGES C CXX)

include(CTest)
include(FetchContent)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(raylib GIT_REPOSITORY https://github.com/raysan5/raylib.git GIT_TAG 5.5)
FetchContent_MakeAvailable(raylib)

get_filename_component(runtime "../../ps2xRuntime" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_LIST_DIR}")
add_library(ps2_runtime STATIC
    "${runtime}/src/lib/gs/gs_frontend.cpp"
    "${runtime}/src/lib/gs/gs_cpu_backend.cpp"
    "${runtime}/src/lib/gs/ps2_gs_memory.cpp")

target_include_directories(ps2_runtime PUBLIC "${runtime}/include")

target_link_libraries(ps2_runtime PUBLIC raylib)

if(NOT MSVC AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    target_compile_options(ps2_runtime PRIVATE -msse4.1)
endif()

include("${runtime}/cmake/ParallelGS.cmake")

add_executable(ps2_gs_parallel_tests ../gs_parallel_tests.cpp)
target_link_libraries(ps2_gs_parallel_tests PRIVATE ps2_runtime)

add_test(NAME gs.parallel_gpu COMMAND ps2_gs_parallel_tests)
set_tests_properties(gs.parallel_gpu PROPERTIES LABELS "gs;gpu;interop" TIMEOUT 120)
