# core-platform

Files: `src/core/platform.c`, `src/core/file.c`, `src/core/dllloader.c`, `src/core/logging.c`, `src/core/profiler.c`. All public declarations live in `include/druid.h` (no per-module headers; "exported" = declared there with `DAPI`).

## Synopsis

Lowest layer of the Druid Engine: OS abstraction (shared-library loading, file copy/delete, pipes, exe path) with `#if PLATFORM_WINDOWS` vs POSIX (`dlfcn`/`unistd`/`dirent`) dual code paths, buffered file IO (`FileData`), a hot-reload-friendly DLL loader built on the platform layer, printf-style leveled logging behind the `FATAL/ERROR/WARN/INFO/DEBUG/TRACE` macros, and an RDTSC + OpenGL-query profiler with per-frame counters. It depends on nothing else in the engine except `druid.h` (profiler additionally uses SDL timing and GL query functions); essentially every other module depends on it — at minimum via the logging macros, and the renderer/ECS via the `profileCount*`/`profileAdd*` hooks.

## Public API Reference

### Platform OS layer (`platform.c`)

Header comment: "Wraps Windows / Linux specific calls so the rest of the engine stays clean." Platform selection macros `PLATFORM_WINDOWS` / `PLATFORM_LINUX` / `PLATFORM_UNIX` / `PLATFORM_APPLE` are defined near the top of `druid.h` (lines 67–85).

- `void *platformLibraryLoad(const c8 *path)` — Load a shared library. Win32: `LoadLibraryA`; else `dlopen(path, RTLD_NOW)`. Returns opaque handle or NULL (logs `ERROR` with `GetLastError()`/`dlerror()` on failure). Called by `dllLoad` in dllloader.c.
- `void platformLibraryFree(void *handle)` — Unload a library (`FreeLibrary` / `dlclose`). NULL-safe. Called by `dllUnload`.
- `void *platformLibrarySymbol(void *handle, const c8 *name)` — Symbol lookup (`GetProcAddress` / `dlsym`). Returns NULL on NULL args. Called by `dllSymbol`.
- `b8 platformFileCopy(const c8 *src, const c8 *dst)` — Copy one file. Win32: `CopyFileA(..., FALSE)` (overwrites existing); POSIX: manual `fopen`/`fread`/`fwrite` loop with a 4096-byte stack buffer (fwrite result unchecked; returns true once the read loop ends). Called by `dllLoad` and by the POSIX branch of `platformDirCopyRecursive`.
- `b8 platformFileDelete(const c8 *path)` — Delete a file (`DeleteFileA` / `remove`). Called by `dllLoad` (pre-clean temp) and `dllUnload` (temp cleanup).
- `void platformDirCopyRecursive(const c8 *src, const c8 *dst)` — Recursively copy a directory tree, creating `dst` first (`CreateDirectoryA` / `mkdir 0755`). Win32 walks with `FindFirstFileA`/`FindNextFileA` and backslash separators; POSIX walks with `opendir`/`readdir`/`stat` and forward slashes. Skips `.`/`..`. No return value; silently returns if `src` can't be opened. No caller within these files — likely external (editor/build tooling).
- `void *platformPipeOpen(const c8 *command)` — `_popen(command, "r")` / `popen(command, "r")`; returns `FILE*` as `void*`. No caller in these files — likely external (compiler invocation / tooling).
- `i32 platformPipeClose(void *pipe)` — `_pclose` / `pclose`; returns exit status, or -1 for NULL pipe. Pairs with `platformPipeOpen`; no caller in these files.
- `void platformGetExePath(c8 *out, u32 size)` — Fill `out` with the running executable's path. Win32: `GetModuleFileNameA`; Linux: `readlink("/proc/self/exe")`; Apple and generic fallback: `getcwd()` (comment notes `_NSGetExecutablePath` "could be used"). Writes `""` on failure. No caller in these files — likely external (engine/editor startup).

### File IO (`file.c`)

- `FileData *loadFile(const c8 *filePath)` — Read an entire file into a heap buffer with an appended `'\0'`. Opens in **text mode** (`"r"`), sizes via `fseek`/`ftell`, allocates `length + 1`, stores actual `fread` count in `fileData->size` and copies the path into `fileData->path`. Returns malloc'd `FileData*` or NULL (logs `ERROR`); `assert`s the `FileData` allocation. Caller frees via `freeFileData`. Called by `loadFileText`; also expected external (asset/shader loading).
- `void freeFileData(FileData *fileData)` — `free(fileData->data)` then `free(fileData)`. NULL-safe. Called by `loadFileText`; otherwise external.
- `c8 *loadFileText(const c8 *fileName)` — Convenience: `loadFile`, detach `fd->data` (set to NULL so `freeFileData` doesn't double-free — noted in a comment), free the wrapper, return the NUL-terminated buffer. Caller owns and must `free()` it. Note: **not** in the druid.h grep results above; see Open questions.
- `b8 writeFile(const c8 *filePath, const u8 *data, u32 size)` — Write `size` bytes in text mode (`"w"`). Returns false and logs `ERROR` if open or a short write fails. No internal callers.
- `b8 fileExists(const c8 *filePath)` — Probe by `fopen(path, "r")`/`fclose`. No internal callers.
- `b8 dirExists(const c8 *path)` — Win32: `GetFileAttributesA` + `FILE_ATTRIBUTE_DIRECTORY`; POSIX: `stat` + `S_ISDIR`. Called by `createDir`.
- `b8 createDir(const c8 *path)` — mkdir -p: copies path to a local buffer, `normalizePath`s it, strips trailing slash, then creates each intermediate segment (`CreateDirectoryA`, tolerating `ERROR_ALREADY_EXISTS`; POSIX `mkdir(0755)` tolerating `EEXIST`). Returns false + `ERROR` log on first hard failure.
- `c8 **listFilesInDirectory(const c8 *directory, u32 *outCount)` — Allocates a growable (initial capacity 256, doubling `realloc`) array of `_strdup`'d full paths and fills it via `listFilesRecursive`. Caller owns the array and every string (no free helper exists in this module). Recursion is **Windows-only**; the `PLATFORM_LINUX || PLATFORM_MAC` branch is empty (see Open questions).
- `void normalizePath(c8 *path)` — In-place: converts `\` to `/` and collapses repeated slashes (the collapse test reads `dst[-2]`, i.e. two chars back, guarded by `dst > path`). Called by `createDir` and `listFilesRecursive`; also external.

### DLL loader (`dllloader.c`)

Header comment: unified API "for loading any Druid DLL (game plugins, ECS systems, etc.). Handles the temp-copy trick so the original file isn't locked." druid.h shows external consumers embedding `DLLHandle` (lines 881, 2591) and `loadECSSystemFromHandle(DLLHandle *dll, ...)` (line 895).

- `b8 dllLoad(const c8 *dllPath, DLLHandle *out)` — Zeroes `out`, builds temp path `"%s.running.dll"`, deletes any stale temp, `platformFileCopy`s the DLL there, `platformLibraryLoad`s the copy, then fills `out->handle`, `out->loadedPath`, `out->loaded = true`. Returns false + `ERROR` log on copy or load failure. Callers external (hot-reload/plugin systems).
- `void *dllSymbol(DLLHandle *handle, const c8 *name)` — NULL-checked wrapper over `platformLibrarySymbol(handle->handle, name)`.
- `void dllUnload(DLLHandle *handle)` — Frees the OS handle, deletes the `.running.dll` temp copy, clears `loadedPath` and `loaded`. Safe on partially initialized handles.

### Logging (`logging.c`)

- `b8 initLogging()` / `void shutdownLogging()` — Currently no-ops (`initLogging` has a `// TODO: Log file` and returns true). Declared in druid.h **without** `DAPI` (lines 172–173). No callers within these files — expected engine startup/shutdown.
- `void logOutput(LogLevel level, const c8 *message, ...)` — The single sink behind all logging macros. Formats varargs into a `LOG_BUFFER_SIZE` (1024) stack buffer, prefixes `[FATAL]`/`[ERROR]`/`[WARNING]`/`[INFO]`/`[DEBUG]`/`[TRACE]`, then either forwards to `logOutputSrc(level, outBuffer)` when `useCustomOutputSrc` is true, or prints ANSI-colored text — `stderr` for `level < LOG_WARNING` (FATAL/ERROR), `stdout` otherwise. Called throughout this module via the macros.
- Macros (druid.h 179–184): `FATAL/ERROR/WARN/INFO/DEBUG/TRACE(message, ...)` expand to `logOutput(LOG_x, message, ##__VA_ARGS__)`. druid.h also defines `LOG_*_ENABLED` flags (lines 150–158; DEBUG/TRACE disabled in the non-default branch), but the macro definitions found do not test them.
- Redirect hooks (exported globals, druid.h 175–176): `useCustomOutputSrc` (`b8`, default false) and `logOutputSrc` (function pointer, default NULL). Setting both routes all log output elsewhere (druid.h comment in logging.c mentions a future imgui console). Note: `logOutput` does not NULL-check `logOutputSrc` when `useCustomOutputSrc` is set.

Also relevant: `platform.c` does `#pragma push_macro("ERROR") / #undef ERROR` around `<windows.h>` and `file.c` defines `NOGDI`/`NOERROR` before `<windows.h>`, both to keep the `ERROR` logging macro usable.

### Profiler (`profiler.c`)

druid.h usage comment: `PROFILE_SCOPE("label")` for auto-timed scopes, `PROFILE_CYCLES_BEGIN/END` for manual ranges; "Enabled when DRUID_PROFILE is defined (-DDRUID_PROFILE in CMake)". Only the macros are compiled out without `DRUID_PROFILE`; the functions always exist.

Frame lifecycle:
- `void profileCalibrate(void)` — Calibrates RDTSC against `SDL_GetPerformanceCounter` over an `SDL_Delay(50)` window; stores `g_cyclesPerUs`; logs `INFO`. Must run before `frameTime_us`/scope microseconds are meaningful. Caller external (engine init).
- `void profileBeginFrame(void)` — Snapshots all per-frame counters (draw/geometry, state changes, uploads, heap) into `g_profFrame`, publishes it to `g_prevFrame` for readers, resets the counters (note: `g_heapLiveBytes` is *not* reset — it is a running total), records `g_frameStartCycles`, lazily `glGenQueries` two double-buffered query pairs, reads back last-completed `GL_TIME_ELAPSED` (→ `gpuFrameTime_us`) and `GL_PRIMITIVES_GENERATED` (→ `primitivesGenerated`) results non-blockingly (`GL_QUERY_RESULT_AVAILABLE`, only after both slots have run ≥2 frames), and begins new queries. Caller external (main loop, must be on the GL thread).
- `void profileEndFrame(void)` — Ends both GL queries, flips the double-buffer indices, computes `frameCycles`/`frameTime_us` from RDTSC. Caller external (main loop).
- `const ProfileFrame *profileGetCurrentFrame(void)` — Returns `&g_prevFrame`, i.e. the last *completed* frame (one frame of latency by design). Caller external (debug UI/overlay).

Scoped timing:
- `void profileRecordEntry(const c8 *name, u64 cycles, f64 elapsed_us)` — Appends to `g_profFrame.entries` (silently dropped past `PROFILE_MAX_ENTRIES` = 128). `name` is stored as a pointer — callers must pass string literals/stable strings. Called by `profileScopeEnd_`; also callable directly.
- `void profileScopeEnd_(ProfileScope_ *s)` — Cleanup callback for `PROFILE_SCOPE`; computes elapsed cycles/us and calls `profileRecordEntry`. The `PROFILE_SCOPE` macro (druid.h 2298–2300) uses `__attribute__((cleanup(...)))` — GCC/Clang extension.
- `profileRDTSC_()` — `static inline` in druid.h (2168–2179): `__rdtsc()` on MSVC, inline `rdtsc` asm on x86 GCC/Clang, `SDL_GetPerformanceCounter()` fallback.

Counter hooks — all trivial increments; **none are called anywhere in this module** (except `profileScopeEnd_`→`profileRecordEntry`); they are hooks that render/ECS/allocator code elsewhere is expected to call, per the druid.h comments quoted:
- `profileAddTriangles(u32)` / `profileAddVertices(u32)` — add to `g_triangles`/`g_vertices`; druid.h: "called from drawMesh, etc.". `g_drawCalls` has no hook function — it is an exported global that draw code increments directly (`DAPI extern u32 g_drawCalls/g_triangles/g_vertices`).
- `profileAddEntities(u32)` — adds directly to `g_profFrame.entityCount` (not snapshotted-then-reset like the others; reset in `profileBeginFrame` alongside `count`).
- `profileCountShaderBind/TextureBind/VAOBind/BufferBind/UniformUpload/FBOBind(void)` — one call = one counted GL state change; druid.h maps them to `glUseProgram`, `glBindTexture`, `glBindVertexArray`, `glBindBuffer` (VBO/EBO/SSBO), `glUniform*`, `glBindFramebuffer`; "call from wrappers or manually".
- `profileCountBufferUpload(u64 bytes)` — counts one `glBufferData`/`glBufferSubData` and accumulates bytes.
- `profileCountAlloc(u64 bytes)` / `profileCountFree(u64 bytes)` — heap tracking: alloc adds to per-frame bytes/count and running `g_heapLiveBytes`; free increments `g_heapFreeCount` and subtracts from live bytes, clamped at 0. Expected to be called from a tracked-allocation wrapper that is not in these files.

Cache analysis:
- `void profileDetectCaches(CacheInfo *out)` — Fills conservative defaults (32 KB L1d / 256 KB L2 / 8 MB L3 / 64 B line), then on `_WIN32` refines via `GetLogicalProcessorInformation` (`RelationCache`, levels 1–3, `CacheData`/`CacheUnified` for L1). No POSIX detection path. Caches result in `g_cacheInfo`, sets `g_cacheDetected`, logs `INFO`. Caller external (startup).
- `const CacheInfo *profileGetCacheInfo(void)` — Returns `&g_cacheInfo`; druid.h notes `profileDetectCaches` must be called first.
- `void profileEstimateCache(u32 entityCount, u32 bytesPerEntity, u32 usefulBytesPerEntity, u32 numArrays)` — Analytical model, no measurement: computes lines touched (AoS when `numArrays<=1`: stride through interleaved struct; SoA: `numArrays` contiguous arrays assuming 4-byte f32 fields), utilisation %, wasted bytes, and estimated L1/L2/L3 misses by comparing the working set against cache sizes; writes results into `g_profFrame.cache*`/`estL*` fields. No-op until `profileDetectCaches` has run. druid.h example params reference `SpaceBodyAoS` (external test code).

## Data Structures

- `FileData` (druid.h 384–388) — `path[MAX_PATH_LENGTH=512]` (copy of source path), `u8 *data` (heap buffer, NUL-terminated), `u32 size` (bytes actually read). Struct and buffer both malloc'd by `loadFile`, freed together by `freeFileData` (or buffer ownership detached by `loadFileText`). Plain heap; not ECS-managed.
- `DLLHandle` (druid.h 429–434) — `void *handle` (OS library handle), `loadedPath[512]` (path of the `.running.dll` temp copy, kept for cleanup), `b8 loaded`. Caller-owned storage (typically embedded in larger structs per druid.h 881/2591); `dllLoad` initializes, `dllUnload` releases OS handle + temp file. Plain struct; not ECS-managed.
- `LogLevel` enum (druid.h 161–170) — `LOG_FATAL=0, LOG_ERROR, LOG_WARNING, LOG_INFO, LOG_DEBUG, LOG_TRACE, LOG_MAX`. Ordering matters: `logOutput` routes `level < LOG_WARNING` to stderr; indexes `colorCodes`/`levelStrings`.
- `ProfileEntry` (druid.h 2181–2186) — `name` (borrowed pointer), `cycles`, `elapsed_us`. Lives inside `ProfileFrame.entries`; no per-entry allocation.
- `ProfileFrame` (druid.h 2188–2237) — entries array + count; CPU frame cycles/us; GPU frame us; `drawCalls/triangles/vertices/entityCount`; state-change counters (`shaderBinds`, `textureBinds`, `vaoBinds`, `bufferBinds`, `uniformUploads`, `fboBinds`); upload stats (`bufferUploadsCount`, `bufferUploadBytes`); heap stats (`heapAllocBytes/Count`, `heapFreeCount`, `heapLiveBytes`); `primitivesGenerated`; cache-estimate fields (`cacheWorkingSetBytes`, `cacheLinesAccessed`, `cacheUsefulBytes`, `cacheWastedBytes`, `cacheUtilisation`, `estL1/L2/L3Misses`). Two static instances in profiler.c: `g_profFrame` (in-progress) and `g_prevFrame` (published, read-only via `profileGetCurrentFrame`). Static storage; not ECS-managed; never freed.
- `CacheInfo` (druid.h 2242–2248) — `l1dSize`, `l2Size`, `l3Size`, `lineSize` in bytes. Filled by `profileDetectCaches` into caller storage and mirrored into static `g_cacheInfo`.
- `ProfileScope_` (druid.h 2265) — `{name, startCycles}`; stack-allocated by `PROFILE_SCOPE`, destroyed via cleanup attribute.
- Exported globals: `g_drawCalls`, `g_triangles`, `g_vertices` (`DAPI extern u32`, profiler.c) — incremented externally by draw code, snapshotted+reset each `profileBeginFrame`. `useCustomOutputSrc` (`b8`), `logOutputSrc` (fn ptr) in logging.c — set externally to redirect log output. `colorCodes[LOG_MAX]` / `colorReset` (logging.c) — ANSI color tables, file-scope but not `static` and not declared in druid.h.
- Static profiler state (profiler.c): state-change/heap counters (`g_shaderBinds` … `g_heapLiveBytes`), calibration (`g_cyclesPerUs`, `g_frameStartCycles`), GL query double-buffers (`g_gpuQueries[2]`, `g_gpuQueryIdx`, `g_gpuQueriesReady`, `g_gpuFrameCount`; same quartet for `g_primQueries`), cache (`g_cacheInfo`, `g_cacheDetected`). GL query objects are created lazily and never `glDeleteQueries`'d.
- Config macros: `MAX_PATH_LENGTH` 512, `LOG_BUFFER_SIZE` 1024, `PROFILE_MAX_ENTRIES` 128, `DEBUG_RESOURCES` 0 (gates the per-file `TRACE` in `listFilesRecursive`).

## Internal (static) functions

- `listFilesRecursive` (file.c) — recursive directory walk appending `_strdup`'d normalized paths to a growable array; Windows implementation only. (File-scope but not marked `static`; forward-declared at top of file.c, not in druid.h.)
- `profileRDTSC_` (druid.h, `static inline`) — raw timestamp counter read with MSVC/GNU-asm/SDL fallbacks.
- profiler.c has no static functions; its internals are the static globals listed above.

## Open questions / unclear intent

1. **POSIX `listFilesRecursive` is an empty branch** (`#elif PLATFORM_LINUX || PLATFORM_MAC` with no body, file.c 261–263), so `listFilesInDirectory` returns an empty list on non-Windows — and would fail to link there since the function has no definition at all in that branch. Also, the guard uses `PLATFORM_MAC`, which druid.h never defines (it defines `PLATFORM_APPLE`). Whether POSIX file listing is pending work or intentionally Windows-only is unclear from these files.
2. **`loadFileText` and `dirExists`/`createDir` export status**: `loadFileText` is defined in file.c but did not appear in the druid.h declarations found by search — unclear whether it is exported elsewhere in the header or is effectively internal. Conversely, `initLogging`/`shutdownLogging` are declared without `DAPI` while everything else here has it; intent (internal-only vs oversight) is unclear.
3. **Text-mode IO**: `loadFile` uses `fopen(..., "r")` and `writeFile` uses `"w"`, so on Windows CRLF translation applies and `fileData->size` can be smaller than the `ftell` length. Whether `FileData` is meant only for text (shaders/config) or also binary assets can't be determined from these files.
4. **Who calls `profileCountAlloc`/`profileCountFree`**: no tracked-allocator wrapper exists in these files (loadFile/listFilesInDirectory call raw `malloc` without counting), so the heap counters depend on external callers I cannot identify from this module.
5. **`normalizePath` collapse test** checks `dst[-2] == '/'` (two characters back) rather than the immediately previous output character; the practical effect is that a doubled slash keeps one pair (e.g. leading `//` or `\\` UNC-style prefixes survive). Whether that is deliberate UNC-path preservation or incidental is unclear.
6. **MSVC support for `PROFILE_SCOPE`**: the macro relies on `__attribute__((cleanup(...)))` while `profileRDTSC_` explicitly supports `_MSC_VER`; how scope profiling is meant to work under MSVC is unclear from these files.
7. **Cache-estimate publication timing**: `profileEstimateCache` writes into `g_profFrame`, but readers get `g_prevFrame` (snapshotted at `profileBeginFrame`), and the cache fields are never reset between frames — whether the one-frame lag / persistence of stale cache fields is intended is not determinable here.
