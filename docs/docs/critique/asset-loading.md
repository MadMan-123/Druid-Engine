# Critique: asset-loading

## Dead / unreachable / redundant code

- **[structural] [READ-VERIFIED]** objloader.c is entirely off the load path. `readResources` (resourcemanager.c:314) routes `.obj` to `loadModelFromAssimp`; repo-wide grep finds zero callers of `objModelCreate` / `objModelToIndexedModel` / `objModelDestroy`. ~430 lines of exported-but-unused parser. Either wire it up as a no-Assimp fallback or delete it.
- **[moderate] [READ-VERIFIED]** druid.h:1220–1228 declares six `DAPI` functions with no definition anywhere (`objModelParseOBJIndex`, `FindNextChar`, `parseOBJIndexValue`, `parseOBJFloatValue`, `SplitString`, `CompareOBJIndexPtr`). Confirmed orphaned (grep: declarations only). Any external caller gets a link error.
- **[trivial] [READ-VERIFIED]** resourcemanager.c:347 tests `strcmp(ext, "geom") == 0` twice in one condition.
- **[trivial] [READ-VERIFIED]** `shaderNameMap` counts occurrences (resourcemanager.c:277–286) but the second pass only checks existence (line 351); the increment logic is dead work.
- **[trivial] [READ-VERIFIED]** `MAX_FILE_SIZE` (resourcemanager.c:18) is never used. objModelCreateFace's capacity check (objloader.c:270) is unreachable: capacity is sized for worst-case all-quads and faces are only ever tri/quad.

## Bugs / missing error handling

- **[structural] [READ-VERIFIED]** No shader capacity check: both the default-shader path (resourcemanager.c:247–249) and the main shader branch (391–393) write `shaderHandles[shaderUsed]` and `shaderUsed++` with no `shaderUsed < shaderCount` guard — heap overrun past the `dalloc`'d array. Textures FATAL (406) and audio WARN (444) on full; shaders silently corrupt.
- **[moderate] [READ-VERIFIED]** `cleanUpResourceManager` (165–187) never `destroyMap`s the six ID hashmaps — leaked map storage. It also doesn't NULL the global `resources`, leaving every getter dangling after teardown.
- **[moderate] [READ-VERIFIED]** loadTextureMetadata loop bound `i < count && i < resources->textureUsed` (722) is wrong: file records are matched by *name*, so capping at current `textureUsed` silently drops trailing records whenever fewer textures are loaded than were saved.
- **[moderate] [READ-VERIFIED]** objModelCreateFace (objloader.c:205) silently truncates n-gons: the loop stops at 4 corners, so a pentagon is emitted as a quad (wrong geometry) rather than ignored, contradicting line 267's comment.
- **[moderate] [READ-VERIFIED]** objModelToIndexedModel:376–378 uses `p = realloc(p, …)` with no NULL check — on failure, leak plus NULL write.
- **[moderate] [NEEDS-BUILD]** createResourceManager checks the manager alloc but relies on `assert` (112–121) for all ten array allocs — compiled out under NDEBUG, so release builds NULL-deref later.
- **[moderate] [READ-VERIFIED]** Unbounded `strcpy(copyBuffer, filePath)` at 264 and 295 into 512-byte stack buffers; paths from recursive directory scan can exceed this. `pathNoExt` (319) also silently truncates >255-char paths, producing wrong shader file paths.
- **[trivial] [READ-VERIFIED]** `.glsl` is accepted (274, 347) but the loader only ever builds `.vert`/`.frag`/`.geom` paths, so a lone `.glsl` file just fails `createGraphicsProgram` silently.

## AI-slop / defensive patterns

- **[trivial] [READ-VERIFIED]** `if (manager->textureAlphaThresholds)` etc. (424–429, 629–630, 669–672, 741–748) guard arrays that are asserted non-NULL at creation — impossible-state defense scattered across four functions.
- **[trivial] [READ-VERIFIED]** saveTextureMetadata is saturated with leftover debug logging ("=== SAVE TEXTURE METADATA START ===", per-texture dumps, 624–690).
- **[trivial] [NEEDS-BUILD]** Local GL macro redefinitions (10–16) with nonstandard names `GL_MIN_FILTER`/`GL_MAG_FILTER` (values are correct); redundant if druid.h pulls in GL headers (it must — the file calls `glTexParameteri`).

## Perf

- **[moderate] [READ-VERIFIED]** saveTextureMetadata name recovery (654–665) is an O(textureUsed × mapCapacity) reverse scan through hashmap internals. A parallel `textureNames[]` array would make it O(n) and stop the function depending on HashMap layout.

## API ergonomics

- **[moderate] [READ-VERIFIED]** Split personality: lifecycle functions take a `ResourceManager *`, all getters read the global `resources`, which this module never assigns. Two managers cannot coexist; getters break silently if init forgets the global.
- **[moderate] [READ-VERIFIED]** No `IndexedModel` destroy function exists; callers of `objModelToIndexedModel` must free four `malloc`'d arrays plus the struct by hand, knowing internals. Mixed allocators (raw malloc in objloader.c vs tagged dalloc elsewhere) also bypass the engine's memory tracking.
- **[trivial] [READ-VERIFIED]** Compute shaders are named `"compute-shader-%d"` by load index (345) — unstable across content changes and impossible to look up by source filename.

## Memory allocation

Covered above (hashmap leak, realloc pattern, mixed allocators). No per-frame allocation issues — this module is init/shutdown-path only.
