# editor-ui-2 — ImGui Panels (Editor/src/editor.cpp, panels slice)

> **Slice note:** `Editor/src/editor.cpp` is documented across three dispatches.
> This document covers the **dockspace + ImGui panel half**:
> - Preview rendering + supporting pickers: lines ~396–836 (`renderMaterialPreview`, `renderModelPreview`, `modelThumbGet`, `modelThumbsClear`, `renderPrefabPreview`), ~1595–1737 (`drawTextureSelector`, `drawModelPickerPopup`), ~1739–1777 (`resizeViewportFramebuffers`)
> - ECS↔scene sync triggered by play/panels: ~1172–1593 (`populateEcsArchetypes`, `cleanupEcsArchetypes`, `setupScenePhysicsArchetype` (summary), `syncEcsToScene`)
> - Panels: ~2339–2594 (Viewport, Debug, Console), ~3051–4173 (Archetypes), ~4178–4899 (Prefabs + prefab loader/field editor), ~4901–5225 (Profiler), ~5229–5568 (scene helpers, Scenes panel, Scene List), ~5571–6316 (SoA field editor + Inspector), ~6321–6385 (multi-FBO helpers), ~6387–6484 (Skybox Settings), ~6488–7338 (Resource Manager tabs), ~7345–7602 (Loaded Entities, Build Log), ~7856–8377 (Camera Settings, `drawDockspaceAndPanels` + scene modals), 8379 (`editorLog`)
> - NOT covered here (other dispatches): `renderGameScene`, ID/gizmo picking, shader hot-reload (`buildShaderSourceTable`/`recompileChangedShaders`/`checkForResourceChanges`), play-in-editor (`doBuildAndRun`/`doStopGame`/`snapshotScene`/`restoreSnapshot`), `scanProjectArchetypes` internals, editor lifecycle (`init`/`update`/`render`/`destroy`).

## Synopsis

This slice implements the Druid Editor's entire ImGui panel layer: the fullscreen dockspace with menu bar, and every `draw*Window` panel (Viewport, Inspector, Console, Archetypes, Prefabs, Profiler, Scenes, Scene List, Resource Manager, Loaded Entities, Build Log, Skybox Settings, Camera Settings, Debug). Panels read and write the **scene archetype's SoA field arrays directly** through the extern pointers declared in `editor.h` (`positions`, `rotations`, `scales`, `modelIDs`, `archetypeIDs`, light/collider arrays, etc.), and call engine APIs (`resources`, `renderer`, `physicsWorld`, scene/prefab/material serializers) for everything else. `drawDockspaceAndPanels()` is the single entry point, called per-frame by the editor's `render()` hook (external to this slice); it also drives the per-frame ECS system tick and `syncEcsToScene()` during play mode.

## Public API Reference

Only four functions in this slice are non-`static` and callable from other Editor files (declared in `editor.h`):

### `void drawDockspaceAndPanels()` (line 7889)
- **Purpose:** Per-frame UI entry point. Creates the fullscreen `MainDockSpace` host window, menu bar (Scene / View / Project menus + Run/Stop buttons), first-run `DockBuilder` default layout, then calls every panel in order (see list at 8112–8125), then `checkForResourceChanges()` (other slice), physics-archetype registration (`physRegisterArchetype` for `ARCH_PHYSICS_BODY` entries, cached via `g_archPhysRegistered`), `physTick(dt)`, and — when `g_gameRunning` — `g_gameDLL.plugin.update(dt)`, per-archetype `g_ecsSystems[a].update(...)`, `syncEcsToScene()`, and a Player-entity ↔ gameplay-camera follow hack (snaps scene entities of archetype "Player" to `renderer` active-camera pos minus 1.6 eye height). Finally handles the Save/Load/New Scene modal popups.
- **Params/return:** none/void.
- **Side effects:** Everything above; menu items call `buildProject`, `doBuildAndRun`, `doStopGame`, `generateProjectFiles`, `updateProject`, `buildStandalone`, `reloadProjectResources`, `SDL_OpenURL`/`system()` (Open Project Folder), and `saveScene`/`editorLoadSceneFile`/`createBlankScene` from modals.
- **Callers:** external — the editor `render()` hook (other slice of this file).

### `void editorLog(LogLevel level, const c8 *msg)` (line 8379)
- **Purpose:** Log sink feeding the Console panel. Appends `strdup(msg)` to `consoleLines` up to `MAX_CONSOLE_LINES` (10000); silently drops lines after that. `level` is ignored (severity is re-derived in the Console by substring match on `[ERROR]` etc.).
- **Side effects:** heap allocation via `strdup`; freed only by the Console "Clear" button.
- **Callers:** external — registered as a logger callback elsewhere (declared in `editor.h`; no call site in this file).

### `void modelThumbsClear()` (line 672)
- **Purpose:** Deletes all cached model-thumbnail GL textures in `s_modelThumbTex[]` (512 slots).
- **Callers:** external (declared in `editor.h`; expected on resource reload). No call site within this slice.

### `void shutdownMaterialRegistry()` (line 513)
- **Purpose:** Frees GPU resources of both preview pipelines: `g_matPreviewGBuffer`/`g_matPreviewOutputFBO`/`g_matPreviewSphere` and `g_modelPreviewGBuffer`/`g_modelPreviewOutputFBO`; resets `g_matPreviewReady`/`g_modelPreviewReady`.
- **Callers:** external — editor `destroy()` per its header comment.

Also relevant but owned by adjacent slices and only *used* here: `initMultiFBOs` / `destroyMultiFBOs` / `renderFBOToScreen` (6321–6385, small public helpers for the viewport FBO chain — `renderFBOToScreen(fboIndex, shaderProgram)` draws `viewportFBs[fboIndex].texture` through a fullscreen quad), `syncSceneArchetypeHashesFromIDs` / `remapSceneArchetypeIDsFromHashes` (109–158, stable archetype-ID↔hash mapping; called by Archetypes panel Remove, scene load, snapshotting).

## Panels (all `static`, all called only from `drawDockspaceAndPanels`)

### `drawViewportWindow()` (2339)
- **Reads/writes:** Fits a 16:9 image into the window; on size change calls `resizeViewportFramebuffers()` and `resizeIDFramebuffer()` (entity picker). Writes `canMoveViewPort`, `g_viewportScreenPos`, `g_viewportSize` (consumed by input/picking code in other slices). Writes `sceneCam` projection from `g_editorFov/Near/Far` (Camera Settings panel values). When not in play mode, copies `sceneCam` into the renderer camera slot `g_editorCamSlot` via `bufferGet(&renderer->cameras, ...)` and pushes `updateCoreShaderUBO` **before** the ID pass so picking uses current matrices.
- **Triggers:** `renderIDPass()`, `renderGameScene()` (other slice), then post-processes `viewportFBs[activeFBO]` into `finalDisplayFB` via `renderFBOToScreen(activeFBO, fboShader)` and displays `finalDisplayFB.texture` as `ImGui::Image`.

### `drawDebugWindow()` (2436)
- Read-only diagnostics window "Debug": FPS, entity counts, inspector state, FBO IDs, skybox state, input axes, `resources` usage counters. One write: VSync checkbox → `setVSync()`.

### `drawConsoleWindow()` (2510)
- Window "Output". Displays `consoleLines[0..g_consoleCount)` with a list clipper; colors lines by `[ERROR]/[FATAL]`, `[WARNING]`, `[DEBUG]/[TRACE]` substrings. Buttons: Clear (frees all lines), Copy All (clipboard), per-line right-click Copy, auto-scroll checkbox. Fed by `editorLog`.

### `drawArchetypesWindow()` (3051)
- **Archetype Designer:** name + flag checkboxes (`isSingle`, `isPhysicsBody`, `isPersistent`, `isBuffered` + pool size, `Uniform Scale (f32)`, `Generate C++`), an editable extra-field table (name + type combo over `f32/i32/u32/b8/Vec2/Vec3/Vec4/Mat4/u64/c8[256]`).
- **"Generate Archetype Files":** prepends the transform block (`PositionX/Y/Z` f32, `Rotation` Vec4, `Scale` Vec3-or-f32), writes/appends a `// <DRUID_GEN_BEGIN name>` sentinel block into `<project>/src/<name>.h` (with `DRUID_FLAGS`, `FIELD(...)` macro list, physics fields when `isPhysicsBody`, `DECLARE_ARCHETYPE`), then calls `generateArchetypeFiles(...)` (project_builder) and registers/updates the entry in `g_archRegistry` (copying field metadata into `g_scannedFields`/`g_scannedFieldNames`, appending default physics fields for physics bodies).
- **Templates:** "Create StaticObject" one-click template (transform + `ModelID`, physics flag) then `scanProjectArchetypes()`.
- **Registered list:** per-entry Remove (unassigns/decrements `archetypeIDs` on scene entities, shifts `g_archRegistry` and the parallel scanned-field arrays in lockstep, then `remapSceneArchetypeIDsFromHashes()`), Edit (re-parses field list from the archetype's `.h` `FIELD(...)` macro — or legacy `_fields[] = {...}` in the `.c` — into the designer, restoring flags from `// DRUID_FLAGS 0x..`), Regenerate (re-emits files with current designer state; requires Edit first).
- **Hot/Cold Field Separation:** purely advisory analysis UI — per-field Hot/Cold radio table, cache-line math (64 B lines, 32 KB L1 batch estimates). Writes nothing to disk or registry.

### `drawPrefabsWindow()` (4266) + `loadPrefabForEditor` (4189) + `drawFieldEditor` (4227) + `pfFindField` (4254)
- Three-state UI (`PFUI_ARCH_SELECT` → `PFUI_PREFAB_LIST` → `PFUI_PREFAB_EDIT`).
- List state: enumerates `<project>/prefabs/*.prefab`, loads each with `loadPrefabForEditor` (validates `PREFAB_MAGIC`/`PREFAB_VERSION`, reads name/archName/field name+size+data triplets) and filters by selected archetype; Edit loads field bytes into `s_fieldData[field][256]` matched by field name; Del `remove()`s the file.
- Edit state: interactive 3D preview via `renderPrefabPreview` (drag = orbit, wheel = zoom, green collider gizmo overlay driven by `s_colliderShape`), Transform section (Position from PositionX/Y/Z, Euler-degree cache `s_eulerDeg` ↔ `Rotation` quaternion via `quatFromEuler`/`eulerFromQuat`, Vec3 or uniform-f32 Scale), Rendering section (`ModelID` via `drawModelPickerPopup`), Physics section (BodyType combo, collider shape/size/offset, Mass/InvMass/Restitution/LinearDamping), ECS Data table for all remaining fields via `drawFieldEditor` (size-based widget selection; 4-byte fields treated as int when name contains ID/Type/Count/Index/Slot, else float).
- Save → `prefabSave(<project>/prefabs/<name>.prefab, ...)`. **"Spawn in Scene"** writes a new scene entity directly into the SoA arrays (`isActive`, `positions`, `scales`, `modelIDs`, `archetypeIDs` + `archetypeHashes` via `archetypeHashFromName`, collider/mass fields, name), bumps `entityCount` and `sceneArchetype.arena[0].count`.

### `drawProfilerWindow()` (4913)
- Gated by `showProfiler`. 120-entry frame time/FPS ring buffers + `PlotLines`; sections read `profileGetCurrentFrame()` (CPU/GPU us, draw calls/triangles/vertices, GL bind counters, upload bandwidth), memory stats from `g_memory` (`memGetTotalUsed`, per-arena bars, per-tag breakdown), per-scope table. **Writes:** the "Arena Config" tree edits a `MemoryConfig` and saves `<project>/memory.conf` via `memSaveConfig` (applies next launch).

### `drawScenesPanel()` (5324) + `drawSceneListWindow()` (5463)
- **Scenes** (gated by `showScenesPanel`): lists `<project>/scenes/*.drsc`; double-click or Load → `editorLoadSceneFile()`; New Scene → `createBlankScene(128)`; "Set as Startup" writes the filename to `<project>/startup_scene.txt`.
- **Scene List:** `InputInt` bound to `entitySize` (display only — capacity changes need New Scene); "Add Entity" appends a default entity to the SoA arrays and bumps `entityCount`/arena count; tag filter (case-insensitive substring over `entityTags`); entity buttons labeled with `[tag] [Camera]/[Light] name`, clicking sets `currentInspectorState = ENTITY_VIEW`, `inspectorEntityID`, and refreshes the Euler cache `EulerAnglesDegrees`.

### Inspector: `drawInspectorWindow()` (5661) + SoA editor `drawSoAField` (5573) / `drawSoAEditorSection` (5635)
- Switches on `currentInspectorState` (EMPTY / SKYBOX — just a button opening Skybox Settings / ENTITY).
- **Entity view** edits the selected entity's SoA slots in place:
  - Header: name (`names[id*MAX_NAME_SIZE]`), Active checkbox, "Goto" (moves `sceneCam` behind the entity, sets `yaw`/`currentPitch`), Delete (swap-with-last over **every** layout field via `getArchetypeFields`+`memcpy`, then decrements `entityCount`).
  - Transform: `positions`/`scales` DragFloat3; rotation edited in Euler degrees (`EulerAnglesDegrees`, re-synced when selection changes) and converted with `quatFromEuler` into `rotations`; `entityTags`.
  - Rendering: model via `drawModelPickerPopup` → `modelIDs`; shader combo over `resources->shaderIDs` → `shaderHandles`; read-only model stats.
  - Material: combo assigns `entityMaterialIDs[id]` (`(u32)-1` = model default); "Edit" jumps to Resource Manager Materials tab (`g_selectedMaterialIdx`, `showResourceManager`, `g_resourceManagerTab=3`); read-only sliders of the effective `Material`.
  - Light: `isLight` checkbox seeds defaults into all light SoA arrays; type/color/intensity/range/direction/cone widgets write `lightTypes`, `lightColorR/G/Bs`, `lightIntensities`, `lightRanges`, `lightDirX/Y/Zs`, `lightInner/OuterCones`.
  - Physics Body (shown when the entity's `archetypeIDs` entry has `ARCH_PHYSICS_BODY`): body type/mass, collider shape/radius/half-extents/offset arrays; "Match Transform" (static `colliderMatchTransform[4096]` per-entity flags) auto-syncs collider size from `scales`. In play mode also shows/edits **runtime** ECS fields (`LinearDamping` editable; velocity/force read-only) by indexing `g_ecsArchetypes[archID]` at `ecsSlotIDs[id]`.
  - Scene Camera: `setSceneCameraEntity` (exclusive flag in `sceneCameraFlags`), `applySceneCameraEntityToSceneCam`, "Match Camera To View" writes `sceneCam` pose into the entity.
  - Archetype: combo writes `archetypeIDs` **and** `archetypeHashes` (kept consistent via `archetypeHashFromName`); "Apply Prefab" stamps PositionX/Y/Z, Scale, ModelID from a chosen `.prefab` onto the entity; when playing, lists/edits the entity's live per-system archetype fields.
- **SoA field editor safety model:** `drawSoAEditorSection` gets `getArchetypeFields(&sceneArchetype, 0)` + `sceneArchetype.layout` and bails if either is null or `entityID >= arena[0].count`. For each field it computes the element pointer as `fields[f] + (u64)fieldSize * entityID` — correct for SoA because each field array is contiguous with stride = field size. `drawSoAField` picks the widget purely from **byte size** (1 → checkbox, >=64 → InputText string, 16/12/8 → DragFloat4/3/2) plus a **name heuristic for 4-byte fields** (suffix `ID`/`id`/`Index`/`Count`/`Handle` or substring `Shader` → clamped-unsigned InputInt, otherwise DragFloat). The same function is reused for live ECS archetype fields in the Archetype section (comment at 6281 notes size-only matching confuses u32 vs f32 — hence the heuristic). Edits go straight into archetype memory; there is no undo, transaction, or type metadata beyond size+name.

### `drawSkyboxSettingsWindow()` (6387)
- Gated by `showSkyboxSettings`. Six per-face path inputs (seeded from `g_skyboxFaces`); Apply → `reloadSkyboxFromFaces` (validates files, creates new cubemap **before** freeing the old one, updates `cubeMapTexture` and `renderer->envMapTex`, error string in `g_skyboxError`); "Apply To Project" → `saveSkyboxFacesToProject` (copies faces into `<project>/res/Textures/Skybox` then reloads); "Load Folder" / "Use Project Skybox" fill the inputs with `right/left/top/bottom/front/back.jpg` conventions.

### Resource Manager: `drawResourceManagerWindow()` (7299) + tabs
- Gated by `showResourceManager`; tab bar with external tab-jump support (`g_resourceManagerTab` consumed into `pendingTab`, used by the Inspector's material Edit button).
- **Textures** (6488): filter + list/grid views over `resources->textureIDs` map; per-texture edits write `resources->textureFlags` (bit 0 = alpha cutoff), `textureAlphaThresholds`, `textureWrapModes`, `textureFilterModes`, and immediately apply wrap/filter via `glTexParameteri` on the live handle.
- **Shaders** (6730): lists `g_editorShaders[]` (editor built-ins with handle pointers) and `g_trackedProjectShaders[]`; per-entry "Recompile" calls `createGraphicsProgram`, patches every entity's `shaderHandles` from old→new handle, `freeShader(old)`, refreshes cached uniform locations (`materialUniforms`, `colourLocation`, skybox view/proj, `idLocation`).
- **Models** (6817): filter + list/grid (grid uses `modelThumbGet` cached thumbnails); detail panel calls `renderModelPreview` each frame and shows mesh/material breakdown. Read-only except `g_selectedModelIdx`.
- **Materials** (7026) + `scanMaterialPresets` (6986): list from `resources->materialIDs` with filter; "+ New Material" appends a blank `Material` and `insertMap`s a generated name; "Load Preset..." scans `<project>/materials/*.drmt` (`loadMaterial`) into new slots. Right panel renders `renderMaterialPreview(matIdx)` live and edits the `Material` in place: four texture slots via `drawTextureSelector` (albedo/normal/metallic/roughness) plus colour/metallic/roughness/emissive/opacity. Dirty tracking via `memcmp` against `g_matCleanState` snapshot shows a green "Save Preset" button → `saveMaterial(<project>/materials/<name>.drmt)`. "Assign to Selected Entity" writes `entityMaterialIDs[inspectorEntityID]`.
  - Note (7062): `insertMap(&resources->materialIDs, newName, &resources->materialUsed)` stores a pointer to `materialUsed` **before** incrementing it, unlike the preset path which passes `&newIdx`; whether `insertMap` copies the value or the pointer determines correctness — see Open questions.
- **Audio** (7233): read-only list of `resources->audioIDs` with computed duration from `AudioClip::spec`; "Play" button → `playSoundID(idx, 0.8f)`.

### `drawLoadedEntitiesWindow()` (7345)
- Gated by `showLoadedEntities` (View menu). Read-only runtime ECS browser: for each `g_ecsArchetypes[a]` with data, a collapsing header with field-layout tree and a clipped table (ID/Alive/Model/Position/Scale, capped at 500 rows) built by name-matching `PositionX/Y/Z`, `ScaleX/Y/Z`, `Alive`, `ModelID` fields. When the game runs, additionally queries the game DLL export `druidGetGameArchetypes` (up to 32 `Archetype*`) and shows those the same way.

### `drawBuildLogWindow()` (7585)
- Gated by `showBuildLog`. Shows `g_buildInProgress` status and the shared `g_buildLog` buffer in a read-only `InputTextMultiline` (both owned by project_builder slice).

### `drawCameraSettingsWindow()` (7856)
- Gated by `showCameraSettings`. Sliders write `g_editorFov`/`g_editorNear`/`g_editorFar` (consumed by `drawViewportWindow`) and `extern f32 camMoveSpeed` from main.cpp; "Reset to defaults".

## ECS ↔ Scene sync (triggered by play mode / panels)

### `populateEcsArchetypes()` (1172, static)
Called after DLL system discovery in `doBuildAndRun()` (other slice). For each registry entry with a loaded system: resolves `"<Name>_layout"` `StructLayout*` from the game DLL, creates a per-system `Archetype` (`g_ecsArchetypes[a]`, min capacity 256 for runtime-spawn pools), builds `g_ecsFieldMap[a][f]` by case-insensitive **name + exact byte-size** match against `sceneArchetype.layout` (special case: system `f32 Scale` ↔ scene `Vec3 scale` recorded in `g_ecsUniformScaleIdx[a]`), then copies matching field bytes scene→system for every scene entity with `archetypeIDs[id] == a`, recording `g_ecsSceneMap[a][idx] = id` and writing `ecsSlotIDs[id] = idx` back into the scene. Also decomposes scene `Vec3 position` into SoA `PositionX/Y/Z`, derives `InvMass` from `masses`, and defaults `Restitution`(0.3)/`LinearDamping`(0.01) on physics archetypes.

### `syncEcsToScene()` (1536, static)
Called every play-mode frame from `drawDockspaceAndPanels` after ECS updates. Reverse of the above: for each mapped field, copies system→scene bytes using `g_ecsFieldMap`; broadcasts uniform `f32` scale to all three scene `Vec3 scale` components; recomposes `PositionX/Y/Z` into the scene `Vec3 position`. This is what makes the Viewport and Inspector show live physics/gameplay results, and what the Inspector's runtime field edits flow through.

### `cleanupEcsArchetypes()` (1354) / `setupScenePhysicsArchetype()` (1371) / `cleanupScenePhysicsArchetype()` (1524) — static
Cleanup destroys per-system archetypes and maps (called from `doStopGame`). `setupScenePhysicsArchetype` builds the `g_scenePhysArch` ("ScenePhysics" layout, 23 fields) from active scene entities that have colliders but whose archetype lacks physics fields, inferring box shape from half-extents or half-scale; registered with the physics world so static geometry collides. (Play-lifecycle callers are in the other dispatch; documented here because the layout/global lives in this slice's data section.)

## Preview rendering (static)

- **`renderMaterialPreview(u32 matIdx)`** (428) — lazily creates a 256x256 GBuffer + RGBA8 output FBO + sphere mesh; overrides the core UBO with a fixed camera, runs a mini deferred pipeline (geometry with `shader`, lighting with `deferredLightingShader`, `cubeMapTexture` as env map); relies on the light SSBO already uploaded by `uploadSceneLights()` this frame (must run after `drawViewportWindow`). Saves/restores bound FBO + viewport.
- **`renderModelPreview(u32 modelIdx)`** (553) — same pipeline for a whole model, auto-framed by `boundingRadius`, per-mesh materials with a grey fallback material so stale G-buffer state can't bleed in.
- **`modelThumbGet(u32 modelIdx)`** (648) — renders once and copies into a cached GL texture (`s_modelThumbTex[512]`, `glCopyTexSubImage2D`); used by model picker/grid. Cleared by `modelThumbsClear()`.
- **`renderPrefabPreview(...)`** (685) — orbit-camera variant (yaw/pitch/zoom), plus a gizmo overlay (`gizmoDrawSphere`/`gizmoDrawBox`) drawing the collider from raw prefab field bytes (`SphereRadius`, `ColliderHalfX/Y/Z`, `ColliderOffsetX/Y/Z`) with 0.5 defaults; shape is passed explicitly, not inferred. Shares `g_modelPreview*` GL objects.

## Data Structures

| Name | Where | Description / lifetime |
|---|---|---|
| `SCENEENTITY_FIELDS` / `SceneEntity_fields[]` / `SceneEntity` (`StructLayout`) | 46–89 | X-macro definition of the 35-field scene entity SoA layout (transform, render IDs, archetype ID+hash, tag, physics/collider, light params). Static/global; `SceneEntity` is the layout used by `createArchetype` for `sceneArchetype`. Field order is load-bearing for `rebindArchetypeFields()` and the hardcoded indices in `setupScenePhysicsArchetype` (`scnFields[8]`, `[13]`–`[19]`) and sync code (`scnFields[0]` pos, `[2]` scale). |
| `g_archRegistry` (`ArchetypeRegistry`) | 92 | Editor-side registry of user archetypes (name, header/source paths, flags, poolCapacity, `layout` pointing into `g_scannedFields`). Static storage; populated by `scanProjectArchetypes` and the Archetypes panel; entries shifted manually on Remove. Not ECS-managed. |
| `g_scannedFields[MAX_ARCHETYPE_SYSTEMS][64]`, `g_scannedFieldNames[...][64][128]` | 358–359 | Backing storage for registry layouts (names + `FieldInfo`); static arrays, must be moved in lockstep with `g_archRegistry.entries` (see Remove logic 3514–3529). |
| `consoleLines` / `g_consoleCount` | 160–161 | `const c8**` array of `strdup`'d log lines (allocated elsewhere — see Open questions), filled by `editorLog`, freed by Console Clear. Plain heap. |
| Panel visibility flags: `showSaveModal/LoadModal/NewSceneModal`, `showProfiler`, `showScenesPanel`, `showSkyboxSettings`, `showCameraSettings`, `showColliders`, `showLightGizmos`, `showGizmosInPlayMode`, `showLoadedEntities`, `showResourceManager`, `showBuildLog` | 166–177, 397, 7340 | Static `b8` toggles driven by the View/Scene menus; a few are non-static (`showSkyboxSettings`, `showColliders`) because other slices read them. |
| `g_editorFov/Near/Far` | 180–182 | Editor camera settings; written by Camera Settings, read by Viewport each frame. |
| `g_skyboxFaces[6][512]`, `g_skyboxFaceSuffixes`, `g_skyboxError[256]` | 185–215 | Current cubemap face paths (defaults to `../res/Textures/Skybox/*`), naming convention, and last-error string for the Skybox panel. |
| `viewportFBs[MAX_FBOS=5]`, `finalDisplayFB`, `viewportWidth/Height`, `activeFBO`, `screenQuadMesh`, `skyboxMesh`, `cubeMapTexture`, `skyboxShader`+locs | 200–212 | Global (extern'd) multi-FBO viewport chain; FBO slot 4 = ID picking (`ID_FBO_INDEX`). Created/resized by `resizeViewportFramebuffers`/`initMultiFBOs`, destroyed by `destroyMultiFBOs`. GPU-owned, not ECS. |
| `sceneCam`, `g_editorCamSlot`, `entityCount`, `entitySizeCache`, `inspectorEntityID`, `currentInspectorState`, `manipulateState/Transform` | 306–324 | Editor camera + selection state, extern'd across editor files. |
| `g_ecsSystems`/`g_ecsSystemLoaded`/`g_ecsSystemCount` | 329–331 | Game-DLL ECS system plugin table (populated in the play-mode slice; read here for update ticking and Inspector runtime views). |
| `g_ecsArchetypes[MAX_ARCHETYPE_SYSTEMS]`, `g_ecsSceneMap` (calloc'd `u32*`), `g_ecsArchEntityCount`, `g_ecsFieldMap[sys][64]`, `g_ecsUniformScaleIdx`, `g_archPhysRegistered` | 345–350 | Per-archetype runtime instances + scene mapping. Archetype arenas are ECS-managed (`createArchetype`/`destroyArchetype`); `g_ecsSceneMap` rows are plain `calloc`/`free`. Lifetime: play start (`populateEcsArchetypes`) → stop (`cleanupEcsArchetypes`). |
| `ColliderFieldCache` / `g_colliderCache` | 353–354 | Cached per-archetype collider field indices (avoid per-frame strcmp); consumed by gizmo drawing in another slice. |
| `g_scenePhysFields[23]` / `g_scenePhysLayout` / `g_scenePhysArch` / `g_scenePhysCreated` | 364–394 | "ScenePhysics" fallback physics archetype for non-ECS static colliders; ECS-managed archetype, created/destroyed at play start/stop. |
| Resource Manager state: `showResourceManager`, `g_resourceManagerTab`, `g_selectedMaterialIdx`, `g_matNameFilter`, preset arrays (`g_matPresetPaths/Names/Count`, max 128), dirty snapshot `g_matCleanState`/`g_matCleanIdx` | 396–411 | Static UI state; `g_resourceManagerTab` doubles as a cross-panel "jump to tab" mailbox (set to 3 by the Inspector, consumed then set to -1). |
| Preview GL state: `g_matPreviewGBuffer/OutputFBO/Sphere/Ready`, `g_modelPreviewGBuffer/OutputFBO/Ready`, `g_selectedModelIdx`, `MATPREVIEW_SIZE=256`, `s_modelThumbTex[512]` | 414–424, 645–646 | Lazily created GPU resources; freed by `shutdownMaterialRegistry` / `modelThumbsClear`. |
| `EditorPrefabData` | 4178 | Stack-sized (~20 KB) parsed `.prefab`: name, archName, up to 64 fields x (name[?], size, 256 data bytes). Stack/static only, no heap. |
| `PfUIState` enum | 4264 | Prefabs panel state machine (`PFUI_ARCH_SELECT/PREFAB_LIST/PREFAB_EDIT`). |
| Profiler ring buffers: `frameTimeHistory[120]`, `fpsHistory[120]`, `profilerHistoryIndex`, accumulator/avg/min/max | 4902–4911 | File-static plain arrays, updated only while the Profiler window is open. |
| `EulerAnglesDegrees` | 5226 | Shared Euler-degree cache for the Inspector rotation widget; re-synced on selection change (both in Scene List click and Inspector). |
| Dock/window constants | `editor.h` | `MAX_FBOS=5`, `ID_FBO_INDEX=4`, `MAX_CONSOLE_LINES=10000`, `PREFAB_UI_MAX_FIELDS=64`, `PREFAB_UI_FIELD_DATA=256`, `EDITOR_FIELD_NAME_MAX=128`. |

Scene entity data itself is **ECS-managed**: it lives in `sceneArchetype` (created by `createArchetype(&SceneEntity, ...)` in `createBlankScene`/scene load), and all the extern SoA pointers are rebound into it by `rebindArchetypeFields()` (other slice). Panels never allocate entity storage; they only write within `entitySizeCache` capacity.

## Internal (static) functions in this slice — name + purpose

- `archetypeHashFromName` — case-insensitive FNV-1a 64 for stable archetype identity.
- `reloadSkyboxFromFaces` / `reloadSkyboxFromFolder` — validate + rebuild cubemap, sync `renderer->envMapTex`.
- `makeGizmoColor` — `GizmoColor` constructor helper.
- `renderMaterialPreview`, `renderModelPreview`, `modelThumbGet`, `renderPrefabPreview` — preview pipelines (see above).
- `fieldNameCmp` — case-insensitive strcmp for field mapping.
- `knownTypeSize` — type-name → byte size table (mirrors designer types).
- `findSceneCameraEntity` / `setSceneCameraEntity` — query/assign the exclusive `sceneCameraFlags` bit.
- `projectSkyboxExists` / `saveSkyboxFacesToProject` — project skybox convention helpers.
- `drawSceneCameraMarker` — draws a yellow cube at the camera entity (used by render slice).
- `populateEcsArchetypes` / `cleanupEcsArchetypes` / `setupScenePhysicsArchetype` / `cleanupScenePhysicsArchetype` / `syncEcsToScene` — ECS↔scene bridge (see above).
- `drawTextureSelector` — labeled texture thumbnail + grid picker popup writing a `u32` GL handle.
- `drawModelPickerPopup` — thumbnail grid model picker writing a `u32` model index (`(u32)-1` = none).
- `resizeViewportFramebuffers` — resize all viewport FBOs + deferred GBuffer + final display FBO.
- `uploadSceneLights` — packs `isLight` entities into a `GPULight[MAX_LIGHTS]` SSBO upload (called by render slice; previews depend on it).
- `hasArchetypeMarkerLine` — exact `// marker` line detector for legacy archetype metadata.
- `drawViewportWindow`, `drawDebugWindow`, `drawConsoleWindow`, `drawArchetypesWindow`, `drawPrefabsWindow`, `drawProfilerWindow`, `drawScenesPanel`, `drawSceneListWindow`, `drawInspectorWindow`, `drawSkyboxSettingsWindow`, `drawResourceManagerWindow` (+ `drawResourceTab_Textures/Shaders/Models/Materials/Audio`, `scanMaterialPresets`), `drawLoadedEntitiesWindow`, `drawBuildLogWindow`, `drawCameraSettingsWindow` — panels (see above).
- `loadPrefabForEditor`, `drawFieldEditor`, `pfFindField` — prefab binary loader + generic field widget + layout field lookup.
- `createBlankScene`, `editorLoadSceneFile` — scene lifecycle helpers used by Scenes panel, Scene menu modals.
- `drawSoAField`, `drawSoAEditorSection` — generic SoA field editor (see Inspector).

## Open questions / unclear intent

1. **Material map insert aliasing (7062):** "+ New Material" calls `insertMap(&resources->materialIDs, newName, &resources->materialUsed)` and then increments `materialUsed`, while the preset-load path passes `&newIdx` (a stack local). If `insertMap` deep-copies the `u32` value at insert time the first is a subtle ordering trick (value == pre-increment index) and the second dangles-by-copy safely; if it stores the pointer, one of the two is wrong. Cannot determine without the hash-map source (out of scope for this slice).
2. **Who allocates `consoleLines`:** `editorLog` writes into it and the Console frees entries, but the `const c8**` array itself is allocated elsewhere (presumably `init()` in the lifecycle slice). Ownership is split across slices.
3. **`drawSceneListWindow`'s `InputInt("Max Entity Size", &entitySize)`** edits the global `entitySize` but nothing in this slice reacts to the change (capacity is `entitySizeCache`, only reset by New Scene / load). Intent unclear — display, or vestigial resize feature.
4. **`colliderMatchTransform[4096]`** (Inspector, 6017) is per-entity UI state in a fixed static array keyed by entity ID; it isn't saved with the scene and silently stops working for IDs >= 4096. Whether that cap is intentional is unclear.
5. **Uniform-scale copy in `populateEcsArchetypes` (1297) and `syncEcsToScene` (1578)** hardcode `scnFields[2]` as the scene scale (and `scnFields[0]` as position at 1586), duplicating knowledge of `SCENEENTITY_FIELDS` ordering rather than using the `SE_*` enum generated at line 84. Works today; fragile if field order changes — unclear if intentional micro-optimisation.
6. **`drawDebugWindow`'s "FBOs Created" count** sums only slots 0–3 of the 5 FBOs (omits `ID_FBO_INDEX` 4) while printing `/MAX_FBOS`; unclear whether the ID FBO is deliberately excluded.
7. **`renderPrefabPreview` collider overlay** reads offsets from `fieldData` assuming rows of exactly `PREFAB_UI_FIELD_DATA` (256) bytes; callers must pass `s_fieldData` shaped that way — the `StructLayout*` param sizes are ignored for addressing. Implicit contract, not enforced.
8. **`s_pendingDelete` prefab deletion** uses `remove()` immediately with no confirmation dialog; unclear if a confirm step was intended (the deferred-one-frame pattern suggests one may have been planned).
9. **`drawResourceTab_Audio` dereferences `resources` before its null check pattern** used by the other tabs (`resources->audioUsed` at 7239 with no `if (!resources) return;`). All call paths go through `drawResourceManagerWindow`, which does check `resources`, so it's safe in practice — noted as inconsistency, not judged here.
