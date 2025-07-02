# Druid Engine

## Overview
Druid is a lightweight game engine and editor built with C++, SDL3, OpenGL, and ImGui. The project is organized into different components:
- **Core Engine** (in `src/`): Handles application logic, math, input, rendering, and systems like physics and world modeling.
- **Editor** (in `Editor/`): A dockable ImGui-based editor for scene and asset management, with a real-time viewport and inspector.
- **Testbed** (in `testbed/`): A sandbox for running engine demos and experiments.

## Features
- Modular architecture (core, editor, testbed)
- Real-time rendering with OpenGL
- ImGui-based editor UI (docking branch)
- Scene hierarchy, inspector, and viewport panels
- Physics and rendering systems
- Asset loading (models, textures, shaders)

## Directory Structure
```
Druid/
  src/         # Core engine source code
  include/     # Engine headers (public API)
  Editor/      # Editor app (ImGui, SDL3, OpenGL)
  testbed/     # Test/demo app
  deps/        # Third-party libraries (SDL3, ImGui, GLEW, etc.)
```

## Prerequisites
- CMake
- SDL3
- OpenGL
- ImGui (docking branch)
- GLEW
- A C++ compiler (tested with MinGW-w64)

## Building
1. Make sure all dependencies are present in the `deps/` folder (DLLs, libs, and headers for SDL3, ImGui, GLEW, etc.).
2. From the project root, you can build the engine, editor, and testbed:
   ```sh
   make           # or use CMake as needed
   cd Editor && make
   cd ../testbed && make
   ```
   Adjust for your platform and build system as needed.

## Running
- **Editor:** Run the executable from `Editor/bin/` or your build output directory. This launches the ImGui-based editor with dockable panels and a real-time viewport.
- **Testbed:** Run the testbed app from `testbed/bin/` to see engine demos and experiments.

## Usage Notes
- The editor uses ImGui's docking branch. You can rearrange panels or reset the layout by restarting.
- The viewport panel in the editor displays the engine's rendered output using an OpenGL framebuffer.
- Asset files (models, textures, shaders) are in the `res/` folders of Editor and testbed.

## Troubleshooting
- **DockBuilder or docking errors:** Make sure you're using the ImGui docking branch and have included `imgui_internal.h` where needed.
- **lerp conflict:** If you have a custom `lerp` function, put it in a namespace or rename it to avoid conflicts with C++20's `std::lerp`.
- **Multi-viewport assertion:** If you enable `ImGuiConfigFlags_ViewportsEnable`, call `ImGui::UpdatePlatformWindows()` and `ImGui::RenderPlatformWindowsDefault()` after your main render call.
- **OpenGL/SDL issues:** Double-check your initialization order and that all required DLLs are present.

## License
See the licenses for ImGui, SDL3, and other dependencies in their respective folders. The Druid engine code is MIT-licensed unless otherwise noted. 
