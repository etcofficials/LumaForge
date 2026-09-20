configure_file(Version.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/lumaforge/core/Version.h" @ONLY)

add_library(lf_core STATIC
    include/lumaforge/core/Time.h
    include/lumaforge/core/Rational.h
    include/lumaforge/core/Result.h
    include/lumaforge/core/Log.h
    include/lumaforge/core/StringUtil.h
    include/lumaforge/core/VisualParams.h
    src/Log.cpp
    src/VisualParams.cpp
    src/StringUtil.cpp
)
add_library(lumaforge::core ALIAS lf_core)
target_include_directories(lf_core PUBLIC include "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(lf_core PUBLIC lumaforge::options)
