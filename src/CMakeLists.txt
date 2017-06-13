set(CMAKE_AUTOMOC ON)
set(CMD_NAME deepin-movie)

add_compile_options("-std=c++1y")
set(PROJECT_INCLUDE ${PROJECT_SOURCE_DIR}/src/include)
include_directories(${PROJECT_INCLUDE})

include_directories(${CMAKE_INCLUDE_CURRENT_DIR})
include_directories(${CMAKE_CURRENT_BINARY_DIR})

find_package(Qt5Widgets)
find_package(Qt5DBus)
find_package(Qt5X11Extras)

pkg_check_modules(Dtk REQUIRED IMPORTED_TARGET
    dtkbase dtkutil dtkwidget dtksettings dtksettingsview)
pkg_check_modules(Mpv REQUIRED IMPORTED_TARGET mpv)
pkg_check_modules(Xcb REQUIRED IMPORTED_TARGET xcb xcb-aux xcb-proto xcb-ewmh xcb-shape)

qt5_add_resources(RCS resources.qrc)

add_executable(${CMD_NAME} main.cpp mainwindow.cpp mpv_proxy.cpp
    actions.cpp xutil.cpp event_monitor.cpp toolbox_proxy.cpp event_relayer.cpp
    compositing_manager.cpp mpv_glwidget.cpp shortcut_manager.cpp
    dmr_settings.cpp utility_x11.cpp toolbutton.cpp movieinfo_dialog.cpp
    burst_screenshots_dialog.cpp 
    ${RCS})
target_include_directories(${CMD_NAME} PUBLIC ${PROJECT_INCLUDE})

target_link_libraries(${CMD_NAME} X11 Xext Xtst PkgConfig::Xcb
    Qt5::Widgets Qt5::X11Extras Qt5::DBus PkgConfig::Dtk PkgConfig::Mpv)

