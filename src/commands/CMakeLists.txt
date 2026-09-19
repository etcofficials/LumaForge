add_library(lf_commands STATIC
    include/lumaforge/commands/Command.h
    include/lumaforge/commands/UndoStack.h
    src/UndoStack.cpp
)
add_library(lumaforge::commands ALIAS lf_commands)
target_include_directories(lf_commands PUBLIC include)
target_link_libraries(lf_commands PUBLIC lumaforge::core)
