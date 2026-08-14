# Module: editor-ui-1 — Editor render path, play-in-editor lifecycle, shader hot-reload

**Scope note.** This documents one slice of `Editor/src/editor.cpp` (file is ~8,400 lines; split across three doc dispatches). This slice covers:

| Region | Lines (approx.) |
|---|---|
| Skybox reload helpers + FBO/skybox globals | 184–299 |
| Shader hot-reload tables + functions | 838–1026 |
| Scene-camera entity helpers, project skybox persistence | 1056–1170 |
| `resizeViewportFramebuffers`, `uploadSceneLights`, `renderGameScene` | 1739–2334 |
| `initMultiFBOs`, `destroyMultiFBOs`, `renderFBOToScreen` | 6321–6385 |
| Scene snapshot/restore + `doBuildAndRun` / `doStopGame` | 7604–7854 |

ImGui panel functions (`draw*Window`, `drawDockspaceAndPanels`) are documented by another dispatch; `drawViewportWindow` (2339–2434) is referenced here only as the call site that drives the render path. Shared globals declared in `Editor/src/editor.h` and defined in `Editor/src/main.cpp` (e.g. `shader`, `cubeMesh`, `sceneArchetype`, the SoA field pointers) are noted where used but owned elsewhere.

## Synopsis

This slice is the editor's per-frame viewport render pipeline and the play-in-editor lifecycle. Each frame `drawViewportWindow` sizes the FBO chain, syncs the editor camera into the engine `Renderer`, runs the entity-ID picking pass (external `entitypicker` module), then calls `renderGameScene`, which draws the scene through the engine's deferred pipeline (G-buffer → `rendererLightingPass` → skybox → gizmo overlay) into `viewportFBs[activeFBO]`, after which the result is post-processed via `fboShader` into `finalDisplayFB` and shown as an ImGui image. The slice also owns shader hot-reload (mtime polling of editor + project shader sources), skybox cubemap management, scene-camera entity sync, and the build → load DLL → snapshot → run → stop → restore play cycle (`doBuildAndRun` / `doStopGame`). It depends on the engine renderer (`renderer` global, `rendererBeginFrame`/`rendererBeginDeferredPass`/`rendererLightingPass`/`rendererUploadLights`), the gizmo primitives (`gizmoBeginFrame`/`gizmoDraw*`/`gizmoEndFrame`), the physics module (`physInit`/`physRegisterArchetype`/`physShutdown`), scene serialization (`saveScene`/`loadScene`), and the project builder (`buildProject`, `loadGameDLL`, `g_gameDLL`, `g_gameRunning` — see `editor-project-builder.md`).

### How the editor render path differs from the engine's runtime renderer

Per `docs/modules/renderer.md`, the engine's designed runtime path is `rendererBeginFrame` → `rendererDefaultArchetypeRender` per archetype (frustum culling, batching by ModelID, instance SSBO fill, instanced/indirect draws). The editor path in `renderGameScene`:

- **Edit mode draws SceneEntities immediately, one at a time** — a plain loop over the `SceneEntity` SoA arrays (`positions`/`rotations`/`scales`/`modelIDs`/`shaderHandles`/`entityMaterialIDs`), calling `glUseProgram` + `getMaterialUniforms` (cached only across consecutive same-shader entities), `updateShaderModel`, `updateMaterial`, `drawMesh` per mesh. No frustum culling, no instancing, no indirect buffer.
- **It still reuses the engine's deferred plumbing**: geometry is wrapped in `rendererBeginDeferredPass`/`rendererEndDeferredPass` against the renderer's active G-buffer, then `rendererLightingPass(renderer, deferredLightingShader)` shades into the editor's own `viewportFBs[activeFBO]` — the editor supplies its own lighting shader (`../res/deferred_lighting.*`) rather than an engine-owned one.
- **Render target is the editor FBO chain**, not the default framebuffer: G-buffer → viewport FBO → (`fboShader` fullscreen pass in `drawViewportWindow`) → `finalDisplayFB` → ImGui image.
- **The skybox is drawn by the editor itself** (raw GL: `skyboxShader`, `skyboxMesh->vao`, `cubeMapTexture`, LEQUAL depth func with depth writes off) after the lighting pass (deferred) or as forward fallback; the engine renderer only receives the cubemap via `renderer->envMapTex`.
- **Play mode is a hybrid**: SceneEntities *not* claimed by an ECS archetype are drawn with the same immediate loop; entities in loaded ECS archetypes go through the runtime path (`g_ecsSystems[a].render` or `rendererDefaultArchetypeRender`), plus `g_gameDLL.plugin.render(dt)` — all into the same G-buffer/FBO.
- **Lights come from scene data, not the game**: `uploadSceneLights` rebuilds the `GPULight` array from the SceneEntity light fields every frame the lighting pass runs.
- **No bloom pass exists in this file** despite the multi-FBO design; the only post-process is the single `fboShader` fullscreen quad (see Open questions).

### Exact play-in-editor sequence

`doBuildAndRun` (7702):
1. If already running: `g_gameDLL.plugin.destroy()`, `unloadGameDLL`, clear `g_gameRunning` (restart path — note: no snapshot restore here).
2. `showBuildLog = true`; `buildProject(hubProjectDir, g_buildLog, ...)` — abort on failure.
3. Load the game DLL: on Windows tries 6 candidate paths (`bin/libgame.dll`, `bin/game.dll`, `bin/{Debug,Release}/{game,libgame}.dll`) via `loadGameDLL(&g_gameDLL)`; on other platforms `bin/libgame.so`. Abort if none load.
4. `snapshotScene()` — serialize the current scene + materials to `<project>/__play_snapshot__.drsc`.
5. `applySceneCameraEntityToSceneCam()` — seed the camera from the designated scene-camera entity.
6. `g_gameDLL.plugin.init(hubProjectDir)`.
7. ECS discovery: for each registry archetype, `loadECSSystemFromHandle(&g_gameDLL.dll, name, &sys)` (looks for `druidGetECSSystem_<name>`); on success store in `g_ecsSystems[a]`, set `g_ecsSystemLoaded[a]`, call `sys.init()`.
8. `populateEcsArchetypes()` — build per-system `Archetype` instances containing only the scene entities whose `archetypeID` matches.
9. Physics: `physInit(runtimeDefaultConfig().gravity, ...physicsTimestep)`; `physRegisterArchetype` for every `ARCH_PHYSICS_BODY` archetype with an arena (tracked in `g_archPhysRegistered`).
10. `setupScenePhysicsArchetype()` + register `g_scenePhysArch` — static colliders for plain scene entities (colliderShape != 0, archetypeID == -1).
11. `g_gameRunning = true`; `showLoadedEntities = true`.

While running, `renderGameScene` takes the play-mode branch (editor camera sync is skipped — the game/ECS owns the renderer camera) and `drawDockspaceAndPanels` (~8133) keeps physics archetype registration in sync.

`doStopGame` (7819):
1. No-op unless `g_gameRunning`.
2. Call `destroy()` on every loaded ECS system; zero `g_ecsSystems`/`g_ecsSystemLoaded`/`g_ecsSystemCount`.
3. `physShutdown()`; clear `g_archPhysRegistered` and `g_colliderCache`.
4. `cleanupScenePhysicsArchetype()`; then `cleanupEcsArchetypes()` — explicitly **before** DLL unload because the archetype layouts point into DLL memory.
5. `g_gameDLL.plugin.destroy()`; `unloadGameDLL(&g_gameDLL)`; `g_gameRunning = false`.
6. `restoreSnapshot()` — reload `__play_snapshot__.drsc`, replace `sceneArchetype`, delete the snapshot file.

## Public API Reference

All declared in `Editor/src/editor.h` unless noted. "External" below means callers outside `editor.cpp` (e.g. `main.cpp`), which this dispatch did not read.

- **`void buildShaderSourceTable(const c8 *resDir)`** (887) — Rebuild the shader-tracking table from scratch. Baselines mtimes for all six built-in editor shaders (`g_editorShaders`), then walks `resources->shaderIDs` hashmap and records up to 128 project shaders whose `<resDir><name>.vert/.frag` both exist, with current mtimes. Params: `resDir` — project `res/` dir (trailing-slash concatenation). Returns void. Side effects: resets `g_trackedProjectShaderCount`, fills `g_trackedProjectShaders`. Callers: per header comment, `main.cpp` after initial `readResources()` (external); not called inside `editor.cpp`.
- **`void reloadProjectResources()`** (996) — Manual full reload: `readResources` from `<hubProjectDir>/res/`, `applyMaterialPresets`, `mergeShaderSourceTable`, zero all tracked mtimes, then `recompileChangedShaders()` (forcing every shader to recompile). No-op without an open project or `resources`. Callers: not in this slice's regions (Resource Manager UI / menu, other dispatch or external).
- **`void applySceneCameraEntityToSceneCam()`** (1078) — Copy `positions`/`rotations` of the first active entity with `sceneCameraFlags[i]` set into `sceneCam.pos`/`sceneCam.orientation`. No-op if none found. Callers in this slice: `restoreSnapshot`, `doBuildAndRun`; also declared for external use.
- **`void loadPreferredSkybox()`** (1149) — If `<project>/res/Textures/Skybox/` contains all six standard faces (`projectSkyboxExists`), load from there; otherwise fall back to the default `g_skyboxFaces` paths. Side effects: replaces `cubeMapTexture`, updates `renderer->envMapTex` (via `reloadSkyboxFromFaces`). Callers: external (project open, per header comment).
- **`void initMultiFBOs()`** (6321) — Create `screenQuadMesh` (via `createQuadMesh`) if absent; zero all `viewportFBs[MAX_FBOS]` and `finalDisplayFB`; reset `viewportWidth/Height = 0`, `activeFBO = 0`. FBOs themselves are created lazily on first resize. Callers: external (editor `init()` in main.cpp, presumably).
- **`void destroyMultiFBOs()`** (6343) — `destroyFramebuffer` + memset each non-zero `viewportFBs[i]` and `finalDisplayFB`; `freeMesh(screenQuadMesh)` and null it. Callers: external (editor `destroy()`).
- **`void renderFBOToScreen(u32 fboIndex, u32 shaderProgram)`** (6368) — Fullscreen-quad pass: bind `viewportFBs[fboIndex].texture` to unit 0, draw `screenQuadMesh` (6 verts, non-indexed) with `shaderProgram`, depth test disabled during the draw. Guards against invalid index / missing quad / FBO / shader. Note it does **not** bind a target FBO — caller binds first. Callers: `drawViewportWindow` (2414, with `activeFBO` + `fboShader`, into `finalDisplayFB`).
- **`void doBuildAndRun()`** (7702) — Full play-in-editor start; see sequence above. Side effects: writes `g_buildLog`, loads `g_gameDLL`, creates snapshot file, creates physics world, sets `g_gameRunning`. Callers: menu/toolbar handlers in `drawDockspaceAndPanels` (7990, 8061).
- **`void doStopGame()`** (7819) — Full play-in-editor stop + scene restore; see sequence above. Callers: `drawDockspaceAndPanels` (7994, 8067).

Declared in `editor.h` but **not defined in `editor.cpp`** (nor anywhere under `Editor/src` per grep): `resetRuntimeFreeViewState()`, `snapRuntimeFreeViewToPlayerEntity()`, and the `g_runtimeFreeView` flag — implemented in some file outside this slice's reading list.

## Data Structures

### FBO chain (editor.h 20–26, definitions editor.cpp 200–204)
- `Framebuffer viewportFBs[MAX_FBOS]` (`MAX_FBOS` = 5) — scene render targets, `GL_RGBA8` with depth (`createFramebuffer(..., true)`). Global statics; GPU objects created lazily in `resizeViewportFramebuffers`, resized there, freed in `destroyMultiFBOs`. Not ECS-managed. `ID_FBO_INDEX` (4) is documented in editor.h as reserved for ID picking, but `editor.cpp` never references it (picking uses the separate `entitypicker` module's ID framebuffer — see `resizeIDFramebuffer`/`renderIDPass` calls at 2363/2401).
- `Framebuffer finalDisplayFB` — post-processed result shown in the ImGui viewport; `GL_RGBA8`, no depth. Same lifetime as above.
- `u32 viewportWidth/viewportHeight` — current FBO dimensions (set only by `resizeViewportFramebuffers`).
- `u32 activeFBO` — index of the FBO being rendered to. Set to 0 in `initMultiFBOs` and **never changed anywhere in editor.cpp**; slots 1–3 are allocated but unused (see Open questions).
- `Mesh *screenQuadMesh` (207) — fullscreen quad, owned here (created `initMultiFBOs`, freed `destroyMultiFBOs`). Plain heap via engine mesh API.

### Skybox (editor.cpp 184–216, editor.h 32–36)
- `Mesh *skyboxMesh`, `u32 cubeMapTexture`, `u32 skyboxShader`, `u32 skyboxViewLoc/skyboxProjLoc` — globals; cubemap is replaced atomically by `reloadSkyboxFromFaces` (new texture created before `freeTexture` of the old), and mirrored into `renderer->envMapTex`. `skyboxMesh`/`skyboxShader` creation is outside this slice (external init).
- `static c8 g_skyboxFaces[6][512]` — current face paths, default `../res/Textures/Skybox/*.jpg`; updated on successful reload.
- `static const c8 *g_skyboxFaceSuffixes[6]` — canonical face filenames (`right.jpg` … `back.jpg`) used for project persistence.
- `static char g_skyboxError[256]` — last skybox load error, consumed by the skybox settings UI (other dispatch).

### Shader hot-reload (editor.cpp 838–867)
- `struct TrackedProjectShader { c8 name[128]; c8 vertPath[512]; c8 fragPath[512]; u64 vertMtime, fragMtime; b8 valid; }` — one per project shader; stored in `static TrackedProjectShader g_trackedProjectShaders[128]` + `g_trackedProjectShaderCount`. Static storage, rebuilt by `buildShaderSourceTable`, appended by `mergeShaderSourceTable`.
- `struct EditorTrackedShader { const c8 *vertPath, *fragPath; u32 *handlePtr; u64 vertMtime, fragMtime; }` — `static g_editorShaders[6]` binds the six built-in editor shader handle globals: `shader` (default lit), `arrowShader`, `skyboxShader`, `fboShader`, `deferredLightingShader`, `idShader` (the last declared in `entitypicker.h`). `handlePtr` indirection lets recompile swap the live handle in place.
- `static u64 g_lastResCheckMs` — throttle for `checkForResourceChanges` (2 s poll).
- Shader handle globals defined in this file (319–322): `arrowShader`, `colourLocation` (uniform loc for arrow color), `fboShader`, `deferredLightingShader`. `shader` itself is defined in `main.cpp`.

### Scene camera
- `Camera sceneCam` (306) — the editor free camera; global static. Projection fields (`fovDeg/aspect/nearClip/farClip/projection`) written every frame by `drawViewportWindow` from `static f32 g_editorFov/g_editorNear/g_editorFar` (180–182, Camera Settings panel). Copied into the renderer camera slot each edit-mode frame.
- `u32 g_editorCamSlot` (307, init `(u32)-1`) — renderer camera slot index holding the editor camera; acquisition site is outside the regions read (see Open questions).
- `b8 *sceneCameraFlags` (308) — SoA pointer into the SceneEntity archetype's `isSceneCamera` field (bound by `rebindArchetypeFields`, other slice). Not owned here; ECS(archetype-arena)-managed.

### Play-mode / ECS-bridge state (329–394)
- `static ECSSystemPlugin g_ecsSystems[MAX_ARCHETYPE_SYSTEMS]`, `g_ecsSystemLoaded[]`, `g_ecsSystemCount` — function tables discovered from the game DLL at play start; zeroed at stop. Function pointers dangle into the DLL, hence the strict teardown ordering in `doStopGame`.
- `static Archetype g_ecsArchetypes[MAX_ARCHETYPE_SYSTEMS]` + `g_ecsSceneMap[]` (malloc'd per-system index→scene-entity maps) + `g_ecsArchEntityCount[]` — per-system archetype instances built by `populateEcsArchetypes` at play start, destroyed by `cleanupEcsArchetypes` at stop (layouts belong to the DLL). Archetype arenas are engine-ECS managed; the maps are plain heap (`free`d in populate/cleanup).
- `static i32 g_ecsFieldMap[..][PREFAB_UI_MAX_FIELDS]`, `g_ecsUniformScaleIdx[]` — field index mappings (populated outside the read regions).
- `static b8 g_archPhysRegistered[]` — which archetypes are registered with the physics world (avoids re-registration each frame).
- `struct ColliderFieldCache { i32 posX..offsetZ; b8 resolved; }` / `static g_colliderCache[]` (353–354) — per-archetype cached field indices for collider gizmo drawing (resolved lazily in `renderGameScene`'s edit-mode collider loop; cleared in `doStopGame`).
- Scene-physics archetype (364–394): `static FieldInfo g_scenePhysFields[23]`, `static StructLayout g_scenePhysLayout` ("ScenePhysics"), `static Archetype g_scenePhysArch`, `static b8 g_scenePhysCreated` — static layout (safe to destroy after DLL unload, unlike the DLL-owned layouts); built/torn down by `setupScenePhysicsArchetype`/`cleanupScenePhysicsArchetype` around play.

### Snapshot state (7604–7606)
- `static const c8 *PLAY_SNAPSHOT_PATH = "__play_snapshot__.drsc"` — temp file created under `hubProjectDir`, deleted by `restoreSnapshot`.
- `static b8 g_hasPlaySnapshot` — set by `snapshotScene` iff `saveScene` succeeded; gates and is cleared by `restoreSnapshot`.

### Externally owned globals used by this slice
`renderer`, `resources`, `physicsWorld` (engine globals from `druid.h`); `g_gameDLL`, `g_gameRunning`, `g_buildLog`, `showBuildLog`, `hubProjectDir` (project-builder / hub modules); the SceneEntity SoA pointers (`isActive`, `positions`, `rotations`, `scales`, `modelIDs`, `shaderHandles`, `entityMaterialIDs`, `archetypeIDs`, `isLight`, `lightTypes`, `lightRanges`, `lightColor*`, `lightIntensities`, `lightDir*`, `light*Cones`, `colliderShapes`, `sphereRadii`, `colliderHalf*`, `colliderOffset*`) all point into `sceneArchetype`'s chunk-0 arena and are rebound by `rebindArchetypeFields` (other slice); `idShader`/`idLocation` from `entitypicker.h`.

## Internal (static) functions

- `reloadSkyboxFromFaces(const c8 *facePaths[6])` (218) — validate all six files exist, create new cubemap first, then free old; update `g_skyboxFaces`, `renderer->envMapTex`, `g_skyboxError`.
- `reloadSkyboxFromFolder(const c8 *folderPath)` (288) — build the six standard-suffix paths and delegate to `reloadSkyboxFromFaces`.
- `makeGizmoColor(r,g,b,a)` (277) — GizmoColor constructor helper.
- `getFileMtime(const c8 *path)` (869) — platform mtime (Win32 `GetFileAttributesExA` FILETIME as u64 / POSIX `stat.st_mtime`); 0 on failure.
- `mergeShaderSourceTable(const c8 *resDir)` (915) — append newly discovered project shaders to the tracking table without resetting existing mtimes.
- `recompileChangedShaders()` (941) — for each tracked editor/project shader whose vert or frag mtime changed: `createGraphicsProgram`, patch every scene entity's `shaderHandles[]` entry from old to new program, `freeShader(old)`, swap handle, and re-query dependent uniform locations (`materialUniforms` for `shader`, `colourLocation` for `arrowShader`, skybox view/proj locs, `idLocation` for `idShader`). Failed compiles keep the old program.
- `checkForResourceChanges()` (1014) — every 2 s (SDL ticks): `readResources` from the project `res/`, `mergeShaderSourceTable`, `recompileChangedShaders`. Called each frame from `drawDockspaceAndPanels` (8126).
- `findSceneCameraEntity()` (1056) — first active entity with `sceneCameraFlags` set, or -1.
- `setSceneCameraEntity(u32 entityID)` (1069) — exclusive-set the scene-camera flag across all entities.
- `projectSkyboxExists(c8 *outFolder, u32 folderSize)` (1088) — true if all six faces exist under `<project>/res/Textures/Skybox`.
- `saveSkyboxFacesToProject(const c8 *facePaths[6])` (1112) — copy faces into the project skybox folder (`platformFileCopy`) and reload from there when all six copied.
- `drawSceneCameraMarker(const Transform &t)` (1164) — draw a small yellow cube with `arrowShader` marking the scene-camera entity (used for model-less entities in `renderGameScene`).
- `populateEcsArchetypes()` (1172) — build per-system Archetype instances + scene-index maps from scene entities' `archetypeID`s (body mostly outside read regions; called from `doBuildAndRun`).
- `cleanupEcsArchetypes()` (1354) — destroy per-system archetypes; must run while the game DLL is loaded.
- `setupScenePhysicsArchetype()` (1371) / `cleanupScenePhysicsArchetype()` (1524) — build/destroy `g_scenePhysArch` from plain scene entities with colliders.
- `resizeViewportFramebuffers(u32 width, u32 height)` (1739) — create-or-resize all `viewportFBs`, `finalDisplayFB`, and (when deferred is on) the renderer's G-buffer via `rendererEnableDeferred` so depth-blit dimensions match the viewport. No-op if size unchanged or invalid.
- `uploadSceneLights()` (1779) — pack up to `MAX_LIGHTS` `GPULight`s from the SceneEntity light SoA fields (iterating `sceneArchetype.arena[0].count`, chunk 0 only; per-field NULL fallbacks) and `rendererUploadLights`. Called from both deferred branches of `renderGameScene`; also relied on by `renderMaterialPreview` (light SSBO binding 3 already populated).
- `renderGameScene()` (1814) — the frame render described in the Synopsis. Key details: `dt` is derived from `editor->fps`, not a measured delta; edit mode copies `sceneCam` into the renderer camera slot, play mode deliberately does not; `rendererBeginFrame` runs every frame; play mode skips entities whose `archetypeID` maps to a loaded ECS archetype to avoid double-rendering; collider gizmos come from `physWorldDebugDraw` in play mode (gated on `showGizmosInPlayMode`) vs. archetype/scene field scans in edit mode; light gizmos (sphere/arrow/cone-circle by light type) and the RGB translate handles (`arrowShader` + `cubeMesh`, distance-scaled, depth test off) are edit-mode only; final gizmo flush computes view-proj from the active renderer camera (play) or `sceneCam` (edit).
- `snapshotScene()` (7608) — wrap `sceneArchetype` + `resources->materialBuffer` in a `SceneData` (fixing up `arena[0].count`/`cachedEntityCount`/`activeChunkCount` from `entityCount` first, and syncing archetype hashes) and `saveScene` to the snapshot path.
- `restoreSnapshot()` (7629) — `loadScene` the snapshot, `sceneRemapModelIDs`, null out `sceneArchetype`'s per-chunk `fields`/`data` pointers before `destroyArchetype` (comment: game DLL may have reallocated arenas; "intentional leak — one-time play-mode allocs"), adopt the loaded archetype, rebuild `entityCount`/`entitySize`, run `migrateSceneArchetypeIfNeeded` → `rebindArchetypeFields` → `remapSceneArchetypeIDsFromHashes` → `applySceneCameraEntityToSceneCam`, restore the material buffer (clamped to `resources->materialCount`), reset inspector state, delete the snapshot file.

## Open questions / unclear intent

1. **Unused FBO slots.** `MAX_FBOS` is 5 and editor.h reserves slot 4 (`ID_FBO_INDEX`) for ID picking, but `activeFBO` is never changed from 0 in `editor.cpp` and `ID_FBO_INDEX` is never referenced; picking instead uses the entitypicker module's own framebuffer (`resizeIDFramebuffer`/`renderIDPass`). Slots 1–4 are allocated and resized every frame-size change but apparently never rendered to. Whether this is a planned ping-pong/bloom chain or dead design isn't determinable from this file.
2. **No bloom/multi-pass post-processing.** The only post-process is the single `fboShader` (`../res/FBOShader.*`) fullscreen pass into `finalDisplayFB`; what that shader actually does (tonemap? passthrough?) is not visible from this file.
3. **`resetRuntimeFreeViewState`, `snapRuntimeFreeViewToPlayerEntity`, `g_runtimeFreeView`** are declared in editor.h but defined nowhere under `Editor/src` that grep can find — their implementation/owner is unknown from the given files.
4. **`g_editorCamSlot` acquisition** — nothing in the read regions calls `rendererAcquireCamera`; presumably done in `main.cpp` init, unverified.
5. **Restart path in `doBuildAndRun`** — when called while a game is already running it unloads the DLL and rebuilds but does not call `restoreSnapshot` or the ECS/physics teardown from `doStopGame` before re-snapshotting; whether the second `snapshotScene` intentionally captures the mid-play (mutated) scene state is unclear.
6. **`restoreSnapshot`'s "intentional leak"** — zeroing `arena[ci].fields/data` before `destroyArchetype` leaks whichever allocations the DLL made; the comment asserts intent but the actual ownership contract with the game DLL is not documented here.
7. **`dt` from `editor->fps`** — `renderGameScene` (and the update path at ~8129) derive dt as `1/fps` rather than a measured frame delta; whether `editor->fps` is a live measurement or a fixed target is defined in engine code not read here.
8. **Leftover debug artifacts** — the blue `glClearColor(0.2,0.4,0.8,1)` is commented "to test scene rendering", and `drawViewportWindow`'s post-process block is labeled "Debug"; unclear if the clear color is intended as the final no-skybox fallback color.
