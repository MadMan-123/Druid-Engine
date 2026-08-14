# core-platform — critique

## Dead / unreachable / redundant code

- [structural][READ-VERIFIED] `file.c:261` — `#elif PLATFORM_LINUX || PLATFORM_MAC` branch of `listFilesRecursive` is empty, and `PLATFORM_MAC` is never defined anywhere (`druid.h:83` defines `PLATFORM_APPLE`). On any non-Windows build, `listFilesInDirectory` (file.c:175) calls a function with no definition → link failure. Doc's flag confirmed; upgraded to structural (build-breaking), [NEEDS-BUILD] only for the exact linker error.
- [moderate][NEEDS-BUILD] `file.c:204 loadFileText` — defined and called by `src/systems/Rendering/shader.c:197-199`, but declared nowhere in `druid.h` → implicit declaration (error in C99+/C23 modes).
- [trivial][READ-VERIFIED] `logging.c:18-27` — `initLogging`/`shutdownLogging` are no-ops; `druid.h:150-158` `LOG_*_ENABLED` flags are tested by nothing (the macros at 179-184 ignore them), and RELEASE_BUILD redefines `LOG_DEBUG_ENABLED` without `#undef` (redefinition diagnostic).
- [trivial][READ-VERIFIED] `dllloader.c:38` NULL-checks duplicate the identical checks inside `platformLibrarySymbol` (platform.c:58).

## Bugs / correctness

- [structural][READ-VERIFIED] `file.c:191 normalizePath` — collapse test reads `dst[-2]` instead of `dst[-1]`. Two consequences: (a) doubled slashes are **not** collapsed (`a//b` survives; only the 3rd+ slash of a run is dropped); (b) for input starting `"//"`, at the second char `dst == path+1` passes the `dst > path` guard and `dst[-2]` reads `path[-1]` — out-of-bounds read/UB. Doc's flag confirmed; not deliberate UNC handling, just off-by-one.
- [structural][READ-VERIFIED] `file.c:20/78` — `loadFile` uses `fopen "r"`, `writeFile` uses `"w"`: on Windows, CRLF translation and Ctrl-Z-as-EOF corrupt/truncate any binary payload; `fseek(SEEK_END)+ftell` on a text stream is also formally UB. Contrast `platformFileCopy` (platform.c:77-79) which correctly uses `"rb"/"wb"`. Doc's flag confirmed.
- [moderate][READ-VERIFIED] `file.c:31` — `ftell` result unchecked; on failure (-1) `u64 length` becomes 2^64-1, `malloc(length+1)` wraps to `malloc(0)`, then `fread(buffer, 1, length, ...)` overruns the allocation.
- [moderate][READ-VERIFIED] `platform.c:83-84` — POSIX `platformFileCopy` ignores `fwrite`'s return; disk-full/short write still returns `true` (also flagged in ref doc).
- [moderate][READ-VERIFIED] `logging.c:44-46` — `useCustomOutputSrc` true with `logOutputSrc == NULL` calls through NULL; no check. Two globals that must be set in lockstep is also poor ergonomics (one setter fn would fix both).
- [moderate][READ-VERIFIED] `platform.c:198-201` — `PLATFORM_APPLE` branch of `platformGetExePath` returns `getcwd()`, which is not the exe path; silently wrong result rather than failure.
- [moderate][NEEDS-BUILD] `druid.h:2298-2300` — `PROFILE_SCOPE` requires `__attribute__((cleanup))`; with `DRUID_PROFILE` on MSVC it won't compile, despite `profileRDTSC_` explicitly supporting `_MSC_VER`.
- [trivial][READ-VERIFIED] `file.c:52` — `FileData` alloc failure handled by `assert` (vanishes under NDEBUG → NULL deref) while the buffer alloc 10 lines up gets a proper error path; inconsistent.

## Memory / allocation

- [moderate][READ-VERIFIED] `file.c:172,250` — `listFilesInDirectory`: initial `malloc` and growth `realloc` both unchecked; `*fileList = realloc(*fileList,...)` loses the old block on failure. Per-file `_strdup` churn plus no `freeFileList` helper anywhere — caller must know to free N strings + the array.
- [trivial][READ-VERIFIED] `profiler.c:116,140` — GL query objects created lazily, never `glDeleteQueries`'d (no profiler shutdown).
- [trivial][READ-VERIFIED] `dllloader.c:21-26` — if `platformLibraryLoad` fails, the freshly copied `.running.dll` temp file is left on disk.

## Silent failures

- [moderate][READ-VERIFIED] `platform.c:102-155 platformDirCopyRecursive` — returns `void`; ignores `CreateDirectoryA`, `CopyFileA`, `mkdir` results and unopenable `src`. A failed tree copy is indistinguishable from success.
- [moderate][READ-VERIFIED] `file.c:224-226` — `listFilesInDirectory` on a nonexistent directory returns a valid empty array; no way to distinguish "empty dir" from "bad path".
- [trivial][READ-VERIFIED] `profiler.c:179` — entries past `PROFILE_MAX_ENTRIES` silently dropped, no overflow counter.

## Inconsistency / style

- [trivial][READ-VERIFIED] `file.c:221-224` uses `MAX_PATH` (260) and TCHAR-generic `FindFirstFile`/`WIN32_FIND_DATA`, while platform.c uses `MAX_PATH_LENGTH` (512) and explicit `-A` variants — truncation mismatch and a break under `UNICODE` builds ([NEEDS-BUILD] for the latter).
- [trivial][READ-VERIFIED] `logging.c:6,14` — `colorCodes`/`colorReset` are non-static file globals with no header declaration (symbol leakage); `levelStrings` (line 31) re-initialized on every call.
- [trivial][READ-VERIFIED] `platform.c:13` — POSIX include guard omits `PLATFORM_POSIX` (druid.h:82), so a `_POSIX_VERSION`-only platform gets no `unistd.h` yet compiles the `#else` dlopen path.

## Perf

- [trivial][READ-VERIFIED] `logging.c:33-42` — double 1 KB stack buffer + second `snprintf` pass per log call; prefix could be written once into a single buffer.
- Otherwise none — this module is not hot-path; profiler counters are appropriately cheap.

## AI-slop patterns

- [trivial][READ-VERIFIED] `profiler.c:274-335 profileEstimateCache` — hardcodes 4-byte fields in the SoA path while ignoring `bytesPerEntity` there, an over-general signature whose params half-apply per mode; purely analytical "miss estimates" ignore associativity/reuse. Results also land in `g_profFrame` cache fields that are never reset, so stale estimates persist in published frames (ref-doc open question 7 confirmed as real staleness).
