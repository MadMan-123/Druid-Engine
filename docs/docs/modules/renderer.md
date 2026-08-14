# Module: renderer

Source: `src/systems/Rendering/renderer.c` (1,175 lines)
Public declarations: `include/druid.h` (~lines 1348–1508: `Renderer` struct, `Frustum`/`FrustumPlane`, `IndirectBuffer`, `GPULight`, all `renderer*` declarations)

## Synopsis

The renderer is the engine's central drawing context. It owns Buffer-based slot pools of `Camera`s, `InstanceBuffer`s (double-buffered persistent-mapped model-matrix SSBOs) and `GBuffer`s, plus the core UBO, a per-draw model SSBO, an indirect-command buffer, and a light SSBO. It provides two draw paths — an ECS archetype path with OpenMP-parallel frustum/sub-pixel culling (`rendererDefaultArchetypeRender`) and an ad-hoc instanced submission queue (`rendererSubmitInstance` / `rendererFlushInstancedModels`) — plus a deferred G-buffer pipeline (Begin/End/LightingPass). It sits on top of the rendering-buffers module (`bufferCreate`/`instanceBufferCreate`/`createGBuffer`/`modelSSBO*`/`indirectBuffer*`, see docs/modules/rendering-buffers.md) and rendering-primitives (`drawMesh`, `updateMaterial`, shaders, cameras), and is consumed by the ECS render callback (`ECSSystemRenderFn`) and the runtime loop (`runtimeUpdate` calls `rendererBeginFrame` per druid.h comments).

### Per-frame call sequence (as designed)

1. `rendererBeginFrame(r, dt)` — advance time, clear `frameVisible`, upload active camera + time to the core UBO, extract and cache the frustum once, rotate/fence-wait all instance-buffer SSBOs (`instanceBufferAdvance`), reset+bind model SSBO (binding 2), reset the indirect buffer, rebind light SSBO (binding 3).
2. Draw submission — either:
   - ECS path: `rendererDefaultArchetypeRender(arch, r)` per archetype (called via `ECSSystemRenderFn` when a plugin's render fn is NULL — likely from the ECS module, external to this file), which culls, batches by ModelID, fills the write-side instance SSBO, and issues instanced draws immediately; or
   - Direct path: N × `rendererSubmitInstance(r, modelID, &t)` then one `rendererFlushInstancedModels(r)`, which groups by modelID and draws via `glMultiDrawElementsIndirect` (or a per-model instanced-draw fallback).
3. Deferred: wrap geometry submission in `rendererBeginDeferredPass` / `rendererEndDeferredPass`, then run `rendererLightingPass(r, lightingShader)` to shade a fullscreen quad from the G-buffer and blit its depth back for later forward passes.
4. `rendererUploadLights` can be called any time to refresh the light SSBO (binding 3).

## Public API Reference

All functions are `DAPI`-exported and declared in druid.h.

### Lifecycle

- **`Renderer *createRenderer(Display *display, f32 fov, f32 nearClip, f32 farClip, u32 maxCameras, u32 maxIBuffers, u32 maxGBuffers)`**
  Allocates the global renderer (via `dalloc`, `MEM_TAG_RENDERER`), creates the camera/instance-buffer/G-buffer slot pools (defaults 8/8/4 if 0 passed), the core UBO (`createCoreShaderUBO`), the model SSBO (`modelSSBOCreate(RENDERER_MAX_INSTANCE)`), an indirect buffer (256 commands), and acquires a default instance buffer with fallback sizing (1M → 256K → 64K instances, dropping from a 128 MB persistent-mapped SSBO to 16 MB then 4 MB). Enables deferred rendering at display resolution. Sets the global `renderer` pointer. Returns NULL on display/alloc/pool failure; SSBO/indirect/instance-buffer failures only WARN and degrade features. Note: the `fov`/`nearClip`/`farClip` parameters are never read in the body — no default camera is created (`activeCamera = (u32)-1`; callers must acquire one). Caller: external (engine init / runtime).

- **`void destroyRenderer(Renderer *r)`**
  Destroys GPU resources of every occupied instance buffer and G-buffer, frees the three pools, the core UBO, model SSBO, indirect buffer, and light SSBO; clears the global `renderer` if it matches; `dfree`s the struct. Caller: external (shutdown).

### Frame

- **`void rendererBeginFrame(Renderer *r, f32 dt)`**
  Per-frame setup (see sequence above). Side effects: resets `s_lastBoundVAO` cache, `r->time += dt`, `r->frameVisible = NULL`, sets `r->hasFrustum` only if the active camera slot is occupied (calls `getView(cam, false)`, `updateCoreShaderUBO`, `getViewProjection`, `frustumExtract`), calls `instanceBufferAdvance` on every occupied instance buffer (rotates write index, waits on GPU fence), `modelSSBOBeginFrame`+`modelSSBOBind(..., 2)`, `indirectBufferReset`, and `glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, r->lightSSBO)`. Caller: external — druid.h documents `runtimeUpdate` calls it.

- **`void rendererFlushInstances(Renderer *r, u32 ibufferIndex, u32 shaderProgram)`**
  Binds the given instance buffer's current write-side SSBO to binding point 1 and does `glUseProgram(shaderProgram)`. Despite the name it issues **no draw calls**. No callers within this file; external callers presumed.

### ECS archetype rendering

- **`void rendererDefaultArchetypeRender(Archetype *arch, Renderer *r)`**
  Default render fn for archetypes with no custom `ECSSystemPlugin.render`. Requires `resources` (global resource manager) and `r->defaultShader` != 0. Resolves SoA field indices by name via `getRenderFieldCache` (see Data Structures): needs `PositionX/Y/Z` (f32), a rotation (combined `Rotation` Vec4 **or** split `RotationX/Y/Z/W` f32s — split preferred), a scale (`Scale` Vec3 **or** `ScaleX/Y/Z`), `ModelID` (u32); `Alive` (b8) is optional.
  **Instanced path** (requires a ready default instance buffer and a `u_modelBaseIndex` uniform in the shader):
  - *Step 1 (parallel)*: per chunk, `#pragma omp parallel for schedule(static) if(count > 50000)` computes a per-entity visibility byte: dead/invalid-model check (models with id ≥ `MODEL_LUT_SIZE` (4096) or ≥ `resources->modelUsed` are marked invisible), sphere frustum test (`frustumTestSphere` with radius = max scale component × `Model.boundingRadius`), and a sub-pixel cull (`MIN_PIXEL_SIZE` 1.5 px: culled when `(rad*pixelFactor)^2 < 1.5^2 * distSq`, where `pixelFactor = projection.m[1][1] * screenHeight`). Each thread writes only `visBuf[i]` — no shared state. Visibility is also memcpy'd into `r->frameVisible` (frame-allocated) so physics can read cull results.
  - *Step 2 (sequential)*: visible entities are assigned to model groups via a 4096-entry `modelToGroup` u16 LUT (O(1) lookup, max 256 groups), packing `(chunk << 20) | entityIndex` into `groupIndices` plus a per-entity group tag.
  - *Scatter*: O(visible) counting-sort into `sortedBuf` so each group is contiguous.
  - *Phase B (parallel)*: `#pragma omp parallel if(totalVisible > 50000)` writes a fused TRS matrix (inline quaternion-to-matrix, scale folded into columns, translation in row 3) per visible entity directly into `ibuf->data[ibuf->count + j]`; each thread caches its last chunk's field pointers.
  - *Draw*: `instanceBufferFlushRange`, then per group: set `u_modelBaseIndex` to the group's SSBO base, and per mesh of the model `updateMaterial` + `drawMeshInstanced`. Finally resets `u_modelBaseIndex` to -1 and places a `glFenceSync` in `ibuf->fences[writeIdx]` for next frame's `instanceBufferAdvance`.
  **Per-entity fallback** (`per_entity_fallback:` label — reached when no instancing shader/buffer, allocation failure, or SSBO overflow): iterates entities, applies the same frustum + pixel culls (scalar), builds a `Transform`, writes it via `modelSSBOWrite` + `u_modelIndex` uniform when available (else `updateShaderModel`), and calls `drawMesh` per mesh. Caller: ECS system dispatch (external; matches `ECSSystemRenderFn` typedef).

### Direct instanced submission

- **`void rendererSubmitInstance(Renderer *r, u32 modelID, const Transform *t)`**
  Appends `(modelID, getModel(t))` to the static `s_submitList` (cap `SUBMIT_MAX_TOTAL` = 131072) and bumps the matching `s_submitGroups` entry (linear scan, cap 256 groups; over-cap submissions are dropped). No GL calls. Intended once per entity per frame (druid.h usage comment). Callers: external (particles/asteroids/foliage per header comment).

- **`void rendererFlushInstancedModels(Renderer *r)`**
  Drains the submission queue: assigns contiguous SSBO base offsets per group starting at `ibuf->count`, scatters each submitted matrix into its group's slot in the default instance buffer's write side, `instanceBufferFlushRange`, `modelSSBOEndFrame` (if model SSBO exists), binds shader + SSBO binding 1, then:
  - **Indirect path** (if `r->indirectBuffer`): `indirectBufferReset`, adds one `IndirectCommand` per mesh per group (`firstIndex`/`drawCount`/`baseVertex`/`baseInstance = group SSBO base`), calling `updateMaterial` during command build, then `indirectBufferUpload` + `indirectBufferDispatch(shader)`.
  - **Fallback path**: per group sets `u_modelBaseIndex = ssboBase + group.base` and calls `drawMeshInstanced` per mesh; places a `glFenceSync` afterward (the indirect path does not place a fence).
  Resets the queue counters. Bails (and clears the queue) if no shader, no ready default instance buffer, missing `u_modelBaseIndex`, or capacity overflow. Callers: external (once per frame per header comment).

### Slot acquire/release wrappers

All return a slot index or `(u32)-1`; all delegate to the generic `Buffer` pool (rendering-buffers module).

- **`u32 rendererAcquireCamera(Renderer *r, Vec3 pos, f32 fov, f32 aspect, f32 nearClip, f32 farClip)`** — acquires a camera slot and `initCamera`s it. Callers: external.
- **`void rendererReleaseCamera(Renderer *r, u32 index)`** — releases the slot; if it was the active camera, `activeCamera` falls back to 0 (occupancy of slot 0 not checked). Callers: external.
- **`u32 rendererAcquireInstanceBuffer(Renderer *r, u32 capacity)`** — acquires a slot and `instanceBufferCreate`s it. Called by `createRenderer` (default-buffer sizing loop); also external.
- **`void rendererReleaseInstanceBuffer(Renderer *r, u32 index)`** — `instanceBufferDestroy` + slot release. Called by `createRenderer` (retry loop); also external.
- **`u32 rendererAcquireGBuffer(Renderer *r, u32 width, u32 height)`** — acquires a slot and fills it with `createGBuffer(w, h)`. Called by `rendererEnableDeferred`; also external.
- **`void rendererReleaseGBuffer(Renderer *r, u32 index)`** — deletes the G-buffer's FBO and four textures directly with GL calls, releases the slot. Called by `rendererEnableDeferred`, `rendererDisableDeferred`; also external.

### Deferred pipeline

- **`b8 rendererEnableDeferred(Renderer *r, u32 width, u32 height)`** — releases any existing active G-buffer, acquires a new one at the given size, sets `activeGBuffer` and `useDeferredRendering = true`. Called by `createRenderer` (on by default at display resolution); also external (e.g. on resize).
- **`void rendererDisableDeferred(Renderer *r)`** — releases the active G-buffer, clears `activeGBuffer`/`useDeferredRendering`. No callers in this file; external.
- **`void rendererBeginDeferredPass(Renderer *r)`** — binds the active G-buffer FBO, enables draw buffers COLOR_ATTACHMENT0..2 (position/normal/albedoSpec), clears color+depth. No-op if no active G-buffer. Callers: external (frame loop).
- **`void rendererEndDeferredPass(Renderer *r)`** — binds framebuffer 0. Callers: external.
- **`void rendererLightingPass(Renderer *r, u32 lightingShader)`** — full-screen shading pass: uses `lightingShader`, caches the `gPosition`/`gNormal`/`gAlbedoSpec`/`envMap` uniform locations per shader (function-local statics), binds the three G-buffer textures to units 0–2 and `r->envMapTex` cubemap (or 0) to unit 3, rebinds the light SSBO at binding 3, disables depth test, draws a lazily-created static fullscreen quad (function-local static `quadVAO`/`quadVBO`, never freed), re-enables depth, then blits the G-buffer depth into the current draw FBO (queried via `GL_DRAW_FRAMEBUFFER_BINDING`) so later forward passes (skybox, gizmos) depth-test correctly. Callers: external.

### Camera state

- **`void rendererSetActiveCamera(Renderer *r, u32 index)`** — sets `activeCamera` (WARNs and refuses if slot unoccupied). Callers: external.
- **`Camera *rendererGetCamera(Renderer *r, u32 index)`** — returns the camera pointer or NULL. Callers: external.
- **`u32 rendererGetActiveCamera(Renderer *r)`** — returns `activeCamera` or `(u32)-1`. Callers: external.

### Lights

- **`void rendererUploadLights(Renderer *r, const GPULight *lights, u32 count)`** — clamps `count` to `MAX_LIGHTS` (128); lazily creates the light SSBO (`glBufferData(sizeof(LightSSBOData), GL_DYNAMIC_DRAW)`); stages a `LightSSBOData` in frame-allocator memory (`frameAlloc` — no free needed, see core-memory doc); uploads header (16 bytes: `count` + 3-u32 pad) plus `count` lights via `glBufferSubData`; binds to SSBO binding point 3; sets `r->lightCount`. Callers: external (game/lighting system).
- **`u32 rendererGetLightSSBO(Renderer *r)`** — returns the GL buffer name (0 if none/NULL). Callers: external.

### Global

- **`DAPI extern Renderer *renderer`** — global singleton set by `createRenderer`, cleared by `destroyRenderer` (only if it still points at the destroyed instance). Referenced externally (e.g. physics reading `renderer->frameVisible`, per in-code comment).

## Data Structures

### `Renderer` (druid.h ~1393) — heap, `dalloc`/`dfree` with `MEM_TAG_RENDERER`; not ECS-managed
- `display` — borrowed `Display *` (window/GL context; owned elsewhere, header says "owned by Renderer" for Display module but createRenderer only stores the pointer).
- `modelSSBO` — owned `ModelSSBO *` (per-draw transforms, binding 2); created/destroyed by create/destroyRenderer. May be NULL (feature degraded).
- `indirectBuffer` — owned `IndirectBuffer *` (256 commands); may be NULL.
- `cameras` / `instanceBuffers` / `gBuffers` — generic `Buffer` slot pools of `Camera` / `InstanceBuffer` / `GBuffer` elements. Pools owned by Renderer; slot contents' GPU resources destroyed in `destroyRenderer` and the release wrappers.
- `frustum` (`Frustum`) + `hasFrustum` — per-frame cached Gribb-Hartmann planes, valid only when an occupied active camera existed at `rendererBeginFrame`.
- `activeCamera`, `activeGBuffer`, `defaultIBuffer` — slot indices, `(u32)-1` = none.
- `coreUBO`, `defaultShader`, `envMapTex`, `lightSSBO` — GL object names. `defaultShader` and `envMapTex` are set externally (never assigned non-zero in this file). `lightSSBO` lazily created by `rendererUploadLights`, deleted in `destroyRenderer`.
- `time` — accumulated seconds, fed to the core UBO.
- `lightCount` — number of lights last uploaded.
- `useDeferredRendering` — flag toggled by Enable/DisableDeferred; not read anywhere in this file (consumers external).
- `frameVisible` / `frameVisibleCount` — per-entity visibility bytes from the last archetype cull, allocated with `frameAlloc` (valid one frame only, reset each `rendererBeginFrame`; no explicit free). Consumed externally ("physics LOD" per comment).

### `Frustum` / `FrustumPlane` (druid.h ~1390) — plain value types
Six normalized planes `{a,b,c,d}`; inside test is `dot(plane, p) >= -radius`. Embedded in Renderer; also usable standalone.

### `IndirectBuffer` / `IndirectCommand` (druid.h ~1370) — heap via rendering-buffers module
Standard GL 5-u32 indirect command (`count`, `instanceCount`, `firstIndex`, `baseVertex`, `baseInstance` used as model-matrix SSBO base). Owned by Renderer; lifecycle via `indirectBufferCreate/Destroy` (rendering-buffers).

### `GPULight` (druid.h ~1494) — plain value type, std430-compatible (64 bytes)
`posX/Y/Z, range, colorR/G/B, intensity, dirX/Y/Z, innerCone, outerCone, type, _pad[2]`. `type` ∈ `LIGHT_TYPE_POINT`(0) / `DIRECTIONAL`(1) / `SPOT`(2). Caller-owned arrays; copied into the SSBO on upload.

### `LightSSBOData` (renderer.c, file-private)
`{ u32 count; u32 _pad[3]; GPULight lights[MAX_LIGHTS]; }` — GPU-side layout of SSBO binding 3. Staged per-upload in frame-allocator memory.

### `RenderFieldCache` (renderer.c, file-private)
Cached SoA field indices for one `StructLayout` (posX/Y/Z, combined vs split rotation/scale, ModelID, Alive; `splitRot`/`splitScale` flags — split fields preferred when both exist). Stored in static array `s_renderFieldCaches[32]` keyed by layout pointer; never evicted (returns NULL when full, which silently skips rendering that archetype).

### `SubmitGroup` / `SubmitEntry` (renderer.c, file-private)
Static submission queue for the direct path: `s_submitList[131072]` of `(modelID, Mat4)` plus `s_submitGroups[256]` of `(modelID, base, count)`. Static storage, reset by `rendererFlushInstancedModels`. `SUBMIT_MAX_TOTAL` must be ≤ `RENDERER_MAX_INSTANCE` (comment).

### Other file-scope statics
- `s_lastBoundVAO` — VAO redundant-bind cache, reset each `rendererBeginFrame`.
- Function-local statics: cached uniform locations in `rendererLightingPass`, `rendererDefaultArchetypeRender`, `rendererFlushInstancedModels` (keyed on shader id, one shader remembered each); lazily-grown `malloc`ed scratch buffers `groupIndices`, `entityGroupTag`, `sortedBuf`, `visBuf` (never freed — process-lifetime caches); the lighting-pass fullscreen `quadVAO`/`quadVBO`.

### Packed index format (instanced path)
`(chunk << 20) | entityIndex` in a u32 → max 1,048,576 entities per chunk (20 bits) and 4,096 chunks (12 bits).

## Internal (static) functions

- `frustumExtract(const Mat4 *vp, Frustum *out)` — Gribb-Hartmann plane extraction from a column-major view-projection matrix; normalizes planes for sphere distance tests.
- `frustumTestSphere(const Frustum *f, cx, cy, cz, r)` — returns false if the sphere is fully outside any of the 6 planes.
- `drawMeshInstanced(Mesh *mesh, u32 instanceCount)` — instanced draw honoring the `s_lastBoundVAO` cache; uses `glDrawElementsInstancedBaseVertex` for meshes in the shared mega-buffer (`mesh->buffered`), else `glDrawElementsInstanced`/`glDrawArraysInstanced`; updates `g_drawCalls`/`g_triangles`/`g_vertices` and `profileCountVAOBind` (profiler globals, defined externally).
- `getRenderFieldCache(StructLayout *layout)` — find-or-build a `RenderFieldCache` by strcmp'ing field names once per layout.

## Open questions / unclear intent

1. `createRenderer` accepts `fov`, `nearClip`, `farClip` but never uses them (no default camera is created; `activeCamera` starts at `(u32)-1`). Possibly vestigial signature.
2. `rendererFlushInstances` only binds an SSBO and a shader — it never draws and has no callers in this file. Its intended role in the frame (relative to `rendererFlushInstancedModels`) is unclear.
3. In `rendererFlushInstancedModels`'s fallback path, the uniform is set to `ssboBase + s_submitGroups[g].base`, but `base` was already assigned as `ssboBase + running` earlier — the offset appears to be added twice. Whether shaders compensate for this can't be determined from this file.
4. The indirect path of `rendererFlushInstancedModels` calls `updateMaterial` per mesh **while building** the command list, then dispatches all commands in one `glMultiDrawElementsIndirect` — so only the last-set material state applies to the whole dispatch. Also, no `glFenceSync` is placed on this path (only the fallback path fences), while the archetype path always fences. Intent unclear.
5. `modelSSBOEndFrame` is called only inside `rendererFlushInstancedModels`; the per-entity fallback of `rendererDefaultArchetypeRender` writes via `modelSSBOWrite` but nothing in this file ends/flushes the model SSBO if `rendererFlushInstancedModels` isn't called that frame. Whether an external caller handles this is unknown.
6. `rendererReleaseCamera` falls back to `activeCamera = 0` without checking that slot 0 is occupied; `rendererBeginFrame` tolerates it (occupancy-checked), but whether 0 is a conventional "main camera" slot is not stated.
7. In the instanced archetype path, entities with `ModelID >= 4096` (`MODEL_LUT_SIZE`) are marked invisible and never drawn, while the per-entity fallback path renders them (only bounded by `resources->modelUsed`). Whether >4095 model IDs are ever expected is unclear.
8. `r->frameVisible` is a flat concatenation of per-chunk visibility in chunk-iteration order; the contract by which physics maps entities to indices in this array is defined outside these files. Also, if the instanced path bails to the fallback mid-way, `frameVisible` may be partially filled — consumers' handling is unknown.
9. Empty braces block at renderer.c ~line 699 with comment "One-shot diagnostic: print instancing state on first call" — dead placeholder, intent removed.
10. `getView(cam, false)` — the meaning of the second (bool) parameter is not visible from these files.
