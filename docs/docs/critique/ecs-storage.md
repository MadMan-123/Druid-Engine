# Critique: ecs-storage

Files: `src/systems/ecs/archetype.c`, `src/systems/ecs/entityarena.c`

## Dead / unreachable / redundant code

- **[structural] [READ-VERIFIED] Entire buffered slow-path (growth) is unreachable, and broken if reached.** `archetypePoolSpawn` (archetype.c:617–636). Invariant: `cachedEntityCount + deadCount == poolCapacity` holds from `createArchetype` (deadCount=poolCap) through every hot-path spawn/despawn (despawn's push at :684 always succeeds while any entity is live). So `deadCount == 0` ⇒ `cachedEntityCount == poolCapacity`, making the guard at :618 false — and `poolCapacity == 0` is impossible because createArchetype (:244–248) always writes a non-zero value for ARCH_BUFFERED. The header comment "0 = unlimited growth" (druid.h:760) is therefore false advertising. If the path *were* reached (e.g. via misuse of `removeEntityFromArchetype` on a buffered archetype, which breaks the invariant at :535 without a free-list push), it grows a whole chunk, uses **only slot 0** (:633–635), never pushes the other `chunkCapacity-1` slots onto `deadIndices`, and does a full-dataset copy per spawn. Either delete the branch or implement it properly (extend free-list, resize `deadIndices`).
- **[trivial] [READ-VERIFIED]** `destroyArchetype` :332 fallback `: arch->capacity` for the deadIndices size is dead — `poolCapacity > 0` whenever `deadIndices != NULL`.
- **[trivial] [READ-VERIFIED]** `mapPoolIndexToChunkLocal` (:52) NULL-out-param guards defend impossible internal call sites.

## Correctness / memory-safety

- **[structural] [READ-VERIFIED] `archetypeGrow` failure leaves the archetype corrupted.** If the arena-array alloc fails (:415), old hot/cold blocks are already freed and `arch->hotData/coldData` repointed (:408–409), but every chunk's `fields[]` still points into the **freed** old blocks (rewire only happens at :422). Caller gets `false` and the archetype silently dangles. Same partial-failure shape at :385–391 is handled; this one isn't.
- **[structural] [READ-VERIFIED] Legacy `createEntityArena` split path is arithmetically wrong** (entityarena.c:87–118): `arenasNeeded = total / ARENA_MAX_SIZE` is floor, not ceil (96 MB → 1 arena); each sub-arena stores the *full* `entityCount` and lays field pointers out with full-count stride (:112–113) into a 64 MB block — field offsets past 64 MB are out of bounds by construction. `freeEntityArena` (:158) then dfrees with `entitySize*entityCount` bytes, not the 64 MB actually allocated. Also `total = entitySize * entityCount` (:84) can overflow u32 silently. If it's truly legacy with no callers, delete it.
- **[moderate] [READ-VERIFIED] `removeEntityFromArena` bounds-checks against `entityCount`, not `count`** (entityarena.c:191). An index in `[count, entityCount)` passes validation, then swap-copies the last *live* entity into a dead slot and decrements `count` — silently destroying the wrong entity.
- **[moderate] [READ-VERIFIED] Unchecked `dalloc` for per-chunk `fields`** at archetype.c:231 and :440; `setupChunkFieldPointers` immediately dereferences it. Every other alloc in these functions is checked.
- **[moderate] [READ-VERIFIED] Division by zero** if `getEntitySize(layout) == 0` (empty layout) at archetype.c:140; no validation despite param checks at :116.
- **[moderate] [READ-VERIFIED] u64→u32 truncation on alloc/free sizes**: `dalloc((u32)hotTotal)` etc. (:183, :194, :363, :385) silently truncates blocks > 4 GB while strides still use full `totalCapacity`.
- **[moderate] [READ-VERIFIED] `ENTITY_PACK` doesn't mask its fields** (druid.h:660): `localIndex >= 65536` (chunkCapacity = 128 KB / entitySize exceeds this for entities < 2 bytes… or any caller-set larger `chunkSizeBytes`) bleeds into the chunkId bits. No unpack macro or consumer exists anywhere in the module to catch it.

## Suspicious AI-slop patterns

- **[moderate] [READ-VERIFIED] `archetypeSpawnIn` (:705) is a near-duplicate of `archetypePoolSpawnFields` (:642)** — one uses `mapPoolIndexToChunkLocal`, the other inlines div/mod; and `archetypeSpawnIn`'s post-spawn "impossible" checks (:725–741 — bounds archetypePoolSpawn just guaranteed) **leak a live entity** on failure (no `archetypePoolDespawn` rollback, unlike its twin at :651).
- **[trivial] [READ-VERIFIED]** Style drift: entityarena.c mixes tab-indented legacy code with space-indented chunk code; `getEntitySize` uses `i32 i` against a `u32` count (:14); `createEntityInArchetype` returns `u8` while everything else returns `b8` (:451); `WARN("Arena Filled\n")` embeds a newline (:172).

## Memory / perf

- **[structural] [READ-VERIFIED] Growth is +1 chunk with full-dataset copy.** Field-major-across-archetype layout means every `archetypeGrow` memcpys *every field of every entity* (:373, :400). Spawning N entities past capacity is O(N²/chunkCapacity) bytes copied. No geometric growth, no chunk-owned storage option.
- **[moderate] [READ-VERIFIED]** `createEntityInArchetype` linearly scans all chunks from 0 on every spawn (:473–480); no first-free-chunk hint.
- **[trivial] [READ-VERIFIED]** Buffered free-list is sized to requested `capacity`, not the rounded-up `totalCapacity` (:244 vs :276) — the padding slots are permanently unspawnable.

## API ergonomics / silent failures

- **[structural] [READ-VERIFIED] `createArchetype` reads `flags`, `chunkSizeBytes`, `poolCapacity` from the *out* parameter** (:127, :136, :245) with no documented requirement to zero-init — a non-memset stack `Archetype` yields garbage flags/chunk size nondeterministically.
- **[structural] [READ-VERIFIED] Growth invalidates `hotData`/`coldData` pointers, cached `fields[f]` elements, and `EntityArena*` chunk pointers** (the arena array itself is reallocated, :414–419) with no generation counter and no header documentation; any system caching `archetypeGetHotData` across a spawn is a use-after-free waiting to happen. [NEEDS-RUNTIME to find an actual offending caller.]
- **[moderate] [READ-VERIFIED]** `archetypePoolDespawn` fails silently in every interesting case — out-of-range chunk (:672), stale index, double-despawn (:677) — no log, no return value; a game-code double-free of a pool slot is invisible.
- **[moderate] [READ-VERIFIED]** Nothing prevents `removeEntityFromArchetype` (swap-remove) on an ARCH_BUFFERED archetype; one call breaks stable indices and the free-list invariant (see first finding). A one-line flag check would close it.
