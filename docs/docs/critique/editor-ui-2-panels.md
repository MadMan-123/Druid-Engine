# Critique — editor-ui-2-panels (Editor/src/editor.cpp, panels slice)

## Dead / redundant code
- [trivial][READ-VERIFIED] `drawDebugWindow` 2504–2506: dead `i32 dummy = -1; (void)dummy;` plus commented-out `ListBox`.
- [trivial][READ-VERIFIED] `drawArchetypesWindow` 3361: `entry->layout.count = totalFields;` is dead — overwritten at 3413 with `nextIdx`.
- [trivial][READ-VERIFIED] `renderPrefabPreview` 783–789: recomputes `gView`/`gProj` identical to `view`/`proj` built at 717–718.
- [trivial][READ-VERIFIED] `drawPrefabsWindow` 4464–4468: `pfFindField(ae, "Scale")` called twice back-to-back.
- [trivial][READ-VERIFIED] `drawTextureSelector` (1595): `comboID` parameter is never used; all 4 callers (7175–7178) pass strings that are ignored.
- [trivial][READ-VERIFIED] Stale comments: 3933 says "cap at 32" (cap is 64); 4011 says "32 archetypes × 32 fields" (arrays are MAX_ARCHETYPE_SYSTEMS × 64).

## Copy-paste / AI-slop patterns
- [structural][READ-VERIFIED] `drawArchetypesWindow`: the entire sentinel-header emission block is duplicated verbatim between "Generate Archetype Files" (3217–3335) and "Regenerate" (3812–3930), ~110 lines each. The 18-entry `physNames` list appears **four times** (3288, 3377, 3885, 3944), `physSizes` twice, and the snake_case enum-name converter is inlined four times. The registry physics-field-append block is also duplicated (3375–3410 vs 3942–3974). One `emitSentinelBlock()` + one shared physics-field table would remove ~250 lines.
- [moderate][READ-VERIFIED] `renderModelPreview` (553) and `renderPrefabPreview` (685) duplicate the whole G-buffer pass + fallback-material mesh loop + lighting pass; `renderMaterialPreview` (428) duplicates the lighting pass a third time. `shutdownMaterialRegistry` (513) then duplicates GBuffer teardown for both pipelines.
- [moderate][READ-VERIFIED] `drawFieldEditor` (4227) and `drawSoAField` (5573) are near-duplicate size+name-heuristic widget pickers with **different** heuristics (substring `ID/Type/Count/Index/Slot` vs. suffix `ID/Index/Count/Handle` + substring `Shader`). The same field can render as int in the Prefabs panel and float in the Inspector.
- [moderate][READ-VERIFIED] `.drsc` scan/filter/display logic duplicated between `drawScenesPanel` (5339–5376) and the Load Scene modal (8278–8344); Save-modal `SceneData` assembly (8239–8250) duplicates `snapshotScene` (7610–7620).
- [moderate][READ-VERIFIED] `drawLoadedEntitiesWindow`: editor-archetype table (7374–7473) and game-archetype table (7506–7570) are near-identical.
- [trivial][READ-VERIFIED] `setupScenePhysicsArchetype`: 25-line eligibility check (incl. inner strcmp loop) duplicated between count pass (1395–1421) and fill pass (1458–1481). Texture list-view vs grid-view editors duplicate wrap/filter/alpha widgets (6571–6606 vs 6638–6721). Skybox suffix list `right.jpg…` re-declared inline 3× (290, 6451, 6464) despite `g_skyboxFaceSuffixes` at 194.

## Bugs / missing error handling
- [moderate][READ-VERIFIED] Regenerate path 3928–3929: `else fclose(hf);` executes when `fopen` returned NULL — `fclose(NULL)` is UB. Triggered whenever the archetype header doesn't exist. The Generate path (3331–3334) handles this case correctly.
- [moderate][READ-VERIFIED] `loadPrefabForEditor` 4216–4217: `fieldSizes[i]` is clamped to 256 but the following `fread(out->fieldData[i], 1, fsz, f)` uses the **unclamped** `fsz` from the file — a corrupt/crafted `.prefab` overflows the 256-byte row (writes into adjacent `EditorPrefabData` memory).
- [moderate][READ-VERIFIED] Flags round-trip inconsistency: Generate writes `DRUID_FLAGS` without `ARCH_PERSISTENT`/`ARCH_FILE_UNIFORM_SCALE` (3247–3250) while Regenerate includes them (3842–3847). A rescan of a freshly generated archetype loses persistent/uniform-scale.
- [moderate][NEEDS-BUILD] Generated physics fields are all emitted as `FIELD(..., f32, HOT)` (3311, 3908) including `PhysicsBodyType`, which everywhere else is `u32` (cf. `g_scenePhysFields` 374, physSizes 3389). Same byte size, wrong declared type in the generated header.
- [moderate][READ-VERIFIED] Hot/Cold Field Separation UI (4006–4168) is pure theater: `hcIsHot` selections are never written anywhere; actual HOT/COLD in generated FIELDs is decided by an unrelated "is float/Vec3/Vec4" heuristic (3281, 3878).
- [moderate][READ-VERIFIED] Inspector Delete (5724–5745) swap-deletes but doesn't invalidate the `lastInspectorID` Euler cache (5686), so the swapped-in entity shows/receives the deleted entity's rotation on next drag. It also doesn't touch `g_ecsSceneMap`/`ecsSlotIDs` — deleting during play leaves `syncEcsToScene` (1564) writing to remapped scene IDs [NEEDS-RUNTIME for the play-mode effect].
- [moderate][READ-VERIFIED] Prefab "Spawn in Scene" (4825): silently no-ops when `entityCount >= entitySizeCache` (condition folded into the button `&&`, no message, button not disabled). It also never applies `ColliderOffsetX/Y/Z` from the prefab (edited at 4713–4728 but absent from the spawn block 4826–4881), and unset fields inherit stale SoA data from previously deleted entities.
- [trivial][READ-VERIFIED] Prefab Del (4448–4453): `remove()` return ignored; "Deleted prefab." reported even on failure. No confirmation.
- [trivial][READ-VERIFIED] Debug "FBOs Created" (2470–2472) sums slots 0–3 only while printing `/MAX_FBOS` (5) — confirms ref-doc Q6.
- [trivial][READ-VERIFIED] `editorLog` (8379) ignores `level` and silently drops all lines past 10000; Debug VSync checkbox (2450) assumes initial state false rather than querying it.

## Perf / allocation
- [moderate][READ-VERIFIED] Prefab list state (4363–4380) calls `listFilesInDirectory` and fully `fopen`/parses **every** `.prefab` file **every frame** while the list is visible. Same per-frame `malloc`/`free` churn for the file list.
- [trivial][READ-VERIFIED] Per-frame O(map-capacity) reverse scans to resolve names from indices: `drawTextureSelector` 1601–1610, shader combo 5782–5788, material combos 5858–5864 and 7148–7153, model name 6930–6935. Previews re-render every frame with per-draw `glGetUniformLocation` string lookups (469–499, 588–633, 735–773).
- [trivial][READ-VERIFIED] `syncEcsToScene` 1553–1560 re-strcmps PositionX/Y/Z field names per archetype per frame instead of caching at `populateEcsArchetypes` time.
- [trivial][READ-VERIFIED] Scenes-panel `sceneFiles` (5331) and Load-modal `sceneFiles` (8278) are never freed at shutdown; `g_ecsSceneMap` uses raw calloc/free alongside ECS-managed arenas.

## API ergonomics / fragile contracts
- [moderate][READ-VERIFIED] Confirmed ref-doc Q5: `scnFields[2]` (1297, 1578), `scnFields[0]` (1586) and the whole `scnFields[0..19]` block in `setupScenePhysicsArchetype` (1379–1389) hardcode SceneEntity field order while the generated `SE_*` enum (line 84) sits unused. Any reorder of `SCENEENTITY_FIELDS` silently corrupts sync/physics.
- [moderate][READ-VERIFIED] Confirmed ref-doc Q4: `colliderMatchTransform[4096]` (6017) — per-entity UI state in a fixed static array, silently inert for IDs ≥ 4096, not saved with the scene, and not remapped on swap-delete.
- [trivial][READ-VERIFIED] Ref-doc Q1 **resolved**: `insertMap` deep-copies the value (src/core/hashmap.c:73 `memcpy(pair->value, value, map->valueSize)`), so `insertMap(..., &resources->materialUsed)` at 7062 is correct — but only because it runs before the `++`; an ordering-dependent style hazard vs. the clearer `&newIdx` pattern at 7110.
- [trivial][READ-VERIFIED] Ref-doc Q3 confirmed: `InputInt("Max Entity Size", &entitySize)` (5466) writes a global nothing reads for resizing — vestigial. Ref-doc Q9 confirmed: `drawResourceTab_Audio` (7239) skips the `if (!resources) return;` guard its four sibling tabs use.
- [trivial][READ-VERIFIED] Scene-field naming inconsistency: `ColliderOffsetX/Y/Z` (79–81) are PascalCase amid otherwise camelCase scene field names — the reason prefab/scene name matching needs case-insensitive `fieldNameCmp`.
