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
)
target_link_libraries(lf_tests PRIVATE lumaforge::core lumaforge::commands lumaforge::rendering lumaforge::media lumaforge::project lumaforge::hardware lumaforge::playback lumaforge::audio)
target_compile_definitions(lf_tests PRIVATE LF_TEST_MEDIA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/media")
add_test(NAME lf_tests COMMAND lf_tests)
