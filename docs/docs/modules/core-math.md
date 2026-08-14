# core-math

Files: `src/core/math.c`, `src/core/simd.c`, `src/core/transform.c`.
Public declarations: `include/druid.h` (MATHS section, lines ~194–344; SIMD section, lines ~346–379; transform section, lines ~1229–1239). There are no per-module headers; "exported" means declared with `DAPI` in `druid.h`.

## Synopsis

Foundational math layer for the Druid engine: scalar/vector (Vec2/Vec3, integer variants), quaternion (stored as `Vec4`), and 4x4/3x3 matrix operations in column-major layout for OpenGL, plus batch SoA float-array SIMD kernels (SSE2, with AVX2 paths for the Vec3 SoA ops) and a `Transform` → model-matrix builder. It owns no long-lived state — every function is pure or writes to caller-provided buffers; the only allocation is the legacy `f32**` matrix helpers (`matCreate`/`freeMat`), which go through the engine's memory module (`dalloc`/`dfree`, `MEM_TAG_ARRAY`). Essentially every other engine subsystem (renderer, camera, physics, ECS systems) is a downstream consumer; core-math itself depends only on libc `<math.h>` and the memory allocator.

## Public API Reference

All functions below are `DAPI`-declared in `druid.h` unless noted. Most are defined `inline` in the `.c` file. None have side effects beyond writing their `out` parameters unless stated.

### Vec2 ops (`math.c`)

| Function | Signature | Purpose |
|---|---|---|
| `v2Add` / `v2Sub` / `v2Mul` | `Vec2 f(Vec2 a, Vec2 b)` | Component-wise add / subtract / multiply. |
| `v2Scale` | `Vec2 v2Scale(Vec2 a, f32 b)` | Multiply both components by scalar `b`. |
| `v2Div` | `Vec2 v2Div(Vec2 a, f32 b)` | Divide both components by scalar `b` (no zero check). |
| `v2Mag` | `f32 v2Mag(Vec2 a)` | Euclidean length. |
| `v2Dis` | `f32 v2Dis(Vec2 a, Vec2 b)` | Distance between points (`v2Mag(v2Sub(b,a))`). |
| `v2Dot` | `f32 v2Dot(Vec2 a, Vec2 b)` | Dot product. |
| `v2Equal` | `b8 v2Equal(Vec2 a, Vec2 b)` | Exact float equality of both components. |
| `v2Tov2i` / `v2iTov2` | `Vec2i(Vec2)` / `Vec2(Vec2i)` | Cast conversions between float and int variants (`v2Tov2i` truncates via `(i32)`). |

Internal callers: `v2Dis` calls `v2Mag`/`v2Sub`. All others have no call sites within this module — callers are external.

### Vec3 ops (`math.c`)

| Function | Signature | Purpose |
|---|---|---|
| `v3Add` / `v3Sub` / `v3Mul` | `Vec3 f(Vec3 a, Vec3 b)` | Component-wise add / subtract / multiply. |
| `v3Scale` | `Vec3 v3Scale(Vec3 a, f32 b)` | Scalar multiply. |
| `v3Div` | `Vec3 v3Div(Vec3 a, f32 b)` | Scalar divide (no zero check). |
| `v3Mag` | `f32 v3Mag(Vec3 a)` | Euclidean length. |
| `v3Dis` | `f32 v3Dis(Vec3 a, Vec3 b)` | Distance between points. |
| `v3Norm` | `Vec3 v3Norm(Vec3 a)` | Normalize; returns `{0,0,0}` if magnitude ≤ 1e-5. |
| `v3Cross` | `Vec3 v3Cross(Vec3 a, Vec3 b)` | Cross product. |
| `v3Dot` | `f32 v3Dot(Vec3 a, Vec3 b)` | Dot product. |
| `v3Equal` | `b8 v3Equal(Vec3 a, Vec3 b)` | Exact float equality. |
| `v3Tov3i` / `v3iTov3` | `Vec3i(Vec3)` / `Vec3(Vec3i)` | Cast conversions. Note: `v3Tov3i` casts through `(u32)` even though `Vec3i` fields are `i32`. |

Internal callers: `v3Dis`/`v3Norm` use `v3Mag`, `v3Sub`, `v3Div`; `mat4LookAt` uses `v3Norm`, `v3Sub`, `v3Cross`, `v3Dot`; `quatFromAxisAngle` uses `v3Mag`. Others: external.

### Legacy dynamic `f32**` matrices (`math.c`)

| Function | Signature | Purpose |
|---|---|---|
| `matAdd` / `matSub` / `matMul` / `matDiv` | `void f(f32** a, f32** b, Vec2i aSize)` | In-place **element-wise** op on `a` (`a[x][y] op= b[x][y]`). Note `matMul`/`matDiv` are Hadamard, not linear-algebra products. Side effect: mutates `a`. |
| `matScale` | `void matScale(f32** a, f32 b, Vec2i aSize)` | In-place scalar multiply of every element. |
| `matCreate` | `f32** matCreate(Vec2i size)` | Allocates a `size.x × size.y` row-pointer matrix via `dalloc(..., MEM_TAG_ARRAY)`. Header comment: "creates a heap allocated matrix, try avoid using this // TODO: refactor". Caller owns; must free with `freeMat`. |
| `freeMat` | `void freeMat(f32** mat, Vec2i size)` | Frees each row then the pointer array via `dfree` with matching sizes/tag. |

No call sites within this module; callers external.

### Mat4 (column-major, `m[col][row]`, OpenGL convention) (`math.c`)

Constructors:

| Function | Signature | Purpose |
|---|---|---|
| `mat4Identity` | `Mat4 mat4Identity(void)` | Identity matrix. Heavily used internally (all rotate/scale constructors, `mat4LookAt`, `mat4Inverse` failure path, `mat3ToMat4`, `quatToRotationMatrix`, `getModel` null-guard). |
| `mat4Zero` | `Mat4 mat4Zero(void)` | All-zero matrix. Used by `mat4Mul`, `mat4Perspective`, `mat4Ortho`. |
| `mat4Translate` | `Mat4 mat4Translate(Mat4 in, Vec3 t)` | Builds a translation matrix with `t` in column 3. **The `in` parameter is ignored** — result is identity + translation, not `in` translated. |
| `mat4Scale` / `mat4ScaleVec` | `Mat4(f32)` / `Mat4(Vec3)` | Uniform / per-axis scale matrix. |
| `mat4RotateX` / `mat4RotateY` / `mat4RotateZ` | `Mat4 f(f32 angleRadians)` | Single-axis rotation matrices. `mat4RotateZ` is defined in `math.c` but **not declared in druid.h**. |
| `mat4Rotate` | `Mat4 mat4Rotate(f32 angleRadians, Vec3 axis)` | Axis-angle rotation (Rodrigues form); normalizes `axis` if length > 1e-4. |
| `mat4Perspective` | `Mat4 mat4Perspective(f32 fovRadians, f32 aspect, f32 nearZ, f32 farZ)` | OpenGL-style perspective projection (clip z in [-1,1]). Declared twice in druid.h (lines 290 and 320). |
| `mat4Ortho` | `Mat4 mat4Ortho(f32 l, f32 r, f32 b, f32 t, f32 nearClip, f32 farClip)` | Orthographic projection. Also declared twice. |
| `mat4LookAt` | `Mat4 mat4LookAt(Vec3 eye, Vec3 center, Vec3 up)` | View matrix from eye/target/up. |

Operations:

| Function | Signature | Purpose |
|---|---|---|
| `mat4Mul` | `Mat4 mat4Mul(Mat4 a, Mat4 b)` | Matrix product `a*b` (column-major accumulation). |
| `mat4ScaleVal` | `Mat4 mat4ScaleVal(Mat4 a, f32 scale)` | Every element multiplied by `scale`. |
| `mat4Determinant` | `f32 mat4Determinant(Mat4 m)` | Cofactor-expansion determinant. Called by `mat4Inverse`. |
| `mat4Inverse` | `Mat4 mat4Inverse(Mat4 m)` | Adjugate/determinant inverse; returns identity if `|det| < 1e-4`. |
| `mat4Transpose` | `Mat4 mat4Transpose(Mat4 m)` | Transpose. Defined in `math.c` but **not declared in druid.h**. |
| `mat4TransformVec4` | `Vec4 mat4TransformVec4(Mat4 m, Vec4 v)` | `m * v` (column-major indexing `m[col][row]`). |
| `mat4TransformPoint` | `Vec3 mat4TransformPoint(Mat4 m, Vec3 p)` | Transforms point with implicit w=1; perspective-divides if `|w| > 1e-4`. |
| `mat4TransformDirection` | `Vec3 mat4TransformDirection(Mat4 m, Vec3 d)` | Transforms direction by upper 3x3 with no translation — but indexes `m.m[row][col]`-style (`m[0][0]*x + m[0][1]*y + m[0][2]*z`), i.e. the transpose of the convention used by `mat4TransformVec4`/`Point`. |
| `mat4ToMat3` / `mat3ToMat4` | `Mat3(const Mat4)` / `Mat4(const Mat3)` | Copy upper-left 3x3 out / embed 3x3 into identity Mat4 (last row/col forced to [0,0,0,1]). Not `inline`, unlike neighbors. |
| `mat4ToPointers` / `pointersToMat4` | `void(Mat4, f32**)` / `Mat4(f32**)` | Copy between `Mat4` and legacy `f32**` format ("for compatibility with existing code"). **Not declared in druid.h.** `mat4ToPointers` writes through `dest` (side effect). |

Declared in druid.h but **not defined in any file of this module**: `mat4Add`, `mat4Sub`, `mat4ScaleMatrix` (lines 307–309).

Internal callers noted above; all other Mat4 functions have no call sites within the module — callers are external (renderer/camera are the likely consumers of `mat4Perspective`, `mat4LookAt`, `mat4Mul`, `getModel`).

### Quaternions (`math.c`) — represented as `Vec4` `{x,y,z,w}`, no dedicated `Quat` type

| Function | Signature | Purpose |
|---|---|---|
| `quatIdentity` | `Vec4 quatIdentity()` | Returns `{0,0,0,1}`. Fallback used by `quatNormalize`. |
| `quatFromAxisAngle` | `Vec4 quatFromAxisAngle(Vec3 axis, f32 angle)` | Axis-angle → quaternion; returns identity if axis length < 1e-6. |
| `quatMul` | `Vec4 quatMul(Vec4 q1, Vec4 q2)` | Hamilton product. Used by `quatRotateVec3`, `quatTransform`. |
| `quatNormalize` | `Vec4 quatNormalize(Vec4 q)` | Normalize; identity if length ≤ 1e-4. Used by `quatSlerp` (lerp branch). |
| `quatConjugate` | `Vec4 quatConjugate(const Vec4 q)` | `{-x,-y,-z,w}`. Used by `quatTransform`. |
| `quatRotateVec3` | `Vec3 quatRotateVec3(Vec4 q, Vec3 v)` | Rotate vector via `q * v * q^-1` with inline conjugate. |
| `quatTransform` | `Vec3 quatTransform(Vec4 q, Vec3 v)` | Functionally identical to `quatRotateVec3` (uses `quatConjugate`; assumes unit `q`). |
| `quatToRotationMatrix` | `Mat4 quatToRotationMatrix(Vec4 q)` | Quaternion → 4x4 rotation matrix. |
| `quatFromEuler` | `Vec4 quatFromEuler(const Vec3 euler)` | Euler (x=roll, y=pitch, z=yaw, radians) → quaternion. Header names the parameter `axis`. |
| `eulerFromQuat` | `Vec3 eulerFromQuat(Vec4 q)` | Quaternion → Euler angles, with ±90° pitch clamp at gimbal singularity. |
| `quatSlerp` | `Vec4 quatSlerp(Vec4 q0, Vec4 q1, f32 t)` | Spherical interpolation; clamps `t` to [0,1], negates `q1` for shortest path, falls back to normalized lerp when `dot > 0.9995`. |

### Scalar helpers (`math.c`)

| Function | Signature | Purpose |
|---|---|---|
| `clamp` | `f32 clamp(f32 value, f32 minVal, f32 maxVal)` | Clamp to range. |
| `lerp` | `f32 lerp(f32 a, f32 b, f32 t)` | Linear interpolation (unclamped). |
| `radians` / `degrees` | `f32 f(f32)` | Degree/radian conversion using `PI` macro. |

### Transform (`transform.c`)

| Function | Signature | Purpose |
|---|---|---|
| `getModel` | `Mat4 getModel(const Transform* transform)` | Builds the T·R·S model matrix directly from `pos`/`rot` (quaternion)/`scale` in one pass (comment: "~34 ops instead of ~230"), rather than composing three matrices. Returns `mat4Identity()` if `transform` is NULL. No side effects. No callers in this module; the ECS/renderer are the likely external callers. |

### SIMD batch kernels (`simd.c`)

All operate on caller-provided `f32` arrays; `out` may alias inputs (per the druid.h comment). Every function has an SSE2 path (unaligned loads/stores) and a scalar fallback; the five SoA-Vec3 functions additionally have AVX2 paths. Header comment in `simd.c`: "count is always a multiple of SIMD_WIDTH (4), no scalar remainder needed" — the SSE2 loops rely on this contract; the AVX2 paths do handle a scalar remainder (for the mod-8 tail). Side effect for all `void` ones: writes `count` floats to the `out` array(s).

Binary element-wise — `void f(const f32 *a, const f32 *b, f32 *out, u32 count)`:
- `simdAdd`, `simdSub`, `simdMul`, `simdDiv`, `simdMin`, `simdMax`
- `simdMadd(a, b, c, out, count)` — `out = a*b + c`; uses `_mm_fmadd_ps` when `__FMA__`.

Unary element-wise — `void f(const f32 *a, f32 *out, u32 count)`:
- `simdNeg`, `simdAbs` (sign-bit mask), `simdSqrt`
- `simdRsqrt`, `simdRcp` — hardware estimate + one Newton-Raphson refinement step (approximate, not IEEE-exact).

Scalar-broadcast — `simdAddScalar(a, s, out, count)`, `simdMulScalar(a, s, out, count)`, `simdClampScalar(a, lo, hi, out, count)`, `simdLerp(a, b, t, out, count)` (`out = a + t*(b-a)`, single `t` for whole array).

SoA Vec3 batches (separate x/y/z arrays; AVX2 + SSE2 + scalar):
- `simdDot3(ax,ay,az, bx,by,bz, out, count)` — per-element dot products.
- `simdCross3(ax..az, bx..bz, outX,outY,outZ, count)` — per-element cross products.
- `simdLengthSq3(ax,ay,az, out, count)` / `simdLength3(...)` — squared length / length.
- `simdNormalize3(inX,inY,inZ, outX,outY,outZ, count)` — normalize each vector via refined rsqrt; outputs zero vector when squared length ≤ 1e-8 (epsilon mask in SIMD paths).

Reductions — return a scalar:
- `f32 simdSum(const f32 *a, u32 count)` — horizontal sum.
- `f32 simdMinReduce(...)` / `f32 simdMaxReduce(...)` — min/max of array; return `0.0f` for `count == 0` (`simdSum` returns 0 naturally).

No call sites for any `simd*` function within this module; all callers are external (SoA layout suggests ECS/physics batch processing).

## Data Structures

All types defined in `include/druid.h`; all are plain value types (POD), passed and returned by value, living on the stack or embedded in other structs. Nothing in this module is ECS-managed or reference-counted.

| Type | Fields | Notes |
|---|---|---|
| `Vec2` / `Vec3` / `Vec4` | `f32 x, y[, z[, w]]` | General float vectors. `Vec4` doubles as the quaternion type (there is no `Quat` typedef despite quat-named functions). |
| `Vec2i` / `Vec3i` | `i32 x, y[, z]` | Integer variants; also used as matrix-dimension arguments (`Vec2i aSize`). |
| `Mat4` | `f32 m[4][4]` | Column-major (`m[col][row]`), per the "COLUMN-MAJOR IMPLEMENTATION FOR OPENGL" comment in math.c. |
| `Mat3` | `f32 m[3][3]` | Used only by `mat4ToMat3`/`mat3ToMat4` within this module. |
| `Transform` | `Vec3 pos; Vec4 rot; Vec3 scale` | Position, rotation quaternion, per-axis scale. Defined at druid.h line ~1232. Owned by whoever embeds it (ECS components, presumably — not visible from these files); this module only reads it in `getModel`. |
| `f32**` legacy matrix | row-pointer array of `f32` rows | Heap-allocated via `matCreate` (memory module, `MEM_TAG_ARRAY`), caller-owned, freed with `freeMat` using the same `Vec2i size`. |

Header constants (druid.h, static within each TU): `PI`, `PI_HALFRES` (3.14159265f), `PI_QUATERRES` (3.14f); `static const Vec3` direction constants `v3Zero`, `v3One`, `v3Up`, `v3Down`, `v3Right`, `v3Left`, `v3Forward` ({0,0,-1} — right-handed OpenGL forward), `v3Back`. None are referenced inside this module's .c files except `PI` (in `radians`/`degrees`).

`simd.c` internal macros: `DRUID_SSE2` (set for `__SSE2__`/`_M_X64`/`_M_AMD64`), `DRUID_FMA` (`__FMA__`), `DRUID_AVX2` (`__AVX2__`) — compile-time feature selection only, not exported.

## Internal (static) functions

None. No file in this module defines a `static` function; every function has external linkage (most are `inline` definitions with `DAPI` declarations in druid.h; four in math.c have external linkage but no header declaration — see Open questions).

## Open questions / unclear intent

1. `mat4Add`, `mat4Sub`, `mat4ScaleMatrix` are declared in druid.h (lines 307–309) but not defined in `math.c`, `simd.c`, or `transform.c`. Either defined elsewhere in the repo or missing; cannot tell from these files.
2. `mat4RotateZ`, `mat4Transpose`, `mat4ToPointers`, `pointersToMat4` are defined in `math.c` with external linkage but have no declaration in druid.h. Unclear whether they are intentionally module-private (by omission) or the declarations were dropped. The `mat4ToPointers` comment ("for compatibility with existing code") implies callers exist, but none are in these files.
3. `mat4Translate(Mat4 in, Vec3 t)` ignores its `in` parameter entirely — the returned matrix is identity-plus-translation. Whether the parameter is vestigial or the function was meant to translate `in` is unclear.
4. `quatRotateVec3` and `quatTransform` compute the same thing (`q * v * conj(q)`); why both exist, and which one callers are meant to use, is not determinable here.
5. `mat4TransformDirection` indexes the matrix transposed relative to `mat4TransformVec4`/`mat4TransformPoint` (`m[0][0]*x + m[0][1]*y + m[0][2]*z` vs. `m[0][0]*x + m[1][0]*y + m[2][0]*z`). Whether this is a deliberate transposed transform (e.g., for normals) or follows a different convention on purpose cannot be determined from this file alone.
6. `simd.c`'s contract comment references `SIMD_WIDTH` (4), but no `SIMD_WIDTH` macro exists in druid.h; and the AVX2 paths do handle non-multiple-of-8 remainders while SSE2 paths assume count % 4 == 0. The exact alignment/count contract callers must satisfy is only documented by that one comment.
7. `v3Tov3i` casts components through `(u32)` although `Vec3i` holds `i32` — intended behavior for negative inputs is unclear.
8. `PI_HALFRES` and `PI_QUATERRES` (reduced-precision PI constants) are unused in this module; their intended consumers are unknown.
9. `math.c` includes `<stdio.h>` but performs no I/O — possibly leftover.
10. `mat4Perspective` and `mat4Ortho` are each declared twice in druid.h (lines 290–291 and 320–321); presumably an editing artifact, but noted for the header cleanup pass.
