# ecs-storage

Files: `src/systems/ecs/archetype.c`, `src/systems/ecs/entityarena.c`
Public surface: declared in `include/druid.h` (~lines 600–846). No per-module header.

## Synopsis

Chunked SoA storage for the ECS. An `Archetype` owns two flat contiguous data
blocks (`hotData` / `coldData`, split by per-field `FieldTemperature`) plus an
array of `EntityArena` "chunks" that are read-only *views* (per-field pointer
windows) into those blocks. `entityarena.c` provides the lower-level chunk/arena
primitives (`createEntity`, `removeEntityFromArena`, standalone/legacy arena
allocation); `archetype.c` builds the chunked archetype on top, including growth,
a free-list pool API for `ARCH_BUFFERED` archetypes, and per-archetype collision
callback storage. All memory comes from the core-memory allocator (`dalloc`/`dfree`
with `MEM_TAG_ARCHETYPE`; see `docs/modules/core-memory.md`). Downstream consumers
(per druid.h declarations, external to these files) include the runtime
(`runtimeRegisterArchetype`), physics (`physRegisterArchetype`, body helpers),
prefabs (`prefabSpawn`), the renderer (`rendererDefaultArchetypeRender`), and
generated archetype system code.

## Memory layout (the important part)

`createArchetype(layout, capacity, out)` computes:

- `entitySize = getEntitySize(layout)` — sum of all field sizes.
- `chunkSizeBytes` — taken from `out->chunkSizeBytes` if non-zero (i.e.
  `archetypeSetChunkSize` must be called on the out-struct *before*
  `createArchetype`), else `CHUNK_DEFAULT_SIZE` (128 KB).
- `chunkCapacity` (entities per chunk) = `chunkSizeBytes / entitySize`, rounded
  **down** to a multiple of `CHUNK_SIMD_ALIGN` (4); falls back to the unaligned
  value if that hits 0, and min 1. `ARCH_SINGLE` forces `chunkCapacity = 1` and
  clamps `capacity` to 1.
- `chunksNeeded = ceil(capacity / chunkCapacity)`, `totalCapacity =
  chunksNeeded * chunkCapacity`. `arch->capacity` is rounded up to `totalCapacity`.

Two blocks are then allocated and zeroed:

- `hotData` — size `hotEntitySize * totalCapacity`, where `hotEntitySize` is the
  sum of sizes of `FIELD_TEMP_HOT` fields (0 if `ARCH_NO_SPLIT`, in which case
  every field lives in the cold block).
- `coldData` — size `coldEntitySize * totalCapacity` (cold fields; all fields
  when `ARCH_NO_SPLIT`).

**Block layout is field-major across the whole archetype, not per chunk.**
Inside each block, fields appear in layout order (filtered by temperature), each
as one contiguous array of `totalCapacity` elements:

```
hotData:  [hotField0 x totalCapacity][hotField1 x totalCapacity]...
coldData: [coldField0 x totalCapacity][coldField1 x totalCapacity]...
```

`setupChunkFieldPointers` wires each chunk's `fields[f]` to
`base + (sum of preceding same-temp field sizes) * totalCapacity + chunkIdx * chunkCapacity * fieldSize`.
So `chunk->fields[f][localIdx]` addresses global entity index
`poolIdx = chunkIdx * chunkCapacity + localIdx`, and the same field's data is
contiguous *across* chunks — chunks are purely logical windows for iteration
granularity. Chunks created this way have `ownsData = false`, `data = NULL`.

### Growth (`archetypeGrow`, static)

Growth allocates new hot/cold blocks at the new `totalCapacity`, copies
field-by-field (each field's old `totalCapacity`-run into the head of its new
run), frees the old blocks, reallocates the `EntityArena` array, and re-wires
every chunk's `fields[]` in place. Consequences:

- Global pool indices and entity data positions are **preserved** across growth.
- A cached `void **fields` array pointer (from `getArchetypeFields`) stays valid
  (the per-chunk `fields` allocation is reused and rewritten in place), but any
  cached *element* of it (e.g. `f32 *xs = fields[POS_X]`) and any cached
  `hotData`/`coldData` pointer or `EntityArena *` chunk pointer is **stale**
  after growth.

## Public API Reference

### Archetype lifecycle

- `b8 createArchetype(StructLayout *layout, u32 capacity, Archetype *outArchetype)`
  Initializes a caller-provided `Archetype` (caller owns the struct itself, e.g.
  embedded in game-state arrays). **Reads `outArchetype->flags`,
  `->chunkSizeBytes`, and `->poolCapacity` as inputs** — callers must pre-set
  these before calling. Allocates hot/cold blocks, chunk array, per-chunk
  `fields` arrays, and (if `ARCH_BUFFERED`) the `deadIndices` free-list, filled
  in reverse so spawns pop 0, 1, 2, … Sets `id = 0` ("caller sets to unique ID").
  Returns false and rolls back partial allocations on failure. Logs an INFO
  summary. Callers: external (runtime/game code; none in these files).

- `b8 destroyArchetype(Archetype *arch)`
  Frees hot/cold blocks (sizes recomputed as `entTempSize * chunkCapacity *
  arenaCount`), every chunk's `fields` array, the chunk array, and
  `deadIndices`. Does not free the `Archetype` struct itself. Callers: external.

### Entity create/remove (non-buffered, dense semantics)

- `u8 createEntityInArchetype(Archetype *arch, u64 *outEntity)`
  Finds the first chunk with `count < entityCount`; if all full, grows by one
  chunk. Calls `createEntity` on that chunk (append at `count`), bumps
  `activeChunkCount` high-water mark and `cachedEntityCount`, and returns a
  packed handle via `*outEntity = ENTITY_PACK(arch->id, chunkIdx, localIndex)`
  (`archId << 32 | chunkId << 16 | idx`). Rejects a second entity on
  `ARCH_SINGLE`. Returns `u8` truthiness. Callers: external.

- `b8 removeEntityFromArchetype(Archetype *arch, u32 arenaIndex, u32 index)`
  Delegates to `removeEntityFromArena` (per-field **swap-remove**: last entity's
  field data is memcpy'd into the removed slot), then decrements
  `cachedEntityCount` and shrinks `activeChunkCount` past trailing empty chunks.
  Because of swap-remove, **the index of the chunk's last entity changes** —
  handles/indices into non-buffered archetypes are not stable across removal.
  Callers: external.

### Field access

- `void **getArchetypeFields(Archetype *arch, u32 arenaIndex)`
  Returns the chunk's SoA field-pointer array (`fields[fieldEnum][localIdx]`).
  Callers in-module: `archetypePoolSpawnFields`, `archetypeSpawnIn`; also
  external (systems iterate chunks with this).

### Pool API (ARCH_BUFFERED, stable-index semantics)

Convention (header comment + code): field index 0 is a `b8` Alive flag; indices
are stable, no swap-remove. `count` on each chunk is a high-water mark, so
iterators must test `Alive`.

- `u32 archetypePoolSpawn(Archetype *arch)`
  Hot path O(1): pops a pool index off `deadIndices`, maps it to
  (chunk, local) via `mapPoolIndexToChunkLocal`, sets `fields[0][localIdx] =
  true`, raises the chunk's `count` and `activeChunkCount` high-water marks,
  bumps `cachedEntityCount`, returns the stable pool index. Slow path (free-list
  empty): if `poolCapacity == 0` or `cachedEntityCount < poolCapacity`, grows by
  one chunk and returns that chunk's slot 0 (only slot 0 is used; the new
  chunk's remaining slots are *not* pushed onto the free-list). Otherwise warns
  "pool is full" and returns `(u32)-1`. Errors also return `(u32)-1` on
  non-buffered archetypes. Callers in-module: `archetypePoolSpawnFields`,
  `archetypeSpawnIn`; also external (e.g. `prefabSpawn` returns a `u32` pool
  index per its druid.h declaration).

- `void archetypePoolDespawn(Archetype *arch, u32 poolIndex)`
  Clears the Alive flag (no-op if already dead or out of range), decrements
  `cachedEntityCount`, pushes the index back onto `deadIndices` if
  `deadCount < poolCapacity`. Does **not** shrink chunk `count` or
  `activeChunkCount`. Callers in-module: `archetypePoolSpawnFields` (failure
  rollback); also external.

- `b8 archetypePoolIsAlive(Archetype *arch, u32 poolIndex)`
  True iff archetype is buffered, `chunkIdx < arenaCount`,
  `localIdx < chunk->count`, and the Alive flag is set. Callers: external.

- `b8 archetypePoolSpawnFields(Archetype *arch, u32 *outPoolIdx, u32 *outLocalIdx, void ***outFields)`
  `archetypePoolSpawn` + `getArchetypeFields` in one call; despawns and returns
  false if the fields lookup fails. All out-params optional. Callers: external.

- `ArchetypeSpawnData archetypeSpawnIn(Archetype *arch)`
  Struct-returning variant: `{fields, chunkIdx, localIdx, poolIdx}`; `fields ==
  NULL` (and indices `(u32)-1`) signals failure. Callers in-module:
  `archetypeSpawnInWithCallback`; also external.

- `ArchetypeSpawnData archetypeSpawnInWithCallback(Archetype *arch, ArchetypeInitCallback callback, void *userdata)`
  `archetypeSpawnIn`, then invokes `callback(&result, userdata)` on success so
  the caller can initialize the new entity's fields. Callers: external.

### Queries / config

- `u32 archetypeEntityCount(Archetype *arch)` — returns `cachedEntityCount`
  (live entities; maintained by all spawn/remove paths). NULL-safe (0). Callers
  in-module: `archetypePoolSpawn` slow path; also external.
- `u32 archetypeChunkCount(Archetype *arch)` — returns `activeChunkCount` (the
  iteration bound: highest chunk that has ever held a live entity since the last
  full drain). Callers: external (system chunk loops).
- `void archetypeSetChunkSize(Archetype *arch, u32 sizeBytes)` — writes
  `chunkSizeBytes`; only meaningful **before** `createArchetype` (which derives
  `chunkCapacity` from it once). Callers: external.

### Hot/cold block accessors

- `void *archetypeGetHotData(Archetype *arch)` / `void *archetypeGetColdData(Archetype *arch)`
  Return the raw contiguous blocks. Intended use (from layout above): fully
  linear iteration over all `capacity` slots of a field without per-chunk
  indirection — field f's array lives at `block + (sum of preceding same-temp
  field sizes) * capacity`, entity at `+ poolIdx * fieldSize`. Pointers are
  invalidated by `archetypeGrow` (i.e. by any spawn that triggers growth).
  Callers: external (no in-module call sites; likely physics/render inner loops).
- `u32 archetypeGetHotEntitySize(Archetype *arch)` / `u32 archetypeGetColdEntitySize(Archetype *arch)`
  Bytes per entity summed over hot/cold fields (e.g. for computing block extents:
  `hotEntitySize * capacity`). Callers: external.

### Collision configuration (storage only; consumed by physics)

- `void archetypeSetTrigger(Archetype *arch, b8 isTrigger)` — sets
  `arch->isTrigger` (per header: skips impulse resolution, fires `onTrigger*`).
- `void archetypeSetCollisionCallbacks(Archetype *arch, CollisionCallbacks cbs)`
  — stores the callback struct by value in `arch->cbs`.
  Both are plain setters; consumed externally (physics module).

### EntityArena / layout primitives (`entityarena.c`)

- `u32 getEntitySize(StructLayout *layout)` — sum of all field sizes. Callers
  in-module: `createArchetype`, `archetypeGrow`, `createEntityArenaChunk`,
  `createEntityArena`; also external.
- `u32 createEntity(EntityArena *arena)` — append allocation: returns
  `arena->count` then increments it; `(u32)-1` + WARN when full. No Alive
  bookkeeping. Caller in-module: `createEntityInArchetype`; exported too.
- `b8 removeEntityFromArena(EntityArena *arena, u32 index)` — swap-remove: for
  every field, memcpy the last entity's element over `index`, then `count--`.
  Caller in-module: `removeEntityFromArchetype`; exported too.
- `EntityArena createEntityArenaChunk(StructLayout *layout, u32 chunkCapacity)`
  Standalone self-owning chunk (`ownsData = true`): one `dalloc` of
  `entitySize * chunkCapacity`, field pointers packed field-major within it.
  Data is **not** zeroed (unlike `createEntityArena`). No callers in these
  files — external/utility.
- `void freeEntityArenaChunk(EntityArena *chunk)` — frees `fields` always, and
  `data` only when `ownsData` (safe on archetype-view chunks). Uses cached
  `entitySize`/`fieldCount` so the layout need not be alive. No in-module callers.
- `EntityArena *createEntityArena(StructLayout *layout, u32 entityCount, u32 *outArenas)`
  Legacy path (per file comment): one big zero-initialized allocation, split
  into multiple 64 MB (`ARENA_MAX_SIZE`) arenas if `entitySize * entityCount`
  exceeds it; returns heap array of arenas and writes the arena count. No
  in-module callers.
- `b8 freeEntityArena(EntityArena *arena, u32 arenaCount)` — frees each arena's
  `fields` + `data` (sized `entitySize * entityCount`) and the arena array.
  Unconditionally frees `data` (ignores `ownsData`) — legacy pair of
  `createEntityArena` only.
- `void printEntityArena(EntityArena *arena)` — printf debug dump of field
  names/sizes.

## Data Structures

All declared in `include/druid.h`. None are "ECS-managed" in a dynamic
component sense — layouts are fixed at creation; storage is plain heap via the
tagged allocator (`MEM_TAG_ARCHETYPE`).

- `FieldTemperature` (enum): `FIELD_TEMP_COLD = 0` (default), `FIELD_TEMP_HOT = 1`
  (touched every frame in inner loops). Drives the hot/cold block split.
- `FieldInfo`: `{const c8 *name; u32 size; FieldTemperature temperature;}` — one
  SoA field descriptor. Usually defined statically via the `FIELD`, `FIELD_HOT`,
  `FIELD_COLD`, `FIELD_OF`, `VEC3_FIELDS[_HOT]`, `VEC4_FIELDS[_HOT]` macros or the
  `DECLARE_ARCHETYPE`/`DEFINE_ARCHETYPE` X-macro pair (which also generates the
  field-index enum, `name##_fields[]`, and `name##_layout`). Static lifetime.
- `StructLayout`: `{name, fields, count}` — describes an archetype's schema.
  Not owned by this module; `Archetype`/`EntityArena` keep borrowed pointers
  (must outlive them; `entitySize`/`fieldCount` are cached on `EntityArena`
  specifically so destroy paths don't need it).
- `EntityArena` (a.k.a. chunk): `count` (live/high-water entities), `entityCount`
  (slot capacity), `entitySize`, `fieldCount` (cached), `layout` (borrowed),
  `data` (backing block — NULL for archetype-view chunks), `fields` (heap array
  of per-field base pointers, always owned and freed by whichever destroy path
  matches), `ownsData` (true only for standalone/legacy arenas). Lives either as
  a value (createEntityArenaChunk), a heap array owned by the caller of
  `createEntityArena`, or inside `Archetype.arena` owned by the archetype.
- `CHUNK_SIMD_ALIGN` (4), `CHUNK_DEFAULT_SIZE` (128 KB): chunk sizing constants.
- `ENTITY_PACK(archId, chunkId, idx)`: `u64` handle = archId in bits 63–32,
  chunkId in bits 31–16, local index in bits 15–0. Produced by
  `createEntityInArchetype`; no unpack macro or consumer in these files.
- `Archetype`: see layout section for `hotData`/`coldData`/`arena`/sizing
  fields. Also: `id` (0 until caller assigns), `arenaCount` (allocated chunks),
  `activeChunkCount` (iteration bound), `capacity` (total slots, rounded up),
  `poolCapacity` (buffered pool cap; input to createArchetype, 0 documented as
  "unlimited growth"), `deadIndices`/`deadCount` (buffered free-list stack,
  sized `poolCapacity`), `cachedEntityCount`, `flags` (b8 bitfield of
  `ARCH_SINGLE`/`ARCH_PERSISTENT`/`ARCH_PHYSICS_BODY`/`ARCH_BUFFERED`/
  `ARCH_NO_SPLIT`, tested with `FLAG_CHECK`; PERSISTENT and PHYSICS_BODY are not
  read in these files), `isTrigger`, `cbs`. The struct itself is caller-owned
  (out-param pattern); interior allocations are owned by the archetype and
  released by `destroyArchetype`.
- `EntityRef`: `{Archetype *arch; u32 poolIdx}` with
  `poolIdx = chunkIdx * chunkCapacity + localIdx`. Per header, a "stable
  identity". Stability analysis from this module: stable for `ARCH_BUFFERED`
  archetypes across spawn/despawn *and* growth (indices never move); **not**
  stable in non-buffered archetypes (swap-remove relocates the last entity);
  dangles after `destroyArchetype`. Used by `ContactInfo` for collision events.
- `ContactInfo` / `ContactCallback` / `CollisionCallbacks`: collision-event
  payload (`self`, `other` EntityRefs, `normal` pointing other→self, `point`,
  `depth`) and the four per-archetype callbacks (`onCollideEnter/Exit`,
  `onTriggerEnter/Exit`). Stored here, invoked by physics (external).
- `ArchetypeSpawnData` / `ArchetypeInitCallback`: spawn result
  (`fields`/`chunkIdx`/`localIdx`/`poolIdx`, `fields == NULL` on failure) and
  the init hook for `archetypeSpawnInWithCallback`. Stack/value types.

## Internal (static) functions (archetype.c)

- `getEntitySizeForTemp` — per-entity byte total of fields matching one
  temperature.
- `setupChunkFieldPointers` — wires a chunk's `fields[]` into the archetype
  hot/cold blocks (handles the `hotBase == NULL` no-split case).
- `mapPoolIndexToChunkLocal` — `poolIndex` → (`poolIndex / chunkCapacity`,
  `poolIndex % chunkCapacity`) with NULL/zero guards.
- `archetypeGrow` — reallocate hot/cold blocks and chunk array to a new chunk
  count, copying per-field and re-wiring all chunk field pointers.

## Open questions / unclear intent

1. **Buffered "unlimited growth" path looks unreachable via createArchetype.**
   `archetypePoolSpawn`'s slow path treats `poolCapacity == 0` as unlimited, but
   `createArchetype` always sets `poolCapacity` to a non-zero value when
   `ARCH_BUFFERED` is set. Unclear whether callers are expected to configure
   flags/poolCapacity outside `createArchetype` to reach it.
2. **Buffered growth uses only slot 0 of each new chunk.** The slow path marks
   `newChunk` slot 0 alive but never pushes the chunk's remaining
   `chunkCapacity - 1` slots (or any grown index) onto `deadIndices`, and
   `deadIndices` is never resized. Grown slots only become reusable via despawn
   (and only while `deadCount < poolCapacity`). Whether this one-entity-per-
   grown-chunk behavior is intentional is unclear from this file.
3. **Legacy `createEntityArena` split path arithmetic.** When splitting at
   64 MB, `arenasNeeded = total / ARENA_MAX_SIZE` (integer division stored in an
   `f32`), each sub-arena keeps the *full* `entityCount` and computes field
   offsets with the full-count stride while allocating only `ARENA_MAX_SIZE`
   bytes. The intended per-sub-arena entity distribution can't be determined
   here (file is marked "Legacy").
4. **Alive-flag convention is unchecked.** Pool functions cast `fields[0]` to
   `b8 *`; nothing validates that a buffered layout's field 0 is a 1-byte Alive
   field — enforcement presumably lives in the code generator / callers.
5. **ENTITY_PACK consumers unknown.** The u64 handle packs chunk/local into
   16 bits each, but no unpack macro or consumer exists in these files; how
   handles relate to `EntityRef.poolIdx` elsewhere is undetermined.
6. **Pre-create out-struct contract is implicit.** `createArchetype` reads
   `flags`, `chunkSizeBytes`, and `poolCapacity` from the *out* parameter (and
   `archetypeSetChunkSize` only matters pre-create), but the header comments
   don't state that the caller must pre-initialize (or zero) the struct.
7. **`createEntityArenaChunk` has no in-module callers** and, unlike the other
   creation paths, does not zero its data block; its intended user is unknown
   from these files.
8. **`removeEntityFromArchetype` vs buffered archetypes.** Nothing prevents
   calling the swap-remove path on an `ARCH_BUFFERED` archetype, which would
   break stable pool indices and free-list integrity; whether that's forbidden
   by convention is not visible here.
