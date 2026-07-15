cmake_minimum_required(VERSION 3.21)

project(ps2xIOP LANGUAGES CXX)

option(PS2X_IOP_ENABLE_PLUGINS "Enable dynamic ps2xIOP plugins" OFF)

add_library(ps2_iop STATIC
    src/iop_subsystem.cpp
    src/builtin_profiles.cpp
    src/plugin_loader.cpp
    src/modules/dbcman.cpp
    src/modules/libsd.cpp
    src/modules/mcserv.cpp
    src/modules/tsnddrv.cpp
    src/modules/cri_dtx.cpp
    src/modules/clfile.cpp
    src/modules/sound_update_stub.cpp
    src/modules/sdrdrv.cpp
)

target_compile_features(ps2_iop PUBLIC cxx_std_20)

target_include_directories(ps2_iop
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_library(ps2x::iop ALIAS ps2_iop)

target_compile_definitions(ps2_iop PUBLIC
    PS2X_IOP_ENABLE_PLUGINS=$<BOOL:${PS2X_IOP_ENABLE_PLUGINS}>
)

if(PS2X_IOP_ENABLE_PLUGINS AND UNIX AND NOT APPLE)
    target_link_libraries(ps2_iop PRIVATE ${CMAKE_DL_LIBS})
endif()

install(TARGETS ps2_iop
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/
    DESTINATION include
)
