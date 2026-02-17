cmake_minimum_required(VERSION 3.21)

project(ps2xTest LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(ps2x_tests
    src/main.cpp
    src/code_generator_tests.cpp
    src/r5900_decoder_tests.cpp
    src/elf_analyzer_tests.cpp
    src/pad_input_tests.cpp
)

option(PRINT_GENERATED_CODE "Print generated code in tests" OFF)
if(PRINT_GENERATED_CODE)
    target_compile_definitions(ps2x_tests PRIVATE PRINT_GENERATED_CODE)
endif()

target_include_directories(ps2x_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/ps2xRecomp/include
    ${CMAKE_SOURCE_DIR}/ps2xAnalyzer/include
    ${CMAKE_SOURCE_DIR}/ps2xRuntime/include
)

target_link_libraries(ps2x_tests PRIVATE
    ps2_recomp_lib
    ps2_analyzer_lib
    ps2_runtime
)
