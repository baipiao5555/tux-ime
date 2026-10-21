add_library(tsf STATIC
    tsf.cpp
    TextInputProcessorEx.cpp
    ThreadMgrEventSink.cpp
    TextEditSink.cpp
    KeyEventSink.cpp
    EditSession.cpp
    CompositionSink.cpp
    ServerClient.cpp
    CandidateWindow.cpp
)

target_include_directories(tsf PUBLIC "${CMAKE_SOURCE_DIR}/..")
target_link_libraries(tsf PUBLIC gdiplus user32 gdi32)
