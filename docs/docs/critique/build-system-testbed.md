# Critique: build-system-testbed

## Dead / unreachable / redundant code

- **[trivial] [READ-VERIFIED]** `druid_msvc.def` is a 2-line stub (`LIBRARY libdruid` + empty `EXPORTS`), referenced by neither `generate_msvc_import_lib.cmake` nor `generate_msvc_lib.bat` (both gendef their own `.def`). Dead file; linking with it would export nothing.
- **[trivial] [READ-VERIFIED]** `testbed/CMakeLists.txt:26-29` — `link_directories()` called *after* `add_executable`, so it never applies to `testbed`; also unused since every lib is an absolute path. Dead code.
- **[trivial] [READ-VERIFIED]** `generate_msvc_import_lib.cmake:8-9` — `DEF_PATH`/`LIB_PATH` set, never used. Line 21: on gendef failure warns "Trying alternative method..." but no alternative exists; script proceeds and prints a manual `lib.exe` command regardless of failure.
- **[trivial] [READ-VERIFIED]** Root `CMakeLists.txt:20-21` — duplicated `# Source files` comment.
- **[trivial] [NEEDS-BUILD]** `Editor/CMakeLists.txt:23` — `"src/entitypicker.cpp"` listed explicitly in `add_executable` although the line-9 glob already matches it (CMake normally dedups, but it's redundant either way).
- **[moderate] [NEEDS-BUILD]** Root `CMakeLists.txt:22-26` — under `GLOB_RECURSE`, `src/*.c` already recurses, so `src/**/*.c` is redundant/duplicating; conversely there is no top-level `src/*.cpp` pattern, and whether `src/**/*.cpp` catches root-level `.cpp` files depends on CMake's `**` handling (not a special token). Fragile glob set.

## AI-slop patterns

- **[moderate] [READ-VERIFIED]** Root `CMakeLists.txt:45-47` — comment claims "-ffast-math ... removed due to aggressive optimization issues"; line 55 passes `-ffast-math` anyway. Same file, contradictory. Editor (line 57) repeats the flag with a "conservative" comment.
- **[moderate] [READ-VERIFIED]** Fake portability: `$<$<NOT:$<PLATFORM_ID:Windows>>:-fPIC>` (root:50, Editor:53) gestures at non-Windows support while the same target hardcodes `WIN32` (root:43), `opengl32 gdi32 user32`, and Windows-only `.dll.a` absolute paths. The generator expression is defensive decoration for a platform that can't build.
- **[moderate] [READ-VERIFIED]** Two overlapping import-lib pipelines: `generate_msvc_import_lib.cmake` (half-manual, gendef only) fully subsumed by `generate_msvc_lib.bat` (automated, also does glew32). No indication which is canonical; the `.cmake` one should be deleted.

## Memory allocation issues

- **[trivial] [READ-VERIFIED]** `testbed/src/main.cpp:85` — `destroy()` empty; the `createEntityArena` allocation (line 38) is never released. Harmless at process exit, but the sample teaches "never free."

## Perf

- **[structural] [READ-VERIFIED]** Root:53-54 / Editor:56 — `-march=native -mavx2` baked into a *distributed DLL*: binaries crash with illegal-instruction on older CPUs, and MSVC consumers (the whole point of the `.lib` pipeline) receive a host-tuned DLL. Also all GCC-style flags are unconditional, so configuring root/Editor with MSVC fails outright.
- **[trivial] [READ-VERIFIED]** Testbed gets no optimization flags or `DRUID_PROFILE`, unlike engine/editor — inconsistent, and any layout-affecting define mismatch across the DLL boundary would be silent ABI breakage [NEEDS-BUILD for the latter].

## API ergonomics

- **[structural] [READ-VERIFIED]** `testbed/CMakeLists.txt:32-41` — MSVC branch links `../bin/libdruid.dll` directly; MSVC `link.exe` does not accept raw DLLs [NEEDS-BUILD to demonstrate]. The branch never references the `druid.lib` the scripts generate, so the MSVC path is dead-on-arrival despite two scripts existing to enable it.
- **[structural] [READ-VERIFIED]** Testbed is orphaned: root `CMakeLists.txt:75` adds only `Editor`; testbed is a separate project linking the prebuilt artifact with `../` paths. Nothing enforces or documents build ordering; a fresh clone's testbed configure "succeeds" then fails at link because `bin/libdruid.dll` doesn't exist yet.
- **[moderate] [READ-VERIFIED]** `main.cpp:42-46` — field pointers taken as `(b8*)&StaticEntites.fields[0]` (address-of the fields slot). If `fields[i]` is itself a pointer, correct usage is `(b8*)StaticEntites.fields[0]`; as written it would index over the pointer array's own bytes [NEEDS-BUILD — macro lives outside this module's file list]. Either way, the canonical sample requiring casts against archetype internals is error-prone API.
- **[moderate] [READ-VERIFIED]** `generate_msvc_lib.bat:14` hardcodes `C:\msys64\mingw64\bin\gendef.exe`; VS search (line 21) is 2022-only.

## Missing error handling / silent failure

- **[moderate] [READ-VERIFIED]** `main.cpp:35-37, 89-90` — `findInMap` return ignored (miss leaves `index = 0`, silently using `shaderHandles[0]`); `createApplication` NULL and shader-not-found checked only via `assert`, which vanishes under `NDEBUG` → NULL deref / rendering with handle 0.
- **[moderate] [NEEDS-RUNTIME]** Runtime DLL gaps: Editor post-build (Editor:35-42) copies glew32/SDL3 but not `libdruid.dll` or `libassimp-6.dll` into `Editor/bin`; testbed (testbed:55-65) omits `libassimp-6.dll` even though `generate_msvc_lib.bat:86` lists it as required. Likely startup failure unless PATH rescues it.
- **[trivial] [READ-VERIFIED]** `generate_msvc_lib.bat:53-70` — `lib.exe` exit codes never checked; prints "Created: druid.lib" unconditionally.

## Reference-doc flag verification

All five flags from `docs/modules/build-system-testbed.md` confirmed: testbed orphaned (structural), MSVC raw-.dll link (structural), empty `.def` stub (trivial), duplicate import-lib pipelines (moderate), `-march=native` portability (structural).
