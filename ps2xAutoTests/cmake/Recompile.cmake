foreach(required IN ITEMS ELF WORK_DIR ANALYZER RECOMPILER)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "Recompile.cmake requires -D${required}=<value>")
    endif()
endforeach()

file(MAKE_DIRECTORY "${WORK_DIR}")
set(configPath "${WORK_DIR}/config.toml")

execute_process(
    COMMAND "${ANALYZER}" "${ELF}" "${configPath}"
    OUTPUT_VARIABLE analyzerLog
    ERROR_VARIABLE analyzerLog
    RESULT_VARIABLE analyzerResult
)
file(WRITE "${WORK_DIR}/analyzer.log" "${analyzerLog}")

if(NOT analyzerResult EQUAL 0)
    message(FATAL_ERROR
        "ps2_analyzer failed on ${ELF} (exit ${analyzerResult}), see ${WORK_DIR}/analyzer.log")
endif()

if(NOT EXISTS "${configPath}")
    message(FATAL_ERROR "ps2_analyzer reported success but wrote no config at ${configPath}")
endif()

file(READ "${configPath}" config)

string(REPLACE
    "single_file_output = false"
    "single_file_output = true\noutput_worker_threads = 1"
    config "${config}")
if(NOT config MATCHES "single_file_output *= *true")
    message(FATAL_ERROR
        "Could not force single-file output in ${configPath}; the analyzer's TOML layout changed")
endif()
file(WRITE "${configPath}" "${config}")

execute_process(
    COMMAND "${RECOMPILER}" "${configPath}"
    OUTPUT_VARIABLE recompilerLog
    ERROR_VARIABLE recompilerLog
    RESULT_VARIABLE recompilerResult
)
file(WRITE "${WORK_DIR}/recompiler.log" "${recompilerLog}")

if(NOT recompilerResult EQUAL 0)
    message(FATAL_ERROR
        "ps2_recomp failed on ${ELF} (exit ${recompilerResult}), see ${WORK_DIR}/recompiler.log")
endif()

foreach(generatedName IN ITEMS
        ps2_recompiled_functions.cpp
        ps2_recompiled_functions.h
        ps2_recompiled_stubs.h
        register_functions.cpp)
    if(NOT EXISTS "${WORK_DIR}/output/${generatedName}")
        message(FATAL_ERROR
            "ps2_recomp reported success but did not write ${WORK_DIR}/output/${generatedName}")
    endif()
endforeach()
