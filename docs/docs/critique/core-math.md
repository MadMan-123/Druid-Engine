# core-math — critique

## Correctness / convention bugs

- **[structural] [READ-VERIFIED]** `quatToRotationMatrix` (math.c:713–740) and `getModel` (transform.c:31–45) store the **transpose** of the rotation under the module's own `m[col][row]` column-major convention. Worked example: for q = +90° about Z (`x=y=0, z=w=√2/2`), `quatToRotationMatrix` yields column 0 = (0,−1,0) while `mat4RotateZ(+90°)` (math.c:276) yields column 0 = (0,+1,0), and `quatRotateVec3(q, x̂)` = (0,+1,0). So quat-derived matrices rotate opposite to `quatRotateVec3`/`mat4RotateX/Y/Z`/`mat4LookAt` when applied via `mat4TransformVec4`. `mat4TransformDirection` (math.c:444–453) is *also* transposed vs `mat4TransformVec4` (math.c:413), which cancels the error only for quat-derived matrices — two errors masking each other. Visible effect on rendering: [NEEDS-RUNTIME].
- **[moderate] [READ-VERIFIED]** `mat4Translate(Mat4 in, Vec3 t)` (math.c:230) ignores `in` entirely; signature promises "translate this matrix". Zero callers in src — dead *and* a trap.
- **[moderate] [READ-VERIFIED]** `v3Tov3i` (math.c:97) casts through `(u32)` into `i32` fields; float→unsigned conversion of negatives is UB.
- **[trivial] [READ-VERIFIED]** `matAdd`-family loops compare `u32 x < aSize.x` where `aSize.x` is `i32` (math.c:139 etc.); negative size wraps to a huge loop.

## Dead / redundant code

- **[moderate] [READ-VERIFIED]** `mat4Add`, `mat4Sub`, `mat4ScaleMatrix` declared in druid.h:307–309, defined nowhere in the repo — guaranteed link failure for any consumer.
- **[moderate] [READ-VERIFIED / NEEDS-BUILD]** `mat4RotateZ` (math.c:276), `mat4Transpose` (:590), `mat4ToPointers` (:607), `pointersToMat4` (:618): defined `inline` with no druid.h declaration and zero callers repo-wide. Dead; and under strict C99 `inline` semantics no external definition is emitted, so future external calls would fail to link (build-mode dependent).
- **[moderate] [READ-VERIFIED]** The whole legacy `f32**` family — `matAdd/Sub/Mul/Div/matScale` (math.c:137–194), `matCreate`/`freeMat` (:196–213) — has zero callers repo-wide yet is exported. `matMul`/`matDiv` are element-wise despite the names. `matCreate` does size.x+1 separate `dalloc`s with no failure checks; cache-hostile row-pointer layout that fights the engine's data-oriented style. Delete the family.
- **[moderate] [READ-VERIFIED]** `quatRotateVec3` (math.c:700) and `quatTransform` (:747) are exact duplicates; both are live (gizmo.c uses one, camera.c the other), splitting the API.
- **[trivial] [READ-VERIFIED]** Unused `<stdio.h>` (math.c:2); unused `PI_HALFRES`/`PI_QUATERRES` (druid.h:332–333); `mat4Perspective`/`mat4Ortho` each declared twice (druid.h:290–291 and 320–321); `mat4Inverse` (math.c:480) recomputes the first-column cofactors `mat4Determinant` just computed.

## SIMD (simd.c)

- **[structural] [READ-VERIFIED]** Every SSE2 path strides `i += 4` with no remainder handling (e.g. `simdAdd`:28): any `count % 4 != 0` reads/writes out of bounds. The contract exists only as a comment (simd.c:5) referencing a `SIMD_WIDTH` macro that doesn't exist; druid.h:346–379 never states it. Worse, AVX2 paths *do* handle remainders (simdDot3:266), so the same function's contract changes with compile flags. Actual overrun crash: [NEEDS-RUNTIME].
- **[moderate] [READ-VERIFIED]** `simdMinReduce`/`simdMaxReduce` (simd.c:514, 538) return `0.0f` for `count == 0` — indistinguishable from a real result; silent failure.
- **[trivial] [READ-VERIFIED]** `simdRsqrt`/`simdRcp` are refined estimates, not IEEE-exact; druid.h doesn't say so.

## API ergonomics / silent failures

- **[moderate] [READ-VERIFIED]** `mat4Inverse` (math.c:482) silently returns identity for singular matrices — callers cannot detect failure.
- **[trivial] [READ-VERIFIED]** No `Quat` typedef: quaternions are raw `Vec4`, so nothing stops passing a plain vector; druid.h:272 even names `quatFromEuler`'s param `axis`. `getModel` (transform.c:5) silently returns identity on NULL, hiding caller bugs.

## Categories with no findings

Missing pooling / per-frame heap churn: none (module is allocation-free except dead `matCreate`).
