cmake_minimum_required(VERSION 3.21)

foreach(requiredTarget IN ITEMS ps2_analyzer ps2_recomp ps2_runtime)
    if(NOT TARGET ${requiredTarget})
        message(FATAL_ERROR
            "PS2X_BUILD_AUTOTESTS needs ${requiredTarget}; enable PS2X_BUILD_ANALYZER, "
            "PS2X_BUILD_RECOMP and PS2X_BUILD_RUNTIME")
    endif()
endforeach()

set(PS2X_AUTOTESTS_GIT_TAG "97469ffbed8631277b94e28d01dabd702aa97ef3" CACHE STRING
    "ps2autotests revision to test against")
set(PS2X_AUTOTESTS_FILTER "" CACHE STRING
    "Regex limiting which ps2autotests cases are built, matched against ids like cpu.ee.alu")
set(PS2X_AUTOTESTS_TIMEOUT "60" CACHE STRING
    "Seconds a single recompiled test may run before it is killed")

include(FetchContent)
FetchContent_Declare(
    ps2autotests
    GIT_REPOSITORY https://github.com/unknownbrackets/ps2autotests.git
    GIT_TAG ${PS2X_AUTOTESTS_GIT_TAG}
    GIT_PROGRESS TRUE
)
FetchContent_MakeAvailable(ps2autotests)

set(PS2X_AUTOTESTS_TESTS_DIR "${ps2autotests_SOURCE_DIR}/tests")

file(GLOB_RECURSE PS2X_AUTOTESTS_ELF_FILES
    RELATIVE "${PS2X_AUTOTESTS_TESTS_DIR}"
    CONFIGURE_DEPENDS
    "${PS2X_AUTOTESTS_TESTS_DIR}/*.elf"
)

set(PS2X_AUTOTESTS_KNOWN_FAILURES "")
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/known_failures.txt" PS2X_AUTOTESTS_KNOWN_FAILURE_LINES)
foreach(line IN LISTS PS2X_AUTOTESTS_KNOWN_FAILURE_LINES)
    string(STRIP "${line}" line)
    if(line AND NOT line MATCHES "^#")
        list(APPEND PS2X_AUTOTESTS_KNOWN_FAILURES "${line}")
    endif()
endforeach()

add_custom_target(ps2x_autotests)

set(PS2X_AUTOTESTS_BUILT_COUNT 0)

foreach(elfRelativePath IN LISTS PS2X_AUTOTESTS_ELF_FILES)
    string(REGEX REPLACE "\\.elf$" "" testPath "${elfRelativePath}")
    string(REPLACE "/" "." testId "${testPath}")
    string(REPLACE "/" "_" targetName "ps2x_autotest_${testPath}")

    if(PS2X_AUTOTESTS_FILTER AND NOT testId MATCHES "${PS2X_AUTOTESTS_FILTER}")
        continue()
    endif()

    set(elfPath "${PS2X_AUTOTESTS_TESTS_DIR}/${elfRelativePath}")
    set(expectedPath "${PS2X_AUTOTESTS_TESTS_DIR}/${testPath}.expected")
    if(NOT EXISTS "${expectedPath}")
        message(WARNING "ps2autotests case ${testId} has no .expected file, skipping")
        continue()
    endif()

    set(workDir "${CMAKE_CURRENT_BINARY_DIR}/generated/${testId}")
    set(generatedDir "${workDir}/output")

    set(generatedSources
        "${generatedDir}/ps2_recompiled_functions.cpp"
        "${generatedDir}/register_functions.cpp"
    )

    add_custom_command(
        OUTPUT ${generatedSources}
               "${generatedDir}/ps2_recompiled_functions.h"
               "${generatedDir}/ps2_recompiled_stubs.h"
        COMMAND "${CMAKE_COMMAND}"
                "-DELF=${elfPath}"
                "-DWORK_DIR=${workDir}"
                "-DANALYZER=$<TARGET_FILE:ps2_analyzer>"
                "-DRECOMPILER=$<TARGET_FILE:ps2_recomp>"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Recompile.cmake"
        DEPENDS ps2_analyzer ps2_recomp
                "${elfPath}"
                "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Recompile.cmake"
        COMMENT "Recompiling ps2autotests ${testId}"
        VERBATIM
    )

    add_executable(${targetName} EXCLUDE_FROM_ALL
        ${generatedSources}
        src/autotests_main.cpp
    )

    target_include_directories(${targetName} PRIVATE
        "${generatedDir}"
        "${CMAKE_SOURCE_DIR}/ps2xRuntime/include"
    )

    target_link_libraries(${targetName} PRIVATE ps2_runtime)

    if(NOT MSVC)
        target_compile_options(${targetName} PRIVATE -O1)
    endif()

    set_target_properties(${targetName} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin"
    )

    add_dependencies(ps2x_autotests ${targetName})

    add_test(
        NAME "autotest.${testId}"
        COMMAND "${CMAKE_COMMAND}"
                "-DRUNNER=$<TARGET_FILE:${targetName}>"
                "-DELF=${elfPath}"
                "-DEXPECTED=${expectedPath}"
                "-DACTUAL=${workDir}/actual.txt"
                "-DRUN_TIMEOUT=${PS2X_AUTOTESTS_TIMEOUT}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunTest.cmake"
    )

    string(REGEX REPLACE "/.*$" "" categoryLabel "${testPath}")
    set(testLabels "${categoryLabel}")
    if(testId IN_LIST PS2X_AUTOTESTS_KNOWN_FAILURES)
        list(APPEND testLabels "known_failure")
    endif()

    math(EXPR ctestTimeout "${PS2X_AUTOTESTS_TIMEOUT} + 60")

    set_tests_properties("autotest.${testId}" PROPERTIES
        LABELS "${testLabels}"
        TIMEOUT ${ctestTimeout}
    )

    math(EXPR PS2X_AUTOTESTS_BUILT_COUNT "${PS2X_AUTOTESTS_BUILT_COUNT} + 1")
endforeach()

message(STATUS "ps2autotests: ${PS2X_AUTOTESTS_BUILT_COUNT} case(s) registered, "
               "build them with the ps2x_autotests target")
