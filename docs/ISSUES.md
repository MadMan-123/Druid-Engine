# Druid Engine Issues

Detailed issue notes for the local kanban board. Each entry is written to be readable in VS Code and to point back to the relevant code and roadmap item.

## Issue 01 - File I/O cleanup

Fix the file helpers so they work correctly on Windows and non-Windows builds.

- Scope: binary-safe file open modes, non-Windows recursive listing, dead declarations in the file helper layer.
- Code: [src/core/file.c](../src/core/file.c#L180), [src/core/file.c](../src/core/file.c#L204), [src/core/platform.c](../src/core/platform.c#L70)
- Related roadmap: [ROADMAP](ROADMAP.md)
- Suggested fix: switch read/write paths to binary mode, finish the non-Windows recursion branch, then remove or wire up any declared-only helper that no one uses.

## Issue 02 - Build matrix cleanup

Make the build scripts less fragile before adding more platform/toolchain support.

- Scope: CMake flags, testbed link setup, MSVC import-lib generation.
- Code: [CMakeLists.txt](../CMakeLists.txt#L45), [testbed/CMakeLists.txt](../testbed/CMakeLists.txt#L23), [generate_msvc_lib.bat](../generate_msvc_lib.bat#L14)
- Related roadmap: [ROADMAP](ROADMAP.md)
- Suggested fix: pick one import-lib path, remove the dead build-script branches, and make the platform flags line up with what the repo actually builds.

## Issue 03 - Input reliability

Make the input layer behave consistently across the editor and runtime.

- Scope: mouse button mapping, controller bounds, dead input entry points.
- Code: [src/core/input.c](../src/core/input.c#L56), [src/core/input.c](../src/core/input.c#L163), [src/core/input.c](../src/core/input.c#L204), [src/core/input.c](../src/core/input.c#L289), [src/core/input.c](../src/core/input.c#L303)
- Related roadmap: [ROADMAP](ROADMAP.md)
- Suggested fix: clamp controller IDs, map mouse buttons to SDL’s numbering, and decide which input path is actually the engine-owned one.

## Issue 04 - Memory stats cleanup

Fix allocator accounting so the numbers in the editor mean something.

- Scope: arena reset stats, dfree underflow, hashmap validation, buffer lifecycle expectations.
- Code: [src/core/vmem.c](../src/core/vmem.c#L111), [src/core/vmem.c](../src/core/vmem.c#L207), [src/core/vmem.c](../src/core/vmem.c#L270), [src/core/arena.c](../src/core/arena.c#L3), [src/core/hashmap.c](../src/core/hashmap.c#L6), [src/core/buffer.c](../src/core/buffer.c#L5)
- Related roadmap: [ROADMAP](ROADMAP.md)
- Suggested fix: clamp the counters, make reset restore a coherent state, and validate map/buffer inputs before doing any work.

## Issue 05 - Scene/prefab load safety

Stop malformed data files from overrunning engine memory.

- Scope: scene loading, prefab loading, ECS system loader cleanup.
- Code: [src/systems/ecs/scene.c](../src/systems/ecs/scene.c#L176), [src/systems/ecs/scene.c](../src/systems/ecs/scene.c#L248), [src/systems/ecs/prefab.c](../src/systems/ecs/prefab.c#L108), [src/systems/ecs/ecssystem.c](../src/systems/ecs/ecssystem.c#L7)
- Related roadmap: [ROADMAP](ROADMAP.md)
- Suggested fix: clamp file-driven counts, check every read, and make layout ownership explicit when loading and unloading scenes.

## Issue 06 - Physics binding cleanup

Put the physics field binding in one place and stop the layout drift.

- Scope: shared binding definition, broadphase lookup, narrowphase consistency, collider constructors.
- Code: [src/systems/Physics/physics.c](../src/systems/Physics/physics.c#L1408), [src/systems/Physics/broadphase.c](../src/systems/Physics/broadphase.c#L46), [src/systems/Physics/narrowphase.c](../src/systems/Physics/narrowphase.c#L172), [src/systems/Physics/collider.c](../src/systems/Physics/collider.c#L300)
- Related roadmap: [ROADMAP](ROADMAP.md)
- Suggested fix: move the binding struct to shared code, then make broadphase and narrowphase read the same field layout.

## Issue 07 - Renderer submission fixes

Fix the instancing path so it does not drop state between command build and draw.

- Scope: indirect path material state, fence sync, visibility publication, buffer sync ownership.
- Code: [src/systems/Rendering/renderer.c](../src/systems/Rendering/renderer.c#L1169), [src/systems/Rendering/renderer.c](../src/systems/Rendering/renderer.c#L367), [src/systems/Rendering/renderer.c](../src/systems/Rendering/renderer.c#L388), [src/systems/Rendering/indirectbuffer.c](../src/systems/Rendering/indirectbuffer.c#L1), [src/systems/Rendering/instancebuffer.c](../src/systems/Rendering/instancebuffer.c#L1)
- Related roadmap: [ROADMAP](ROADMAP.md)
- Suggested fix: keep per-group state attached to the commands, fence the indirect path like the fallback path, and move the sync API into the buffer layer.

## Issue 08 - Editor restart safety

Make play-in-editor restart recoverable instead of fragile.

- Scope: snapshot/restore order, DLL lifetime, arena migration, picker guard rails.
- Code: [Editor/src/editor.cpp](../Editor/src/editor.cpp#L7608), [Editor/src/editor.cpp](../Editor/src/editor.cpp#L7702), [Editor/src/editor.cpp](../Editor/src/editor.cpp#L7819), [Editor/src/main.cpp](../Editor/src/main.cpp#L191), [Editor/src/main.cpp](../Editor/src/main.cpp#L245), [Editor/src/entitypicker.cpp](../Editor/src/entitypicker.cpp#L18)
- Related roadmap: [ROADMAP](ROADMAP.md)
- Suggested fix: preserve the snapshot until restore succeeds, keep DLL-owned layouts valid until teardown is complete, and make picker bounds explicit.
