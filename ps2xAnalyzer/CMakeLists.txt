cmake_minimum_required(VERSION 3.20)
project(PS2Analyzer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
 
file(GLOB_RECURSE PS2ANALYZER_SOURCES
    "src/*.cpp"
)
 
add_executable(ps2_analyzer ${PS2ANALYZER_SOURCES})
 
target_include_directories(ps2_analyzer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/ps2xRecomp/include
)
 
target_link_libraries(ps2_analyzer PRIVATE
    fmt::fmt
    ps2_recomp_lib
)
 
install(TARGETS ps2_analyzer
    RUNTIME DESTINATION bin
)