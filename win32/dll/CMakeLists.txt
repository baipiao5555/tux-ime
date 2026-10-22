if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(TUX_DLL_NAME "tuxime-x86_64")
else()
    set(TUX_DLL_NAME "tuxime-x86")
endif()

add_library(${TUX_DLL_NAME} SHARED
    main.cpp
    register.cpp
    util.cpp
    tuxime.def
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(${TUX_DLL_NAME} PRIVATE "-Wno-dll-attribute-on-redeclaration")
endif()

target_link_libraries(${TUX_DLL_NAME} tsf)
