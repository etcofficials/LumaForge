# Unit + integration tests for the Qt-free engine modules. A tiny header-only
# harness keeps the dependency footprint at zero. Integration tests use the
# deterministic media in tests/media (generated with the bundled ffmpeg.exe).
add_executable(lf_tests
    TestMain.h
    test_time.cpp
    test_undo.cpp
    test_frame_layout.cpp
    test_media.cpp
    test_project.cpp
    test_decoders.cpp
    test_compositor.cpp
    test_playback.cpp
    test_timeline.cpp
    test_serializer.cpp
    test_text.cpp
    test_editing.cpp
    test_library.cpp
)
target_link_libraries(lf_tests PRIVATE lumaforge::core lumaforge::commands lumaforge::rendering lumaforge::media lumaforge::project lumaforge::hardware lumaforge::playback lumaforge::audio)
target_compile_definitions(lf_tests PRIVATE LF_TEST_MEDIA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/media")
add_test(NAME lf_tests COMMAND lf_tests)

# UI tests: a real MainWindow without a GPU, driven by synthetic key events
# through QWindowSystemInterface (Qt::GuiPrivate), on the offscreen platform.
add_executable(lf_ui_tests TestMain.h ui/test_ui.cpp)
target_link_libraries(lf_ui_tests PRIVATE lumaforge::ui Qt6::Widgets Qt6::GuiPrivate)
get_target_property(_lf_qmake Qt6::qmake IMPORTED_LOCATION)
get_filename_component(_lf_qt_root "${_lf_qmake}/../.." ABSOLUTE)
add_custom_command(TARGET lf_ui_tests POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:lf_ui_tests>/platforms"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_lf_qt_root}/plugins/platforms/qoffscreen$<$<CONFIG:Debug>:d>.dll" "$<TARGET_FILE_DIR:lf_ui_tests>/platforms/"
    COMMENT "Deploying the offscreen platform plugin for lf_ui_tests"
    VERBATIM)
add_test(NAME lf_ui_tests COMMAND lf_ui_tests)
