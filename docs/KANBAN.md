# Druid Engine Kanban

Lightweight board for the current bug-fix push. This is the fast version: no GitHub setup, no issue creation, just a simple view of what needs doing.

## Backlog

- [ ] File I/O cleanup - binary-safe read/write modes, non-Windows recursive listing, remove dead file declarations.
  - Issue: [Issue 01](ISSUES.md#issue-01---file-io-cleanup)
  - Ref: [ROADMAP](ROADMAP.md)
  - Code: [src/core/file.c](../src/core/file.c)

- [ ] Build matrix cleanup - make the build scripts and import-lib path less fragile before widening platform support.
  - Issue: [Issue 02](ISSUES.md#issue-02---build-matrix-cleanup)
  - Ref: [ROADMAP](ROADMAP.md)
  - Code: [CMakeLists.txt](../CMakeLists.txt), [testbed/CMakeLists.txt](../testbed/CMakeLists.txt), [generate_msvc_lib.bat](../generate_msvc_lib.bat)

- [ ] Input reliability - fix mouse-button mapping, controller bounds, and dead input paths.
  - Issue: [Issue 03](ISSUES.md#issue-03---input-reliability)
  - Ref: [ROADMAP](ROADMAP.md)
  - Code: [src/core/input.c](../src/core/input.c)

- [ ] Memory stats cleanup - stop the allocator counters from lying after reset/free.
  - Issue: [Issue 04](ISSUES.md#issue-04---memory-stats-cleanup)
  - Ref: [ROADMAP](ROADMAP.md)
  - Code: [src/core/vmem.c](../src/core/vmem.c), [src/core/hashmap.c](../src/core/hashmap.c), [src/core/buffer.c](../src/core/buffer.c)

- [ ] Scene/prefab load safety - clamp file-driven counts and check reads.
  - Issue: [Issue 05](ISSUES.md#issue-05---sceneprefab-load-safety)
  - Ref: [ROADMAP](ROADMAP.md)
  - Code: [src/systems/ecs/scene.c](../src/systems/ecs/scene.c), [src/systems/ecs/prefab.c](../src/systems/ecs/prefab.c)

- [ ] Physics binding cleanup - put the binding layout in one place and fix the broadphase/narrowphase mismatch.
  - Issue: [Issue 06](ISSUES.md#issue-06---physics-binding-cleanup)
  - Ref: [ROADMAP](ROADMAP.md)
  - Code: [src/systems/Physics/physics.c](../src/systems/Physics/physics.c), [src/systems/Physics/broadphase.c](../src/systems/Physics/broadphase.c), [src/systems/Physics/narrowphase.c](../src/systems/Physics/narrowphase.c)

- [ ] Renderer submission fixes - material state, fence sync, and visibility publication.
  - Issue: [Issue 07](ISSUES.md#issue-07---renderer-submission-fixes)
  - Ref: [ROADMAP](ROADMAP.md)
  - Code: [src/systems/Rendering/renderer.c](../src/systems/Rendering/renderer.c)

- [ ] Editor restart safety - keep DLL-owned layouts alive until teardown order is safe.
  - Issue: [Issue 08](ISSUES.md#issue-08---editor-restart-safety)
  - Ref: [ROADMAP](ROADMAP.md)
  - Code: [Editor/src/editor.cpp](../Editor/src/editor.cpp), [Editor/src/main.cpp](../Editor/src/main.cpp)

## Next Up

- [ ] Finish file I/O cleanup.
- [ ] Fix the input layer so the editor and runtime stop disagreeing on buttons/controllers.
- [ ] Stabilize ECS storage before touching more renderer or editor work.

## Doing

- [ ] Nothing active yet.

## Blocked / Watch

- [ ] Anything that depends on physics bindings or ECS growth should wait until those are fixed.
- [ ] Editor restart work should wait until scene/load ownership is cleaned up.

## Done

- [ ] None yet.
