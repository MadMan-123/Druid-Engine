# physics-world — critique

Source: `src/systems/Physics/physics.c`. Line refs are to that file unless noted.

## Structural

- **[structural] [READ-VERIFIED] `PhysFieldBinding` layout drift across TUs.** `physGetBindingByIndex` (1438) is exported at link level with no druid.h declaration; consumers re-declare the private struct by hand. `narrowphase.c:9-25` matches, but `broadphase.c:9-17` declares a *different* layout (`posX..velZ, radius, halfX..Z, bodyType, mass`). Through that copy, `b->radius` actually reads `forceX`'s index, `halfX/Y/Z` read `forceY/forceZ/bodyType`, `bodyType` reads `mass` — broadphase queries silently read wrong SoA columns. Needs a shared internal header.
- **[structural] [READ-VERIFIED] All sim state is file-scope static; ~100 MB BSS unconditionally.** 10 f32 + 5 u32 `g_bp*` arrays at `MAX_PHYS_BODIES`=1M (~61 MB, lines 52-68), ghost pools `SH_MAX_GHOSTS`=4M ×2 u32 (32 MB, 152-155), hash/dirty tables (~2 MB). Paid even for a 10-body scene; second `PhysicsWorld` shares/corrupts it; `physWorldDebugDraw` (1603) ignores its `world` arg.
- **[structural] [READ-VERIFIED] Unguarded `Force*`/`vel*` field indices.** Integration casts `fields[b->forceX]` with no `>= 0` check (893-895, 1228-1248 — memset of `frcX` too); solver casts `fields[bA->velX]` unguarded (1123-1124). `buildBodyList` admits archetypes with only `posX` (408), and registration merely WARNs (1422) — a Position-only archetype makes step 5 index `fields[-1]` and memset a garbage pointer. Crash itself [NEEDS-RUNTIME].
- **[structural] [READ-VERIFIED logic / NEEDS-RUNTIME impact] CCD bodies lose discrete collision.** The `continue` at 1059 runs whenever a moving CCD body attempted a sweep, hit or not. Already-overlapping starts return false (`sweptSphereVsSphere` 694: `t < 0`), so an interpenetrating CCD body gets no manifold at all — it sinks; and CCD boxes sweep as spheres (1016).

## Moderate

- [READ-VERIFIED] `ContactManifold.bodyA/bodyB` both store the *pair index* (1051, 1065), contradicting the field names; legacy `onCollision` (1373) passes chunk-local `indexA/indexB` with no arch/chunk — externally unusable without internals knowledge.
- [READ-VERIFIED] `world->onTrigger` dead: set by `physWorldSetTriggerCallback` (1395), never fired anywhere in src.
- [READ-VERIFIED] `unloadPhysicsDLL` (1573) skips `plugin.shutdown`; no code in src ever calls the validated `init/step/shutdown` vtable.
- [READ-VERIFIED] Visibility-index desync: gravity pass skips archetypes when `velX<0||posX<0` (882) without advancing `visIdx`; `buildBodyList` skips only on `posX<0` (408). One archetype missing velocity shifts all subsequent archetypes' visibility mapping between the two passes.
- [READ-VERIFIED] Silent capacity failures: >16 archetypes dropped with no WARN (1410); oversized-list overflow (228) drops bodies from *all* collision despite the "never silently dropped" comment (224); pair/manifold caps and the 16-probe dedup set (537) and 32-probe `csInsert` (94) drop or duplicate pairs/events silently.
- [READ-VERIFIED missing check / NEEDS-RUNTIME trigger] `physRegisterArchetype` never checks for duplicates; `physAutoRegisterScene` re-registering an already-registered archetype makes every entity collide with its own duplicate (different arch index defeats `_i == _j` at 529).
- [READ-VERIFIED] Per-substep hot costs: `csSwapAndClear` does 1 MB memcpy + 1 MB memset (117-118) where a pointer swap suffices; exit scan walks all 131072 `g_csPrev` slots (1318) even with zero contacts; gravity does two f32 divides per dynamic entity (913-916) when `vel += gravity*subDt` is algebraically identical; `frameAlloc` of `invMassScaled`/`dampFactor` per chunk per substep (924, 935).
- [READ-VERIFIED] `PhysicsWorld` embeds `pairs[65536]`+`manifolds[32768]` (~6.5 MB heap per world, 257-261) regardless of scene size.

## Trivial

- [READ-VERIFIED] `g_csCurrCount` write-only (83, 1262). `world->grid` vestigial (266). Dead `: 0.5f` branch at 1034 (shape is always sphere/box). `sh_clear`'s next-chain clearing loops (170-171) redundant — insertion overwrites; ghost/oversized counters reset twice (510-511 then 172-173).
- [READ-VERIFIED] Style/API nits: raw `(flags >> ARCH_BUFFERED) & 1u` at 1342-1344 vs `FLAG_CHECK` at 431; `DAPI` on definition only for `physWorldDebugDraw` (1603); `physWorldSetIterations` accepts 0 while `SetSubsteps` rejects it (1392-1393); restitution default 0.4 duplicated (1130-1131); defensive `!imAPtr` checks against impossible states (1127, 1179); `physBodyGetPosition` (1532) returns raw Position while the sim works in offset space.
