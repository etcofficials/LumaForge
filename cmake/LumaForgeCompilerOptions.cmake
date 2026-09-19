# Shared compiler settings, applied through the INTERFACE target lumaforge::options.

add_library(lumaforge_options INTERFACE)
add_library(lumaforge::options ALIAS lumaforge_options)

target_compile_features(lumaforge_options INTERFACE cxx_std_20)

if(MSVC)
    target_compile_options(lumaforge_options INTERFACE
        /W4                 # high warning level
        /permissive-        # standards conformance
        /Zc:__cplusplus     # report the real __cplusplus value
        /Zc:preprocessor    # conforming preprocessor
        /utf-8              # source and execution charset
        /EHsc
        /MP                 # parallel compilation within a target
        /w44062             # enum value not handled in switch (with no default)
        /w44265             # class has virtual functions but destructor is not virtual
        /wd4251             # dll-interface warnings are noise for static libs
    )
    target_compile_definitions(lumaforge_options INTERFACE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        UNICODE
        _UNICODE
        _CRT_SECURE_NO_WARNINGS
        _WIN32_WINNT=0x0A00     # Windows 10
    )
    if(LUMAFORGE_WARNINGS_AS_ERRORS)
        target_compile_options(lumaforge_options INTERFACE /WX)
    endif()
    # Debug builds: full debug info; Release: keep PDBs for crash analysis.
    target_compile_options(lumaforge_options INTERFACE $<$<CONFIG:Debug>:/Od /Zi> $<$<NOT:$<CONFIG:Debug>>:/Zi>)
    target_link_options(lumaforge_options INTERFACE /DEBUG $<$<NOT:$<CONFIG:Debug>>:/OPT:REF /OPT:ICF>)
endif()
