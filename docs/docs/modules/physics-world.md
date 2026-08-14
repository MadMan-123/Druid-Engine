# physics-world

Source: `src/systems/Physics/physics.c` (1,621 lines). Public declarations: the "Physics Engine" section of `include/druid.h` (~lines 2533–2678) plus `ContactInfo`/`EntityRef`/`CollisionCallbacks` (~lines 728–789).

## Synopsis

Implements the engine's rigid-body physics world: a fixed-timestep simulation that operates directly on ECS archetype SoA fields (no separate body objects). Each step it integrates gravity/velocity/damping with SIMD, builds a flat broadphase body list, finds candidate pairs via a spatial hash grid, generates sphere/box contacts (with optional swept CCD), runs a sequential-impulse velocity solver plus one position-correction pass, and fires per-archetype enter/exit collision and trigger callbacks. The module owns the `PhysicsWorld` struct (heap, `MEM_TAG_PHYSICS`) and the global singleton `physicsWorld`; it also provides the loader for external physics-plugin DLLs (`PhysicsDLL`). Depends on: ECS storage (`Archetype`, `getArchetypeFields`, `archetypePoolIsAlive`, `StructLayout`), core-memory (`dalloc`/`dfree`, `frameAlloc`, `memArenaReset`), SIMD math (`simdMadd`, `simdMulScalar`, `simdMul`, `simdNeg`, `simdAddScalar`), core-math (`Vec3`, `v3*`), platform DLL loading (`dllLoad`/`dllSymbol`/`dllUnload`), and logging (`INFO`/`WARN`). The Runtime layer (per druid.h comments) drives `physInit`/`physWorldStep`/`physShutdown` and deferred archetype registration.

Note: `physics.c` does **not** contain the collider-handle API (`physColliderCreate*`), broadphase queries (`physBroadphaseRebuild`/`physBroadphaseQuery`), raycasts (`physRaycast*`), or overlap queries (`physOverlapSphere/Box`) even though druid.h declares them in the same section — those live in sibling files (see physics-shapes / physics-queries docs when available).

## Public API Reference

All exported symbols are `DAPI` in druid.h unless noted.

### World lifecycle

- `PhysicsWorld *physWorldCreate(Vec3 gravity, f32 timestep)`
  Allocates and zero-inits a `PhysicsWorld` via `dalloc(..., MEM_TAG_PHYSICS)`. Defaults: `solverIterations = 8`, `substeps = 1`. Side effects on **file-scope globals**: initializes the spatial-hash table (`g_shTable` → sentinel), resets ghost/oversized/dirty counters, and (once per process, guarded by `g_csInitialised`) clears the contact-state tables `g_csPrev`/`g_csCurr`. Returns NULL on allocation failure. Callers: `physInit` (this file); external (runtime/editor per druid.h comments).

- `void physWorldDestroy(PhysicsWorld *world)`
  Frees the world (`dfree`) and resets all static broadphase/contact-state globals (`g_bodyCount`, cell size, dirty/ghost/oversized counts, `g_csPrev/Curr`, `g_csInitialised = false`) so a re-created world starts clean. Callers: `physInit` (replacing an existing singleton), `physShutdown`; likely external for non-singleton worlds.

- `void physWorldStep(PhysicsWorld *world, f32 dt)`
  Fixed-timestep tick. Adds `dt` to `world->accumulator`, clamps the accumulator to `5 * fixedTimestep` (spiral-of-death guard), then runs whole fixed steps. Each fixed step runs `world->substeps` substeps with `subDt = fixedTimestep / substeps`; per-substep order:
  1. **Gravity + velocity integration** — per registered archetype/chunk: for `PHYS_BODY_DYNAMIC` entities, `Force += mass * gravity` (lazily computes `InvMass = 1/Mass` if `InvMass == 0` and `Mass > 0`); then SIMD `vel += force * invMass * subDt` (`simdMadd` on all lanes, including non-dynamic — relies on their forces being 0) and SIMD damping `vel *= max(0, 1 - LinearDamping*subDt)` (factor forced to 1 for non-dynamic lanes). Archetypes missing `velX`/`posX` bindings are skipped; missing `invMass` skips the whole chunk. Visibility LOD: non-visible entities skip gravity accumulation except every `lodInterval`-th step (`physFrameCounter % lodInterval == 0`). Scratch arrays come from `frameAlloc`.
  2. **Broadphase** — `buildBodyList(world, subDt)` flattens all live, visible entities into the `g_bp*` arrays, then `broadphaseGetPairs()` fills `world->pairs` (≤ `MAX_PHYS_PAIRS` = 65536). Logs an `INFO` dump of counts and first 8 bodies for the first 3 `physFrameCounter` values.
  3. **Narrowphase** — per pair: recompute world positions (`Position* + ColliderOffset*`). If either body has `CCDEnabled` and is moving, run the swept path: `sweptSphereVsBox`/`sweptSphereVsSphere` from the start-of-step position over `vel*subDt`; on hit, **snap the CCD body's Position to the impact point, zero its LinearVelocity**, and emit a depth-0 manifold. Otherwise dispatch `contactSphereSphere` / `contactSphereBox` (normal flipped for box-vs-sphere order) / `contactBoxBox` into `world->manifolds` (≤ `MAX_PHYS_MANIFOLDS` = 32768). Convention: contact normals point **from A to B**. Manifolds store the *pair index* in both `bodyA` and `bodyB`.
  4. **Velocity solver** — `solverIterations` passes of sequential impulses: `j = -(1+e)*rvn / (imA+imB)`, restitution `e = min(eA, eB)` (default 0.4 if no `Restitution` field). Non-`PHYS_BODY_DYNAMIC` bodies get `invMass` forced to 0; pairs where either archetype `isTrigger` are skipped entirely. No friction term.
  4b. **Position correction** — single pass, Baumgarte-free direct translation: penetration beyond `SLOP` (0.005) split by inverse-mass ratio, applied straight to `Position*`.
  5. **Position integration** — `pos += vel * subDt` for dynamic bodies only (or, without a body-type field, bodies with `invMass != 0`), then `memset` `Force*` to 0.
  6. **Contact events** — packs each manifold's pair into a canonical u64 key (`csEntityKey`: arch 4b | chunk 12b | local 16b per entity) inserted into `g_csCurr`; fires `arch->cbs.onCollideEnter`/`onTriggerEnter` for keys absent from `g_csPrev` (a `ContactInfo` per side, self/other swapped and normal negated for B); scans `g_csPrev` for vanished keys and fires `onCollideExit`/`onTriggerExit` (skipping despawned pooled entities via `archetypePoolIsAlive`; exit info has placeholder normal `{0,1,0}`). Also invokes the legacy `world->onCollision(indexA, indexB, manifold)` per manifold if set (indices are chunk-local). Ends with `csSwapAndClear()`.
  After the fixed-step loop, `world->visibility`/`visCount` are cleared (the buffer is frame-allocated by the caller and becomes invalid after frame reset). Callers: `physTick` (this file); external (runtime `runtimeUpdate` per druid.h comments).

### Settings

- `void physWorldSetGravity(PhysicsWorld*, Vec3)` — sets `world->gravity`. External callers only.
- `void physWorldSetIterations(PhysicsWorld*, u32)` — sets `solverIterations`. External.
- `void physWorldSetSubsteps(PhysicsWorld*, u32)` — sets `substeps` (rejects 0). External.
- `void physWorldSetCollisionCallback(PhysicsWorld*, CollisionFn)` — sets legacy global `onCollision`. External.
- `void physWorldSetTriggerCallback(PhysicsWorld*, TriggerFn)` — sets `onTrigger`; **never invoked anywhere in physics.c** (see open questions). External.
- `void physWorldSetVisibility(PhysicsWorld*, const b8 *visible, u32 count, u32 lodInterval)` — points the world at a frame-allocated visibility buffer (frustum-cull results per druid.h comment) plus LOD interval; not owned, cleared at end of `physWorldStep`. `lodInterval = 0` means non-visible entities never receive gravity (they are also always excluded from broadphase). External (renderer/runtime).

### Archetype registration

- `void physRegisterArchetype(PhysicsWorld *world, Archetype *arch)`
  Registers an archetype as a physics body source (max `MAX_PHYS_ARCHETYPES` = 16, silently dropped beyond). Sets `ARCH_PHYSICS_BODY` on `arch->flags`, builds a `PhysFieldBinding` by name-lookup in `arch->layout` (`buildBinding`), logs the binding, and `WARN`s if Position/LinearVelocity or any collider-size field is missing. Field names resolved (first match wins): `PositionX/Y/Z` (fallback `positionX/…`), `LinearVelocityX/Y/Z`, `ForceX/Y/Z`, `PhysicsBodyType` (fallback `PhysBodyType`), `Mass`, `InvMass`, `Restitution`, `LinearDamping`, `SphereRadius` (fallback `Radius`), `ColliderHalfX/Y/Z`, `ColliderOffsetX/Y/Z`, `ColliderShape`, `CCDEnabled`, `Scale` (fallback `scale`, size sniffed for Vec3 vs f32). Callers: `physAutoRegisterScene` (this file); external (runtime deferred registration).

- `u32 physGetBodyArchetypeCount(PhysicsWorld*)` — returns `bodyArchetypeCount`. External.
- `Archetype *physGetBodyArchetype(PhysicsWorld*, u32 index)` — bounds-checked accessor into `bodyArchetypes[]`. External.
- `PhysFieldBinding *physGetBindingByIndex(PhysicsWorld*, u32 index)` — returns cached binding pointer. **Not declared in druid.h**; exported at link level, presumably consumed by sibling physics TUs (queries/raycast). No callers within this file.

- `void physAutoRegisterScene(SceneData *scene)`
  Iterates `scene->archetypes[]` and calls `physRegisterArchetype(physicsWorld, ...)` for each with `ARCH_PHYSICS_BODY` set (scene loader sets this from `isPhysicsBody`). No-op if the singleton or scene is NULL. External callers (runtime, per druid.h "Re-registers scene archetypes with physics").

### Rigid-body operations (explicit-world)

All resolve `(arch, poolIdx)` via static `resolveBody` (`poolIdx = chunkIdx * chunkCapacity + localIdx`, as returned by `archetypeSpawnIn`); silently no-op if the archetype isn't registered or the chunk is out of range.

- `void physBodyApplyForce(PhysicsWorld*, Archetype*, u32 index, Vec3 force)` — `Force* += force`.
- `void physBodyApplyTorque(...)` — **stub**; no angular-velocity fields in the SoA layout yet.
- `void physBodyApplyImpulse(...)` — `vel += impulse * InvMass`; no-op if `InvMass == 0`.
- `void physBodyApplyImpulseAt(..., Vec3 point)` — ignores `point` (no angular term), forwards to `physBodyApplyImpulse`.
- `void physBodySetVelocity(...)` / `Vec3 physBodyGetVelocity(...)` — write/read `LinearVelocity*`.
- `Vec3 physBodyGetPosition(...)` — reads raw `Position*` (no collider offset applied).
All external callers (gameplay code); none within this file except the singleton wrappers.

### Rigid-body operations (singleton wrappers)

- `physApplyForce`, `physApplyImpulse`, `physSetVelocity`, `physGetVelocity`, `physGetPosition` — same signatures minus the world parameter; forward to the `physBody*` versions with the global `physicsWorld`. External callers.

### Physics-as-DLL

- `b8 loadPhysicsDLL(const c8 *dllPath, PhysicsDLL *out)`
  Zeroes `*out`, loads the DLL (`dllLoad`), resolves the exported symbol `"druidGetPhysicsSystem"` as a `GetPhysicsPluginFn`, calls it to fill `out->plugin`, and validates that `init`, `step`, and `shutdown` are all non-NULL (unloading and returning false otherwise). Sets `out->loaded = true` on success. **Nothing in physics.c ever calls `out->plugin.init/step/shutdown`** — this file only provides the loading machinery; the driver that swaps the built-in `physWorldStep` for `plugin.step` is external. No callers in this file.

- `void unloadPhysicsDLL(PhysicsDLL *dll)` — `dllUnload` + `loaded = false`. Does **not** call `plugin.shutdown`. External callers.

### Global singleton lifecycle

- `PhysicsWorld *physicsWorld` (exported global) — engine-wide world, NULL until `physInit`. Owned by this module.
- `PhysicsWorld *physInit(Vec3 gravity, f32 timestep)` — destroys any existing singleton, creates a fresh one, returns it. External (runtime/editor).
- `void physShutdown(void)` — destroys the singleton, NULLs it, and `memArenaReset(MEM_ARENA_PHYSICS)`. External.
- `void physTick(f32 dt)` — `physWorldStep(physicsWorld, dt)` if the singleton exists. External (app/editor loop).

### Debug

- `void physWorldDebugDraw(PhysicsWorld *world, PhysDebugBodyFn fn, void *userdata)`
  Invokes `fn(center, half, radius, shape, userdata)` for every entry in the **static** broadphase arrays (`g_bp*`, `g_bodyCount`) — i.e. the body list as of the last substep of the last step, including CCD-inflated radii. The `world` parameter is only NULL-checked; the data actually comes from file-scope globals. External callers (editor gizmos).

## Data Structures

### Public (druid.h)

- `PhysBodyType` enum — `PHYS_BODY_STATIC = 0`, `PHYS_BODY_DYNAMIC = 1`, `PHYS_BODY_KINEMATIC = 2`. Stored per-entity in the `PhysicsBodyType` u32 field. Missing field ⇒ treated as static everywhere (broadphase, integration, solver).
- `PhysShapeType` enum — `PHYS_SHAPE_SPHERE/BOX/CYLINDER/MESH/COUNT`. **Not used by physics.c**, which instead uses the macro tags `PSHAPE_SPHERE = 1` / `PSHAPE_BOX = 2` (defined in both druid.h and physics.c; comment: "match scene entity").
- `PhysMaterial { friction, restitution, density }` — declared in the header; not referenced in physics.c (collider-module concern).
- `ContactPoint { Vec3 point; Vec3 normal; f32 depth; }` — one contact; normal points A→B.
- `ContactManifold { u32 bodyA, bodyB; ContactPoint points[4]; u32 pointCount; }` — narrowphase output; this module always emits `pointCount = 1` and stores the **pair index** into `world->pairs` in both `bodyA` and `bodyB` (field names are misleading here). Lives in `world->manifolds[]` (embedded array), rebuilt every substep.
- `CollisionFn` / `TriggerFn` — legacy global callback types (`u32 self, u32 other`, chunk-local indices). `TriggerFn` is stored but never fired by this file.
- `ContactInfo { EntityRef self, other; Vec3 normal, point; f32 depth; }` + `EntityRef { Archetype *arch; u32 poolIdx; }` — payload for per-archetype `CollisionCallbacks` (`onCollideEnter/Exit`, `onTriggerEnter/Exit` on `arch->cbs`). Stack-constructed per event inside `physWorldStep`; header comment says normal "points from other toward self" while physics.c builds A's info with the A→B normal and flips it for B's callback.
- `PhysRay` / `PhysRayHit` — declared for the raycast API; unused in physics.c.
- `SpatialHashGrid` — opaque typedef, never defined; `world->grid` is a vestigial NULL pointer (comment: "unused — hash is managed as file-scope statics").
- `PHYSICS_BODY_FIELDS` macro — field pack for archetype definitions: `PhysicsBodyType`, `ColliderHandle`, `LinearVelocity`, `AngularVelocity`, `Force`, `Torque`, `Mass`, `InvMass`, `LinearDamping`, `AngularDamping`, `CollisionLayer`, `CollisionMask`. Of these, physics.c consumes only `PhysicsBodyType`, `LinearVelocity*`, `Force*`, `Mass`, `InvMass`, `LinearDamping`; the angular/layer/mask/handle fields are declared but unread here. Notably the macro does **not** include `Restitution`, `SphereRadius`, `ColliderHalf*/Offset*/Shape`, or `CCDEnabled`, which the binding also looks for.
- `PhysicsPlugin { init, step, shutdown }` / `PhysicsInitFn`/`PhysicsStepFn`/`PhysicsShutdownFn` / `GetPhysicsPluginFn` — DLL plugin vtable; filled by the DLL's `druidGetPhysicsSystem` export.
- `PhysicsDLL { DLLHandle dll; PhysicsPlugin plugin; b8 loaded; }` — caller-owned (stack or wherever the caller keeps it); populated by `loadPhysicsDLL`, torn down by `unloadPhysicsDLL`.
- `PhysDebugBodyFn` — per-body debug-draw callback (center, half-extents, bounding radius, shape tag).

### Private to physics.c

- `struct PhysicsWorld` — opaque to users. Fields: `gravity`, `fixedTimestep`, `solverIterations` (8), `substeps` (1), `accumulator`; `bodyArchetypes[16]` (borrowed `Archetype*`, owned by ECS/scene) + `bindings[16]` + `bodyArchetypeCount`; `pairs[65536]`/`pairCount`; `manifolds[32768]`/`manifoldCount` (both embedded — `sizeof(PhysicsWorld)` is several MB); legacy `onCollision`/`onTrigger`; unused `grid`; visibility LOD state (`visibility` borrowed frame-allocated buffer, `visCount`, `lodInterval`, `physFrameCounter`). Heap-allocated by `physWorldCreate` (MEM_TAG_PHYSICS), freed by `physWorldDestroy`. Not ECS-managed.
- `PhysFieldBinding` — cached per-archetype field indices (i32, −1 = absent) for every name listed under `physRegisterArchetype` above, plus `scaleFieldSize` to distinguish Vec3 vs f32 `Scale`. Value type inside `world->bindings[]`; built once at registration (stale if the layout changes afterward).
- `CollisionPair` — broadphase output: `(archA,chunkA,indexA)` / `(archB,chunkB,indexB)` archetype coordinates, cached `shapeA/shapeB`, and flat broadphase indices `bpIdxA/bpIdxB` for the CCD narrowphase. Embedded in `world->pairs[]`.
- **Flat broadphase body arrays** (file-scope statics, `MAX_PHYS_BODIES` = 1,048,576 entries each): `g_bpPosX/Y/Z` (world pos incl. collider offset), `g_bpRadius` (bounding-sphere radius; box diagonal for boxes; **inflated by `speed*subDt` for CCD bodies**), `g_bpHalfX/Y/Z` (0 for spheres), `g_bpShape`, `g_bpBodyType`, `g_bpArchIdx`/`g_bpChunkIdx`/`g_bpEntityIdx` (back-pointers into archetype storage), `g_bpVelX/Y/Z` (start-of-step velocity, CCD bodies only), `g_bpCCD`, `g_bodyCount`. Rebuilt from scratch every substep by `buildBodyList`; static storage, shared by **all** worlds; also read by `physWorldDebugDraw` after the step. These arrays are tens of MB of BSS.
- **Spatial hash** statics: `g_shTable[262144]` (bucket heads, power-of-two, hashed via three large-prime multiplies XORed), `g_shNext[]` (per-body chain), `g_shCellSize`/`g_shInvCell` (cell size = 2× the max bounding radius of **dynamic** bodies only, to stop huge statics collapsing the grid; min 0.01 → fallback 1.0), `g_shDirtyBuckets`/`g_shDirtyCount` (only touched buckets are cleared), ghost pool `g_shGhostBody/Next[4·MAX_PHYS_BODIES]` + `g_shGhostCount` (extra entries for bodies spanning multiple cells; encoded in buckets as `MAX_PHYS_BODIES + ghostIdx`), oversized sidelist `g_shOversized[256]` + count (bodies spanning > `SH_MAX_CELLS_PER_BODY` = 64 cells skip the hash and are brute-force paired against every dynamic body).
- **Contact-state tables**: `g_csPrev`/`g_csCurr` (`u64[131072]` open-addressing sets, 32-probe cap, sentinel `~0`), `g_csCurrCount`, `g_csInitialised`. Pair key = canonical `(minEntityKey << 32) | maxEntityKey` where an entity key packs arch(4b)/chunk(12b)/local(16b) — implying hard limits of 16 archetypes, 4096 chunks, 65536 entities/chunk for correct event dedup. Swapped/cleared each substep by `csSwapAndClear`.
- Broadphase-local dedup set: static `seenPairs[16384]`/`seenSlots` inside `broadphaseGetPairs`, cleaned via the slot list after each call.
- `PSHAPE_SPHERE`/`PSHAPE_BOX` macros, `MAX_PHYS_ARCHETYPES/PAIRS/MANIFOLDS/BODIES`, `CS_*`, `SH_*`, `SLOP` constants.

### Entity fields physics writes

`LinearVelocity*` (integration, damping, solver impulses, CCD zeroing, `physBodySetVelocity`/`physApplyImpulse`), `Position*` (position integration, position correction, CCD snap), `Force*` (gravity accumulation, `physBodyApplyForce`, then zeroed each substep), `InvMass` (lazy `1/Mass` for dynamic bodies). Everything else is read-only.

## Internal (static) functions

- `csEntityKey` / `csInsert` / `csContains` / `csSwapAndClear` — contact-state pair-key packing and prev/curr hash-set maintenance for enter/exit events.
- `sh_floor` / `sh_hash` — cell coordinate + bucket hash for the spatial grid.
- `sh_clear` — dirty-bucket-only reset of the hash, chains, and ghost/oversized counters.
- `sh_insertSingle` — push one body (or ghost) entry into a bucket, tracking dirty buckets.
- `sh_insert` — AABB-aware insertion: single cell if the body fits, multi-cell with ghosts otherwise, oversized sidelist beyond 64 cells.
- `findField` — linear name lookup in a `StructLayout`.
- `buildBinding` — constructs a `PhysFieldBinding` from field names (with fallbacks).
- `clampf_local` — scalar clamp helper.
- `effectiveRadius` / `effectiveHalf` — collider size with fallback to half of `Scale` (Vec3 or uniform f32), then 0.5 default.
- `inferShape` — explicit `ColliderShape` if valid, else box iff half-extents set and radius 0, else sphere.
- `buildBodyList` — flattens registered archetypes into the `g_bp*` arrays each substep (skips dead pooled entities and non-visible entities; expands CCD bounding radii by swept distance).
- `broadphaseGetPairs` — computes dynamic-only cell size, rebuilds the hash, queries each body's AABB (±1 cell) with dedup + bounding-sphere prune (`TRY_EMIT_PAIR` macro; static/static pairs rejected), then brute-forces the oversized list against all dynamic bodies.
- `sweptSphereVsBox` — slab test against the Minkowski-expanded AABB; returns first-hit `t` and outward face normal.
- `sweptSphereVsSphere` — quadratic ray-vs-expanded-sphere sweep.
- `contactSphereSphere` / `contactSphereBox` / `contactBoxBox` — one-point manifold generation (axis-aligned only; box-box uses minimum-overlap axis; sphere-center-inside-box handled via deepest-axis push-out).
- `resolveBody` — maps `(world, arch, poolIdx)` to `(binding, chunk fields, localIdx)` for the `physBody*` API.

## Open questions / unclear intent

1. **`world->onTrigger` / `physWorldSetTriggerCallback`** — the callback is stored but never invoked anywhere in physics.c; trigger events go exclusively through per-archetype `arch->cbs.onTriggerEnter/Exit`. Dead API or wired elsewhere?
2. **`world->grid` / `SpatialHashGrid`** — opaque type declared in druid.h, pointer field kept in the struct but explicitly commented "unused". Vestigial or planned?
3. **`physGetBindingByIndex`** — defined (non-static) but not declared in druid.h, and `PhysFieldBinding` itself is defined only inside physics.c. Which sibling TU consumes it, and via what declaration?
4. **DLL plugin never driven here** — `loadPhysicsDLL` validates `init/step/shutdown` but nothing in this file calls them, and `unloadPhysicsDLL` doesn't call `shutdown`. The component that substitutes `plugin.step` for the built-in `physWorldStep` is outside the given files.
5. **File-scope simulation state vs multiple worlds** — `g_bp*`, the spatial hash, and the contact-state tables are shared statics; two live `PhysicsWorld` instances would interleave state (and `physWorldDebugDraw` ignores its `world` argument beyond a NULL check). Single-world assumption appears intended but is undocumented.
6. **Visibility buffer indexing** — `buildBodyList`/gravity use a running `visIdx` across archetypes-in-registration-order × chunks × chunk count; the producer of `visible[]` must iterate in exactly the same order. That producer (frustum cull) is outside this module, so the contract can't be verified here.
7. **Force-field assumption** — step 1 and step 5 cast `fields[b->forceX/Y/Z]` without checking the bindings are ≥ 0 (unlike pos/vel/invMass), implying registered archetypes are assumed to always carry `Force*` fields; whether that's a guaranteed invariant of `PHYSICS_BODY_FIELDS` usage or an oversight isn't determinable from this file.
8. **`ContactManifold.bodyA/bodyB` semantics** — header names suggest body indices, but this module stores the collision-pair index in both; the legacy `CollisionFn` receives chunk-local `indexA/indexB` instead. External consumers of these fields would need to know which convention applies.
9. **`Restitution` field source** — the solver reads a `Restitution` per-entity field (default 0.4), but `PHYSICS_BODY_FIELDS` doesn't define one; unclear where archetypes are expected to get it (scene entity fields? manual layout?).
10. **Unbound fields in `PHYSICS_BODY_FIELDS`** — `AngularVelocity`, `Torque`, `AngularDamping`, `CollisionLayer`, `CollisionMask`, `ColliderHandle` are declared in the macro but never bound or read here (torque API is an explicit stub); `PhysRay.layerMask` likewise suggests layer filtering that this module doesn't implement. Presumably reserved for the collider/query sibling module or future work.
11. **Capacity overflow behavior** — the broadphase dedup set (`SEEN_SIZE` 16384, 16-probe cap) is much smaller than `MAX_PHYS_PAIRS` (65536), and `csInsert` gives up after 32 probes; in dense scenes pairs/events could be silently dropped or double-emitted. Intent (accepted trade-off vs bug) not stated.
12. **CCD response** — on swept hit the body is snapped and its velocity fully zeroed (no reflection/slide), and only the first CCD body of a pair sweeps (`ccdIsA = ccdA` wins when both have CCD). Whether "dead stop" is the intended CCD response isn't documented.
