cmake_minimum_required(VERSION 3.28)
project(LumaForgeToolchainCheck LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake")

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets)
find_package(FFmpeg REQUIRED COMPONENTS avcodec avformat avutil swscale swresample)

qt_standard_project_setup()

qt_add_executable(qt_check qt_check.cpp)
target_link_libraries(qt_check PRIVATE Qt6::Widgets)

add_executable(ffmpeg_check ffmpeg_check.cpp)
target_link_libraries(ffmpeg_check PRIVATE FFmpeg::FFmpeg)

# Report what was found so the diagnostic script can print exact paths.
message(STATUS "LF_QT_DIR=${Qt6_DIR}")
message(STATUS "LF_QT_VERSION=${Qt6_VERSION}")
message(STATUS "LF_FFMPEG_ROOT=${FFmpeg_ROOT_DIR}")
message(STATUS "LF_FFMPEG_VERSION=${FFmpeg_VERSION}")
message(STATUS "LF_FFMPEG_LICENSE=${FFmpeg_LICENSE}")
message(STATUS "LF_CXX_COMPILER=${CMAKE_CXX_COMPILER}")
message(STATUS "LF_CXX_COMPILER_VERSION=${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "LF_RC_COMPILER=${CMAKE_RC_COMPILER}")
message(STATUS "LF_MT=${CMAKE_MT}")
message(STATUS "LF_GENERATOR=${CMAKE_GENERATOR}")
message(STATUS "LF_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}")
