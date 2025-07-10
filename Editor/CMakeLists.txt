cmake_minimum_required(VERSION 3.15)
project(Editor CXX)

set(CMAKE_CXX_STANDARD 17)

# Source files
file(GLOB EDITOR_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

file(GLOB IMGUI_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/deps/imgui/imgui.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/deps/imgui/imgui_demo.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/deps/imgui/imgui_draw.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/deps/imgui/imgui_tables.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/deps/imgui/imgui_widgets.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/deps/imgui/imgui_impl_sdl3.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/deps/imgui/imgui_impl_opengl3.cpp"
)

add_executable(editor ${EDITOR_SOURCES})

set_target_properties(editor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
)

# Link libraries
target_link_libraries(editor PRIVATE
    druid
)

# Post-build: copy DLLs to output directory
add_custom_command(TARGET editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:druid>
        $<TARGET_FILE_DIR:editor>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/deps/glew32.dll"
        $<TARGET_FILE_DIR:editor>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/deps/SDL3.dll"
        $<TARGET_FILE_DIR:editor>
)

target_include_directories(editor PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/deps/imgui"
)

target_sources(editor PRIVATE ${IMGUI_SOURCES}) 