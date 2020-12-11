
project(libdmr_test)

set(CMAKE_AUTOMOC ON)
set(CMD_NAME dmr_test)

set(CMAKE_CXX_FLAGS "-std=c++1y -fpermissive -Wno-error")

include_directories(${CMAKE_INCLUDE_CURRENT_DIR})

find_package(Qt5Widgets)
find_package(Qt5Concurrent)
find_package(Qt5Network)
find_package(Qt5X11Extras)
find_package(Qt5Sql)
find_package(Qt5DBus)

pkg_check_modules(Dtk REQUIRED IMPORTED_TARGET dtkwidget)
pkg_check_modules(AV REQUIRED IMPORTED_TARGET libavformat libavutil libavcodec)

set(SRCS dmr_test.cpp)

add_executable(${CMD_NAME} ${SRCS})

target_link_libraries(${CMD_NAME} PkgConfig::Dtk Qt5::Widgets Qt5::Concurrent
    Qt5::Network Qt5::X11Extras Qt5::Sql Qt5::DBus PkgConfig::AV
    ${FFTHUMB_LIBRARIES} GL)
target_include_directories(${CMD_NAME} PUBLIC 
    ${PROJECT_SOURCE_DIR}/../../src/libdmr
    ${PROJECT_SOURCE_DIR})

target_link_libraries(${CMD_NAME} Qt5::Widgets dmr)

