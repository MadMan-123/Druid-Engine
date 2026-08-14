# editor-project-builder

Files: `Editor/src/project_builder.h`, `Editor/src/project_builder.cpp`. C++ editor code (not engine C); includes `<druid.h>` and builds entirely on core-platform primitives (`writeFile`, `createDir`, `fileExists`, `normalizePath`, `platformFileCopy`, `platformDirCopyRecursive`, `platformPipeOpen/Close`, `platformGetExePath`, `dllLoad/dllSymbol/dllUnload`, `INFO`/`ERROR` logging).

## Synopsis

Owns the editor's project lifecycle: scaffolds a new game project on disk (source templates, shaders, CMakeLists), vendors engine headers/libs/DLLs into the project's `deps/`, drives CMake configure+build via shell pipes (capturing output into a log buffer), packages a standalone build into `export/`, and loads/unloads the resulting game DLL for play-in-editor through the `GamePlugin` function table. Owned by the editor; nothing here is called from within these two files except internally — all public functions are invoked by other editor code (external callers), and the game DLL contract it defines is consumed by the projects it generates.

## Public API Reference

All declared in `project_builder.h`.

### `void getEngineRoot(c8 *out, u32 size)`
Resolve the engine repo root from the running editor executable's path. Calls `platformGetExePath`, then `stripLastComponent` three times (exe filename → `bin` → `Editor`), then `normalizePath`. Assumes the editor exe lives at `<engineRoot>/Editor/bin/<exe>`. No return value; `out` receives the path. Called by `copyEngineFiles` and `updateProject`; also declared public for external editor use.

### `b8 generateProjectFiles(const c8 *projectDir)`
Write a fresh game project into `projectDir`. Via `writeTemplate` it creates, in order:
- `src/game.h` (`TPL_GAME_H`) — `GAME_API` export macro (`__declspec(dllexport/dllimport)` on Win32, `visibility("default")` elsewhere, gated on `GAME_EXPORT`), the `GamePlugin` struct + function-pointer typedefs (duplicating the definitions in `project_builder.h`), and the `extern "C" GAME_API void druidGetPlugin(GamePlugin *out)` declaration.
- `src/game.cpp` (`TPL_GAME_CPP`) — default plugin: `gameInit` calls `runtimeCreate(projectDir, runtimeDefaultConfig())`, `gameUpdate` → `runtimeUpdate(runtime, dt)`, `gameRender` → `runtimeBeginScenePass`/`runtimeEndScenePass`, `gameDestroy` → `runtimeDestroy(runtime)`; `druidGetPlugin` fills the table with these.
- `src/main.cpp` (`TPL_MAIN_CPP`) — standalone launcher: calls `druidGetPlugin` statically-linked, then `createApplication(DRUID_APP_TITLE, _init, _update, _render, _destroy)` at 1280x720 and `run(g_app)`; `_init` passes `"."` as the project dir.
- `res/shader.vert` (`TPL_SHADER_VERT`) — GLSL 420 vertex shader matching the engine interleaved layout (pos loc 0, texcoord 1, normal 2), `CoreShaderData` std140 UBO at binding 0 (`camPos`, `time`, `view`, `projection`), `model` uniform, outputs `tc`/`Normal`/`FragPos`/`TBN`.
- `res/shader.frag` (`TPL_SHADER_FRAG`) — fragment shader using the engine material uniform names (`albedoTexture`, `metallicTexture`, `roughnessTexture`, `normalTexture`, `roughness`, `metallic`, `transparency`, `colour`) so `updateMaterial()` works; only albedo/colour/transparency are actually sampled.
- `CMakeLists.txt` (`TPL_CMAKELISTS`) — project `Game` (C11/C++17), all outputs to `<project>/bin` for every config; includes `deps/include{,GL,SDL3}`; globs `src/*.c(pp)` excluding `main.cpp` into shared lib target `game` (compiled with `GAME_EXPORT`); links MSVC (`druid.lib`, `glew32.lib`, `libSDL3.dll.a`) vs non-MSVC (`libdruid.dll.a`, `libglew.dll.a`, `libSDL3.dll.a`), both plus `opengl32 gdi32 user32 winmm`; `game_standalone` exe from `main.cpp` links `game` + the mingw import libs, gets `DRUID_APP_TITLE` set to the project directory name and is renamed to `${DRUID_PROJECT_NAME}`; POST_BUILD on `game` copies `libdruid.dll`, `glew32.dll`, `SDL3.dll`, `libassimp-6.dll` from `deps/bin` next to the built target.

Then calls `copyEngineFiles(projectDir)` (fails the whole call with `ERROR` if that fails). Returns false on any template write failure. Side effects: creates files/directories, logs `INFO`. Caller: external (editor "new project" UI).

### `b8 copyEngineFiles(const c8 *projectDir)`
Vendor engine binaries/headers into the project. Resolves `getEngineRoot`, creates `deps/include`, `deps/lib`, `deps/bin`, recursively copies `<engineRoot>/include` → `deps/include` (`platformDirCopyRecursive`), then copies a fixed 14-entry file table via `platformFileCopy`: runtime DLLs (`bin/libdruid.dll`, `bin/libassimp-6.dll`, `deps/glew32.dll`, `deps/SDL3.dll`, `deps/assimp-vc143-mt.dll` → all into `deps/bin/`), mingw import libs (`bin/libdruid.dll.a`, `deps/libglew.dll.a`, `deps/libSDL3.dll.a`, `deps/libassimp.dll.a` → `deps/lib/`), MSVC import libs/def files (`bin/druid.lib`, `bin/druid.exp`, `bin/libdruid.def`, `deps/glew32.lib`, `deps/glew32.def` → `deps/lib/`). Logs `INFO` with `copied/fileCount`; individual copy failures are counted but not fatal — returns true unless `projectDir` is empty. Called by `generateProjectFiles` and `updateProject`; also public.

### `b8 buildProject(const c8 *projectDir, c8 *outLog, u32 logSize)`
CMake configure + build of the game project. Sets `g_buildInProgress = true` for the duration. Creates `<project>/build`, then runs two piped shell commands (Windows `cmd` syntax, output merged with `2>&1` and streamed line-by-line into `outLog`, truncating at `logSize`):
1. Configure: `cd /d "<project>/build" && cmake .. [-G "<generator>"]` — the generator is re-read from `build/CMakeCache.txt` by `readCMakeCacheGenerator` so re-configures reuse the original generator; first configure lets CMake pick the default (`buildConfigureCommand`).
2. Build: `cd /d "<project>/build" && cmake --build .`.

Returns false if either pipe fails to open or exits non-zero (`platformPipeClose` return). Side effects: writes `outLog`, toggles `g_buildInProgress`, creates `build/` and (via CMake) `bin/`. Caller: external (editor build button / play flow).

### `b8 buildStandalone(const c8 *projectDir, const c8 *scenePath, c8 *outLog, u32 logSize)`
Build the shippable game and package it into `<project>/export/`. Same configure step as `buildProject`, then `cmake --build . --target game_standalone` (failure aborts, returns false), then `cmake --build . --target game` (comment: "game_standalone links against it"; this step's exit code and pipe failure are ignored). Packaging:
- `createDir(export/)`; `writeStartupSceneConfig` writes `startup_scene.txt` containing just the scene *filename* (from `scenePath`, defaulting to `scene.drsc`) into **both** `projectDir` and `export/`.
- Copies the exe: tries `bin/<projectBaseName>.exe`, falls back to `bin/game_standalone.exe` → `export/<projectBaseName>.exe` (non-Windows: same without `.exe`).
- Copies a platform table: Win32 `bin/libgame.dll` plus `deps/bin/{libdruid.dll, glew32.dll, SDL3.dll, libassimp-6.dll}` → `export/`; non-Windows `bin/game_standalone`, `bin/libgame.so`, `deps/bin/libdruid.so`.
- Recursively copies `res/` → `export/res` and `scenes/` → `export/scenes`.

Copy failures are only reflected in the `copied` count logged via `INFO`; the function returns true if both mandatory cmake steps succeeded. Caller: external (editor export/ship action).

### `b8 updateProject(const c8 *projectDir, c8 *outLog, u32 logSize)`
Rebuild the **engine** and then refresh the project's `deps/`. Resolves `getEngineRoot`; if `<engineRoot>/build.ninja` exists, runs `cd /d "<engineRoot>" && C:\msys64\mingw64\bin\ninja.exe` (hardcoded absolute ninja path, comment: "in case it's not in PATH"), else `cd /d "<engineRoot>/build" && cmake --build .`. Streams output into `outLog`, then also dumps command/exit code/output to a `build_log.txt` in the **current working directory** (relative `fopen`). On non-zero exit appends a failure note pointing at `build_log.txt` and returns false. On success calls `copyEngineFiles(projectDir)` and returns its result. Toggles `g_buildInProgress`. Caller: external (editor "update engine in project" action).

### `b8 loadGameDLL(const c8 *dllPath, GameDLL *out)`
Load a built game DLL for play-in-editor. Zeroes `*out`, calls `dllLoad` (core-platform: copies the file to `<path>.running.dll` first so the original isn't locked during rebuilds — enabling rebuild-while-running), resolves the single required export `druidGetPlugin` via `dllSymbol` (typed as `GetPluginFn`), calls it to fill `out->plugin`, and validates that all four pointers (`init`, `update`, `render`, `destroy`) are non-NULL — any NULL causes `unloadGameDLL` + false with `ERROR` logs. Sets `out->loaded = true` and logs `INFO` on success. **Game DLL contract:** export exactly `extern "C" void druidGetPlugin(GamePlugin *out)` which must populate all four callbacks; the editor then drives play mode by calling `plugin.init(projectDir)`, `plugin.update(dt)`, `plugin.render(dt)`, `plugin.destroy()` (the call sites are external — not in these files). Caller: external (editor play button, presumably into `g_gameDLL`).

### `void unloadGameDLL(GameDLL *dll)`
No-op unless `dll->dll.loaded`; calls `dllUnload` (frees OS handle, deletes the `.running.dll` temp copy) and zeroes the whole `GameDLL`. Called by `loadGameDLL` on partial failure; otherwise external (stop-play / reload).

## Data Structures

- `PluginInitFn` / `PluginUpdateFn` / `PluginRenderFn` / `PluginDestroyFn` (project_builder.h 5–8) — function-pointer typedefs for the plugin callbacks: `init(const c8 *projectDir)`, `update(f32 dt)`, `render(f32 dt)`, `destroy(void)`. Mirrored verbatim inside the generated `src/game.h` template so game projects compile against the same ABI without including editor headers.
- `GamePlugin` (project_builder.h 10–16) — the four callbacks; filled by the DLL's `druidGetPlugin`. Lives inside `GameDLL`; no allocation of its own. Not ECS-managed.
- `GetPluginFn` (project_builder.h 19) — typedef for the `druidGetPlugin` export used by `dllSymbol` cast.
- `GameDLL` (project_builder.h 22–27) — `DLLHandle dll` (core-platform handle, owns the temp-copy path), `GamePlugin plugin`, `b8 loaded`. Storage is caller-owned (the module provides global `g_gameDLL`); initialized by `loadGameDLL`, released by `unloadGameDLL` (memset to zero). Plain static/stack struct; not ECS-managed.
- `g_gameDLL` (`GameDLL`, project_builder.cpp 7) — the editor's single loaded game DLL. Zero-initialized static global; never touched by name inside this .cpp apart from its definition — populated/consumed by external editor code.
- `g_gameRunning` (`b8`, line 8) — play-mode flag. Defined here, **never read or written anywhere in these files** — purely external editor state.
- `g_buildLog[4096]` (`c8`, line 9) — shared build-log buffer, presumably passed by external callers as the `outLog`/`logSize` arguments (this file never references it after definition).
- `g_buildInProgress` (`b8`, line 10) — set true/false around `buildProject`, `buildStandalone`, `updateProject`; read externally (e.g. to disable UI). Not thread-anything — plain global.
- Template string constants (`TPL_GAME_H`, `TPL_GAME_CPP`, `TPL_MAIN_CPP`, `TPL_SHADER_VERT`, `TPL_SHADER_FRAG`, `TPL_CMAKELISTS`) — static `const c8 *` literals; contents described under `generateProjectFiles`. Static storage, never freed.
- Local file tables — `exeFiles[]` in `buildStandalone` and `files[]` in `copyEngineFiles`: anonymous `{src, dst}` struct arrays on the stack listing relative copy pairs (contents enumerated above).

## Internal (static) functions

- `writeTemplate(dir, relPath, content)` — snprintf `dir/relPath` and `writeFile` the string.
- `stripLastComponent(path)` — truncate at the last `/` or `\` (used by `getEngineRoot`).
- `copyFileSingle(src, dst)` — thin wrapper over `platformFileCopy` (comment notes copy helpers "now delegate to platform.c").
- `readCMakeCacheGenerator(buildDir, outGen, outSize)` — parse `CMAKE_GENERATOR:INTERNAL=`/`:STRING=` out of `build/CMakeCache.txt`, trimming CR/LF.
- `buildConfigureCommand(projectDir, cmd, cmdSize)` — compose the `cd /d ... && cmake ..` configure command, adding `-G "<cached generator>"` when a cache exists.
- `getProjectBaseName(projectDir, out, size)` — last path component of the project dir (used for the standalone exe name).
- `getFileNameOnly(path, out, size)` — last path component of any path (used for the startup scene filename).
- `writeStartupSceneConfig(dir, scenePath)` — write `dir/startup_scene.txt` containing the scene filename (default `scene.drsc`).

## Open questions / unclear intent

1. **Who reads `startup_scene.txt`?** `writeStartupSceneConfig` writes only the scene filename into the project root and `export/`, but nothing in these files (or the generated `main.cpp`/`game.cpp` templates, which call `runtimeCreate` with no scene argument) consumes it. Presumably the engine runtime looks it up relative to the project dir, but that cannot be confirmed from this module.
2. **Platform scope is inconsistent.** All pipe commands use Windows `cmd` syntax (`cd /d`), `updateProject` hardcodes `C:\msys64\mingw64\bin\ninja.exe`, and the generated CMakeLists links `opengl32 gdi32 user32 winmm` in *both* the MSVC and non-MSVC branches — yet `buildStandalone` carries a `#else` (non-`_WIN32`) export table with `.so` names. Whether Linux support is intended, vestigial, or in-progress is unclear.
3. **`bin/libgame.dll` naming.** The export table and (implicitly) the play-in-editor path expect the shared lib at `bin/libgame.dll` / `bin/libgame.so`, which matches MinGW's `lib` prefix for the CMake target `game`; under MSVC the artifact would be `game.dll`. Since only MinGW artifact names appear in the copy tables, the actual supported toolchain matrix is unclear from these files.
4. **`buildStandalone` ignores the `game` target's build result** (exit code unchecked, pipe-open failure silently skipped) even though the comment says the standalone links against it, and the function returns true regardless of how many packaging copies succeeded (`copied` is log-only). Whether "best-effort packaging" is intentional is not determinable here.
5. **`copyEngineFiles` return semantics** — it returns true even if 0 of the 14 files (or the header tree) copied; failures surface only in the `INFO` count. `generateProjectFiles`/`updateProject` treat its return as a hard success signal, so the intended failure contract is unclear.
6. **`assimp-vc143-mt.dll`** is copied into `deps/bin/` but appears in neither the template's POST_BUILD copy list nor the `buildStandalone` export table (which use `libassimp-6.dll`) — its consumer is unknown from this module.
7. **`build_log.txt` location** — `updateProject` opens it with a relative path, so it lands in whatever the editor's current working directory is; whether that is meant to be the engine root, project dir, or editor bin dir is unclear.
8. **`g_gameRunning` and `g_buildLog`** are exported state with no uses inside this translation unit; their exact ownership protocol (who sets `g_gameRunning`, whether `g_buildLog` is always the buffer passed to the build functions) lives in external editor code not in scope here.
