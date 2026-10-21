add_executable(tuxime-server
    main.cpp
    server.cpp
)
target_include_directories(tuxime-server PRIVATE "${CMAKE_SOURCE_DIR}")
target_link_libraries(tuxime-server Fcitx5::Core)
install(TARGETS tuxime-server DESTINATION "${CMAKE_INSTALL_BINDIR}")
