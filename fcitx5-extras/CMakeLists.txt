add_fcitx5_addon(notifications notifications_win.cpp)
target_link_libraries(notifications Fcitx5::Core)
target_include_directories(notifications PRIVATE
    "${CMAKE_SOURCE_DIR}/fcitx5/src/modules/notifications")
install(TARGETS notifications
        RUNTIME DESTINATION "${FCITX_INSTALL_ADDONDIR}"
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
configure_file(notifications.conf.in notifications.conf @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/notifications.conf"
        DESTINATION "${FCITX_INSTALL_PKGDATADIR}/addon" COMPONENT config)

add_fcitx5_addon(tuxkeyboard tuxkeyboard.cpp)
target_link_libraries(tuxkeyboard Fcitx5::Core)
install(TARGETS tuxkeyboard
        RUNTIME DESTINATION "${FCITX_INSTALL_ADDONDIR}"
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(FILES tuxkeyboard.conf
        DESTINATION "${FCITX_INSTALL_PKGDATADIR}/addon" COMPONENT config)
install(FILES tux-keyboard.conf
        DESTINATION "${FCITX_INSTALL_PKGDATADIR}/inputmethod" COMPONENT config)
install(FILES tux-layout.conf
        DESTINATION "${FCITX_INSTALL_PKGDATADIR}/inputmethod" COMPONENT config)
