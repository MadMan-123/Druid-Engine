# rendering-primitives

Files: `src/systems/Rendering/shader.c`, `texture.c`, `camera.c`, `display.c`, `gizmo.c`.
Public declarations live in `include/druid.h` (no per-module headers; "exported" = declared there with `DAPI`).

## Synopsis

Low-level OpenGL building blocks for the Druid renderer: GLSL program
compilation/linking (`shader.c`), texture and cubemap loading via stb_image
(`texture.c`), a quaternion-based free camera (`camera.c`), the SDL3 window +
GL context singleton (`display.c`), and an immediate-mode debug line renderer
(`gizmo.c`). Nothing here is ECS-managed; shaders and textures are bare `u32`
GL handles owned by whoever created them (in practice the renderer/resource
manager), the `Display` is a module-static singleton, and the gizmo batch is a
module-static struct. The module depends on the filesystem (`loadFile` /
`freeFileData`), the tagged allocator (`dalloc`/`dfree`), the `HashMap`
container, the global `renderer` and its `modelSSBO`, and the profiler
(`profileCountTextureBind`); the higher-level renderer and game runtime depend
on it (per druid.h comments: `display` is "consumed by createRenderer",
`Renderer` holds `Display*` and camera indices).

---

## Public API Reference

### shader.c

Shaders/programs are plain `u32` GL object names — there is no `Shader` struct.

- **`void checkShaderError(u32 shader, u32 flag, b8 isProgram, const c8 *errorMessage)`**
  Queries compile/link/validate status (`glGetShaderiv`/`glGetProgramiv`) and
  logs the info log via `ERROR()` on failure. Params: GL object, status enum
  (e.g. `GL_COMPILE_STATUS`), whether the object is a program, message prefix.
  Returns nothing; does not abort — failed objects are still returned to
  callers. Called by `createShader`, `createComputeProgram`,
  `createGraphicsProgram`, `createGraphicsProgramWithGeometry`; exported for
  external use as well.

- **`u32 createShader(const c8 *text, u32 type)`**
  Compiles one shader stage from in-memory source. Params: NUL-terminated GLSL
  source, GL stage enum. Returns the shader object (may still be a valid name
  even if compilation failed — only logged). Called by `createComputeProgram`,
  `createGraphicsProgram`, `createGraphicsProgramWithGeometry`; also exported.

- **`u32 createProgram(u32 shader)`**
  Creates a program and attaches a single shader — does **not** link. No call
  sites in this module; caller is presumably external (or the function is
  vestigial — see Open questions).

- **`u32 createComputeProgram(const c8 *computePath)`**
  Loads a file, compiles it as `GL_COMPUTE_SHADER`, links + validates a
  program, detaches/deletes the stage shader. Returns program name, or 0 on
  file-load/compile failure. Side effects: GL object creation, `free(code)` on
  the file data pointer (see Open questions). No callers in this module —
  external (compute users).

- **`u32 createGraphicsProgram(const c8 *vertPath, const c8 *fragPath)`**
  Loads two files (`loadFile` → copy into `dalloc(MEM_TAG_SHADER)` buffer with
  NUL terminator → `freeFileData`), compiles vertex + fragment stages, binds
  attribute locations 0/1/2 to `position`/`texCoord`/`normal`, links,
  validates, deletes stage shaders, and — if the program exposes a
  `CoreShaderData` uniform block — binds it to UBO binding point 0. Returns
  program name or 0 on failure (cleans up GL objects on the failure paths).
  No callers in this module — external (renderer / resource manager).

- **`u32 createGraphicsProgramWithGeometry(const c8 *vertPath, const c8 *geomPath, const c8 *fragPath)`**
  Same as `createGraphicsProgram` plus a `GL_GEOMETRY_SHADER` stage; identical
  attribute bindings and `CoreShaderData` → binding-0 setup. Returns program
  name or 0. No callers in this module — external.

- **`void freeShader(u32 shader)`**
  `glDeleteProgram(shader)`. Despite the name it deletes a *program*, not a
  stage shader. External callers.

- **`void freeUBO(u32 ubo)`**
  Deletes a GL buffer object (no-op for 0). Exported in the druid.h shader
  section; no callers in this module — external. (The corresponding UBO
  creation, `updateFrameUBOData`, is declared in druid.h but defined outside
  this module.)

- **`void updateShaderMVP(u32 shaderProgram, const Transform transform, const Camera camera)`**
  Legacy-shaped entry point: view/projection are now supplied via UBO, so this
  just forwards to `updateShaderModel` (the `camera` param is unused). A TODO
  comment says the plan is model-only SSBO + camera UBO. Callers external
  (per-draw path in the renderer).

- **`void updateShaderModel(u32 shaderProgram, const Transform transform)`**
  Uploads the model transform for the current draw. Fast path: if the global
  `renderer` and `renderer->modelSSBO` exist and the program has a
  `u_modelIndex` uniform, writes the transform into the shared model SSBO via
  `modelSSBOWrite` (SSBO assumed bound to slot 2 by `rendererBeginFrame`) and
  sets `u_modelIndex` to the returned slot. Fallback (no uniform, no SSBO, or
  SSBO full): computes `getModel(&transform)` and uploads a plain `model`
  mat4 uniform. Called by `updateShaderMVP`; also exported.

- **`void updateShaderTime(u32 shaderProgram, f32 time)`**
  Sets a `time` float uniform if the program has one; no-op for program 0.
  Defined in shader.c; **not found among the grepped druid.h declarations**
  (see Open questions). No callers in this module.

### texture.c

Textures are plain `u32` GL handles — no `Texture` struct in druid.h.

- **`u32 initTexture(const c8 *fileName)`**
  Loads an image with `stbi_load` (forced RGBA), creates a `GL_TEXTURE_2D`
  with repeat wrap, linear min/mag filtering, uploads via `glTexImage2D`, and
  generates mipmaps. Returns the texture handle, or 0 on NULL name / load
  failure. Side effects: lazily creates the module-static `s_failedTextures`
  HashMap and records failed paths so each bad path is only warned about once
  (avoids per-frame log spam from materials). No callers in this module —
  external (resource manager / materials).

- **`u32 createCubeMapTexture(const c8 **faces, u32 count)`**
  Loads `count` face images into a `GL_TEXTURE_CUBE_MAP`
  (`POSITIVE_X + i` targets, RGB or RGBA by channel count), sets
  trilinear-min/linear-mag filtering, clamp-to-edge on S/T/R, generates
  mipmaps, unbinds. On any face failure: logs, deletes the texture, returns 0.
  External callers (skybox).

- **`void freeTexture(u32 texture)`** — `glDeleteTextures` on the handle.
  External callers.

- **`void textureSystemReset(void)`**
  Destroys the `s_failedTextures` map and clears the initialized flag, so
  failure logging starts fresh (e.g. on scene reload). External callers.

- **`void bindTexture(u32 texture, u32 unit, GLenum type)`**
  `glActiveTexture(GL_TEXTURE0 + unit)` + `glBindTexture(type, texture)`;
  asserts `unit <= 31`; increments the profiler's texture-bind counter via
  `profileCountTextureBind()`. External callers (material/renderer bind path).

### camera.c

All functions operate on a caller-owned `Camera` (no allocation here).

- **`Mat4 getViewProjection(const Camera *camera)`**
  Builds a look-at view from `pos`/`orientation` (quaternion applied to
  `v3Forward`/`v3Up`) and returns `projection * view`. External callers
  (renderer frame setup, gizmoEndFrame's argument).
- **`Mat4 getView(const Camera *camera, b8 removeTranslation)`**
  Returns just the view matrix; if `removeTranslation`, strips translation via
  Mat4→Mat3→Mat4 round-trip (skybox-style use). External callers.
- **`void initCamera(Camera *camera, const Vec3 pos, f32 fov, f32 aspect, f32 nearClip, f32 farClip)`**
  Fills all fields; identity orientation; computes `projection` with
  `mat4Perspective(fov, ...)` — note: `fov` is passed through *without*
  `radians()` here, unlike the setters below (see Open questions). External
  callers (renderer camera pool).
- **`void moveForward(Camera *camera, f32 amt)` / `void moveRight(Camera *camera, f32 amt)`**
  Translate `pos` along the orientation's forward/right vector. External
  callers (game/editor camera controllers).
- **`void pitch(Camera *camera, f32 angle)`**
  Post-multiplies orientation by a quaternion about the camera's local right
  axis; `angle` in degrees (converted with `radians()`); normalizes.
- **`void rotateY(Camera *camera, f32 angle)`**
  Post-multiplies by a yaw quaternion about world `v3Up`; degrees; normalizes.
- **`void cameraSetPerspective(Camera *cam, f32 fovDeg, f32 aspect, f32 nearClip, f32 farClip)`**
  Updates the four cached params and rebuilds `projection` with
  `radians(fovDeg)`. NULL-safe.
- **`void cameraSetFov(Camera *cam, f32 fovDeg)`**
  Rebuilds projection from the new fov + cached aspect/clip planes. NULL-safe.
- **`void cameraSetOrthographic(Camera *cam, f32 left, f32 right, f32 bottom, f32 top, f32 nearClip, f32 farClip)`**
  Replaces `projection` with `mat4Ortho(...)`; does not update
  `fovDeg`/`aspect`/clip fields. NULL-safe.

### display.c

- **`void initDisplay(const c8 *title, f32 width, f32 height)`**
  Points the global `display` at the module-static `s_display`, inits SDL
  (events + gamepad; SDL3-style `SDL_CreateWindow(title, w, h, flags)`), sets
  GL attributes (8-bit RGB, 16-bit depth, double buffer), creates a resizable
  GL window + context, runs `glewInit`, enables depth test and face culling,
  sets a cyan clear color. druid.h says the singleton is "consumed by
  createRenderer". Must precede any GL work (druid.h: gizmo init "must be
  called AFTER the OpenGL context is alive").
- **`void swapBuffer(const Display *display)`** — `SDL_GL_SwapWindow`. Called
  once per frame by the render loop (external).
- **`void clearDisplay(f32 r, f32 g, f32 b, f32 a)`** — `glClearColor` +
  clear of color and depth buffers. External callers.
- **`void returnError(const c8 *errorString)`**
  Fatal-ish display error handler: prints (note malformed `"/n"` literal in
  the format string), blocks on `getchar()`, calls `SDL_Quit()` — but then
  *returns*; `initDisplay` continues after calling it. Called by `initDisplay`
  (3 sites); also exported.
- **`void onDestroy(Display *d)`**
  Destroys the GL context and window. No free of the struct — it is the static
  `s_display`. External caller (engine shutdown).
- **`void setVSync(i32 interval)`**
  `SDL_GL_SetSwapInterval`; per druid.h: 0 = off, 1 = on, -1 = adaptive. On
  failure warns, and if adaptive was requested falls back to plain vsync (1).
  External callers.

### gizmo.c

Immediate-mode debug line renderer. Uniquely in this module, gizmo.c includes
`<GL/glew.h>` directly (the other files get GL through druid.h) and carries
its own **inline GLSL** as C string literals (`gizmoVertSrc` /
`gizmoFragSrc`, `#version 330 core`): pos+color pass-through with a single
`uViewProj` mat4 — it does not use the engine's file-based
`createGraphicsProgram` path, `CoreShaderData` UBO, or model SSBO. All
`gizmoDraw*` calls just append line vertices to a CPU batch; nothing renders
until `gizmoEndFrame`.

Lifecycle:
- **`void gizmoInit(void)`** — idempotent; allocates the 65,536-vertex CPU
  array (`dalloc`, `MEM_TAG_RENDERER`), creates VAO/VBO
  (`GL_DYNAMIC_DRAW`, attribs: loc 0 = vec3 pos, loc 1 = vec4 color), compiles
  the inline shader via `compileGizmoShader`. External caller (renderer/engine
  startup, after GL context exists).
- **`void gizmoShutdown(void)`** — deletes VAO/VBO/program, `dfree`s the
  vertex array, clears `initialized`.
- **`void gizmoBeginFrame(void)`** — resets `gizmo.count` to 0.
- **`void gizmoEndFrame(Mat4 viewProj)`** — uploads the batch with
  `glBufferSubData`, sets `uViewProj`, draws `GL_LINES` with depth test
  temporarily disabled (gizmos draw on top), re-enables depth test, resets the
  count. External caller (end of frame in the renderer).

Primitives (all append to the batch; silently drop vertices past
`GIZMO_MAX_VERTICES`):
- **`gizmoDrawLine(Vec3 from, Vec3 to, GizmoColor color)`** — one segment.
  Called externally.
- **`gizmoDrawRay(Vec3 origin, Vec3 direction, f32 length, GizmoColor color)`**
  — line from origin along `direction * length` (direction not normalized
  here).
- **`gizmoDrawArrow(Vec3 from, Vec3 to, f32 headSize, GizmoColor color)`** —
  shaft + 4-line head built from a basis around the direction. Called by
  `gizmoDrawAxes`, `gizmoDrawTransform`, `gizmoDrawContactManifold`.
- **`gizmoDrawSphere(Vec3 center, f32 radius, GizmoColor color)`** — three
  axis-aligned great circles, 24 segments each. Called by `gizmoDrawCollider`,
  `gizmoDrawContactManifold`.
- **`gizmoDrawBox(Vec3 center, Vec3 half, GizmoColor color)`** — 12-edge
  wireframe box. Called by `gizmoDrawAABB`, `gizmoDrawCollider`.
- **`gizmoDrawCylinder(Vec3 center, f32 radius, f32 halfHeight, GizmoColor color)`**
  — top/bottom circles + 4 vertical lines (Y-axis aligned). Called by
  `gizmoDrawCollider`.
- **`gizmoDrawAABB(AABB box, GizmoColor color)`** — converts min/max to
  center/half and forwards to `gizmoDrawBox`. Called by `gizmoDrawCollider`
  (mesh colliders).
- **`gizmoDrawCircle(Vec3 center, Vec3 normal, f32 radius, GizmoColor color)`**
  — circle in the plane of `normal`.
- **`gizmoDrawGrid(Vec3 center, f32 size, u32 divisions, GizmoColor color)`**
  — XZ-plane grid.
- **`gizmoDrawAxes(Vec3 origin, f32 length)`** — world-axis arrows in
  `GIZMO_RED`/`GREEN`/`BLUE`.
- **`gizmoDrawTransform(Transform *t, f32 length)`** — arrows along the
  transform's rotated right/up/forward (`quatRotateVec3`; forward is -Z).
  NULL-safe.
- **`gizmoDrawCollider(Collider *col, Vec3 pos, GizmoColor color)`** —
  physics debug: switches on `col->type` (`COLLIDER_SPHERE`/`BOX`/`CYLINDER`/
  `MESH`) using `getRadius`/`getHalfExtents`/`colliderComputeAABB`; for
  cylinders it *reinterprets* `col->state` through a locally re-declared
  `CylState { f32 radius; f32 halfHeight; }` struct (see Open questions).
  NULL/state-guarded.
- **`gizmoDrawContactManifold(const ContactManifold *manifold, GizmoColor color)`**
  — small sphere + normal arrow for each contact point. NULL-safe.

---

## Data Structures

### `Camera` (druid.h ~1242)
Fields: `Mat4 projection`; `Vec3 pos`; `Vec4 orientation` (quaternion);
cached perspective params `f32 fovDeg, aspect, nearClip, farClip`.
Plain value struct — caller-owned; no alloc/free in this module. The renderer
owns a pool (druid.h: `rendererAcquireCamera`/`rendererReleaseCamera`,
`maxCameras` heap-allocated by `createRenderer`); `GameRuntime` exposes an
active `Camera *camera`. Not ECS-managed here (though an ECS `sceneCameraFlags`
array exists elsewhere in druid.h).

### `Display` (druid.h ~1323) and globals `display` / `s_display` (display.c)
Fields: `SDL_GLContext glContext`, `SDL_Window *sdlWindow`,
`f32 screenWidth, screenHeight`. Storage is the file-static
`static Display s_display` in display.c; the exported global
`Display *display` points at it after `initDisplay`. druid.h's comment says
"initDisplay allocates" and "owned by Renderer", but the implementation is
static storage — never heap-freed; `onDestroy` only tears down SDL/GL
resources. Not ECS-managed.

### Shader / Texture "types"
No structs — both are raw `u32` OpenGL object names. Ownership sits with the
caller (resource manager / renderer); freed with `freeShader` /
`freeTexture` / `freeUBO`. Temporary shader-source buffers use
`dalloc`/`dfree` with `MEM_TAG_SHADER`.

### `GizmoColor` (druid.h ~2683) + color macros
`{ f32 r, g, b, a; }` value struct passed by value. Convenience macros:
`GIZMO_RED`, `GIZMO_GREEN`, `GIZMO_BLUE`, `GIZMO_YELLOW`, `GIZMO_CYAN`,
`GIZMO_WHITE` (compound literals).

### `GizmoVertex` + static `gizmo` batch (gizmo.c)
`GizmoVertex { Vec3 pos; f32 r,g,b,a; }` — interleaved VBO layout.
The anonymous file-static `gizmo` struct holds `u32 vao, vbo, shader`,
`GizmoVertex *vertices` (heap, `dalloc` `MEM_TAG_RENDERER`,
`GIZMO_MAX_VERTICES` = 65536 capacity), `u32 count`, `b8 initialized`.
Allocated by `gizmoInit`, freed by `gizmoShutdown`. Singleton, not
ECS-managed. Constants: `GIZMO_MAX_VERTICES`, `GIZMO_CIRCLE_SEGMENTS` (24).

### `s_failedTextures` / `s_failedTexturesInitialized` (texture.c)
File-static `HashMap` (capacity 256, key size 512 chars, value size `u8`)
using `djb2Hash`/`equals` declared `extern` from resourcemanager.c. Caches
texture paths that failed to load so warnings are logged once per path.
Lazily created inside `initTexture`; destroyed by `textureSystemReset`.

---

## Internal (static) functions

- `compileGizmoShader` (gizmo.c) — compiles/links the inline pos+color line
  shader; no error checking (does not use `checkShaderError`).
- `pushVertex` (gizmo.c) — append one vertex to the batch; silently drops
  when full.
- `pushLine` (gizmo.c) — two `pushVertex` calls.
- (texture.c and camera.c and display.c have no static functions; display.c's
  `s_display` is static data, not a function.)

---

## Open questions / unclear intent

1. **`checkShaderError` format string** — `ERROR("s\n Error: %s \n", errorMessage, error)`
   has one `%s` specifier but two arguments, and starts with a bare `"s"`.
   Intended output format is unclear from this file (likely meant `"%s\n Error: %s\n"`).
2. **`createProgram(u32 shader)`** — attaches a single shader and returns
   without linking; no call sites in this module. Whether it's a live external
   API or dead code can't be determined here.
3. **`createComputeProgram` memory handling** — it calls `free(code)` where
   `code` aliases `fileData->data`, and never calls `freeFileData(fileData)`,
   unlike the graphics-program loaders (which copy into `dalloc` buffers and
   call `freeFileData`). Whether `FileData`/its `data` may legally be
   `free()`d piecemeal depends on the filesystem module, not readable here.
4. **`initCamera` fov units** — passes `fov` straight to `mat4Perspective`,
   while `cameraSetPerspective`/`cameraSetFov` wrap with `radians(fovDeg)`.
   Whether `initCamera` expects radians (and the field name `fovDeg` is then
   misleading for that path) can't be resolved from these files.
5. **`updateShaderTime`** — defined and commented in shader.c but no
   declaration for it turned up in the druid.h sections searched; its exported
   status/callers are unknown from this file set.
6. **`gizmoDrawCollider`'s local `CylState`** — re-declares
   `{ f32 radius; f32 halfHeight; }` and casts `col->state` to it; correctness
   depends on the physics module's actual cylinder state layout, which isn't
   in this file set.
7. **`updateFrameUBOData(const Camera*, f32)`** — declared in druid.h's
   shader section (line 1539) but not defined in any file of this module; the
   camera-UBO half of the shader-data pipeline lives elsewhere (presumably the
   renderer).
8. **Display ownership comments vs. implementation** — druid.h says the
   Display is "owned by Renderer" and that `initDisplay` "allocates" the
   singleton, but display.c uses static storage and comments "no free". Which
   description reflects intent is unclear.
9. **`returnError` control flow** — it prints, waits for a key, calls
   `SDL_Quit()`, then returns, after which `initDisplay` continues executing
   (e.g. proceeds to `glewInit` after a failed window). Whether it was meant
   to terminate the program is not determinable from this file.
