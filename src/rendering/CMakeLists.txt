include(LumaForgeShaders)

add_library(lf_rendering STATIC
    include/lumaforge/rendering/FrameLayout.h
    include/lumaforge/rendering/PreviewRenderer.h
    include/lumaforge/rendering/FrameTexture.h
    include/lumaforge/rendering/Compositor.h
    src/PreviewRenderer.cpp
    src/FrameTexture.cpp
    src/Compositor.cpp
)
add_library(lumaforge::rendering ALIAS lf_rendering)
target_include_directories(lf_rendering PUBLIC include)
target_link_libraries(lf_rendering PUBLIC lumaforge::core lumaforge::hardware lumaforge::media PRIVATE d3d11 dxgi)

lumaforge_add_shaders(lf_rendering
    "${CMAKE_SOURCE_DIR}/resources/shaders/quad_vs.hlsl"  main vs_4_0 g_quad_vs
    "${CMAKE_SOURCE_DIR}/resources/shaders/layer_ps.hlsl" main ps_4_0 g_layer_ps
)
