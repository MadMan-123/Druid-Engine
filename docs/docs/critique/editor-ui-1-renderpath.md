# Critique: editor-ui-1-renderpath (Editor/src/editor.cpp slice + editor.h)

## Dead / unreachable / redundant code

- **[structural] [READ-VERIFIED] Unreachable 67-line collider branch.** `renderGameScene` 2002: `else if (showColliders && !g_gameRunning)` sits *inside* the play-mode block opened at 1852 (`if (g_gameRunning && g_gameDLL.loaded)`), so `!g_gameRunning` is always false. Lines 2002–2069 never execute — which also makes `ColliderFieldCache`/`g_colliderCache` (353–354) fully dead: its only read is 2011, its only other touch the memset at 7836. The redundant `g_gameRunning` re-check at 1992 and `if (g_gameDLL.loaded)` at 1962 are the same confusion in miniature.
- **[moderate] [READ-VERIFIED] Phantom API.** `resetRuntimeFreeViewState()`, `snapRuntimeFreeViewToPlayerEntity()`, `g_runtimeFreeView` (editor.h 88, 141, 145) are defined nowhere in the repo (whole-tree grep) and called nowhere — dead declarations that would be link errors if ever used.
- **[moderate] [READ-VERIFIED] 4 of 5 viewport FBOs never rendered to.** `activeFBO` is set to 0 (6339) and never changed; `ID_FBO_INDEX` (editor.h 21) never referenced — picking uses entitypicker's own FBO (`resizeIDFramebuffer`, 2363). Yet `resizeViewportFramebuffers` (1750–1760) creates/resizes all 5 RGBA8+depth FBOs every viewport resize: ~50 MB VRAM at 1080p for nothing.
- **[trivial] [READ-VERIFIED]** Dead check at 2319: edit-path gizmo flush tests `g_gameRunning` after the play branch already `return`ed (2084). Also `reloadSkyboxFromFaces` 220–222 copies `facePaths` into an unused-until-248 `faces[]`; `reloadSkyboxFromFolder` 290 re-declares the suffix list instead of using `g_skyboxFaceSuffixes` (194).

## Play-lifecycle bugs (doBuildAndRun / restoreSnapshot)

- **[structural] [READ-VERIFIED] Restart violates the module's own DLL-lifetime invariant.** `doBuildAndRun` 7704–7709 unloads the old DLL without calling `cleanupEcsArchetypes` — but `doStopGame` 7841–7842 documents that archetype destruction "must happen while DLL is still loaded since the layouts belong to the DLL". `populateEcsArchetypes` 1177 later calls `destroyArchetype` on archetypes whose `layout` dangles into the unloaded DLL (crash [NEEDS-RUNTIME]). The restart also skips per-system `destroy()`, `physShutdown` (then `physInit` at 7791 double-inits [NEEDS-BUILD]), and `cleanupScenePhysicsArchetype`.
- **[structural] [READ-VERIFIED] Restart destroys the pre-play scene.** 7753 `snapshotScene()` runs on the mid-play (mutated) scene without a prior `restoreSnapshot`, overwriting the only backup; on the eventual stop the user gets play-mutated state, not their edits.
- **[structural] [READ-VERIFIED] Failed restart strands the editor.** If `buildProject` (7716) or DLL load (7749) fails after the unload block, `g_gameRunning` is false so `doStopGame` can never run: scene stays mid-play, snapshot unrestored, `physicsWorld` alive, `g_ecsSystemLoaded[]` true with function pointers into the unloaded DLL.
- **[moderate] [READ-VERIFIED] Restore failure deletes the backup.** `restoreSnapshot` 7698 `remove(fullPath)` runs even on the `loadScene`-failed path (7693–7696) — the one file that could recover the scene is deleted.
- **[moderate] [READ-VERIFIED] Unconditional per-cycle leak.** 7644–7651 nulls `arena[ci].fields/data` before `destroyArchetype` on *every* stop, leaking the editor's own original arena even when the DLL never reallocated it ("intentional leak" comment). Grows with scene size × play cycles.

## Editor-vs-runtime render divergence (known structural)

- **[structural] [READ-VERIFIED]** Both scene loops (play 1876–1945, edit 2097–2158) are immediate-mode: per-entity `updateShaderModel`, per-shader-switch `glUseProgram` + `getMaterialUniforms` (live uniform-location queries per frame), no frustum culling, no instancing. Consequences: editor perf is O(draw calls) and unrepresentative of the instanced runtime path; culling/instancing/SSBO bugs are invisible until play; shaders get exercised under two different draw paths so "works in editor" proves little. The two loops are near-duplicates with divergent edge handling: edit logs `ERROR` on impossible-null model (2121–2125) and drops entities with `modelID >= modelUsed` without the camera-marker fallback that play mode gives them (1901 vs 2109/2118).

## AI-slop / defensive patterns

- **[trivial] [READ-VERIFIED]** `if (!model) continue;` after `&resources->modelBuffer[modelID]` (1912, 2121) — address-of can't be null. Same for `!mesh` (1932, 2144).
- **[trivial] [READ-VERIFIED]** `mergeShaderSourceTable` (915–938) is a copy-paste of `buildShaderSourceTable`'s loop (897–911) plus a dup check; build could reset count and call merge. Magic `128` hardcoded three ways (856, 897, 919); overflow past 128 shaders is silent.
- **[trivial] [READ-VERIFIED]** Skybox draw block (bind VAO, LEQUAL, mask off, 36 verts) copy-pasted four times: 1862–1873, 1980–1988, 2172–2180, 2185–2193.
- **[trivial] [READ-VERIFIED]** `width <= 0` on unsigned `u32` (1741) is just `== 0`; callers pass casted `i32`s (2362).

## Perf / correctness details

- **[moderate] [READ-VERIFIED, cost NEEDS-RUNTIME]** `checkForResourceChanges` (1014–1026) calls full `readResources()` + table merge every 2 s unconditionally — directory scan/asset ingest on the render thread even when nothing changed; mtime check happens only for shaders, after the re-read.
- **[moderate] [READ-VERIFIED]** A shader whose compile fails retries (and `ERROR`s) every 2 s forever — `recompileChangedShaders` 951/982 skip mtime update on failure, so the "changed" condition never clears. Deleted sources (mtime 0, `getFileMtime` 869) hit the same loop.
- **[moderate] [READ-VERIFIED]** Redundant clear: 1848–1849 clears `viewportFBs[activeFBO]` (with leftover debug blue, comment says "to test scene rendering"), then the deferred branches rebind and clear it again (1975–1976, 2167–2168). The debug-labeled post-process block in `drawViewportWindow` (2404–2407) has mis-indented "Debug" scaffolding shipped as the real pipeline.
- **[trivial] [READ-VERIFIED, semantics NEEDS-RUNTIME]** `dt = 1/editor->fps` (1823) instead of a measured delta — fed to `plugin.render(dt)`.
- **[trivial] [READ-VERIFIED]** `uploadSceneLights` (1787) silently truncates at `MAX_LIGHTS` and reads chunk 0 only; the per-field `?:` null fallbacks (1795–1805) guard pointers that `rebindArchetypeFields` binds all-or-nothing.

## API ergonomics

- **[moderate] [READ-VERIFIED]** editor.h exports ~60 mutable globals (all SoA field pointers, FBO state, camera) to every includer with no accessor layer; `ID_FBO_INDEX` documents a contract the code doesn't honor; `renderFBOToScreen` (6368) binds source texture + shader but requires the caller to have bound the target FBO — asymmetry you can only learn from the one call site (2410–2414). Guard failures at 6370 return silently.
