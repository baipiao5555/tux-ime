cmake_minimum_required(VERSION 3.27)

option(TUX_USE_MSVC "Build the TSF frontend with MSVC" OFF)
if(NOT TUX_USE_MSVC)
    set(CMAKE_C_COMPILER clang)
    set(CMAKE_CXX_COMPILER clang++)
endif()

project(fcitx5-win32 VERSION 0.1.0)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 20)

if(MSVC)
    add_compile_options(/utf-8)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_subdirectory(assets)
add_subdirectory(tsf)
add_subdirectory(dll)

enable_testing()
add_subdirectory(tests)

add_executable(candidate-test ../tools/candidate-test.cpp)
target_link_libraries(candidate-test tsf)

add_executable(server-client-test ../tools/server-client-test.cpp
    dll/register.cpp
    dll/util.cpp
)
target_link_libraries(server-client-test tsf ole32 uuid)
