qt_add_executable(LumaForge WIN32
    main.cpp
    CrashHandler.h CrashHandler.cpp
    LumaForge.manifest      # DPI awareness, long paths, common controls v6
    LumaForge.rc            # version resource
)
target_link_libraries(LumaForge PRIVATE
    lumaforge::ui lumaforge::core lumaforge::hardware lumaforge::diagnostics lumaforge::rendering
    Qt6::Widgets dbghelp)
set_target_properties(LumaForge PROPERTIES OUTPUT_NAME "LumaForge")

# Make build/<preset>/bin runnable from any shell: Qt DLLs + plugins, the
# matching MSVC runtime and the FFmpeg DLLs land next to the executable.
include(LumaForgeDeploy)
lumaforge_deploy_runtime(LumaForge)
