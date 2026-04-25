#include "../../../include/druid.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

DAPI PrefabRegistry *prefabRegistry = NULL;

DAPI b8 prefabRegistryCreate(void)
{
    if (prefabRegistry) return true;
    prefabRegistry = (PrefabRegistry *)malloc(sizeof(PrefabRegistry));
    if (!prefabRegistry) return false;
    memset(prefabRegistry, 0, sizeof(PrefabRegistry));
    return true;
}

DAPI void prefabRegistryDestroy(void)
{
    if (!prefabRegistry) return;
    free(prefabRegistry);
    prefabRegistry = NULL;
}

// Find or create a bucket for the given archetype name. Returns NULL if full.
static PrefabBucket *getOrCreateBucket(const c8 *archName)
{
    for (u32 i = 0; i < prefabRegistry->bucketCount; i++)
        if (strcmp(prefabRegistry->buckets[i].archName, archName) == 0)
            return &prefabRegistry->buckets[i];

    if (prefabRegistry->bucketCount >= PREFAB_MAX_BUCKETS)
    {
        WARN("prefab: bucket table full — cannot register archetype '%s'", archName);
        return NULL;
    }
    PrefabBucket *b = &prefabRegistry->buckets[prefabRegistry->bucketCount++];
    memset(b, 0, sizeof(PrefabBucket));
    strncpy(b->archName, archName, MAX_SCENE_NAME - 1);
    return b;
}

DAPI PrefabBucket *prefabGetBucket(Archetype *arch)
{
    if (!prefabRegistry || !arch || !arch->layout) return NULL;
    const c8 *name = arch->layout->name;

    for (u32 i = 0; i < prefabRegistry->bucketCount; i++)
    {
        PrefabBucket *b = &prefabRegistry->buckets[i];
        if (b->archRuntime == arch) return b;
        if (strcmp(b->archName, name) == 0)
        {
            b->archRuntime = arch;
            return b;
        }
    }
    return NULL;
}

DAPI u32 prefabCount(Archetype *arch)
{
    PrefabBucket *b = prefabGetBucket(arch);
    return b ? b->prefabCount : 0;
}

static void stampPrefab(const Prefab *p, Archetype *arch, u32 localIdx, void **fields)
{
    for (u32 f = 0; f < p->fieldCount; f++)
    {
        const PrefabField *pf = &p->fields[f];
        for (u32 j = 0; j < arch->layout->count; j++)
        {
            if (strcmp(arch->layout->fields[j].name, pf->name) == 0)
            {
                if (fields[j])
                    memcpy((u8 *)fields[j] + (u64)localIdx * pf->size,
                           p->data + pf->dataOffset, pf->size);
                break;
            }
        }
    }
}

DAPI u32 prefabSpawn(Archetype *arch, u32 prefabIdx, Vec3 pos)
{
    PrefabBucket *b = prefabGetBucket(arch);
    if (!b || prefabIdx >= b->prefabCount) return (u32)-1;

    u32 poolIdx, localIdx;
    void **fields;
    if (!archetypePoolSpawnFields(arch, &poolIdx, &localIdx, &fields)) return (u32)-1;

    stampPrefab(&b->prefabs[prefabIdx], arch, localIdx, fields);

    // Apply spawn position (always overrides whatever the prefab stored)
    for (u32 j = 0; j < arch->layout->count; j++)
    {
        const c8 *fn = arch->layout->fields[j].name;
        if (!fields[j]) continue;
        if      (strcmp(fn, "PositionX") == 0) ((f32 *)fields[j])[localIdx] = pos.x;
        else if (strcmp(fn, "PositionY") == 0) ((f32 *)fields[j])[localIdx] = pos.y;
        else if (strcmp(fn, "PositionZ") == 0) ((f32 *)fields[j])[localIdx] = pos.z;
    }

    return poolIdx;
}

DAPI b8 prefabLoad(const c8 *path)
{
    if (!prefabRegistry || !path) return false;

    FILE *f = fopen(path, "rb");
    if (!f) { WARN("prefabLoad: cannot open '%s'", path); return false; }

    u32 magic, version;
    if (fread(&magic,   sizeof(u32), 1, f) != 1 || magic   != PREFAB_MAGIC ||
        fread(&version, sizeof(u32), 1, f) != 1 || version != PREFAB_VERSION)
    {
        WARN("prefabLoad: bad header in '%s'", path);
        fclose(f); return false;
    }

    c8 prefabName[MAX_SCENE_NAME] = {0};
    c8 archName[MAX_SCENE_NAME]   = {0};
    if (fread(prefabName, 1, MAX_SCENE_NAME, f) != MAX_SCENE_NAME ||
        fread(archName,   1, MAX_SCENE_NAME, f) != MAX_SCENE_NAME)
    {
        WARN("prefabLoad: truncated header in '%s'", path);
        fclose(f); return false;
    }

    u32 fieldCount;
    if (fread(&fieldCount, sizeof(u32), 1, f) != 1 || fieldCount > PREFAB_MAX_FIELDS)
    {
        WARN("prefabLoad: invalid field count in '%s'", path);
        fclose(f); return false;
    }

    PrefabBucket *bucket = getOrCreateBucket(archName);
    if (!bucket) { fclose(f); return false; }
    if (bucket->prefabCount >= PREFAB_MAX_PER_ARCH)
    {
        WARN("prefabLoad: bucket for '%s' is full (max %u), skipping '%s'",
             archName, PREFAB_MAX_PER_ARCH, prefabName);
        fclose(f); return false;
    }

    Prefab *prefab = &bucket->prefabs[bucket->prefabCount];
    memset(prefab, 0, sizeof(Prefab));
    strncpy(prefab->name, prefabName, MAX_SCENE_NAME - 1);

    u32 dataOff = 0;
    for (u32 i = 0; i < fieldCount; i++)
    {
        c8  fname[PREFAB_FIELD_NAME_MAX] = {0};
        u32 fsize;
        if (fread(fname,  1,           PREFAB_FIELD_NAME_MAX, f) != PREFAB_FIELD_NAME_MAX ||
            fread(&fsize, sizeof(u32), 1,                     f) != 1)
        {
            WARN("prefabLoad: truncated field %u in '%s'", i, path);
            fclose(f); return false;
        }
        if (dataOff + fsize > PREFAB_DATA_POOL_SIZE)
        {
            WARN("prefabLoad: field data overflow in '%s' (field %u)", path, i);
            fclose(f); return false;
        }
        if (fread(prefab->data + dataOff, 1, fsize, f) != fsize)
        {
            WARN("prefabLoad: truncated field data in '%s'", path);
            fclose(f); return false;
        }

        PrefabField *pf = &prefab->fields[i];
        strncpy(pf->name, fname, PREFAB_FIELD_NAME_MAX - 1);
        pf->size       = fsize;
        pf->dataOffset = dataOff;
        dataOff       += fsize;
    }

    prefab->fieldCount = fieldCount;
    bucket->prefabCount++;
    fclose(f);

    INFO("prefab: loaded '%s' (arch='%s', %u fields)", prefabName, archName, fieldCount);
    return true;
}

DAPI b8 prefabSave(const c8 *path, const c8 *archName, const c8 *prefabName,
                    const c8 (*fieldNames)[PREFAB_FIELD_NAME_MAX],
                    const void **fieldValues, const u32 *fieldSizes, u32 fieldCount)
{
    if (!path || !archName || !prefabName) return false;
    if (fieldCount > PREFAB_MAX_FIELDS) return false;

    FILE *f = fopen(path, "wb");
    if (!f) { ERROR("prefabSave: cannot open '%s' for writing", path); return false; }

    u32 magic   = PREFAB_MAGIC;
    u32 version = PREFAB_VERSION;
    fwrite(&magic,   sizeof(u32), 1, f);
    fwrite(&version, sizeof(u32), 1, f);

    c8 nameBuf[MAX_SCENE_NAME] = {0};
    strncpy(nameBuf, prefabName, MAX_SCENE_NAME - 1);
    fwrite(nameBuf, 1, MAX_SCENE_NAME, f);
    memset(nameBuf, 0, MAX_SCENE_NAME);
    strncpy(nameBuf, archName, MAX_SCENE_NAME - 1);
    fwrite(nameBuf, 1, MAX_SCENE_NAME, f);

    fwrite(&fieldCount, sizeof(u32), 1, f);
    for (u32 i = 0; i < fieldCount; i++)
    {
        c8 fn[PREFAB_FIELD_NAME_MAX] = {0};
        strncpy(fn, fieldNames[i], PREFAB_FIELD_NAME_MAX - 1);
        fwrite(fn,              1,           PREFAB_FIELD_NAME_MAX, f);
        fwrite(&fieldSizes[i],  sizeof(u32), 1,                     f);
        fwrite(fieldValues[i],  1,           fieldSizes[i],         f);
    }

    fclose(f);
    return true;
}

DAPI u32 prefabLoadDirectory(const c8 *dir)
{
    if (!prefabRegistry || !dir) return 0;

    u32 count = 0;
    u32 fileCount = 0;
    c8 **files = listFilesInDirectory(dir, &fileCount);
    if (!files) return 0;

    for (u32 i = 0; i < fileCount; i++)
    {
        const c8 *path = files[i];
        u32 len = (u32)strlen(path);
        if (len > 7 && strcmp(path + len - 7, ".prefab") == 0)
            if (prefabLoad(path)) count++;
        free(files[i]);
    }
    free(files);

    if (count > 0)
        INFO("prefab: loaded %u prefab(s) from '%s'", count, dir);
    return count;
}
