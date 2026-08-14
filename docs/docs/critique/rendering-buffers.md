# Critique: rendering-buffers

## Structural

- **[structural][READ-VERIFIED] InstanceBuffer fence API is half-external.**
  `instancebuffer.c` consumes/deletes fences (`instanceBufferAdvance` 63-74,
  `instanceBufferDestroy` 97-101) but never creates them; renderer.c:1050 and
  :1305 reach directly into `ibuf->fences[ibuf->writeIdx]` with raw
  `glFenceSync`. Any other caller must know the double-buffer internals or the
  sync scheme silently does nothing. Needs an `instanceBufferSignal()`-style
  entry point.

## Moderate

- **[moderate][READ-VERIFIED] Create-by-value with no failure signal.**
  `createFramebuffer` (framebuffer.c:29-36) and `createGBuffer` (gbuffer.c:52-60)
  log ERROR on incomplete FBO but return a normal-looking struct; callers
  (e.g. renderer.c:309) cannot detect failure.
- **[moderate][READ-VERIFIED] Module-boundary confusion around UBO API.**
  `freeUBO` is declared in the UBO block (druid.h:1545) but defined in
  shader.c:181, not ubo.c. `updateFrameUBOData` (druid.h:1539) has **no
  definition anywhere in src/** — dead declaration; any call is a link error
  [NEEDS-BUILD for the link error itself].
- **[moderate][READ-VERIFIED] `g_coreUBO` stale after shutdown.** renderer.c:227
  calls `freeUBO(r->coreUBO)` but ubo.c's static `g_coreUBO` stays nonzero, so
  a later `createCoreShaderUBO`/`updateCoreShaderUBO` reuses a deleted GL name
  [NEEDS-RUNTIME to observe].
- **[moderate][READ-VERIFIED] `updateCoreShaderUBO` uploads stack garbage.**
  ubo.c:59-62 NULL-checks `view`/`projection` but leaves those `CoreShaderData`
  fields uninitialized when NULL and still uploads the whole struct; `camPos`
  is dereferenced unconditionally (ubo.c:64).
- **[moderate][READ-VERIFIED] `modelSSBOUpload` is dead and semantically wrong.**
  No callers anywhere in src/; its `glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT)`
  (modelssbo.c:77) guards shader writes, not CPU persistent-map writes — the
  real flush is `modelSSBOEndFrame`.
- **[moderate][READ-VERIFIED] `indirectBufferAddCommand` `modelID` unused**
  (indirectbuffer.c:57-72); renderer.c:1261 passes `mid`, silently discarded.
- **[moderate][READ-VERIFIED] GBuffer has create-only API; teardown copy-pasted.**
  No `destroyGBuffer`; renderer.c deletes the four textures + fbo inline twice
  (:205-219 and :315-328) — encapsulation leak, easy to miss a texture.
- **[moderate][READ-VERIFIED] `instanceBufferAdvance` proceeds after timeout.**
  Second 500 ms wait result (instancebuffer.c:70) is assigned but never checked;
  on timeout/`GL_WAIT_FAILED` the fence is deleted and CPU writes over data the
  GPU may still read [NEEDS-RUNTIME for actual corruption].
- **[moderate][READ-VERIFIED] Silent-failure paths.** `instanceBufferFlushRange`
  returns without log on out-of-range (instancebuffer.c:84) — GPU then reads
  stale transforms; `geometryBufferUpload`'s non-indexed rejection
  (geometrybuffer.c:81-82) returns false with no log, indistinguishable from
  "buffer full".
- **[moderate][READ-VERIFIED] `createFramebuffer` hardcodes `GL_RGBA`/`GL_UNSIGNED_BYTE`**
  (framebuffer.c:16, :47) while `internalFormat` is parameterized — allocation
  fails (`GL_INVALID_OPERATION`) for integer internal formats [NEEDS-RUNTIME].
- **[moderate][READ-VERIFIED] GeometryBuffer is append-only** — cursors never
  rewind (geometrybuffer.c:124-125) and there is no reset/free-range API, so
  unloading a model leaks its slice of the mega-VBO for the process lifetime.
- **[moderate][READ-VERIFIED] ubo.c include path anomaly.** ubo.c:1 is
  `#include "../include/druid.h"` vs siblings' `"../../../include/druid.h"`;
  file-relative it resolves to nonexistent `src/systems/include/druid.h`, so
  the build depends on an `-I` search path [NEEDS-BUILD].

## Trivial

- **[trivial][READ-VERIFIED] Dead defensive re-checks** in `geometryBufferUpload`:
  lines 90 and 104 re-test `indexCount > 0 && indices` after line 81 already
  guaranteed both.
- **[trivial][READ-VERIFIED] Copy-paste duplicates:** ssbo.c and ubo.c
  create/update/bindBase trios differ only in target enum.
- **[trivial][READ-VERIFIED] NULL-check inconsistency:** `instanceBufferCreate`
  and `instanceBufferDestroy` dereference `buf` unchecked; `Advance`/`FlushRange`
  guard it.
- **[trivial][READ-VERIFIED] Unused `<stdlib.h>`** in geometrybuffer.c:2 and
  indirectbuffer.c:2 (only `memset` from string.h is used).
- **[trivial][READ-VERIFIED] Slop residue in ubo.c:** stray bare block around
  the camPos writes (:63-67) and commented-out `data.pad0/1/2` line (:68) from
  an older struct layout; `GBuffer gb;` (gbuffer.c:6) uninitialized vs
  framebuffer.c's `{0}` style.

## Categories with no findings

Cache-hostile layout / missing SoA: none (buffers are already flat GPU-side
arrays; structs are small and hot paths write straight into persistent maps).
