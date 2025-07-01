// OBJ model loader for Druid engine
// Loads and parses .obj files for mesh data
#include "../../include/druid.h"

//hash functions for objkey
u32 hashOBJKey(const void* key, u32 capacity)
{
    const OBJKey* k = (const OBJKey*)key;
    u32 h = k->v;
    h = h * 31 + k->vt;
    h = h * 31 + k->vn;
    return h % capacity;
}

bool equalsOBJKey(const void* a, const void* b)
{
    const OBJKey* ka = (const OBJKey*)a;
    const OBJKey* kb = (const OBJKey*)b;
    return ka->v == kb->v && ka->vt == kb->vt && ka->vn == kb->vn;
}

//calculate normals for indexed model
void indexedModelCalcNormals(IndexedModel* model)
{
    //initialize normals to zero
    for (u32 i = 0; i < model->positionsCount; i++)
        model->normals[i] = (Vec3){0.0f, 0.0f, 0.0f};

    for (u32 i = 0; i < model->indicesCount; i += 3)
    {
        u32 i0 = model->indices[i];
        u32 i1 = model->indices[i + 1];
        u32 i2 = model->indices[i + 2];

        //bounds checking
        if (i0 >= model->positionsCount || i1 >= model->positionsCount || i2 >= model->positionsCount)
        {
            printf("Warning: Index out of bounds in normal calculation\n");
            continue;
        }

        Vec3 v1 = v3Sub(model->positions[i1], model->positions[i0]);
        Vec3 v2 = v3Sub(model->positions[i2], model->positions[i0]);

        Vec3 normal = v3Cross(v1, v2);
        //only normalize if the cross product is not zero
        f32 length = v3Mag(normal);
        if (length > 0.0001f)
            normal = v3Norm(normal);

        model->normals[i0] = v3Add(model->normals[i0], normal);
        model->normals[i1] = v3Add(model->normals[i1], normal);
        model->normals[i2] = v3Add(model->normals[i2], normal);
    }

    for (u32 i = 0; i < model->positionsCount; i++)
    {
        f32 length = v3Mag(model->normals[i]);
        if (length > 0.0001f)
            model->normals[i] = v3Norm(model->normals[i]);
        else
            //default normal if calculation failed
            model->normals[i] = (Vec3){0.0f, 1.0f, 0.0f};
    }
}

//parse file metadata - first pass to count elements
typedef struct
{
    u32 verticesCount;
    u32 uvsCount;
    u32 normalsCount;
    u32 facesCount;
    bool hasUVs;
    bool hasNormals;
} OBJMetadata;

OBJMetadata parseOBJMetadata(const char* fileName)
{
    OBJMetadata meta = {0};
    
    FILE* file = fopen(fileName, "r");
    if (!file)
    {
        printf("failed to open file: %s\n", fileName);
        return meta;
    }

    char line[1024];
    while (fgets(line, sizeof(line), file))
    {
        if (line[0] == 'v')
        {
            if (line[1] == 't')
            {
                meta.uvsCount++;
                meta.hasUVs = true;
            }
            else if (line[1] == 'n')
            {
                meta.normalsCount++;
                meta.hasNormals = true;
            }
            else if (line[1] == ' ' || line[1] == '\t')
                meta.verticesCount++;
        }
        else if (line[0] == 'f' && (line[1] == ' ' || line[1] == '\t'))
            meta.facesCount++;
    }

    fclose(file);
    return meta;
}

//create obj model with proper allocation
OBJModel* objModelCreate(const char* fileName)
{
    //first pass: get file metadata
    OBJMetadata meta = parseOBJMetadata(fileName);
    if (meta.verticesCount == 0)
    {
        printf("Error: No vertices found in file: %s\n", fileName);
        return NULL;
    }

    OBJModel* model = malloc(sizeof(OBJModel));
    if (!model)
    {
        printf("Error: obj not allocated\n");
        return NULL;
    }

    //allocate exact sizes based on metadata
    model->objIndicesCapacity = meta.facesCount * 6; //worst case: all quads = 6 indices
    model->verticesCapacity = meta.verticesCount;
    model->uvsCapacity = meta.hasUVs ? meta.uvsCount : 1;
    model->normalsCapacity = meta.hasNormals ? meta.normalsCount : 1;

    model->objIndices = malloc(sizeof(OBJIndex) * model->objIndicesCapacity);
    model->vertices = malloc(sizeof(Vec3) * model->verticesCapacity);
    model->uvs = malloc(sizeof(Vec2) * model->uvsCapacity);
    model->normals = malloc(sizeof(Vec3) * model->normalsCapacity);

    //check all allocations
    if (!model->objIndices || !model->vertices || !model->uvs || !model->normals)
    {
        printf("Error: Failed to allocate OBJ model arrays\n");
        objModelDestroy(model);
        return NULL;
    }

    model->objIndicesCount = 0;
    model->verticesCount = 0;
    model->uvsCount = 0;
    model->normalsCount = 0;
    model->hasUVs = meta.hasUVs;
    model->hasNormals = meta.hasNormals;

    //second pass: parse the data
    FILE* file = fopen(fileName, "r");
    if (!file)
    {
        printf("failed to open file: %s\n", fileName);
        objModelDestroy(model);
        return NULL;
    }

    char line[1024];
    while (fgets(line, sizeof(line), file))
    {
        u32 length = strlen(line);
        if (length > 0 && line[length - 1] == '\n') 
            line[length - 1] = '\0';

        if (line[0] == 'v')
        {
            if (line[1] == 't')
                model->uvs[model->uvsCount++] = objModelParseVec2(line);
            else if (line[1] == 'n')
                model->normals[model->normalsCount++] = objModelParseVec3(line);
            else if (line[1] == ' ' || line[1] == '\t')
                model->vertices[model->verticesCount++] = objModelParseVec3(line);
        }
        else if (line[0] == 'f' && (line[1] == ' ' || line[1] == '\t'))
            objModelCreateFace(model, line);
    }

    fclose(file);
    return model;
}

void objModelDestroy(OBJModel* model)
{
    if (!model) return;
    free(model->objIndices);
    free(model->vertices);
    free(model->uvs);
    free(model->normals);
    free(model);
}

void objModelCreateFace(OBJModel* model, const char* line)
{
    if (line[0] != 'f') return;

    char buffer[512];
    strncpy(buffer, line, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    char* context = NULL;
    char* token = strtok_r(buffer, " \t\r\n", &context); //skip 'f'

    OBJIndex indices[4];
    u32 indexCount = 0;

    while ((token = strtok_r(NULL, " \t\r\n", &context)) && indexCount < 4)
    {
        OBJIndex idx = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};

        char* v = token;
        char* vt = strchr(v, '/');
        if (vt)
        {
            *vt = '\0';
            vt++;
            char* vn = strchr(vt, '/');
            if (vn)
            {
                *vn = '\0';
                vn++;
                if (*vn && strcmp(vn, "") != 0)
                {
                    int normalIndex = atoi(vn);
                    if (normalIndex > 0 && (u32)(normalIndex - 1) < model->normalsCount)
                        idx.normalIndex = (u32)normalIndex - 1;
                }
            }
            if (*vt && strcmp(vt, "") != 0)
            {
                int uvIndex = atoi(vt);
                if (uvIndex > 0 && (u32)(uvIndex - 1) < model->uvsCount)
                    idx.uvIndex = (u32)uvIndex - 1;
            }
        }

        if (*v && strcmp(v, "") != 0)
        {
            int vertexIndex = atoi(v);
            if (vertexIndex > 0 && (u32)(vertexIndex - 1) < model->verticesCount)
                idx.vertexIndex = (u32)vertexIndex - 1;
            else
            {
                printf("Warning: Invalid vertex index %d\n", vertexIndex);
                return; //skip this face
            }
        }

        indices[indexCount++] = idx;
    }

    //triangulate face (support triangle or quad)
    u32 faceIndices[6];
    u32 faceCount = 0;

    if (indexCount == 3)
    {
        faceIndices[0] = 0; faceIndices[1] = 1; faceIndices[2] = 2;
        faceCount = 3;
    }
    else if (indexCount == 4)
    {
        //quad -> 2 triangles: 0,1,2 and 0,2,3
        faceIndices[0] = 0; faceIndices[1] = 1; faceIndices[2] = 2;
        faceIndices[3] = 0; faceIndices[4] = 2; faceIndices[5] = 3;
        faceCount = 6;
    }
    else
        return; //ignore non-triangle/quad faces

    //check bounds
    if (model->objIndicesCount + faceCount >= model->objIndicesCapacity)
    {
        printf("Warning: Face indices capacity exceeded\n");
        return;
    }

    for (u32 i = 0; i < faceCount; ++i)
        model->objIndices[model->objIndicesCount++] = indices[faceIndices[i]];
}

IndexedModel* objModelToIndexedModel(OBJModel* objModel)
{
    if (objModel == NULL)
    {
        printf("Error: Object is null\n");
        return NULL;
    }
    
    if (objModel->objIndicesCount == 0)
    {
        printf("Error: No indices in OBJ model\n");
        return NULL;
    }
       	
    IndexedModel* result = malloc(sizeof(IndexedModel));
    if (!result)
    {
        printf("Error: Failed to allocate IndexedModel\n");
        return NULL;
    }

    //estimate capacity based on obj model size
    u32 cap = objModel->objIndicesCount;
    if (cap < 100) cap = 100;

    result->positions = malloc(sizeof(Vec3) * cap);
    result->texCoords = malloc(sizeof(Vec2) * cap);
    result->normals = malloc(sizeof(Vec3) * cap);
    result->indices = malloc(sizeof(u32) * objModel->objIndicesCount);
    
    if (!result->positions || !result->texCoords || !result->normals || !result->indices)
    {
        printf("Error: Failed to allocate IndexedModel arrays\n");
        free(result->positions);
        free(result->texCoords);
        free(result->normals);
        free(result->indices);
        free(result);
        return NULL;
    }

    result->positionsCount = result->texCoordsCount = result->normalsCount = result->indicesCount = 0;
    result->positionsCapacity = result->texCoordsCapacity = result->normalsCapacity = cap;
    result->indicesCapacity = objModel->objIndicesCount;

    //create hashmap for vertex deduplication
    HashMap indexMap;
    u32 mapCapacity = cap * 2;
    if (!createMap(&indexMap, mapCapacity, sizeof(OBJKey), sizeof(u32), hashOBJKey, equalsOBJKey))
    {
        printf("Error: Failed to create hash map\n");
        free(result->positions);
        free(result->texCoords);
        free(result->normals);
        free(result->indices);
        free(result);
        return NULL;
    }

    for (u32 i = 0; i < objModel->objIndicesCount; i++)
    {
        OBJIndex* current = &objModel->objIndices[i];

        //bounds checking
        if (current->vertexIndex >= objModel->verticesCount)
        {
            printf("Warning: Vertex index %u out of bounds (max %u)\n", 
                   current->vertexIndex, objModel->verticesCount - 1);
            continue;
        }

        Vec3 pos = objModel->vertices[current->vertexIndex];
        Vec2 tex = (Vec2){0, 0};
        Vec3 nor = (Vec3){0, 0, 0};

        if (objModel->hasUVs && current->uvIndex != 0xFFFFFFFF && current->uvIndex < objModel->uvsCount)
            tex = objModel->uvs[current->uvIndex];

        if (objModel->hasNormals && current->normalIndex != 0xFFFFFFFF && current->normalIndex < objModel->normalsCount)
            nor = objModel->normals[current->normalIndex];

        OBJKey key = {
            .v = current->vertexIndex,
            .vt = objModel->hasUVs ? current->uvIndex : 0xFFFFFFFF,
            .vn = objModel->hasNormals ? current->normalIndex : 0xFFFFFFFF
        };

        u32 index;
        if (!findInMap(&indexMap, &key, &index))
        {
            //not found, insert new vertex
            index = result->positionsCount;
            
            //expand arrays if needed
            if (result->positionsCount >= result->positionsCapacity)
            {
                result->positionsCapacity *= 2;
                result->positions = realloc(result->positions, sizeof(Vec3) * result->positionsCapacity);
                result->texCoords = realloc(result->texCoords, sizeof(Vec2) * result->positionsCapacity);
                result->normals = realloc(result->normals, sizeof(Vec3) * result->positionsCapacity);
                result->texCoordsCapacity = result->normalsCapacity = result->positionsCapacity;
            }
            
            result->positions[result->positionsCount] = pos;
            result->texCoords[result->texCoordsCount] = tex;
            result->normals[result->normalsCount] = nor;
            result->positionsCount++;
            result->texCoordsCount++;
            result->normalsCount++;

            if (!insertMap(&indexMap, &key, &index))
            {
                printf("Error: Failed to insert into hash map\n");
                destroyMap(&indexMap);
                free(result->positions);
                free(result->texCoords);
                free(result->normals);
                free(result->indices);
                free(result);
                return NULL;
            }
        }

        result->indices[result->indicesCount++] = index;
    }

    destroyMap(&indexMap);

    //calculate normals if not present
    if (!objModel->hasNormals)
        indexedModelCalcNormals(result);

    return result;
}

//parses a line like "vt 0.123 0.456"
Vec2 objModelParseVec2(const char* line)
{
    Vec2 result = {0.0f, 0.0f};
    if (sscanf(line, "%*s %f %f", &result.x, &result.y) != 2)
        printf("Warning: Failed to parse Vec2 from line: %s\n", line);
    return result;
}

//parses a line like "v 1.0 2.0 3.0" or "vn 0.0 1.0 0.0"
Vec3 objModelParseVec3(const char* line)
{
    Vec3 result = {0.0f, 0.0f, 0.0f};
    if (sscanf(line, "%*s %f %f %f", &result.x, &result.y, &result.z) != 3)
        printf("Warning: Failed to parse Vec3 from line: %s\n", line);
    return result;
}
