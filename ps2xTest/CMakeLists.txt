cmake_minimum_required(VERSION 3.21)

project(ps2xTest LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(ps2x_tests
    src/main.cpp
    src/code_generator_tests.cpp
    src/r5900_decoder_tests.cpp
    src/runtime_syscall_tests.cpp
)

target_include_directories(ps2x_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/ps2xRecomp/include
)

target_link_libraries(ps2x_tests PRIVATE
    ps2_recomp_lib
)

if (TARGET ps2_runtime)
    target_include_directories(ps2x_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/ps2xRuntime/include
    )

    target_link_libraries(ps2x_tests PRIVATE
        ps2_runtime
    )

    target_compile_definitions(ps2x_tests PRIVATE
        PS2X_HAS_RUNTIME=1
    )
endif()

add_test(NAME ps2x_tests COMMAND $<TARGET_FILE:ps2x_tests>)
