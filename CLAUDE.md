# Druid Engine

A data-oriented C game engine with an ECS architecture, targeting millions of entities.

## Build

```bash
cmake -B build -G "MinGW Makefiles" && cmake --build build
```

- Engine builds as shared library (`druid.dll`) — C11, `-O3 -Wall -fPIC`
- Editor builds as executable (C++17) — links against druid + ImGui
- Output goes to `bin/`
- Dependencies: SDL3, GLEW, Assimp (prebuilt `.dll.a` in `deps/`)

## Project Structure

```
include/druid.h          — Monolithic public API (Raylib-style, doubles as documentation). DO NOT SPLIT.
src/core/                — Arena, Buffer, HashMap, File, Input, Logging, Math, Platform, DLLLoader, Transform
src/systems/rendering/   — Renderer, Camera, Shader, Mesh, Material, Texture, Model, Framebuffer, GBuffer, UBO, InstanceBuffer
src/systems/physics/     — Collider
src/systems/ecs/         — ECS: Archetype, EntityArena, ECSSystem (DLL plugins), Scene
src/application/         — Application lifecycle, ResourceManager
src/external/            — Third-party code (stb_image)
Editor/src/              — C++ editor with ImGui (main, editor, entitypicker, hub, project_builder)
testbed/                 — Example project
```

## Coding Style

### Naming
- **Types:** PascalCase — `Arena`, `Buffer`, `EntityManager`, `Transform`
- **Functions:** camelCase with type prefix — `arenaCreate()`, `bufferAcquire()`, `v3Add()`, `rendererBeginFrame()`
- **Enums:** UPPER_SNAKE_CASE — `LOG_FATAL`, `KEY_ESCAPE`, `MOUSE_LEFT`
- **Primitives:** short lowercase — `u8 u16 u32 u64 i8 i16 i32 i64 f32 f64 b8 b32 c8`
- **Variables:** camelCase — `activeCamera`, `elemSize`, `entityCount`
- **Macros:** UPPER_CASE — `DAPI`, `STATIC_ASSERT`, `FIELD_OF`, `FLAG_SET`

### Patterns
- `#pragma once` for header guards
- Include order: C stdlib → third-party (SDL3, GLEW, Assimp) → `druid.h`
- Source files include only `"../../include/druid.h"` (or appropriate relative path)
- Section separators: `//=====================...` (full-width equals)
- Allman braces for functions, K&R for control flow (`if`, `for`, `while`)
- 4-space indentation

### API Design
- `DAPI` macro for public exports (handles dllexport/dllimport)
- Create/Destroy: `createType(...)` returns pointer, `destroyType(Type *obj)` frees
- Init pattern: `typeCreate(Type *obj, ...)` returns `b8` success
- Handle-based resources: return `u32` index, not pointer
- Out-params: `b8 func(..., u64 *outEntity)` returns success, fills `*out`

### Memory
- Arena allocator: `arenaCreate()`, `aalloc()`, `arenaDestroy()`
- Buffer (slot pool): `bufferCreate()`, `bufferAcquire()`, `bufferGet()`, `bufferRelease()`
- Direct `malloc`/`free` where appropriate, NULL-check always

### Error Handling
- `b8` return for success/failure
- NULL return for pointer-returning failures
- Logging: `FATAL()`, `ERROR()`, `WARN()`, `INFO()`, `DEBUG()`, `TRACE()`
- Early return on NULL: `if (!ptr) return;`
- Assertions: `assert(ptr != NULL && "message");`

### Math
- Inline functions, pass structs by value, return new struct
- `inline Vec3 v3Add(Vec3 a, Vec3 b) { return (Vec3){...}; }`

### ECS
- SoA layout: `void **fields` — fields[0] = all positions, fields[1] = all velocities
- Archetype-based: `StructLayout` + `FieldInfo` metadata
- Macros: `FIELD_OF()`, `FIELD()`, `VEC3_FIELDS()`, `DEFINE_ARCHETYPE()`
- DLL plugin system: `ECSSystemPlugin` with init/update/render/destroy function pointers
- Entity handles: packed `u64` = `(archetype_id << 32) | index`

## Key Rules

- **druid.h stays monolithic** — it's the Raylib-style API reference. Add new declarations there.
- **Engine is pure C** (C11). Editor is C++ (C++17 for ImGui).
- **Data-oriented** — prefer SoA, contiguous memory, cache-friendly access patterns.
- **No over-engineering** — minimal abstraction, no unnecessary indirection.
- Keep functions small and focused. No OOP patterns in engine code.
