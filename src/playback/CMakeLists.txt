add_library(lf_playback STATIC
    include/lumaforge/playback/Program.h
    include/lumaforge/playback/SourceProgram.h
    include/lumaforge/playback/VideoReader.h
    include/lumaforge/playback/AudioMixer.h
    include/lumaforge/playback/PlaybackEngine.h
    src/SourceProgram.cpp
    src/VideoReader.cpp
    src/AudioMixer.cpp
    src/PlaybackEngine.cpp
)
add_library(lumaforge::playback ALIAS lf_playback)
target_include_directories(lf_playback PUBLIC include)
target_link_libraries(lf_playback PUBLIC lumaforge::core lumaforge::media lumaforge::rendering lumaforge::audio lumaforge::hardware)
