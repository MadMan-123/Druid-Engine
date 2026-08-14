# physics-shapes — critique

## Structural

- **[structural][READ-VERIFIED] Stale `PhysFieldBinding` mirror in broadphase.c corrupts radius lookup.** physics.c:21–39 defines the real 22-field struct; narrowphase.c:9–25 matches it exactly, but broadphase.c:9–17 declares a 12-field version that diverges after `velZ`. Its `radius` (7th member, offset 24) lands on the real struct's `forceX`. So `physBroadphaseQuery` (broadphase.c:71) reads the *ForceX field index* as the radius index: bodies with a force field get their force-X array treated as radii; bodies without get `r=0` point AABBs. Every query result is wrong or degenerate.
- **[structural][READ-VERIFIED] `createMeshCollider` (collider.c:300) produces a poisoned collider.** It tags `COLLIDER_MESH` but stores `LegacyMeshState {Mesh*, Transform*}`; every `COLLIDER_MESH` consumer — `cleanCollider` (:330), `colliderComputeAABB` (:411), `isMeshVsSphere` (:555), `isMeshVsBox` (:591) — casts to `MeshState` and will read the `Mesh*` as a vertex pointer / free wild pointers. `LegacyMeshState` is never read anywhere: dead type + UB trap in one.
- **[structural][READ-VERIFIED] contact.c is 100% stubs with silent no-op setters.** Creators WARN and return `(u32)-1` (no documented invalid-handle constant in druid.h:2624–2633); `physColliderDestroy/SetOffset/SetTrigger/SetMaterial` (contact.c:37–55) don't even WARN, and `physColliderComputeAABB` (:57) silently returns a point AABB. Callers get plausible garbage with no signal.
- **[structural][READ-VERIFIED] `physRaycastAll` (narrowphase.c:172) is a diverged copy-paste of `physRaycast` that dropped the box path** — never reads `halfX/Y/Z`, so box bodies are raycast as radius-0.5 spheres. Same-file `physRaycast` handles boxes, so callers get inconsistent results between the two.
- **[structural][READ-VERIFIED] `bodyIndex`/query outputs are unusable with >1 archetype.** `hit.bodyIndex = c * chunkCapacity + i` (narrowphase.c:143,164,218; broadphase.c:82) omits the archetype; the caller cannot map a hit back to an entity without physics.c internals.

## Moderate

- **[moderate][READ-VERIFIED] Raycasts ignore binding fields `offsetX/Y/Z` and `shape`** (present in the true binding, physics.c:33–34) — rays test entity origin, diverging from the collision step's offset AABBs, and box-vs-sphere is re-inferred from `half > 0` instead of the cached shape. `ray.layerMask` (druid.h:2554) is also never read in either raycast.
- **[moderate][READ-VERIFIED] Two heap allocs per collider for ≤8-byte payloads** (all `create*Collider`, collider.c:164–318). A union inside `Collider` removes the `state` indirection, the churn, and the entire `dfree(state, 0, ...)` question — `cleanCollider`:338 frees with size 0 while allocs used real sizes, risking allocator accounting drift **[NEEDS-BUILD]** (depends on `dfree` contract).
- **[moderate][READ-VERIFIED] `collidersOverlap` silently degrades MESH-vs-MESH, MESH-vs-CYLINDER, and all CIRCLE/BOX_2D pairs to AABB tests** (collider.c:696–699); BOX_2D's AABB is min-corner-based (:421) while everything else is centered, so mixed 2D/3D fallbacks are geometrically inconsistent.
- **[moderate][READ-VERIFIED] Dead `SpatialHashGrid` definition** (broadphase.c:25–34): never instantiated; physics.c:266 comments its `world->grid` is unused and the druid.h:2558 comment claims it's defined in physics.c.

## Trivial

- [trivial][READ-VERIFIED] narrowphase.c:137 `if (tmin > hit.distance) continue;` is unreachable — `tmax` starts at `hit.distance` and `tmin > tmax` already bailed.
- [trivial][READ-VERIFIED] `isCircleColliding` (collider.c:443) uses `v2Mag` (sqrt) where every 3D test uses squared distance.
- [trivial][READ-VERIFIED] Six-line `layer/isColliding/isTrigger/response/onTrigger` init block copy-pasted into all 7 constructors (collider.c); a common `colliderInit` or designated-init would do. Those fields are never read in this module.
- [trivial][READ-VERIFIED] Self-congratulatory "OPTIMIZED: O(1) cached binding lookup, no strcmp" comment (broadphase.c:58) on the very lookup that dereferences the wrong struct layout.
- [trivial][READ-VERIFIED] `contact.c` contains zero contact-manifold code (manifolds live in physics.c) — misleading filename.
- [trivial][READ-VERIFIED] Sphere raycast math assumes normalized `ray.direction` (narrowphase.c:150–156) — undocumented; wrong distances for unnormalized input.

## No findings

- Per-frame allocations: none (queries write caller buffers; no allocs in narrowphase/broadphase).
