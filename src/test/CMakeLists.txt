project(libdmr_test)

set(CMAKE_AUTOMOC ON)
set(CMD_NAME dmr_test)

set(CMAKE_CXX_FLAGS "-std=c++1y -fpermissive -Wno-error")

include_directories(${CMAKE_INCLUDE_CURRENT_DIR})

set(SRCS dmr_test.cpp)

add_executable(${CMD_NAME} ${SRCS})
target_include_directories(${CMD_NAME} PUBLIC 
    ${PROJECT_SOURCE_DIR}/../libdmr
    ${PROJECT_SOURCE_DIR})

target_link_libraries(${CMD_NAME} Qt5::Widgets dmr)

