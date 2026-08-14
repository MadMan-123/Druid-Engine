# build-system-testbed

Files covered: `CMakeLists.txt` (root), `Editor/CMakeLists.txt`, `testbed/CMakeLists.txt`,
`generate_msvc_import_lib.cmake`, `generate_msvc_lib.bat`, `druid_msvc.def`, `testbed/src/main.cpp`.

## Synopsis

The build system produces three artifacts: the engine as a shared library (`druid` /
`libdruid.dll`), the `editor` executable (linked into the same CMake tree via
`add_subdirectory(Editor)`), and a standalone `testbed` executable that is its own CMake
project linking directly against the prebuilt `bin/libdruid.dll`. The toolchain is
MinGW-GCC-flavored throughout (`-O3 -march=native`, `libglew.dll.a`-style import libs);
two helper scripts (`generate_msvc_import_lib.cmake`, `generate_msvc_lib.bat`) exist to
bridge the MinGW-built DLL to MSVC consumers by generating `.lib` import libraries. The
testbed also serves as the canonical "minimal Druid game" sample.

## Targets & how they link

### `druid` (shared library, root `CMakeLists.txt`)

- Project `Druid`, languages C and CXX; C11 / C++17 (`CMAKE_C_STANDARD 11`,
  `CMAKE_CXX_STANDARD 17`).
- Sources: `file(GLOB_RECURSE ... CONFIGURE_DEPENDS)` over `src/*.c`, `src/**/*.c`,
  `src/**/*.cpp`. `src/external/stb_image.c` gets `-w` (warnings suppressed).
- Output: all runtime/library/archive output goes to `${CMAKE_SOURCE_DIR}/bin`.
- Public include dirs: `include/`, `include/GL/`, and notably `Editor/deps` (so anything
  linking `druid` also sees the editor's dependency headers, e.g. imgui).
- Compile definitions (PRIVATE): `DRUID_EXPORT`, `WIN32`, `DRUID_PROFILE`.
- Compile options: `-Wall -g -O3 -march=native -mavx2 -ffast-math -funroll-loops -fopenmp`;
  `-fPIC` only on non-Windows via generator expression. Comments in the file note LTO is
  deliberately disabled (MinGW GCC ICE when building the engine DLL with `-flto`) and that
  the "game plugin" can use LTO because it's a separate TU.
- Links (PUBLIC): `deps/libglew.dll.a`, `deps/libSDL3.dll.a`, `deps/libassimp.dll.a`
  (MinGW import libs referenced by absolute path), plus `opengl32 gdi32 user32` and
  `-fopenmp` (libgomp runtime).
- **Symbol export strategy**: `CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON` — every symbol is
  exported from the DLL rather than relying on per-function `__declspec(dllexport)`
  annotations (the `DRUID_EXPORT` define presumably also drives export macros in headers;
  header not in scope here).
- Post-build: copies `deps/libassimp-6.dll` into `bin/`.
- Extra targets: `clean_cache` (custom target that deletes `CMakeFiles/` and
  `CMakeCache.txt` from the build dir). `CMAKE_EXPORT_COMPILE_COMMANDS ON` is set.
- `add_subdirectory(Editor)` pulls the editor into this build; `testbed/` is **not**
  added (see Open questions).

### `editor` (executable, `Editor/CMakeLists.txt`)

- Sub-project `Editor`, C++17.
- Sources: glob of `Editor/src/*.cpp` plus an explicit, redundant `"src/entitypicker.cpp"`
  in `add_executable` (the glob already matches it, listing it twice in the source list);
  imgui sources (`imgui.cpp`, `imgui_demo.cpp`, `imgui_draw.cpp`, `imgui_tables.cpp`,
  `imgui_widgets.cpp`, `imgui_impl_sdl3.cpp`, `imgui_impl_opengl3.cpp`) from
  `Editor/deps/imgui` are appended later via `target_sources`.
- Output dir: `Editor/bin` (overrides the root's `bin/` via
  `RUNTIME_OUTPUT_DIRECTORY` target property) — so the editor exe lives in `Editor/bin/`
  while `libdruid.dll` lives in the root `bin/`.
- Links: `druid` (as a CMake target, so it inherits druid's PUBLIC includes and PUBLIC
  link libs: GLEW/SDL3/assimp/opengl32/etc.).
- Compile definitions: `DRUID_PROFILE`, `DRUID_SYSTEM_EXPORT` (meaning of
  `DRUID_SYSTEM_EXPORT` not determinable from these files).
- Compile options: `-Wall -g -O3 -march=native -ffast-math -funroll-loops` (no `-mavx2`,
  no `-fopenmp`, unlike the engine).
- Post-build: copies `deps/glew32.dll` and `deps/SDL3.dll` next to the editor exe.
  Note: `libdruid.dll` and `libassimp-6.dll` are **not** copied to `Editor/bin` here,
  even though the exe depends on them at runtime.
- Extra target: `debug` — runs `gdb` on the editor exe with `Editor/bin` as the working
  directory (`USES_TERMINAL`).

### `testbed` (executable, `testbed/CMakeLists.txt` — standalone project)

- Independent project `Testbed`, C++20 (newer standard than engine/editor). Because it is
  a top-level project, `${CMAKE_SOURCE_DIR}` here means `testbed/`, and all engine paths
  are reached via `../`.
- Output: `testbed/bin`.
- Includes (directory-wide `include_directories`): `../include`, `../include/GL`,
  `../include/SDL3`.
- Sources: glob of `testbed/src/*.cpp` (currently just `main.cpp`).
- Linking: does **not** link the `druid` CMake target; it links the built artifact
  directly. There is an `if(MSVC)`/`else()` split:
  - MSVC branch: `../bin/libdruid.dll`, `../deps/glew32.lib`, `../deps/SDL3.lib`,
    `opengl32 gdi32 user32 winmm`. Note it passes the **`.dll` itself** to the MSVC
    linker rather than the `druid.lib` import library that `generate_msvc_lib.bat`
    produces — MSVC's `link.exe` cannot link a raw DLL, so this branch appears to depend
    on the import-lib workflow but doesn't reference it (see Open questions).
  - non-MSVC branch (MinGW): `../bin/libdruid.dll` (MinGW ld *can* link directly against
    a DLL), `../deps/libglew32.dll.a`, `../deps/libSDL3.dll.a`, plus the same system libs.
- `link_directories(../bin ../deps)` is called **after** `add_executable`, so it does not
  apply to the `testbed` target (harmless here since all libs are absolute paths).
- Post-build: copies `../bin/libdruid.dll`, `../deps/glew32.dll`, `../deps/SDL3.dll` next
  to the testbed exe. (`libassimp-6.dll` is not copied, though `generate_msvc_lib.bat`
  lists it among required runtime DLLs.)
- No compile options/definitions are set for testbed (no `-O3`, no `DRUID_PROFILE`);
  build type defaults apply.

### MSVC import-lib generation path

Two overlapping tools convert the MinGW-built `bin/libdruid.dll` into something MSVC can
link:

- `generate_msvc_import_lib.cmake` — script mode (`cmake -P`). Runs
  `gendef -a bin/libdruid.dll` (MinGW tool that extracts exported symbols into a `.def`),
  then only *prints* the second step for the user to run manually in a VS Developer
  Command Prompt: `lib /def:bin\libdruid.def /out:bin\druid.lib /machine:x64`. It sets
  `DEF_PATH`/`LIB_PATH` variables (`bin/druid.def`, `bin/druid.lib`) that are never
  actually used by the script. On gendef failure it warns "Trying alternative method..."
  but no alternative is implemented.
- `generate_msvc_lib.bat` — the fuller, automated version. Locates `lib.exe` (PATH first,
  then scans `C:\Program Files\Microsoft Visual Studio\2022\*\VC\Tools\MSVC\*\bin\Hostx64\x64`),
  requires `gendef.exe` at the hardcoded path `C:\msys64\mingw64\bin\gendef.exe`, then in
  `bin/`: generates `libdruid.def` from `libdruid.dll` and runs
  `lib /def:libdruid.def /out:druid.lib /machine:x64`; repeats for `glew32.dll` →
  `glew32.lib`. Ends by printing the VS linker settings to use
  (`druid.lib;glew32.lib;opengl32.lib`) and the runtime DLLs needed
  (`libdruid.dll, glew32.dll, SDL3.dll, libassimp-6.dll`).
- `druid_msvc.def` — a 2-line stub (`LIBRARY libdruid` / empty `EXPORTS`). It exports
  nothing and is not referenced by either script (both generate their own `.def` via
  gendef). Appears to be a leftover/placeholder.

This whole path exists because the engine relies on
`CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS` + MinGW, so no MSVC `.lib` is produced by the normal
build; gendef+lib.exe reconstruct one from the DLL's export table.

## The testbed game (`testbed/src/main.cpp`)

The minimal shape of a Druid application. Single include: `<druid.h>`.

Entry point:

```c
game = createApplication(init, update, render, destroy);
game->width = 1280; game->height = 720;
run(game);
```

i.e. the engine owns the main loop; the game registers four callbacks
(`init`, `update(f32 dt)`, `render(f32 dt)`, `destroy`) and configures the `Application*`
(here just `width`/`height`) before calling `run`. `createApplication` failure is checked
only with `assert`.

Engine features exercised:

- **Camera**: file-scope `Camera cam = {0};` initialized in `init` via
  `initCamera(&cam, {0,0,5}, fov /*70*/, 16.0f/9.0f, 0.1f, 100.0f)`.
- **Resources (engine global)**: a global `resources` object (defined by the engine, not
  this file) is consulted for the default shader — `findInMap(&resources->shaderIDs,
  "default", &index)` then `resources->shaderHandles[index]` — and for models
  (`resources->modelBuffer[0]`). How `resources` gets populated (presumably from
  `testbed/res/` or `testbed/scenes/default.scene`) is not visible in this file.
- **ECS archetype (SoA)**: `DEFINE_ARCHETYPE(StaticEntites, FIELD(b8, isActive),
  FIELD(Vec3, position), FIELD(Vec4, rotation), FIELD(Vec3, scale), FIELD(u32, modelID))`
  declares a struct-of-arrays archetype at file scope. `init` calls
  `createEntityArena(&StaticEntites, 128, NULL)` to allocate storage for 128 entities,
  then caches raw field pointers by casting `&StaticEntites.fields[N]`
  (`isActive`, `positions`, `rotations`, `scales`, `modelIDs` file-scope pointers) and
  zero-initializes `isActive`/`modelIDs` for all `StaticEntites.count` entries. Storage
  is engine/arena-owned; the testbed never frees it (`destroy` is empty).
- **Render loop**: `render` calls `clearDisplay(0.2,0.2,0.2,1)`, binds the shader with
  raw `glUseProgram(defaultShader)`, then for each active entity builds a
  `Transform t = {positions[i], rotations[i], scales[i]}`, calls
  `updateShaderModel(defaultShader, t)` and `draw(model, defaultShader, false)` on
  `resources->modelBuffer[0]`. Since every `isActive[i]` is set false in `init` and
  nothing sets one true, nothing is actually drawn — the file's own comment calls it a
  "Blank testbed".
- `update` and `destroy` are no-ops.

Data/ownership summary: `Application *game` is created by the engine
(`createApplication`) and driven by `run`; the archetype arena is allocated by
`createEntityArena` (engine-owned); `cam`, `fov`, `defaultShader`, and the cached field
pointers are plain file-scope globals in the game module.

### Sample assets (listed, not read)

- `testbed/res/`: shader pairs `shader`, `Lighting`, `bump`, `fog`, `Toon`, `warble`,
  `water` (`.vert`/`.frag` each), terrain set (`terrain.vert/.frag/.comp` — includes a
  compute shader), `res/Models/Buildings.mtl` (an .mtl with no accompanying .obj in this
  listing), and `res/Textures/`: `Brick.png`, `bricks.jpg`, `Grass(0).png`,
  `Grass(1).png`, `normal.jpg`, `Snow.png`, `Stone(0).png`, `Stone(1).png`, `Water.jpg`,
  `water.png`.
- `testbed/scenes/`: `default.scene` (single sample scene).

## Internal (static) helpers

None — no static functions in any of these files. The four callbacks in `main.cpp`
(`init`, `update`, `render`, `destroy`) are non-static file-level functions passed to
`createApplication`.

## Open questions / unclear intent

1. **Testbed is orphaned from the root build.** Root `CMakeLists.txt` does
   `add_subdirectory(Editor)` but never adds `testbed/`; `testbed/CMakeLists.txt` is
   written as an independent top-level project (own `project()`, `../`-relative paths,
   links the prebuilt `../bin/libdruid.dll` file rather than the `druid` target). The
   intended workflow is presumably: build the root project first to produce
   `bin/libdruid.dll`, then configure/build `testbed/` separately — but nothing in these
   files documents or enforces that ordering.
2. **MSVC testbed link path looks broken/incomplete.** The `if(MSVC)` branch links
   `../bin/libdruid.dll` directly, which MSVC's linker doesn't accept; the import-lib
   scripts produce `bin/druid.lib`, but the MSVC branch never references it.
3. **`druid_msvc.def` has an empty EXPORTS section** and is unreferenced by both
   generation scripts. Placeholder or dead file?
4. **`generate_msvc_import_lib.cmake` half-finished**: `DEF_PATH`/`LIB_PATH` are set but
   unused; the "alternative method" after a gendef failure is a message only; the final
   `lib` step is manual. Overlaps entirely with `generate_msvc_lib.bat` — unclear which
   is canonical.
5. **`resources` global and asset loading**: how `resources->shaderIDs` /
   `modelBuffer[0]` get populated (from `testbed/res/`? `scenes/default.scene`? engine
   defaults?) cannot be determined from these files; nothing in the testbed CMake copies
   `res/` or `scenes/` next to the exe either.
6. **Field-pointer cast pattern**: `isActive = (b8*)&StaticEntites.fields[0]` takes the
   address of the fields-array element and casts it; whether `fields[i]` is itself a
   pointer (making this a cast of a pointer-to-pointer) depends on the
   `DEFINE_ARCHETYPE` macro definition, which lives in engine headers outside this
   module's file list.
7. **`DRUID_SYSTEM_EXPORT`** (editor) vs **`DRUID_EXPORT`** (engine): the semantics of
   these defines are in engine headers, not readable here.
8. **Editor runtime DLLs**: editor post-build copies glew32/SDL3 but not
   `libdruid.dll`/`libassimp-6.dll` into `Editor/bin`; unclear whether PATH or a manual
   copy is the intended runtime arrangement.
9. **`res/Models/Buildings.mtl`** exists with no matching mesh file in `testbed/res/` —
   unclear if the model lives elsewhere or is missing.
