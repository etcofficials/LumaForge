# FindFFmpeg.cmake — locate FFmpeg shared development libraries.
#
# Expects a prebuilt "shared" layout (e.g. BtbN FFmpeg-Builds win64-*-shared):
#     <root>/include/libavcodec/avcodec.h
#     <root>/lib/avcodec.lib            (MSVC import libraries)
#     <root>/bin/avcodec-*.dll
#
# Search order for <root>:  FFMPEG_ROOT (CMake var)  ->  ENV{FFMPEG_ROOT}  ->  ENV{LUMAFORGE_FFMPEG_ROOT}
#
# Provides imported targets  FFmpeg::avcodec  FFmpeg::avformat  FFmpeg::avutil
#                            FFmpeg::swscale  FFmpeg::swresample  FFmpeg::avfilter (if present)
# and the umbrella target    FFmpeg::FFmpeg   (all requested components).
#
# Each target carries IMPORTED_LOCATION = the runtime DLL and IMPORTED_IMPLIB = the .lib, so
# $<TARGET_RUNTIME_DLLS:...> can be used to copy DLLs next to executables.
#
# Result variables: FFmpeg_FOUND, FFmpeg_VERSION (from libavcodec/version.h), FFmpeg_ROOT_DIR,
#                   FFmpeg_INCLUDE_DIR, FFmpeg_RUNTIME_DLLS, FFmpeg_LICENSE ("GPL"/"LGPL"/"unknown")

include(FindPackageHandleStandardArgs)

set(_ffmpeg_hints ${FFMPEG_ROOT} $ENV{FFMPEG_ROOT} $ENV{LUMAFORGE_FFMPEG_ROOT})
find_path(FFmpeg_INCLUDE_DIR
    NAMES libavcodec/avcodec.h
    HINTS ${_ffmpeg_hints}
    PATH_SUFFIXES include
    NO_DEFAULT_PATH)
find_path(FFmpeg_INCLUDE_DIR NAMES libavcodec/avcodec.h PATH_SUFFIXES include)

if(FFmpeg_INCLUDE_DIR)
    get_filename_component(FFmpeg_ROOT_DIR "${FFmpeg_INCLUDE_DIR}" DIRECTORY)
endif()

if(NOT FFmpeg_FIND_COMPONENTS)
    set(FFmpeg_FIND_COMPONENTS avcodec avformat avutil swscale swresample)
endif()

set(FFmpeg_RUNTIME_DLLS "")
set(_ffmpeg_all_found TRUE)
foreach(_comp IN LISTS FFmpeg_FIND_COMPONENTS)
    find_library(FFmpeg_${_comp}_IMPLIB
        NAMES ${_comp}
        HINTS ${FFmpeg_ROOT_DIR}
        PATH_SUFFIXES lib
        NO_DEFAULT_PATH)
    file(GLOB _ffmpeg_dll_candidates "${FFmpeg_ROOT_DIR}/bin/${_comp}-*.dll")
    list(GET _ffmpeg_dll_candidates 0 FFmpeg_${_comp}_DLL)
    if(NOT FFmpeg_${_comp}_DLL)
        set(FFmpeg_${_comp}_DLL "FFmpeg_${_comp}_DLL-NOTFOUND")
    endif()

    if(FFmpeg_${_comp}_IMPLIB AND FFmpeg_${_comp}_DLL)
        set(FFmpeg_${_comp}_FOUND TRUE)
        if(NOT TARGET FFmpeg::${_comp})
            add_library(FFmpeg::${_comp} SHARED IMPORTED)
            set_target_properties(FFmpeg::${_comp} PROPERTIES
                IMPORTED_LOCATION "${FFmpeg_${_comp}_DLL}"
                IMPORTED_IMPLIB   "${FFmpeg_${_comp}_IMPLIB}"
                INTERFACE_INCLUDE_DIRECTORIES "${FFmpeg_INCLUDE_DIR}")
        endif()
        list(APPEND FFmpeg_RUNTIME_DLLS "${FFmpeg_${_comp}_DLL}")
    else()
        set(FFmpeg_${_comp}_FOUND FALSE)
        if(FFmpeg_FIND_REQUIRED_${_comp} OR NOT DEFINED FFmpeg_FIND_REQUIRED_${_comp})
            set(_ffmpeg_all_found FALSE)
        endif()
    endif()
    mark_as_advanced(FFmpeg_${_comp}_IMPLIB)
endforeach()

# Version from libavcodec/version.h (+ version_major.h in FFmpeg >= 5)
if(FFmpeg_INCLUDE_DIR)
    set(_maj 0 _min 0 _mic 0)
    if(EXISTS "${FFmpeg_INCLUDE_DIR}/libavcodec/version_major.h")
        file(STRINGS "${FFmpeg_INCLUDE_DIR}/libavcodec/version_major.h" _l REGEX "#define LIBAVCODEC_VERSION_MAJOR +[0-9]+")
        string(REGEX REPLACE ".*MAJOR +([0-9]+).*" "\\1" _maj "${_l}")
    endif()
    file(STRINGS "${FFmpeg_INCLUDE_DIR}/libavcodec/version.h" _l REGEX "#define LIBAVCODEC_VERSION_(MAJOR|MINOR|MICRO) +[0-9]+")
    foreach(_line IN LISTS _l)
        if(_line MATCHES "MAJOR +([0-9]+)")
            set(_maj "${CMAKE_MATCH_1}")
        elseif(_line MATCHES "MINOR +([0-9]+)")
            set(_min "${CMAKE_MATCH_1}")
        elseif(_line MATCHES "MICRO +([0-9]+)")
            set(_mic "${CMAKE_MATCH_1}")
        endif()
    endforeach()
    set(FFmpeg_VERSION "libavcodec ${_maj}.${_min}.${_mic}")
endif()

# License hint from the bundled LICENSE file (BtbN builds ship LICENSE.txt naming GPL or LGPL)
set(FFmpeg_LICENSE "unknown")
if(FFmpeg_ROOT_DIR)
    file(GLOB _lic "${FFmpeg_ROOT_DIR}/LICENSE*")
    if(_lic)
        list(GET _lic 0 _lic)
        file(READ "${_lic}" _lic_text LIMIT 4000)
        if(_lic_text MATCHES "GNU General Public License" AND NOT _lic_text MATCHES "Lesser General Public")
            set(FFmpeg_LICENSE "GPL")
        elseif(_lic_text MATCHES "Lesser General Public")
            set(FFmpeg_LICENSE "LGPL")
        endif()
    endif()
endif()

find_package_handle_standard_args(FFmpeg
    REQUIRED_VARS FFmpeg_INCLUDE_DIR FFmpeg_ROOT_DIR _ffmpeg_all_found
    VERSION_VAR FFmpeg_VERSION
    HANDLE_COMPONENTS)

if(FFmpeg_FOUND AND NOT TARGET FFmpeg::FFmpeg)
    add_library(FFmpeg::FFmpeg INTERFACE IMPORTED)
    foreach(_comp IN LISTS FFmpeg_FIND_COMPONENTS)
        if(TARGET FFmpeg::${_comp})
            target_link_libraries(FFmpeg::FFmpeg INTERFACE FFmpeg::${_comp})
        endif()
    endforeach()
endif()

mark_as_advanced(FFmpeg_INCLUDE_DIR)
unset(_ffmpeg_hints)
unset(_ffmpeg_all_found)
