# To build with GCC/MinGW on Windows, run:
#   cmake -G "MinGW Makefiles" .
#   mingw32-make
# Or with Ninja:
#   cmake -G "Ninja" .
#   ninja

# Only set compiler if not already set
if(NOT CMAKE_C_COMPILER)
    set(CMAKE_C_COMPILER gcc)
endif()

cmake_minimum_required(VERSION 3.16)
project(Druid C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

# Set output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/GL
)

# Source files
file(GLOB_RECURSE ENGINE_SOURCES
    ${CMAKE_SOURCE_DIR}/src/*.c
)

add_library(druid SHARED ${ENGINE_SOURCES})

# Compiler definitions and options
target_compile_definitions(druid PRIVATE DRUID_EXPORT WIN32)
target_compile_options(druid PRIVATE -Wall -O2 -std=c11 -fPIC -g)

# Link libraries manually from deps
target_link_libraries(druid PRIVATE
    ${CMAKE_SOURCE_DIR}/deps/libglew32.dll.a
    ${CMAKE_SOURCE_DIR}/deps/libSDL3.dll.a
    opengl32
    gdi32
    user32
)


