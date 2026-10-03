add_library(lf_audio STATIC
    include/lumaforge/audio/AudioOutput.h
    src/AudioOutput.cpp
    include/lumaforge/audio/AudioRecorder.h
    src/AudioRecorder.cpp
)
add_library(lumaforge::audio ALIAS lf_audio)
target_include_directories(lf_audio PUBLIC include)
target_link_libraries(lf_audio PUBLIC lumaforge::core PRIVATE ole32 avrt)
