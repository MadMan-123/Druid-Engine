cmake_minimum_required(VERSION 3.16)

# Project definition
project(Druid C CXX)

# Set C/C++ standards
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

# Note: LTO disabled — mingw GCC crashes (silent ICE) when compiling shared library with -flto
# The game plugin uses LTO fine since it's a separate TU; engine DLL triggers GCC bug

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)


# Source files
# Source files
file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/src/*.c
    ${CMAKE_SOURCE_DIR}/src/**/*.c
    ${CMAKE_SOURCE_DIR}/src/**/*.cpp
)

# Disable warnings on stb_image
set_source_files_properties(${CMAKE_SOURCE_DIR}/src/external/stb_image.c PROPERTIES COMPILE_FLAGS "-w")

# Build engine as a shared library using the globbed sources only
add_library(druid SHARED ${ENGINE_SOURCES})

# Include directories
target_include_directories(druid PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/GL
    ${CMAKE_SOURCE_DIR}/Editor/deps
)


# Compiler options
target_compile_definitions(druid PRIVATE DRUID_EXPORT WIN32 DRUID_PROFILE)

# Conservative optimization flags: -march=native for CPU tuning, keep -O3/-Ofast
# Removed: -flto (Link-Time Optimization) and -ffast-math due to aggressive optimization issues
# These flags removed: -fno-strict-aliasing -fvisibility=hidden (too aggressive)
target_compile_options(druid PRIVATE
    -Wall
    -fPIC
    -g
    -O3                        # Always optimize (was behind generator expressions that never fired)
    -march=native              # Optimize for current CPU (5-10% gain, enables SIMD/AVX)
    -mavx2                     # Explicit AVX2 for SoA Vec3 batch operations
    -ffast-math                # Fast floating-point for physics/rendering hot paths
    -funroll-loops
    -fopenmp                   # OpenMP for multithreaded SSBO transform writes in renderer
)

target_link_libraries(druid PUBLIC
    ${CMAKE_SOURCE_DIR}/deps/libglew.dll.a
    ${CMAKE_SOURCE_DIR}/deps/libSDL3.dll.a
    ${CMAKE_SOURCE_DIR}/deps/libassimp.dll.a
    opengl32
    gdi32
    user32
    -fopenmp                   # Link OpenMP runtime (libgomp)
)


# Optional: Enable compile_commands.json generation
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Add Editor as a subdirectory if it's a separate project
add_subdirectory(Editor)
# Add a custom clean target to wipe CMake config
add_custom_target(clean_cache
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${CMAKE_BINARY_DIR}/CMakeFiles"
    COMMAND ${CMAKE_COMMAND} -E remove -f "${CMAKE_BINARY_DIR}/CMakeCache.txt"
    COMMENT "Clearing CMake cache and configuration files..."
)


add_custom_command(TARGET druid POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_SOURCE_DIR}/deps/libassimp-6.dll"
    "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/libassimp-6.dll"
)

# Regenerate MSVC-compatible import library after every DLL build
add_custom_command(TARGET druid POST_BUILD
    WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
    COMMAND gendef $<TARGET_FILE:druid>
    COMMAND dlltool -D $<TARGET_FILE_NAME:druid> -d libdruid.def -l druid.lib
    COMMENT "Regenerating druid.lib from updated DLL exports"
)

