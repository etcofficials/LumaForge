# Version resource + icon, generated so the exe always carries the project version.
function(lumaforge_version_rc out description internal original)
    set(LF_ICON_PATH "${CMAKE_SOURCE_DIR}/resources/icons/lumaforge.ico")
    file(TO_CMAKE_PATH "${LF_ICON_PATH}" LF_ICON_PATH)
    set(LF_FILE_DESCRIPTION "${description}")
    set(LF_INTERNAL_NAME "${internal}")
    set(LF_ORIGINAL_FILENAME "${original}")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/LumaForge.rc.in" "${out}" @ONLY)
endfunction()
lumaforge_version_rc("${CMAKE_CURRENT_BINARY_DIR}/LumaForge.rc" "LumaForge Video Editor" "LumaForge" "LumaForge.exe")
lumaforge_version_rc("${CMAKE_CURRENT_BINARY_DIR}/Launcher.rc" "LumaForge Launcher" "LumaForge Launcher" "LumaForge Launcher.exe")

qt_add_executable(LumaForge WIN32
    main.cpp
    CrashHandler.h CrashHandler.cpp
    LumaForge.manifest      # DPI awareness, long paths, common controls v6
    "${CMAKE_CURRENT_BINARY_DIR}/LumaForge.rc"
)
target_link_libraries(LumaForge PRIVATE
    lumaforge::ui lumaforge::core lumaforge::hardware lumaforge::diagnostics lumaforge::rendering
    Qt6::Widgets dbghelp)
set_target_properties(LumaForge PROPERTIES OUTPUT_NAME "LumaForge")

# Make build/<preset>/bin runnable from any shell: Qt DLLs + plugins, the
# matching MSVC runtime and the FFmpeg DLLs land next to the executable.
include(LumaForgeDeploy)
lumaforge_deploy_runtime(LumaForge)

# Native launcher for the portable package: "LumaForge Launcher.exe" in the
# package root starts app\LumaForge.exe (no Qt, no dependencies).
add_executable(LumaForgeLauncher WIN32 Launcher.cpp LumaForge.manifest "${CMAKE_CURRENT_BINARY_DIR}/Launcher.rc")
set_target_properties(LumaForgeLauncher PROPERTIES OUTPUT_NAME "LumaForge Launcher")
target_link_libraries(LumaForgeLauncher PRIVATE shell32 user32)
target_compile_definitions(LumaForgeLauncher PRIVATE UNICODE _UNICODE)
# Static CRT: the launcher sits in the package root without the runtime DLLs.
set_target_properties(LumaForgeLauncher PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
