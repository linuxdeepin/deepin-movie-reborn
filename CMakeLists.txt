# 设置CMake的最低版本要求为3.0
cmake_minimum_required(VERSION 3.5)

# 定义项目名称为deepin-movie-reborn
project(deepin-movie-reborn)

# 定义选项，用于控制是否启用dxcb平台插件的集成，默认关闭
option(USE_DXCB "integration with dxcb platform plugin" OFF)
# 定义选项，用于控制是否开启调试输出，默认关闭
option(DMR_DEBUG "turn on debug output" off)
# 定义选项，用于控制是否开启dman portal支持，默认关闭
option(DTK_DMAN_PORTAL "turn on dman portal support" off)

# 执行命令获取当前系统的架构信息，存储在变量MACH中
execute_process(COMMAND uname -m OUTPUT_VARIABLE MACH
    ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)

# 如果构建类型是Debug，则开启调试输出
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(DMR_DEBUG on)
endif()

# 如果使用GNU编译器，设置C++标准为C++11
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
endif()

# 设置C++标准为C++17
set(CMAKE_CXX_STANDARD 17)

# 导出编译命令到compile_commands.json文件
set(CMAKE_EXPORT_COMPILE_COMMANDS on)

# 查找PkgConfig包管理工具
find_package(PkgConfig REQUIRED)

# 使用PkgConfig查找mpris-qt5库
#pkg_check_modules(MPRIS REQUIRED IMPORTED_TARGET mpris-qt5)
pkg_check_modules(MPRIS REQUIRED IMPORTED_TARGET mpris-qt6)

# 测试是否使用deepin fork的mpris-qt5
try_compile(USE_FORK_MPRIS
              ${CMAKE_CURRENT_BINARY_DIR}/try_compile
            SOURCES
              ${CMAKE_CURRENT_SOURCE_DIR}/tests/mpris_test.cc
            LINK_LIBRARIES
              PkgConfig::MPRIS
            OUTPUT_VARIABLE TRY_COMPILE_OUTPUT
)

# 输出是否使用deepin fork的mpris-qt5
#message(STATUS "use deepin fork mpris-qt5: ${USE_FORK_MPRIS}")

# 如果使用deepin fork的mpris-qt5，添加编译定义
if(USE_FORK_MPRIS)
  add_definitions(-DUSE_FORK_MPRIS)
endif()

# 在相应的构建目录中查找包含文件
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# 包含项目的二进制目录和源代码目录
include_directories(${PROJECT_BINARY_DIR})
include_directories(${PROJECT_SOURCE_DIR})

# 包含GNU安装目录模块
include(GNUInstallDirs)
# 如果安装前缀未初始化为默认值，则设置安装前缀为/usr
if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
   set(CMAKE_INSTALL_PREFIX /usr)
endif()

# 配置文件，将config.h.in转换为config.h
configure_file(${PROJECT_SOURCE_DIR}/config.h.in ${PROJECT_BINARY_DIR}/config.h @ONLY)

# 添加src子目录
add_subdirectory(src)
# 添加examples/test子目录
add_subdirectory(examples/test)

# 如果构建类型是Debug，添加tests子目录
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
  #  add_subdirectory(tests)
endif()
