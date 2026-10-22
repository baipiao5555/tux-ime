cmake_minimum_required(VERSION 3.27)

set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/windows-cross/msys2.toolchain.cmake")

project(fcitx5-windows VERSION 0.1.0)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 20)

option(ENABLE_TESTING_ADDONS "" OFF)
option(ENABLE_TEST "" OFF)
option(ENABLE_COVERAGE "" OFF)
option(ENABLE_ENCHANT "" OFF)
option(ENABLE_X11 "" OFF)
option(ENABLE_WAYLAND "" OFF)
option(ENABLE_DBUS "" OFF)
option(ENABLE_DOC "" OFF)
option(ENABLE_SERVER "" OFF)
option(ENABLE_KEYBOARD "" OFF)
option(USE_SYSTEMD "" OFF)
option(ENABLE_XDGAUTOSTART "" OFF)
option(ENABLE_EMOJI "" OFF)
option(ENABLE_LIBUUID "" OFF)
option(BUILD_SPELL_DICT "" OFF)

if(NOT DEFINED CMAKE_INSTALL_PREFIX)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/prefix")
endif()
# The upstream CI installs with DESTDIR and expects a drive-root prefix.
if(DEFINED ENV{CI} AND CMAKE_INSTALL_PREFIX MATCHES "Program Files")
    set(CMAKE_INSTALL_PREFIX "C:/" CACHE PATH "" FORCE)
endif()
# Fcitx on Windows resolves its data directory relative to the executable:
# <prefix>/bin, <prefix>/lib/fcitx5, <prefix>/data/fcitx5.
set(CMAKE_INSTALL_BINDIR "bin")
set(CMAKE_INSTALL_LIBDIR "lib")
set(CMAKE_INSTALL_INCLUDEDIR "include")
set(CMAKE_INSTALL_DATADIR "data")
set(CMAKE_INSTALL_LOCALEDIR "data/locale")
set(ENV{PKG_CONFIG_PATH} "${CMAKE_SYSROOT}/lib/pkgconfig")
# For std::osyncstream
add_compile_options(-fexperimental-library)

add_subdirectory(fcitx5)
target_compile_definitions(Fcitx5Utils PUBLIC -DFcitx5Utils_EXPORTS)
target_compile_definitions(Fcitx5Core PUBLIC -DFcitx5Core_EXPORTS)

set(FCITX_INSTALL_ADDONDIR "${CMAKE_INSTALL_PREFIX}/lib/fcitx5")
set(FCITX_INSTALL_PKGDATADIR "${CMAKE_INSTALL_PREFIX}/data/fcitx5")
add_subdirectory(fcitx5-extras)
add_subdirectory(src)

add_executable(tux-ime-test tools/tux-ime-test.cpp)
target_link_libraries(tux-ime-test Fcitx5::Core)

add_executable(tux-client-test tools/tux-client-test.cpp)
target_include_directories(tux-client-test PRIVATE "${CMAKE_SOURCE_DIR}")

add_executable(sync-test tools/sync-test.cpp src/clipboard_sync.cpp
    src/settings.cpp src/clipboard_image.cpp)
target_include_directories(sync-test PRIVATE "${CMAKE_SOURCE_DIR}")
target_link_libraries(sync-test winhttp bcrypt ws2_32 crypt32 shell32
    ole32 gdiplus)

add_executable(settings-test tools/settings-test.cpp src/settings.cpp)
target_include_directories(settings-test PRIVATE "${CMAKE_SOURCE_DIR}")
target_link_libraries(settings-test shell32 crypt32)

add_executable(image-clipboard-test tools/image-clipboard-test.cpp
    src/clipboard_image.cpp)
target_include_directories(image-clipboard-test PRIVATE "${CMAKE_SOURCE_DIR}")
target_link_libraries(image-clipboard-test gdiplus ole32 crypt32)
