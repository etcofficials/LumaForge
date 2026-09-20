add_library(lf_media STATIC
    include/lumaforge/media/MediaInfo.h
    include/lumaforge/media/MediaProbe.h
    include/lumaforge/media/Thumbnailer.h
    include/lumaforge/media/Image.h
    include/lumaforge/media/VideoFrame.h
    include/lumaforge/media/VideoDecoder.h
    include/lumaforge/media/AudioDecoder.h
    include/lumaforge/media/Waveform.h
    src/FFmpegUtil.h
    src/FFmpegUtil.cpp
    src/MediaProbe.cpp
    src/Thumbnailer.cpp
    src/VideoFrame.cpp
    src/VideoDecoder.cpp
    src/AudioDecoder.cpp
    src/Waveform.cpp
)
add_library(lumaforge::media ALIAS lf_media)
target_include_directories(lf_media PUBLIC include)
target_link_libraries(lf_media
    PUBLIC lumaforge::core lumaforge::hardware
    PRIVATE FFmpeg::avformat FFmpeg::avcodec FFmpeg::avutil FFmpeg::swscale FFmpeg::swresample)
