add_library(lf_hardware STATIC
    include/lumaforge/hardware/GpuDevice.h
    include/lumaforge/hardware/HwCapabilities.h
    include/lumaforge/hardware/HardwareDetector.h
    src/GpuDevice.cpp
    src/HardwareDetector.cpp
    src/EncoderProbe.cpp
)
add_library(lumaforge::hardware ALIAS lf_hardware)
target_include_directories(lf_hardware PUBLIC include)
target_link_libraries(lf_hardware
    PUBLIC lumaforge::core
    PRIVATE FFmpeg::avcodec FFmpeg::avutil d3d11 dxgi dxguid)
