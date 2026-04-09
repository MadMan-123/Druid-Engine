cmake_minimum_required(VERSION 3.15)
project(Editor CXX)

set(CMAKE_CXX_STANDARD 17)

# Note: LTO disabled due to heap corruption issues with aggressive optimization

# Source files
file(GLOB EDITOR_SOURCES CONFIGURE_DEPENDS
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

add_executable(editor ${EDITOR_SOURCES}    "src/entitypicker.cpp")

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

target_compile_definitions(editor PRIVATE DRUID_PROFILE)

# Conservative optimization flags: match engine optimization strategy without aggressive options
target_compile_options(editor PRIVATE
    -Wall
    -fPIC
    -g
    -O3                        # Always optimize (unconditional, like engine)
    -march=native              # Optimize for current CPU
    -ffast-math                # Fast floating-point math
    -funroll-loops
)

target_sources(editor PRIVATE ${IMGUI_SOURCES}) 

add_custom_target(debug
    COMMAND gdb $<TARGET_FILE:editor>
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin
    COMMENT "Running editor in GDB..."
    USES_TERMINAL
) 