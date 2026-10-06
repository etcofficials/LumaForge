add_library(lf_export STATIC
    include/lumaforge/export/ExportSettings.h
    include/lumaforge/export/Exporter.h
    include/lumaforge/export/Proxy.h
    include/lumaforge/export/FastCut.h
    src/ExportSettings.cpp
    src/Exporter.cpp
    src/Muxer.h
    src/Muxer.cpp
    src/Proxy.cpp
    src/FastCut.cpp
)
add_library(lumaforge::export ALIAS lf_export)
target_include_directories(lf_export PUBLIC include)
target_link_libraries(lf_export
    PUBLIC lumaforge::core lumaforge::project lumaforge::playback lumaforge::rendering lumaforge::media lumaforge::hardware
    PRIVATE FFmpeg::avformat FFmpeg::avcodec FFmpeg::avutil FFmpeg::swscale)
