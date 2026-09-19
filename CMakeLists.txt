cmake_minimum_required(VERSION 3.28)

project(LumaForge
    VERSION 0.1.0
    DESCRIPTION "Lightweight professional video editor"
    LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "LumaForge currently targets Windows only (Direct3D 11 preview pipeline).")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(LUMAFORGE_BUILD_TESTS "Build unit tests" ON)
option(LUMAFORGE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# All executables and DLL-dependent targets land in one directory so the app
# runs straight out of the build tree.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

include(LumaForgeCompilerOptions)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Svg)
find_package(FFmpeg REQUIRED COMPONENTS avcodec avformat avutil swscale swresample)
qt_standard_project_setup()

message(STATUS "LumaForge ${PROJECT_VERSION}")
message(STATUS "  Qt       ${Qt6_VERSION}  (${Qt6_DIR})")
message(STATUS "  FFmpeg   ${FFmpeg_VERSION} [${FFmpeg_LICENSE}]  (${FFmpeg_ROOT_DIR})")
message(STATUS "  Compiler ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")

add_subdirectory(src)

if(LUMAFORGE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
