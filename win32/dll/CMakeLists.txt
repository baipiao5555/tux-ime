add_library(tuxime-x86_64 SHARED
    main.cpp
    register.cpp
    util.cpp
    tuxime.def
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(tuxime-x86_64 PRIVATE "-Wno-dll-attribute-on-redeclaration")
endif()

target_link_libraries(tuxime-x86_64 tsf)
