include(FetchContent)
set(PARALLEL_GS_STANDALONE ON CACHE BOOL "Build parallel-gs as a library" FORCE)
set(GRANITE_TOOLS OFF CACHE BOOL "" FORCE)
set(GRANITE_INSTALL_TARGETS OFF CACHE BOOL "" FORCE)

if(MSVC AND NOT CMAKE_MSVC_RUNTIME_LIBRARY)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

FetchContent_Declare(parallel_gs
    GIT_REPOSITORY https://github.com/Arntzen-Software/parallel-gs.git
    GIT_TAG 3a66c1976170cbc2cb53a3593fabbc7c4b2ccfbd
    GIT_SUBMODULES_RECURSE TRUE
    EXCLUDE_FROM_ALL
) 

# Workaround for my pc large path remove this later hahaha
if(CMAKE_HOST_WIN32)
    set(_ps2x_git_config_count "$ENV{GIT_CONFIG_COUNT}")
    set(_ps2x_git_config_index 0)
    if(NOT _ps2x_git_config_count STREQUAL "")
        set(_ps2x_git_config_index "${_ps2x_git_config_count}")
    endif()
    set(ENV{GIT_CONFIG_KEY_${_ps2x_git_config_index}} "core.longpaths")
    set(ENV{GIT_CONFIG_VALUE_${_ps2x_git_config_index}} "true")
    math(EXPR _ps2x_git_config_next "${_ps2x_git_config_index} + 1")
    set(ENV{GIT_CONFIG_COUNT} "${_ps2x_git_config_next}")
endif()

FetchContent_MakeAvailable(parallel_gs)
if(CMAKE_HOST_WIN32)
    set(ENV{GIT_CONFIG_COUNT} "${_ps2x_git_config_count}")
    unset(ENV{GIT_CONFIG_KEY_${_ps2x_git_config_index}})
    unset(ENV{GIT_CONFIG_VALUE_${_ps2x_git_config_index}})
endif()

include("${CMAKE_CURRENT_LIST_DIR}/ParallelGSTransferTails.cmake")
target_sources(ps2_runtime PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../src/lib/gs/gs_parallel_backend.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/lib/gs/gs_gl_interop.cpp"
)
target_link_libraries(ps2_runtime PRIVATE parallel-gs)
target_compile_definitions(ps2_runtime PUBLIC PS2X_GS_PARALLEL=1)
install(FILES "${parallel_gs_SOURCE_DIR}/COPYING.LGPLv3" DESTINATION share/licenses/parallel-gs)
