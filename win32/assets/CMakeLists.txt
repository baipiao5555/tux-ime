set(PENGUIN_SVG "${CMAKE_CURRENT_SOURCE_DIR}/penguin.svg")
set(PENGUIN_ICO "${PROJECT_BINARY_DIR}/dll/penguin.ico")
find_program(MAGICK_EXE magick)
if(MAGICK_EXE)
    add_custom_command(
        OUTPUT "${PENGUIN_ICO}"
        COMMAND "${MAGICK_EXE}" -background none "${PENGUIN_SVG}" "${PENGUIN_ICO}"
        DEPENDS "${PENGUIN_SVG}"
    )
else()
    # ImageMagick is optional. The checked-in icon keeps clean builds
    # working on machines that only have the C++ toolchain installed.
    add_custom_command(
        OUTPUT "${PENGUIN_ICO}"
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_CURRENT_SOURCE_DIR}/penguin.ico" "${PENGUIN_ICO}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/penguin.ico"
    )
endif()
add_custom_target(GenerateIco ALL
    DEPENDS "${PENGUIN_ICO}"
)
