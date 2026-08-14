# Critique: editor-project-builder

Files: `Editor/src/project_builder.cpp` (.cpp), `Editor/src/project_builder.h`.

## Dead / unreachable / redundant code

- **[moderate] [READ-VERIFIED]** `buildStandalone` .cpp:561-575: the extra `cmake --build . --target game` step is redundant — the CMake template links `game_standalone PRIVATE game` (.cpp:390-391), so CMake already builds `game` as a dependency of the first build step. On top of being redundant, its exit code and pipe-open failure are both ignored (flagged item confirmed: `platformPipeClose(pipe)` at :574 is unchecked, `if (pipe)` at :566 silently skips).
- **[trivial] [READ-VERIFIED]** `getProjectBaseName` (.cpp:102-120) and `getFileNameOnly` (.cpp:122-140) are byte-for-byte identical copy-paste duplicates.
- **[trivial] [READ-VERIFIED]** `copyFileSingle` (.cpp:29-32) is a one-line pass-through to `platformFileCopy`; callers could call it directly.
- **[moderate] [READ-VERIFIED]** The non-`_WIN32` branches (.cpp:596-600, 613-617) are unreachable in practice: every command uses `cmd.exe` syntax (`cd /d`, .cpp:90/473/537/562/748/752), so no non-Windows path through this module can work. Vestigial cross-platform theater.

## AI-slop patterns

- **[moderate] [READ-VERIFIED]** The configure/pipe-open/fgets-loop/close block is copy-pasted four times (`buildProject` :450-497, `buildStandalone` :514-575, `updateProject` :756-772) with identical bodies. One `runCommandCaptured(cmd, outLog, logSize, &off)` helper would eliminate ~90 lines and the inconsistent error handling among the copies.
- **[trivial] [READ-VERIFIED]** Inconsistent defensiveness: `projectDir` is NULL-checked everywhere, but `outLog` is dereferenced unchecked (`outLog[0]='\0'`, .cpp:443/507/731); `logSize - 1` (.cpp:466 etc.) underflows if `logSize==0` (u32).

## Memory allocation

None (all stack/static; no heap in this module).

## Perf

None material — build orchestration, not hot path.

## API ergonomics

- **[structural] [READ-VERIFIED]** Silent-success chain: `getEngineRoot` (.cpp:653-663) blindly assumes `<root>/Editor/bin/<exe>` (confirmed: three unconditional `stripLastComponent` calls, no validation that the result contains `include/` or `bin/libdruid.dll`). If wrong, `copyEngineFiles` (.cpp:665-724) copies 0/14 files yet **returns true unconditionally** (:722-723, only an INFO count), so `generateProjectFiles` (:425) and `updateProject` (:795) report success on a broken project. Worst silent-failure path in the module.
- **[moderate] [NEEDS-BUILD]** MinGW-only artifact names confirmed: export table expects `bin/libgame.dll` (.cpp:591) and the copy tables ship `.dll.a` import libs, but the CMake template has an MSVC branch (:373-379) whose `game` target would produce `game.dll` — MSVC-built projects would package/play with a missing game DLL, still returning true.
- **[moderate] [READ-VERIFIED]** Hardcoded `C:\msys64\mingw64\bin\ninja.exe` (`updateProject` .cpp:748) — confirmed; fails on any machine without that exact MSYS2 install, with no PATH fallback.
- **[moderate] [READ-VERIFIED]** `g_gameRunning`, `g_gameDLL`, `g_buildLog` (.h:52-54) are defined here but never touched in this TU — play-mode state actually managed by `editor.cpp`/`main.cpp` (verified by grep). Misplaced ownership; the header advertises globals whose protocol lives elsewhere.
- **[trivial] [READ-VERIFIED]** `updateProject` writes `build_log.txt` via relative `fopen` (.cpp:775) — lands in whatever the editor's CWD is; the outLog message (:789) tells users to check a file at an unknowable location.

## Missing error handling / silent failures

- **[moderate] [READ-VERIFIED]** `buildStandalone` returns true regardless of packaging results (:648): exe-copy failure (:619), all DLL copies (:628), and both `platformDirCopyRecursive` calls (:638/643) only affect the logged `copied` count.
- **[moderate] [READ-VERIFIED]** Log truncation drops the wrong end: `if (off + len < logSize - 1)` keeps the *earliest* 4 KB, but build errors appear *last* in cmake output — on a long failing build, `g_buildLog[4096]` shows only successful early lines, no truncation marker. (`NEEDS-RUNTIME` to see it in the UI, but the logic is read-verified.)
- **[trivial] [READ-VERIFIED]** `writeStartupSceneConfig` (.cpp:157) ignores `writeFile`'s return; a failed write silently falls back to the engine's scene-scan. (Flagged consumer question resolved: `loadStartupSceneData` in `src/systems/ecs/scene.c:120-125` does read it — not a bug.)
