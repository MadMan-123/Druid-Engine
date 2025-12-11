# CMake script for generating MSVC-compatible import library
# Usage: cmake -P generate_msvc_import_lib.cmake

# This requires Visual Studio to be installed
# The lib.exe tool must be in PATH (run from VS Developer Command Prompt)

set(DLL_PATH "${CMAKE_CURRENT_LIST_DIR}/bin/libdruid.dll")
set(DEF_PATH "${CMAKE_CURRENT_LIST_DIR}/bin/druid.def")
set(LIB_PATH "${CMAKE_CURRENT_LIST_DIR}/bin/druid.lib")

message(STATUS "Generating MSVC import library from: ${DLL_PATH}")

# Use gendef (from MinGW) to generate .def file
execute_process(
    COMMAND gendef -a ${DLL_PATH}
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/bin"
    RESULT_VARIABLE GENDEF_RESULT
)

if(NOT GENDEF_RESULT EQUAL 0)
    message(WARNING "gendef failed. Trying alternative method...")
endif()

message(STATUS "To complete the process, run in VS Developer Command Prompt:")
message(STATUS "  lib /def:bin\\libdruid.def /out:bin\\druid.lib /machine:x64")
