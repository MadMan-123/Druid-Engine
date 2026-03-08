#include "../../../include/druid.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
        EntityArena *ea = &arch->arena[0]; // first arena

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

        // live entity count
        u32 liveCount = ea->count;
        fwrite(&liveCount, sizeof(u32), 1, f);

        // raw SOA data – one block per field, only the live entities
        for (u32 fi = 0; fi < lay->count; fi++)
        {
            u32 bytes = lay->fields[fi].size * liveCount;
            if (bytes > 0)
            {
                fwrite(ea->fields[fi], bytes, 1, f);
            }
        }
    }

    // -- material block --
    fwrite(&data->materialCount, sizeof(u32), 1, f);
    if (data->materialCount > 0 && data->materials)
    {
        fwrite(data->materials, sizeof(Material), data->materialCount, f);
    }

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

    if (hdr.version != SCENE_VERSION)
    {
        ERROR("loadScene: unsupported version %u (expected %u)",
              hdr.version, SCENE_VERSION);
        fclose(f);
        return out;
    }

    out.archetypeCount = hdr.archetypeCount;
    out.archetypes =
        (Archetype *)malloc(sizeof(Archetype) * hdr.archetypeCount);
    if (!out.archetypes)
    {
        ERROR("loadScene: allocation failed");
        fclose(f);
        return out;
    }

    for (u32 a = 0; a < hdr.archetypeCount; a++)
    {
        //archetype name 
        fread(out.archetypeNames[a], MAX_SCENE_NAME, 1, f);

        // field layout 
        u32 fieldCount = 0;
        fread(&fieldCount, sizeof(u32), 1, f);

        FieldInfo *fields =
            (FieldInfo *)malloc(sizeof(FieldInfo) * fieldCount);

        for (u32 fi = 0; fi < fieldCount; fi++)
        {
            FieldFileHeader fh = {0};
            fread(&fh, sizeof(FieldFileHeader), 1, f);

            // duplicate name string so it persists
            u32 len = (u32)strlen(fh.name);
            c8 *nameCopy = (c8 *)malloc(len + 1);
            memcpy(nameCopy, fh.name, len + 1);

            fields[fi].name = nameCopy;
            fields[fi].size = fh.size;
        }

        // build a StructLayout from what we just read
        StructLayout *layout =
            (StructLayout *)malloc(sizeof(StructLayout));

        u32 nameLen = (u32)strlen(out.archetypeNames[a]);
        c8 *layoutName = (c8 *)malloc(nameLen + 1);
        memcpy(layoutName, out.archetypeNames[a], nameLen + 1);

        layout->name = layoutName;
        layout->fields = fields;
        layout->count = fieldCount;

        // capacity (saved so we don't shrink the arena)
        u32 capacity = 0;
        fread(&capacity, sizeof(u32), 1, f);
        if (capacity == 0)
            capacity = 128; // fallback default

        // live entity count
        u32 liveCount = 0;
        fread(&liveCount, sizeof(u32), 1, f);

        // ensure capacity is at least as large as the live count
        if (capacity < liveCount)
            capacity = liveCount;
        if (!createArchetype(layout, capacity, &out.archetypes[a]))
        {
            ERROR("loadScene: failed to create archetype '%s'",
                  out.archetypeNames[a]);
            fclose(f);
            return out;
        }

        // read SOA data straight into the arena fields
        EntityArena *ea = &out.archetypes[a].arena[0];
        ea->count = liveCount;

        for (u32 fi = 0; fi < fieldCount; fi++)
        {
            u32 bytes = fields[fi].size * liveCount;
            if (bytes > 0)
            {
                fread(ea->fields[fi], bytes, 1, f);
            }
        }
    }

    u32 matCount = 0;
    if (fread(&matCount, sizeof(u32), 1, f) == 1 && matCount > 0)
    {
        out.materialCount = matCount;
        out.materials = (Material *)malloc(sizeof(Material) * matCount);
        if (out.materials)
        {
            fread(out.materials, sizeof(Material), matCount, f);
        }
    }

    fclose(f);
    INFO("Loaded scene from %s (%u archetypes, %u materials)",
         filePath, out.archetypeCount, out.materialCount);
    return out;
}


SceneManager *createSceneManager(u32 sceneCapacity)
{
    SceneManager *manager = (SceneManager *)malloc(sizeof(SceneManager));
    if (!manager)
    {
        ERROR("Failed to allocate memory for SceneManager");
        return NULL;
    }

    // create an arena for scene data
    manager->data = (Arena *)malloc(sizeof(Arena));
    if (!manager->data)
    {
        ERROR("Failed to allocate memory for SceneManager arena");
        free(manager);
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
        free(manager->data);
        free(manager);
        return NULL;
    }
    manager->scenes =
        (SceneData *)aalloc(manager->data, sizeof(SceneData) * sceneCapacity);
    if (!manager->scenes)
    {
        ERROR("Failed to allocate memory for SceneData array");
        free(manager);
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
        free(manager);
        return NULL;
    }

    return manager;
}

void destroySceneManager(SceneManager *manager)
{
    if (!manager)
        return;

    arenaDestroy(manager->data);
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

    // tear down current scene's archetypes
    Scene *cur = manager->currentScene;
    for (u32 i = 0; i < cur->manager.archetypeCount; i++)
    {
        destroyArchetype(&cur->manager.archetypes[i]);
    }

    // apply the stored SceneData as the new live scene
    SceneData *sd = &manager->scenes[sceneIndex];
    cur->manager.archetypeCount = sd->archetypeCount;
    cur->manager.archetypes = sd->archetypes;

    INFO("Switched to scene %u", sceneIndex);
}
