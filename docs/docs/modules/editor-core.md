# editor-core

Files: `Editor/src/main.cpp`, `Editor/src/editor.h`, `Editor/src/hub.cpp`,
`Editor/src/hub.h`, `Editor/src/entitypicker.cpp`, `Editor/src/entitypicker.h`.
(`editor.cpp` and `project_builder.cpp` are documented separately; several
symbols declared in `editor.h` are implemented there and only referenced here.)

## Synopsis

editor-core is the Druid Editor executable's entry point and frame driver. It
runs two `Application` lifecycles back-to-back: the **hub** (project
launcher/creator, persisted to `projects.conf`) and then the **editor** proper
(`init`/`update`/`render`/`destroy` hooks passed to the engine's
`createApplication`/`run`). It owns the single `SceneEntity` archetype
(`sceneArchetype`) that holds all editor-scene entities as SoA columns, exposes
those columns as ~35 global typed pointers (`positions`, `rotations`, ...,
`colliderOffsetZs`), keeps them valid across scene loads and schema changes
(`rebindArchetypeFields`, `migrateSceneArchetypeIfNeeded`), and implements
mouse picking through a color-coded ID framebuffer pass (`renderIDPass` /
`getEntityAtMouse`). It depends on the engine (`druid.h`: ECS storage, scene
serialization, renderer, input, filesystem — see `ecs-storage.md`,
`ecs-scene-prefab-systems.md`, `renderer.md`), on ImGui (SDL3 + OpenGL3
backends), and on `project_builder` / `editor.cpp` for build-and-run and panel
drawing.

## Editor lifecycle overview

`main(argc, argv)` (main.cpp:1091):

1. **Hub phase** — `createApplication("Druid Hub", hubStart, hubUpdate,
   hubRender, hubDestroy)` at 720x480 with `hubProcessInput`; `run()` blocks
   until the hub exits. If `hubProjectSelected` is still false, `SDL_Quit()`
   and return 0 (user cancelled).
2. **Editor phase** — installs `editorLog` as the engine's custom log sink
   (`logOutputSrc = &editorLog; useCustomOutputSrc = true`), `calloc`s
   `consoleLines` (`MAX_CONSOLE_LINES` = 10000 `c8*` slots), creates the
   editor `Application` sized to 80% of the primary display (fallback
   1920x1080), seeds `viewportWidth/Height` from it, sets
   `editor->inputProcess = processInput`, and `run(editor)`.

`init()` order (main.cpp:394):
1. `createArchetype(&SceneEntity, 128, &sceneArchetype)` then
   `rebindArchetypeFields()`.
2. ImGui context + SDL3/OpenGL3 backends; ini persisted to
   `<SDL_GetBasePath()>imgui_editor.ini`; docking + keyboard/gamepad nav on.
3. Shader compilation from `../res/`: `shader` (lighting) + `materialUniforms`,
   `arrowShader` (+`colourLocation`), skybox (`skyboxShader`, view/proj uniform
   locations, `loadPreferredSkybox()`), `fboShader` (post-process, warn-only on
   failure), `deferredLightingShader` (warn-only).
4. `initCamera(&sceneCam, {0,0,5}, 70°, aspect 1.0, 0.1/100)`.
5. Global `Renderer` creation (`createRenderer(display, 70, 0.1, 100, 8, 16,
   8)`), `r->defaultShader = shader`, acquires `g_editorCamSlot` via
   `rendererAcquireCamera` and makes it active, sets `r->envMapTex`.
6. `gizmoInit()`, `cubeMesh = createBoxMesh()`, registers Box/Plane/Sphere
   primitives via `resRegisterPrimitive` (temporary `Mesh` structs `dfree`d
   immediately after).
7. `initIDFramebuffer()` (picking shader) and `initMultiFBOs()` (viewport FBO
   set including the ID FBO at `ID_FBO_INDEX` = 4 of `MAX_FBOS` = 5).
8. **Startup scene auto-load**: scans `<hubProjectDir>/scenes` with
   `listFilesInDirectory`, picks the first `.drsc`, `loadScene` +
   `sceneRemapModelIDs`, copies path into `scenePathBuffer`, then
   `destroyArchetype(&sceneArchetype)` and adopts `sd.archetypes[0]` as the
   new `sceneArchetype` (sets `entitySizeCache` from capacity, `entityCount`
   from `arena[0].count`). Then `migrateSceneArchetypeIfNeeded()` →
   `rebindArchetypeFields()` → `applySceneCameraEntityToSceneCam()`. Scene
   materials are memcpy'd into `resources->materialBuffer` (clamped to
   `resources->materialCount`) and the `SceneData.modelRefs` array is *stashed*
   in `g_startupModelRefs` for a second remap later.
9. `scanProjectArchetypes(hubProjectDir)` (archetype designer registry,
   implemented in editor.cpp).
10. Project resource load: `readResources(resources, "<proj>/res/")`,
    `loadTextureMetadata("<proj>/res/textures.meta")`,
    `applyMaterialPresets`, `buildShaderSourceTable`. Then the deferred
    second `sceneRemapModelIDs` runs using `g_startupModelRefs` against the
    now-fully-populated resource manager (so startup-scene model IDs resolve
    to project models loaded *after* the scene), and the refs are `dfree`d.
11. `SDL_GL_SetSwapInterval(0)` — vsync off.

`processInput(appData)` (per frame, before update): `SDL_PumpEvents` +
`SDL_PollEvent` loop forwarding to `ImGui_ImplSDL3_ProcessEvent` (guarded by
`ImGui::GetCurrentContext()` for pre-init events); handles `SDL_EVENT_QUIT`
(sets file-static `g_quitRequested` — does **not** exit directly, so render()
can show the save-confirm modal), gamepad add/remove, and window resize
(updates `app->width/height` + `glViewport`). Then computes an input-capture
flag from `io.WantCaaptureMouse/Keyboard`, forced *off* when `canMoveViewPort`
(viewport hovered) or `g_gameRunning` (play mode — the game DLL must get raw
input), and pushes it via `setInputCaptureState`.

`update(dt)` (main.cpp:680):
- If `entitySize != entitySizeCache`, calls `reallocateSceneArena()` (currently
  an empty stub — see Open questions) and syncs the cache.
- **Gizmo drag** (runs even when the cursor leaves the viewport so drags don't
  cancel): on left-mouse-held with `canMoveAxis`, projects `manipulateAxis`
  through `sceneCam`'s view-projection into screen space, dots the mouse delta
  against it (`mag`), and applies `mag * 0.01` to
  `positions`/`rotations` (quat pre-multiply)/`scales` of
  `inspectorEntityID` per `manipulateState`.
- When `canMoveViewPort && !g_gameRunning`: `moveViewPortCamera(dt)` (WASD-axis
  move + RMB/arrow-key rotate + scroll-wheel speed 1..25); E/R/T select
  position/rotation/scale gizmo mode; left-click runs
  `getEntityAtMouse(mousePos, g_viewportScreenPos)` — `PICK_ENTITY` selects
  `id - 1` into `inspectorEntityID` (switches inspector to `ENTITY_VIEW`,
  enables `manipulateTransform`), `PICK_GIZMO_X/Y/Z` arms `canMoveAxis` with
  the matching world axis, `PICK_NONE` deselects.
- **Hotkeys** (all edge-triggered with `canX` latch booleans): F5 →
  `doBuildAndRun()`, Shift+F5 → `doStopGame()` (both in editor.cpp);
  Ctrl+S → builds a `SceneData` pointing at `sceneArchetype` and the resource
  manager's material buffer, calls `syncSceneArchetypeHashesFromIDs()`, names
  archetype 0 `"SceneEntity"`, and `saveScene(scenePathBuffer, &sd)`.
- **Ctrl+C/Ctrl+V entity copy-paste** (blocked while `io.WantTextInput` or
  `g_gameRunning`): copy serializes the inspected entity's per-field slices
  into a static 4096-byte `clipboardBuf` in layout order; paste writes them
  into slot `entityCount` (respecting `entitySizeCache` cap), nudges
  `positions[dst].x += 1`, appends `" (copy)"` to the name, resets
  `ecsSlotIDs[dst]` to `(u32)-1` (forces fresh ECS mapping on next play),
  increments `entityCount` and `sceneArchetype.arena[0].count`, and selects
  the clone.

`render(dt)` (main.cpp:970): ImGui new-frame (OpenGL3, SDL3, ImGui), then
`drawDockspaceAndPanels()` (all panel drawing lives in editor.cpp). If
`g_quitRequested`, opens the "Save before exiting?" modal: "Save & Exit" calls
static `saveCurrentScene()` and sets `editor->state = EXIT`; "Exit without
Saving" just sets EXIT; "Cancel" clears `g_quitRequested`. Finishes with
`ImGui::Render`, draw-data submission, and multi-viewport platform-window
update. Note: the 3D viewport itself (scene render, `renderIDPass`) is driven
from `drawDockspaceAndPanels` in editor.cpp, not from this function directly.

`destroy()` (main.cpp:1022): `saveTextureMetadata` to the project;
`plugin.destroy()` + `unloadGameDLL` if a game is still running; frees
`g_startupModelRefs`; `destroyArchetype(&sceneArchetype)` and nulls the first
eight field pointers; `shutdownMaterialRegistry`; frees `cubeMesh`, the five
shaders, `skyboxMesh`, `cubeMapTexture`; `destroyIDFramebuffer` +
`destroyMultiFBOs`; `physShutdown` + `gizmoShutdown`; `destroyRenderer`;
frees each `consoleLines[i]` and the array; `modelThumbsClear`; ImGui
backend/context shutdown.

### Editor vs. game

The editor never ticks game logic itself in these files: play mode is entered
via `doBuildAndRun()` / left via `doStopGame()` (editor.cpp), and editor-core
only *observes* `g_gameRunning` / `g_gameDLL` to (a) route raw input to the
game (`processInput`), (b) suppress camera fly, picking, hotkey save-guards,
and copy-paste while playing (`update`), and (c) tear the plugin down in
`destroy()`.

## Public API Reference

### Application hooks and camera (main.cpp; declared in editor.h)

- `void processInput(void *appData)` — per-frame SDL event pump + ImGui
  forwarding + input-capture routing. Param: `appData` is the `Application*`.
  Side effects: `g_quitRequested`, `app->width/height`, `glViewport`,
  `setInputCaptureState`. Caller: engine main loop via
  `editor->inputProcess` (set in `main`).
- `void init()` / `void update(f32 dt)` / `void render(f32 dt)` /
  `void destroy()` — the editor lifecycle described above. Callers: engine
  `run()` via `createApplication`.
- `void rotateCamera(f32 dt)` — RMB mouse-look (yaw/pitch quats, pitch clamped
  to ±89°) plus arrow-key rotation of `sceneCam`. Non-static but only called
  by `moveViewPortCamera` in these files; possibly also external.
- `void moveViewPortCamera(f32 dt)` — `moveCamera` + `rotateCamera` +
  scroll-wheel `camMoveSpeed` adjust (only while RMB held). Caller: `update`.
- `i32 main(i32, char**)` — process entry; hub phase then editor phase (see
  overview).

### Archetype-migration trio (main.cpp; declared in editor.h except the first)

- `void reallocateSceneArena()` — **empty body** (main.cpp:191-193). Called
  from `update()` when the user-edited `entitySize` diverges from
  `entitySizeCache`; currently does nothing, the cache is just resynced.
- `void rebindArchetypeFields()` — refreshes all ~35 global SoA column
  pointers from `getArchetypeFields(&sceneArchetype, 0)`. Fields 0–7
  (`positions`..`entityMaterialIDs`) are assumed present; fields 8–34
  (`archetypeIDs`.. `colliderOffsetZs`) are nulled when
  `sceneArchetype.layout->count` is too small (supports partially-migrated /
  old layouts). `FATAL`s if `getArchetypeFields` returns null. Callers:
  `init()` (twice: after create and after scene adoption); external callers in
  editor.cpp expected (declared in editor.h "after the archetype changes").
- `void migrateSceneArchetypeIfNeeded()` — schema upgrade for old `.drsc`
  scenes. If `sceneArchetype.layout->count >= SceneEntity.count` it returns.
  Otherwise: (1) `malloc`+memcpy each old field column (`size * liveCount`)
  into temp `fieldCopies[32]`, recording sizes and names; (2)
  `destroyArchetype` and, when the layout is the dynamic one allocated by
  `loadScene` (`dynLayout != &SceneEntity`), manually `dfree`s each field
  name, the `fields` array, the layout name, and the `StructLayout` itself
  (all `MEM_TAG_SCENE`); (3) `createArchetype(&SceneEntity, capacity, ...)`;
  (4) copies old columns into the new archetype **matched by field name and
  size** (not index), so fields inserted mid-layout don't shift data; (5) if
  the old schema had ≤ 8 fields, defaults `archetypeID` (`newFields[8]`) to
  `(u32)-1` for every live entity (all other new fields stay zero from
  `createArchetype`); (6) restores `arena[0].count = liveCount` and frees the
  temp copies. Callers: `init()` after startup-scene adoption; declared in
  editor.h for external (editor.cpp scene-open) use.

### Hub (hub.cpp / hub.h)

- `void hubStart()` — `loadConfig()` (parses `projects.conf`), creates the
  hub's own ImGui context + SDL3/OpenGL3 backends. Caller: engine `run()`.
- `void hubUpdate(f32 dt)` — ticks the 4-second `statusTimer` down; sets
  `hubApplication->state = EXIT` once `hubProjectSelected` is true (this is
  how "open project" ends the hub loop). Caller: engine `run()`.
- `void hubRender(f32 dt)` — full-window ImGui UI: recent-project list
  (invalid projects greyed out with tooltip "Missing required directories!";
  single click selects + fills `hubProjectDir`; double-click on a valid one
  sets `hubProjectSelected`), a path `InputText`, and buttons:
  - **Load Project** — `normalizePath`, `isValidProject` check
    (must contain `src`, `res`, `bin`, `scenes`), then `addToSaved` + select.
  - **New Project** — refuses if a valid project already exists there;
    `scaffoldProject` creates the four required dirs plus `deps/`, then
    `generateProjectFiles(hubProjectDir)` (project_builder: default sources,
    shaders, CMakeLists), `addToSaved`, select.
  - **Update Project** — `updateProject(hubProjectDir, log, ...)`
    (project_builder: rebuild engine, copy headers/libs/DLLs); disabled unless
    the typed path is a valid project.
  - **Remove from List** — `removeFromSaved(selectedProjectIdx)`.
  - **Cancel** — `hubApplication->state = EXIT` without selecting (main()
    then quits instead of launching the editor).
  Status messages render for 4 s at the bottom. Caller: engine `run()`.
- `void hubDestroy()` — `saveConfig()` + ImGui backend/context shutdown. The
  editor phase then creates a *fresh* ImGui context in `init()`.
- `void hubProcessInput(void *appData)` — minimal pump: forwards events to
  ImGui, `SDL_EVENT_QUIT` → `app->state = EXIT`.

### Entity picker (entitypicker.cpp / entitypicker.h)

- `void initIDFramebuffer()` — compiles `../res/idShader.vert/.frag` into
  `idShader` and caches the `entityID` uniform location. The actual FBO lives
  in the multi-FBO system (`viewportFBs[ID_FBO_INDEX]`, created by
  `initMultiFBOs` in editor.cpp). Caller: `init()`.
- `void renderIDPass()` — renders the whole scene into the ID FBO with flat
  RGB-encoded 24-bit IDs:
  - Each active entity draws with `objectID = (i + 1) | ID_TYPE_ENTITY`
    (i.e. index+1 in the low 16 bits) split into R=bits16-23, G=8-15, B=0-7.
    Entities with `modelIDs[i] == (u32)-1` draw a small `cubeMesh` marker
    (slightly elongated for `sceneCameraFlags` camera entities) so model-less
    entities remain pickable; otherwise every mesh of
    `resources->modelBuffer[modelIDs[i]]` is drawn via `drawMeshIDPass` with
    bounds checks and error logs.
  - The skybox draws as `ID_TYPE_SKYBOX` (0x040000) with depth-write off /
    `GL_LEQUAL`.
  - If `manipulateTransform`, three fat axis boxes (distance-scaled ~3x the
    visual gizmo thickness, offsets along +X/+Y/−Z) draw with IDs 0x010000 /
    0x020000 / 0x030000 (encoded as raw red values 1/2/3 over 255), with
    depth test and face culling disabled so gizmos always win the pick.
  Early-outs with a WARN if `viewportFBs[ID_FBO_INDEX].fbo == 0`. Side
  effects: binds/unbinds the ID framebuffer, mutates GL state (restored).
  Caller: none in these files — invoked from the viewport draw in editor.cpp
  (external to this module's file set).
- `PickResult getEntityAtMouse(ImVec2 mouse, ImVec2 viewportTopLeft)` —
  converts the mouse to viewport-relative coordinates (bounds-checked against
  `viewportWidth/Height`, Y flipped for GL), `glReadPixels` one RGB pixel from
  the ID FBO, reassembles the 24-bit id, and decodes it via `ID_MASK_TYPE` /
  `ID_MASK_ENTITY` into a `PickResult` (`PICK_NONE` for id 0 or out-of-view).
  Emits a DEBUG line with full coordinates. Caller: `update()`.
- `void drawMeshIDPass(Mesh *mesh)` — draw helper mirroring engine
  `drawMesh()`: `glDrawElementsBaseVertex` for `mesh->buffered` meshes in the
  shared GeometryBuffer (using `firstIndex`/`baseVertex`), else
  `glDrawElements` when an EBO is bound, else `glDrawArrays`. Null/VAO-0/
  drawCount-0 guarded. Callers: `renderIDPass`; declared in the header so
  editor.cpp can reuse it.
- `void destroyIDFramebuffer()` — frees `idShader` only (the FBO belongs to
  `destroyMultiFBOs`). Caller: `destroy()`.
- `void resizeIDFramebuffer(u32, u32)` — **no-op**; resizing is handled by
  `resizeViewportFramebuffers()` in the multi-FBO system. No caller in these
  files.

### Macros

- `entitypicker.h`: `ID_TYPE_ENTITY` (0 << 16 — entity ids occupy the low 16
  bits with no tag), `ID_TYPE_GIZMO_X/Y/Z` (1/2/3 << 16), `ID_TYPE_SKYBOX`
  (4 << 16), `ID_MASK_TYPE` (0xFF0000), `ID_MASK_ENTITY` (0x00FFFF).
- `editor.h`: `MAX_CONSOLE_LINES` (10000), `MAX_CONSOLE_LINE_LENGTH` (256),
  `DRUID_PROFILE` (1), `MAX_FBOS` (5), `ID_FBO_INDEX` (4).

### Declared in editor.h, implemented elsewhere (editor.cpp — not documented here)

`doBuildAndRun`, `doStopGame`, `resetRuntimeFreeViewState`,
`snapRuntimeFreeViewToPlayerEntity`, `drawDockspaceAndPanels`,
`scanProjectArchetypes`, `syncSceneArchetypeHashesFromIDs`,
`remapSceneArchetypeIDsFromHashes`, `applySceneCameraEntityToSceneCam`,
`shutdownMaterialRegistry`, `buildShaderSourceTable`,
`reloadProjectResources`, `loadPreferredSkybox`, `editorLog`,
`initMultiFBOs`, `destroyMultiFBOs`, `renderFBOToScreen`, `modelThumbsClear`.
editor-core calls into most of these from `init`/`update`/`render`/`destroy`.

## Data Structures

### Enums / structs

- `ManipulateTransformState` (editor.h) — `MANIPULATE_POSITION/ROTATION/SCALE
  (+_MAX)`; current mode in extern `manipulateState`, set by E/R/T keys.
- `InspectorState` (editor.h) — `EMPTY_VIEW(-1)`, `ENTITY_VIEW(0)`,
  `SKYBOX_VIEW(1)`, `MAX_STATE`; extern `currentInspectorState` +
  `inspectorEntityID` select what the inspector panel (editor.cpp) shows.
- `PickType` (entitypicker.h) — `PICK_NONE/ENTITY/GIZMO_X/GIZMO_Y/GIZMO_Z/
  SKYBOX`.
- `AxisType` (entitypicker.h) — `AXIS_X/Y/Z`. Not referenced in any of these
  files.
- `PickResult` (entitypicker.h) — `{PickType type; union { u32 entityID; }}`.
  Stack value; `entityID` is the raw low-16-bit id (index + 1), only
  meaningful for `PICK_ENTITY`. A file-scope copy `result` lives in main.cpp.

### Scene archetype and column globals (defined in main.cpp)

- `Archetype sceneArchetype` — THE editor scene container (single arena
  chunk 0 used throughout). Created in `init()` with the compiled-in
  `SceneEntity` layout (defined in editor.cpp) at capacity 128
  (`entityDefaultCount`); replaced wholesale by `loadScene`'s archetype on
  startup auto-load; recreated by `migrateSceneArchetypeIfNeeded`; freed by
  `destroyArchetype` in `destroy()`. ECS-managed storage (engine arenas), but
  editor-owned lifetime — it is *not* part of a runtime `Scene`/ECS world;
  play-mode mapping happens through `ecsSlotIDs`/`syncSceneArchetypeHashesFromIDs`
  in editor.cpp.
- Column pointer globals (all extern in editor.h, defined main.cpp:358-392):
  `isActive(b8*)`, `positions(Vec3*)`, `rotations(Vec4*)`, `scales(Vec3*)`,
  `names(c8*, MAX_NAME_SIZE stride)`, `modelIDs`, `shaderHandles`,
  `entityMaterialIDs`, `archetypeIDs(u32*)`, `archetypeHashes(u64*)`,
  `sceneCameraFlags(b8*)` (declared in editor.h, defined in editor.cpp),
  `ecsSlotIDs`, `entityTags(c8*)`, `physicsBodyTypes`, `masses`,
  `colliderShapes`, `sphereRadii`, `colliderHalfXs/Ys/Zs`,
  `colliderOffsetXs/Ys/Zs`, `isLight(b8*)`, `lightTypes`, `lightRanges`,
  `lightColorRs/Gs/Bs`, `lightIntensities`, `lightDirXs/Ys/Zs`,
  `lightInnerCones`, `lightOuterCones`. These are *aliases into
  `sceneArchetype`'s arena memory* — never freed directly, invalidated by any
  archetype destroy/create, re-established only by `rebindArchetypeFields`.
  `Material *materials` is also defined here but never assigned in these
  files.
- `i32 entitySize` / `u32 entitySizeCache` (cache defined in editor.cpp) —
  UI-editable capacity vs. actual archetype capacity; divergence is supposed
  to trigger reallocation (see Open questions). `u32 entityCount` (defined
  editor.cpp) — live entity count, mirrored into
  `sceneArchetype.arena[0].count` on paste/save.

### Other main.cpp globals

- Statics: `evnt` (SDL_Event), `g_quitRequested`, `camRotateSpeed` (5.0),
  `entityDefaultCount` (128), `g_startupModelRefs` +
  `g_startupModelRefCount` (deferred model-name refs from the startup scene,
  `dfree`d after the second remap or in `destroy()`), `canBuildRun`,
  `canStop`; in `update()`: static clipboard state (`clipboardBuf[4096]`,
  `clipboardSize`, `clipboardValid`, `canCopy`, `canPaste`).
- Non-static: `cubeMesh` (heap `Mesh*` from `createBoxMesh`, freed with
  `freeMesh`), `shader` (GL program handle), `yaw`, `currentPitch`,
  `camMoveSpeed`, `materialUniforms` (`MaterialUniforms` value struct),
  `inputBoxBuffer[100]` (not used in these files — presumably editor.cpp),
  `canMoveViewPort`, `canMoveAxis`, `manipulateAxis` (world axis of the
  active gizmo drag), `cacheMouse` (last drag mouse pos), `result`
  (last `PickResult`), `canSave`.

### Hub globals (hub.cpp / hub.h)

- `Application *hubApplication` — hub app instance; allocated by
  `createApplication` in `main()`; not explicitly destroyed in these files.
- `c8 hubProjectDir[MAX_PATH_LENGTH]` — the selected/typed project root;
  written by the hub UI, read by the entire editor afterwards (scene dir,
  res dir, texture meta, archetype scan). Note: hub.h re-declares it as
  `c8 hubProjectDir[512]` (relies on `MAX_PATH_LENGTH` == 512).
- `b8 hubProjectSelected` — the hub→editor handshake flag.
- Statics: `HUB_CONFIG` (`"../projects.conf"`, newline-separated MRU list of
  project paths, ≤ `MAX_SAVED_PROJECTS` = 64 entries of `MAX_PATH_LENGTH`),
  `REQUIRED_DIRS` (`src`, `res`, `bin`, `scenes`), `savedProjects` /
  `savedCount` / `selectedProjectIdx`, `statusMsg` / `statusTimer`,
  `hubEvnt`.

### Entity picker globals (entitypicker.cpp)

- `u32 idShader`, `u32 idLocation` — GL program + `entityID` uniform for the
  ID pass; created in `initIDFramebuffer`, freed in `destroyIDFramebuffer`.
- `u32 count = 0` — file-scope, never referenced (see Open questions).

## Internal (static) functions

- `MyMalloc(size_t, void*)` / `MyFree(void*, void*)` (main.cpp) — thin
  malloc/free wrappers commented "ImGui allocator wrappers"; **never
  installed or called** in these files (no `ImGui::SetAllocatorFunctions`
  call anywhere in this file set).
- `moveCamera(f32 dt)` (main.cpp) — WASD/left-stick fly via
  `xInputAxis`/`yInputAxis` with a 0.45 dead zone.
- `saveCurrentScene()` (main.cpp) — Ctrl+S logic reused by the quit modal;
  no-op when `scenePathBuffer` is empty.
- `setStatus(msg)` (hub.cpp) — sets `statusMsg` + 4 s `statusTimer`.
- `pathBasename(path)` (hub.cpp) — last `/` or `\` component.
- `isValidProject(path)` (hub.cpp) — dir exists + all four `REQUIRED_DIRS`.
- `scaffoldProject(path)` (hub.cpp) — `createDir` for the four required dirs
  (hard-fail) plus best-effort `deps/`.
- `saveConfig()` / `loadConfig()` (hub.cpp) — write/parse `projects.conf`
  (handles `\n` and `\r\n`, `normalizePath`s each entry).
- `addToSaved(path)` (hub.cpp) — MRU: dedupe, shift, insert at front, save.
- `removeFromSaved(idx)` (hub.cpp) — remove + resave, clears selection.

## Open questions / unclear intent

1. **`reallocateSceneArena()` is an empty stub.** `update()` detects
   `entitySize != entitySizeCache`, calls it, and resyncs the cache — so
   editing the entity capacity in the UI currently changes nothing about the
   actual arena. Whether growth is meant to happen here or is handled
   elsewhere (e.g. `archetypeGrow` per ecs-storage.md) is unclear from these
   files.
2. **`MyMalloc`/`MyFree` are defined but never wired up** — no
   `ImGui::SetAllocatorFunctions` call exists in this file set. Dead code, or
   intended for a call that was removed?
3. **`migrateSceneArchetypeIfNeeded` temp arrays are capped at 32 fields**
   (`fieldCopies[32]`, `fieldSizes[32]`, `fieldNames[32][64]`) while the
   current `SceneEntity` layout has 35 fields (indices 0–34 in
   `rebindArchetypeFields`). A future scene saved with 33–34 fields that
   still needs migration would have fields ≥ 32 silently not copied (the
   copy loops guard with `i < 32`). Intended limit or leftover constant?
4. **`u32 count = 0` in entitypicker.cpp** is never read or written elsewhere
   in these files — purpose unknown.
5. **16-bit entity-id ceiling in the pick encoding.** `ID_TYPE_ENTITY` is 0,
   so an entity's pick id is just `i + 1`; if `i + 1` ever exceeded 0xFFFF
   the id would bleed into the type byte and decode as a gizmo/skybox. No
   guard exists in `renderIDPass`; presumably fine at the default capacity of
   128, but the invariant is implicit.
6. **`if (fileExists(projRes) || true)`** in `init()` — the condition is
   always true (comment says `readResources` handles a missing dir); the
   dead `fileExists` call's intent is unclear.
7. **`HUB_CONFIG = "../projects.conf"`** is CWD-relative although the comment
   says "written next to the editor binary" — whether the editor guarantees a
   specific working directory is not determinable from these files (contrast
   with the ImGui ini, which explicitly uses `SDL_GetBasePath`).
8. **`inputBoxBuffer[100]` and `Material *materials`** are defined in
   main.cpp but never used there; their consumers are presumably in
   editor.cpp (out of scope for this dispatch), so ownership intent couldn't
   be confirmed.
9. **`destroy()` nulls only the first eight column pointers** after
   `destroyArchetype`; the remaining externs (`archetypeIDs` through
   `lightOuterCones`, `ecsSlotIDs`, etc.) keep their stale values. Since this
   runs at shutdown it may be intentional, but the asymmetry with
   `rebindArchetypeFields` is unexplained.
10. **`AxisType` enum** (entitypicker.h) is declared but unused in this file
    set — possibly consumed by editor.cpp or vestigial.
11. **`resizeIDFramebuffer` is a kept-but-empty API** — retained presumably
    for source compatibility after the multi-FBO refactor; whether any
    external caller still exists couldn't be verified without reading
    editor.cpp.
