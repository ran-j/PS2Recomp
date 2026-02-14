cmake_minimum_required(VERSION 3.20)
project(PS2Analyzer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PS2ANALYZER_LIB_SOURCES
    src/elf_analyzer.cpp
)

add_library(ps2_analyzer_lib STATIC ${PS2ANALYZER_LIB_SOURCES})

target_include_directories(ps2_analyzer_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/ps2xRecomp/include
)

target_link_libraries(ps2_analyzer_lib PUBLIC
    ps2_recomp_lib
)

add_executable(ps2_analyzer
    src/analyzer_main.cpp
)

target_link_libraries(ps2_analyzer PRIVATE
    ps2_analyzer_lib
)

install(TARGETS ps2_analyzer ps2_analyzer_lib
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)
