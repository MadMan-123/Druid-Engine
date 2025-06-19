#include "../../include/druid.h"

u32 hashOBJKey(const void* key, u32 capacity)
{
    const OBJKey* k = (const OBJKey*)key;
    u32 h = k->v;
    h = h * 31 + k->vt;
    h = h * 31 + k->vn;
    return h % (u32)capacity;
}

bool equalsOBJKey(const void* a, const void* b)
{
    const OBJKey* ka = (const OBJKey*)a;
    const OBJKey* kb = (const OBJKey*)b;
    return ka->v == kb->v && ka->vt == kb->vt && ka->vn == kb->vn;
}


void IndexedModelCalcNormals(IndexedModel* model)
{
    for (u32 i = 0; i < model->indicesCount; i += 3)
    {
        u32 i0 = model->indices[i];
        u32 i1 = model->indices[i + 1];
        u32 i2 = model->indices[i + 2];

        Vec3 v1 = v3Sub(model->positions[i1], model->positions[i0]);
        Vec3 v2 = v3Sub(model->positions[i2], model->positions[i0]);

        Vec3 normal = v3Norm(v3Cross(v1, v2));

        model->normals[i0] = v3Add(model->normals[i0], normal);
        model->normals[i1] = v3Add(model->normals[i1], normal);
        model->normals[i2] = v3Add(model->normals[i2], normal);
    }

    for (u32 i = 0; i < model->positionsCount; i++)
    {
        model->normals[i] = v3Norm(model->normals[i]);
    }
}

OBJModel* OBJModelCreate(const char* fileName)
{
    OBJModel* model = malloc(sizeof(OBJModel));
    if (!model) return 0;

    model->OBJIndicesCapacity = 100;
    model->verticesCapacity = 100;
    model->uvsCapacity = 100;
    model->normalsCapacity = 100;

    model->OBJIndices = malloc(sizeof(OBJIndex) * model->OBJIndicesCapacity);
    model->vertices = malloc(sizeof(Vec3) * model->verticesCapacity);
    model->uvs = malloc(sizeof(Vec2) * model->uvsCapacity);
    model->normals = malloc(sizeof(Vec3) * model->normalsCapacity);

    model->OBJIndicesCount = 0;
    model->verticesCount = 0;
    model->uvsCount = 0;
    model->normalsCount = 0;

    model->hasUVs = false;
    model->hasNormals = false;

    FILE* file = fopen(fileName, "r");
    if (!file)
    {
        printf("failed to open file: %s\n", fileName);
        OBJModelDestroy(model);
        return 0;
    }

    char line[1024];
    while (fgets(line, sizeof(line), file))
    {
        u32 length = strlen(line);
        if (line[length - 1] == '\n') line[length - 1] = 0;

        if (line[0] == 'v')
        {
            if (line[1] == 't')
            {
                if (model->uvsCount >= model->uvsCapacity)
                {
                    model->uvsCapacity *= 2;
                    model->uvs = realloc(model->uvs, sizeof(Vec2) * model->uvsCapacity);
                }
                model->uvs[model->uvsCount++] = OBJModelParseOBJVec2(line);
            }
            else if (line[1] == 'n')
            {
                if (model->normalsCount >= model->normalsCapacity)
                {
                    model->normalsCapacity *= 2;
                    model->normals = realloc(model->normals, sizeof(Vec3) * model->normalsCapacity);
                }
                model->normals[model->normalsCount++] = OBJModelParseOBJVec3(line);
            }
            else
            {
                if (model->verticesCount >= model->verticesCapacity)
                {
                    model->verticesCapacity *= 2;
                    model->vertices = realloc(model->vertices, sizeof(Vec3) * model->verticesCapacity);
                }
                model->vertices[model->verticesCount++] = OBJModelParseOBJVec3(line);
            }
        }
        else if (line[0] == 'f')
        {
            OBJModelCreateOBJFace(model, line);
        }
    }

    fclose(file);
    return model;
}

void OBJModelDestroy(OBJModel* model)
{
    if (!model) return;

    free(model->OBJIndices);
    free(model->vertices);
    free(model->uvs);
    free(model->normals);
    free(model);
}

IndexedModel* OBJModelToIndexedModel(OBJModel* objModel)
{
    IndexedModel* result = malloc(sizeof(IndexedModel));
    IndexedModel* normalModel = malloc(sizeof(IndexedModel));

    if (!result || !normalModel) return 0;

    u32 cap = objModel->OBJIndicesCount;

    result->positions = malloc(sizeof(Vec3) * cap);
    result->texCoords = malloc(sizeof(Vec2) * cap);
    result->normals = malloc(sizeof(Vec3) * cap);
    result->indices = malloc(sizeof(u32) * cap);
    result->positionsCount = result->texCoordsCount = result->normalsCount = result->indicesCount = 0;
    result->positionsCapacity = result->texCoordsCapacity = result->normalsCapacity = result->indicesCapacity = cap;

    normalModel->positions = malloc(sizeof(Vec3) * cap);
    normalModel->texCoords = malloc(sizeof(Vec2) * cap);
    normalModel->normals = malloc(sizeof(Vec3) * cap);
    normalModel->indices = malloc(sizeof(u32) * cap);
    normalModel->positionsCount = normalModel->texCoordsCount = normalModel->normalsCount = normalModel->indicesCount = 0;
    normalModel->positionsCapacity = normalModel->texCoordsCapacity = normalModel->normalsCapacity = normalModel->indicesCapacity = cap;

    HashMap map;
    createMap(&map, cap * 2);

    for (u32 i = 0; i < objModel->OBJIndicesCount; i++)
    {
        OBJIndex* current = &objModel->OBJIndices[i];

        Vec3 pos = objModel->vertices[current->vertexIndex];
        Vec2 tex = objModel->hasUVs ? objModel->uvs[current->uvIndex] : (Vec2){0, 0};
        Vec3 nor = objModel->hasNormals ? objModel->normals[current->normalIndex] : (Vec3){0, 0, 0};

        OBJKey key =
        {
            .v = current->vertexIndex,
            .vt = objModel->hasUVs ? current->uvIndex : 0xFFFFFFFF,
            .vn = objModel->hasNormals ? current->normalIndex : 0xFFFFFFFF
        };

        u32 index;
        if (!findInMap(&map, key, &index))
        {
            index = result->positionsCount;
            insertMap(&map, &key, index);
            result->positions[result->positionsCount++] = pos;
            result->texCoords[result->texCoordsCount++] = tex;
            result->normals[result->normalsCount++] = nor;
        }

        result->indices[result->indicesCount++] = index;

        normalModel->positions[normalModel->positionsCount++] = pos;
        normalModel->texCoords[normalModel->texCoordsCount++] = tex;
        normalModel->normals[normalModel->normalsCount++] = nor;
        normalModel->indices[normalModel->indicesCount++] = index;
    }

    destroyMap(&map);

    if (!objModel->hasNormals)
    {
        IndexedModelCalcNormals(normalModel);
        for (u32 i = 0; i < result->positionsCount; i++)
        {
            result->normals[i] = normalModel->normals[i];
        }
    }

    free(normalModel->positions);
    free(normalModel->texCoords);
    free(normalModel->normals);
    free(normalModel->indices);
    free(normalModel);

    return result;
}
