cmake_minimum_required(VERSION 3.16)

# To build with GCC/MinGW on Windows, run:
#   cmake -G "MinGW Makefiles" .
#   mingw32-make
# Or with Ninja:
#   cmake -G "Ninja" .
#   ninja

# Detect OS and set compiler if not already set
if(WIN32)
    # Default to MinGW GCC if not set, but allow user override
    if(NOT DEFINED CMAKE_C_COMPILER)
        set(CMAKE_C_COMPILER gcc)
    endif()
    if(NOT DEFINED CMAKE_CXX_COMPILER)
        set(CMAKE_CXX_COMPILER g++)
    endif()
elseif(UNIX)
    # On Linux, use system default (usually GCC/Clang)
    # Optionally, you can set defaults here too
endif()

project(Druid C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

# Set output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

# Source files
file(GLOB_RECURSE ENGINE_SOURCES
    ${CMAKE_SOURCE_DIR}/src/*.c
    ${CMAKE_SOURCE_DIR}/src/*.cpp
)

set_source_files_properties(${CMAKE_SOURCE_DIR}/src/stb_image.c PROPERTIES COMPILE_FLAGS "-w")

# 1. Create the library target first
add_library(druid SHARED ${ENGINE_SOURCES})

# 2. THEN set include directories, compile options, etc.
target_include_directories(druid PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/GL
)

target_compile_definitions(druid PRIVATE DRUID_EXPORT WIN32)
target_compile_options(druid PRIVATE -Wall -O2 -std=c11 -fPIC -g)

target_link_libraries(druid PRIVATE
    ${CMAKE_SOURCE_DIR}/deps/libglew32.dll.a
    ${CMAKE_SOURCE_DIR}/deps/libSDL3.dll.a
    opengl32
    gdi32
    user32
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Add a custom target to clean the CMake cache and configuration files.
# This forces a complete reconfiguration on the next run.
#
# Usage with Makefiles:  mingw32-make clean_cache
# Usage with Ninja:      ninja clean_cache
# Usage with MSBuild:    msbuild.exe /t:clean_cache ALL_BUILD.vcxproj
#
add_custom_target(clean_cache
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${CMAKE_BINARY_DIR}/CMakeFiles"
    COMMAND ${CMAKE_COMMAND} -E remove -f "${CMAKE_BINARY_DIR}/CMakeCache.txt"
    COMMENT "Clearing CMake cache and configuration files..."
)


