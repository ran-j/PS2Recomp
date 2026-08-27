foreach(required IN ITEMS RUNNER ELF EXPECTED ACTUAL RUN_TIMEOUT)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "RunTest.cmake requires -D${required}=<value>")
    endif()
endforeach()

set(reproCommand "${RUNNER} ${ELF}")

if(NOT EXISTS "${RUNNER}")
    message(FATAL_ERROR
        "Recompiled runner was not built: ${RUNNER}\n"
        "Build it with: cmake --build <dir> --target ps2x_autotests")
endif()

function(ps2x_split_lines text outVar)
    string(REPLACE "\\" "\\\\" text "${text}")
    string(REPLACE ";" "\\;" text "${text}")
    string(REPLACE "\n" ";" text "${text}")
    set(${outVar} "${text}" PARENT_SCOPE)
endfunction()

function(ps2x_extract_test_span text outVar)
    string(REPLACE "\r\n" "\n" text "${text}")

    string(FIND "${text}" "-- TEST BEGIN" beginIndex)
    if(beginIndex EQUAL -1)
        set(${outVar} "" PARENT_SCOPE)
        return()
    endif()

    string(FIND "${text}" "-- TEST END" endIndex REVERSE)
    if(endIndex EQUAL -1 OR endIndex LESS beginIndex)
        set(${outVar} "" PARENT_SCOPE)
        return()
    endif()

    string(LENGTH "-- TEST END" endMarkerLength)
    math(EXPR spanLength "${endIndex} + ${endMarkerLength} - ${beginIndex}")
    string(SUBSTRING "${text}" ${beginIndex} ${spanLength} span)

    set(${outVar} "${span}\n" PARENT_SCOPE)
endfunction()

execute_process(
    COMMAND "${RUNNER}" "${ELF}"
    OUTPUT_VARIABLE runnerOutput
    ERROR_QUIET
    TIMEOUT ${RUN_TIMEOUT}
    RESULT_VARIABLE runnerResult
)

get_filename_component(actualDir "${ACTUAL}" DIRECTORY)
file(MAKE_DIRECTORY "${actualDir}")
file(WRITE "${ACTUAL}.raw" "${runnerOutput}")

if(NOT runnerResult EQUAL 0)
    message(FATAL_ERROR
        "Runner did not exit cleanly: ${runnerResult}\n"
        "Raw stdout: ${ACTUAL}.raw\n"
        "Reproduce with: ${reproCommand}")
endif()

ps2x_extract_test_span("${runnerOutput}" actualText)
file(WRITE "${ACTUAL}" "${actualText}")

if(actualText STREQUAL "")
    message(FATAL_ERROR
        "Runner produced no '-- TEST BEGIN'/'-- TEST END' output.\n"
        "Raw stdout: ${ACTUAL}.raw\n"
        "Reproduce with: ${reproCommand}")
endif()

file(READ "${EXPECTED}" expectedRaw)
ps2x_extract_test_span("${expectedRaw}" expectedText)

if(actualText STREQUAL expectedText)
    return()
endif()

ps2x_split_lines("${expectedText}" expectedLines)
ps2x_split_lines("${actualText}" actualLines)

list(LENGTH expectedLines expectedLineCount)
list(LENGTH actualLines actualLineCount)
set(compareCount ${expectedLineCount})
if(actualLineCount LESS compareCount)
    set(compareCount ${actualLineCount})
endif()

set(firstDifference "")
foreach(lineIndex RANGE 0 ${compareCount})
    if(lineIndex GREATER_EQUAL compareCount)
        break()
    endif()
    list(GET expectedLines ${lineIndex} expectedLine)
    list(GET actualLines ${lineIndex} actualLine)
    if(NOT expectedLine STREQUAL actualLine)
        math(EXPR lineNumber "${lineIndex} + 1")
        set(firstDifference
            "line ${lineNumber}:\n  expected: ${expectedLine}\n  actual:   ${actualLine}")
        break()
    endif()
endforeach()

if(firstDifference STREQUAL "")
    set(firstDifference
        "output truncated: expected ${expectedLineCount} line(s), got ${actualLineCount}")
endif()

message(FATAL_ERROR
    "Output does not match ${EXPECTED}\n"
    "${firstDifference}\n"
    "Full output: ${ACTUAL}\n"
    "Reproduce with: ${reproCommand}")
