if(NOT DEFINED source_dir OR source_dir STREQUAL "")
    message(FATAL_ERROR "CopyFfmpegDlls.cmake requires -Dsource_dir=<ffmpeg bin dir>")
endif()

if(NOT DEFINED dest_dir OR dest_dir STREQUAL "")
    message(FATAL_ERROR "CopyFfmpegDlls.cmake requires -Ddest_dir=<target output dir>")
endif()

file(GLOB ffmpeg_runtime_dlls "${source_dir}/*.dll")
if(NOT ffmpeg_runtime_dlls)
    message(FATAL_ERROR "No FFmpeg runtime DLLs found in '${source_dir}'")
endif()

file(MAKE_DIRECTORY "${dest_dir}")

foreach(ffmpeg_runtime_dll IN LISTS ffmpeg_runtime_dlls)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${ffmpeg_runtime_dll}" "${dest_dir}"
        RESULT_VARIABLE copy_result
    )
    if(NOT copy_result EQUAL 0)
        message(FATAL_ERROR "Failed to copy '${ffmpeg_runtime_dll}' to '${dest_dir}'")
    endif()
endforeach()
