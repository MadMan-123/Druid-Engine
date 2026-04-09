# Druid Engine

A data-oriented game engine written in C11 with an archetype ECS, targeting millions of entities.

The engine is built around SoA (Structure of Arrays) memory layouts, arena allocation, and SIMD-accelerated batch processing. Everything goes through a single public header (`include/druid.h`) in the style of raylib.

The editor is a separate C++17 app using ImGui.

## Design

- **ECS with archetypes** - entities are stored in chunked SoA arrays grouped by field layout. Hot and cold fields are split into separate contiguous memory blocks so physics iteration doesn't pull in render-only data.
- **Arena memory** - one large OS allocation at startup, carved into bump arenas (General, ECS, Renderer, Physics, Frame). No per-frame malloc.
- **Single header API** - `include/druid.h` is the entire public interface. If you want to know what the engine can do, read that file.
- **DLL plugin system** - game code and ECS systems compile as shared libraries, hot-reloadable by the editor.
- **SSBO instanced rendering** - entities are batched by model ID into a GPU buffer and drawn with ~44 draw calls for 1M entities.

## Building

Requires MSYS2 with GCC, CMake, and Ninja. Dependencies (SDL3, GLEW, Assimp) are prebuilt in `deps/`.

```
cmake -B build -G Ninja
cmake --build build
```

Engine outputs `bin/libdruid.dll`. Editor outputs `Editor/bin/editor.exe`.

## Project structure

```
include/druid.h         - public API (monolithic, don't split it)
src/core/               - arena, buffer, hashmap, input, logging, math, SIMD, profiler
src/systems/rendering/  - renderer, camera, shader, mesh, material, SSBO, GBuffer
src/systems/physics/    - rigidbody, spatial hash broadphase, collision
src/systems/ecs/        - archetype, entity arena, DLL plugin system, scene
src/application/        - application lifecycle, resource manager
Editor/src/             - ImGui editor (C++17)
testbed/                - example project
```

## Usage

Game code links against `libdruid.dll` and includes `druid.h`. A minimal app:

```c
#include <druid.h>

void init(void)    { /* setup */ }
void update(f32 dt){ /* logic */ }
void render(f32 dt){ /* draw  */ }
void destroy(void) { /* cleanup */ }

int main(void)
{
    Application *app = createApplication("My Game", init, update, render, destroy);
    run(app);
    return 0;
}
```

Entities live in archetypes. Define a field layout, create the archetype, spawn entities:

```c
FieldInfo fields[] = {
    {"Alive",      sizeof(b8),  FIELD_TEMP_COLD},
    {"PositionX",  sizeof(f32), FIELD_TEMP_HOT},
    {"PositionY",  sizeof(f32), FIELD_TEMP_HOT},
    {"PositionZ",  sizeof(f32), FIELD_TEMP_HOT},
    {"ModelID",    sizeof(u32), FIELD_TEMP_COLD},
};
StructLayout layout = { "Enemy", fields, 5 };

Archetype arch = {0};
arch.flags = ARCH_BUFFERED;
createArchetype(&layout, 10000, &arch);

u32 id = archetypePoolSpawn(&arch);
```

Fields are accessed as flat arrays through `getArchetypeFields()`:

```c
void **f = getArchetypeFields(&arch, chunkIndex);
f32 *posX = (f32 *)f[1];
posX[entityIndex] = 50.0f;
```

## License

MIT License - Copyright (c) 2025 Madoc Wolstencroft

Dependencies (SDL3, GLEW, Assimp, ImGui) are under their own licenses.
