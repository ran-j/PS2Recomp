# EnableAndroidGLES3.cmake
#
# Include this file immediately AFTER FetchContent_MakeAvailable(raylib)
# in ps2xRuntime/CMakeLists.txt.
#
# This switches Raylib's Android renderer to OpenGL ES 3.0 and fixes the
# Android EGL context request, which Raylib 5.5 otherwise hardcodes to ES 2.
#
# Scope: presentation renderer only. The PS2 GS rasterizer remains the
# existing CPU backend. This file does NOT implement GPU GS rasterization.

if(NOT ANDROID)
    message(STATUS "Android GLES3 patch skipped: target is not Android")
    return()
endif()

if(NOT TARGET raylib)
    message(FATAL_ERROR "Android GLES3 patch: Raylib target does not exist")
endif()

set(_raylib_android_source
    "${raylib_SOURCE_DIR}/src/platforms/rcore_android.c")

if(NOT EXISTS "${_raylib_android_source}")
    message(FATAL_ERROR
        "Android GLES3 patch: Raylib Android source not found: "
        "${_raylib_android_source}")
endif()

file(READ "${_raylib_android_source}" _raylib_android_text)

set(_old "EGL_CONTEXT_CLIENT_VERSION, 2,")
set(_new "EGL_CONTEXT_CLIENT_VERSION, (rlGetVersion() == RL_OPENGL_ES_30) ? 3 : 2,")

string(FIND "${_raylib_android_text}" "${_new}" _already_patched)
if(NOT _already_patched EQUAL -1)
    message(STATUS "Raylib Android EGL context already supports GLES 3")
    return()
endif()

string(FIND "${_raylib_android_text}" "${_old}" _old_found)
if(_old_found EQUAL -1)
    message(FATAL_ERROR
        "Android GLES3 patch: expected Raylib 5.5 EGL context line was not found. "
        "No source file was modified.")
endif()

string(REPLACE "${_old}" "${_new}" _raylib_android_text
       "${_raylib_android_text}")
file(WRITE "${_raylib_android_source}" "${_raylib_android_text}")

message(STATUS "Raylib Android EGL context patched for OpenGL ES 3.0")
