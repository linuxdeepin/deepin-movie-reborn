# SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
#
# SPDX-License-Identifier: GPL-3.0-or-later

project(libdmr_test)

set(CMAKE_AUTOMOC ON)
set(CMD_NAME dmr_test)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++1y -fpermissive -Wno-error")

include_directories(${CMAKE_INCLUDE_CURRENT_DIR})

#qt5
# find_package(Qt5Widgets)
# find_package(Qt5Concurrent)
# find_package(Qt5Network)
# find_package(Qt5X11Extras)
# find_package(Qt5Sql)
# find_package(Qt5DBus)
# find_package(Qt5Svg)

#qt6
find_package(Qt6Widgets)
find_package(Qt6Concurrent)
find_package(Qt6Network)
find_package(Qt6Sql)
find_package(Qt6DBus)
find_package(Qt6Svg)

pkg_check_modules(Dtk REQUIRED IMPORTED_TARGET dtk6widget)
pkg_check_modules(Gst REQUIRED IMPORTED_TARGET gstreamer-1.0 glib-2.0)
include_directories(${Gst_INCLUDE_DIRS})

# 定义 source 文件
FILE (GLOB SRCS
    *.cpp
    ../../src/libdmr/sysutils.cpp
)

add_executable(${CMD_NAME} ${SRCS})

if (Qt6_FOUND)
    # Qt6 environment
  	  target_link_libraries(${CMD_NAME} PkgConfig::Dtk Qt6::Widgets Qt6::Concurrent
		 Qt6::Network  Qt6::Sql Qt6::DBus PkgConfig::Gst GL)
else()
    # Qt5 environment
	 target_link_libraries(${CMD_NAME} PkgConfig::Dtk Qt5::Widgets Qt5::Concurrent
		 Qt5::Network Qt5::X11Extras Qt5::Sql Qt5::DBus PkgConfig::Gst GL)
endif()

target_include_directories(${CMD_NAME} PUBLIC
    ${PROJECT_SOURCE_DIR}/../../src/libdmr
    ${PROJECT_SOURCE_DIR}
    /usr/include/glib-2.0 /usr/include/gstreamer-1.0)

if (Qt6_FOUND)
	target_link_libraries(${CMD_NAME} Qt6::Widgets dmr)
else()
	target_link_libraries(${CMD_NAME} Qt5::Widgets dmr)
endif()
