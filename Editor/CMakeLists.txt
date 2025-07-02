cmake_minimum_required(VERSION 3.15)
project(Editor CXX)

set(CMAKE_CXX_STANDARD 17)

# Set output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/../include
    ${CMAKE_SOURCE_DIR}/../include/GL
    ${CMAKE_SOURCE_DIR}/../include/SDL3
)

# Source files
file(GLOB EDITOR_SOURCES
    ${CMAKE_SOURCE_DIR}/src/*.cpp
)

file(GLOB IMGUI_SOURCES
    ${CMAKE_SOURCE_DIR}/deps/imgui/imgui*.cpp
    ${CMAKE_SOURCE_DIR}/deps/imgui/backends/imgui_impl_sdl3.cpp
    ${CMAKE_SOURCE_DIR}/deps/imgui/backends/imgui_impl_opengl3.cpp
)

add_executable(editor ${EDITOR_SOURCES})

# Link directories
link_directories(
    ${CMAKE_SOURCE_DIR}/../bin
    ${CMAKE_SOURCE_DIR}/../deps
)

# Link libraries
if(MSVC)
    target_link_libraries(editor PRIVATE
        ${CMAKE_SOURCE_DIR}/../bin/libdruid.dll
        ${CMAKE_SOURCE_DIR}/../deps/glew32.lib
        ${CMAKE_SOURCE_DIR}/../deps/SDL3.lib
        opengl32
        gdi32
        user32
        winmm
    )
else()
    target_link_libraries(editor PRIVATE
        ${CMAKE_SOURCE_DIR}/../bin/libdruid.dll
        ${CMAKE_SOURCE_DIR}/../deps/libglew32.dll.a
        ${CMAKE_SOURCE_DIR}/../deps/libSDL3.dll.a
        opengl32
        gdi32
        user32
        winmm
    )
endif()

# Post-build: copy DLLs to output directory
add_custom_command(TARGET editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/../bin/libdruid.dll
        $<TARGET_FILE_DIR:editor>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/../deps/glew32.dll
        $<TARGET_FILE_DIR:editor>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/../deps/SDL3.dll
        $<TARGET_FILE_DIR:editor>
)

target_include_directories(editor PRIVATE
    ${CMAKE_SOURCE_DIR}/deps/imgui
)

target_sources(editor PRIVATE ${IMGUI_SOURCES}) 