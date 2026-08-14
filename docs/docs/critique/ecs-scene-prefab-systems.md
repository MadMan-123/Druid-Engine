# Critique: ecs-scene-prefab-systems

## Structural

1. **[structural][READ-VERIFIED] `loadScene` buffer overflow via unclamped `archetypeCount`.** scene.c:641-655: `hdr.archetypeCount` comes straight from the file and is never clamped to `MAX_SCENE_ARCHETYPES` (32), but `fread(out.archetypeNames[a], MAX_SCENE_NAME, 1, f)` indexes the fixed `c8[32][128]` array in `SceneData` (druid.h:1044). A corrupt/malicious file with count > 32 writes past the struct.

2. **[structural][READ-VERIFIED] Use-after-free + double-free in `loadStartupSceneData`.** scene.c:150-167: inside the scan loop, `files[]` and `files` are freed *before* checking load success. If the first `.drsc` fails to load, the loop continues with `strlen(files[i])` on freed memory and the post-loop cleanup frees everything again.

3. **[structural][READ-VERIFIED] `loadScene` layout allocations have no free path.** scene.c:661-689 dallocs a `StructLayout`, name copy, `FieldInfo` array, and per-field name copies per archetype. `destroyArchetype` (archetype.c:287-339) never touches `arch->layout`, and neither `sceneRuntimeLoad` (scene.c:293-299) nor `sceneRuntimeDestroy` (scene.c:404-410) frees them — leaked on every scene switch and at shutdown.

4. **[structural][READ-VERIFIED] `loadScene` reads are unchecked/unbounded.** scene.c:655-761: every `fread` in the archetype loop (name, `fieldCount`, field headers, capacity, flags, `liveCount`, SOA data) ignores its return value; a truncated file silently yields garbage archetypes. `fieldCount` and `matCount` (755-758) drive unbounded `dalloc`s; `fh.name` and `archetypeNames[a]` (strlen'd at 670/683) are not guaranteed NUL-terminated by the file.

5. **[structural][READ-VERIFIED] `prefabLoad` integer-overflow bounds check.** prefab.c:163: `dataOff + fsize > PREFAB_DATA_POOL_SIZE` with u32 `fsize` from the file — `fsize ≈ 0xFFFFFFFF` wraps the sum, passes the check, and `fread(prefab->data + dataOff, 1, fsize, f)` (168) then overflows the 768-byte pool with whatever bytes remain in the file.

6. **[structural][NEEDS-BUILD] `createSceneManager` arena is undersized.** scene.c:818-831 sizes the arena at ~1024 B per scene, but each `SceneData` is > 4 KB (`archetypeNames[32][128]` alone). `aalloc` of `scenes[sceneCapacity]` should fail for capacity ≥ 2, making the function unusable beyond one scene.

## Moderate

7. **[moderate][READ-VERIFIED] `loadECSSystemDLL` per-archetype path unreachable.** ecssystem.c:10 memsets `*out`, then line 21 checks `out->name[0] != '\0'` — always false; only the legacy symbol is ever tried. `ECSSystemDLL.name` is never assigned anywhere. Near-duplicate of `loadECSSystemFromHandle` (49-65), which is the working version.

8. **[moderate][READ-VERIFIED] Dead enum-name-preservation machinery.** `findExistingEnumName` (ecssystem.c:134-161) is never called; `buildGenBlock`'s `existingBlock` param (173) is never read — regeneration always re-derives enum names, so user renames are silently clobbered. Delete or finish.

9. **[moderate][READ-VERIFIED] `stampPrefab` trusts the prefab's field size.** prefab.c:76-77 uses `pf->size` as both stride and copy length with no check against `arch->layout->fields[j].size`; after archetype regeneration changes a field's type, stale prefabs corrupt adjacent entities' data.

10. **[moderate][READ-VERIFIED] `sceneRuntimeLoad` failure path leaks.** scene.c:319-334: if `loadScene` returns `archetypeCount == 0` but a non-NULL `archetypes` array (mid-file `createArchetype` failure at a=0), the branch overwrites `data.archetypes` with `persistArchetypes`, leaking the array (and any loaded materials/modelRefs are silently kept from the failed file).

11. **[moderate][READ-VERIFIED] `switchScene` leaks its merged array and mutates stored scenes.** scene.c:932-951: `cur->manager.archetypes` aliases `manager->scenes[sceneIndex].archetypes`; the merged (persistent-appended) array is never freed on the next switch nor written back, and the next switch's `destroyArchetype` guts archetypes still referenced by the stored `SceneData`.

12. **[moderate][READ-VERIFIED] `Arena` struct leaked in SceneManager paths.** `arenaDestroy` (arena.c:13-19) frees only `arena->data`; `destroySceneManager` (scene.c:860-861) never dfrees the dalloc'd `Arena` struct (scene.c:809). The `scenes` aalloc-failure path (833-837) leaks both the Arena struct *and* its buffer (no `arenaDestroy`).

13. **[moderate][READ-VERIFIED] `buildGenBlock`'s `BA` macro: unchecked `realloc`** (ecssystem.c:183) — OOM → `blk = NULL` → memcpy crash; and the 512-byte `_t` staging buffer silently truncates long FIELD lines.

14. **[moderate][NEEDS-RUNTIME] `buildArchetypeSystems` is Windows-only.** ecssystem.c:469 hardcodes `cd /d "..."` cmd.exe syntax into `platformPipeOpen`; fails on any POSIX shell.

15. **[moderate][NEEDS-RUNTIME] `PrefabBucket.archRuntime` dangles across scene loads.** prefab.c:50: cached pointer compared against freed archetype slots after `sceneRuntimeLoad` reallocates the array; address reuse would bind a bucket to the wrong archetype.

## Trivial

16. **[trivial][READ-VERIFIED]** saveScene layout comment (scene.c:446-454) omits the `capacity`, `flags`, material, and modelRef blocks the code writes (525-586) — actively misleading for format consumers.
17. **[trivial][READ-VERIFIED]** Ten-line field-pointer extraction block copy-pasted verbatim in `sceneRuntimeInit` (scene.c:200-209) and `sceneRuntimeLoad` (369-378); should be one helper.
18. **[trivial][READ-VERIFIED]** prefab.c:11/20 uses libc `malloc`/`free` while every sibling allocation uses tagged `dalloc`/`dfree` — invisible to the memory tracker.
19. **[trivial][READ-VERIFIED]** `generateArchetypeFiles`: `upperName[strlen(archetypeName)] = '\0'` (ecssystem.c:270) writes OOB for names ≥ 256 chars; `camelCaseName` (264) reads `archetypeName[1]` on an empty string.

Dead code beyond items 7-8: none. Missing findings categories otherwise: none worth inventing.
