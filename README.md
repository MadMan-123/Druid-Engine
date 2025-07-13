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
├── src/         # Core engine implementation
├── include/     # Public API (single header: druid.h)
├── Editor/      # ImGui-based editor
├── testbed/     # Experimental sandbox
├── deps/        # Third-party libraries (SDL3, GLEW, ImGui, etc.)
└── bin/         # Output binaries

```

## Public API

All engine functionality is exposed via `include/druid.h`. This is a single-header API designed to act as both the **entry point and documentation** for Druid.

You can:

- Create entities and assign components
- Control rendering and physics behavior
- Load textures, models, and shaders
- Hook into input, simulation, and draw updates

---

## Building

### Windows (MSYS2 + GCC)

1. Install MSYS2 from https://www.msys2.org/ and update packages:
   pacman -Syu
   pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja

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

2. Clone the repository and ensure all third-party libraries are placed inside the `deps/` folder.

3. Build using Ninja or Makefiles:

- Using Ninja (recommended):

  ```
  cmake -G "Ninja" -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ .
  ninja
  ```

- Using MinGW Makefiles:

  ```
  cmake -G "MinGW Makefiles" .
  mingw32-make
  ```

---

## Running

### Editor

Run:
./bin/Editor.exe
- Opens the dockable ImGui editor UI
- Shows the real-time OpenGL viewport output
- Hierarchy and Inspector panels for entity/component editing

### Testbed

Run:
./bin/testbed.exe


- Opens the sandbox for engine demos and experiments

---

## Visual Studio Support (Experimental)

Visual Studio is not officially supported yet, but you can build using MSYS2 GCC + Ninja through CMake.

Example `CMakeSettings.json`:

```json
{
  "configurations": [
    {
      "name": "x64-Debug",
      "generator": "Ninja",
      "configurationType": "Debug",
      "buildRoot": "${projectDir}\\out\\build\\${name}",
      "installRoot": "${projectDir}\\out\\install\\${name}",
      "environment": {
        "CC": "gcc",
        "CXX": "g++"
      },
      "inheritEnvironments": [ "gcc" ]
    }
  ]
}
```
You must still install MSYS2 and configure GCC environment accordingly.


| Issue                 | Solution                                                                                                          |
|-----------------------|-------------------------------------------------------------------------------------------------------------------|
| ImGui docking errors  | Use the docking branch of ImGui and include `imgui_internal.h`                                                    |
| `std::lerp` conflict  | Rename or namespace your custom `lerp` function                                                                   |
| Viewport crash/assert | If using multi-viewport mode, call `ImGui::UpdatePlatformWindows();` and `ImGui::RenderPlatformWindowsDefault();` |
| Missing DLLs          | Ensure SDL3.dll, glew32.dll, and other dependencies are present in `deps/`                                        |


## License
The Druid engine source code is licensed under the MIT License.

Dependencies (SDL3, GLEW, ImGui, etc.) are licensed under their own terms, see their folders in deps/.
## Future Licensing Note
Druid is currently MIT-licensed for open and commercial use.

However, future versions of the engine, editor, or tools may adopt a dual-license or commercial licensing model to support ongoing development.

## MIT License
Copyright (c) 2025 Madoc Wolstencroft

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.