# lf_render: command-line renderer for .lfproj files (no Qt). Shares the
# export engine with the application; lands in bin/ next to LumaForge.exe,
# whose deployment already provides the FFmpeg and CRT DLLs.
add_executable(lf_render main.cpp)
target_link_libraries(lf_render PRIVATE lumaforge::core lumaforge::project lumaforge::playback lumaforge::export lumaforge::hardware)
target_compile_definitions(lf_render PRIVATE UNICODE _UNICODE)
