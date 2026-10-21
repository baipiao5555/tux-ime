add_executable(tuxime-server
    main.cpp
    server.cpp
    settings.cpp
    clipboard_sync.cpp
    tray.cpp
)
target_include_directories(tuxime-server PRIVATE "${CMAKE_SOURCE_DIR}")
target_link_libraries(tuxime-server Fcitx5::Core
    winhttp bcrypt ws2_32 crypt32 shell32 ole32)
install(TARGETS tuxime-server DESTINATION "${CMAKE_INSTALL_BINDIR}")
