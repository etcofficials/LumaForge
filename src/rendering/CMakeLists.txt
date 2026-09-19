add_library(lf_rendering STATIC
    include/lumaforge/rendering/FrameLayout.h
    include/lumaforge/rendering/PreviewRenderer.h
    src/PreviewRenderer.cpp
)
add_library(lumaforge::rendering ALIAS lf_rendering)
target_include_directories(lf_rendering PUBLIC include)
target_link_libraries(lf_rendering PUBLIC lumaforge::core lumaforge::hardware PRIVATE d3d11 dxgi)
