add_library(tuxime_imgui STATIC
    "${CMAKE_SOURCE_DIR}/third_party/imgui/imgui.cpp"
    "${CMAKE_SOURCE_DIR}/third_party/imgui/imgui_draw.cpp"
    "${CMAKE_SOURCE_DIR}/third_party/imgui/imgui_tables.cpp"
    "${CMAKE_SOURCE_DIR}/third_party/imgui/imgui_widgets.cpp"
    "${CMAKE_SOURCE_DIR}/third_party/imgui/backends/imgui_impl_win32.cpp"
    "${CMAKE_SOURCE_DIR}/third_party/imgui/backends/imgui_impl_dx11.cpp"
)
target_include_directories(tuxime_imgui PUBLIC
    "${CMAKE_SOURCE_DIR}/third_party/imgui"
    "${CMAKE_SOURCE_DIR}/third_party/imgui/backends"
)
target_compile_definitions(tuxime_imgui PUBLIC
    IMGUI_DEFINE_MATH_OPERATORS
    IMGUI_IMPL_WIN32_DISABLE_GAMEPAD
)
target_link_libraries(tuxime_imgui PUBLIC d3d11 dxgi d3dcompiler dwmapi)

add_executable(tuxime-server
    main.cpp
    server.cpp
    settings.cpp
    clipboard_sync.cpp
    clipboard_image.cpp
    tray.cpp
)
target_include_directories(tuxime-server PRIVATE "${CMAKE_SOURCE_DIR}")
target_link_libraries(tuxime-server Fcitx5::Core tuxime_imgui
    winhttp bcrypt ws2_32 crypt32 shell32 ole32 gdiplus)
install(TARGETS tuxime-server DESTINATION "${CMAKE_INSTALL_BINDIR}")
