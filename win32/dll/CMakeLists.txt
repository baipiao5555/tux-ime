add_library(tuxime-x86_64 SHARED
    main.cpp
    register.cpp
    util.cpp
)

target_compile_options(tuxime-x86_64 PRIVATE "-Wno-dll-attribute-on-redeclaration")

target_link_libraries(tuxime-x86_64 tsf)
