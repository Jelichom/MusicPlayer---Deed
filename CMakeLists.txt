cmake_minimum_required(VERSION 3.16)
project(Deed LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia DBus Network)
find_package(PkgConfig REQUIRED)
pkg_check_modules(TAGLIB REQUIRED taglib)
pkg_check_modules(LIBPULSE REQUIRED libpulse)

qt_standard_project_setup()

qt_add_executable(Deed
    main.cpp
    clickableslider.h
    clickableslider.cpp
    playlistwidget.h
    playlistwidget.cpp
    mprisservice.h
    mprisservice.cpp
    playerwindow.h
    playerwindow.cpp
)

target_include_directories(Deed PRIVATE
    ${TAGLIB_INCLUDE_DIRS}
    ${LIBPULSE_INCLUDE_DIRS}
)

target_link_libraries(Deed PRIVATE
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::DBus
    Qt6::Network
    ${TAGLIB_LIBRARIES}
    ${LIBPULSE_LIBRARIES}
)

target_compile_options(Deed PRIVATE
    ${TAGLIB_CFLAGS_OTHER}
    ${LIBPULSE_CFLAGS_OTHER}
)