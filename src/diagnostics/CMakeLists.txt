add_library(lf_diagnostics STATIC
    include/lumaforge/diagnostics/SystemInfo.h
    src/SystemInfo.cpp
)
add_library(lumaforge::diagnostics ALIAS lf_diagnostics)
target_include_directories(lf_diagnostics PUBLIC include)
target_link_libraries(lf_diagnostics PUBLIC lumaforge::core PRIVATE FFmpeg::avcodec FFmpeg::avformat FFmpeg::avutil)
