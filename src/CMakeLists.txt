set(CMAKE_AUTOMOC ON)
set(CMD_NAME deepin-movie)

add_compile_options("-std=c++1y")
set(PROJECT_INCLUDE ${PROJECT_SOURCE_DIR}/src/include)
set(PROJECT_INCLUDE ${PROJECT_SOURCE_DIR}/src/widgets)
include_directories(${PROJECT_INCLUDE})

include_directories(${CMAKE_INCLUDE_CURRENT_DIR})
include_directories(${CMAKE_CURRENT_BINARY_DIR})

find_package(Qt5Widgets)
find_package(Qt5DBus)
find_package(Qt5X11Extras)
find_package(Qt5LinguistTools)

pkg_check_modules(Dtk REQUIRED IMPORTED_TARGET
    dtkbase dtkutil dtkwidget dtksettings dtksettingsview)
pkg_check_modules(Mpv REQUIRED IMPORTED_TARGET mpv)
pkg_check_modules(Xcb REQUIRED IMPORTED_TARGET xcb xcb-aux xcb-proto xcb-ewmh xcb-shape)
pkg_check_modules(Other REQUIRED IMPORTED_TARGET libffmpegthumbnailer 
    libavformat libavutil libavcodec)

qt5_add_resources(RCS resources.qrc)
file(GLOB_RECURSE SRCS *.cpp)

set(TS 
    ${PROJECT_SOURCE_DIR}/src/translations/${CMD_NAME}.ts
    ${PROJECT_SOURCE_DIR}/src/translations/${CMD_NAME}_zh_CN.ts)

set_source_files_properties( ${TS}
    PROPERTIES OUTPUT_LOCATION ${PROJECT_SOURCE_DIR}/src/translations)

qt5_create_translation(QM ${SRCS} ${TS})

add_executable(${CMD_NAME} ${SRCS} ${RCS} ${QM})
target_include_directories(${CMD_NAME} PUBLIC ${PROJECT_INCLUDE})

target_link_libraries(${CMD_NAME} X11 Xext Xtst PkgConfig::Xcb
    Qt5::Widgets Qt5::X11Extras Qt5::DBus PkgConfig::Dtk PkgConfig::Mpv 
    PkgConfig::Other)

install(TARGETS ${CMD_NAME} DESTINATION bin)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/src/translations
    DESTINATION share/${CMD_NAME}
    FILES_MATCHING PATTERN "*.qm")
