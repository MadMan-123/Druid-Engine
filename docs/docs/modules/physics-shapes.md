# physics-shapes

Files: `src/systems/Physics/collider.c`, `contact.c`, `narrowphase.c`, `broadphase.c`, `rigidbody.c`.
Public declarations: `include/druid.h` (~lines 2466–2530 "Colliders", 2532–2657 "Physics Engine" query/collider sections). There are no per-module headers; "exported" means declared in `druid.h` with `DAPI`.

## Synopsis

This module implements collision shape primitives and geometric queries for the Druid engine's physics. It contains two parallel, generational APIs: the older heap-object `Collider` API in `collider.c` (`create*Collider`, `collidersOverlap`, pairwise `is*Vs*` tests) and the newer handle-based `physCollider*` API in `contact.c`, which is entirely stubbed out. `narrowphase.c` and `broadphase.c` provide raycasting and AABB queries against ECS-stored physics bodies owned by a `PhysicsWorld` (created/stepped in `physics.c`, not part of this module). Consumers visible from `druid.h` include the gizmo system (`gizmoDrawCollider`, `gizmoDrawAABB`) and game code.

### Old vs new API relationship (as visible from these files)

- **Old API (`collider.c`)**: fully implemented. `Collider` is a standalone heap object (`dalloc`, `MEM_TAG_PHYSICS`) with a type tag and an opaque `state` pointer to a per-shape struct. Overlap tests are position-parameterized (caller supplies `Vec3 pos`; the collider stores no transform, except the legacy `createMeshCollider` variant which stores a `Transform*`). Boolean-only results — no contact manifolds.
- **New API (`contact.c`)**: `u32` handles owned by a `PhysicsWorld`, with per-collider offset, trigger flag, and `PhysMaterial`. **Every function is a stub**: the four `physColliderCreate*` functions log a `WARN` and return `(u32)-1`; the setters are empty; `physColliderComputeAABB` returns a degenerate point AABB `{pos, pos}`. The `WARN` in `physColliderCreateSphere` says "use SphereRadius field", implying the working path stores shape data directly as ECS archetype fields (radius / half-extents), which is exactly what `narrowphase.c`/`broadphase.c` read via `PhysFieldBinding`. Despite the file name, `contact.c` contains no contact-manifold code; `ContactPoint`/`ContactManifold` are declared in `druid.h` but not produced anywhere in this module.
- The `druid.h` archetype macro `PHYSICS_BODY_FIELDS` includes a `FIELD(u32, ColliderHandle)`, the intended consumer of the stubbed handle API.

## Public API Reference

### Old-style collider construction (collider.c)

All constructors: allocate a `Collider` plus a shape-specific state struct with `dalloc(..., MEM_TAG_PHYSICS)`, initialize `layer=0`, `isColliding=false`, `isTrigger=false`, `response=NULL`, `onTrigger=NULL`; return `NULL` on allocation failure (partial allocations are freed). No call sites within this module's files — callers are external (game code / other systems).

- `Collider *createCircleCollider(f32 radius)` — legacy 2D circle (`COLLIDER_CIRCLE`, `CircleState`).
- `Collider *createBoxCollider(Vec2 scale)` — legacy 2D box (`COLLIDER_BOX_2D`, `Box2DState`). Note: 2D box is min-corner + scale, not centered.
- `Collider *createSphereCollider(f32 radius)` — 3D sphere (`COLLIDER_SPHERE`, `SphereState`).
- `Collider *createCubeCollider(Vec3 halfExtents)` — 3D axis-aligned box (`COLLIDER_BOX`, `BoxState`), centered, half-extents.
- `Collider *createCylinderCollider(f32 radius, f32 halfHeight)` — Y-axis-aligned cylinder (`COLLIDER_CYLINDER`, `CylinderState`).
- `Collider *createMeshCollider3D(const Vec3 *vertices, const u32 *indices, u32 vertexCount, u32 indexCount)` — triangle-mesh collider (`COLLIDER_MESH`, `MeshState`). Deep-copies vertices and (optionally) indices; precomputes a local-space AABB via `computeMeshAABB`. Returns `NULL` if `vertices == NULL` or `vertexCount == 0`. If `indices` is NULL/empty, triangles are taken as consecutive vertex triples.
- `Collider *createMeshCollider(Mesh *mesh, Transform *transform)` — legacy variant; also tags `COLLIDER_MESH` but stores a `LegacyMeshState` (non-owning `Mesh*`/`Transform*` pointers). **Hazard visible in-file**: every other `COLLIDER_MESH` consumer (`cleanCollider`, `colliderComputeAABB`, `isMeshVsSphere`, `isMeshVsBox`) casts `state` to `MeshState*`, so a collider from this constructor would be misinterpreted by all of them. No caller of this constructor exists in these files.

### Old-style utilities (collider.c)

- `b8 cleanCollider(Collider *col)` — frees `state` (for `COLLIDER_MESH`: vertices, indices, then `MeshState`; assumes `MeshState` layout, see above) then the `Collider`. Returns `false` only if `col` is NULL. Note non-mesh states are freed with `dfree(col->state, 0, MEM_TAG_PHYSICS)` — size argument 0 with a comment that they're "small fixed-size structs" (whether size 0 is legal depends on the `dfree` contract in the memory module).
- `f32 getRadius(Collider *col)` — radius for CIRCLE/SPHERE/CYLINDER, else 0.
- `Vec2 getScale(Collider *col)` — 2D box scale, else `{0,0}`.
- `Vec3 getHalfExtents(Collider *col)` — 3D box half-extents, else `{0,0,0}`.
- `b8 setBoxScale(Collider *col, Vec2 scale)` — mutates 2D box scale; `false` for other types.
- `AABB colliderComputeAABB(Collider *col, Vec3 pos)` — world AABB for any collider type at `pos`. Mesh: precomputed local AABB translated by `pos` (translation only, no rotation/scale). 2D shapes get z=0 AABBs; 2D box uses `pos` as min corner. Called internally by `collidersOverlap` (fallback path); likely also external (gizmos).
- `b8 isAABBOverlapping(AABB a, AABB b)` — inclusive-boundary AABB intersection test. Called by `collidersOverlap` (fallback) and by `physBroadphaseQuery` in broadphase.c; likely also external.

### Pairwise overlap tests (collider.c)

All are pure boolean tests, no side effects, no contact info. All called by `collidersOverlap`; also exported for direct use.

- `b8 isCircleColliding(Vec2 posA, f32 radA, Vec2 posB, f32 radB)` — legacy 2D circle test (uses `v2Mag`, non-squared). Not referenced by `collidersOverlap` (no CIRCLE dispatch case).
- `b8 isBoxColliding(Vec2 posA, Vec2 scaleA, Vec2 posB, Vec2 scaleB)` — legacy 2D min-corner AABB test. Also not dispatched by `collidersOverlap`.
- `b8 isSphereVsSphere(Vec3, f32, Vec3, f32)` — squared-distance vs summed radii.
- `b8 isBoxVsBox(Vec3 posA, Vec3 halfA, Vec3 posB, Vec3 halfB)` — center/half-extent AABB test.
- `b8 isSphereVsBox(Vec3 spherePos, f32 radius, Vec3 boxPos, Vec3 boxHalf)` — closest-point-on-box clamp then distance check.
- `b8 isSphereVsCylinder(Vec3 spherePos, f32 sphereRad, Vec3 cylPos, f32 cylRad, f32 cylHalfH)` — clamps sphere center into cylinder volume (Y clamp + XZ radial clamp) then distance check.
- `b8 isCylinderVsCylinder(Vec3 posA, f32 radA, f32 halfHA, Vec3 posB, f32 radB, f32 halfHB)` — Y-interval overlap plus XZ circle overlap.
- `b8 isBoxVsCylinder(Vec3 boxPos, Vec3 boxHalf, Vec3 cylPos, f32 cylRad, f32 cylHalfH)` — Y-interval overlap plus closest-point-on-box-XZ-footprint vs cylinder radius.
- `b8 isMeshVsSphere(Collider *meshCol, Vec3 meshPos, Vec3 spherePos, f32 sphereRad)` — brute-force per-triangle `closestPointOnTriangle` distance test; supports indexed and non-indexed meshes; triangles translated by `meshPos`. Requires `meshCol->type == COLLIDER_MESH` with a `MeshState`.
- `b8 isMeshVsBox(Collider *meshCol, Vec3 meshPos, Vec3 boxPos, Vec3 boxHalf)` — brute-force per-triangle SAT test via `triangleVsAABB`.

### Generic dispatch (collider.c)

- `b8 collidersOverlap(Collider *a, Vec3 posA, Collider *b, Vec3 posB)` — type-pair dispatch (both orders) covering SPHERE/BOX/CYLINDER pairs plus MESH-vs-SPHERE and MESH-vs-BOX. Unhandled pairs (anything with CIRCLE or BOX_2D, MESH-vs-CYLINDER, MESH-vs-MESH) fall through to a conservative AABB-vs-AABB test via `colliderComputeAABB` + `isAABBOverlapping`. Returns `false` if either collider or its state is NULL. Callers: external only (probably the collision step in `physics.c` or game code).

### Handle-based collider API (contact.c) — all stubs

- `u32 physColliderCreateSphere(PhysicsWorld *world, f32 radius)` — logs `WARN("...not yet implemented, use SphereRadius field")`, returns `(u32)-1`.
- `u32 physColliderCreateBox(PhysicsWorld *world, Vec3 halfExtents)` — WARN + `(u32)-1`.
- `u32 physColliderCreateCylinder(PhysicsWorld *world, f32 radius, f32 halfHeight)` — WARN + `(u32)-1`.
- `u32 physColliderCreateMesh(PhysicsWorld *world, const Vec3 *verts, const u32 *indices, u32 vertCount, u32 idxCount)` — WARN + `(u32)-1`.
- `void physColliderDestroy(PhysicsWorld *world, u32 handle)` — no-op.
- `void physColliderSetOffset(PhysicsWorld *world, u32 handle, Vec3 offset)` — no-op.
- `void physColliderSetTrigger(PhysicsWorld *world, u32 handle, b8 isTrigger)` — no-op.
- `void physColliderSetMaterial(PhysicsWorld *world, u32 handle, PhysMaterial mat)` — no-op.
- `AABB physColliderComputeAABB(PhysicsWorld *world, u32 handle, Vec3 pos, Vec4 rot)` — returns point AABB `{pos, pos}`; ignores handle and rotation.

No call sites in this module; callers would be external.

### Raycasting (narrowphase.c)

- `PhysRayHit physRaycast(PhysicsWorld *world, PhysRay ray)` — closest-hit raycast over every body in every physics-registered archetype. Iterates `physGetBodyArchetypeCount`/`physGetBodyArchetype`, resolves per-archetype ECS field arrays via `physGetBindingByIndex` (implemented in `physics.c`) and `getArchetypeFields`. Per body: if any of `HalfX/Y/Z` fields is > 0 it is treated as an AABB and tested with slab intersection (hit normal = face axis); otherwise as a sphere of radius from the radius field (default 0.5 if the archetype has no radius field). Tracks the nearest hit (`hit.distance` initialized to `ray.maxDistance`, tightened as hits are found; the box path also uses the running `hit.distance` as `tmax`, so it early-outs on farther boxes). `hit.bodyIndex = chunkIndex * arch->chunkCapacity + i` — an index within one archetype's chunk layout; the archetype itself is not returned in the hit. `ray.layerMask` is never read. `t <= 0` hits (origin inside/behind) are skipped. No side effects. Callers external.
- `u32 physRaycastAll(PhysicsWorld *world, PhysRay ray, PhysRayHit *hits, u32 maxHits)` — collects up to `maxHits` hits into caller-provided array, returns count. **Sphere-only**: does not read half-extent fields, so box bodies are also treated as spheres (radius field or 0.5 default). Unlike `physRaycast`, if the near root is negative it accepts the far root (rays starting inside a sphere hit its backside). Results unsorted. Callers external.

### Broadphase (broadphase.c)

- `void physBroadphaseRebuild(PhysicsWorld *world)` — intentional no-op. Comment states pair generation is done brute-force N*N inline in `physWorldStep` (physics.c) and this is reserved for a future spatial-hash rebuild.
- `u32 physBroadphaseQuery(PhysicsWorld *world, AABB query, u32 *outBodies, u32 maxBodies)` — writes body indices (same `chunk * chunkCapacity + i` scheme as raycasts) whose bounding AABB overlaps `query`, returns count. The per-body AABB is a cube of half-size = radius field (0 if archetype lacks a radius field, producing a point AABB — box bodies' half-extents are not consulted). Uses `isAABBOverlapping` from collider.c. Callers external.

### rigidbody.c

Contains only `#include "../../../include/druid.h"` — a placeholder translation unit. The rigidbody functions declared in `druid.h` (`physBodyApplyForce`, `physApplyImpulse`, etc.) are not implemented here.

## Data Structures

### Public (druid.h)

- `ColliderType` (enum) — `COLLIDER_CIRCLE=0, COLLIDER_BOX_2D=1, COLLIDER_SPHERE=2, COLLIDER_BOX=3, COLLIDER_CYLINDER=4, COLLIDER_MESH=5`. Tags `Collider.type`.
- `AABB` — `{ Vec3 min; Vec3 max; }`. Value type, stack/embedded; used by both old and new APIs and by gizmos.
- `Collider` — `{ void *state; void (*response)(u32 self, u32 other); void (*onTrigger)(u32 self, u32 other); ColliderType type; i32 layer; b8 isColliding; b8 isTrigger; }`. Plain heap object (NOT ECS-managed), allocated by the `create*Collider` functions via `dalloc(MEM_TAG_PHYSICS)`, freed by `cleanCollider`. `response`/`onTrigger`/`layer`/`isColliding`/`isTrigger` are initialized but never read or invoked anywhere in this module's files — presumably consumed by an external collision loop (physics.c or game code).
- `PhysMaterial` — `{ f32 friction; f32 restitution; f32 density; }`. Value type; only appears here as the ignored parameter of stub `physColliderSetMaterial`. (Task brief calls this "PhysicsMaterial"; the actual `druid.h` name is `PhysMaterial`.)
- `PhysRay` — `{ Vec3 origin; Vec3 direction; f32 maxDistance; u32 layerMask; }`. Caller-owned value. `direction` is assumed normalized by the sphere-intersection math; `layerMask` is unused in this module.
- `PhysRayHit` — `{ Vec3 point; Vec3 normal; f32 distance; u32 bodyIndex; b8 hit; }`. Caller-owned value/array. `bodyIndex` encodes `chunk * chunkCapacity + localIndex` within an unspecified archetype.
- `ContactPoint` / `ContactManifold` / `CollisionFn` / `TriggerFn`, `PHYSICS_BODY_FIELDS` (declared in druid.h near these types) — not used by any code in this module's files; documented owners live elsewhere (physics.c).
- `SpatialHashGrid` — forward-declared opaque in druid.h (comment says "defined in physics.c").

### Internal (collider.c) — per-shape `state` payloads

All heap-allocated with the owning `Collider` (`dalloc`/`dfree`, `MEM_TAG_PHYSICS`), plain heap, not ECS:

- `CircleState { f32 radius; }`
- `Box2DState { Vec2 scale; }`
- `BoxState { Vec3 halfExtents; }`
- `SphereState { f32 radius; }`
- `CylinderState { f32 radius; f32 halfHeight; }`
- `MeshState { Vec3 *vertices; u32 *indices; u32 vertexCount; u32 indexCount; AABB aabb; }` — owns deep copies of vertex/index data; local-space AABB cached at creation.
- `LegacyMeshState { Mesh *mesh; Transform *transform; }` — non-owning references; only written by `createMeshCollider`, never read in these files.

### Internal (narrowphase.c / broadphase.c)

- `PhysFieldBinding` (narrowphase.c) — local mirror of a struct "in physics.c": 21 `i32` field-index slots (`posX..posZ, velX..velZ, forceX..forceZ, bodyType, mass, invMass, restitution, damping, radius, halfX..halfZ, offsetX..offsetZ, shape, ccdEnabled, scaleField`) plus `u32 scaleFieldSize`. `-1` means "archetype lacks this field". Owned/populated by physics.c; accessed via `extern PhysFieldBinding *physGetBindingByIndex(PhysicsWorld*, u32)`.
- `PhysFieldBinding` (broadphase.c) — a **different, shorter** local mirror of the same physics.c struct: `posX..posZ, velX..velZ, radius, halfX..halfZ, bodyType, mass` (12 `i32`). The two mirrors diverge after `velZ` (broadphase's `radius` sits at the offset of narrowphase's `forceX`), so at most one matches physics.c's real layout. Both files only read `posX/posY/posZ`, `radius`, and (narrowphase only) `halfX/Y/Z`.
- `struct SpatialHashGrid` (broadphase.c) — `{ f32 cellSize; u32 tableSize; u32 *cellStart; u32 *cellCount; u32 *entries; u32 entryCapacity; u32 entryCount; }`. Definition for the druid.h opaque type; labeled "placeholder for future spatial partitioning". Never instantiated in these files. Note druid.h's comment says it is defined in physics.c, while the definition here is in broadphase.c.

## Internal (static) functions

All in `collider.c`:

- `clampf` — scalar clamp helper.
- `closestPointOnTriangle` — closest point on triangle to a point (Voronoi-region method); used by `isMeshVsSphere`.
- `triangleVsAABB` — SAT triangle-vs-centered-AABB test (9 edge-cross axes, 3 face axes, triangle normal); used by `isMeshVsBox`.
- `computeMeshAABB` — min/max over a vertex array; used by `createMeshCollider3D`.

`narrowphase.c` and `broadphase.c` have no static functions, only the `extern` declaration of `physGetBindingByIndex` (implemented in physics.c).

## Open questions / unclear intent

1. **Which `PhysFieldBinding` mirror is correct?** narrowphase.c and broadphase.c each declare a local `PhysFieldBinding` said to match physics.c "exactly", but the two declarations differ in layout after the first six fields. The authoritative struct lives in physics.c (not read in this pass); which mirror is stale cannot be determined from these files.
2. **Who invokes `Collider.response`/`onTrigger` and reads `layer`/`isColliding`/`isTrigger`?** Initialized in every constructor but never used within these files — presumably the collision loop in physics.c or game code.
3. **Is `createMeshCollider` (LegacyMeshState) dead code?** Its state layout is incompatible with every `COLLIDER_MESH` consumer in collider.c (`cleanCollider`, `colliderComputeAABB`, mesh tests all assume `MeshState`), and no caller appears in these files. Whether any external caller still uses it is unknown.
4. **`dfree(ptr, 0, tag)` semantics** — `cleanCollider` frees non-mesh states with size 0. Whether the memory subsystem's `dfree` tolerates/expects a 0 size (vs. producing accounting drift) is defined in the memory module, not here.
5. **`hit.bodyIndex` addressing** — `PhysRayHit.bodyIndex` and `physBroadphaseQuery` output use `chunk * chunkCapacity + i` but do not identify which archetype the index belongs to; with multiple registered archetypes the mapping back to an entity is ambiguous from this module alone. How physics.c/callers resolve this is unknown.
6. **Intended fate of the stubbed `physCollider*` API and `ColliderHandle` field** — contact.c's WARN messages ("use SphereRadius field") imply shape data currently lives directly in archetype fields; whether the handle system is actively being built (and how `PHYSICS_BODY_FIELDS`' `ColliderHandle` is currently consumed) is only resolvable in physics.c.
7. **`PhysRay.layerMask` and `Collider.layer` filtering** — declared but never consulted in any query in these files.
8. **`physRaycastAll` box omission** — it treats all bodies as spheres while `physRaycast` handles boxes; whether this is intentional (fast path) or unfinished is not determinable here.
9. **`rigidbody.c` placeholder** — contains only the druid.h include; the `physBody*`/`phys*` rigidbody functions declared in druid.h must be implemented elsewhere (presumably physics.c).
10. **`isCircleColliding` inclusive/2D dispatch gap** — the 2D shapes (CIRCLE, BOX_2D) have exported tests but no case in `collidersOverlap` (they fall to the AABB fallback, where BOX_2D's AABB uses `pos` as min-corner unlike all 3D shapes). Whether 2D support is deprecated or handled by a separate 2D path elsewhere is unclear.
