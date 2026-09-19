qt_add_executable(LumaForge WIN32
    main.cpp
    LumaForge.manifest      # DPI awareness, long paths, common controls v6
    LumaForge.rc            # version resource
)
target_link_libraries(LumaForge PRIVATE
    lumaforge::ui lumaforge::core lumaforge::hardware lumaforge::diagnostics lumaforge::rendering
    Qt6::Widgets)
set_target_properties(LumaForge PROPERTIES OUTPUT_NAME "LumaForge")

# FFmpeg DLLs next to the executable so it runs from the build tree without
# PATH help. copy_if_different keeps incremental builds cheap. Qt DLLs are
# found via the environment during development; `windeployqt` handles them
# for distribution (see TOOLS.md).
add_custom_command(TARGET LumaForge POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FFmpeg_RUNTIME_DLLS} $<TARGET_FILE_DIR:LumaForge>
    COMMAND_EXPAND_LISTS
    COMMENT "Copying FFmpeg runtime DLLs")
