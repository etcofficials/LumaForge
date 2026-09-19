# Engine modules are Qt-free static libraries (plain C++20 + Win32/D3D11/FFmpeg)
# so they can be unit-tested and reasoned about without a GUI. Only ui/ and app/
# depend on Qt.
add_subdirectory(core)
add_subdirectory(diagnostics)
add_subdirectory(hardware)
add_subdirectory(commands)
add_subdirectory(media)
add_subdirectory(project)
add_subdirectory(rendering)
add_subdirectory(audio)
add_subdirectory(playback)

add_subdirectory(ui)
add_subdirectory(app)
