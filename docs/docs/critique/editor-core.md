# editor-core — critique

## Dead / unreachable / redundant code
- [trivial][READ-VERIFIED] `MyMalloc`/`MyFree` (main.cpp:35-42): static, never installed via `ImGui::SetAllocatorFunctions`, never called. Dead.
- [trivial][READ-VERIFIED] `if (fileExists(projRes) || true)` (main.cpp:644): condition always true; `fileExists` call is dead.
- [trivial][READ-VERIFIED] `u32 outArenas = 0` (main.cpp:398): never used.
- [trivial][READ-VERIFIED] `u32 count = 0` (entitypicker.cpp:16): non-static file-scope global, unused in this file set; the name `count` is a link-collision hazard.
- [trivial][NEEDS-BUILD] `AxisType` (entitypicker.h:30), `resizeIDFramebuffer` no-op (entitypicker.cpp:284), `inputBoxBuffer` (main.cpp:27), `Material *materials` (main.cpp:392) — unused here; possible editor.cpp consumers.
- [moderate][READ-VERIFIED] renderIDPass (entitypicker.cpp:64-94): `model = &resources->modelBuffer[index]` can never be null, so the `else ERROR("Model index %d out of bounds")` branch is unreachable — while the *actual* out-of-bounds case (`index >= modelUsed`, line 62) silently skips with no log. The error message is attached to the wrong branch.

## AI-slop patterns
- [moderate][READ-VERIFIED] Ctrl+S block (main.cpp:828-847) is a copy-paste of `saveCurrentScene()` (main.cpp:954-968) with one behavioral difference: it lacks the empty-`scenePathBuffer` guard, so Ctrl+S before any scene exists calls `saveScene("")`. Should just call `saveCurrentScene()`.
- [trivial][READ-VERIFIED] `rotateCamera` (main.cpp:84-119): four arrow-key blocks each duplicate the same 5-line quat rebuild; the KEY_LEFT/KEY_RIGHT blocks clamp `currentPitch` despite only changing yaw.
- [trivial][READ-VERIFIED] `PickResult` (entitypicker.h:37-42): single-member union — pointless genericization.

## Memory / ownership
- [structural][READ-VERIFIED] `reallocateSceneArena()` is empty (main.cpp:191-193) **and** `update()` (main.cpp:682-689) still syncs `entitySizeCache = entitySize` afterward. So a UI capacity edit changes the *cache* but not the arena. Paste bounds-check (`entityCount >= entitySizeCache`, main.cpp:902), pick guard (`id <= entitySizeCache`, main.cpp:764) and gizmo guard (main.cpp:700) all trust the cache — after raising `entitySize` in the UI, paste can write past the real arena. Worse than a stub: it invalidates every capacity check. Also `entitySize` is `i32` vs `u32` cache — a negative UI value wraps.
- [moderate][READ-VERIFIED] `migrateSceneArchetypeIfNeeded` (main.cpp:288-308) manually `dfree`s loadScene's layout internals with hand-computed `strlen+1` sizes — the editor hardcodes another module's allocation scheme; any change in scene loading silently corrupts the tag accounting. It also mixes raw `malloc/free` (main.cpp:277, 351) with the engine's `dmalloc/dfree` used everywhere else, and never checks `malloc` before `memcpy` (277-278).
- [moderate][READ-VERIFIED] Migration temp arrays capped at 32 (`fieldCopies[32]`, main.cpp:266-268) while `SceneEntity` has 35 fields (rebind indices 0-34). A scene saved with 33-34 fields that later needs migration silently drops fields ≥32 — which are exactly `colliderOffsetX/Y/Z`. Data loss with no warning.
- [trivial][NEEDS-BUILD] `destroy()` never frees `deferredLightingShader` (loaded main.cpp:485; absent from 1057-1063) — shutdown leak unless editor.cpp frees it.
- [trivial][READ-VERIFIED] `destroy()` (main.cpp:1048-1055) nulls only 8 of the ~35 column globals — the other 27 dangle after `destroyArchetype`.

## Perf
- [moderate][READ-VERIFIED] `drawMeshIDPass` (entitypicker.cpp:263-265) calls `glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING)` per mesh per frame in the ID pass — a driver round-trip to learn something the `Mesh` already knows. Cache an `hasEBO` flag.
- [moderate][NEEDS-RUNTIME] `renderIDPass` re-renders the whole scene into the ID FBO every frame, but `glReadPixels` only happens on click (`getEntityAtMouse`). Rendering on-demand (on click) would delete the pass from steady-state frames.

## API ergonomics
- [moderate][READ-VERIFIED] `rebindArchetypeFields` (main.cpp:205-212) dereferences `fields[0..7]` unconditionally; a degenerate scene layout with <8 fields reads past the fields array. The ≥8 nulling discipline stops exactly where it's needed most. And on `getArchetypeFields` failure it `FATAL`s then `return`s, leaving all 35 globals stale rather than nulled.
- [moderate][READ-VERIFIED] 16-bit pick-ID ceiling: `objectID = (i+1) | ID_TYPE_ENTITY` (entitypicker.cpp:38) with `ID_TYPE_ENTITY == 0` — entity 0xFFFF+ would decode as gizmo/skybox. No guard in `renderIDPass`; invariant is enforced nowhere (capacity is nominally user-editable). Low practical risk at cap 128, but one `entityCount <= ID_MASK_ENTITY` assert would make it explicit.
- [trivial][READ-VERIFIED] `hub.h:6` re-declares `hubProjectDir` as `c8[512]` vs `c8[MAX_PATH_LENGTH]` in hub.cpp:8 — equal today (512), silent UB the day the constant changes.
- [trivial][READ-VERIFIED] `HUB_CONFIG = "../projects.conf"` (hub.cpp:12) is CWD-relative despite the "next to the editor binary" comment; the ImGui ini correctly uses `SDL_GetBasePath` (main.cpp:422) — inconsistent.

## Missing error handling / silent failures
- [moderate][READ-VERIFIED] `initIDFramebuffer` (entitypicker.cpp:9-14) never checks `createGraphicsProgram`'s result (unlike `fboShader`/`deferredLightingShader` in init). On failure, `renderIDPass` runs with program 0 and picking silently returns garbage.
- [trivial][READ-VERIFIED] Clipboard copy overflow path (main.cpp:883) resets `off = 0` and leaves `clipboardValid = false` with no log — Ctrl+C just silently does nothing.
- [moderate][NEEDS-BUILD] `init()` early-returns after `FATAL` on archetype-create failure (main.cpp:399-403) with ImGui uninitialized; `render()` will then call `ImGui::NewFrame()` on a dead context unless `FATAL` aborts the process.
