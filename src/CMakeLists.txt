# SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
#
# SPDX-License-Identifier: GPL-3.0-or-later

set(CMAKE_AUTOMOC ON)
set(CMD_NAME deepin-movie)

set(PROJECT_INCLUDE ${PROJECT_SOURCE_DIR}/src/widgets
    ${PROJECT_SOURCE_DIR}/src/common
    ${PROJECT_SOURCE_DIR}/src/libdmr
    ${PROJECT_SOURCE_DIR}/src/dlna
    ${PROJECT_SOURCE_DIR}/src/backends/mpv
    /usr/include/glib-2.0 
    /usr/include/gstreamer-1.0)

add_definitions(-D_MOVIE_USE_)

include_directories(${CMAKE_INCLUDE_CURRENT_DIR})
include_directories(${CMAKE_CURRENT_BINARY_DIR})

find_package(Qt6 COMPONENTS Gui Widgets DBus LinguistTools Network Concurrent Sql Svg SvgWidgets Multimedia MultimediaWidgets Xml OpenGL Core OpenGLWidgets REQUIRED)

# qt5需要将qt6的find_package替换成qt5对应的：
# find_package(Qt5Widgets)
# find_package(Qt5DBus)
# find_package(Qt5X11Extras)
# find_package(Qt5LinguistTools)
# find_package(Qt5Network)
# find_package(Qt5Concurrent)
# find_package(Qt5Sql)
# find_package(Qt5Svg)
# find_package(Qt5Multimedia)
# find_package(Qt5MultimediaWidgets)
# find_package(Qt5Xml)
# find_package(DtkWidget REQUIRED)

if (Qt6_FOUND)
    # Qt6 environment
    pkg_check_modules(Dtk REQUIRED IMPORTED_TARGET dtk6widget)
    pkg_check_modules(Dtk REQUIRED IMPORTED_TARGET dtk6gui)
    pkg_check_modules(Dtk REQUIRED IMPORTED_TARGET dtk6core)
    pkg_check_modules(Xcb REQUIRED IMPORTED_TARGET xcb xcb-shape)
    #pkg_check_modules(DBusextended REQUIRED IMPORTED_TARGET dbusextended-qt5)
    pkg_check_modules(Gst REQUIRED IMPORTED_TARGET gobject-2.0)
    # IMPORTED_TARGET failed to work for some of libs under flatpak env
    pkg_check_modules(Other REQUIRED  gsettings-qt)

    qt6_add_resources(RCS ../assets/resources.qrc)
    qt6_add_resources(RCS ../assets/icons/theme-icons.qrc)
    if (${Dtk_VERSION} LESS 2.0.6.1)
        qt6_add_resources(RCS ../assets/theme.qrc)
    endif()

    file(GLOB_RECURSE SRCS LIST_DIRECTORIES false common/*.cpp widgets/*.cpp libdmr/*.cpp vendor/*.cpp dlna/*.cpp dlna/*.c)
    list(APPEND SRCS main.cpp)

    # mpv backend
    file(GLOB_RECURSE MPV_SRCS LIST_DIRECTORIES false backends/mpv/*.cpp)
    file(GLOB_RECURSE MEDIAPLAYER_SRCS LIST_DIRECTORIES false backends/mediaplayer/*.cpp)
    list(APPEND SRCS ${MPV_SRCS} ${MEDIAPLAYER_SRCS})
    list(APPEND PROJECT_INCLUDE ${PROJECT_SOURCE_DIR}/src/backends/mpv)
    # list(APPEND PROJECT_INCLUDE ${PROJECT_SOURCE_DIR}/src/backends/mediaplayer)
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
        qt6_create_translation(QM ${SRCS} ${TS})
    endif ()
    # ~

    add_executable(${CMD_NAME} ${SRCS} ${RCS} ${QM})

    target_include_directories(${CMD_NAME} PUBLIC ${PROJECT_INCLUDE})

    set(TARGET_LIBS X11 Xtst PkgConfig::Xcb Qt6::Widgets Qt6::Network Qt6::DBus Qt6::Sql Qt6::Svg Qt6::Multimedia
        Qt6::MultimediaWidgets PkgConfig::Dtk PkgConfig::MPRIS PkgConfig::Gst GL va va-x11
        Qt6::Concurrent Qt6::Xml Qt6::SvgWidgets Qt6::OpenGL Qt6::Core Qt6::Gui Qt6::GuiPrivate Qt6::OpenGLWidgets)

    target_link_libraries(${CMD_NAME} ${TARGET_LIBS} ${Other_LIBRARIES} dmr)

    install(TARGETS ${CMD_NAME} DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(DIRECTORY ${PROJECT_SOURCE_DIR}/translations
        DESTINATION ${CMAKE_INSTALL_DATADIR}/${CMD_NAME}
        FILES_MATCHING PATTERN "*.qm")
    install(FILES ${PROJECT_SOURCE_DIR}/deepin-movie.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES ${CMAKE_SOURCE_DIR}/src/libdmr/com.deepin.deepin-movie.gschema.xml
        DESTINATION ${CMAKE_INSTALL_DATADIR}/glib-2.0/schemas)
    install(FILES ${PROJECT_SOURCE_DIR}/assets/resources/icons/logo-big.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
        RENAME deepin-movie.svg)
    install(DIRECTORY ${CMAKE_SOURCE_DIR}/assets/deepin-movie DESTINATION ${CMAKE_INSTALL_DATADIR}/deepin-manual/manual-assets/application/)
    install (FILES ${CMAKE_SOURCE_DIR}/com.deepin.movie.service DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/services/)

    # 加速编译优化参数
    if (CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "mips64")
        SET(CMAKE_CXX_FLAGS "$ENV{CXXFLAGS} -O3 -ftree-vectorize -march=loongson3a -mhard-float -mno-micromips -mno-mips16 -flax-vector-conversions -mloongson-ext2 -mloongson-mmi -Wl,--as-needed -fPIE -z noexecstack")
        SET(CMAKE_C_FLAGS "$ENV{CFLAGS} -O3 -ftree-vectorize -march=loongson3a -mhard-float -mno-micromips -mno-mips16 -flax-vector-conversions -mloongson-ext2 -mloongson-mmi -z noexecstack")
        SET(CMAKE_EXE_LINKER_FLAGS "-pie")
    else()
        SET(CMAKE_CXX_FLAGS "$ENV{CXXFLAGS} -fstack-protector-strong -Wformat -Werror=format-security -Wdate-time -D_FORTIFY_SOURCE=2 -std=c++14 -O3 -DNDEBUG -fPIC -Wl,--as-needed -fPIE")
        SET(CMAKE_C_FLAGS "$ENV{CFLAGS} -fstack-protector-strong -Wformat -Werror=format-security -Wdate-time -D_FORTIFY_SOURCE=2 -std=c++14 -O3 -DNDEBUG -fPIC")
        SET(CMAKE_EXE_LINKER_FLAGS "-pie")
    endif()

    add_subdirectory(libdmr)
else()
    # Qt5 environment
    pkg_check_modules(Dtk REQUIRED IMPORTED_TARGET dtkwidget)
    pkg_check_modules(Dtk REQUIRED IMPORTED_TARGET dtkcore)
    pkg_check_modules(Xcb REQUIRED IMPORTED_TARGET xcb xcb-shape)
    pkg_check_modules(DBusextended REQUIRED IMPORTED_TARGET dbusextended-qt5)
    pkg_check_modules(Gst REQUIRED IMPORTED_TARGET gobject-2.0)
    # IMPORTED_TARGET failed to work for some of libs under flatpak env
    pkg_check_modules(Other REQUIRED gsettings-qt)

    # make linglong build success
    pkg_check_modules(3rd REQUIRED libavformat mpv libavcodec libffmpegthumbnailer gstreamer-1.0 glib-2.0)
    include_directories(${3rd_INCLUDE_DIRS})

    qt5_add_resources(RCS ../assets/resources.qrc)
    qt5_add_resources(RCS ../assets/icons/theme-icons.qrc)
    if (${Dtk_VERSION} LESS 2.0.6.1)
        qt5_add_resources(RCS ../assets/theme.qrc)
    endif()


    file(GLOB_RECURSE SRCS LIST_DIRECTORIES false common/*.cpp widgets/*.cpp libdmr/*.cpp vendor/*.cpp dlna/*.cpp dlna/*.c)
    list(APPEND SRCS main.cpp)

    # mpv backend
    file(GLOB_RECURSE MPV_SRCS LIST_DIRECTORIES false backends/mpv/*.cpp)
    file(GLOB_RECURSE MEDIAPLAYER_SRCS LIST_DIRECTORIES false backends/mediaplayer/*.cpp)
    list(APPEND SRCS ${MPV_SRCS} ${MEDIAPLAYER_SRCS})
    list(APPEND PROJECT_INCLUDE ${PROJECT_SOURCE_DIR}/src/backends/mpv)
    list(APPEND PROJECT_INCLUDE ${PROJECT_SOURCE_DIR}/src/backends/mediaplayer)
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

    set(TARGET_LIBS X11 Xtst PkgConfig::Xcb Qt5::Widgets Qt5::X11Extras Qt5::Network Qt5::DBus Qt5::Sql Qt5::Svg Qt5::Multimedia
        Qt5::MultimediaWidgets PkgConfig::Dtk PkgConfig::MPRIS PkgConfig::DBusextended PkgConfig::Gst GL va va-x11
        Qt5::Concurrent Qt5::Xml)

    target_link_libraries(${CMD_NAME} ${TARGET_LIBS} ${Other_LIBRARIES})


    install(TARGETS ${CMD_NAME} DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(DIRECTORY ${PROJECT_SOURCE_DIR}/translations
        DESTINATION ${CMAKE_INSTALL_DATADIR}/${CMD_NAME}
        FILES_MATCHING PATTERN "*.qm")
    install(FILES ${PROJECT_SOURCE_DIR}/deepin-movie.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES ${CMAKE_SOURCE_DIR}/src/libdmr/com.deepin.deepin-movie.gschema.xml
        DESTINATION ${CMAKE_INSTALL_DATADIR}/glib-2.0/schemas)
    install(FILES ${PROJECT_SOURCE_DIR}/assets/resources/icons/logo-big.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
        RENAME deepin-movie.svg)
    install(DIRECTORY ${CMAKE_SOURCE_DIR}/assets/deepin-movie DESTINATION ${CMAKE_INSTALL_DATADIR}/deepin-manual/manual-assets/application/)
    install(FILES ${PROJECT_SOURCE_DIR}/assets/deepin-movie.json DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/uos-ai-assistant/functions)
    install(FILES ${PROJECT_SOURCE_DIR}/com.deepin.movie.service DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/services/)

    #hw机型增加DConfig配置
    set(APPID org.deepin.movie)
    set(configFile ${PROJECT_SOURCE_DIR}/assets/org.deepin.movie.minimode.json)
    if (DEFINED DSG_DATA_DIR)
        message("-- DConfig is supported by DTK")
        dconfig_meta_files(APPID ${APPID} FILES ${configFile})
    else()
        install(FILES ${configFile} DESTINATION ${CMAKE_INSTALL_DATADIR}/dsg/configs/org.deepin.movie/)
    endif()

    # 加速编译优化参数
    if (CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "mips64")
        SET(CMAKE_CXX_FLAGS "$ENV{CXXFLAGS} -O3 -ftree-vectorize -march=loongson3a -mhard-float -mno-micromips -mno-mips16 -flax-vector-conversions -mloongson-ext2 -mloongson-mmi -Wl,--as-needed -fPIE -z noexecstack")
        SET(CMAKE_C_FLAGS "$ENV{CFLAGS} -O3 -ftree-vectorize -march=loongson3a -mhard-float -mno-micromips -mno-mips16 -flax-vector-conversions -mloongson-ext2 -mloongson-mmi -z noexecstack")
        SET(CMAKE_EXE_LINKER_FLAGS "-pie")
    else()
        SET(CMAKE_CXX_FLAGS "$ENV{CXXFLAGS} -fstack-protector-strong -Wformat -Werror=format-security -Wdate-time -D_FORTIFY_SOURCE=2 -std=c++14 -O3 -DNDEBUG -fPIC -Wl,--as-needed -fPIE")
        SET(CMAKE_C_FLAGS "$ENV{CFLAGS} -fstack-protector-strong -Wformat -Werror=format-security -Wdate-time -D_FORTIFY_SOURCE=2 -std=c++14 -O3 -DNDEBUG -fPIC")
        SET(CMAKE_EXE_LINKER_FLAGS "-pie")
    endif()

    add_subdirectory(libdmr)
    #add_subdirectory(test)
endif()

