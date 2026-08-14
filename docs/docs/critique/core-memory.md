# core-memory — critique

## Structural

- **[structural][READ-VERIFIED] `dfree` never reclaims memory, but the module's own containers pretend it does.** `dfree` (vmem.c:270-302) only adjusts stats; bytes stay bumped until an arena reset. Yet `bufferDestroy` (buffer.c:87-95), `destroyMap` (hashmap.c:50-58) and `arenaDestroy` (arena.c:13-19) present normal create/destroy lifecycles. Any repeated create/destroy cycle (e.g. reloading a scene's Buffers/HashMaps) permanently consumes GENERAL-arena space with no path to reclaim short of `memorySystemReset`, which invalidates *everything* in all arenas. This is the core design tension of the module.

- **[structural][READ-VERIFIED] HashMap is triple-indirect and cache-hostile.** `createMap` (hashmap.c:19-44) dallocs a separate `Arena` struct, then stores per-slot `key`/`value` *pointers* in `Pair` (druid.h:570-575). Every probe in `insertMap`/`findInMap` chases `pairs[i].key` before comparing. Inline key/value storage (flat `keys[cap*keySize]`, `values[cap*valueSize]`, `occupied[cap]` arrays) would remove two pointer hops per probe, shrink memory, and let `arena` be embedded by value instead of heap-allocated.

## Moderate

- **[moderate][READ-VERIFIED] `createMap` failure-path leak + impossible-state checks.** hashmap.c:24-27: on `arenaCreate` failure the dalloc'd `Arena` struct is not dfree'd (a real libc leak pre-init). hashmap.c:37-41: the per-slot `aalloc` NULL checks are dead defensive code — `arenaSize` (line 23) includes `sizeof(b8)` per slot even though `occupied` lives inside `Pair`, so the arena is strictly over-provisioned and those aallocs cannot fail. Meanwhile `u32 arenaSize` can silently overflow for large `capacity * (keySize+valueSize)`.

- **[moderate][READ-VERIFIED] `createMap`/`insertMap` never validate `capacity != 0` or non-NULL `hashFunc`/`equalsFunc`.** hashmap.c:60-86: `% map->capacity` divides by zero and `map->hashFunc(key, ...)` calls NULL if the caller passed them; contrast with `bufferCreate`'s param checks.

- **[moderate][READ-VERIFIED] `dfree` stat underflow with no clamps.** vmem.c:292-297: a size/tag mismatched `dfree` wraps `taggedAllocs[tag]` (u64) and `allocCount` (u32) to huge values. A `frameAlloc`'d pointer lies inside the block, so dfree'ing one corrupts `totalAllocated`/`allocCount` silently.

- **[moderate][READ-VERIFIED] `memArenaReset` desyncs stats permanently.** vmem.c:207-225 zeroes `used` and `totalAllocated` share but not `allocCount` or `taggedAllocs[]`, so after any arena reset the per-tag panel lies and `memorySystemShutdown` (vmem.c:177-181) WARNs about "live" allocations that were already reclaimed.

- **[moderate][READ-VERIFIED] `aalloc` does no alignment.** arena.c:21-31. HashMap suballocates `keySize`/`valueSize` blobs back-to-back; odd sizes misalign every subsequent key/value pointer. Safe only while consumers stick to `memcpy`; any caller casting `pair->value` to a typed pointer hits UB.

- **[moderate][READ-VERIFIED] Silent pre-init calloc fallback / post-shutdown UB.** `dalloc` (vmem.c:235) and `frameAlloc` (vmem.c:310) fall back to `calloc` with no WARN, masking init-order bugs; and after `memorySystemShutdown`, any straggler `dfree` of an arena pointer passes it to libc `free()` (vmem.c:275) — heap corruption.

- **[moderate][READ-VERIFIED] Slack memory committed but unusable, and caller config mutated.** `memorySystemInit` commits all of `totalMB` up front (`MEM_RESERVE|MEM_COMMIT`, vmem.c:21) but only carves `sum(arenaMB)` into arenas — the default config commits 896 MB while arenas sum to 864, wasting 32 MB forever. It also mutates the caller's `config->totalMB` (vmem.c:134) as a side effect.

- **[moderate][READ-VERIFIED] `frameAlloc` invisible to every stat.** vmem.c:307-327 skips `totalAllocated`, `allocCount`, tag stats, and `profileCountAlloc`, so the hottest per-frame allocation path is exactly the one the editor/profiler cannot see.

## Trivial

- **[trivial][READ-VERIFIED] Unreachable code:** `insertMap`'s final `return false` (hashmap.c:85) — the `count >= capacity` guard (line 63) guarantees the probe loop finds an empty slot or a match.
- **[trivial][READ-VERIFIED] `memGetTotalSize` (vmem.c:404-411) recomputes the arena sum instead of returning `memState.totalSize`; the two differ whenever slack exists (see above), so "total size" is ambiguous.
- **[trivial][NEEDS-BUILD] `MAX__NAME` (druid.h:568) unused in all four files; `hash`/`printMap` declared (druid.h:593-594) but defined outside the module — `hash` takes non-const `c8 *`, inconsistent with the `const void *` callback style.
- **[trivial][READ-VERIFIED] `bufferRelease` (buffer.c:63-64) silently ignores out-of-range and double-release while `bufferAcquire` ERRORs on full — inconsistent failure reporting.
- **[trivial][READ-VERIFIED] Style drift in hashmap.c: tab-indented lines 19-21 amid 4-space file; redundant `<stdlib.h>`/`<stdbool.h>`/`<string.h>` includes vary between buffer.c and hashmap.c.
- **[trivial][READ-VERIFIED] `tagToArena[]` (vmem.c:59-80) and `tagNames[]` (vmem.c:338-359) are positional arrays with no designated initializers or static assert — appending a `MemTag` mid-enum silently misroutes allocations.
- **[trivial][NEEDS-BUILD] `memLoadConfig` casts `u64 *` to `unsigned long long *` for sscanf (vmem.c:465) — works where the types match but is an aliasing smell; values are also unvalidated (0 or absurd MB accepted).
