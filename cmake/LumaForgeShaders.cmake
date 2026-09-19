# Compiles HLSL to C headers (byte arrays) at build time with the Windows SDK's
# fxc.exe, so no d3dcompiler DLL is needed at run time and shaders ship inside
# the binary.
#
#   lumaforge_add_shaders(<target> <output_var> SHADERS <file> ENTRY <entry> PROFILE <profile> VAR <cname> ...)
#
# Each shader is given as a 4-tuple: path entry profile variable-name.

get_filename_component(_lf_sdk_bin "${CMAKE_RC_COMPILER}" DIRECTORY)
find_program(LUMAFORGE_FXC NAMES fxc fxc.exe HINTS "${_lf_sdk_bin}" NO_DEFAULT_PATH)
if(NOT LUMAFORGE_FXC)
    message(FATAL_ERROR "fxc.exe not found next to rc.exe (${_lf_sdk_bin}); the Windows SDK 'Desktop Tools' component is required")
endif()

function(lumaforge_add_shaders target)
    set(_out_dir "${CMAKE_CURRENT_BINARY_DIR}/shaders")
    file(MAKE_DIRECTORY "${_out_dir}")
    set(_headers "")
    set(_args ${ARGN})
    list(LENGTH _args _n)
    math(EXPR _last "${_n} - 1")
    set(_i 0)
    while(_i LESS _n)
        list(GET _args ${_i} _src)
        math(EXPR _i "${_i} + 1")
        list(GET _args ${_i} _entry)
        math(EXPR _i "${_i} + 1")
        list(GET _args ${_i} _profile)
        math(EXPR _i "${_i} + 1")
        list(GET _args ${_i} _var)
        math(EXPR _i "${_i} + 1")
        get_filename_component(_name "${_src}" NAME_WE)
        set(_hdr "${_out_dir}/${_name}.h")
        add_custom_command(
            OUTPUT "${_hdr}"
            COMMAND "${LUMAFORGE_FXC}" /nologo /T ${_profile} /E ${_entry} /O3 /Fh "${_hdr}" /Vn ${_var} "${_src}"
            DEPENDS "${_src}"
            COMMENT "Compiling shader ${_name} (${_profile})"
            VERBATIM)
        list(APPEND _headers "${_hdr}")
    endwhile()
    target_sources(${target} PRIVATE ${_headers})
    target_include_directories(${target} PRIVATE "${_out_dir}")
endfunction()
