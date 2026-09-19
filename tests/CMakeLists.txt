# Unit tests for the Qt-free engine modules. A tiny header-only harness keeps
# the dependency footprint at zero; it can be swapped for Catch2/doctest later
# without touching the test bodies much.
add_executable(lf_tests
    TestMain.h
    test_time.cpp
    test_undo.cpp
    test_frame_layout.cpp
)
target_link_libraries(lf_tests PRIVATE lumaforge::core lumaforge::commands lumaforge::rendering)
add_test(NAME lf_tests COMMAND lf_tests)
