cmake_minimum_required(VERSION 3.16)
project(Velion LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets DBus Network)
find_package(PkgConfig REQUIRED)
pkg_check_modules(TAGLIB REQUIRED taglib)
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0)
pkg_check_modules(GSTREAMER_AUDIO REQUIRED gstreamer-audio-1.0)

qt_standard_project_setup()

qt_add_executable(Velion
    main.cpp
    clickableslider.h
    clickableslider.cpp
    playlistwidget.h
    playlistwidget.cpp
    mprisservice.h
    mprisservice.cpp
    playerwindow.h
    playerwindow.cpp
    playbacknotificationmanager.h
    playbacknotificationmanager.cpp
    trackmetadata.h
    trackmetadata.cpp
    coverartmanager.h
    coverartmanager.cpp
    gstplayerbackend.h
    gstplayerbackend.cpp
    lyricsmanager.h
    lyricsmanager.cpp
)

target_include_directories(Velion PRIVATE
    ${TAGLIB_INCLUDE_DIRS}
    ${GSTREAMER_INCLUDE_DIRS}
    ${GSTREAMER_AUDIO_INCLUDE_DIRS}
)

target_link_libraries(Velion PRIVATE
    Qt6::Widgets
    Qt6::DBus
    Qt6::Network
    ${TAGLIB_LIBRARIES}
    ${GSTREAMER_LIBRARIES}
    ${GSTREAMER_AUDIO_LIBRARIES}
)

target_compile_options(Velion PRIVATE
    ${TAGLIB_CFLAGS_OTHER}
    ${GSTREAMER_CFLAGS_OTHER}
    ${GSTREAMER_AUDIO_CFLAGS_OTHER}
)
