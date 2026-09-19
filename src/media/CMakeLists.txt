add_library(lf_media STATIC
    include/lumaforge/media/MediaInfo.h
    include/lumaforge/media/MediaProbe.h
    include/lumaforge/media/Thumbnailer.h
    include/lumaforge/media/Image.h
    src/FFmpegUtil.h
    src/FFmpegUtil.cpp
    src/MediaProbe.cpp
    src/Thumbnailer.cpp
)
add_library(lumaforge::media ALIAS lf_media)
target_include_directories(lf_media PUBLIC include)
target_link_libraries(lf_media
    PUBLIC lumaforge::core
    PRIVATE FFmpeg::avformat FFmpeg::avcodec FFmpeg::avutil FFmpeg::swscale FFmpeg::swresample)
