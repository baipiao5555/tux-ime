# Extends the upstream MSYS2 toolchain with a writable dependency prefix.
#
# libime and fcitx5-chinese-addons are built as separate CMake projects and
# discover Fcitx5 through its installed CMake package. The upstream toolchain
# restricts find_package/find_library to the sysroot, so the prefix is appended
# here when TUX_PREFIX is set.
include("${CMAKE_CURRENT_LIST_DIR}/../windows-cross/msys2.toolchain.cmake")

if(DEFINED ENV{TUX_PREFIX} AND NOT "$ENV{TUX_PREFIX}" STREQUAL "")
    file(TO_CMAKE_PATH "$ENV{TUX_PREFIX}" _TUX_PREFIX)
    list(APPEND CMAKE_FIND_ROOT_PATH "${_TUX_PREFIX}")
    set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
    set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
    set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
    list(PREPEND CMAKE_PREFIX_PATH "${_TUX_PREFIX}")
endif()
