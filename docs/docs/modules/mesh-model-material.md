# mesh-model-material

Files: `src/systems/Rendering/mesh.c`, `src/systems/Rendering/model.c`,
`src/systems/Rendering/material.c`, plus declarations/types in `include/druid.h`
(no per-module headers; "exported" = declared in `druid.h` with `DAPI`).

## Synopsis

Turns geometry from three sources — Assimp-imported scenes, engine-generated
primitives/terrain, and `IndexedModel` data (the custom OBJ loader's output
format, loader implemented elsewhere) — into GL-renderable `Mesh` objects, and
groups meshes into `Model`s registered with the global `ResourceManager`
(`resources`). The material half imports PBR materials via Assimp, loads/saves
them in the engine's binary `.drmt` preset format, and binds material textures
and scalar uniforms at draw time. GPU vertex/index storage is preferentially
owned by the shared `GeometryBuffer` (`resources->geoBuffer`); standalone
VAO/VBO/EBO per mesh is the fallback. Depends on: memory system
(`dalloc`/`dfree`/arenas), hashmap (`insertMap`/`findInMap`), shader system
(`createComputeProgram`), texture registry in `ResourceManager`, profiling
counters (`g_drawCalls`, `profileCountVAOBind`).

### The two import paths

1. **Assimp path**: `loadModelFromAssimp` (model.c) → `aiImportFile` →
   `readMaterial` per `aiMaterial` + `loadMeshFromAssimpScene` per scene →
   per-aiMesh copy into a temporary `Vertices` (arena-backed) → `createMesh`.
   Results (meshes, materials, one `Model`) are copied into the
   `ResourceManager`'s flat buffers and name-indexed via hashmaps
   (`"<file>-mesh-<i>"`, `"<file>-material-<i>"`).
2. **Custom OBJ / IndexedModel path**: `objModelCreate` /
   `objModelToIndexedModel` (declared in druid.h, implemented outside these
   files) produce an `IndexedModel`, which is consumed directly by
   `initMeshFromModel`. `createMesh` is itself a thin adapter: it packs a
   `Vertices` + index array into a temporary arena-backed `IndexedModel` and
   calls `initMeshFromModel`. So *all* mesh creation funnels through
   `initMeshFromModel`.

### GL buffer creation/free for a mesh

`initMeshFromModel` interleaves position/texcoord/normal into a 32-byte-stride
(`GEO_VERTEX_STRIDE`) f32 array, then:
- If `resources->geoBuffer` exists and `geometryBufferUpload` succeeds, the
  mesh becomes `buffered = true`; it stores only `baseVertex`/`firstIndex`
  offsets into the shared mega-buffer and owns **no** GL objects
  (`vao`/`vbo`/`ebo` stay 0 per the header comment).
- Otherwise it generates its own VAO/VBO(/EBO if indexed), uploads with
  `GL_STATIC_DRAW`, and sets attributes 0 (vec3 pos), 1 (vec2 uv), 2 (vec3
  normal). `buffered = false`.

Freeing mirrors this: `freeMesh`/`freeMeshArray` call
`glDeleteVertexArrays`/`glDeleteBuffers` only when `buffered` is false —
buffered meshes' GPU memory is owned by the `GeometryBuffer`. `drawMesh`
issues `glDrawElementsBaseVertex` for buffered meshes, plain
`glDrawElements`/`glDrawArrays` otherwise.

## Public API Reference

### mesh.c

- `void freeMesh(Mesh *mesh)` — Delete the mesh's standalone GL objects (skipped
  if `buffered`) and `dfree` the `Mesh` struct itself (`MEM_TAG_MESH`).
  Side effects: GL deletes. Callers: none in these files (likely external —
  resource shutdown/editor).
- `void freeMeshArray(Mesh *meshes, u32 meshCount)` — Same for an array of
  meshes; frees the array in one `dfree`. **Defined in mesh.c but not declared
  in druid.h** — effectively internal linkage-wise exported symbol with no
  in-module callers.
- `void drawMesh(Mesh *mesh)` — Bind VAO and issue the draw
  (`glDrawElementsBaseVertex` if `buffered`, else `glDrawElements`/`glDrawArrays`).
  Bumps `g_drawCalls`, `g_triangles`, `g_vertices`, and `profileCountVAOBind()`.
  No-op with ERROR log if mesh NULL or `vao == 0`; silent no-op if
  `drawCount == 0`. Callers: `draw()` (model.c); likely also external renderer
  code.
- `Mesh *loadMeshFromAssimp(const c8 *filename, u32 *meshCount)` — Convenience:
  `aiImportFile` (Triangulate | FlipUVs | CalcTangentSpace | GenNormals) →
  `loadMeshFromAssimpScene` → `aiReleaseImport`. Returns dalloc'd `Mesh` array
  or NULL. Callers: none in these files (likely external).
- `Mesh *loadMeshFromAssimpScene(const struct aiScene *scene, u32 *meshCount)` —
  For each `aiMesh`: copy positions, texcoords (V flipped as `1.0f - y`; (0,0)
  if absent), normals ((0,0,0) if absent) into an arena-backed `Vertices`,
  memcpy triangle indices, then `createMesh` into an output `Mesh` array
  (`dalloc`, `MEM_TAG_MESH`) that the caller owns. Params: scene must be
  triangulated; `*meshCount` receives `scene->mNumMeshes`. Callers:
  `loadMeshFromAssimp`, `loadModelFromAssimp` (model.c).
- `b8 createMesh(Mesh *mesh, const Vertices *vertices, u32 numVertices, const u32 *indices, u32 numIndices)` —
  Copies vertex/index data into a temporary arena-backed `IndexedModel` and
  calls `initMeshFromModel`; destroys the arena before returning. `indices`
  may be NULL with `numIndices == 0` (non-indexed mesh). Returns false on bad
  params or arena failure. Callers: `loadMeshFromAssimpScene`, all
  `createTerrainMesh*`, `createBoxMesh`, `createSkyboxMesh`, `createQuadMesh`,
  `createPlaneMesh`, `createSphereMesh`; likely external too.
- `void initMeshFromModel(Mesh *mesh, const IndexedModel model)` — The single
  GPU-upload point (see "GL buffer creation" above). Takes `IndexedModel` **by
  value**; does not free the model's arrays (caller owns). Sets
  `mesh->drawCount` (indices count, or vertex count if non-indexed) and
  `mesh->buffered`. Callers: `createMesh`; also the direct entry for the OBJ
  path (external).
- `HeightMap generateHeightMap(i32 sizeX, i32 sizeZ, f32 heightScale, const c8 *computeShaderPath)` —
  Runs a compute shader (16x16 workgroups, uniforms `gridSize`,
  `heightScale`, `seed` from `srand(time(0))`/`rand()`) writing into an SSBO,
  reads back `sizeX*sizeZ` floats into a `dalloc`'d array. Deletes the SSBO;
  does **not** delete the compute program. Caller owns `heights`. Callers:
  `createTerrainMeshWithHeight`. Not declared in druid.h per the greps found
  (only `createTerrainMeshWithHeight`/`createTerrainMesh` are DAPI) — see open
  questions.
- `u32 *createTerrainIndices(u32 cellsX, u32 cellsZ, u32 *outIndexCount)` —
  Builds a `dalloc`'d index array for a grid: 2 CCW triangles per cell,
  `*outIndexCount = cellsX*cellsZ*6`. Caller frees. Callers:
  `createTerrainMeshWithHeight`, `createTerrainMesh`.
- `void calculateTerrainNormals(Vertices *vertices, i32 width, i32 height)` —
  Zeroes normals, accumulates per-triangle cross-product normals into shared
  vertices (2 triangles per grid quad), then normalizes (defaults to
  `(0,1,0)` for degenerate). In-place on `vertices->normals`. Callers:
  `createTerrainMeshWithHeight`.
- `Mesh *createTerrainMeshWithHeight(u32 cellsX, u32 cellsZ, f32 cellSize, f32 heightScale, const c8 *computeShaderPath, HeightMap *output)` —
  Full terrain pipeline: `generateHeightMap((cellsX+1),(cellsZ+1),...)` →
  positions at `(x*cellSize, height, z*cellSize)` with world-space UVs scaled
  by `textureScale = 0.1` → `calculateTerrainNormals` →
  `createTerrainIndices` → `createMesh`. If `output != NULL`, deep-copies the
  heightmap into it (`output->heights` is `dalloc`'d — caller must free). Frees
  all its temporaries; **note it does not free `terrainIndices`** (unlike
  `createTerrainMesh`). Returns `dalloc`'d `Mesh*`. Callers: external.
- `Vertices *createTerrainVertices(u32 cellsX, u32 cellsZ, f32 cellSize)` —
  Flat grid: y=0, UVs 0..1 over the grid, normals `v3Up`. Returns `dalloc`'d
  `Vertices` (caller frees each array + struct). Callers: `createTerrainMesh`.
- `Mesh *createTerrainMesh(u32 cellsX, u32 cellsZ, f32 cellSize)` — Flat
  terrain: `createTerrainVertices` + `createTerrainIndices` + `createMesh`;
  frees all temporaries including indices. Callers: external.
- `Mesh *createBoxMesh()` — Unit cube (half-extent 1) centered at origin,
  24 verts (4/face) with per-face UVs and hard normals, 36 indices. Returns
  `dalloc`'d `Mesh*`. Callers: external (registered via `resRegisterPrimitive`
  presumably).
- `Mesh *createSkyboxMesh()` — 36 non-indexed position-only vertices (inward
  winding), zeroed UVs/normals. Callers: external.
- `Mesh *createQuadMesh()` — Fullscreen quad, 6 non-indexed verts in NDC
  Z=0, dummy normals `(0,0,1)`. Callers: external.
- `Mesh *createPlaneMesh()` — Unit XZ plane, 4 verts / 6 indices, normals Y+.
  Callers: external.
- `Mesh *createSphereMesh()` — UV sphere r=1, 128 slices x 64 stacks, position
  == normal, CCW outward winding. Callers: external.

### model.c

- `void loadModelFromAssimp(ResourceManager *manager, const c8 *filename)` —
  Full model import into the resource manager. Steps: dedup via
  `manager->modelIDs`; `aiImportFile` (Triangulate | JoinIdenticalVertices |
  ImproveCacheLocality | SortByPType | CalcTangentSpace | GenSmoothNormals |
  **PreTransformVertices** — note different flags than `loadMeshFromAssimp`);
  capacity checks on mesh/material/model buffers; `readMaterial` each material
  into `materialBuffer` (named `"<file>-material-<i>"`); build a `Model` with
  `dalloc`'d `meshIndices`/`materialIndices`/`name`; `loadMeshFromAssimpScene`,
  copy each `Mesh` by value into `meshBuffer` (named `"<file>-mesh-<i>"`), map
  Assimp material index → manager index; compute `boundingRadius` (max vertex
  distance from origin) for frustum culling; register model under its base
  filename; free the temporary mesh array and release the scene. Return: void;
  failures only log. Callers: external (resource loading).
- `void resRegisterPrimitive(ResourceManager *manager, const c8 *name, Mesh *mesh)` —
  Copies `*mesh` by value into `meshBuffer` and wraps it in a single-mesh
  `Model` (`materialCount = 0` but `materialIndices[0] = 0`,
  `boundingRadius = 1.0f` for unit-scale primitives); registers both under
  `name`. Does not free the caller's `Mesh*`. Callers: external (engine init,
  pairing with `createBoxMesh` etc.).
- `void draw(Model *model, u32 shader, b8 shouldUpdateMaterials)` — For each
  submesh: bounds-check indices against `resources->meshUsed`/`materialUsed`;
  if `shouldUpdateMaterials`, `getMaterialUniforms(shader)` +
  `updateMaterial`; then `drawMesh`. Side effects: GL texture binds + uniform
  uploads per submesh. Callers: external (render loop).

### material.c

- `u32 loadMaterialTexture(struct aiMaterial *mat, enum aiTextureType type)` —
  Resolve an Assimp texture reference to a GL handle: get texture path, strip
  to filename, look up in `resources->textureIDs`; if miss, retry the basename
  with extensions `.png .jpg .jpeg .bmp .tga`. Returns GL handle or 0.
  Callers: `readMaterial`.
- `void readMaterial(Material *out, struct aiMaterial *mat)` — Populate a
  `Material` from Assimp: albedo/normal/metallic/roughness textures via
  `loadMaterialTexture`; `metallic` (default 0.0) and `roughness` (default
  1.0) factors; forces `colour = (1,1,1)`, `transparency = 1.0`. Does not set
  `emissive`. Callers: `loadModelFromAssimp`, `loadMaterialFromAssimp`.
- `MaterialUniforms getMaterialUniforms(u32 shader)` — `glGetUniformLocation`
  for `albedoTexture`, `metallicTexture`, `roughnessTexture`, `normalTexture`,
  `roughness`, `metallic`, `colour`, `transparency`, `emissive`,
  `alphaThreshold`; cached in function-local statics keyed on the last shader
  ID (single-entry cache). Callers: `draw()` (model.c); likely external
  renderer paths.
- `void updateMaterial(Material *material, const MaterialUniforms *uniforms)` —
  Apply a material to the currently bound shader: binds albedo/normal/
  metallic/roughness to texture units 0–3, uploads `metallic`, `roughness`,
  `transparency`, `colour`, `emissive`. Computes `alphaThreshold` by
  reverse-searching `resources->textureHandles` for the albedo handle and
  checking `textureFlags[i] & 0x01` (transparent flag) →
  `textureAlphaThresholds[i]` (or 0.5 fallback), else 0.0 (disabled).
  Callers: `draw()`; likely external.
- `void applyMaterialPresets(ResourceManager *manager, const c8 *projectDir)` —
  Walks `manager->materialIDs` hashmap; for each material name probes
  `<projectDir>/materials/<name>.drmt` and if present overwrites the material
  slot with `loadMaterial`'s result. Callers: external (project load).
- `b8 saveMaterial(const c8 *filePath, const c8 *name, const Material *mat)` —
  Write the `.drmt` format (see below). Texture GL handles are converted back
  to registry names via `texHandleToName` so files are session-portable.
  Callers: external (editor).
- `Material loadMaterial(const c8 *filePath, c8 *outName, u32 nameSize)` —
  Read `.drmt`; validates `MATERIAL_MAGIC`, warns on version mismatch. Texture
  names are resolved to GL handles through `resources->textureIDs` (slot left
  0 with WARN if not found). On any failure returns the defaults accumulated
  so far (starts as zeroed + `colour=(1,1,1)`, `roughness=0.5`,
  `transparency=1.0`). Callers: `applyMaterialPresets`; likely external.
- `Material *loadMaterialFromAssimp(struct aiScene *scene, u32 *count)` —
  dalloc a `Material` array (`MEM_TAG_MATERIAL`) and `readMaterial` each scene
  material. Caller owns the array. Callers: none in these files (likely
  external or vestigial — `loadModelFromAssimp` does this inline instead).

### `.drmt` material file format (version 2)

Sequential binary, all fixed-size fields:
1. `MaterialFileHeader { u32 magic = 0x544D5244 ("DRMT"), u32 version = 2 }`
2. `c8 name[256]` (MAX_NAME_SIZE, zero-padded)
3. 4 x `c8 texName[256]` — albedo, normal, metallic, roughness (empty string =
   no texture; names are ResourceManager texture-registry keys, not paths)
4. `f32 roughness`, `f32 metallic`, `f32 transparency`, `f32 emissive`,
   `Vec3 colour`
5. `u8` dummy `isTransparent` (v2 field, always written 0, skipped on load —
   alpha cutoff is now per-texture via `textureFlags`/`textureAlphaThresholds`)

## Data Structures

(All defined in `include/druid.h`.)

- `Mesh` (druid.h:1632) — `vao`, `vbo`, `ebo` (0 when buffered),
  `subMeshCount`, `drawCount` (index count, or vertex count when non-indexed),
  `baseVertex`, `firstIndex`, `b8 buffered`. Plain struct; the struct memory
  is `dalloc`'d by whoever creates it (primitive/terrain creators, or
  `loadMeshFromAssimpScene` as an array) and freed by `freeMesh`/
  `freeMeshArray` — but `ResourceManager` stores meshes **by value** in
  `meshBuffer`, so registered meshes' GL objects are reachable from there
  after the temporary is freed. GPU memory: `GeometryBuffer` when `buffered`,
  else the mesh's own GL objects. Not ECS-managed.
- `Model` (druid.h:1806) — `name` (dalloc'd, MAX_NAME_SIZE or strlen+1
  depending on creator), `meshIndices`/`materialIndices` (dalloc'd u32 arrays,
  `MEM_TAG_MODEL`, indices into ResourceManager buffers), `meshCount`,
  `materialCount`, `boundingRadius`. Created on the stack in
  `loadModelFromAssimp`/`resRegisterPrimitive`, then copied by value into
  `resources->modelBuffer` which effectively owns the heap arrays. No free
  path in these files. Not ECS-managed.
- `Material` (druid.h:1596) — 4 GL texture handles (`albedoTex`, `normalTex`,
  `metallicTex`, `roughnessTex`), `roughness`, `metallic`, `transparency`,
  `Vec3 colour`, `emissive`. POD, no owned heap memory (texture handles owned
  by the texture system). Lives by value in `resources->materialBuffer` or
  returned by value from `loadMaterial`. Not ECS-managed.
- `MaterialUniforms` (druid.h:1582) — 10 uniform locations mirroring
  `Material` fields plus `alphaThreshold`; stack value, produced by
  `getMaterialUniforms` per shader (with single-entry static cache).
- `IndexedModel` (druid.h:1174) — SoA `positions`/`texCoords`/`normals`/
  `indices` with per-array count and capacity fields. In this module only used
  as a transient (arena-backed inside `createMesh`); the OBJ loader elsewhere
  produces heap ones. Capacity fields are unused here.
- `Vertices` (druid.h:1611) — `positions`, `normals`, `texCoords`, `u32
  ammount` (sic). Transient SoA container; each creator dallocs and frees its
  own instance.
- `HeightMap` (druid.h:1569) — `f32 *heights`, `i32 width/height`. `heights`
  is `dalloc`'d (`MEM_TAG_MESH`) by `generateHeightMap` (freed by
  `createTerrainMeshWithHeight`) or by the copy given to the caller's `output`
  (caller frees).
- `MaterialFileHeader` (druid.h:1071) — `{u32 magic; u32 version}` with
  `MATERIAL_MAGIC 0x544D5244`, `MATERIAL_VERSION 2`.
- Constants: `GEO_VERTEX_STRIDE 32u` (interleaved vertex byte stride),
  `MAX_NAME_SIZE 256`.
- Referenced globals (owned elsewhere): `resources` (global
  `ResourceManager*`), `g_drawCalls`/`g_triangles`/`g_vertices` (profiling
  counters), `sceneManager` (not used by this module).

## Internal (static) functions

- `texHandleToName` (material.c) — reverse lookup: GL texture handle →
  registry index via `resources->textureHandles`, then walks the
  `textureIDs` hashmap pairs to recover the registered name. Used by
  `saveMaterial`.

(That is the only `static` function; everything else in these three files has
external linkage, whether or not it is declared in druid.h.)

## Open questions / unclear intent

1. `Mesh.subMeshCount` is never written or read anywhere in these files;
   purpose unknown from this module.
2. `freeMeshArray` has no declaration in druid.h and no callers in these
   files — cannot tell whether it is used elsewhere or dead.
3. `generateHeightMap`, `createTerrainIndices`, `calculateTerrainNormals`, and
   `createTerrainVertices` were not found among the druid.h declarations
   grepped — unclear whether they are declared elsewhere in the header or are
   de-facto internal helpers with external linkage.
4. `loadMaterialFromAssimp` has no in-module callers and duplicates the inline
   material loop in `loadModelFromAssimp`; possibly vestigial.
5. `generateHeightMap` never deletes the compute program it creates, and on
   `createComputeProgram` failure it logs but continues to dispatch with
   program 0 — intended behavior unclear.
6. `createTerrainMeshWithHeight` frees vertex temporaries but not
   `terrainIndices` (its sibling `createTerrainMesh` frees them) — cannot tell
   from this file whether that is deliberate.
7. In `draw()`, `material` is `&resources->materialBuffer[materialIndex]` so
   the subsequent `material == NULL` check can never fire; the intended
   validity check is unclear.
8. `insertMap(&manager->mesheIDs, meshName, &manager->meshUsed)` passes a
   pointer to a counter that is incremented immediately after — whether
   `insertMap` copies the value or stores the pointer cannot be determined
   from these files (the by-value semantics are assumed throughout).
9. `readMaterial` never initializes `Material.emissive`; in
   `loadModelFromAssimp` the struct is zeroed first, but direct callers of
   `readMaterial` on uninitialized memory would get garbage — contract
   unclear.
10. Two different Assimp postprocess flag sets are used
    (`loadMeshFromAssimp`: FlipUVs + GenNormals vs `loadModelFromAssimp`:
    GenSmoothNormals + PreTransformVertices, no FlipUVs — though
    `loadMeshFromAssimpScene` always flips V manually). Whether the
    divergence is intentional per-path behavior is not determinable here.
