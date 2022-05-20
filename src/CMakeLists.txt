set(CMAKE_AUTOMOC ON)
set(CMD_NAME deepin-movie)

set(PROJECT_INCLUDE ${PROJECT_SOURCE_DIR}/src/widgets
    ${PROJECT_SOURCE_DIR}/src/common
    ${PROJECT_SOURCE_DIR}/src/libdmr)

add_definitions(-D_MOVIE_USE_)

include_directories(${CMAKE_INCLUDE_CURRENT_DIR})
include_directories(${CMAKE_CURRENT_BINARY_DIR})

find_package(Qt5Widgets)
find_package(Qt5DBus)
find_package(Qt5X11Extras)
find_package(Qt5LinguistTools)
find_package(Qt5Network)
find_package(Qt5Concurrent)
find_package(Qt5Sql)
find_package(Qt5Svg)
find_package(Qt5Multimedia)
find_package(Qt5MultimediaWidgets)

pkg_check_modules(Dtk REQUIRED IMPORTED_TARGET dtkwidget)
pkg_check_modules(Dtk REQUIRED IMPORTED_TARGET dtkcore)
pkg_check_modules(Xcb REQUIRED IMPORTED_TARGET xcb xcb-shape)
pkg_check_modules(MPRIS REQUIRED IMPORTED_TARGET mpris-qt5)
pkg_check_modules(DBusextended REQUIRED IMPORTED_TARGET dbusextended-qt5)
pkg_check_modules(Gst REQUIRED IMPORTED_TARGET gstreamer-1.0 glib-2.0 gstreamer-pbutils-1.0)
# IMPORTED_TARGET failed to work for some of libs under flatpak env
pkg_check_modules(Other REQUIRED  gsettings-qt)

qt5_add_resources(RCS ../assets/resources.qrc)
qt5_add_resources(RCS ../assets/icons/theme-icons.qrc)
if (${Dtk_VERSION} LESS 2.0.6.1)
    qt5_add_resources(RCS ../assets/theme.qrc)
endif()


file(GLOB_RECURSE SRCS LIST_DIRECTORIES false common/*.cpp widgets/*.cpp libdmr/*.cpp vendor/*.cpp)
list(APPEND SRCS main.cpp)

# mpv backend
file(GLOB_RECURSE MPV_SRCS LIST_DIRECTORIES false backends/mpv/*.cpp)
file(GLOB_RECURSE MEDIAPLAYER_SRCS LIST_DIRECTORIES false backends/mediaplayer/*.cpp)
list(APPEND SRCS ${MPV_SRCS} ${MEDIAPLAYER_SRCS})
list(APPEND PROJECT_INCLUDE ${PROJECT_SOURCE_DIR}/src/backends/mpv)
list(APPEND PROJECT_INCLUDE ${PROJECT_SOURCE_DIR}/src/backends/mediaplayer)
list(APPEND PROJECT_INCLUDE /usr/include/glib-2.0 /usr/include/gstreamer-1.0)
#~

set (DTK_SETTINGS_TOOLS_EXECUTABLE ${DTKCORE_TOOL_DIR}/dtk-settings)
if (EXISTS ${DTK_SETTINGS_TOOLS_EXECUTABLE})
add_custom_target(json_i18n ALL
        ${DTKCORE_TOOL_DIR}/dtk-settings resources/data/settings.json -o common/settings_translation.cpp
    COMMAND sed -i "s,#include .*,#include <DSettings>," common/settings_translation.cpp
    VERBATIM
    SOURCES resources/data/settings.json
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/src/)

endif ()
## translations
file(GLOB TS LIST_DIRECTORIES false ../translations/${CMD_NAME}*.ts)

set_source_files_properties(${TS}
    PROPERTIES OUTPUT_LOCATION ${PROJECT_SOURCE_DIR}/translations)

if (NOT (${CMAKE_BUILD_TYPE} MATCHES "Debug"))
    qt5_create_translation(QM ${SRCS} ${TS})
endif ()
# ~

add_executable(${CMD_NAME} ${SRCS} ${RCS} ${QM})

target_include_directories(${CMD_NAME} PUBLIC ${PROJECT_INCLUDE})

set(TARGET_LIBS X11 Xtst PkgConfig::Xcb Qt5::Widgets Qt5::X11Extras Qt5::Network
    Qt5::DBus Qt5::Sql Qt5::Svg Qt5::Multimedia Qt5::MultimediaWidgets PkgConfig::Dtk PkgConfig::MPRIS PkgConfig::DBusextended PkgConfig::Gst GL va va-x11
    Qt5::Concurrent)

#匹配mpris差异
execute_process(COMMAND cat /etc/os-version COMMAND grep Community OUTPUT_VARIABLE CommunityVersion)
message("version:" ${CommunityVersion})
if( ${CommunityVersion} MATCHES "Community" )
    add_definitions(-D_COMMUNITY_)
else()
    find_package(gui-private)
    set(TARGET_LIBS Qt5::GuiPrivate)
endif()

target_link_libraries(${CMD_NAME} ${TARGET_LIBS} ${Other_LIBRARIES})


install(TARGETS ${CMD_NAME} DESTINATION bin)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/translations
    DESTINATION share/${CMD_NAME}
    FILES_MATCHING PATTERN "*.qm")
install(FILES ${PROJECT_SOURCE_DIR}/deepin-movie.desktop
    DESTINATION share/applications)
install(FILES ${CMAKE_SOURCE_DIR}/src/libdmr/com.deepin.deepin-movie.gschema.xml
    DESTINATION /usr/share/glib-2.0/schemas)
install(FILES ${PROJECT_SOURCE_DIR}/assets/resources/icons/logo-big.svg
    DESTINATION share/icons/hicolor/scalable/apps
    RENAME deepin-movie.svg)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/assets/deepin-movie DESTINATION /usr/share/deepin-manual/manual-assets/application/)

#匹配mpris差异
execute_process(COMMAND dpkg -s libmpris-qt5-1 COMMAND grep -i version COMMAND cut -d\  -f2 OUTPUT_VARIABLE libmprisVersion)
message(${libmprisVersion})
if( ${libmprisVersion} MATCHES "0.1.0-" )
    add_definitions(-D_LIBMPR_)
endif()

# 加速编译优化参数
if (CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "mips64")
    SET(CMAKE_CXX_FLAGS "$ENV{CXXFLAGS} -O3 -ftree-vectorize -march=loongson3a -mhard-float -mno-micromips -mno-mips16 -flax-vector-conversions -mloongson-ext2 -mloongson-mmi -Wl,--as-need -fPIE -z noexecstack")
    SET(CMAKE_C_FLAGS "$ENV{CFLAGS} -O3 -ftree-vectorize -march=loongson3a -mhard-float -mno-micromips -mno-mips16 -flax-vector-conversions -mloongson-ext2 -mloongson-mmi -z noexecstack")
    SET(CMAKE_EXE_LINKER_FLAGS "-pie")
else()
    SET(CMAKE_CXX_FLAGS "$ENV{CXXFLAGS} -fstack-protector-strong -Wformat -Werror=format-security -Wdate-time -D_FORTIFY_SOURCE=2 -std=c++14 -O3 -DNDEBUG -fPIC -Wl,--as-need -fPIE")
    SET(CMAKE_C_FLAGS "$ENV{CFLAGS} -fstack-protector-strong -Wformat -Werror=format-security -Wdate-time -D_FORTIFY_SOURCE=2 -std=c++14 -O3 -DNDEBUG -fPIC")
    SET(CMAKE_EXE_LINKER_FLAGS "-pie")
endif()

add_subdirectory(libdmr)
#add_subdirectory(test)
