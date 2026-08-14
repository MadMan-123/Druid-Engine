# core-memory

Files: `src/core/vmem.c`, `src/core/arena.c`, `src/core/buffer.c`, `src/core/hashmap.c`.
Declarations: `include/druid.h` (Arena section ~446-457, Memory System section ~459-561, Hash Map section ~563-598, Buffer section ~1272-1299). There are no per-module headers; "exported" means declared in `druid.h` with `DAPI`.

## Synopsis

core-memory is the engine's foundation allocator layer. `vmem.c` reserves one OS-level virtual memory block at startup (`VirtualAlloc` on Windows, `mmap` on Linux/Unix/Apple, `calloc` fallback otherwise), carves it into five bump arenas (`MEM_ARENA_GENERAL/ECS/RENDERER/PHYSICS/FRAME`), and exposes the tagged allocator pair `dalloc`/`dfree` plus a per-frame scratch allocator (`frameAlloc`/`frameReset`) and per-tag/per-arena stats for the editor. On top of that sit three small utility containers that themselves allocate through `dalloc`: a caller-owned linear `Arena` (`arena.c`), a fixed-capacity slot/pool `Buffer` with a free-index stack (`buffer.c`), and a fixed-capacity linear-probing `HashMap` (`hashmap.c`). Effectively every other engine subsystem depends on this module; this module itself depends only on the platform VM APIs, libc, the logging macros (`INFO`/`WARN`/`ERROR`/`DEBUG`), and the profiler hooks `profileCountAlloc`/`profileCountFree` (declared in druid.h's profiler section, defined elsewhere). Nothing here is ECS-managed.

## Public API Reference

### Memory system lifecycle (vmem.c)

**`MemoryConfig memDefaultConfig(void)`**
Returns a default config: `totalMB = 896`, General 256 MB, ECS 256 MB, Renderer 96 MB, Physics 128 MB, Frame 128 MB. No side effects. Called internally by `memLoadConfig` (as the base before parsing); external callers likely (engine bootstrap).

**`b8 memorySystemInit(MemoryConfig *config)`**
One-time init: sums `config->arenaMB[]`, **mutates `config->totalMB` upward** if the sum exceeds it (with a WARN), allocates `totalMB` MB via `platformVMemAlloc`, zeroes the static `memState`, carves the arenas sequentially from the block, and publishes `g_memory = &memState`. Returns false if already initialized, `config` is NULL, or the OS allocation fails. Logs each arena size via `memGetArenaName`. No call sites in these files; caller is external (engine startup).

**`void memorySystemShutdown(void)`**
Releases the whole block via `platformVMemFree`, zeroes `memState`, sets `g_memory = NULL`. WARNs if `allocCount > 0` (live allocations at shutdown). No-op if not initialized. External caller.

**`void memorySystemReset(void)`**
Zeroes every arena's `used` bump pointer and all stats (`totalAllocated`, `allocCount`, `taggedAllocs`, `taggedAllocsFrame`, `taggedFreesFrame`). All previously returned pointers become invalid. External caller.

**`void memArenaReset(MemArenaID arena)`**
Zeroes one arena's `used`, subtracts its prior `used` from `totalAllocated` (clamped at 0), and calls `profileCountFree(was)` so the profiler's live-bytes counter matches (the comment references a CSV `heap_live_bytes` column). Note it does *not* adjust `allocCount` or per-tag `taggedAllocs`. External caller.

### Allocator (vmem.c)

**`void *dalloc(u64 size, MemTag tag)`**
Bump-allocates `alignUp(size, MEM_ALIGNMENT)` (16 bytes) from the arena selected by the static `tagToArena[tag]` table (out-of-range tags fall back to `MEM_ARENA_GENERAL`). Returns zeroed memory, or NULL for `size == 0` or when the target arena is full (ERROR logged, no growth/fallback). If `g_memory` is NULL (pre-init), falls back to `calloc(1, size)`. Side effects: advances `arena->used`, updates `totalAllocated`, `allocCount`, `taggedAllocs[tag]`, `taggedAllocsFrame[tag]`, calls `profileCountAlloc(aligned)`. Called in-module by `arenaCreate`, `bufferCreate` (x3), `createMap`; external callers throughout the engine.

**`void dfree(void *block, u64 size, MemTag tag)`**
Statistical "free": arenas never reclaim individual allocations. If `g_memory` is NULL, or the pointer lies outside `[memState.block, block+totalSize)`, it's assumed to be a libc allocation (pre-init fallback) and is passed to `free()`. Otherwise it only decrements `totalAllocated`, `allocCount`, `taggedAllocs[tag]`, increments `taggedFreesFrame[tag]`, and calls `profileCountFree(0)` (explicitly zero — live bytes are only reclaimed in bulk by `memArenaReset`). The memory itself remains bumped until an arena reset. Called in-module by `arenaDestroy`, `bufferCreate` (failure path), `bufferDestroy`, `createMap` (failure path), `destroyMap`; external callers throughout.

**Macros `DALLOC_TYPE(type, tag)`, `DFREE_TYPE(ptr, type, tag)`, `DALLOC_ARRAY(type, count, tag)`, `DFREE_ARRAY(ptr, type, count, tag)`** (druid.h ~541-544)
Sizeof-computing convenience wrappers over `dalloc`/`dfree`. Not used within this module's files; callers external.

### Frame allocator (vmem.c)

**`void *frameAlloc(u64 size)`**
Bump-allocates zeroed, 16-aligned memory from `MEM_ARENA_FRAME` directly (bypasses tag stats, `totalAllocated`, `allocCount`, and profiler counters). Returns NULL on `size == 0` or arena exhaustion (ERROR); calloc fallback pre-init. External callers.

**`void frameReset(void)`**
Sets the FRAME arena's `used` to 0. Intended once per frame (per header comment). External caller (main loop, likely).

### Stats / editor queries (vmem.c)

**`const c8 *memGetTagName(MemTag tag)`** — returns static string from `tagNames[]`, `"INVALID"` if out of range. Called in-module only implicitly by nothing; external (editor UI likely).
**`const c8 *memGetArenaName(MemArenaID arena)`** — static string from `arenaNames[]` (`"General"`, `"ECS"`, `"Renderer"`, `"Physics"`, `"Frame"`), `"INVALID"` out of range. Called in-module by `memorySystemInit`, `memArenaReset`, `dalloc`; likely external too.
**`u64 memGetTagUsage(MemTag tag)`** — current live bytes per tag (`taggedAllocs[tag]`); 0 if uninitialized/out-of-range. External.
**`u64 memGetArenaUsed(MemArenaID arena)`** / **`u64 memGetArenaSize(MemArenaID arena)`** — bump offset / capacity of one arena; 0 on bad input. External.
**`u64 memGetTotalUsed(void)`** — `totalAllocated` (dalloc-tracked bytes; excludes `frameAlloc`). External.
**`u64 memGetTotalSize(void)`** — sum of arena sizes (recomputed, not `memState.totalSize`). External.
**`u32 memGetAllocCount(void)`** — live `dalloc` count. External.
**`void memResetFrameStats(void)`** — zeroes `taggedAllocsFrame[]` and `taggedFreesFrame[]`; intended per-frame. External.

### Config persistence (vmem.c)

**`b8 memSaveConfig(const c8 *path, const MemoryConfig *cfg)`**
Writes `total=`, `general=`, `ecs=`, `renderer=`, `physics=`, `frame=` (MB values) as `key=value` lines via stdio. Returns false on NULL args or fopen failure. External caller.

**`b8 memLoadConfig(const c8 *path, MemoryConfig *cfg)`**
Initializes `*cfg` from `memDefaultConfig()`, then overrides keys parsed with `sscanf("%63[^=]=%llu")`. Returns false on NULL args or missing file (leaving `*cfg` untouched in that case). External caller.

### Linear arena (arena.c)

**`b8 arenaCreate(Arena *arena, u64 maxSize)`**
Sets `size`/`used` and backs `arena->data` with `dalloc(maxSize, MEM_TAG_ARENA)` (so the storage lives in the GENERAL arena). Returns false on NULL arena or allocation failure. Called in-module by `createMap`; external callers likely.

**`void *aalloc(Arena *arena, u64 size)`**
Unaligned linear bump from a user `Arena`; returns NULL on NULL arena or overflow. Does not zero memory (though `dalloc`-backed arenas start zeroed). Called in-module by `createMap` (pairs, keys, values); external callers likely.

**`void arenaDestroy(Arena *arena)`**
`dfree`s `arena->data` (stats-only per dfree semantics) and NULLs it; `size`/`used` are left as-is. Called in-module by `createMap` failure path and `destroyMap`; external likely.

### Slot buffer (buffer.c)

**`b8 bufferCreate(Buffer *buf, u32 elemSize, u32 capacity)`**
Allocates `data` (capacity*elemSize), `occupied` (b8 array), and `freeStack` (u32 array) via `dalloc(MEM_TAG_BUFFER)`; initializes the free stack with indices 0..capacity-1 and `freeCount = capacity`. On any allocation failure it `dfree`s all three, zeroes `*buf`, returns false. Rejects NULL/zero params with ERROR. No in-module callers; external (renderer's camera/instance-buffer/GBuffer pools per druid.h comments).

**`u32 bufferAcquire(Buffer *buf)`**
Pops an index off `freeStack`, marks `occupied[index] = true`, increments `count`, zeroes the slot, returns the index; `(u32)-1` when full or buf invalid (ERROR when full). External callers.

**`void bufferRelease(Buffer *buf, u32 index)`**
Zeroes the slot, clears `occupied`, decrements `count`, pushes the index back on `freeStack`. Silently ignores out-of-range indices and double-releases (`occupied` guard). External callers.

**`void *bufferGet(Buffer *buf, u32 index)`**
Pointer to slot `index` (no occupancy check); NULL on invalid buf/index. External callers.

**`b8 bufferIsOccupied(Buffer *buf, u32 index)`**
Reads the `occupied` flag; false on invalid input. External callers.

**`void bufferDestroy(Buffer *buf)`**
`dfree`s all three arrays and zeroes `*buf`. External callers.

### Hash map (hashmap.c)

**`b8 createMap(HashMap *map, u32 capacity, u32 keySize, u32 valueSize, u32 (*hashFunc)(const void*, u32), b8 (*equalsFunc)(const void*, const void*))`**
Fixed-capacity open-addressing map. Heap-allocates `map->arena` (an `Arena` struct) via `dalloc(sizeof(Arena), MEM_TAG_ARRAY)`, then `arenaCreate`s it sized `capacity * (keySize + valueSize + sizeof(b8) + sizeof(Pair))` and suballocates the `pairs` array plus per-pair key/value storage with `aalloc`. Stores the caller's hash/equals function pointers. Returns false on NULL map, arena struct allocation failure, `arenaCreate` failure (note: does not `dfree` the arena struct on this particular path), `pairs` allocation failure (does clean up), or key/value `aalloc` failure (returns false without cleanup). No in-module callers; external (druid.h shows `Resources.textureIDs` etc. and `indexMap` fields of type `HashMap`).

**`b8 insertMap(HashMap *map, const void *key, const void *value)`**
Linear probing from `hashFunc(key, capacity)`; copies key+value into the first unoccupied slot (`memcpy` of `keySize`/`valueSize`), or overwrites the value if `equalsFunc` matches an existing key. Returns false on NULL args, `count >= capacity`, or a full probe cycle. External callers.

**`b8 findInMap(HashMap *map, const void *key, void *outValue)`**
Linear probing; copies the value into `outValue` on match. Stops (returns false) at the first unoccupied slot — there is no tombstone mechanism, and no removal function exists in these files. External callers.

**`void destroyMap(HashMap *map)`**
`arenaDestroy`s and `dfree`s `map->arena`, NULLs it. Other fields (`pairs`, `count`, function pointers) are left stale. External callers.

**Declared in druid.h but NOT defined in these files:** `u32 hash(c8 *name, u32 mapSize)` and `void printMap(HashMap *map)` (druid.h lines 593-594). They belong to the HashMap API surface but live in some other translation unit.

## Data Structures

**`Arena`** (druid.h ~448) — `void *data; u64 size; u64 used;`. A plain linear bump region. Two ownership modes: (1) the five system arenas inside `MemorySystem.arenas[]`, whose `data` points into the single OS block (owned by vmem.c, freed only via `memorySystemShutdown`); (2) caller-owned arenas created with `arenaCreate`, whose `data` comes from `dalloc(MEM_TAG_ARENA)` and is released with `arenaDestroy`. Stack or embedded struct storage for the `Arena` header itself (HashMap heap-allocates one via `dalloc`). Not ECS-managed.

**`MemTag`** (druid.h ~466) — 20 allocation-purpose tags (`MEM_TAG_UNKNOWN` … `MEM_TAG_AUDIO`) + `MEM_TAG_MAX`. Used for stats bucketing and arena routing via `tagToArena[]`. Names in the static `tagNames[]` table must stay in sync with the enum.

**`MemArenaID`** (druid.h ~492) — `MEM_ARENA_GENERAL/ECS/RENDERER/PHYSICS/FRAME` + `MEM_ARENA_COUNT`. Identifies a bump region in the single block. Names in static `arenaNames[]`.

**`MEM_ALIGNMENT`** (druid.h 502) — 16; every `dalloc`/`frameAlloc` size is rounded up to this.

**`MemoryConfig`** (druid.h ~505) — `u64 totalMB; u64 arenaMB[MEM_ARENA_COUNT];`. Value type, caller-owned (stack). Produced by `memDefaultConfig`/`memLoadConfig`, consumed (and possibly mutated) by `memorySystemInit`, persisted by `memSaveConfig`. Plain stack.

**`MemorySystem`** (druid.h ~511) — `totalSize`, `block` (base pointer of the OS allocation), `arenas[MEM_ARENA_COUNT]`, per-tag `taggedAllocs`/`taggedAllocsFrame`/`taggedFreesFrame`, `totalAllocated`, `allocCount`. Single static instance `memState` in vmem.c; lifetime = `memorySystemInit`..`memorySystemShutdown`. `taggedAllocsFrame`/`taggedFreesFrame` accumulate per-frame churn and are cleared by `memResetFrameStats` (and never read within these files — consumer is external, presumably the editor/profiler). Not ECS-managed.

**`MemorySystem *g_memory`** (exported global, defined vmem.c:86) — NULL before init/after shutdown; used throughout vmem.c as the "is initialized" flag and exported via `DAPI extern` so other modules (and presumably hot-reloaded game DLLs) can see the same state. Points at the static `memState`; never heap-allocated.

**`Buffer`** (druid.h ~1276) — `data`, `occupied`, `elemSize`, `capacity`, `count`, `freeStack`, `freeCount`. Fixed-capacity object pool with O(1) acquire/release via a LIFO free-index stack. Struct header is caller-owned (often embedded, e.g. `Renderer.cameras`); the three arrays are `dalloc(MEM_TAG_BUFFER)`-backed (GENERAL arena) and released by `bufferDestroy`. Not ECS-managed.

**`Pair`** (druid.h ~570) — `void *key; void *value; b8 occupied;`. One hashmap slot; `key`/`value` point into the map's private arena. Lives inside `HashMap.pairs`.

**`HashMap`** (druid.h ~577) — `capacity`, `count`, `pairs`, `keySize`, `valueSize`, `arena` (heap-allocated `Arena*` holding all pair/key/value storage), plus caller-supplied `hashFunc`/`equalsFunc`. Fixed capacity, no rehash/growth, no delete. Struct header caller-owned; internal storage owned by `createMap`/`destroyMap`. Not ECS-managed.

**`MAX__NAME`** (druid.h 568) — `#define MAX__NAME 256` in the HashMap section; not referenced in any of this module's .c files.

**Static globals in vmem.c** — `tagToArena[MEM_TAG_MAX]` (tag→arena routing table; AUDIO/EDITOR/GAME/TEMP route to GENERAL), `memState` (the one `MemorySystem`), `tagNames[]`, `arenaNames[]` (display strings).

## Internal (static) functions

All in `vmem.c`:
- `platformVMemAlloc(u64 size)` — OS page allocation: `VirtualAlloc(MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE)` / anonymous private `mmap(PROT_READ|PROT_WRITE)` / `calloc` fallback; logs ERROR on failure.
- `platformVMemFree(void *ptr, u64 size)` — matching `VirtualFree(MEM_RELEASE)` / `munmap` / `free`.
- `alignUp(u64 value, u64 alignment)` — `static inline` power-of-two round-up used by `dalloc`/`frameAlloc`.

`arena.c`, `buffer.c`, `hashmap.c` contain no static functions.

## Open questions / unclear intent

1. **`hash()` and `printMap()` are declared in druid.h (lines 593-594) but not defined in `hashmap.c`.** Their defining translation unit is outside this module's file list, so their behavior (e.g., what string hash `hash` implements) is undocumented here.
2. **`MAX__NAME 256`** (double underscore) is defined in the HashMap section of druid.h but unused in all four .c files; its intended consumer is unknown from these files.
3. **Frame-stats consumers unknown.** `taggedAllocsFrame`/`taggedFreesFrame` are written by `dalloc`/`dfree` and cleared by `memResetFrameStats`, but nothing in these files reads them — presumably an editor memory panel or the profiler CSV mentioned in `memArenaReset`'s comment, but that can't be confirmed from this module.
4. **`memArenaReset` stats scope.** It subtracts the arena's `used` from `totalAllocated` but does not touch `allocCount` or the per-tag `taggedAllocs[]`, so after a reset the per-tag numbers still report bytes for memory that has been reclaimed. Whether that divergence is intended (tags surviving as "historical" data) or an accepted approximation is not stated.
5. **`dalloc`/`frameAlloc` profiler asymmetry.** `dalloc` calls `profileCountAlloc` but `frameAlloc` doesn't, and `frameAlloc` also skips `totalAllocated`/`allocCount`; whether frame allocations are deliberately invisible to all stats is not documented.
6. **`memGetTotalUsed` vs `memGetArenaUsed(MEM_ARENA_FRAME)`.** Because `frameAlloc` bumps `arenas[MEM_ARENA_FRAME].used` without updating `totalAllocated`, the two totals measure different things; the intended semantic distinction ("tracked live bytes" vs "bump offsets") is only implied.
7. **`memorySystemInit` mutates the caller's `MemoryConfig`** (expands `totalMB` when arena sums exceed it). Whether callers rely on reading back the adjusted value is unknown from this module.
8. **`createMap` partial-failure behavior differs by stage** (arena-create failure leaves the dalloc'd `Arena` struct; key/value `aalloc` failure returns without any cleanup) — since `dfree` is stats-only this may be deliberate, but the intent isn't stated.
9. **`aalloc` performs no alignment** (unlike `dalloc`'s 16-byte alignment); whether user arenas are meant only for byte-packed data (as in HashMap's key/value blobs) or this is an accepted limitation is unclear.
10. **HashMap has no remove operation and `findInMap` stops at the first unoccupied slot** — consistent with a no-deletion design, but no comment confirms deletion is intentionally unsupported.
