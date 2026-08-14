# Critique: mesh-model-material

## Dead / redundant code
- [moderate][READ-VERIFIED] `freeMeshArray` (mesh.c:22) — not declared in druid.h, zero callers repo-wide. Dead. Also diverges from `freeMesh` (guards `vao != 0`; `freeMesh` doesn't) — copy-paste drift.
- [moderate][READ-VERIFIED] `loadMaterialFromAssimp` (material.c:410, DAPI at druid.h:1658) — zero callers; duplicates the inline material loop in `loadModelFromAssimp` (model.c:72-92). Vestigial.
- [trivial][READ-VERIFIED] `Mesh.subMeshCount` (druid.h:1641) — never read or written anywhere. Dead field padding a hot struct.
- [trivial][READ-VERIFIED] `generateHeightMap`/`createTerrainIndices`/`calculateTerrainNormals`/`createTerrainVertices` (mesh.c:362-627) — external linkage, no druid.h declarations, only internal callers. Should be `static`.
- [trivial][READ-VERIFIED] Stale comment druid.h:1634 says vao is "0 when buffered", but geometrybuffer.c:119 sets `mesh->vao = buf->vao`; the module doc repeats the falsehood.
- [trivial][READ-VERIFIED] `updateMaterial` `shouldDebug = false` block (material.c:151-165) — permanently dead.

## AI-slop patterns
- [moderate][READ-VERIFIED] `draw()` (model.c:226-249): `material = &buffer[idx]` then `material == NULL` check — unreachable. Same for `mesh` (model.c:241); worse, the else-branch ERROR does `mesh ? mesh->vao : 0` in the branch where mesh is "NULL".
- [trivial][READ-VERIFIED] `printf` used for errors in mesh.c:84,100,106,369 while the rest of the module uses ERROR/WARN macros.

## Memory issues
- [moderate][READ-VERIFIED] `createTerrainMeshWithHeight` leaks `terrainIndices` (alloc mesh.c:570; cleanup 577-582 omits it; sibling `createTerrainMesh` frees at 649).
- [moderate][READ-VERIFIED] `generateHeightMap` (mesh.c:366) never deletes the compute program — one GL program leaked per call.
- [moderate][READ-VERIFIED] Triple copy per mesh creation: SoA source → `createMesh` memcpy into arena `IndexedModel` (mesh.c:275-279) → `initMeshFromModel` interleave into another dalloc (mesh.c:303-321). The `createMesh` copy layer adds nothing.
- [moderate][READ-VERIFIED] Unchecked `dalloc` in `createBoxMesh`/`createPlaneMesh`/`createSphereMesh`/terrain creators/`createTerrainIndices`; `generateHeightMap` passes unchecked `heights` to `glGetBufferSubData` (mesh.c:394-396). Skybox/quad do check — inconsistent.
- [moderate][READ-VERIFIED] Ownership footgun: creators return dalloc'd `Mesh*`, `resRegisterPrimitive` copies by value; calling `freeMesh` on the temp (non-buffered) deletes GL objects the registered copy still uses.

## Bugs / silent failures
- [structural][READ-VERIFIED] `resRegisterPrimitive` (model.c:181,199) passes the raw caller `name` to `insertMap`, which `memcpy`s keySize = MAX_NAME_SIZE = 256 bytes (hashmap.c:72; resourcemanager.c:143) — out-of-bounds read past short names/string literals. `loadModelFromAssimp` pads for exactly this reason (model.c:20-23).
- [moderate][READ-VERIFIED] `loadMeshFromAssimp` double-flips UVs: `aiProcess_FlipUVs` (mesh.c:79) plus manual `1.0f - y` (mesh.c:180) = net unflipped, diverging from `loadModelFromAssimp` (no FlipUVs). One path is wrong. [visual confirm NEEDS-RUNTIME]
- [moderate][READ-VERIFIED] `srand(time(0))` per `generateHeightMap` call (mesh.c:379) — identical terrain within the same second; stomps global RNG state.
- [moderate][READ-VERIFIED] `generateHeightMap` logs then proceeds with program 0 on shader failure (mesh.c:367-370), returning a garbage heightmap; `HeightMap` has no failure signal.
- [moderate][READ-VERIFIED] `createMesh`'s return is ignored by all 8 in-module callers (mesh.c:209,574,641,763,857,915,955,1022); `initMeshFromModel` returns void, so `createMesh` returns true even when GPU init failed → uninitialized Mesh registered/drawn.
- [moderate][READ-VERIFIED] `loadModelFromAssimp` basename split handles only `/` (model.c:17); material.c:25-27 handles `\\` too — Windows paths register under the full path.
- [moderate][READ-VERIFIED] `insertMap` return ignored at model.c:83,133,160,181,199 — a full map silently drops name registrations while buffers still advance.

## Perf
- [moderate][READ-VERIFIED] `updateMaterial` linear-scans `textureUsed` per submesh per frame for alphaThreshold (material.c:197-211); should be resolved once at material load.
- [moderate][NEEDS-RUNTIME] `getMaterialUniforms` single-entry static cache (material.c:127-129) thrashes when ≥2 shaders alternate per frame (e.g. shadow + main pass); also returns stale locations if a GL program ID is reused after deletion.

## API ergonomics
- [moderate][READ-VERIFIED] `resRegisterPrimitive` sets `materialCount = 0` yet `materialIndices[0] = 0` (model.c:189-191); `draw()` with `shouldUpdateMaterials` then binds material slot 0 for primitives — implicit dependency on whatever occupies slot 0.
