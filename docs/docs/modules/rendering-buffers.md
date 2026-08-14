# Module: rendering-buffers

Files covered:
- `src/systems/Rendering/framebuffer.c`
- `src/systems/Rendering/gbuffer.c`
- `src/systems/Rendering/geometrybuffer.c`
- `src/systems/Rendering/instancebuffer.c`
- `src/systems/Rendering/indirectbuffer.c`
- `src/systems/Rendering/modelssbo.c`
- `src/systems/Rendering/ssbo.c`
- `src/systems/Rendering/ubo.c`
- Declarations/types in `include/druid.h` (approx. lines 1302-1318, 1355-1385, 1699-1730, 1749-1804)

## Synopsis

Thin OpenGL buffer-object wrappers used by the Druid renderer: offscreen render
targets (`Framebuffer`, deferred-shading `GBuffer`), a single mega VBO/EBO for
all static mesh geometry (`GeometryBuffer`), per-instance transform storage
(`InstanceBuffer`, `ModelSSBO`), multi-draw indirect command batching
(`IndirectBuffer`), and generic raw SSBO/UBO helpers (`ssbo.c`, `ubo.c`
including the `CoreShaderData` camera/time UBO). Instances of these types are
owned by the global `Renderer` (`renderer->modelSSBO`, `renderer->indirectBuffer`,
`Buffer instanceBuffers`, `Buffer gBuffers`, `u32 coreUBO`, `u32 lightSSBO`) and
by `resources->geoBuffer`; the wrappers themselves depend only on the GL API,
the tagged allocator (`dalloc`/`dfree`), logging macros, and profiler counters
(`profileCountFBOBind`, `profileCountBufferUpload`). Nothing here is
ECS-managed; all state is plain heap/stack plus GL-driver-owned GPU objects.

All exported functions below are declared with `DAPI` in `include/druid.h`
(there are no per-module headers). Callers listed as "external" were not found
inside the files above and are presumed to live in renderer/mesh/system code.

## Public API Reference

### Framebuffer (framebuffer.c)

- `Framebuffer createFramebuffer(u32 width, u32 height, GLenum internalFormat, b8 hasDepth)`
  Creates an FBO with one 2D color texture (`GL_NEAREST` filtering) and,
  if `hasDepth`, a `GL_DEPTH24_STENCIL8` renderbuffer. Logs `ERROR` on
  incomplete framebuffer but still returns the struct by value. Side effects:
  GL object creation, binds/unbinds `GL_FRAMEBUFFER`. Callers: external
  (likely renderer/post-processing).
- `void resizeFramebuffer(Framebuffer *fb, u32 width, u32 height)`
  Reallocates the color texture and depth RBO storage at the new size; no-op
  if `fb` is NULL or size unchanged. Callers: external (likely window resize).
- `void bindFramebuffer(Framebuffer *fb)`
  Binds `fb->fbo`, sets `glViewport(0,0,w,h)`, increments profiler FBO-bind
  counter. Callers: external.
- `void unbindFramebuffer(void)`
  Binds framebuffer 0 (default backbuffer); increments profiler counter.
  Does not restore any viewport. Callers: external.
- `void destroyFramebuffer(Framebuffer *fb)`
  Deletes texture/RBO/FBO if nonzero and zeroes the handles. Does not free
  `fb` itself (value type). Callers: external.

### GBuffer (gbuffer.c)

- `GBuffer createGBuffer(u32 width, u32 height)`
  Builds a deferred-shading MRT framebuffer: RT0 `positionTex`
  (`GL_RGBA16F`, position.rgb + metallic.a per comment), RT1 `normalTex`
  (`GL_RGBA16F`, normal.rgb + roughness.a), RT2 `albedoSpecTex` (`GL_RGBA8`,
  albedo.rgb + transparency.a), plus `depthTex` (`GL_DEPTH_COMPONENT24`
  texture attachment). Sets `glDrawBuffers` to the 3 color attachments and
  logs `ERROR` if incomplete. Returns by value. No bind/destroy/resize
  counterparts exist in this module (see Open questions). Callers: external
  (druid.h shows `rendererAcquireGBuffer(Renderer*, w, h)` as the likely
  wrapper).

### GeometryBuffer (geometrybuffer.c)

One global VAO+VBO+EBO for all static, indexed mesh geometry; interleaved
32-byte vertices (`GEO_VERTEX_STRIDE`): Vec3 position @0, Vec2 texCoord @12,
Vec3 normal @20. Attribute locations 0/1/2 match that order.

- `GeometryBuffer *geometryBufferCreate(u32 maxVertices, u32 maxIndices)`
  Heap-allocates the struct (`dalloc`, `MEM_TAG_GEOMETRY_BUFFER`), creates
  VAO/VBO/EBO with `GL_DYNAMIC_DRAW` storage sized `maxVertices * 32` and
  `maxIndices * 4` bytes, configures the 3 vertex attributes. Returns NULL on
  zero capacities or alloc failure. druid.h comment: must run after the GL
  context exists; `resources->geoBuffer` is created in `initSystems()`
  (external). Callers: external.
- `void geometryBufferDestroy(GeometryBuffer *buf)`
  Deletes VAO/VBO/EBO and `dfree`s the struct. Callers: external.
- `b8 geometryBufferUpload(GeometryBuffer *buf, Mesh *mesh, const void *interleavedVertices, u32 vertexCount, const u32 *indices, u32 indexCount)`
  Appends one mesh's vertex/index data via `glBufferSubData` at the current
  write cursors. Returns false on NULL args, non-indexed input
  (`indexCount == 0 || !indices` — comment says skybox/quad must use
  standalone VAOs), or capacity overflow (`WARN`). Side effects: mutates the
  passed `Mesh` (`baseVertex`, `firstIndex`, `drawCount`, `vao` = shared VAO,
  `vbo`/`ebo` = 0, `buffered` = true), advances `buf->vertexCount`/`indexCount`,
  calls `profileCountBufferUpload` per upload, unbinds both buffer targets.
  Callers: external (druid.h comment: `initMeshFromModel()` uploads here when
  `resources->geoBuffer` is non-NULL).

### InstanceBuffer (instancebuffer.c)

Double-buffered (`INST_BUF_COUNT == 2`) persistently mapped SSBO of `Mat4`
instance transforms; CPU writes one buffer while GPU reads the other, with
`GLsync` fences guarding reuse.

- `void instanceBufferCreate(InstanceBuffer *buf, u32 capacity)`
  Initializes caller-provided struct (no allocation of `buf` itself). Creates
  2 GL buffers via `glBufferStorage` (`WRITE | PERSISTENT | DYNAMIC_STORAGE`),
  persistently maps each with `GL_MAP_FLUSH_EXPLICIT_BIT`, size
  `capacity * sizeof(Mat4)`. On any map failure: unmaps/deletes everything
  already created, leaves `ready = false`, `data = NULL`. On success:
  `data = maps[0]`, `ready = true`. Callers: external (likely behind
  `rendererAcquireInstanceBuffer`).
- `void instanceBufferAdvance(InstanceBuffer *buf)`
  Rotates `writeIdx` to the next buffer, then `glClientWaitSync`s on that
  buffer's fence (50 ms, then a 500 ms retry on timeout — blocking is stated
  as intended), deletes the fence, points `data` at the new mapping, resets
  `count` to 0. Callers: external (per-frame renderer path).
- `void instanceBufferFlushRange(InstanceBuffer *buf, u32 offset, u32 count)`
  `glFlushMappedBufferRange` on the current write buffer for `count` Mat4s at
  Mat4-`offset`; silently returns on out-of-range. Deliberately leaves the
  buffer bound to `GL_SHADER_STORAGE_BUFFER` ("caller may still need the
  binding active"). Callers: external.
- `void instanceBufferDestroy(InstanceBuffer *buf)`
  Deletes fences, unmaps and deletes both GL buffers, zeroes all fields.
  Does not free `buf` (caller-owned storage). Callers: external.

Note: no function in this file creates fences (`glFenceSync`) — fence
insertion after draws must happen elsewhere (see Open questions).

### IndirectBuffer (indirectbuffer.c)

CPU staging array + GPU `GL_DRAW_INDIRECT_BUFFER` for
`glMultiDrawElementsIndirect`. `STATIC_ASSERT(sizeof(IndirectCommand) == 20)`
enforces the GL command layout.

- `IndirectBuffer *indirectBufferCreate(u32 maxCommands)`
  Heap-allocates the struct and a `maxCommands`-sized `IndirectCommand`
  staging array (`MEM_TAG_RENDERER`); defaults `maxCommands` to 256 if 0.
  Creates the GPU buffer with `GL_DYNAMIC_DRAW`. Callers: external
  (`renderer->indirectBuffer`).
- `void indirectBufferDestroy(IndirectBuffer *buf)`
  Deletes GL buffer, `dfree`s staging array and struct. Callers: external.
- `void indirectBufferReset(IndirectBuffer *buf)`
  Zeroes `commandCount` (per-frame reset). Callers: external.
- `b8 indirectBufferAddCommand(IndirectBuffer *buf, u32 modelID, u32 firstIndex, u32 indexCount, u32 baseVertex, u32 baseInstance)`
  Appends one command (`count = indexCount`, `instanceCount = 1`,
  `baseVertex` cast to i32). Returns false when full. The `modelID` parameter
  is not used in the function body. Callers: external.
- `void indirectBufferUpload(IndirectBuffer *buf)`
  `glBufferSubData`s the built commands to the GPU buffer, bound via
  `GL_COPY_WRITE_BUFFER`. No-op if empty. Callers: external.
- `void indirectBufferDispatch(IndirectBuffer *buf, u32 shaderProgram)`
  `glUseProgram(shaderProgram)`, binds `GL_DRAW_INDIRECT_BUFFER`, issues one
  `glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, 0, commandCount, 0)`,
  unbinds. No-op if empty or `shaderProgram == 0`. Callers: external.

### ModelSSBO (modelssbo.c)

Persistently mapped SSBO of per-draw model matrices ("binding point 2" per
druid.h comment; shader indexes it via `gl_BaseInstance` / `u_modelIndex`).
A code comment explains the choice of `GL_MAP_FLUSH_EXPLICIT_BIT` over
coherent mapping: batching writes into one flush avoids write-combining
stalls when writing 1M+ Mat4s per frame.

- `ModelSSBO *modelSSBOCreate(u32 capacity)`
  Heap-allocates struct (`MEM_TAG_RENDERER`), creates immutable storage
  (`glBufferStorage`, WRITE|PERSISTENT|DYNAMIC_STORAGE) of
  `capacity * sizeof(Mat4)` and persistently maps it into `ssbo->data`.
  Full cleanup + NULL return on map failure. Callers: external
  (`renderer->modelSSBO`).
- `void modelSSBODestroy(ModelSSBO *ssbo)` — unmap, delete buffer, `dfree`
  struct. Callers: external.
- `void modelSSBOBeginFrame(ModelSSBO *ssbo)` — resets `count` to 0.
  Callers: external.
- `u32 modelSSBOWrite(ModelSSBO *ssbo, const Transform *t)`
  Writes `getModel(t)` (transform → Mat4, external function) into the next
  slot of the mapped memory; returns the slot index, or `(u32)-1` on NULL
  args or full buffer (with `ERROR`). Callers: external.
- `void modelSSBOUpload(ModelSSBO *ssbo)`
  Only issues `glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT)` — no data
  transfer (data is written through the persistent map). Callers: external.
- `void modelSSBOEndFrame(ModelSSBO *ssbo)`
  `glFlushMappedBufferRange` for the `count` matrices written this frame.
  Callers: external.
- `void modelSSBOBind(ModelSSBO *ssbo, u32 bindingPoint)`
  Delegates to `bindSSBOBase(ssbo->buffer, bindingPoint)` (ssbo.c — the one
  intra-module call site in this file set). Callers: external.

### Raw SSBO helpers (ssbo.c)

Handle-based (plain `u32` GL name), no struct.

- `u32 createSSBO(u32 size, const void *data, GLenum usage)` — gen + 
  `glBufferData` on `GL_SHADER_STORAGE_BUFFER`; returns GL name. Callers:
  external (likely `renderer->lightSSBO`, cf. `rendererGetLightSSBO`).
- `void updateSSBO(u32 ssbo, u32 offset, u32 size, const void *data)` —
  `glBufferSubData`; no-op for handle 0. Callers: external.
- `void bindSSBOBase(u32 ssbo, u32 bindingPoint)` — `glBindBufferBase`;
  no-op for handle 0. Callers: `modelSSBOBind` (modelssbo.c); others external.
- `void destroySSBO(u32 ssbo)` — `glDeleteBuffers`; no-op for handle 0.
  Callers: external.

### UBO helpers + core shader UBO (ubo.c)

Include-path note (flagged in Stage 0, confirmed): ubo.c line 1 is
`#include "../include/druid.h"`, while every sibling in this directory uses
`"../../../include/druid.h"`. Relative to `src/systems/Rendering/`, that
quoted path would resolve to a nonexistent `src/systems/include/druid.h`, so
compilation must be relying on a compiler include search path (e.g. an `-I`
pointing at the repo root or similar) rather than file-relative resolution.
Observation only; not verified against the build scripts (out of scope).

- `u32 createUBO(u32 size, const void *data, GLenum usage)` — gen +
  `glBufferData` on `GL_UNIFORM_BUFFER`; returns GL name. Callers:
  `createCoreShaderUBO` (this file); others external.
- `void updateUBO(u32 ubo, u32 offset, u32 size, const void *data)` —
  `glBufferSubData`; no-op for handle 0. Callers: `updateCoreShaderUBO`
  (this file); others external.
- `void bindUBOBase(u32 ubo, u32 bindingPoint)` — `glBindBufferBase`;
  no-op for handle 0. Callers: `createCoreShaderUBO` (this file); others
  external.
- `u32 createCoreShaderUBO(void)`
  Lazily creates the singleton `g_coreUBO` (zero-initialized
  `CoreShaderData`, `GL_DYNAMIC_DRAW`) and binds it to UBO binding point 0.
  Idempotent (returns existing handle). Callers: `updateCoreShaderUBO`
  (lazy init); likely external too (`renderer->coreUBO` field exists).
- `void updateCoreShaderUBO(f32 timeSeconds, const Vec3 *camPos, const Mat4 *view, const Mat4 *projection)`
  Fills a stack `CoreShaderData` and uploads the whole struct via
  `updateUBO`. `view`/`projection` are NULL-checked (leaving those fields
  uninitialized stack garbage when NULL), but `camPos` is dereferenced
  unconditionally. Callers: external (druid.h comment: `rendererBeginFrame`
  "pushes active camera + time into the core UBO").

Also declared in druid.h next to this API but NOT defined in any file in this
module: `void freeUBO(u32 ubo)` and
`void updateFrameUBOData(const Camera *camera, f32 deltaTime)`.

## Data Structures

- `Framebuffer` (druid.h ~1775): `fbo`, `texture`, `rbo`, `width`, `height`,
  `internalFormat` (GLenum), `hasDepth` (b8). Value type; the struct lives in
  caller stack/heap storage while the GL objects are driver-owned, created by
  `createFramebuffer` and released by `destroyFramebuffer`. Not ECS-managed.
- `GBuffer` (druid.h ~1794): `fbo`, `positionTex`, `normalTex`,
  `albedoSpecTex`, `depthTex`, `width`, `height`. Value type returned by
  `createGBuffer`; stored in the Renderer's `gBuffers` `Buffer` collection
  (external). No destructor in this module. Not ECS-managed.
- `GeometryBuffer` (druid.h ~1751): `vao` (shared by all buffered meshes),
  `vbo`, `ebo`, `maxVertices`/`maxIndices` (capacities), `vertexCount`/
  `indexCount` (append cursors — never rewound; upload is append-only).
  Heap-allocated by `geometryBufferCreate` with tag `MEM_TAG_GEOMETRY_BUFFER`,
  freed by `geometryBufferDestroy`; owned by `resources->geoBuffer` per
  druid.h. Not ECS-managed.
- `GEO_VERTEX_STRIDE` (macro, 32u): interleaved vertex byte stride
  (Vec3 + Vec2 + Vec3).
- `InstanceBuffer` (druid.h ~1702): `data` (alias of `maps[writeIdx]`, the
  live CPU write pointer), `buffer[2]` (GL names), `maps[2]` (persistent
  mappings), `fences[2]` (`void*` holding `GLsync`, typed as void* "to avoid
  GL header here"), `capacity`, `count` (instances written this frame),
  `writeIdx`, `ready` (b8, false until creation fully succeeds). Struct
  storage is caller-owned (create/destroy take a pointer and never
  malloc/free it); GPU storage created in `instanceBufferCreate`. Renderer
  keeps them in its `instanceBuffers` `Buffer`. Not ECS-managed.
- `INST_BUF_COUNT` (macro, 2): double-buffering depth.
- `IndirectCommand` (druid.h ~1370): `count`, `instanceCount`, `firstIndex`,
  `baseVertex` (i32), `baseInstance` — exact GL
  `DrawElementsIndirectCommand` layout, static-asserted to 20 bytes.
- `IndirectBuffer` (druid.h ~1379): `buffer` (GL name), `commands` (CPU
  staging array), `commandCount`, `maxCommands`. Struct and staging array
  heap-allocated with `MEM_TAG_RENDERER` by `indirectBufferCreate`, freed by
  `indirectBufferDestroy`; owned by `renderer->indirectBuffer`. Not
  ECS-managed.
- `ModelSSBO` (druid.h ~1304): `data` (persistent Mat4 mapping), `buffer`
  (GL name), `capacity`, `count` (per-frame write cursor). Heap-allocated
  (`MEM_TAG_RENDERER`) by `modelSSBOCreate`, freed by `modelSSBODestroy`;
  owned by `renderer->modelSSBO`. Not ECS-managed.
- `CoreShaderData` (file-local struct in ubo.c): `f32 camPos[3]`, `f32 time`,
  `Mat4 view`, `Mat4 projection`. Commented as "std140 friendly" (vec3+float
  pack into one 16-byte slot ahead of the matrices). Stack-instantiated per
  update; not visible outside ubo.c.
- `static u32 g_coreUBO` (ubo.c): singleton GL handle for the core UBO,
  lazily created, bound to UBO binding 0, never deleted within this module.
- Raw SSBOs/UBOs from ssbo.c/ubo.c have no wrapper struct — ownership of the
  `u32` handle is entirely the caller's.

## Internal (static) functions

- ubo.c: `g_coreUBO` is the only `static` item (a variable, not a function).
- No static functions exist in any of the eight .c files.

## Open questions / unclear intent

1. `freeUBO(u32)` is declared in druid.h (line 1545) but has no definition in
   ubo.c or any other file in this module; consequently `g_coreUBO` has no
   visible release path. Defined elsewhere, or unimplemented?
2. `updateFrameUBOData(const Camera*, f32)` is declared alongside the UBO API
   (druid.h line 1539, under "New optimized functions for UBO rendering") but
   is not defined in these files. Implemented elsewhere or a dead declaration?
3. ubo.c's include is `"../include/druid.h"` versus the siblings'
   `"../../../include/druid.h"` — it must resolve via a compiler include
   directory, not file-relative lookup. Whether this is intentional or a
   leftover from the file living in another directory is unclear from these
   files.
4. `indirectBufferAddCommand` accepts a `modelID` parameter that is never
   used in the body (the command stores only the other four values plus
   `instanceCount = 1`). Intended for a mapping that was removed, or future
   use?
5. `GBuffer` has create-only API here: no destroy/bind/resize counterpart in
   this module, so where `positionTex`/`normalTex`/`albedoSpecTex`/`depthTex`/
   `fbo` are deleted (presumably `rendererReleaseGBuffer`) cannot be
   confirmed from these files.
6. `modelSSBOUpload` only issues a `glMemoryBarrier` while the actual flush
   lives in `modelSSBOEndFrame`; the intended per-frame ordering of
   `Upload` vs `EndFrame` (and why both exist) is not determinable from
   modelssbo.c alone.
7. `updateCoreShaderUBO` NULL-checks `view`/`projection` (leaving those
   struct fields as uninitialized stack data when NULL) but dereferences
   `camPos` unconditionally — whether NULL `camPos` is a supported call is
   unclear. A commented-out `data.pad0/1/2 = 0` line also hints the
   `CoreShaderData` layout changed at some point.
8. instancebuffer.c consumes and deletes `fences[]` (in
   `instanceBufferAdvance`/`Destroy`) but never creates them — the
   `glFenceSync` insertion point after GPU reads must be elsewhere (likely
   the renderer's flush path) and can't be confirmed from this file set.
