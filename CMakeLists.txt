cmake_minimum_required(VERSION 3.0)

project(deepin-movie-reborn VERSION 3.0.0)


option(USE_DXCB "integration with dxcb platform plugin" OFF)
option(ENABLE_VPU_PLATFORM "integration with coda vpu (only on sunway platform)" off)
option(DMR_DEBUG "turn on debug output" off)
option(DTK_DMAN_PORTAL "turn on dman portal support" off)

execute_process(COMMAND uname -m OUTPUT_VARIABLE MACH                
    ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)

if (MACH STREQUAL "sw_64") 
    set(ENABLE_VPU_PLATFORM on)
endif()

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(DMR_DEBUG on)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS on)

find_package(PkgConfig REQUIRED)

# Find includes in corresponding build directories
set(CMAKE_INCLUDE_CURRENT_DIR ON)

include_directories(${PROJECT_BINARY_DIR})
include_directories(${PROJECT_SOURCE_DIR})

configure_file(${PROJECT_SOURCE_DIR}/config.h.in ${PROJECT_BINARY_DIR}/config.h @ONLY)

add_subdirectory(src)
