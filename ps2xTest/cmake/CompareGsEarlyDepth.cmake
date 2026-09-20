# Usage: cmake -DTEST_EXECUTABLE=/path/to/ps2x_tests -P CompareGsEarlyDepth.cmake
if(NOT DEFINED TEST_EXECUTABLE OR NOT EXISTS "${TEST_EXECUTABLE}")
    message(FATAL_ERROR "TEST_EXECUTABLE must name the built ps2x_tests executable")
endif()
get_filename_component(source_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

foreach(mode IN ITEMS reference optimized)
    if(mode STREQUAL "reference")
        set(depth_setting "PS2X_GS_DISABLE_EARLY_DEPTH=1")
    else()
        set(depth_setting "--unset=PS2X_GS_DISABLE_EARLY_DEPTH")
    endif()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            --unset=PS2X_SKIP_CPU_RASTER
            --unset=PS2X_SKIP_CPU_RASTER_BEFORE_PRESENT
            --unset=PS2X_DEBUG_WHITE_WIREFRAME
            --unset=PS2X_GS_VERIFY_EARLY_DEPTH_PRESENT
            --unset=MINITEST_FILTER
            "${depth_setting}" "${TEST_EXECUTABLE}"
        WORKING_DIRECTORY "${source_root}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors
        TIMEOUT 60)
    if(NOT result STREQUAL "0")
        message(FATAL_ERROR "${mode} depth tests failed (${result}):\n${output}\n${errors}")
    endif()
    if(NOT output MATCHES "\\[gs-depth-differential\\] cases=128 hash=([0-9a-f]+)")
        message(FATAL_ERROR "Missing ${mode} differential digest:\n${output}")
    endif()
    set(${mode}_hash "${CMAKE_MATCH_1}")
    message(STATUS "${mode}: ${${mode}_hash}")
endforeach()

if(NOT reference_hash STREQUAL optimized_hash)
    message(FATAL_ERROR "Early depth rejection changed graphics memory")
endif()
message(STATUS "Early depth rejection preserves all 128 complete-VRAM results")
