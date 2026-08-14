# Module: asset-loading

Files covered:
- `src/application/resourcemanager.c` (central resource registry; also compiles the dr_mp3 implementation via `DR_MP3_IMPLEMENTATION`)
- `src/core/objloader.c` (custom OBJ parser)
- Declarations in `include/druid.h` (~lines 1155–1230 for OBJ types, ~1818–1910 for `AudioClip`/`ResourceManager`/getters)

## Synopsis

This module owns the engine-wide resource registry: a single global `ResourceManager *resources` holding flat arrays of materials, meshes, models, GL texture handles, GL shader program handles, and decoded audio clips, each paired with a name→index `HashMap`. `readResources()` scans a resource directory recursively, dispatches every file by extension to the appropriate loader (Assimp for models, GL program creation for shaders, `initTexture` for images, dr_mp3/SDL for audio), and registers results by filename. The OBJ loader (`objloader.c`) is a standalone parser producing an `IndexedModel` (deduplicated vertex/index arrays) for consumption by mesh creation (`initMeshFromModel` in druid.h). Depends on the hashmap module (`createMap`/`findInMap`/`insertMap`/`destroyMap`), memory module (`dalloc`/`dfree` with `MEM_TAG_*`), platform file listing (`listFilesInDirectory`), the shader/texture/geometry-buffer render code, SDL audio, and Assimp (via `loadModelFromAssimp`, defined elsewhere).

## Public API Reference

"Exported" = declared in `include/druid.h` (mostly `DAPI`).

### Resource manager lifecycle

**`DAPI ResourceManager *createResourceManager(u32 materialCount, u32 textureCount, u32 meshCount, u32 modelCount, u32 shaderCount, u32 audioCount)`**
- Purpose: allocate the manager and all its fixed-capacity buffers and hash maps.
- Params: capacity (max element count) for each resource class.
- Return: heap-allocated manager, or `NULL` if the top-level `dalloc` fails.
- Side effects: `dalloc`s the manager (`MEM_TAG_RENDERER`) and each array with a class-specific tag (`MEM_TAG_MATERIAL/MESH/MODEL/TEXTURE/SHADER/AUDIO`); `assert`s each array allocation; creates six hash maps (`textureIDs`, `shaderIDs`, `materialIDs`, `mesheIDs`, `modelIDs`, `audioIDs`), each keyed by a fixed 256-byte string (`MAX_NAME_SIZE`) mapping to a `u32` index, using `djb2Hash`/`equals`; `FATAL`s if any map creation fails. `geoBuffer` is set to `NULL` — the comment says it is created "later in initSystems after GL context" (initSystems is outside this module). Audio map capacity falls back to 8 if `audioCount == 0`.
- Callers: none within these files; external (application/engine init, which also presumably assigns the result to the global `resources`).

**`void cleanUpResourceManager(ResourceManager *manager)`** (declared in druid.h without `DAPI`)
- Purpose: tear down the manager and everything it owns.
- Side effects: calls `textureSystemReset()`; destroys `geoBuffer` via `geometryBufferDestroy` if non-NULL; `dfree`s every buffer using the original size/tag; frees each used `AudioClip.pcm` (`byteLen`, `MEM_TAG_AUDIO`); frees the manager struct itself. Note (factual): it does not call `destroyMap` on any of the six ID hash maps.
- Callers: external (engine shutdown).

**`DAPI void readResources(ResourceManager *manager, const c8 *filename)`**
- Purpose: scan a resource directory recursively and register/load every recognized asset.
- Params: `manager` — target registry; `filename` — resource directory (falls back to `"../" RES_FOLDER` i.e. `"../res/"` if NULL/empty; a trailing `/` is appended if missing).
- Return: void.
- Behaviour, in order:
  1. `listFilesInDirectory(resDir, &outCount)` (platform module) returns a recursive file list; sorted with `compare_files_for_loading_priority` via `qsort` so textures (`png/jpg/bmp`) load before everything else, with lexical order as tie-breaker (comment: keeps model/shader indices stable across runs so scene-stored IDs remain valid).
  2. Loads a default shader named `"default"` from `<resDir>shader.vert` + `<resDir>shader.frag` via `createGraphicsProgram`, only if `"default"` is not already in `shaderIDs`.
  3. First pass over files: builds a temporary `shaderNameMap` (basename without extension → count) for `.vert/.frag/.geom/.glsl` files.
  4. Second pass, dispatch by extension against `fileExtentions[] = {fbx,obj,blend, vert,frag,geom,glsl,comp, png,jpg,bmp, wav,mp3}`:
     - **Models** (`fbx/obj/blend`): `loadModelFromAssimp(manager, filePath)` (defined elsewhere; this module never calls the objloader for these).
     - **Shaders**: skipped if the basename is already in `shaderIDs`. `.comp` → `createComputeProgram(filePath)`, named `"compute-shader-%d"` using the current `shaderUsed`. `.vert/.frag/.geom/.glsl` → looked up in `shaderNameMap` by basename; builds `<base>.vert` + `<base>.frag` (+ `<base>.geom` if `fopen` probe succeeds) and calls `createGraphicsProgram` or `createGraphicsProgramWithGeometry`. On non-zero handle: inserted into `shaderIDs` at index `shaderUsed`, handle stored in `shaderHandles`, `shaderUsed++`. There is no `shaderUsed >= shaderCount` bounds check on this path.
     - **Textures**: keyed by full filename (with extension). Skipped if already in `textureIDs`; `FATAL` + skip if `textureUsed >= textureCount`; `initTexture(filePath)` returns a GL handle (0 = failure, skipped). On success: handle stored, name inserted, per-texture defaults set: `textureAlphaThresholds = 0.5f`, `textureWrapModes = GL_REPEAT`, `textureFilterModes = GL_LINEAR` (`textureFlags` is not defaulted here).
     - **Audio** (`wav/mp3`): keyed by filename; skipped if already registered or `audioUsed >= audioCount`. MP3: `drmp3_open_file_and_read_pcm_frames_f32` → clip `spec = {SDL_AUDIO_F32, channels, sampleRate}`, PCM copied into a `dalloc`'d buffer (`MEM_TAG_AUDIO`), dr_mp3 buffer freed. WAV: `SDL_LoadWAV` → spec/bytes copied into `dalloc`'d buffer, SDL buffer freed. Then inserted into `audioIDs`, `audioUsed++`.
  5. Frees the file list with plain `free()` (the list is allocated by the platform module) and destroys `shaderNameMap`.
- Callers: external (engine/application init, editor project loading).

**`DAPI void applyMaterialPresets(ResourceManager *manager, const c8 *projectDir)`** — declared in druid.h next to `readResources` (overlay saved `.drmt` presets on Assimp-generated materials) but **not defined in either file of this module**; documented here only because the header groups it with this API.

### Typed getters — index-based

All are bounds-checked against the corresponding `*Used` counter and read through the **global** `resources` (the `manager` parameter of other functions is not used here).

- **`DAPI Mesh *resGetMesh(u32 index)`** — returns `&resources->meshBuffer[index]` or `NULL`.
- **`DAPI Model *resGetModel(u32 index)`** — returns `&resources->modelBuffer[index]` or `NULL`.
- **`DAPI Material *resGetMaterial(u32 index)`** — returns `&resources->materialBuffer[index]` or `NULL`.
- **`DAPI u32 resGetTexture(u32 index)`** — returns GL texture handle or `0`.
- **`DAPI u32 resGetShader(u32 index)`** — returns GL program handle or `0`.

No side effects. Callers: the name-based getters below; otherwise external (renderer, ECS render components, editor).

### Typed getters — name-based

Each resolves `name` through the matching `HashMap` (`mesheIDs`, `modelIDs`, `materialIDs`, `textureIDs`, `shaderIDs`) to a `u32` index, then defers to the index-based getter. Return `NULL`/`0` when `resources` is NULL, `name` is NULL, or the name is absent.

- **`DAPI Mesh *resGetMeshByName(const c8 *name)`**
- **`DAPI Model *resGetModelByName(const c8 *name)`**
- **`DAPI Material *resGetMaterialByName(const c8 *name)`**
- **`DAPI u32 resGetTextureByName(const c8 *name)`**
- **`DAPI u32 resGetShaderByName(const c8 *name)`**

Note on key conventions: texture and audio names include the file extension (`fileName` as scanned); shader names are the basename without extension; model/mesh/material names are populated by `loadModelFromAssimp` (outside this module). Callers: external.

### Audio lookup

- **`DAPI i32 resFindAudio(const c8 *name)`** — hashmap lookup in `audioIDs`; returns index as `i32`, or `-1` if not found / no manager. Callers: external (audio system).
- **`DAPI AudioClip *resGetAudio(u32 index)`** — bounds-checked pointer into `audioBuffer`, or `NULL`. Callers: external (audio system).

### Texture metadata persistence

**`DAPI void saveTextureMetadata(const c8 *filePath)`**
- Purpose: persist per-texture editor settings (transparency flag, alpha cutoff, GL wrap/filter modes) to a binary file.
- Format: `u32 count`, then per texture `i` in `[0, textureUsed)`: 256-byte (`MAX_NAME_SIZE`) name buffer, `u8 flags`, `f32 threshold`, `u32 wrapMode`, `u32 filterMode`.
- The name is recovered by a reverse lookup: linear scan of `textureIDs.pairs[]` for the occupied pair whose stored `u32` value equals `i` (O(capacity) per texture). Missing arrays fall back to defaults (`flags=0`, `0.5f`, `GL_REPEAT`, `GL_LINEAR`).
- Side effects: writes/overwrites `filePath` (`fopen "wb"`); very verbose `INFO` logging ("=== SAVE TEXTURE METADATA START ===", per-texture dumps). `WARN` + return if `resources`/`filePath` NULL or the file can't be opened.
- Callers: external (editor save path).

**`DAPI void loadTextureMetadata(const c8 *filePath)`**
- Purpose: read the file written by `saveTextureMetadata` and re-apply settings to currently loaded textures.
- Behaviour: silently returns with an INFO ("first run?") if the file doesn't exist. Reads `count`, then iterates `i < count && i < resources->textureUsed` records; each record's `texName` is resolved through `textureIDs` (so ordering differences between save and current load are tolerated — matching is by name, not index). On match: writes `textureFlags/textureAlphaThresholds/textureWrapModes/textureFilterModes[texIdx]` and immediately applies wrap/filter to the live GL object (`glBindTexture` + `glTexParameteri` on `GL_TEXTURE_WRAP_S/T` and min/mag filter, then unbinds). Unmatched names log `WARN "Texture not found in map"`.
- Side effects: mutates GL texture object state; must therefore run with a current GL context after `readResources`.
- Callers: external (editor/engine startup after resource scan).

### OBJ loader (druid.h ~1213–1228)

**`DAPI OBJModel *objModelCreate(const c8 *fileName)`**
- Purpose: parse a Wavefront OBJ file into raw `v`/`vt`/`vn`/face-index arrays.
- Two-pass: `parseOBJMetadata` counts elements to size exact-capacity `malloc` arrays (`objIndicesCapacity = facesCount * 6`, worst case all quads), then a second read fills them (`objModelParseVec2/3`, `objModelCreateFace`).
- Return: `malloc`'d `OBJModel`, or `NULL` (no vertices, allocation failure, or file unopenable — partially built model is destroyed first).
- Callers: none within these files; external (likely mesh/model creation elsewhere, e.g. as a non-Assimp path).

**`DAPI void objModelDestroy(OBJModel *model)`**
- Frees the four arrays and the struct via plain `free`; NULL-safe. Callers: `objModelCreate` (error paths) + external.

**`DAPI IndexedModel *objModelToIndexedModel(OBJModel *objModel)`**
- Purpose: convert an `OBJModel` into a renderer-friendly `IndexedModel` with deduplicated vertices.
- Deduplication: a temporary `HashMap` keyed by `OBJKey {v, vt, vn}` (using `hashOBJKey`/`equalsOBJKey`); each unique (position, uv, normal) triple becomes one output vertex, arrays grow by doubling `realloc`. Missing uv/normal fall back to `{0,0}` / `{0,0,0}`; if the OBJ had no normals at all, `indexedModelCalcNormals` generates smooth normals afterwards.
- Return: `malloc`'d `IndexedModel`, or `NULL` on error (all partial allocations freed).
- Callers: external (druid.h declares `initMeshFromModel(Mesh *, const IndexedModel)`, the likely consumer).

**`DAPI void indexedModelCalcNormals(IndexedModel *model)`**
- Purpose: compute per-vertex smooth normals from triangle faces (accumulate face cross-products, then normalize; degenerate vertices get `{0,1,0}`). Skips out-of-bounds triangles with a printf warning. Callers: `objModelToIndexedModel`; also exported.

**`DAPI void objModelCreateFace(OBJModel *model, const c8 *line)`**
- Purpose: parse one `f ...` line. Tokenizes with `strtok_r`, parses up to 4 `v/vt/vn` corners (missing components stay `0xFFFFFFFF`; indices are validated 1-based and converted to 0-based; an invalid vertex index skips the whole face). Triangles emitted as-is; quads split into `(0,1,2)(0,2,3)`; faces with any other corner count are ignored. Bounds-checks against `objIndicesCapacity` with a printf warning. Callers: `objModelCreate`; also exported.

**`DAPI Vec2 objModelParseVec2(const c8 *line)` / `DAPI Vec3 objModelParseVec3(const c8 *line)`**
- `sscanf`-based parsers for `vt` / `v`/`vn` lines; return zero vectors (with printf warning) on parse failure. Callers: `objModelCreate`; also exported.

**Declared in druid.h but not defined in these files:** `objModelParseOBJIndex`, `FindNextChar`, `parseOBJIndexValue`, `parseOBJFloatValue`, `SplitString`, `CompareOBJIndexPtr` — see Open questions.

### Referenced but defined elsewhere

`loadModelFromAssimp`, `resRegisterPrimitive` (declared in druid.h in this section, defined in other modules), `initTexture`, `createGraphicsProgram`, `createGraphicsProgramWithGeometry`, `createComputeProgram`, `textureSystemReset`, `geometryBufferDestroy`, `listFilesInDirectory`, `initMeshFromModel`.

## Data Structures

**`ResourceManager` (druid.h ~1828–1864)** — the registry.
- Storage arrays (all `dalloc`'d in `createResourceManager`, sized by the `*Count` capacities, freed in `cleanUpResourceManager`): `materialBuffer` (`Material[]`), `meshBuffer` (`Mesh[]`), `modelBuffer` (`Model[]`), `textureHandles` (`u32[]` GL texture names), `shaderHandles` (`u32[]` GL program names), `audioBuffer` (`AudioClip[]`).
- Per-texture metadata arrays, parallel to `textureHandles`: `textureFlags` (`u8`, header comment: bit 0 = isTransparent), `textureAlphaThresholds` (`f32`, 0–1 cutoff), `textureWrapModes` / `textureFilterModes` (`u32` GL enums).
- `geoBuffer` (`GeometryBuffer *`): global geometry pool "owned by the resource manager" per the header comment; NULL until created externally after GL context; destroyed in `cleanUpResourceManager`.
- Six `HashMap`s (`textureIDs`, `shaderIDs`, `mesheIDs` [sic], `modelIDs`, `materialIDs`, `audioIDs`): 256-byte string key → `u32` index into the matching array.
- `*Count` = capacity, `*Used` = elements populated so far (also the next insertion index).
- Lifetime: created once by the application, referenced everywhere through the global; freed at shutdown. Plain heap via the engine's tagged allocator — **not** ECS-managed.

**`ResourceManager *resources` (global, resourcemanager.c:20, `DAPI extern` in druid.h)** — the singleton all `res*` getters and the metadata save/load read. Defined as `NULL` here; never assigned within these files (assignment happens externally).

**`AudioClip` (druid.h ~1819–1825)** — `name[256]`, `pcm` (`u8*` decoded PCM, `dalloc` `MEM_TAG_AUDIO`, owned by the resource manager per the header comment, freed in `cleanUpResourceManager`), `byteLen`, `spec` (`SDL_AudioSpec`; format is `SDL_AUDIO_F32` for MP3, whatever `SDL_LoadWAV` reports for WAV). Lives inside `audioBuffer`; not individually allocated.

**`OBJKey` (druid.h ~1158–1163)** — `{u32 v, vt, vn}` dedup key for `objModelToIndexedModel`'s temporary hashmap; `0xFFFFFFFF` = component absent. Stack/hashmap-copy lifetime only.

**`OBJIndex` (druid.h ~1166–1171)** — `{vertexIndex, uvIndex, normalIndex}` (0-based, `0xFFFFFFFF` = absent), one per face corner after triangulation; stored in `OBJModel.objIndices`.

**`OBJModel` (druid.h ~1193–1211)** — raw OBJ data: `objIndices`, `vertices` (`Vec3`), `uvs` (`Vec2`), `normals` (`Vec3`), plus `hasUVs`/`hasNormals` and per-array count/capacity. Allocated by `objModelCreate` with plain `malloc` (note: not `dalloc`), freed by `objModelDestroy`. Caller owns it.

**`IndexedModel` (druid.h ~1174–1190)** — deduplicated GPU-ready arrays: `positions`/`texCoords`/`normals` (parallel, shared capacity) and `indices`, each with count/capacity. Allocated by `objModelToIndexedModel` with plain `malloc`; caller owns it. No destroy function exists in these files (see Open questions).

**`OBJMetadata` (objloader.c:62–70, TU-local typedef)** — counts (`verticesCount`, `uvsCount`, `normalsCount`, `facesCount`) plus `hasUVs`/`hasNormals` from the pre-scan pass; stack value returned by `parseOBJMetadata`.

None of the above is ECS-managed; everything is plain heap (tagged `dalloc` in resourcemanager.c, raw `malloc` in objloader.c) or stack.

## Internal (static) functions

No function in either file is declared `static`; the following have external linkage but are not declared in druid.h, so they are internal by convention:

- `djb2Hash` (resourcemanager.c) — DJB2 string hash modulo capacity; hashmap callback for all six ID maps.
- `equals` (resourcemanager.c) — `strcmp`-based key equality callback.
- `compare_files_for_loading_priority` (resourcemanager.c) — `qsort` comparator: image files first, then lexical order (keeps resource indices deterministic).
- `hashOBJKey` / `equalsOBJKey` (objloader.c) — hash/equality callbacks for the `OBJKey` dedup map.
- `parseOBJMetadata` (objloader.c) — pre-scan pass counting `v`/`vt`/`vn`/`f` lines to size allocations.

## Open questions / unclear intent

1. **Who assigns the global `resources`?** It is defined `NULL` in resourcemanager.c and never set in these files, yet every getter and the metadata functions depend on it. Presumably the application init assigns `createResourceManager`'s return value, but that is outside this module.
2. **Hash maps never destroyed:** `cleanUpResourceManager` frees all buffers but does not `destroyMap` the six ID maps. Intentional (process-exit cleanup) or an oversight — cannot tell from this file.
3. **No shader capacity check:** the shader branch of `readResources` increments `shaderUsed` and writes `shaderHandles[shaderUsed]` without comparing against `shaderCount` (textures and audio do check). Unclear whether callers guarantee capacity.
4. **`textureFlags` defaulting:** `readResources` initializes thresholds/wrap/filter defaults for each new texture but not `textureFlags[textureUsed]`; whether `dalloc` zero-initializes (making flags default to 0) depends on the memory module's semantics.
5. **Orphaned druid.h declarations:** `objModelParseOBJIndex`, `FindNextChar`, `parseOBJIndexValue`, `parseOBJFloatValue`, `SplitString`, `CompareOBJIndexPtr` are declared in the OBJ section of druid.h but have no definition in objloader.c — likely leftovers from an earlier parser, but their status (defined elsewhere vs. dead) can't be confirmed from these files.
6. **No `IndexedModel` destructor:** `objModelToIndexedModel` returns a `malloc`'d model with four `malloc`'d arrays, but no free/destroy function for `IndexedModel` exists in these files; who frees it (and whether `initMeshFromModel` takes ownership) is unknown.
7. **Is the OBJ loader still on the load path?** `readResources` routes `.obj` files to `loadModelFromAssimp`, never to `objModelCreate`. Whether objloader.c is a fallback, an editor import path, or legacy code is not determinable from these files.
8. **`applyMaterialPresets`** is declared alongside `readResources` in druid.h (with a detailed comment about `.drmt` overlay) but is not defined in this module's files; its actual location/behaviour is unverified here.
9. **Mixed allocators:** resourcemanager.c uses tagged `dalloc`/`dfree` while objloader.c uses raw `malloc`/`free`/`realloc`; whether this split is intentional (e.g. objloader predating the memory system) is unclear from the sources alone.
