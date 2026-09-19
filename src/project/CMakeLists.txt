add_library(lf_project STATIC
    include/lumaforge/project/Ids.h
    include/lumaforge/project/MediaItem.h
    include/lumaforge/project/Project.h
    include/lumaforge/project/Document.h
    include/lumaforge/project/MediaCommands.h
    src/Document.cpp
    src/MediaCommands.cpp
)
add_library(lumaforge::project ALIAS lf_project)
target_include_directories(lf_project PUBLIC include)
target_link_libraries(lf_project PUBLIC lumaforge::core lumaforge::commands lumaforge::media)
