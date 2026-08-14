# Critique: renderer (src/systems/Rendering/renderer.c)

## Bugs / correctness

- **[structural] [READ-VERIFIED] Double-added SSBO base in flush fallback.** `rendererFlushInstancedModels`: line 1202 sets `s_submitGroups[g].base = ssboBase + running`, but the fallback draw at line 1288 uploads `ssboBase + s_submitGroups[g].base` — `ssboBase` added twice. Wrong matrices whenever `ibuf->count != 0` at flush (i.e. whenever the archetype path ran first). The indirect path (line 1249) uses `base` correctly, confirming the fallback is the bug.
- **[structural] [READ-VERIFIED] Material state is last-wins on the indirect path.** Lines 1267–1270 call `updateMaterial` per mesh *while building* commands; all commands then dispatch in one `indirectBufferDispatch` (1275–1276). Every batch renders with the final group's material/textures. Same for VAO: nothing checks `mesh->buffered` (cf. `drawMeshInstanced` line 602), so standalone-VAO meshes get garbage `firstIndex`/`baseVertex` commands [NEEDS-BUILD].
- **[structural] [READ-VERIFIED] No fence on the indirect path.** The fallback fences (1303–1305) but the indirect branch returns without `glFenceSync`, so next frame's `instanceBufferAdvance` won't wait — CPU can overwrite the persistent-mapped SSBO while the GPU reads it. Archetype path always fences (1048–1050). Visible corruption is [NEEDS-RUNTIME].
- **[moderate] [READ-VERIFIED] Inconsistent ModelID>=4096 handling.** Instanced path culls `mid >= MODEL_LUT_SIZE` (line 825); the per-entity fallback (line 1090) draws them. Same entity appears/disappears depending on which path runs. Silent — no WARN.
- **[moderate] [READ-VERIFIED] `frameVisible` can be published partially filled.** `frameVisibleCount` is set to full entity count (749–750) before the fill; any `goto per_entity_fallback` (736, 777, 808, 893, 929) or the mid-loop `overflow` break leaves an uninitialized `frameAlloc` tail that physics will read as garbage.
- **[moderate] [READ-VERIFIED] Unused `fov/nearClip/farClip` in `createRenderer`** (line 12) — vestigial signature; callers reasonably assume a default camera gets created, but `activeCamera = (u32)-1`.
- **[trivial] [READ-VERIFIED] `rendererReleaseCamera` falls back to `activeCamera = 0`** (270–271) without checking slot 0 occupancy.
- **[trivial] [READ-VERIFIED] No guard on packed-index limits** (line 882): `ch >= 4096` or `i >= 1M` silently corrupts `(ch<<20)|i`.

## Dead / redundant code

- **[trivial] [READ-VERIFIED]** Empty braces block, lines 698–700 ("One-shot diagnostic" — body deleted, husk left).
- **[trivial] [READ-VERIFIED]** Wrong comment at 1182–1184: claims it uses the cache "populated in rendererDefaultArchetypeRender" but declares its own separate statics — copy-paste residue.
- **[trivial] [READ-VERIFIED]** `indirectBufferReset` runs in both `rendererBeginFrame` (168–171) and again in flush (1239); `destroyRenderer` (205–217) copy-pastes the GBuffer GL teardown from `rendererReleaseGBuffer` (319–324).
- **[trivial] [READ-VERIFIED]** Comment at 434 says "Disable depth writes" but the code disables depth *test*.

## Memory

- **[moderate] [READ-VERIFIED] ~9.4 MB static BSS always resident:** `s_submitList[131072]` of `{u32, Mat4}` (line 1143), paid even if the direct path is never used.
- **[moderate] [READ-VERIFIED] Raw `malloc/free` scratch buffers** (`groupIndices` 725–731, `entityGroupTag` 766–772, `visBuf` 800–807, `sortedBuf` 919–926) bypass `dalloc`/`MEM_TAG_RENDERER` tracking, are never freed at shutdown, and duplicate what the engine's frame allocator (used 6 lines away at 749) is for.
- **[trivial] [READ-VERIFIED]** `rendererLightingPass` `quadVAO/quadVBO` (438–460) never deleted; stale across renderer destroy/recreate. `rendererUploadLights` memsets a full ~8.2 KB `LightSSBOData` per call (1360) to upload `count` lights, and hardcodes the header size as `sizeof(u32)*4` (1366).

## Perf

- **[moderate] [READ-VERIFIED] O(N×G) scatter in flush:** lines 1207–1218 rescan up to 256 groups per submitted entry (worst case ~33M compares/frame); `rendererSubmitInstance` also linear-scans groups per call (1156–1157). The archetype path built a `modelToGroup` LUT (712) to kill exactly this pattern — the submit path never got it.
- **[trivial] [READ-VERIFIED] Single-entry uniform caches** (398–407, 686–694, 1183–1189) thrash if two shaders alternate, and go stale/wrong if a deleted program's GL id is reused [NEEDS-RUNTIME].

## API ergonomics / silent failures

- **[moderate] [READ-VERIFIED] `rendererFlushInstances` (178) never draws** despite the name — binds an SSBO and calls `glUseProgram`, nothing else. Trap for callers.
- **[moderate] [READ-VERIFIED] Silent no-render paths:** `getRenderFieldCache` returns NULL when its 32-entry cache fills (515–516) and the archetype is skipped with no log (620–621); flush bails and *discards the whole queue* on missing uniform or capacity overflow (1191–1195) silently; `rendererUploadLights` returns silently on `frameAlloc` failure (1359).
- **[moderate] [READ-VERIFIED] `modelSSBOEndFrame` only runs inside `rendererFlushInstancedModels`** (1224–1227): the archetype fallback's `modelSSBOWrite`s (1109) are never flushed unless the unrelated submit-queue flush also happens that frame [NEEDS-BUILD for modelSSBO internals].
