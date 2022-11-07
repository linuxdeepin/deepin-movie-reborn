# SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
#
# SPDX-License-Identifier: GPL-3.0-or-later

# 定义需要的cmake版本
cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_STANDARD 17)

# 设置cmake参数
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

add_subdirectory(deepin-movie)
add_subdirectory(ut_dmr-test)
add_subdirectory(deepin-movie-platform)
