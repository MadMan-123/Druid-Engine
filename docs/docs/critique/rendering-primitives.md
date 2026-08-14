# Critique: rendering-primitives

## Correctness / error handling

- [moderate][READ-VERIFIED] `shader.c:21` `checkShaderError`: `ERROR("s\n Error: %s \n", errorMessage, error)` — one `%s` for two args. The GL info log (`error`) is never printed, so every compile/link failure loses its actual diagnostic; output also starts with a literal "s".
- [moderate][READ-VERIFIED] `shader.c:30` `createShader`: `ERROR("Error type creation failed %s\n", type)` passes a `u32` to `%s` — undefined behavior (likely crash) on the exact path meant to report an error.
- [structural][READ-VERIFIED] Shader failure never propagates: `checkShaderError` returns void, `createShader` returns the object name even when compilation failed, so `createGraphicsProgram`/`WithGeometry` link broken stages and return a nonzero program. Callers cannot detect a bad shader; `return 0` only happens on file-load failure.
- [moderate][READ-VERIFIED] `shader.c:68` `createComputeProgram`: `free(code)` frees `fileData->data` (legal — `file.c:35` mallocs it) but the `FileData` struct itself (`file.c:51`) leaks on every call, and on the compute path per shader load. Live code: called from `resourcemanager.c:344` and `mesh.c:366`. Should use `freeFileData(fileData)`.
- [moderate][READ-VERIFIED] `shader.c:130-136` (and `:258-263`): if `dalloc` fails, the NULL text is logged then passed to `createShader`, where `strlen(NULL)` crashes. Defensive check that doesn't actually defend.
- [moderate][READ-VERIFIED] `camera.c:39` `initCamera` passes `fov` raw to `mat4Perspective(fovRadians,...)` (druid.h:290) while storing it in `fovDeg`; `cameraSetPerspective`/`cameraSetFov` apply `radians()`. Testbed (`testbed/src/main.cpp:8,30`) passes 70.0 — a 70-*radian* frustum. Same raw pass-through via `rendererAcquireCamera` (`renderer.c:257`). [NEEDS-RUNTIME] for the visual symptom.
- [moderate][READ-VERIFIED] `gizmo.c:47-65` `compileGizmoShader` does zero error checking despite `checkShaderError` living in the same module. A GLSL failure yields silent black/no gizmos. [NEEDS-RUNTIME] to observe.
- [moderate][READ-VERIFIED] `display.c:6-12` `returnError` prints malformed `"/n"`, blocks on `getchar()`, calls `SDL_Quit()`, then *returns* — `initDisplay` continues to `glewInit`/GL calls with no window/context.
- [moderate][NEEDS-RUNTIME] `display.c:32` `SDL_Init(SDL_INIT_EVENTS | SDL_INIT_GAMEPAD)` omits `SDL_INIT_VIDEO` before `SDL_CreateWindow`; return value also unchecked.
- [trivial][NEEDS-RUNTIME] `texture.c:82-95` cubemap upload uses `GL_RGB` for 3-channel faces without setting `GL_UNPACK_ALIGNMENT` — row misalignment for widths not divisible by 4.

## Dead / redundant code

- [moderate][READ-VERIFIED] `shader.c:46` `createProgram`: declared druid.h:1516, zero call sites repo-wide; attaches without linking, so it's also useless as-is. Dead.
- [trivial][READ-VERIFIED] `shader.c:360` `updateShaderTime`: defined, not declared in druid.h, no callers anywhere. Dead.
- [trivial][READ-VERIFIED] `texture.c:103` `stbi_image_free(data)` in the branch where `data == NULL` — no-op.
- [trivial][READ-VERIFIED] `texture.c:141` `assert(unit >= 0 && ...)` on unsigned `unit` — always-true half.
- [trivial][READ-VERIFIED] `gizmo.c:110/130` `gizmo.count = 0` in both begin and end frame — one is redundant.

## AI-slop / duplication / consistency

- [moderate][READ-VERIFIED] `shader.c:88-178` vs `189-312`: `createGraphicsProgram` and `...WithGeometry` are ~120 lines of copy-paste with drifted details (immediate `freeFileData` vs deferred at `:254-256`; `GLuint` vs `u32` in the UBO-binding blocks `:169` vs `:304`). Should share one N-stage helper.
- [moderate][READ-VERIFIED] `gizmo.c:343` locally re-declares `CylState` mirroring physics' private `CylinderState` (`collider.c:13`). Layouts match today; silent breakage if physics changes. Needs a `getCylinderHalfHeight` accessor.
- [moderate][READ-VERIFIED] `texture.c:5-6` `extern u32 djb2Hash(...); extern b8 equals(...)` borrowed from resourcemanager.c — layering inversion (rendering → application), and `equals` is a collision-prone global symbol name.
- [trivial][READ-VERIFIED] `camera.c`: `cameraSet*` NULL-check, `initCamera`/`pitch`/`moveForward` don't — inconsistent defensiveness on the same struct.

## Performance

- [moderate][READ-VERIFIED] `shader.c:334,351` `updateShaderModel` calls `glGetUniformLocation` (string lookup) per draw, every frame, for `u_modelIndex` and possibly `model`. Locations should be cached at program creation. [NEEDS-RUNTIME] for measured cost.
- [trivial][READ-VERIFIED] `shader.c:321` `updateShaderMVP` copies a whole `Camera` (Mat4 + fields) by value only to ignore it.

## API ergonomics

- [trivial][READ-VERIFIED] `shader.c:315` `freeShader` deletes a *program*; name invites calling it on stage shaders.
- [trivial][READ-VERIFIED] `camera.c:103` `cameraSetOrthographic` leaves `fovDeg/aspect` stale, so a later `cameraSetFov` silently snaps back to perspective.
- [trivial][READ-VERIFIED] `gizmo.c:138` batch overflow drops vertices with no warning; combined with the unchecked `dalloc` at `gizmo.c:74`, gizmos can vanish or crash silently.

## Memory

Covered above (`createComputeProgram` FileData leak). No other leaks found; gizmo batch is a one-time 1.75 MB allocation, acceptable.
