# application-runtime

Files: `src/application/application.c`, `src/systems/runtime.c`
Public declarations: `include/druid.h` (Application section ~lines 2148, 2311-2339; Game Runtime section ~lines 2341-2421)

## Synopsis

The application layer owns the top-level app lifecycle: engine subsystem bring-up (`initSystems`), the fixed per-frame loop (`startApplication`), and orderly teardown (`destroyApplication`). It is driven entirely through four user-supplied callbacks (`init`/`update`/`render`/`destroy`) plus an optional `inputProcess` hook, so it has no knowledge of game content. The runtime layer (`runtime.c`) sits above it as the game-facing facade used by generated game projects and editor play mode: it wires together the scene (`sceneRuntime`), prefab registry, physics (`physicsWorld`), renderer, deferred pipeline shaders, and skybox, exposed through the `GameRuntime` singleton. Both layers depend on nearly every other engine module (memory, logging, display, input, audio, resources, renderer, physics, ECS scene/prefab); external callers are game `main` functions, the editor, and generated game plugins.

## Public API Reference

### Application (application.c)

**`Application *createApplication(const c8* title, FncPtr init, FncPtrFloat update, FncPtrFloat render, FncPtr destroy)`**
Allocates and populates an `Application`.
- Params: window title (copied, truncated to `MAX_PATH_LENGTH-1`), four required callbacks (all asserted non-NULL).
- Returns: heap `Application*` (plain `calloc`, asserted non-NULL). Sets `state = RUN`.
- Side effects: none beyond allocation. Does NOT set `width`/`height` — callers must assign those fields directly (or `initSystems` defaults them to 1920x1080).
- Callers: external only (game main / editor / hub; mentioned in `docs/modules/build-system-testbed.md` and `editor-project-builder.md`).

**`void run(Application *app)`**
Convenience entry point: `initSystems(app)` then `startApplication(app)`.
- Callers: external (game main / editor launchers).

**`void initSystems(const Application *app)`**
Boots all engine subsystems, in this exact order:
1. Memory: `memDefaultConfig()` → `memLoadConfig("memory.conf")` falling back to `"../memory.conf"` → `memorySystemInit`.
2. Logging: `initLogging()`.
3. Resources: `createResourceManager(8192 materials, 2048 textures, 8192 meshes, 2048 models, 128 shaders, 256 audio)` into global `resources` (asserted).
4. Profiler: `profileCalibrate()`.
5. Display: `initDisplay(app->title, width, height)` — width/height default to 1920/1080 when `app->width/height == 0`. (Input init is not visible here; per `docs/modules/core-input.md` it happens inside `initDisplay`.)
6. Shared geometry buffer: `resources->geoBuffer = geometryBufferCreate(2000000, 6000000)`; WARNs and continues on failure (meshes fall back to standalone VAO/VBO/EBO).
7. Asset load: res path probe (`./res/shader.vert` exists → `./`RES_FOLDER, else `../`RES_FOLDER) → `readResources(resources, resPath)` → `loadTextureMetadata("<resPath>textures.meta")`.
8. Audio: `audioInit()`.
9. Game: `app->init()`.
10. `SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST)` — discards events queued during init or left over from a previous application run (hub window-destroy, stale focus-loss).
11. Timer seed: `previousTime`/`performanceFreq` from SDL performance counter.
- Callers: `run`. Exported via DAPI (referenced by `core-input.md`, `asset-loading.md`, `rendering-buffers.md` docs).

**`void startApplication(Application *app)`**
Runs the main loop until `app->state == EXIT`, then calls `destroyApplication(app)`. Per-frame order:
1. `frameReset()` — frame allocator reset (top of frame, before anything else).
2. `profileBeginFrame()`.
3. dt computed from `SDL_GetPerformanceCounter()` delta / `performanceFreq`.
4. `PROFILE_SCOPE("Input")`: `inputUpdate(app)` → optional `app->inputProcess(app)` then `updateInputAxes()`.
5. `PROFILE_SCOPE("Update")`: `app->update(dt)` — game logic. Note: there is no engine-driven physics step here; physics is stepped inside game code via `runtimeUpdate` (see below).
6. `PROFILE_SCOPE("Audio")`: `audioPump()`.
7. `PROFILE_SCOPE("Render")`: `applicationRenderStep(app, dt)` — clear, `app->render(dt)`, swap.
8. `profileEndFrame()`.
9. FPS accounting: once per second `FPS = frameCount / elapsed`; `app->fps = FPS` every frame.
- Side effects: loop exit path frees the app. `EXIT` is set externally (by `processInput` in the input module on quit events).
- Callers: `run`; referenced externally per `core-input.md`.

**`void applicationRenderStep(Application *app, f32 dt)`**
One render step: `clearDisplay(0,0,0,1)` → `app->render(dt)` → `swapBuffer(renderer ? renderer->display : display)` (prefers the renderer's display if a global renderer exists).
- Callers: `startApplication`. DAPI-exported (also referenced in `core-input.md`).

**`void destroyApplication(Application *app)`**
Teardown, in order: `saveTextureMetadata("<resPath>textures.meta")` (same `./res` vs `../res` probe as init) → `app->destroy()` → `audioShutdown()` → `onDestroy(display)` → `shutdownLogging()` → `cleanUpResourceManager(resources)` → `memorySystemShutdown()` → `free(app)`.
- Callers: `startApplication` on loop exit; also DAPI-exported for direct use.

**`void inputUpdate(Application *app)`** *(exported symbol but NOT declared in druid.h)*
Calls `app->inputProcess(app)` if set, then `updateInputAxes()`.
- Callers: `startApplication` only (within these files).

### Game Runtime (runtime.c) — all `DAPI`

**`RuntimeConfig runtimeDefaultConfig(void)`**
Returns a zeroed `RuntimeConfig` with defaults: gravity (0,-9.81,0), physicsTimestep 1/60, camFov 70, camNear 0.1, camFar 1000, camStartPos (0,2,8), camAspect 16/9.
- Callers: external (generated game projects / editor; referenced in `editor-project-builder.md`, `editor-ui-1-renderpath.md`).

**`GameRuntime *runtimeCreate(const c8 *projectDir, RuntimeConfig cfg)`**
Creates the `runtime` singleton (returns existing one if already created). Allocated with `dalloc(..., MEM_TAG_GAME)`. `standaloneMode = (strcmp(projectDir, ".") == 0)`. Flow:
- Scene: `sceneRuntimeInit(projectDir)` if `sceneRuntime` absent/unloaded (editor play mode never pre-loads it).
- Prefabs: `prefabRegistryCreate()` then `prefabLoadDirectory("<projectDir>/prefabs")`.
- Standalone only:
  - If no global `renderer` but a `display` exists: `createRenderer(display, cfg.camFov, cfg.camNear, cfg.camFar, 8, 16, 8)`, sets `r->defaultShader` from the `"default"` entry in `resources->shaderIDs`, acquires+activates a camera from `cfg`, `rendererEnableDeferred` at screen size.
  - Physics: `physInit(cfg.gravity, cfg.physicsTimestep)`; `runtime->world = physicsWorld`; `physAutoRegisterScene(&sceneRuntime->data)`.
  - Deferred shaders: `createGraphicsProgram("./res/gbuffer.vert|frag")` and `"./res/deferred_lighting.vert|frag"`.
  - Skybox: `runtimeLoadSkybox_` (silently skipped if any of the six `./res/Textures/Skybox/*.jpg` faces is missing).
- Both modes: caches `runtime->renderer`, `runtime->scene`, and `runtime->camera` (via `bufferGet(&renderer->cameras, renderer->activeCamera)`).
- Returns NULL only if `dalloc` fails.
- Callers: external — generated game plugin `init` (standalone) and editor play mode (per `ecs-scene-prefab-systems.md`, `editor-project-builder.md`).

**`void runtimeRegisterArchetype(GameRuntime *rt, Archetype *arch)`**
Registers a gameplay archetype for physics. If `physicsWorld` exists and the archetype has `ARCH_PHYSICS_BODY` flag: `physRegisterArchetype` immediately and set `rt->physRegistered`. Otherwise queues it in `rt->pendingArchetypes` (cap `RUNTIME_MAX_PENDING_ARCHETYPES` = 32; silently dropped past the cap). Non-flagged archetypes also land in the pending queue (see Open questions).
- Callers: external (game `gameInit`; referenced in `ecs-storage.md`, `ecs-scene-prefab-systems.md`).

**`void runtimeUpdate(GameRuntime *rt, f32 dt)`**
Per-frame runtime tick, intended to be called at the END of the game's update callback (so the camera has already been moved before the UBO upload). Flow:
1. Refreshes `rt->scene = sceneRuntime`.
2. Deferred physics registration: once `physicsWorld` becomes available (editor creates it after plugin.init), flushes all `pendingArchetypes` via `physRegisterArchetype`.
3. Standalone only: `physWorldStep(physicsWorld, dt)` then `rendererBeginFrame(renderer, dt)` (camera + time → UBO). In editor mode the function returns after step 2.
- Callers: external (game update; referenced in `renderer.md`, `physics-world.md`).

**`void runtimeBeginScenePass(GameRuntime *rt, f32 dt)`**
Begins the scene geometry pass (`dt` unused). No-op unless standalone with a renderer. If deferred rendering is on and `rt->gbufferShader` exists: `rendererBeginDeferredPass`, swaps `renderer->defaultShader` to the G-buffer shader, `glUseProgram(gbufferShader)`. Then `runtimeDrawScene_()` draws all active `sceneRuntime` entities. Game archetype renders are expected between Begin and End.
- Callers: external (game render callback).

**`void runtimeEndScenePass(GameRuntime *rt)`**
Ends the pass. No-op unless standalone with a renderer. If deferred: `rendererEndDeferredPass` → `rendererLightingPass(renderer, rt->lightingShader)` → restores `renderer->defaultShader` to the `"default"` resource shader (for gizmos/UI drawn afterwards). Then draws the skybox if loaded (raw GL: `GL_LEQUAL` depth func, depth writes off, cube-map bind, 36-vertex draw, state restored).
- Callers: external (game render callback).

**`void runtimeDestroy(GameRuntime *rt)`**
Standalone only: `physShutdown()`, `sceneRuntimeDestroy()`, frees skybox mesh/texture/shader and both deferred shaders (`freeMesh`/`freeTexture`/`freeShader`). Both modes: `prefabRegistryDestroy()`, `dfree(rt, ..., MEM_TAG_GAME)`, `runtime = NULL`.
- Callers: external (game plugin shutdown / editor stop-play).

**`b8 changeScene(GameRuntime *rt, const c8 *sceneName)`**
Loads `<rt->projectDir>/scenes/<sceneName>` via `sceneRuntimeLoad` (header example: `"level2.drsc"` — name includes extension). On success updates `rt->scene` and re-runs `physAutoRegisterScene(&sceneRuntime->data)`. Returns false on NULL args or load failure. Safe mid-game.
- Callers: external (game code, level transitions).

**`Camera *getActiveCamera(GameRuntime *rt)`**
Re-fetches and caches `rt->camera` from `rt->renderer->cameras` at `activeCamera`; returns NULL if `rt` or its renderer is NULL. Use after the renderer's active camera slot changes.
- Callers: external (game code).

## Data Structures

**`enum ApplicationState`** (druid.h:2148) — `RUN`, `EXIT`. Loop control; `EXIT` set externally by `processInput`.

**`Application`** (druid.h:2312-2324) — plain heap (`calloc` in `createApplication`, `free` in `destroyApplication`). Not ECS-managed. Fields:
- `init/update/render/destroy` — required game callbacks.
- `inputProcess (void(*)(void*))` — optional per-frame input hook, invoked by `inputUpdate` before `updateInputAxes`; not set by `createApplication` (caller assigns).
- `title[MAX_PATH_LENGTH]` — window title copy.
- `state` — RUN/EXIT.
- `width`, `height` (f32) — window size; 0 means "default 1920x1080" in `initSystems`. No setter; assigned directly by callers.
- `fps` (f64) — updated every frame from the once-per-second FPS average.

**`FncPtr` / `FncPtrFloat`** (druid.h:2327-2328) — `void(*)()` and `void(*)(f32)` callback typedefs.

**`RuntimeConfig`** (druid.h:2361-2370) — by-value config struct: `gravity`, `physicsTimestep`, `camFov/camNear/camFar/camStartPos/camAspect`. Stack/value semantics; copied into `GameRuntime.config`.

**`GameRuntime`** (druid.h:2372-2397) — singleton, allocated with `dalloc(MEM_TAG_GAME)` in `runtimeCreate`, freed with `dfree` in `runtimeDestroy`. Not ECS-managed. Fields:
- `projectDir[MAX_PATH_LENGTH]`, `config`, `standaloneMode`.
- Borrowed engine references (read-only for game code): `renderer` (owned by runtime in standalone — created but destroyed nowhere in these files, see Open questions; borrowed in editor), `world` (= global `physicsWorld`, owned in standalone via `physInit`/`physShutdown`), `scene` (= global `sceneRuntime`), `camera` (pointer into `renderer->cameras` Buffer).
- Skybox: `skyboxMesh` (Mesh*, freed by `freeMesh`), `skyboxTex` (GL cube-map handle), `skyboxShader` (GL program) — standalone only, auto-loaded.
- Deferred shaders: `gbufferShader`, `lightingShader` (GL programs) — standalone only.
- Deferred archetype registration: `pendingArchetypes[32]`, `pendingCount`, `physRegistered`.

**Globals**
- `GameRuntime *runtime` (runtime.c:4, `DAPI extern` in druid.h:2399) — the singleton; NULL when not running.
- `f64 FPS` (application.c:6; `extern double FPS` druid.h:2311) — rolling once-per-second FPS average, readable engine-wide.
- File-scope in application.c: `performanceFreq` (f64), `previousTime` (u64), `frameCount` (f32) — timing state; `u32 fps = 0` — declared but never used in this file.
- `#define RUNTIME_MAX_PENDING_ARCHETYPES 32` (druid.h:2359).
- application.c-local `#define`s for resource pool sizes: `MATERIAL_COUNT 8192`, `TEXTURE_COUNT 2048`, `MESH_COUNT 8192`, `MODEL_COUNT 2048`, `SHADER_COUNT 128`, `AUDIO_COUNT 256`.
- External globals consumed (owned elsewhere): `display`, `resources`, `renderer`, `sceneRuntime`, `physicsWorld`.

## Internal (static) functions

runtime.c:
- `runtimeLoadSkybox_(GameRuntime *rt)` — probes the six `./res/Textures/Skybox/{right,left,top,bottom,front,back}.jpg` faces; if all exist, creates cube-map texture, skybox mesh, and `./res/Skybox.vert|frag` program. Silently returns on any missing face.
- `runtimeDrawScene_(void)` — iterates `sceneRuntime` entities (active + valid `modelIDs`), builds a `Transform` from positions/rotations/scales (defaulting to `v3Zero`/`quatIdentity`/`v3One` for missing arrays), `updateShaderModel` with `renderer->defaultShader`, applies per-entity material override (`materialIDs[id] != (u32)-1`) via `getMaterialUniforms` + `updateMaterial`, and `drawMesh` for each mesh of the model.

application.c has no static functions (`inputUpdate` is non-static; see API section).

## Open questions / unclear intent

1. `u32 fps = 0;` (application.c:7) is never read or written after initialization — purpose unknown (the actual FPS uses `f64 FPS` and `f32 frameCount`).
2. `inputUpdate` is a non-static exported symbol but is not declared in druid.h; unclear whether any external caller exists or it is simply un-`static`ed by oversight.
3. In standalone mode `runtimeCreate` creates the `Renderer`, but `runtimeDestroy` never calls `destroyRenderer` — who owns renderer teardown in the standalone flow is not visible from these files (the header comment says "created by standalone launcher via the global display singleton", suggesting the launcher may own it).
4. `runtimeRegisterArchetype` queues archetypes WITHOUT the `ARCH_PHYSICS_BODY` flag into `pendingArchetypes`, and `runtimeUpdate` later passes them to `physRegisterArchetype` unconditionally — whether non-physics archetypes are intended to reach physics registration (or the flag check on the deferred path was intentionally omitted) is unclear.
5. Skybox and pipeline shader paths in runtime.c are hard-coded to `./res/...` regardless of `projectDir` and regardless of the `./res` vs `../res` probe used by application.c — intent for non-cwd project layouts unclear.
6. `runtimeCreate` caches `rt->camera` via `bufferGet` at creation; whether that pointer stays valid across later `rendererAcquireCamera`/`rendererReleaseCamera` calls (vs. requiring `getActiveCamera` refresh) depends on Buffer semantics not visible here.
7. Where `initInput` is called is not visible in these files (docs for core-input indicate `initDisplay` performs it).
8. `destroyApplication` re-probes the resource path with `fileExists("./res/shader.vert")` at shutdown — if the working directory changed during the run, metadata could save to a different location than it loaded from; intended behavior unknown.

## Unclaimed druid.h declarations

Best-effort sweep of `DAPI` function declarations in `include/druid.h` against all 18 docs in `docs/modules/` (including this one). Functions no module doc mentions:

- `initShader(const c8 *filename)` (druid.h:1512) — declared in the shader block but absent from every module doc; possibly legacy/never-defined.
- `physOverlapBox` (druid.h:2662) — `physOverlapSphere` is documented in physics-world.md; the box variant is never mentioned.
- `profileCountUniformUpload` (druid.h:2285) and `profileCountBufferBind` (druid.h:2284) — core-platform.md documents the profiler family (and names ShaderBind/TextureBind/VAOBind/FBOBind/BufferUpload/Alloc/Free explicitly, plus a generic "profileCount" mention), but these two members are never named anywhere.

Everything else declared in druid.h is claimed by at least one module doc (math/SIMD → core-math; files/DLL/logging/profiler → core-platform; memory/hashmap/Buffer → core-memory; input → core-input; display/shaders/textures/camera/gizmos → rendering-primitives; UBO/SSBO/instance/indirect/geometry/framebuffer/GBuffer → rendering-buffers; renderer → renderer; mesh/model/material → mesh-model-material; OBJ/resources → asset-loading; EntityArena/Archetype → ecs-storage; scene/prefab/ECS-DLL/codegen → ecs-scene-prefab-systems; colliders → physics-shapes; physics world/runtime wrappers → physics-world; audio → audio; Application/GameRuntime → this doc).
