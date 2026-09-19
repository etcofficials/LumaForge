add_library(lf_project STATIC
    include/lumaforge/project/Ids.h
    include/lumaforge/project/MediaItem.h
    include/lumaforge/project/Sequence.h
    include/lumaforge/project/Project.h
    include/lumaforge/project/Document.h
    include/lumaforge/project/MediaCommands.h
    include/lumaforge/project/TimelineCommands.h
    include/lumaforge/project/TimelineOps.h
    include/lumaforge/project/ProjectSerializer.h
    src/Sequence.cpp
    src/Document.cpp
    src/MediaCommands.cpp
    src/TimelineCommands.cpp
    src/TimelineOps.cpp
    src/ProjectSerializer.cpp
)
add_library(lumaforge::project ALIAS lf_project)
target_include_directories(lf_project PUBLIC include PRIVATE "${CMAKE_SOURCE_DIR}/third_party")
target_link_libraries(lf_project PUBLIC lumaforge::core lumaforge::commands lumaforge::media)
