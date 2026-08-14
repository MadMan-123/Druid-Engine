# Critique: application-runtime

Files: `src/application/application.c`, `src/systems/runtime.c`

## Dead / redundant code
- [trivial][READ-VERIFIED] `u32 fps = 0;` (application.c:7) never read or written again; actual FPS uses `f64 FPS` + `f32 frameCount`. Delete.
- [trivial][READ-VERIFIED] `previousTime` seeded twice: initSystems (application.c:110) then immediately re-seeded in startApplication (application.c:118). One is redundant.
- [trivial][READ-VERIFIED] Timing globals (`performanceFreq`, `previousTime`, `frameCount`, application.c:3-5) are non-`static` file-scope with external linkage for no reason — symbol pollution.

## AI-slop patterns
- [trivial][READ-VERIFIED] Res-path probe `fileExists("./"RES_FOLDER"shader.vert") ? ... : ...` copy-pasted in initSystems (application.c:93) and destroyApplication (application.c:41).
- [trivial][READ-VERIFIED] runtimeDrawScene_ (runtime.c:43-44) re-checks loop-invariant pointers `sceneRuntime->isActive` / `sceneRuntime->modelIDs` every iteration; should early-out once before the loop.
- [trivial][READ-VERIFIED] Naming: three FPS spellings in one file (`FPS`, `fps`, `app->fps`); `f32 frameCount` for an integer counter.

## Memory / ownership
- [moderate][READ-VERIFIED] Standalone renderer leak: runtimeCreate calls `createRenderer` (runtime.c:102) but `destroyRenderer` has **zero callers in src/** (only its definition at renderer.c:191). runtimeDestroy frees skybox/shaders but never the renderer it created.
- [moderate][READ-VERIFIED] Double-free hazard: startApplication frees the app itself via destroyApplication on loop exit (application.c:147), yet `destroyApplication` is also DAPI-exported (druid.h:2333). A caller doing `run(app); destroyApplication(app);` double-frees; ownership is undocumented in the header.
- [trivial][READ-VERIFIED] `Application` uses raw `calloc`/`free` while `GameRuntime` uses tagged `dalloc`/`dfree` — Application bypasses the memory tracker (partly forced: it exists before memorySystemInit).

## Perf
none significant (getMaterialUniforms is cached; scene draw is small-N).

## API ergonomics / error handling
- [structural][READ-VERIFIED] No engine-owned SDL event pump: the only `SDL_PollEvent` in the codebase is `processInput` (input.c:167), reached solely via the optional `app->inputProcess` hook. If a game forgets `app->inputProcess = processInput`, the loop has no QUIT path and the OS marks the window unresponsive — `while (app->state != EXIT)` can never exit. [NEEDS-RUNTIME to observe the hang; absence is read-verified.]
- [structural][READ-VERIFIED] runtimeRegisterArchetype/runtimeUpdate flush logic is doubly broken: (a) the flush (runtime.c:162) is gated on `!rt->physRegistered`, but a single successful immediate registration sets `physRegistered = true` (runtime.c:149) — any archetypes still queued are stranded forever; (b) non-`ARCH_PHYSICS_BODY` archetypes fall into the pending queue (runtime.c:152) and the flush calls `physRegisterArchetype` unconditionally, which **force-sets** `ARCH_PHYSICS_BODY` (physics.c:1413) — render-only archetypes become simulated physics bodies.
- [moderate][READ-VERIFIED] Silent drops: pending queue overflow past 32 discards the archetype with no log (runtime.c:152-153); `findInMap(..., "default", &idx)` return ignored at runtime.c:106 and :207, so a missing "default" shader silently uses `shaderHandles[0]`; `createGraphicsProgram` results (runtime.c:123-126) unchecked.
- [moderate][READ-VERIFIED] runtime.c hard-codes `./res/...` for skybox (runtime.c:27,33) and deferred shaders (runtime.c:123-126), ignoring both `projectDir` and application.c's `./res` vs `../res` probe — running from a build dir silently loses skybox and deferred pipeline (falls back per the `rt->gbufferShader` guard at runtime.c:186).
- [moderate][NEEDS-RUNTIME] destroyApplication re-probes the res path at shutdown (application.c:41); if cwd changed during the run, `textures.meta` saves to a different location than it loaded from.
- [trivial][NEEDS-BUILD] `assert` is the only failure handling in createApplication/initSystems (application.c:19,83); NDEBUG builds proceed into NULL derefs.
- [trivial][READ-VERIFIED] `inputUpdate` (application.c:9) is exported but undeclared in druid.h — either `static` it or declare it.
