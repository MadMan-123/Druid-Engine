# Druid Engine Bug-Fix Timeline

This is ordered by risk and dependency: fix the bugs that can corrupt state, break builds, or hide the real problem first; then stabilize renderer/physics/editor flows; then clean up dead code and ergonomics.

## Sprint 0: Unblock builds and diagnostics

1. Fix the platform/build breakages that can stop the tree from compiling or reporting errors clearly.
   - Ref: [core-platform critique](critique/core-platform.md) - [file.c branch and text-loading failures](critique/core-platform.md) plus [build-system portability notes](critique/build-system-testbed.md).
   - Code: [src/core/file.c](../src/core/file.c#L180), [src/core/file.c](../src/core/file.c#L204), [src/core/platform.c](../src/core/platform.c#L70).
   - Fix: complete the non-Windows recursive listing path, make file I/O binary-safe, and remove dead declarations before widening the build matrix.
   - Ref: [rendering-buffers critique](critique/rendering-buffers.md) - [UBO module boundary confusion](critique/rendering-buffers.md).
   - Code: [src/systems/Rendering/ubo.c](../src/systems/Rendering/ubo.c#L18), [src/systems/Rendering/ubo.c](../src/systems/Rendering/ubo.c#L54).
   - Fix: move the UBO API onto one module boundary and delete the orphaned declaration.
   - Ref: [build-system-testbed critique](critique/build-system-testbed.md).
   - Code: [CMakeLists.txt](../CMakeLists.txt#L45), [testbed/CMakeLists.txt](../testbed/CMakeLists.txt#L23), [generate_msvc_lib.bat](../generate_msvc_lib.bat#L14).
   - Fix: pick one MSVC import-lib path, then make the CMake/testbed link rules match it.

2. Make the editor/runtime entry points fail loudly instead of drifting into undefined state.
   - Ref: [application-runtime critique](critique/application-runtime.md) and [core-input critique](critique/core-input.md).
   - Code: [src/application/application.c](../src/application/application.c#L16), [src/application/application.c](../src/application/application.c#L56), [src/application/application.c](../src/application/application.c#L117), [src/systems/runtime.c](../src/systems/runtime.c#L156), [src/core/input.c](../src/core/input.c#L56), [src/core/input.c](../src/core/input.c#L163), [src/core/input.c](../src/core/input.c#L204), [src/core/input.c](../src/core/input.c#L289), [src/core/input.c](../src/core/input.c#L303).
   - Fix: give the app loop explicit event-pump ownership, make resource lookup return failures instead of defaulting silently, and clamp input indices / remap mouse buttons.

## Sprint 1: Stop data corruption and lifetime bugs

1. Fix ECS storage invariants first.
   - Ref: [ecs-storage critique](critique/ecs-storage.md) - [buffered growth invariant failures](critique/ecs-storage.md), [growth failure corruption](critique/ecs-storage.md), and [legacy arena math bugs](critique/ecs-storage.md).
   - Code: [src/systems/ecs/archetype.c](../src/systems/ecs/archetype.c#L114), [src/systems/ecs/archetype.c](../src/systems/ecs/archetype.c#L345), [src/systems/ecs/archetype.c](../src/systems/ecs/archetype.c#L569), [src/systems/ecs/entityarena.c](../src/systems/ecs/entityarena.c#L183).
   - Fix: remove or finish the buffered slow path, repair growth rollback, validate zero-sized layouts, and make entity removal bounds check live count.
   - Why now: every higher-level system depends on archetype correctness.

2. Fix scene/prefab loading so corrupted files cannot poison the engine.
   - Ref: [ecs-scene-prefab-systems critique](critique/ecs-scene-prefab-systems.md) - [scene overflow / unchecked read issues](critique/ecs-scene-prefab-systems.md), [prefab overflow](critique/ecs-scene-prefab-systems.md), and [unreachable loader path](critique/ecs-scene-prefab-systems.md).
   - Code: [src/systems/ecs/scene.c](../src/systems/ecs/scene.c#L176), [src/systems/ecs/scene.c](../src/systems/ecs/scene.c#L248), [src/systems/ecs/scene.c](../src/systems/ecs/scene.c#L400), [src/systems/ecs/prefab.c](../src/systems/ecs/prefab.c#L108), [src/systems/ecs/ecssystem.c](../src/systems/ecs/ecssystem.c#L7), [src/systems/ecs/ecssystem.c](../src/systems/ecs/ecssystem.c#L49).
   - Fix: clamp and validate file-driven counts, check every read, and make scene/layout ownership explicit at load and shutdown.

3. Tighten core memory accounting before relying on it for debugging.
   - Ref: [core-memory critique](critique/core-memory.md) - [dfree/stat bugs](critique/core-memory.md), [reset desync](critique/core-memory.md), and [hashmap validation gaps](critique/core-memory.md).
   - Code: [src/core/vmem.c](../src/core/vmem.c#L111), [src/core/vmem.c](../src/core/vmem.c#L207), [src/core/vmem.c](../src/core/vmem.c#L270), [src/core/arena.c](../src/core/arena.c#L3), [src/core/hashmap.c](../src/core/hashmap.c#L6), [src/core/hashmap.c](../src/core/hashmap.c#L50), [src/core/hashmap.c](../src/core/hashmap.c#L60), [src/core/buffer.c](../src/core/buffer.c#L5), [src/core/buffer.c](../src/core/buffer.c#L61), [src/core/buffer.c](../src/core/buffer.c#L87).
   - Fix: clamp counters, make reset restore stats coherently, validate map/buffer inputs up front, and stop implying destroy actually reclaims memory.
   - Why now: the profiler and editor rely on these stats being trustworthy.

## Sprint 2: Stabilize runtime rendering and physics

1. Repair the renderer submission paths.
   - Ref: [renderer critique](critique/renderer.md) - [double-added SSBO base](critique/renderer.md), [material last-wins](critique/renderer.md), [missing fence](critique/renderer.md), and [partial visibility publication](critique/renderer.md).
   - Code: [src/systems/Rendering/renderer.c](../src/systems/Rendering/renderer.c#L1169), [src/systems/Rendering/renderer.c](../src/systems/Rendering/renderer.c#L126), [src/systems/Rendering/renderer.c](../src/systems/Rendering/renderer.c#L367), [src/systems/Rendering/renderer.c](../src/systems/Rendering/renderer.c#L388), [src/systems/Rendering/indirectbuffer.c](../src/systems/Rendering/indirectbuffer.c#L1), [src/systems/Rendering/instancebuffer.c](../src/systems/Rendering/instancebuffer.c#L1).
   - Fix: carry per-group material state through command building, fence the indirect path the same way as the fallback, and keep visibility buffers all-or-nothing.
   - Ref: [rendering-buffers critique](critique/rendering-buffers.md) - [instance buffer fence ownership split](critique/rendering-buffers.md).
   - Fix: move the sync API into the buffer layer so the renderer stops poking raw fence slots.

2. Clean up buffer and UBO APIs so they fail as units, not as ad hoc call sites.
   - Ref: [rendering-buffers critique](critique/rendering-buffers.md) - [FBO/GBuffer create-by-value failure](critique/rendering-buffers.md), [dead modelSSBO upload path](critique/rendering-buffers.md), [stale UBO state](critique/rendering-buffers.md), and [garbage upload hazards](critique/rendering-buffers.md).
   - Code: [src/systems/Rendering/framebuffer.c](../src/systems/Rendering/framebuffer.c#L1), [src/systems/Rendering/gbuffer.c](../src/systems/Rendering/gbuffer.c#L1), [src/systems/Rendering/geometrybuffer.c](../src/systems/Rendering/geometrybuffer.c#L1), [src/systems/Rendering/modelssbo.c](../src/systems/Rendering/modelssbo.c#L1), [src/systems/Rendering/ssbo.c](../src/systems/Rendering/ssbo.c#L1), [src/systems/Rendering/ubo.c](../src/systems/Rendering/ubo.c#L18), [src/systems/Rendering/ubo.c](../src/systems/Rendering/ubo.c#L54).
   - Fix: return explicit success/failure from resource creators, delete dead upload paths, and make each GL wrapper own its own teardown.

3. Fix physics binding drift before touching any broadphase/narrowphase logic.
   - Ref: [physics-world critique](critique/physics-world.md) and [physics-shapes critique](critique/physics-shapes.md).
   - Code: [src/systems/Physics/physics.c](../src/systems/Physics/physics.c#L1408), [src/systems/Physics/physics.c](../src/systems/Physics/physics.c#L1614), [src/systems/Physics/broadphase.c](../src/systems/Physics/broadphase.c#L46), [src/systems/Physics/collider.c](../src/systems/Physics/collider.c#L300), [src/systems/Physics/contact.c](../src/systems/Physics/contact.c#L1), [src/systems/Physics/narrowphase.c](../src/systems/Physics/narrowphase.c#L172).
   - Fix: put the binding struct in one shared header, then repair the field lookups and collider constructors that currently depend on the wrong layout.
   - Why now: the renderer and editor both assume physics metadata is stable.

## Sprint 3: Fix editor play/restart behavior

1. Make play-in-editor restart safe.
   - Ref: [editor-ui-1-renderpath critique](critique/editor-ui-1-renderpath.md) and [editor-core critique](critique/editor-core.md).
   - Code: [Editor/src/editor.cpp](../Editor/src/editor.cpp#L7608), [Editor/src/editor.cpp](../Editor/src/editor.cpp#L7629), [Editor/src/editor.cpp](../Editor/src/editor.cpp#L7702), [Editor/src/editor.cpp](../Editor/src/editor.cpp#L7819), [Editor/src/main.cpp](../Editor/src/main.cpp#L191), [Editor/src/main.cpp](../Editor/src/main.cpp#L245), [Editor/src/entitypicker.cpp](../Editor/src/entitypicker.cpp#L18).
   - Fix: keep DLL-owned layouts alive until teardown order is safe, preserve the snapshot until restore succeeds, and make failed restart paths recoverable.
   - Ref: [editor-core critique](critique/editor-core.md).
   - Code: [Editor/src/entitypicker.cpp](../Editor/src/entitypicker.cpp#L18), [Editor/src/main.cpp](../Editor/src/main.cpp#L191), [Editor/src/main.cpp](../Editor/src/main.cpp#L245).
   - Fix: make picker errors explicit and guard ID lookups / capacity limits.

2. Remove the biggest editor/runtime render divergence.
   - Ref: [editor-ui-1-renderpath critique](critique/editor-ui-1-renderpath.md) - [immediate-mode render divergence](critique/editor-ui-1-renderpath.md).
   - Code: [Editor/src/editor.cpp](../Editor/src/editor.cpp#L1814), [Editor/src/editor.cpp](../Editor/src/editor.cpp#L1779), [Editor/src/editor.cpp](../Editor/src/editor.cpp#L2339).
   - Fix: consolidate the editor and runtime scene paths so the editor actually exercises the same submission path.
   - Ref: [editor-core critique](critique/editor-core.md) - [scene arena migration/cache bug](critique/editor-core.md).
   - Code: [Editor/src/main.cpp](../Editor/src/main.cpp#L191), [Editor/src/main.cpp](../Editor/src/main.cpp#L245).
   - Fix: make capacity edits update the real arena, not just the cached number.

## Sprint 4: Remove dead code and tighten ergonomics

1. Delete or localize dead code that obscures the real paths.
   - Ref: [audio critique](critique/audio.md), [mesh-model-material critique](critique/mesh-model-material.md), and [ecs-scene-prefab-systems critique](critique/ecs-scene-prefab-systems.md).
   - Fix: fix the live audio state bugs, delete the unused mesh/material helpers, and collapse duplicate scene/codegen blocks.

2. Clean up API surface area that forces callers to know internals.
   - Ref: [core-input critique](critique/core-input.md), [application-runtime critique](critique/application-runtime.md), and the broader module critiques above.
   - Fix: move hidden globals behind accessor functions and stop exporting private layouts that only work if callers already know the implementation.
   - This is best done after the runtime is stable, otherwise the cleanup work will be churned by bug fixes.

## Verification pass

Run these checks after the corresponding fixes land:

- Does the tree build cleanly in the active toolchain, including the MSVC/import-lib path and any non-Windows guards?
- Does `core-input` reject invalid controller IDs and use the correct mouse-button mapping?
- Does archetype growth stay valid after a forced allocation failure and after repeated spawn/despawn cycles?
- Can a malformed `.drsc` or `.prefab` file no longer overrun scene/prefab storage?
- Does the renderer indirect path preserve material selection, fencing, and visibility buffers across two frames in a row?
- Do physics broadphase and narrowphase agree on field bindings after a single shared binding definition is introduced?
- Can the editor restart play mode without losing the original scene or unloading DLL-owned layouts too early?
