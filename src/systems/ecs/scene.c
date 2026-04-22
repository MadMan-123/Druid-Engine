#include "../../../include/druid.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

SceneRuntime *sceneRuntime = NULL;
SceneManager *sceneManager = NULL;

static SceneTransitionFn g_onBeforeUnload = NULL;
static SceneTransitionFn g_onAfterLoad    = NULL;
static void             *g_callbackData   = NULL;

static b8 strEqIgnoreCase(const c8 *a, const c8 *b)
{
    if (!a || !b) return false;
    u32 i = 0;
    while (a[i] || b[i])
    {
        if (tolower((u8)a[i]) != tolower((u8)b[i]))
            return false;
        i++;
    }
    return true;
}

static i32 findFieldIndex(StructLayout *layout, const c8 *name)
{
    if (!layout || !name) return -1;
    for (u32 i = 0; i < layout->count; i++)
    {
        if (layout->fields[i].name && strEqIgnoreCase(layout->fields[i].name, name))
            return (i32)i;
    }
    return -1;
}

DAPI SceneRuntime *sceneRuntimeGetData(void)
{
    return sceneRuntime;
}

DAPI void sceneRuntimeSetCallbacks(SceneTransitionFn onBeforeUnload,
                                   SceneTransitionFn onAfterLoad,
                                   void *userData)
{
    g_onBeforeUnload = onBeforeUnload;
    g_onAfterLoad    = onAfterLoad;
    g_callbackData   = userData;
}

//=====================================================================================================================
// SceneRuntime helpers

// Case-insensitive field lookup within archetype 0 of a SceneData.
// Returns the raw field pointer or NULL if not found.
static void *findField(SceneData *sd, const c8 *name)
{
    if (!sd || sd->archetypeCount == 0 || !sd->archetypes) return NULL;
    Archetype *arch = &sd->archetypes[0];
    void **fields = getArchetypeFields(arch, 0);
    if (!fields || !arch->layout) return NULL;

    i32 idx = findFieldIndex(arch->layout, name);
    if (idx >= 0)
        return fields[idx];

    return NULL;
}

DAPI u32 sceneRemapModelIDs(SceneData *data)
{
    if (!data || !resources || !data->modelRefs || data->modelRefCount == 0)
        return 0;
    if (data->archetypeCount == 0 || !data->archetypes)
        return 0;

    Archetype *arch = &data->archetypes[0];
    if (!arch->layout || arch->activeChunkCount == 0)
        return 0;

    i32 modelIdx = findFieldIndex(arch->layout, "modelID");
    if (modelIdx < 0)
        return 0;

    u32 remapped = 0;
    u32 cursor = 0;
    for (u32 ch = 0; ch < arch->activeChunkCount && cursor < data->modelRefCount; ch++)
    {
        u32 count = arch->arena[ch].count;
        u32 *modelIDs = (u32 *)arch->arena[ch].fields[modelIdx];
        if (!modelIDs)
        {
            cursor += count;
            continue;
        }

        for (u32 e = 0; e < count && cursor < data->modelRefCount; e++, cursor++)
        {
            const c8 *modelName = data->modelRefs[cursor];
            if (!modelName || modelName[0] == '\0')
                continue;

            u32 mapped = 0;
            if (findInMap(&resources->modelIDs, modelName, &mapped)
                && mapped < resources->modelUsed)
            {
                if (modelIDs[e] != mapped)
                {
                    modelIDs[e] = mapped;
                    remapped++;
                }
            }
        }
    }

    return remapped;
}

// Find and load the startup scene: checks startup_scene.txt, then scans scenes/.
static b8 loadStartupSceneData(const c8 *projectDir, SceneData *out)
{
    // Try startup_scene.txt first
    c8 cfgPath[512];
    snprintf(cfgPath, sizeof(cfgPath), "%s/startup_scene.txt", projectDir);
    FILE *cfg = fopen(cfgPath, "rb");
    if (cfg)
    {
        c8 name[256] = {0};
        size_t r = fread(name, 1, sizeof(name) - 1, cfg);
        fclose(cfg);
        name[r] = '\0';
        for (size_t i = 0; name[i]; i++)
            if (name[i] == '\r' || name[i] == '\n') { name[i] = '\0'; break; }

        if (name[0])
        {
            c8 path[512];
            snprintf(path, sizeof(path), "%s/scenes/%s", projectDir, name);
            *out = loadScene(path);
            if (out->archetypeCount > 0 && out->archetypes)
                return true;
        }
    }

    // Fall back to scanning the scenes/ directory
    c8 scenesDir[512];
    snprintf(scenesDir, sizeof(scenesDir), "%s/scenes", projectDir);
    u32 fileCount = 0;
    c8 **files = listFilesInDirectory(scenesDir, &fileCount);
    if (files)
    {
        for (u32 i = 0; i < fileCount; i++)
        {
            u32 len = (u32)strlen(files[i]);
            if (len > 5 && strcmp(files[i] + len - 5, ".drsc") == 0)
            {
                *out = loadScene(files[i]);
                for (u32 j = 0; j < fileCount; j++) free(files[j]);
                free(files);
                if (out->archetypeCount > 0 && out->archetypes)
                    return true;
            }
        }
        for (u32 i = 0; i < fileCount; i++) free(files[i]);
        free(files);
    }

    // Last resort: try a default name
    c8 path[512];
    snprintf(path, sizeof(path), "%s/scenes/scene.drsc", projectDir);
    *out = loadScene(path);
    return (out->archetypeCount > 0 && out->archetypes);
}

b8 sceneRuntimeInit(const c8 *projectDir)
{
    if (!projectDir) return false;

    if (!sceneRuntime)
    {
        sceneRuntime = (SceneRuntime *)dalloc(sizeof(SceneRuntime), MEM_TAG_SCENE);
        if (sceneRuntime) memset(sceneRuntime, 0, sizeof(SceneRuntime));
    }
    if (!sceneRuntime) return false;

    if (!loadStartupSceneData(projectDir, &sceneRuntime->data))
    {
        ERROR("sceneRuntimeInit: no scene found in '%s'", projectDir);
        return false;
    }

    sceneRemapModelIDs(&sceneRuntime->data);

    // Extract entity count from archetype 0
    Archetype *arch = &sceneRuntime->data.archetypes[0];
    sceneRuntime->entityCount = archetypeEntityCount(arch);

    // Extract named field pointers
    sceneRuntime->positions        = (Vec3 *)findField(&sceneRuntime->data, "position");
    sceneRuntime->rotations        = (Vec4 *)findField(&sceneRuntime->data, "rotation");
    sceneRuntime->scales           = (Vec3 *)findField(&sceneRuntime->data, "scale");
    sceneRuntime->isActive         = (b8   *)findField(&sceneRuntime->data, "isActive");
    sceneRuntime->modelIDs         = (u32  *)findField(&sceneRuntime->data, "modelID");
    sceneRuntime->shaderHandles    = (u32  *)findField(&sceneRuntime->data, "shaderHandle");
    sceneRuntime->materialIDs      = (u32  *)findField(&sceneRuntime->data, "materialID");
    sceneRuntime->sceneCameraFlags = (b8   *)findField(&sceneRuntime->data, "isSceneCamera");
    sceneRuntime->archetypeIDs     = (u32  *)findField(&sceneRuntime->data, "archetypeID");
    sceneRuntime->ecsSlotIDs       = (u32  *)findField(&sceneRuntime->data, "ecsSlotID");

    // Apply scene materials only if ResourceManager has none yet.
    // Editor pre-populates materials (including .drmt presets) before calling runtimeCreate,
    // so skip the overwrite when materials are already present.
    if (sceneRuntime->data.materialCount > 0 && sceneRuntime->data.materials && resources
        && resources->materialUsed == 0)
    {
        u32 count = sceneRuntime->data.materialCount;
        if (count > resources->materialCount) count = resources->materialCount;
        memcpy(resources->materialBuffer, sceneRuntime->data.materials,
               sizeof(Material) * count);
        resources->materialUsed = count;
    }

    // Apply the designated scene-camera entity to the active renderer camera
    if (renderer && sceneRuntime->sceneCameraFlags
        && sceneRuntime->positions && sceneRuntime->rotations && sceneRuntime->isActive)
    {
        Camera *cam = (Camera *)bufferGet(&renderer->cameras, renderer->activeCamera);
        if (cam)
        {
            for (u32 i = 0; i < sceneRuntime->entityCount; i++)
            {
                if (sceneRuntime->isActive[i] && sceneRuntime->sceneCameraFlags[i])
                {
                    cam->pos         = sceneRuntime->positions[i];
                    cam->orientation = sceneRuntime->rotations[i];
                    break;
                }
            }
        }
    }

    sceneRuntime->loaded = true;
    INFO("SceneRuntime ready: %u entities", sceneRuntime->entityCount);
    return true;
}

b8 sceneRuntimeLoad(const c8 *filePath)
{
    if (!filePath || !sceneRuntime)
    {
        ERROR("sceneRuntimeLoad: NULL argument or sceneRuntime not initialized");
        return false;
    }

    // Invoke pre-unload callback before tearing down the old scene
    if (g_onBeforeUnload)
        g_onBeforeUnload(g_callbackData);

    // Collect persistent archetypes before destroying the old scene
    u32 persistCount = 0;
    Archetype *persistArchetypes = NULL;
    c8 persistNames[MAX_SCENE_ARCHETYPES][MAX_SCENE_NAME];
    memset(persistNames, 0, sizeof(persistNames));

    if (sceneRuntime->data.archetypes)
    {
        // Count persistent archetypes
        for (u32 i = 0; i < sceneRuntime->data.archetypeCount; i++)
        {
            if (FLAG_CHECK(sceneRuntime->data.archetypes[i].flags, ARCH_PERSISTENT))
                persistCount++;
        }

        // Stash persistent archetypes into a side array
        if (persistCount > 0)
        {
            persistArchetypes = (Archetype *)dalloc(sizeof(Archetype) * persistCount, MEM_TAG_SCENE);
            u32 p = 0;
            for (u32 i = 0; i < sceneRuntime->data.archetypeCount; i++)
            {
                if (FLAG_CHECK(sceneRuntime->data.archetypes[i].flags, ARCH_PERSISTENT))
                {
                    persistArchetypes[p] = sceneRuntime->data.archetypes[i];
                    strncpy(persistNames[p], sceneRuntime->data.archetypeNames[i],
                            MAX_SCENE_NAME - 1);
                    p++;
                }
            }
        }

        // Destroy only non-persistent archetypes
        for (u32 i = 0; i < sceneRuntime->data.archetypeCount; i++)
        {
            if (!FLAG_CHECK(sceneRuntime->data.archetypes[i].flags, ARCH_PERSISTENT))
                destroyArchetype(&sceneRuntime->data.archetypes[i]);
        }
        dfree(sceneRuntime->data.archetypes,
              sizeof(Archetype) * sceneRuntime->data.archetypeCount, MEM_TAG_SCENE);
        sceneRuntime->data.archetypes    = NULL;
        sceneRuntime->data.archetypeCount = 0;
    }
    if (sceneRuntime->data.materials)
    {
        dfree(sceneRuntime->data.materials,
              sizeof(Material) * sceneRuntime->data.materialCount, MEM_TAG_SCENE);
        sceneRuntime->data.materials    = NULL;
        sceneRuntime->data.materialCount = 0;
    }
    if (sceneRuntime->data.modelRefs)
    {
        dfree(sceneRuntime->data.modelRefs,
              sizeof(c8[MAX_NAME_SIZE]) * sceneRuntime->data.modelRefCount, MEM_TAG_SCENE);
        sceneRuntime->data.modelRefs = NULL;
        sceneRuntime->data.modelRefCount = 0;
    }

    // Load new scene
    sceneRuntime->data = loadScene(filePath);
    if (!sceneRuntime->data.archetypes || sceneRuntime->data.archetypeCount == 0)
    {
        ERROR("sceneRuntimeLoad: failed to load scene '%s'", filePath);
        // Restore persistent archetypes even on failure
        if (persistArchetypes)
        {
            sceneRuntime->data.archetypes = persistArchetypes;
            sceneRuntime->data.archetypeCount = persistCount;
            for (u32 p = 0; p < persistCount; p++)
                strncpy(sceneRuntime->data.archetypeNames[p], persistNames[p],
                        MAX_SCENE_NAME - 1);
        }
        sceneRuntime->loaded = false;
        return false;
    }

    sceneRemapModelIDs(&sceneRuntime->data);

    // Merge persistent archetypes back into the new scene's archetype array
    if (persistCount > 0 && persistArchetypes)
    {
        u32 newTotal = sceneRuntime->data.archetypeCount + persistCount;
        Archetype *merged = (Archetype *)dalloc(sizeof(Archetype) * newTotal, MEM_TAG_SCENE);
        if (merged)
        {
            // Copy new scene archetypes first
            memcpy(merged, sceneRuntime->data.archetypes,
                   sizeof(Archetype) * sceneRuntime->data.archetypeCount);

            // Append persistent archetypes
            for (u32 p = 0; p < persistCount; p++)
            {
                merged[sceneRuntime->data.archetypeCount + p] = persistArchetypes[p];
                strncpy(sceneRuntime->data.archetypeNames[sceneRuntime->data.archetypeCount + p],
                        persistNames[p], MAX_SCENE_NAME - 1);
            }

            dfree(sceneRuntime->data.archetypes,
                  sizeof(Archetype) * sceneRuntime->data.archetypeCount, MEM_TAG_SCENE);
            sceneRuntime->data.archetypes = merged;
            sceneRuntime->data.archetypeCount = newTotal;
        }
        dfree(persistArchetypes, sizeof(Archetype) * persistCount, MEM_TAG_SCENE);
        persistArchetypes = NULL;
    }

    // Re-extract field pointers
    Archetype *arch = &sceneRuntime->data.archetypes[0];
    sceneRuntime->entityCount      = archetypeEntityCount(arch);
    sceneRuntime->positions        = (Vec3 *)findField(&sceneRuntime->data, "position");
    sceneRuntime->rotations        = (Vec4 *)findField(&sceneRuntime->data, "rotation");
    sceneRuntime->scales           = (Vec3 *)findField(&sceneRuntime->data, "scale");
    sceneRuntime->isActive         = (b8   *)findField(&sceneRuntime->data, "isActive");
    sceneRuntime->modelIDs         = (u32  *)findField(&sceneRuntime->data, "modelID");
    sceneRuntime->shaderHandles    = (u32  *)findField(&sceneRuntime->data, "shaderHandle");
    sceneRuntime->materialIDs      = (u32  *)findField(&sceneRuntime->data, "materialID");
    sceneRuntime->sceneCameraFlags = (b8   *)findField(&sceneRuntime->data, "isSceneCamera");
    sceneRuntime->archetypeIDs     = (u32  *)findField(&sceneRuntime->data, "archetypeID");
    sceneRuntime->ecsSlotIDs       = (u32  *)findField(&sceneRuntime->data, "ecsSlotID");

    // Apply materials to ResourceManager
    if (sceneRuntime->data.materialCount > 0 && sceneRuntime->data.materials && resources)
    {
        u32 count = sceneRuntime->data.materialCount;
        if (count > resources->materialCount) count = resources->materialCount;
        memcpy(resources->materialBuffer, sceneRuntime->data.materials,
               sizeof(Material) * count);
        resources->materialUsed = count;
    }

    sceneRuntime->loaded = true;

    // Invoke post-load callback after the new scene is fully ready
    if (g_onAfterLoad)
        g_onAfterLoad(g_callbackData);

    INFO("sceneRuntimeLoad: '%s' (%u entities)", filePath, sceneRuntime->entityCount);
    return true;
}

void sceneRuntimeDestroy(void)
{
    if (!sceneRuntime) return;

    if (sceneRuntime->data.archetypes)
    {
        for (u32 i = 0; i < sceneRuntime->data.archetypeCount; i++)
            destroyArchetype(&sceneRuntime->data.archetypes[i]);
        dfree(sceneRuntime->data.archetypes,
              sizeof(Archetype) * sceneRuntime->data.archetypeCount, MEM_TAG_SCENE);
    }
    if (sceneRuntime->data.materials)
        dfree(sceneRuntime->data.materials,
              sizeof(Material) * sceneRuntime->data.materialCount, MEM_TAG_SCENE);
    if (sceneRuntime->data.modelRefs)
        dfree(sceneRuntime->data.modelRefs,
              sizeof(c8[MAX_NAME_SIZE]) * sceneRuntime->data.modelRefCount, MEM_TAG_SCENE);

    dfree(sceneRuntime, sizeof(SceneRuntime), MEM_TAG_SCENE);
    sceneRuntime = NULL;
}

//=====================================================================================================================

SceneData bakeScene(Scene *scene)
{
    SceneData sd = {0};
    if (!scene)
        return sd;

    EntityManager *em = &scene->manager;
    sd.archetypeCount = em->archetypeCount;
    sd.archetypes = em->archetypes; 

    for (u32 i = 0; i < em->archetypeCount && i < MAX_SCENE_ARCHETYPES; i++)
    {
        if (em->archetypes[i].layout && em->archetypes[i].layout->name)
        {
            strncpy(sd.archetypeNames[i],
                    em->archetypes[i].layout->name,
                    MAX_SCENE_NAME - 1);
        }
    }
    return sd;
}

// Layout:
//   SceneFileHeader
//   for each archetype:
//     archetypeName  [MAX_SCENE_NAME bytes]
//     fieldCount     (u32)
//     FieldFileHeader * fieldCount
//     entityCount    (u32)
//     raw SOA data   (fieldCount blocks, each entityCount * fieldSize bytes)


b8 saveScene(const c8 *filePath, SceneData *data)
{
    if (!filePath || !data)
    {
        ERROR("saveScene: NULL path or data");
        return false;
    }

    // Build optional model-name refs for archetype 0 so modelID can be remapped
    // on load when resource index ordering differs.
    u32 modelRefCount = 0;
    c8 (*modelRefs)[MAX_NAME_SIZE] = NULL;
    if (data->archetypeCount > 0 && data->archetypes && resources)
    {
        Archetype *arch0 = &data->archetypes[0];
        if (arch0->layout && arch0->activeChunkCount > 0)
        {
            i32 modelField = findFieldIndex(arch0->layout, "modelID");
            if (modelField >= 0)
            {
                modelRefCount = archetypeEntityCount(arch0);
                if (modelRefCount > 0)
                {
                    u64 modelRefsSize = (u64)modelRefCount * sizeof(*modelRefs);
                    modelRefs = (c8 (*)[MAX_NAME_SIZE])dalloc(modelRefsSize, MEM_TAG_SCENE);
                    if (modelRefs) memset(modelRefs, 0, modelRefsSize);
                }

                u32 cursor = 0;
                for (u32 ch = 0; ch < arch0->activeChunkCount && cursor < modelRefCount; ch++)
                {
                    u32 count = arch0->arena[ch].count;
                    u32 *ids = (u32 *)arch0->arena[ch].fields[modelField];
                    if (!ids)
                    {
                        cursor += count;
                        continue;
                    }

                    for (u32 e = 0; e < count && cursor < modelRefCount; e++, cursor++)
                    {
                        u32 mid = ids[e];
                        if (mid == (u32)-1 || mid >= resources->modelUsed)
                            continue;

                        const c8 *src = resources->modelBuffer[mid].name;
                        if (!src || src[0] == '\0')
                            continue;

                        const c8 *slash = strrchr(src, '/');
                        if (!slash) slash = strrchr(src, '\\');
                        const c8 *base = slash ? slash + 1 : src;

                        strncpy(modelRefs[cursor], base, MAX_NAME_SIZE - 1);
                        modelRefs[cursor][MAX_NAME_SIZE - 1] = '\0';
                    }
                }
            }
        }
    }

    FILE *f = fopen(filePath, "wb");
    if (!f)
    {
        ERROR("saveScene: cannot open %s for writing", filePath);
        return false;
    }

    SceneFileHeader hdr = {0};
    hdr.magic = SCENE_MAGIC;
    hdr.version = SCENE_VERSION;
    hdr.archetypeCount = data->archetypeCount;
    fwrite(&hdr, sizeof(SceneFileHeader), 1, f);

    for (u32 a = 0; a < data->archetypeCount; a++)
    {
        Archetype *arch = &data->archetypes[a];
        StructLayout *lay = arch->layout;

        // archetype name (fixed-size block)
        fwrite(data->archetypeNames[a], MAX_SCENE_NAME, 1, f);

        // field layout so the file is self-describing
        fwrite(&lay->count, sizeof(u32), 1, f);
        for (u32 fi = 0; fi < lay->count; fi++)
        {
            FieldFileHeader fh = {0};
            strncpy(fh.name, lay->fields[fi].name, MAX_FIELD_NAME - 1);
            fh.size = lay->fields[fi].size;
            fwrite(&fh, sizeof(FieldFileHeader), 1, f);
        }

        // entity capacity (so loader doesn't shrink the arena)
        fwrite(&arch->capacity, sizeof(u32), 1, f);

        // archetype flags (v4+): ARCH_SINGLE, ARCH_PERSISTENT, ARCH_BUFFERED, ARCH_PHYSICS_BODY
        fwrite(&arch->flags, sizeof(u8), 1, f);

        // total live entity count across all chunks
        u32 liveCount = archetypeEntityCount(arch);
        fwrite(&liveCount, sizeof(u32), 1, f);

        // raw SOA data — linearize all chunks into a flat stream per field
        for (u32 fi = 0; fi < lay->count; fi++)
        {
            u32 fieldSize = lay->fields[fi].size;
            for (u32 ch = 0; ch < arch->activeChunkCount; ch++)
            {
                u32 chunkCount = arch->arena[ch].count;
                u32 bytes = fieldSize * chunkCount;
                if (bytes > 0)
                {
                    fwrite(arch->arena[ch].fields[fi], bytes, 1, f);
                }
            }
        }
    }

    // -- material block --
    fwrite(&data->materialCount, sizeof(u32), 1, f);
    if (data->materialCount > 0 && data->materials)
    {
        fwrite(data->materials, sizeof(Material), data->materialCount, f);
    }

    // v5+: optional model-name references for archetype-0 entities
    fwrite(&modelRefCount, sizeof(u32), 1, f);
    if (modelRefCount > 0 && modelRefs)
    {
        fwrite(modelRefs, sizeof(*modelRefs), modelRefCount, f);
    }

    if (modelRefs)
        dfree(modelRefs, sizeof(*modelRefs) * modelRefCount, MEM_TAG_SCENE);

    fclose(f);
    INFO("Saved scene to %s (%u archetypes, %u materials)",
         filePath, data->archetypeCount, data->materialCount);
    return true;
}

SceneData loadScene(const c8 *filePath)
{
    SceneData out = {0};
    if (!filePath)
    {
        ERROR("loadScene: NULL path");
        return out;
    }

    FILE *f = fopen(filePath, "rb");
    if (!f)
    {
        ERROR("loadScene: cannot open %s", filePath);
        return out;
    }

    SceneFileHeader hdr = {0};
    if (fread(&hdr, sizeof(SceneFileHeader), 1, f) != 1)
    {
        ERROR("loadScene: failed to read header");
        fclose(f);
        return out;
    }

    if (hdr.magic != SCENE_MAGIC)
    {
        ERROR("loadScene: bad magic – not a Druid scene file");
        fclose(f);
        return out;
    }

    // Accept any version >= 2; the format is self-describing (field names + sizes
    // are stored inline) so older scenes can be loaded and migrated at runtime.
    if (hdr.version < 2 || hdr.version > SCENE_VERSION)
    {
        ERROR("loadScene: unsupported version %u (expected 2–%u)",
              hdr.version, SCENE_VERSION);
        fclose(f);
        return out;
    }
    if (hdr.version < SCENE_VERSION)
        WARN("loadScene: scene version %u < current %u — will migrate on next save",
             hdr.version, SCENE_VERSION);

    out.archetypeCount = hdr.archetypeCount;
    out.archetypes =
        (Archetype *)dalloc(sizeof(Archetype) * hdr.archetypeCount, MEM_TAG_SCENE);
    if (!out.archetypes)
    {
        ERROR("loadScene: allocation failed");
        fclose(f);
        return out;
    }
    memset(out.archetypes, 0, sizeof(Archetype) * hdr.archetypeCount);

    for (u32 a = 0; a < hdr.archetypeCount; a++)
    {
        //archetype name 
        fread(out.archetypeNames[a], MAX_SCENE_NAME, 1, f);

        // field layout 
        u32 fieldCount = 0;
        fread(&fieldCount, sizeof(u32), 1, f);

        FieldInfo *fields =
            (FieldInfo *)dalloc(sizeof(FieldInfo) * fieldCount, MEM_TAG_SCENE);

        for (u32 fi = 0; fi < fieldCount; fi++)
        {
            FieldFileHeader fh = {0};
            fread(&fh, sizeof(FieldFileHeader), 1, f);

            // duplicate name string so it persists
            u32 len = (u32)strlen(fh.name);
            c8 *nameCopy = (c8 *)dalloc(len + 1, MEM_TAG_SCENE);
            memcpy(nameCopy, fh.name, len + 1);

            fields[fi].name = nameCopy;
            fields[fi].size = fh.size;
            fields[fi].temperature = FIELD_TEMP_COLD;
        }

        // build a StructLayout from what we just read
        StructLayout *layout =
            (StructLayout *)dalloc(sizeof(StructLayout), MEM_TAG_SCENE);

        u32 nameLen = (u32)strlen(out.archetypeNames[a]);
        c8 *layoutName = (c8 *)dalloc(nameLen + 1, MEM_TAG_SCENE);
        memcpy(layoutName, out.archetypeNames[a], nameLen + 1);

        layout->name = layoutName;
        layout->fields = fields;
        layout->count = fieldCount;

        // capacity (saved so we don't shrink the arena)
        u32 capacity = 0;
        fread(&capacity, sizeof(u32), 1, f);
        if (capacity == 0)
            capacity = 128; // fallback default

        // archetype flags (v4+)
        u8 archFlags = 0;
        if (hdr.version >= 4)
            fread(&archFlags, sizeof(u8), 1, f);

        // live entity count
        u32 liveCount = 0;
        fread(&liveCount, sizeof(u32), 1, f);

        // ensure capacity is at least as large as the live count,
        // and always at least 128 so there is room for new entities.
        if (capacity < 128)
            capacity = 128;
        if (capacity < liveCount)
            capacity = liveCount;
        if (!createArchetype(layout, capacity, &out.archetypes[a]))
        {
            ERROR("loadScene: failed to create archetype '%s'",
                  out.archetypeNames[a]);
            fclose(f);
            // Reset archetypeCount to only successfully loaded archetypes
            out.archetypeCount = a;
            return out;
        }
        out.archetypes[a].flags = archFlags;

        // read SOA data — distribute into chunks
        {
            Archetype *loadArch = &out.archetypes[a];
            u32 remaining = liveCount;
            u32 chunkIdx = 0;

            while (remaining > 0 && chunkIdx < loadArch->arenaCount)
            {
                u32 toRead = remaining;
                if (toRead > loadArch->chunkCapacity)
                    toRead = loadArch->chunkCapacity;

                loadArch->arena[chunkIdx].count = toRead;

                for (u32 fi = 0; fi < fieldCount; fi++)
                {
                    u32 bytes = fields[fi].size * toRead;
                    if (bytes > 0)
                    {
                        fread(loadArch->arena[chunkIdx].fields[fi], bytes, 1, f);
                    }
                }

                remaining -= toRead;
                chunkIdx++;
            }
            loadArch->activeChunkCount = chunkIdx;
            loadArch->cachedEntityCount = liveCount;  // Update entity count cache
        }
    }

    u32 matCount = 0;
    if (fread(&matCount, sizeof(u32), 1, f) == 1 && matCount > 0)
    {
        out.materialCount = matCount;
        out.materials = (Material *)dalloc(sizeof(Material) * matCount, MEM_TAG_SCENE);
        if (out.materials)
        {
            fread(out.materials, sizeof(Material), matCount, f);
        }
    }

    if (hdr.version >= 5)
    {
        u32 modelRefCount = 0;
        if (fread(&modelRefCount, sizeof(u32), 1, f) == 1 && modelRefCount > 0)
        {
            out.modelRefs = (c8 (*)[MAX_NAME_SIZE])dalloc(sizeof(*out.modelRefs) * modelRefCount, MEM_TAG_SCENE);
            if (out.modelRefs)
            {
                size_t got = fread(out.modelRefs, sizeof(*out.modelRefs), modelRefCount, f);
                if (got != modelRefCount)
                {
                    dfree(out.modelRefs, sizeof(*out.modelRefs) * modelRefCount, MEM_TAG_SCENE);
                    out.modelRefs = NULL;
                    out.modelRefCount = 0;
                }
                else
                {
                    out.modelRefCount = modelRefCount;
                }
            }
        }
    }

    fclose(f);

    // Opportunistic remap when resources are already available.
    sceneRemapModelIDs(&out);

    INFO("Loaded scene from %s (%u archetypes, %u materials)",
         filePath, out.archetypeCount, out.materialCount);
    return out;
}


SceneManager *createSceneManager(u32 sceneCapacity)
{
    SceneManager *manager = (SceneManager *)dalloc(sizeof(SceneManager), MEM_TAG_SCENE);
    if (!manager)
    {
        ERROR("Failed to allocate memory for SceneManager");
        return NULL;
    }

    // create an arena for scene data
    manager->data = (Arena *)dalloc(sizeof(Arena), MEM_TAG_SCENE);
    if (!manager->data)
    {
        ERROR("Failed to allocate memory for SceneManager arena");
        dfree(manager, sizeof(SceneManager), MEM_TAG_SCENE);
        return NULL;
    }

    // 1024 bytes extra
    const u32 EXTRA_DATA = 1024;

    if (!arenaCreate(manager->data,
                     sizeof(SceneManager) +
                         (sizeof(SceneData) + (EXTRA_DATA) * sceneCapacity +
                          sizeof(Scene))))
    {
        ERROR("Failed to create arena for SceneManager");
        dfree(manager->data, sizeof(Arena), MEM_TAG_SCENE);
        dfree(manager, sizeof(SceneManager), MEM_TAG_SCENE);
        return NULL;
    }
    manager->scenes =
        (SceneData *)aalloc(manager->data, sizeof(SceneData) * sceneCapacity);
    if (!manager->scenes)
    {
        ERROR("Failed to allocate memory for SceneData array");
        dfree(manager, sizeof(SceneManager), MEM_TAG_SCENE);
        return NULL;
    }

    manager->sceneCapacity = sceneCapacity;
    manager->sceneCount = 0;

    // allocate the current scene as an empty scene
    manager->currentScene = (Scene *)aalloc(manager->data, sizeof(Scene));
    if (!manager->currentScene)
    {
        ERROR("Failed to allocate memory for current Scene");
        arenaDestroy(manager->data);
        dfree(manager, sizeof(SceneManager), MEM_TAG_SCENE);
        return NULL;
    }

    return manager;
}

void destroySceneManager(SceneManager *manager)
{
    if (!manager)
        return;

    arenaDestroy(manager->data);
    dfree(manager, sizeof(SceneManager), MEM_TAG_SCENE);
    DEBUG("Destroyed scene manager");
}

u32 addScene(SceneManager *manager, SceneData *sceneData)
{
    if (manager->sceneCount >= manager->sceneCapacity)
    {
        WARN("SceneManager capacity reached, cannot add more scenes");
        return (u32)-1;
    }

    manager->scenes[manager->sceneCount] = *sceneData;
    manager->sceneCount++;
    return manager->sceneCount - 1;
}

void removeScene(SceneManager *manager, u32 sceneIndex)
{
    if (sceneIndex >= manager->sceneCount)
    {
        WARN("Invalid scene index %u for removal", sceneIndex);
        return;
    }

    // shift all scenes down
    for (u32 i = sceneIndex; i < manager->sceneCount - 1; i++)
    {
        manager->scenes[i] = manager->scenes[i + 1];
    }
    manager->sceneCount--;
}

void switchScene(SceneManager *manager, u32 sceneIndex)
{
    if (sceneIndex >= manager->sceneCount)
    {
        WARN("switchScene: invalid index %u", sceneIndex);
        return;
    }

    // Collect persistent archetypes from the current scene
    Scene *cur = manager->currentScene;
    u32 persistCount = 0;
    Archetype *persistArchetypes = NULL;

    for (u32 i = 0; i < cur->manager.archetypeCount; i++)
    {
        if (FLAG_CHECK(cur->manager.archetypes[i].flags, ARCH_PERSISTENT))
            persistCount++;
    }

    if (persistCount > 0)
    {
        persistArchetypes = (Archetype *)dalloc(sizeof(Archetype) * persistCount, MEM_TAG_SCENE);
        u32 p = 0;
        for (u32 i = 0; i < cur->manager.archetypeCount; i++)
        {
            if (FLAG_CHECK(cur->manager.archetypes[i].flags, ARCH_PERSISTENT))
                persistArchetypes[p++] = cur->manager.archetypes[i];
        }
    }

    // Destroy only non-persistent archetypes
    for (u32 i = 0; i < cur->manager.archetypeCount; i++)
    {
        if (!FLAG_CHECK(cur->manager.archetypes[i].flags, ARCH_PERSISTENT))
            destroyArchetype(&cur->manager.archetypes[i]);
    }

    // Apply the stored SceneData as the new live scene
    SceneData *sd = &manager->scenes[sceneIndex];
    cur->manager.archetypeCount = sd->archetypeCount;
    cur->manager.archetypes = sd->archetypes;

    // Append persistent archetypes to the new scene
    if (persistCount > 0 && persistArchetypes)
    {
        // Need to grow the archetype array to hold persistent entries
        u32 newTotal = cur->manager.archetypeCount + persistCount;
        Archetype *merged = (Archetype *)dalloc(sizeof(Archetype) * newTotal, MEM_TAG_SCENE);
        if (merged)
        {
            memcpy(merged, cur->manager.archetypes,
                   sizeof(Archetype) * cur->manager.archetypeCount);
            memcpy(merged + cur->manager.archetypeCount, persistArchetypes,
                   sizeof(Archetype) * persistCount);
            cur->manager.archetypes = merged;
            cur->manager.archetypeCount = newTotal;
        }
        dfree(persistArchetypes, sizeof(Archetype) * persistCount, MEM_TAG_SCENE);
    }

    INFO("Switched to scene %u (%u persistent archetypes carried over)",
         sceneIndex, persistCount);
}
