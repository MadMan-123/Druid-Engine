# ecs-scene-prefab-systems

Files: `src/systems/ecs/scene.c`, `src/systems/ecs/prefab.c`, `src/systems/ecs/ecssystem.c`
Public surface: declared in `include/druid.h` (~lines 850–1153). No per-module headers.
Archetype/chunk semantics referenced below are per `docs/modules/ecs-storage.md`.

## Synopsis

Three related subsystems layered on ECS storage: (1) **scene.c** — binary scene
(de)serialization (`.drsc` files, magic `"DRSC"`, version 5), the engine-owned
`SceneRuntime` global (active scene for standalone play: loads the startup scene,
caches standard field pointers, applies materials/camera, handles scene switching
with `ARCH_PERSISTENT` carry-over), and a small `SceneManager` (arena-backed array
of `SceneData` plus a live `Scene`); (2) **prefab.c** — a global `PrefabRegistry`
of named field-value presets loaded from binary `.prefab` files and stamped onto
pooled (`ARCH_BUFFERED`) entities via `prefabSpawn`; (3) **ecssystem.c** — the
game-code system pipeline: loading `ECSSystemPlugin` vtables out of a hot-loaded
game DLL, and editor-driven code generation of per-archetype `.h`/`.c` files
(`generateArchetypeFiles`) plus rebuilding the game DLL (`buildArchetypeSystems`).
Owned by the engine core; consumers (per druid.h comments) are `runtimeCreate`/
`runtimeDestroy`, the editor (save/bake/codegen), physics
(`physAutoRegisterScene(SceneData*)`), and game DLL code (`sceneRuntime->*`,
`prefabSpawn`). Depends on ecs-storage (`createArchetype`, `destroyArchetype`,
`archetypePoolSpawnFields`, `getArchetypeFields`, `archetypeEntityCount`), the
tagged allocator (`dalloc`/`dfree`, `MEM_TAG_SCENE`), `resources`
(ResourceManager), `renderer`, `listFilesInDirectory`, `dllLoad`/`dllSymbol`,
and `platformPipeOpen`/`platformPipeClose`.

## The .drsc scene file format (binary, as written by saveScene / read by loadScene)

Despite the sibling `startup_scene.txt` (which is plain text: just a scene file
name), scene files themselves are **binary**, written with `fwrite` of raw
structs. Layout, in order:

```
SceneFileHeader { u32 magic = 0x43535244 "DRSC"; u32 version = 5; u32 archetypeCount; }
for each archetype:
    c8  archetypeName[MAX_SCENE_NAME]     // fixed 128-byte block
    u32 fieldCount
    FieldFileHeader { c8 name[MAX_FIELD_NAME=64]; u32 size; } * fieldCount
    u32 capacity                          // arena capacity, so the loader doesn't shrink
    u8  flags                             // v4+ only: ARCH_SINGLE|ARCH_PERSISTENT|ARCH_BUFFERED|ARCH_PHYSICS_BODY
    u32 liveCount                         // total live entities across all chunks
    raw SOA data, field-major: for each field fi, for each active chunk ch:
        arena[ch].count * fields[fi].size bytes
u32 materialCount
Material * materialCount                  // raw struct dump
u32 modelRefCount                         // v5+ only
c8  modelRefs[modelRefCount][MAX_NAME_SIZE=256]   // model file basenames, archetype-0
                                          // entities in chunk-linear order
```

Loader accepts versions 2..5 (self-describing via inline field names/sizes);
`< 4` skips flags, `< 5` skips modelRefs; `< SCENE_VERSION` logs a
"will migrate on next save" WARN. Note the in-file layout comment above
`saveScene` (scene.c:446–454) omits the `capacity` and `flags` entries that the
code actually writes.

## Public API Reference

### scene.c — serialization

- `DAPI SceneData bakeScene(Scene *scene)`
  Snapshot a live `Scene` into a `SceneData`. **Shallow**: `sd.archetypes`
  aliases `scene->manager.archetypes` (same pointer, no copy); only copies
  `layout->name` strings into `sd.archetypeNames[i]` (first `MAX_SCENE_ARCHETYPES`).
  Params: scene (NULL → zeroed SceneData). Return: by value. No side effects.
  Callers: none in these files (external — editor, presumably before `saveScene`).

- `DAPI b8 saveScene(const c8 *filePath, SceneData *data)`
  Write `data` in the format above. Before writing, builds the optional
  modelRefs table: for archetype 0 only, if its layout has a field named
  `modelID` (case-insensitive), walks all chunks in order and, for each entity
  whose `modelID` is valid (`!= (u32)-1`, `< resources->modelUsed`), stores the
  basename (`strrchr` on `/` or `\`) of `resources->modelBuffer[id].name`.
  Params: path, data. Return: false on NULL args or fopen failure. Side effects:
  creates/overwrites file; temp modelRefs buffer dalloc'd/dfree'd
  (`MEM_TAG_SCENE`); INFO log. Callers: none in these files (external — editor).

- `DAPI SceneData loadScene(const c8 *filePath)`
  Parse the format above. For each archetype it heap-builds a `StructLayout`
  (dalloc'd struct + dalloc'd name copy + dalloc'd `FieldInfo` array with
  dalloc'd per-field name copies, all `FIELD_TEMP_COLD`), clamps capacity to
  `max(fileCapacity, 128, liveCount)`, calls `createArchetype`, restores
  `flags`, then distributes the flat per-field stream into chunks
  (`toRead = min(remaining, chunkCapacity)` per chunk), sets
  `activeChunkCount`/`cachedEntityCount`. Reads materials and (v5+) modelRefs,
  then calls `sceneRemapModelIDs(&out)` opportunistically. Return: `SceneData`
  by value; on any header/magic/version/alloc failure returns partially-zeroed
  data (on mid-archetype `createArchetype` failure, truncates
  `out.archetypeCount` to the archetypes loaded so far). Side effects: many
  `MEM_TAG_SCENE` allocations; ERROR/WARN/INFO logs. Callers in-file:
  `loadStartupSceneData`, `sceneRuntimeLoad`; also external (DAPI).

- `DAPI u32 sceneRemapModelIDs(SceneData *data)`
  Fix up `modelID` values after resource load-order changes: walks archetype 0's
  chunks in linear order paired with `data->modelRefs[cursor]`, looks each name
  up in `resources->modelIDs` (`findInMap`), and rewrites the entity's `u32
  modelID` when the mapped index differs. Return: number of entities changed;
  0 if no `resources`, no modelRefs, or no `modelID` field. Side effects:
  mutates archetype-0 field data in place. Callers in-file: `loadScene`,
  `sceneRuntimeInit`, `sceneRuntimeLoad`; also external.

### scene.c — SceneRuntime

- `DAPI b8 sceneRuntimeInit(const c8 *projectDir)`
  One-shot init for standalone play. Allocates the global `sceneRuntime` (if
  absent), finds and loads the startup scene via static
  `loadStartupSceneData` (order: filename in `<projectDir>/startup_scene.txt` →
  first `*.drsc` returned by `listFilesInDirectory(<projectDir>/scenes)` →
  `<projectDir>/scenes/scene.drsc`), remaps model IDs, caches
  `entityCount = archetypeEntityCount(arch0)` and the ten standard field
  pointers (see `SceneRuntime`) via case-insensitive `findField` on archetype 0
  chunk 0 (valid as a flat array across chunks because ecs-storage blocks are
  field-major contiguous). Copies scene materials into
  `resources->materialBuffer` **only if `resources->materialUsed == 0`** (editor
  pre-populates materials before runtimeCreate). Copies the first entity with
  `isActive && isSceneCamera` into the active renderer camera's
  `pos`/`orientation`. Return: false on NULL dir, alloc failure, or no scene
  found. Callers: external (game `init` per header comment).

- `DAPI b8 sceneRuntimeLoad(const c8 *filePath)`
  Runtime scene switch. Sequence: fire `g_onBeforeUnload(g_callbackData)`;
  stash all `ARCH_PERSISTENT` archetypes (structs copied by value into a side
  array, names into `persistNames`); `destroyArchetype` every non-persistent
  archetype and free the archetype array, materials, and modelRefs of the old
  scene; `loadScene(filePath)`; on failure, reinstall the persistent archetypes
  as the whole scene, set `loaded = false`, return false. On success:
  `sceneRemapModelIDs`, merge persistent archetypes onto the end of the new
  archetype array (fresh dalloc'd merged array; names appended to
  `archetypeNames`), re-extract entityCount + the ten field pointers,
  unconditionally overwrite `resources->materialBuffer` with the new scene's
  materials, set `loaded = true`, fire `g_onAfterLoad`. Callers: external
  (level transitions).

- `DAPI void sceneRuntimeDestroy(void)`
  `destroyArchetype` on every archetype (persistent included), frees the
  archetype array, materials, modelRefs, and `sceneRuntime` itself; NULLs the
  global. Callers: external.

- `DAPI SceneRuntime *sceneRuntimeGetData(void)` — returns the global
  `sceneRuntime` pointer (may be NULL). Callers: external.

- `DAPI void sceneRuntimeSetCallbacks(SceneTransitionFn onBeforeUnload, SceneTransitionFn onAfterLoad, void *userData)`
  Stores the two callbacks + userData in file-static globals consumed only by
  `sceneRuntimeLoad`. NULL disables a callback. Callers: external.

### scene.c — SceneManager

- `DAPI SceneManager *createSceneManager(u32 sceneCapacity)`
  dalloc's the manager and an `Arena` (`arenaCreate` sized
  `sizeof(SceneManager) + sizeof(SceneData) + 1024*sceneCapacity + sizeof(Scene)`),
  then `aalloc`s `scenes[sceneCapacity]` and an empty `currentScene` from that
  arena. Returns NULL (with partial cleanup) on any allocation failure.
  Callers: external. Note: this module never assigns the global `sceneManager`;
  the caller must.

- `DAPI void destroySceneManager(SceneManager *manager)` — `arenaDestroy` (frees
  scenes + currentScene wholesale) then dfree of the manager. Does not destroy
  archetypes referenced by stored SceneData. Callers: external.

- `DAPI u32 addScene(SceneManager *manager, SceneData *sceneData)` — copies
  `*sceneData` by value into `scenes[sceneCount++]`; returns the index or
  `(u32)-1` when full. Callers: external.

- `DAPI void removeScene(SceneManager *manager, u32 sceneIndex)` — order-
  preserving shift-down removal; does not free the removed SceneData's
  archetypes/materials. Callers: external.

- `DAPI void switchScene(SceneManager *manager, u32 sceneIndex)`
  Manager-level analogue of `sceneRuntimeLoad`, operating on
  `manager->currentScene->manager` (an `EntityManager`): stash `ARCH_PERSISTENT`
  archetypes from the current scene, destroy the non-persistent ones, then point
  `currentScene->manager.archetypes` at `manager->scenes[sceneIndex].archetypes`
  (aliased, not copied), appending persistent archetypes via a freshly dalloc'd
  merged array when needed (the merged array is not written back into
  `manager->scenes[sceneIndex]`). No callbacks fired. Callers: external.

### prefab.c

`.prefab` file format (binary): `u32 magic 0x46505244 "DRPF"`, `u32 version 1`,
`c8 prefabName[MAX_SCENE_NAME]`, `c8 archName[MAX_SCENE_NAME]`, `u32 fieldCount`,
then per field: `c8 name[PREFAB_FIELD_NAME_MAX=32]`, `u32 size`, `size` raw
value bytes.

- `DAPI b8 prefabRegistryCreate(void)` / `DAPI void prefabRegistryDestroy(void)`
  malloc/zero (plain libc `malloc`, not `dalloc`) and free the global
  `prefabRegistry`; create is idempotent. Callers: external (druid.h says
  runtimeCreate/runtimeDestroy).

- `DAPI b8 prefabLoad(const c8 *path)`
  Parse one `.prefab` into `getOrCreateBucket(archName)`; validates magic/
  version/fieldCount (`<= PREFAB_MAX_FIELDS`) and that packed field data fits
  `PREFAB_DATA_POOL_SIZE` (768 B); fields stored as `PrefabField{name,size,
  dataOffset}` with values packed into `Prefab.data`. Fails (WARN) on bucket
  table full or bucket at `PREFAB_MAX_PER_ARCH`. Callers in-file:
  `prefabLoadDirectory`; also external.

- `DAPI u32 prefabLoadDirectory(const c8 *dir)`
  `listFilesInDirectory`, `prefabLoad` every `*.prefab`, free the list; returns
  count loaded. Callers: external (runtimeCreate on `<projectDir>/prefabs/`).

- `DAPI b8 prefabSave(const c8 *path, const c8 *archName, const c8 *prefabName, const c8 (*fieldNames)[32], const void **fieldValues, const u32 *fieldSizes, u32 fieldCount)`
  Writes the format above from parallel arrays. Does not touch the registry.
  Callers: external (editor).

- `DAPI u32 prefabSpawn(Archetype *arch, u32 prefabIdx, Vec3 pos)`
  Spawns a pooled entity via `archetypePoolSpawnFields` (so `arch` must be
  `ARCH_BUFFERED`), stamps prefab field values via static `stampPrefab`
  (name-matched `memcpy` per field into `fields[j] + localIdx * size`; prefab
  fields with no matching layout field are silently skipped), then overrides the
  scalar fields named exactly `"PositionX"/"PositionY"/"PositionZ"` (f32,
  case-sensitive) with `pos`. Returns the stable pool index or `(u32)-1`.
  Callers in-file: none (but the source generated by `generateArchetypeFiles`
  calls it from `<name>Spawn`); also external game code.

- `DAPI u32 prefabCount(Archetype *arch)` — `prefabGetBucket(arch)->prefabCount`
  or 0. Callers: external.

- `DAPI PrefabBucket *prefabGetBucket(Archetype *arch)`
  Resolves a bucket by cached `archRuntime` pointer or by matching
  `arch->layout->name` to `archName` (caching `archRuntime` on first match —
  lazy binding of file data to the runtime archetype). NULL if no bucket.
  Callers in-file: `prefabCount`, `prefabSpawn`; also external (editor
  inspection per druid.h).

### ecssystem.c — game DLL system contract

- `DAPI b8 loadECSSystemDLL(const c8 *dllPath, ECSSystemDLL *out)`
  `memset(out, 0)`, `dllLoad(dllPath, &out->dll)`, then resolves the entry
  point: it *intends* to try `druidGetECSSystem_<out->name>` first, but since
  `out` was just zeroed, `out->name[0]` is always `'\0'` and only the legacy
  `"druidGetECSSystem"` symbol is looked up. Calls `getSystem(&out->plugin)`
  to fill the vtable and sets `out->loaded`. On missing symbol, unloads and
  returns false. Callers in-file: none (external — runtime/editor).

- `void unloadECSSystemDLL(ECSSystemDLL *dll)` (DAPI in druid.h)
  `dllUnload` + `loaded = false`. Does not call `plugin.destroy`. Callers
  in-file: `loadECSSystemDLL` error path; also external.

- `DAPI b8 loadECSSystemFromHandle(DLLHandle *dll, const c8 *archetypeName, ECSSystemPlugin *out)`
  For an already-loaded DLL (e.g. the single game DLL): resolves
  `druidGetECSSystem_<archetypeName>`, falling back to `druidGetECSSystem`,
  and invokes it to fill `*out`. This is the working per-archetype path.
  Callers: external (runtime, per druid.h comment).

  **Contract for game DLLs** (from these files + druid.h): export
  `void druidGetECSSystem_<ArchName>(ECSSystemPlugin *out)` (one per archetype;
  legacy single `druidGetECSSystem` also accepted) which fills
  `{init, update, render, destroy}`. Typedefs: `init(void)`,
  `update(Archetype *arch, f32 dt)` (the per-frame call), optional
  `render(Archetype *arch, Renderer *r)` — NULL means the engine uses its
  default (`rendererDefaultArchetypeRender` per druid.h:1442 comment) —
  and `destroy(void)`. The frame-loop invocation itself lives outside these
  files (runtime module).

### ecssystem.c — code generation

- `DAPI b8 generateArchetypeFiles(const c8 *projectDir, const c8 *archetypeName, const FieldInfo *fields, const c8 **typeNames, u32 fieldCount, b8 isSingle, b8 isBuffered, u32 poolCapacity, b8 isPhysicsBody, b8 isPersistent, b8 uniformScale, b8 useCpp)`
  Editor-facing generator. Input: the archetype's user-defined fields
  (`FieldInfo` name/size/temperature) plus a parallel array of C type-name
  strings (`"f32"`, `"Vec3"`, ...) and flag options. Output files:

  **`<projectDir>/src/<ArchetypeName>.h`** — contains a sentinel-wrapped
  generated block built by static `buildGenBlock`:
  ```
  // <DRUID_GEN_BEGIN Name>
  // DRUID_FLAGS 0xNN            (ARCH_SINGLE/PERSISTENT/PHYSICS_BODY/BUFFERED/FILE_UNIFORM_SCALE)
  // isBuffered / isSingle / isPhysicsBody   (marker comments, as applicable)
  #define POOL_CAPACITY N        (only if buffered and poolCapacity > 0)
  #define NAME_FIELDS(FIELD) \
      FIELD(NAME_ALIVE, "Alive", b8, COLD) \        (auto-prepended if buffered)
      FIELD(NAME_UPPER_SNAKE, "displayName", type, HOT|COLD) \   (per user field)
      FIELD(..., "LinearVelocityX", f32, HOT) ...   (18 physics fields if isPhysicsBody,
                                                     minus any the user already declared)
  DECLARE_ARCHETYPE(Name, NAME_FIELDS)
  // <DRUID_GEN_END Name>
  ```
  Enum names come from `camelToUpperSnake(fieldName)` prefixed with the
  upper-cased archetype name. If the header already exists, only the region
  between `// <DRUID_GEN_BEGIN Name>` / `// <DRUID_GEN_END Name>` is replaced
  (user code outside survives); if the markers are missing the header is left
  untouched with a WARN. Fresh headers additionally get `#pragma once`,
  `#include <druid.h>`, `extern "C"` guards, and `DSAPI` declarations of
  `<name>Init/Update/Destroy/GetArchetype/Spawn` and
  `druidGetECSSystem_<Name>`, plus a comment showing the expected gameInit
  wiring (`createArchetype(&Name_layout, POOL_CAPACITY, &g_nameArch)` /
  `runtimeRegisterArchetype`).

  **`<projectDir>/src/<ArchetypeName>.c`** (or `.cpp` if `useCpp`) — written
  **only if it doesn't exist** (never overwrites user system code). Skeleton:
  `#define DRUID_SYSTEM_EXPORT`, include of the header,
  `DEFINE_ARCHETYPE(Name, NAME_FIELDS)` (instantiates `Name_fields[]` /
  `Name_layout` per the druid.h X-macros), a static `s_arch`,
  `<name>Init/Destroy/GetArchetype`, an `<name>Update` stub (chunk loop over
  `getArchetypeFields` with an `alive[i]` skip for buffered archetypes; single
  archetypes get a chunk-0-only stub), `<name>Spawn(Vec3)` calling
  `prefabSpawn(s_arch, 0, position)`, a commented-out `<name>Render`, and the
  `druidGetECSSystem_<Name>` export filling the plugin
  (`render = NULL`).

  Physics-body handling: the 18-entry `s_physFieldNames`/`s_physFieldTypes`
  tables (LinearVelocityX/Y/Z, ForceX/Y/Z, PhysicsBodyType(u32), Mass, InvMass,
  Restitution, LinearDamping, SphereRadius, ColliderHalfX/Y/Z,
  ColliderOffsetX/Y/Z — all f32 unless noted) are appended as HOT fields,
  skipping any name the user already declared (`physFieldSkip`).

  Return: false on invalid params or file I/O failure. Callers: none in these
  files (external — editor).

- `DAPI b8 buildArchetypeSystems(const c8 *projectDir, c8 *outLog, u32 logSize)`
  Does **not** compile per-archetype DLLs despite the druid.h comment: it
  relies on the project's CMakeLists globbing `src/*.c` into the game DLL, so
  it just ensures `<projectDir>/build` exists (`createDir`) and runs
  `cd /d "<projectDir>/build" && cmake --build . 2>&1` through
  `platformPipeOpen` (Windows `cmd` syntax), streaming output into `outLog`
  (truncated at `logSize`). Returns `platformPipeClose(pipe) == 0`.
  Callers: external (editor).

## Data Structures

All type declarations live in druid.h; storage tags below. None are
ECS-managed; everything is plain heap (tagged `dalloc` or libc `malloc`) or
static/global.

### Globals defined in these files
- `SceneRuntime *sceneRuntime` (scene.c, `DAPI extern` in druid.h) — allocated by
  `sceneRuntimeInit` (`MEM_TAG_SCENE`), freed by `sceneRuntimeDestroy`.
- `SceneManager *sceneManager` (scene.c) — declared/exported but **never
  assigned in these files**; `createSceneManager` returns a pointer the caller
  must store.
- `PrefabRegistry *prefabRegistry` (prefab.c, `DAPI`) — libc `malloc`/`free`
  via `prefabRegistryCreate`/`Destroy`.
- scene.c statics: `g_onBeforeUnload`, `g_onAfterLoad` (`SceneTransitionFn`),
  `g_callbackData` — set by `sceneRuntimeSetCallbacks`, used by
  `sceneRuntimeLoad`.
- ecssystem.c statics: `s_physFieldNames[18]`, `s_physFieldTypes[18]`,
  `s_physFieldTableCount` — the physics-field codegen tables.

### Scene types
- `SceneData` — `{archetypeCount, archetypeNames[32][128], Archetype *archetypes,
  materialCount, Material *materials, modelRefCount,
  c8 (*modelRefs)[MAX_NAME_SIZE]}`. Value type; produced by `loadScene`
  (owning: archetypes array, each archetype's storage, its heap-built layouts,
  materials, modelRefs — all `MEM_TAG_SCENE`) or by `bakeScene` (non-owning
  alias of an `EntityManager`'s archetypes). Freed piecemeal by
  `sceneRuntimeLoad`/`sceneRuntimeDestroy`; the heap `StructLayout`s and field-
  name copies created by `loadScene` have **no free path in these files**.
- `SceneFileHeader` `{magic, version, archetypeCount}` and `FieldFileHeader`
  `{name[64], size}` — on-disk POD, stack lifetime during (de)serialization.
- `Scene` — `{EntityManager manager}`. `EntityManager` is
  `{archetypeCount, Archetype *archetypes, SystemFn *systems, HashMap indexMap}`;
  only `archetypeCount`/`archetypes` are touched here (`systems`/`indexMap`
  unused in these files). `SceneManager.currentScene` is arena-allocated.
- `SceneManager` — `{Arena *data, SceneData *scenes, Scene *currentScene,
  sceneCount, sceneCapacity}`. Manager struct and its `Arena` struct are
  dalloc'd; `scenes` and `currentScene` come from the arena; whole thing torn
  down by `destroySceneManager` (`arenaDestroy` + dfree).
- `SceneRuntime` — embedded `SceneData data` plus cached flat field pointers
  into archetype 0's cold block (`positions "position"` Vec3,
  `rotations "rotation"` Vec4, `scales "scale"` Vec3, `isActive` b8,
  `modelIDs`/`shaderHandles`/`materialIDs`/`archetypeIDs`/`ecsSlotIDs` u32,
  `sceneCameraFlags "isSceneCamera"` b8), `entityCount`, `loaded`. Field
  lookups are case-insensitive; pointers are NULL if the field is absent.
  Lifetime: `sceneRuntimeInit` → `sceneRuntimeDestroy`. Cached pointers are
  re-extracted on every `sceneRuntimeLoad` but would go stale if archetype 0
  ever grows (per ecs-storage growth semantics).
- `SceneTransitionFn` — `void (*)(void *userData)` scene-switch hooks.
- Constants: `MAX_SCENE_ARCHETYPES 32`, `MAX_SCENE_NAME 128`,
  `MAX_FIELD_NAME 64`, `SCENE_MAGIC 0x43535244`, `SCENE_VERSION 5`,
  `MAX_NAME_SIZE 256`.

### Prefab types
- `PrefabField` `{name[32], size, dataOffset}` — descriptor into `Prefab.data`.
- `Prefab` `{name[128], fields[48], fieldCount, data[768]}` — fully inline,
  value-embedded in its bucket; no per-prefab allocation.
- `PrefabBucket` `{archName[128], Archetype *archRuntime (lazy, borrowed),
  prefabs[8], prefabCount}` — one per archetype name.
- `PrefabRegistry` `{buckets[16], bucketCount}` — single malloc'd global;
  everything inside is inline, so `free(prefabRegistry)` releases it all.
- Constants: `PREFAB_MAGIC 0x46505244`, `PREFAB_VERSION 1`,
  `PREFAB_MAX_FIELDS 48`, `PREFAB_MAX_PER_ARCH 8`, `PREFAB_MAX_BUCKETS 16`,
  `PREFAB_FIELD_NAME_MAX 32`, `PREFAB_DATA_POOL_SIZE 768`.

### ECS-system types
- `ECSSystemPlugin` `{init, update, render (nullable), destroy}` — function-
  pointer vtable filled by the DLL's `druidGetECSSystem[_<Name>]` export;
  pointers borrowed from the DLL image (invalid after `dllUnload`).
- `GetECSSystemFn` — `void (*)(ECSSystemPlugin *out)`, the export signature.
- `ECSSystemDLL` `{DLLHandle dll, ECSSystemPlugin plugin, b8 loaded,
  c8 name[128]}` — caller-owned (out-param pattern). `name` is documented as
  the archetype the system belongs to, but nothing in these files ever sets it.
- `ArchetypeFileEntry` `{name, headerPath, sourcePath, StructLayout layout,
  poolCapacity, u8 flags}` and `ArchetypeRegistry` `{entries[32], count}` —
  druid.h-declared tracking of generated file paths; **not referenced anywhere
  in these three .c files** (presumably editor-owned).
- `ARCH_FILE_UNIFORM_SCALE` (flag id 4, extending the ecs-storage
  ARCH_* set) — set into the generated `// DRUID_FLAGS` comment when
  `uniformScale` is true; not consumed in these files.
- `DSAPI` — dllexport when `DRUID_SYSTEM_EXPORT` is defined (generated sources
  define it first), dllimport/empty otherwise; marks game-DLL system exports.

## Internal (static) functions

scene.c:
- `strEqIgnoreCase` — NULL-safe case-insensitive string equality.
- `findFieldIndex` — case-insensitive field-name → index in a `StructLayout`.
- `findField` — resolve a named field of archetype 0 to its chunk-0 base
  pointer (flat SoA array).
- `loadStartupSceneData` — startup-scene discovery + load
  (startup_scene.txt → scan scenes/ for .drsc → scenes/scene.drsc).

prefab.c:
- `getOrCreateBucket` — find bucket by archName or claim a new one (NULL when
  the 16-bucket table is full).
- `stampPrefab` — memcpy each prefab field's value into the matching layout
  field at a local index.

ecssystem.c:
- `camelToUpperSnake` — "PositionX" → "POSITION_X" for enum-name generation.
- `readFileToString` — whole-file read (rejects empty or > 8 MB).
- `findExistingEnumName` — parse `FIELD(enum, "display", ...)` entries out of an
  existing sentinel block to recover a field's enum name. **Never called.**
- `buildGenBlock` — assemble the sentinel-wrapped generated header block
  (flags comment, POOL_CAPACITY, FIELDS X-macro, DECLARE_ARCHETYPE) into a
  malloc'd string (local `BA` append macro with realloc growth).

## Open questions / unclear intent

1. **Enum-name preservation is wired but disconnected.** `buildGenBlock` takes
   an `existingBlock` parameter (documented "Used to preserve user-defined enum
   names") and `findExistingEnumName` exists to support it, but the parameter is
   never read and the helper is never called — regeneration always re-derives
   enum names via `camelToUpperSnake`. Intent (unfinished feature vs. abandoned)
   can't be determined here.
2. **`loadECSSystemDLL`'s per-archetype branch is unreachable.** It memsets
   `*out` and then checks `out->name[0] != '\0'` to build
   `druidGetECSSystem_<name>` — always false, so this entry point only ever
   finds the legacy `druidGetECSSystem` symbol. Whether `name` was meant to be
   an *input* (set by the caller before the call) is unclear.
3. **`ECSSystemDLL.name` is never assigned** anywhere in these files; who
   populates it (editor? runtime?) is unknown.
4. **Ownership of `loadScene`-built layouts.** Each loaded archetype gets a
   heap `StructLayout` + name/field-name copies (`MEM_TAG_SCENE`), but
   `destroyArchetype` (per ecs-storage) doesn't free layouts and no scene
   teardown path here does either. Intended owner/free point undetermined.
5. **Aliasing between `SceneManager.scenes`, `Scene.manager`, and `bakeScene`.**
   `bakeScene` and `switchScene` share `Archetype*` arrays between `SceneData`
   and the live `EntityManager` without copies, and `switchScene`'s merged
   (persistent-appended) array is not stored back into
   `manager->scenes[sceneIndex]`. Which copy is authoritative, and who frees the
   merged array on the next switch, is not visible in these files.
6. **Persistent-archetype identity across `sceneRuntimeLoad`.** Persistent
   `Archetype` structs are copied by value into a side array and back;
   interior pointers survive, but any external pointer held to the old
   `data.archetypes[i]` slot (e.g. `PrefabBucket.archRuntime`, physics
   registrations) now points into freed memory. Whether consumers are expected
   to re-resolve after a scene switch is unstated here.
7. **Position naming duality.** `prefabSpawn` writes scalar
   `"PositionX/Y/Z"` (case-sensitive, generated-archetype style) while
   `SceneRuntime` reads a Vec3 `"position"` (case-insensitive, scene-entity
   style). The two conventions never meet in these files; whether prefabs are
   ever meant to target scene archetype 0 is unclear.
8. **`buildArchetypeSystems` vs. its druid.h comment.** The header says
   "compile all archetype system DLLs", the implementation builds one CMake
   target (the game DLL, per its own comment). Also the command string uses
   `cd /d`, so behavior of the non-Windows path of `platformPipeOpen` here is
   unknown.
9. **`hdr.version == 3` semantics.** Versions 2–3 are accepted and parsed
   identically (no flags byte); what actually changed between 2 and 3 is not
   recoverable from this code.
10. **`EntityManager.systems` / `indexMap` and `ArchetypeRegistry`** are
    declared in the druid.h sections owned by this module but untouched by all
    three .c files; their users are elsewhere.
