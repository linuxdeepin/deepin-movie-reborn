set(CMAKE_AUTOMOC ON)
set(CMD_NAME deepin-movie)

set(CMAKE_CXX_FLAGS "-std=c++1y -fpermissive -Wno-error")
set(CMAKE_C_FLAGS "-Wno-error")
if (ENABLE_VPU_PLATFORM) 
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mieee")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mieee")
endif()

set(PROJECT_INCLUDE ${PROJECT_SOURCE_DIR}/src/widgets ${PROJECT_SOURCE_DIR}/src/common)

include_directories(${CMAKE_INCLUDE_CURRENT_DIR})
include_directories(${CMAKE_CURRENT_BINARY_DIR})

find_package(Qt5Widgets)
find_package(Qt5DBus)
find_package(Qt5X11Extras)
find_package(Qt5LinguistTools)
find_package(Qt5Network)
find_package(Qt5Concurrent)
find_package(Qt5Sql)

pkg_check_modules(Dtk REQUIRED IMPORTED_TARGET dtkwidget)
pkg_check_modules(Mpv REQUIRED IMPORTED_TARGET mpv)
pkg_check_modules(Xcb REQUIRED IMPORTED_TARGET xcb xcb-aux xcb-proto xcb-ewmh xcb-shape)
pkg_check_modules(Other REQUIRED IMPORTED_TARGET libffmpegthumbnailer 
    libavformat libavutil libavcodec libavresample libpulse libpulse-simple
    openssl dvdnav)

qt5_add_resources(RCS resources.qrc)

file(GLOB_RECURSE SRCS LIST_DIRECTORIES false common/*.cpp widgets/*.cpp)
list(APPEND SRCS main.cpp)

# mpv backend
file(GLOB_RECURSE MPV_SRCS LIST_DIRECTORIES false backends/mpv/*.cpp)
list(APPEND SRCS ${MPV_SRCS})
list(APPEND PROJECT_INCLUDE ${PROJECT_SOURCE_DIR}/src/backends/mpv)
#~ 

if (ENABLE_VPU_PLATFORM)
    list(APPEND PROJECT_INCLUDE ${PROJECT_SOURCE_DIR}/src/backends/vpu/)
    list(APPEND PROJECT_INCLUDE ${PROJECT_SOURCE_DIR}/src/backends/vpu/gal/include)
    list(APPEND PROJECT_INCLUDE ${PROJECT_SOURCE_DIR}/src/backends/vpu/include)
    list(APPEND PROJECT_INCLUDE ${PROJECT_SOURCE_DIR}/src/backends/vpu/vpuapi)
    #FIXME: change this!
    #link_directories(backends/vpu/gal/lib)

    file(GLOB_RECURSE VPU_SRCS LIST_DIRECTORIES false 
        backends/vpu/*.c
        backends/vpu/*.cpp)
    list(APPEND SRCS ${VPU_SRCS})
endif()

add_custom_target(json_i18n ALL 
    /usr/lib/dtk2/dtk-settings-tool resources/data/settings.json -o common/settings_translation.cpp
    COMMAND sed -i "s,#include .*,#include <DSettings>," common/settings_translation.cpp
    VERBATIM
    SOURCES resources/data/settings.json
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/src/)

## translations 
set(TS 
    ${PROJECT_SOURCE_DIR}/src/translations/${CMD_NAME}.ts
    ${PROJECT_SOURCE_DIR}/src/translations/${CMD_NAME}_zh_CN.ts)

set_source_files_properties(${TS}
    PROPERTIES OUTPUT_LOCATION ${PROJECT_SOURCE_DIR}/src/translations)

qt5_create_translation(QM ${SRCS} ${TS})
# ~

add_executable(${CMD_NAME} ${SRCS} ${RCS} ${QM})
add_dependencies(${CMD_NAME} json_i18n)
target_include_directories(${CMD_NAME} PUBLIC ${PROJECT_INCLUDE})

set(TARGET_LIBS X11 Xext Xtst PkgConfig::Xcb Qt5::Widgets Qt5::X11Extras Qt5::Network
    Qt5::Concurrent Qt5::DBus Qt5::Sql PkgConfig::Dtk PkgConfig::Mpv PkgConfig::Other pthread)
if (ENABLE_VPU_PLATFORM) 
    list(APPEND TARGET_LIBS GAL pulse pulse-simple)
endif()
target_link_libraries(${CMD_NAME} ${TARGET_LIBS})


install(TARGETS ${CMD_NAME} DESTINATION bin)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/src/translations
    DESTINATION share/${CMD_NAME}
    FILES_MATCHING PATTERN "*.qm")
install(FILES ${PROJECT_SOURCE_DIR}/src/deepin-movie.desktop 
    DESTINATION share/applications)
install(FILES ${PROJECT_SOURCE_DIR}/src/resources/icons/logo-big.svg
    DESTINATION share/icons/hicolor/scalable/apps
    RENAME deepin-movie.svg)

install(DIRECTORY ${PROJECT_SOURCE_DIR}/src/dman/deepin-movie DESTINATION share/dman)

