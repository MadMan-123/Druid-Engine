#include "../../../include/druid.h"
#include <assimp/cimport.h>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <math.h>
#include <time.h>

void freeMesh(Mesh *mesh)
{
    if (!mesh) return;
    mesh->drawCount = 0;
    if (!mesh->buffered)
    {
        glDeleteVertexArrays(1, &mesh->vao);
        glDeleteBuffers(1, &mesh->vbo);
        glDeleteBuffers(1, &mesh->ebo);
    }
    // buffered meshes: GPU memory is owned by GeometryBuffer — don't delete here
    dfree(mesh, sizeof(Mesh), MEM_TAG_MESH);
}
void freeMeshArray(Mesh *meshes, u32 meshCount)
{
    if (!meshes) return;

    for (u32 i = 0; i < meshCount; i++)
    {
        if (meshes[i].buffered) continue;  // GPU memory owned by GeometryBuffer
        if (meshes[i].vao != 0) glDeleteVertexArrays(1, &meshes[i].vao);
        if (meshes[i].vbo != 0) glDeleteBuffers(1, &meshes[i].vbo);
        if (meshes[i].ebo != 0) glDeleteBuffers(1, &meshes[i].ebo);
    }

    dfree(meshes, sizeof(Mesh) * meshCount, MEM_TAG_MESH);
}
void drawMesh(Mesh *mesh)
{
    if (!mesh)
    {
        ERROR("drawMesh: mesh is NULL");
        return;
    }
    if (mesh->vao == 0)
    {
        ERROR("drawMesh: mesh VAO is 0 (uninitialized)");
        return;
    }
    if (mesh->drawCount == 0) return;

    glBindVertexArray(mesh->vao);
    profileCountVAOBind();

    if (mesh->buffered)
    {
        glDrawElementsBaseVertex(
            GL_TRIANGLES,
            (GLsizei)mesh->drawCount,
            GL_UNSIGNED_INT,
            (void *)(uintptr_t)((u64)mesh->firstIndex * sizeof(u32)),
            (GLint)mesh->baseVertex);
    }
    else
    {
        if (mesh->ebo)
            glDrawElements(GL_TRIANGLES, (GLsizei)mesh->drawCount, GL_UNSIGNED_INT, 0);
        else
            glDrawArrays(GL_TRIANGLES, 0, (GLsizei)mesh->drawCount);
    }

    g_drawCalls++;
    g_triangles += mesh->drawCount / 3;
    g_vertices  += mesh->drawCount;
}

Mesh *loadMeshFromAssimp(const c8 *filename, u32 *meshCount)
{
    // Load the scene from file using Assimp
    const struct aiScene *scene = aiImportFile(
        filename, aiProcess_Triangulate | aiProcess_FlipUVs |
                      aiProcess_CalcTangentSpace | aiProcess_GenNormals);

    if (!scene)
    {
        printf("Failed to load mesh file: %s\n", aiGetErrorString());
        return NULL;
    }

    // Use the scene-based function to load the mesh
    Mesh *result = loadMeshFromAssimpScene(scene, meshCount);

    // Release the scene since we're done with it
    aiReleaseImport(scene);
    return result;
}

Mesh *loadMeshFromAssimpScene(const struct aiScene *scene, u32 *meshCount)
{
    // null check
    if (!scene)
    {
        printf("Failed to load mesh: %s\n", aiGetErrorString());
        return NULL;
    }

    // check if there are any meshes
    if (scene->mNumMeshes == 0)
    {
        printf("Mesh has no meshes\n");
        return NULL;
    }

    // set the mesh count
    *meshCount = scene->mNumMeshes;

    // calculate total vertices and indices across all meshes
    u32 totalVertices = 0;
    u32 totalIndices = 0;
    for (u32 m = 0; m < scene->mNumMeshes; m++)
    {
        struct aiMesh *aimesh = scene->mMeshes[m];
        totalVertices += aimesh->mNumVertices;
        // Since we use aiProcess_Triangulate, all faces are triangles (3 indices each)
        totalIndices += aimesh->mNumFaces * 3;
    }

    // Calculate arena size: positions + texcoords + normals + indices
    u32 arenaSize = (totalVertices * sizeof(Vec3)) +  // positions
                    (totalVertices * sizeof(Vec2)) +  // texcoords  
                    (totalVertices * sizeof(Vec3)) +  // normals
                    (totalIndices * sizeof(u32));     // indices

    // Create arena for all temporary mesh data (single allocation)
    Arena meshArena;
    if (!arenaCreate(&meshArena, arenaSize))
    {
        ERROR("Failed to create mesh loading arena (size: %u bytes)", arenaSize);
        return NULL;
    }

    // allocate array for output meshes
    Mesh *outputMesh = (Mesh *)dalloc(sizeof(Mesh) * scene->mNumMeshes, MEM_TAG_MESH);
    if (!outputMesh)
    {
        ERROR("Failed to allocate output mesh array");
        arenaDestroy(&meshArena);
        return NULL;
    }

    // create each mesh individually
    for (u32 m = 0; m < scene->mNumMeshes; m++)
    {
        struct aiMesh *aimesh = scene->mMeshes[m];

        u32 meshIndexCount = aimesh->mNumFaces * 3;

        // Allocate all vertex data from arena 
        Vertices vertices;
        vertices.ammount = aimesh->mNumVertices;
        vertices.positions = (Vec3 *)aalloc(&meshArena, sizeof(Vec3) * aimesh->mNumVertices);
        vertices.texCoords = (Vec2 *)aalloc(&meshArena, sizeof(Vec2) * aimesh->mNumVertices);
        vertices.normals = (Vec3 *)aalloc(&meshArena, sizeof(Vec3) * aimesh->mNumVertices);
        u32 *indices = (u32 *)aalloc(&meshArena, sizeof(u32) * meshIndexCount);

        // Check for allocation failures
        if (!vertices.positions || !vertices.texCoords || !vertices.normals || !indices)
        {
            ERROR("Arena allocation failed for mesh %u", m);
            dfree(outputMesh, sizeof(Mesh) * scene->mNumMeshes, MEM_TAG_MESH);
            arenaDestroy(&meshArena);
            return NULL;
        }

        // copy vertex data for this mesh
        for (u32 i = 0; i < aimesh->mNumVertices; i++)
        {
            // copy vertex position from Assimp mesh to our vertex buffer
            vertices.positions[i] =
                (Vec3){aimesh->mVertices[i].x, aimesh->mVertices[i].y,
                       aimesh->mVertices[i].z};

            // copy texture coordinates
            if (aimesh->mTextureCoords[0])
            {
                vertices.texCoords[i] =
                    (Vec2){aimesh->mTextureCoords[0][i].x,
                           1.0f - aimesh->mTextureCoords[0][i].y};
            }
            else
            {
                // if no texture coords, set to default (0,0)
                vertices.texCoords[i] = (Vec2){0.0f, 0.0f};
            }

            // copy normals
            if (aimesh->mNormals)
            {
                vertices.normals[i] =
                    (Vec3){aimesh->mNormals[i].x, aimesh->mNormals[i].y,
                           aimesh->mNormals[i].z};
            }
            else
            {
                // if no normals exist, set to default (0,0,0)
                vertices.normals[i] = (Vec3){0.0f, 0.0f, 0.0f};
            }
        }

        // we can use memcpy per face instead of nested index loops
        u32 *indexPtr = indices;
        for (u32 i = 0; i < aimesh->mNumFaces; i++)
        {
            // Direct memcpy of 3 indices (12 bytes) per face
            memcpy(indexPtr, aimesh->mFaces[i].mIndices, 3 * sizeof(u32));
            indexPtr += 3;
        }

        // create mesh for this mesh data (uploads to GPU)
        createMesh(&outputMesh[m], &vertices, aimesh->mNumVertices, indices,
                   meshIndexCount);
        
    }

    arenaDestroy(&meshArena);
    
    return outputMesh;
}

b8 createMesh(Mesh *mesh, const Vertices *vertices, u32 numVertices,
                const u32 *indices, u32 numIndices)
{

    if (!mesh || !vertices || numVertices == 0 )
    {
        ERROR("createMesh: invalid input params\n"
              "\tmesh=%p\n"
              "\tvertices=%p (positions=%p texCoords=%p normals=%p)\n"
              "\tindices=%p\n"
              "\tnumVertices=%u\n"
              "\tnumIndices=%u",
              (void *)mesh, (void *)vertices,
              vertices ? (void *)vertices->positions : NULL,
              vertices ? (void *)vertices->texCoords : NULL,
              vertices ? (void *)vertices->normals : NULL, (void *)indices,
              numVertices, numIndices);
        return false;
    }

    IndexedModel model;
    Arena indexModelArena;
    u32 arenaSize = (numVertices * sizeof(Vec3)) +  // positions
                    (numVertices * sizeof(Vec2)) +  // texcoords  
                    (numVertices * sizeof(Vec3));   // normals
    if (numIndices > 0) {
        arenaSize += (numIndices * sizeof(u32));     // indices (only if needed)
    }
    
    if (!arenaCreate(&indexModelArena, arenaSize))
    {
        ERROR("Failed to create arena for indexed model");
        return false;
    }


    model.positionsCount = numVertices;
    model.texCoordsCount = numVertices;
    model.normalsCount = numVertices;
    model.indicesCount = numIndices;

    model.positions = aalloc(&indexModelArena, sizeof(Vec3) * numVertices);
    model.texCoords = aalloc(&indexModelArena, sizeof(Vec2) * numVertices);
    model.normals = aalloc(&indexModelArena, sizeof(Vec3) * numVertices);
    model.indices = (numIndices > 0) ? aalloc(&indexModelArena, sizeof(u32) * numIndices) : NULL;

    if (!model.positions || !model.texCoords || !model.normals ||
        (numIndices > 0 && !model.indices))
    {
        ERROR("createMesh: model alloc failed");
        // Don't free arena-allocated memory manually - the arena cleanup handles it
        arenaDestroy(&indexModelArena);
        return false;
    }

    // copy vertex data
    memcpy(model.positions, vertices->positions, sizeof(Vec3) * numVertices);
    memcpy(model.texCoords, vertices->texCoords, sizeof(Vec2) * numVertices);
    memcpy(model.normals, vertices->normals, sizeof(Vec3) * numVertices);
    if (indices && numIndices > 0) {
        memcpy(model.indices, indices, sizeof(u32) * numIndices);
    }

    initMeshFromModel(mesh, model);

    // cleanup
    arenaDestroy(&indexModelArena);
    return true;
}

void initMeshFromModel(Mesh *mesh, const IndexedModel model)
{
    if (!mesh || !model.positions || !model.texCoords || !model.normals)
    {
        ERROR("Invalid mesh or model data provided to initModel");
        return;
    }

    mesh->drawCount = (model.indicesCount > 0) ? model.indicesCount : model.positionsCount;
    mesh->buffered  = false;

    const u32 stride = GEO_VERTEX_STRIDE;  // 32 bytes: Vec3+Vec2+Vec3

    // Build interleaved vertex data
    f32 *interleavedData = (f32 *)dalloc(model.positionsCount * stride, MEM_TAG_MESH);
    if (!interleavedData)
    {
        ERROR("initMeshFromModel: failed to allocate %u bytes for interleaved data",
              model.positionsCount * stride);
        return;
    }
    u32 offset = 0;
    for (u32 i = 0; i < model.positionsCount; i++)
    {
        interleavedData[offset++] = model.positions[i].x;
        interleavedData[offset++] = model.positions[i].y;
        interleavedData[offset++] = model.positions[i].z;
        interleavedData[offset++] = model.texCoords[i].x;
        interleavedData[offset++] = model.texCoords[i].y;
        interleavedData[offset++] = model.normals[i].x;
        interleavedData[offset++] = model.normals[i].y;
        interleavedData[offset++] = model.normals[i].z;
    }

    if (resources && resources->geoBuffer)
    {
        if (geometryBufferUpload(resources->geoBuffer, mesh,
                                  interleavedData, model.positionsCount,
                                  model.indices,   model.indicesCount))
        {
            dfree(interleavedData, model.positionsCount * stride, MEM_TAG_MESH);
            return;
        }
        DEBUG("initMeshFromModel: GeometryBuffer upload failed (non-indexed or full), using standalone VAO");
    }

    glGenVertexArrays(1, &mesh->vao);
    glGenBuffers(1, &mesh->vbo);
    glGenBuffers(1, &mesh->ebo);

    glBindVertexArray(mesh->vao);

    glBindBuffer(GL_ARRAY_BUFFER, mesh->vbo);
    glBufferData(GL_ARRAY_BUFFER, model.positionsCount * stride, interleavedData, GL_STATIC_DRAW);

    if (model.indicesCount > 0 && model.indices)
    {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh->ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     model.indicesCount * sizeof(u32), model.indices, GL_STATIC_DRAW);
    }

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void *)(sizeof(Vec3)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride,
                          (void *)(sizeof(Vec3) + sizeof(Vec2)));

    dfree(interleavedData, model.positionsCount * stride, MEM_TAG_MESH);
}
// Function to generate height data using compute shader
HeightMap generateHeightMap(i32 sizeX, i32 sizeZ, f32 heightScale,
                            const c8 *computeShaderPath)
{
    // Create compute shader
    u32 computeShader = createComputeProgram(computeShaderPath);
    if (computeShader == 0)
    {
        printf("Failed to create compute shader!\n");
    }
    // Create height buffer
    u32 heightBuffer;
    glGenBuffers(1, &heightBuffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, heightBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, sizeX * sizeZ * sizeof(f32), NULL,
                 GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, heightBuffer);

    srand(time(0));
    // get a random seed wit time
    u32 seed = rand();
    // Run compute shader
    glUseProgram(computeShader);
    glUniform1i(glGetUniformLocation(computeShader, "gridSize"), sizeX);
    glUniform1f(glGetUniformLocation(computeShader, "heightScale"),
                heightScale);
    glUniform1i(glGetUniformLocation(computeShader, "seed"), seed);

    i32 workGroupsX = (sizeX + 15) / 16; // Ceil(sizeX / 16)
    i32 workGroupsY = (sizeZ + 15) / 16; // Ceil(sizeZ / 16)
    glDispatchCompute(workGroupsX, workGroupsY, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    // Read back height data
    f32 *heights = (f32 *)dalloc(sizeof(f32) * (sizeX * sizeZ), MEM_TAG_MESH);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeX * sizeZ * sizeof(f32),
                       heights);

    // Clean up
    glDeleteBuffers(1, &heightBuffer);

    return (HeightMap){heights, sizeX, sizeZ};
}

u32 *createTerrainIndices(u32 cellsX, u32 cellsZ, u32 *outIndexCount)
{
    u32 verticesX = cellsX + 1;
    u32 totalIndices =
        cellsX * cellsZ * 6; // 2 triangles per cell, 3 indices per triangle

    // Allocate indices array
    u32 *indices = dalloc(sizeof(u32) * totalIndices, MEM_TAG_MESH);

    // Track index count for caller
    *outIndexCount = totalIndices;

    // Generate indices
    u32 indexIdx = 0;
    for (u32 z = 0; z < cellsZ; ++z)
    {
        for (u32 x = 0; x < cellsX; ++x)
        {
            // First triangle of the cell
            indices[indexIdx++] = z * verticesX + x;
            indices[indexIdx++] = (z + 1) * verticesX + x;
            indices[indexIdx++] = z * verticesX + x + 1;

            // Second triangle of the cell
            indices[indexIdx++] = z * verticesX + x + 1;
            indices[indexIdx++] = (z + 1) * verticesX + x;
            indices[indexIdx++] = (z + 1) * verticesX + x + 1;
        }
    }

    return indices;
}

void calculateTerrainNormals(Vertices *vertices, i32 width, i32 height)
{
    // reset all normals to zero
    for (i32 i = 0; i < width * height; i++)
    {
        vertices->normals[i] = (Vec3){0, 0, 0};
    }

    // calculate normals for each triangle, add to shared vertices
    for (i32 z = 0; z < height - 1; z++)
    {
        for (i32 x = 0; x < width - 1; x++)
        {
            // Get indices for the quad corners
            i32 topLeft = z * width + x;
            i32 topRight = topLeft + 1;
            i32 bottomLeft = (z + 1) * width + x;
            i32 bottomRight = bottomLeft + 1;

            // Get positions
            Vec3 v1 = vertices->positions[topLeft];
            Vec3 v2 = vertices->positions[topRight];
            Vec3 v3 = vertices->positions[bottomLeft];
            Vec3 v4 = vertices->positions[bottomRight];

            // Calculate triangle normals using cross product
            // First triangle (v1, v3, v2)
            Vec3 edge1 = (Vec3){v3.x - v1.x, v3.y - v1.y, v3.z - v1.z};
            Vec3 edge2 = (Vec3){v2.x - v1.x, v2.y - v1.y, v2.z - v1.z};
            Vec3 normal1 = (Vec3){edge1.y * edge2.z - edge1.z * edge2.y,
                                  edge1.z * edge2.x - edge1.x * edge2.z,
                                  edge1.x * edge2.y - edge1.y * edge2.x};

            // Add normal to shared vertices
            vertices->normals[topLeft].x += normal1.x;
            vertices->normals[topLeft].y += normal1.y;
            vertices->normals[topLeft].z += normal1.z;
            vertices->normals[topRight].x += normal1.x;
            vertices->normals[topRight].y += normal1.y;
            vertices->normals[topRight].z += normal1.z;
            vertices->normals[bottomLeft].x += normal1.x;
            vertices->normals[bottomLeft].y += normal1.y;
            vertices->normals[bottomLeft].z += normal1.z;

            // Second triangle (v2, v3, v4)
            edge1 = (Vec3){v3.x - v2.x, v3.y - v2.y, v3.z - v2.z};
            edge2 = (Vec3){v4.x - v2.x, v4.y - v2.y, v4.z - v2.z};
            Vec3 normal2 = (Vec3){edge1.y * edge2.z - edge1.z * edge2.y,
                                  edge1.z * edge2.x - edge1.x * edge2.z,
                                  edge1.x * edge2.y - edge1.y * edge2.x};

            // Add normal to shared vertices
            vertices->normals[topRight].x += normal2.x;
            vertices->normals[topRight].y += normal2.y;
            vertices->normals[topRight].z += normal2.z;
            vertices->normals[bottomLeft].x += normal2.x;
            vertices->normals[bottomLeft].y += normal2.y;
            vertices->normals[bottomLeft].z += normal2.z;
            vertices->normals[bottomRight].x += normal2.x;
            vertices->normals[bottomRight].y += normal2.y;
            vertices->normals[bottomRight].z += normal2.z;
        }
    }

    // Normalize all normals
    for (i32 i = 0; i < width * height; i++)
    {
        f32 length = sqrtf(vertices->normals[i].x * vertices->normals[i].x +
                           vertices->normals[i].y * vertices->normals[i].y +
                           vertices->normals[i].z * vertices->normals[i].z);
        if (length > 0.0001f)
        {
            vertices->normals[i].x /= length;
            vertices->normals[i].y /= length;
            vertices->normals[i].z /= length;
        }
        else
        {
            vertices->normals[i] =
                (Vec3){0, 1, 0}; // Default to up if degenerate
        }
    }
}

Mesh *createTerrainMeshWithHeight(u32 cellsX, u32 cellsZ, f32 cellSize,
                                  f32 heightScale,
                                  const c8 *computeShaderPath,
                                  HeightMap *output)
{
    // First generate height data
    HeightMap heightData = generateHeightMap(cellsX + 1, cellsZ + 1,
                                             heightScale, computeShaderPath);

    // copuy all the data from the heightmap to the output
    if (output != NULL)
    {
        // setup heights array
        output->heights =
            (f32 *)dalloc(sizeof(f32) * heightData.width * heightData.height, MEM_TAG_MESH);
        // copy the height data to the output
        memcpy(output->heights, heightData.heights,
               sizeof(f32) * heightData.width * heightData.height);
        output->width = heightData.width;
        output->height = heightData.height;
    }

    // Generate vertices with height
    Vertices *terrainVertices = (Vertices *)dalloc(sizeof(Vertices), MEM_TAG_MESH);
    terrainVertices->ammount = heightData.width * heightData.height;
    terrainVertices->positions =
        (Vec3 *)dalloc(terrainVertices->ammount * sizeof(Vec3), MEM_TAG_MESH);
    terrainVertices->texCoords =
        (Vec2 *)dalloc(terrainVertices->ammount * sizeof(Vec2), MEM_TAG_MESH);
    terrainVertices->normals =
        (Vec3 *)dalloc(terrainVertices->ammount * sizeof(Vec3), MEM_TAG_MESH);

    // Generate vertex data with heights
    for (u32 z = 0; z < heightData.height; ++z)
    {
        for (u32 x = 0; x < heightData.width; ++x)
        {
            u32 idx = z * heightData.width + x;
            f32 height = heightData.heights[idx];

            terrainVertices->positions[idx] = (Vec3){
                x * cellSize, // X position
                height,       // Height from compute shader
                z * cellSize  // Z position
            };
            // Normalize to 0.0-1.0 range
            f32 textureScale = 0.1f; // 1/10 = 0.1
            terrainVertices->texCoords[idx] = (Vec2){
                x * cellSize * textureScale, z * cellSize * textureScale};
        }
    }

    calculateTerrainNormals(terrainVertices, heightData.width,
                            heightData.height);
    // Generate indices (same as before)
    u32 indexCount;
    u32 *terrainIndices = createTerrainIndices(cellsX, cellsZ, &indexCount);

    // Create mesh
    Mesh *terrainMesh = dalloc(sizeof(Mesh), MEM_TAG_MESH);
    createMesh(terrainMesh, terrainVertices, terrainVertices->ammount,
               terrainIndices, indexCount);

    // Clean up
    dfree(heightData.heights, sizeof(f32) * heightData.width * heightData.height, MEM_TAG_MESH);
    dfree(terrainVertices->positions, terrainVertices->ammount * sizeof(Vec3), MEM_TAG_MESH);
    dfree(terrainVertices->texCoords, terrainVertices->ammount * sizeof(Vec2), MEM_TAG_MESH);
    dfree(terrainVertices->normals, terrainVertices->ammount * sizeof(Vec3), MEM_TAG_MESH);
    dfree(terrainVertices, sizeof(Vertices), MEM_TAG_MESH);

    return terrainMesh;
}

Vertices *createTerrainVertices(u32 cellsX, u32 cellsZ, f32 cellSize)
{
    // Calculate total number of vertices
    u32 verticesX = cellsX + 1;
    u32 verticesZ = cellsZ + 1;
    u32 totalVertices = verticesX * verticesZ;

    Vertices *vertices = dalloc(sizeof(Vertices), MEM_TAG_MESH);
    vertices->ammount = totalVertices;

    vertices->positions = dalloc(vertices->ammount * sizeof(Vec3), MEM_TAG_MESH);
    vertices->texCoords = dalloc(vertices->ammount * sizeof(Vec2), MEM_TAG_MESH);
    vertices->normals = dalloc(vertices->ammount * sizeof(Vec3), MEM_TAG_MESH);

    // Generate vertex data
    for (u32 z = 0; z < verticesZ; ++z)
    {
        for (u32 x = 0; x < verticesX; ++x)
        {
            u32 idx = z * verticesX + x;

            // Position calculation
            vertices->positions[idx] = (Vec3){
                x * cellSize, // X position
                0.0f,         // Initial flat height
                z * cellSize  // Z position
            };

            // Texture coordinate mapping
            vertices->texCoords[idx] = (Vec2){
                (f32)(x) / cellsX, // U coordinate
                (f32)(z) / cellsZ  // V coordinate
            };

            // Normal vector (upward for flat terrain)
            vertices->normals[idx] = v3Up;
        }
    }

    return vertices;
}

// Create terrain mesh using the existing createMesh function
Mesh *createTerrainMesh(u32 cellsX, u32 cellsZ, f32 cellSize)
{
    // Generate vertices
    Vertices *terrainVertices = createTerrainVertices(cellsX, cellsZ, cellSize);

    // Generate indices
    u32 indexCount;
    u32 *terrainIndices = createTerrainIndices(cellsX, cellsZ, &indexCount);

    // Create mesh using the provided createMesh function
    Mesh *terrainMesh = dalloc(sizeof(Mesh), MEM_TAG_MESH);
    createMesh(terrainMesh, terrainVertices, terrainVertices->ammount,
               terrainIndices, indexCount);

    // Clean up temporary arrays (createMesh should have made copies)
    dfree(terrainVertices->positions, terrainVertices->ammount * sizeof(Vec3), MEM_TAG_MESH);
    dfree(terrainVertices->texCoords, terrainVertices->ammount * sizeof(Vec2), MEM_TAG_MESH);
    dfree(terrainVertices->normals, terrainVertices->ammount * sizeof(Vec3), MEM_TAG_MESH);
    dfree(terrainVertices, sizeof(Vertices), MEM_TAG_MESH);
    dfree(terrainIndices, sizeof(u32) * indexCount, MEM_TAG_MESH);

    return terrainMesh;
}

Mesh *createBoxMesh()
{
    // Define vertices for a unit cube centered at origin
    // Each face needs its own vertices since texture coordinates are different
    Vertices *boxVertices = (Vertices *)dalloc(sizeof(Vertices), MEM_TAG_MESH);
    boxVertices->ammount = 24; // 4 vertices per face * 6 faces

    boxVertices->positions =
        (Vec3 *)dalloc(boxVertices->ammount * sizeof(Vec3), MEM_TAG_MESH);
    boxVertices->texCoords =
        (Vec2 *)dalloc(boxVertices->ammount * sizeof(Vec2), MEM_TAG_MESH);
    boxVertices->normals = (Vec3 *)dalloc(boxVertices->ammount * sizeof(Vec3), MEM_TAG_MESH );

    // Fill in vertex data for each face
    // FRONT FACE (Z+)
    boxVertices->positions[0] = (Vec3){-1.0f, 1.0f, 1.0f};
    boxVertices->positions[1] = (Vec3){-1.0f, -1.0f, 1.0f};
    boxVertices->positions[2] = (Vec3){1.0f, -1.0f, 1.0f};
    boxVertices->positions[3] = (Vec3){1.0f, 1.0f, 1.0f};

    // BACK FACE (Z-)
    boxVertices->positions[4] = (Vec3){1.0f, 1.0f, -1.0f};
    boxVertices->positions[5] = (Vec3){1.0f, -1.0f, -1.0f};
    boxVertices->positions[6] = (Vec3){-1.0f, -1.0f, -1.0f};
    boxVertices->positions[7] = (Vec3){-1.0f, 1.0f, -1.0f};

    // LEFT FACE (X-)
    boxVertices->positions[8] = (Vec3){-1.0f, 1.0f, -1.0f};
    boxVertices->positions[9] = (Vec3){-1.0f, -1.0f, -1.0f};
    boxVertices->positions[10] = (Vec3){-1.0f, -1.0f, 1.0f};
    boxVertices->positions[11] = (Vec3){-1.0f, 1.0f, 1.0f};

    // RIGHT FACE (X+)
    boxVertices->positions[12] = (Vec3){1.0f, 1.0f, 1.0f};
    boxVertices->positions[13] = (Vec3){1.0f, -1.0f, 1.0f};
    boxVertices->positions[14] = (Vec3){1.0f, -1.0f, -1.0f};
    boxVertices->positions[15] = (Vec3){1.0f, 1.0f, -1.0f};

    // TOP FACE (Y+)
    boxVertices->positions[16] = (Vec3){-1.0f, 1.0f, -1.0f};
    boxVertices->positions[17] = (Vec3){-1.0f, 1.0f, 1.0f};
    boxVertices->positions[18] = (Vec3){1.0f, 1.0f, 1.0f};
    boxVertices->positions[19] = (Vec3){1.0f, 1.0f, -1.0f};

    // BOTTOM FACE (Y-)
    boxVertices->positions[20] = (Vec3){-1.0f, -1.0f, 1.0f};
    boxVertices->positions[21] = (Vec3){-1.0f, -1.0f, -1.0f};
    boxVertices->positions[22] = (Vec3){1.0f, -1.0f, -1.0f};
    boxVertices->positions[23] = (Vec3){1.0f, -1.0f, 1.0f};

    // Per-face UVs (0..1) and hard normals for proper texturing + flat lighting
    // Front (Z+)
    boxVertices->texCoords[0] = (Vec2){0.0f, 1.0f};
    boxVertices->texCoords[1] = (Vec2){0.0f, 0.0f};
    boxVertices->texCoords[2] = (Vec2){1.0f, 0.0f};
    boxVertices->texCoords[3] = (Vec2){1.0f, 1.0f};
    for (u32 i = 0; i < 4; i++) boxVertices->normals[i] = (Vec3){0.0f, 0.0f, 1.0f};

    // Back (Z-)
    boxVertices->texCoords[4] = (Vec2){0.0f, 1.0f};
    boxVertices->texCoords[5] = (Vec2){0.0f, 0.0f};
    boxVertices->texCoords[6] = (Vec2){1.0f, 0.0f};
    boxVertices->texCoords[7] = (Vec2){1.0f, 1.0f};
    for (u32 i = 4; i < 8; i++) boxVertices->normals[i] = (Vec3){0.0f, 0.0f, -1.0f};

    // Left (X-)
    boxVertices->texCoords[8]  = (Vec2){0.0f, 1.0f};
    boxVertices->texCoords[9]  = (Vec2){0.0f, 0.0f};
    boxVertices->texCoords[10] = (Vec2){1.0f, 0.0f};
    boxVertices->texCoords[11] = (Vec2){1.0f, 1.0f};
    for (u32 i = 8; i < 12; i++) boxVertices->normals[i] = (Vec3){-1.0f, 0.0f, 0.0f};

    // Right (X+)
    boxVertices->texCoords[12] = (Vec2){0.0f, 1.0f};
    boxVertices->texCoords[13] = (Vec2){0.0f, 0.0f};
    boxVertices->texCoords[14] = (Vec2){1.0f, 0.0f};
    boxVertices->texCoords[15] = (Vec2){1.0f, 1.0f};
    for (u32 i = 12; i < 16; i++) boxVertices->normals[i] = (Vec3){1.0f, 0.0f, 0.0f};

    // Top (Y+)
    boxVertices->texCoords[16] = (Vec2){0.0f, 1.0f};
    boxVertices->texCoords[17] = (Vec2){0.0f, 0.0f};
    boxVertices->texCoords[18] = (Vec2){1.0f, 0.0f};
    boxVertices->texCoords[19] = (Vec2){1.0f, 1.0f};
    for (u32 i = 16; i < 20; i++) boxVertices->normals[i] = (Vec3){0.0f, 1.0f, 0.0f};

    // Bottom (Y-)
    boxVertices->texCoords[20] = (Vec2){0.0f, 1.0f};
    boxVertices->texCoords[21] = (Vec2){0.0f, 0.0f};
    boxVertices->texCoords[22] = (Vec2){1.0f, 0.0f};
    boxVertices->texCoords[23] = (Vec2){1.0f, 1.0f};
    for (u32 i = 20; i < 24; i++) boxVertices->normals[i] = (Vec3){0.0f, -1.0f, 0.0f};

    // Define indices for the cube
    u32 indices[] = {// Front face
                     0, 1, 2, 0, 2, 3,
                     // Back face
                     4, 5, 6, 4, 6, 7,
                     // Left face
                     8, 9, 10, 8, 10, 11,
                     // Right face
                     12, 13, 14, 12, 14, 15,
                     // Top face
                     16, 17, 18, 16, 18, 19,
                     // Bottom face
                     20, 21, 22, 20, 22, 23};

    // Create the mesh
    Mesh *boxMesh = dalloc(sizeof(Mesh), MEM_TAG_MESH);
    createMesh(boxMesh, boxVertices, boxVertices->ammount, indices, 36);

    // Clean up
    dfree(boxVertices->positions, boxVertices->ammount * sizeof(Vec3), MEM_TAG_MESH);
    dfree(boxVertices->texCoords, boxVertices->ammount * sizeof(Vec2), MEM_TAG_MESH);
    dfree(boxVertices->normals, boxVertices->ammount * sizeof(Vec3), MEM_TAG_MESH);
    dfree(boxVertices, sizeof(Vertices), MEM_TAG_MESH);

    return boxMesh;
}
Mesh *createSkyboxMesh()
{
    // Define vertices for a skybox
    Vertices *skyboxVertices = (Vertices *)dalloc(sizeof(Vertices), MEM_TAG_MESH);
    if (!skyboxVertices)
        return NULL;

    skyboxVertices->ammount = 36;

    // Allocate and initialize positions
    skyboxVertices->positions = (Vec3 *)dalloc(36 * sizeof(Vec3), MEM_TAG_MESH);
    if (!skyboxVertices->positions)
    {
        dfree(skyboxVertices, sizeof(Vertices), MEM_TAG_MESH);
        return NULL;
    }

    // Allocate empty (but valid) arrays for texCoords and normals
    skyboxVertices->texCoords = (Vec2 *)dalloc(36 * sizeof(Vec2), MEM_TAG_MESH);
    skyboxVertices->normals = (Vec3 *)dalloc(36 * sizeof(Vec3), MEM_TAG_MESH);
    if (!skyboxVertices->texCoords || !skyboxVertices->normals)
    {
        dfree(skyboxVertices->positions, 36 * sizeof(Vec3), MEM_TAG_MESH);
        dfree(skyboxVertices->texCoords, 36 * sizeof(Vec2), MEM_TAG_MESH);
        dfree(skyboxVertices->normals, 36 * sizeof(Vec3), MEM_TAG_MESH);
        dfree(skyboxVertices, sizeof(Vertices), MEM_TAG_MESH);
        return NULL;
    }

    // Initialize with default values
    memset(skyboxVertices->texCoords, 0, 36 * sizeof(Vec2));
    memset(skyboxVertices->normals, 0, 36 * sizeof(Vec3));

    // Fill vertex positions
    // Back face (Z-)
    skyboxVertices->positions[0] = (Vec3){-1.0f, 1.0f, -1.0f};
    skyboxVertices->positions[1] = (Vec3){-1.0f, -1.0f, -1.0f};
    skyboxVertices->positions[2] = (Vec3){1.0f, -1.0f, -1.0f};
    skyboxVertices->positions[3] = (Vec3){1.0f, -1.0f, -1.0f};
    skyboxVertices->positions[4] = (Vec3){1.0f, 1.0f, -1.0f};
    skyboxVertices->positions[5] = (Vec3){-1.0f, 1.0f, -1.0f};

    // Left face (X-)
    skyboxVertices->positions[6] = (Vec3){-1.0f, -1.0f, 1.0f};
    skyboxVertices->positions[7] = (Vec3){-1.0f, -1.0f, -1.0f};
    skyboxVertices->positions[8] = (Vec3){-1.0f, 1.0f, -1.0f};
    skyboxVertices->positions[9] = (Vec3){-1.0f, 1.0f, -1.0f};
    skyboxVertices->positions[10] = (Vec3){-1.0f, 1.0f, 1.0f};
    skyboxVertices->positions[11] = (Vec3){-1.0f, -1.0f, 1.0f};

    // Right face (X+)
    skyboxVertices->positions[12] = (Vec3){1.0f, -1.0f, -1.0f};
    skyboxVertices->positions[13] = (Vec3){1.0f, -1.0f, 1.0f};
    skyboxVertices->positions[14] = (Vec3){1.0f, 1.0f, 1.0f};
    skyboxVertices->positions[15] = (Vec3){1.0f, 1.0f, 1.0f};
    skyboxVertices->positions[16] = (Vec3){1.0f, 1.0f, -1.0f};
    skyboxVertices->positions[17] = (Vec3){1.0f, -1.0f, -1.0f};

    // Front face (Z+)
    skyboxVertices->positions[18] = (Vec3){-1.0f, -1.0f, 1.0f};
    skyboxVertices->positions[19] = (Vec3){-1.0f, 1.0f, 1.0f};
    skyboxVertices->positions[20] = (Vec3){1.0f, 1.0f, 1.0f};
    skyboxVertices->positions[21] = (Vec3){1.0f, 1.0f, 1.0f};
    skyboxVertices->positions[22] = (Vec3){1.0f, -1.0f, 1.0f};
    skyboxVertices->positions[23] = (Vec3){-1.0f, -1.0f, 1.0f};

    // Top face (Y+)
    skyboxVertices->positions[24] = (Vec3){-1.0f, 1.0f, -1.0f};
    skyboxVertices->positions[25] = (Vec3){1.0f, 1.0f, -1.0f};
    skyboxVertices->positions[26] = (Vec3){1.0f, 1.0f, 1.0f};
    skyboxVertices->positions[27] = (Vec3){1.0f, 1.0f, 1.0f};
    skyboxVertices->positions[28] = (Vec3){-1.0f, 1.0f, 1.0f};
    skyboxVertices->positions[29] = (Vec3){-1.0f, 1.0f, -1.0f};

    // Bottom face (Y-)
    skyboxVertices->positions[30] = (Vec3){-1.0f, -1.0f, -1.0f};
    skyboxVertices->positions[31] = (Vec3){-1.0f, -1.0f, 1.0f};
    skyboxVertices->positions[32] = (Vec3){1.0f, -1.0f, -1.0f};
    skyboxVertices->positions[33] = (Vec3){1.0f, -1.0f, -1.0f};
    skyboxVertices->positions[34] = (Vec3){-1.0f, -1.0f, 1.0f};
    skyboxVertices->positions[35] = (Vec3){1.0f, -1.0f, 1.0f};

    // Create the mesh
    Mesh *skyboxMesh = (Mesh *)dalloc(sizeof(Mesh), MEM_TAG_MESH);
    createMesh(skyboxMesh, skyboxVertices, 36, NULL, 0);

    // Clean up
    dfree(skyboxVertices->positions, 36 * sizeof(Vec3), MEM_TAG_MESH);
    dfree(skyboxVertices->texCoords, 36 * sizeof(Vec2), MEM_TAG_MESH);
    dfree(skyboxVertices->normals, 36 * sizeof(Vec3), MEM_TAG_MESH);
    dfree(skyboxVertices, sizeof(Vertices), MEM_TAG_MESH);

    return skyboxMesh;
}

Mesh *createQuadMesh()
{
    // Create vertices for fullscreen quad
    Vertices *quadVertices = (Vertices *)dalloc(sizeof(Vertices), MEM_TAG_MESH);
    if (!quadVertices) return NULL;
    
    quadVertices->ammount = 6;
    quadVertices->positions = (Vec3 *)dalloc(6 * sizeof(Vec3), MEM_TAG_MESH);
    quadVertices->texCoords = (Vec2 *)dalloc(6 * sizeof(Vec2), MEM_TAG_MESH);
    quadVertices->normals = (Vec3 *)dalloc(6 * sizeof(Vec3), MEM_TAG_MESH);

    if (!quadVertices->positions || !quadVertices->texCoords || !quadVertices->normals)
    {
        if (quadVertices->positions) dfree(quadVertices->positions, 6 * sizeof(Vec3), MEM_TAG_MESH);
        if (quadVertices->texCoords) dfree(quadVertices->texCoords, 6 * sizeof(Vec2), MEM_TAG_MESH);
        if (quadVertices->normals) dfree(quadVertices->normals, 6 * sizeof(Vec3), MEM_TAG_MESH);
        dfree(quadVertices, sizeof(Vertices), MEM_TAG_MESH);
        return NULL;
    }
    
    // Quad vertices (positions and texture coordinates)
    // Triangle 1
    quadVertices->positions[0] = (Vec3){-1.0f, 1.0f, 0.0f}; // Top-left
    quadVertices->texCoords[0] = (Vec2){0.0f, 1.0f};
    quadVertices->positions[1] = (Vec3){-1.0f, -1.0f, 0.0f}; // Bottom-left
    quadVertices->texCoords[1] = (Vec2){0.0f, 0.0f};
    quadVertices->positions[2] = (Vec3){1.0f, -1.0f, 0.0f}; // Bottom-right
    quadVertices->texCoords[2] = (Vec2){1.0f, 0.0f};
    
    // Triangle 2
    quadVertices->positions[3] = (Vec3){-1.0f, 1.0f, 0.0f}; // Top-left
    quadVertices->texCoords[3] = (Vec2){0.0f, 1.0f};
    quadVertices->positions[4] = (Vec3){1.0f, -1.0f, 0.0f}; // Bottom-right
    quadVertices->texCoords[4] = (Vec2){1.0f, 0.0f};
    quadVertices->positions[5] = (Vec3){1.0f, 1.0f, 0.0f}; // Top-right
    quadVertices->texCoords[5] = (Vec2){1.0f, 1.0f};
    
    // Set dummy normals (not used for screen quad)
    for (i32 i = 0; i < 6; i++)
    {
        quadVertices->normals[i] = (Vec3){0.0f, 0.0f, 1.0f};
    }
    
    // Create the mesh
    Mesh *quadMesh = (Mesh *)dalloc(sizeof(Mesh), MEM_TAG_MESH);
    if (quadMesh)
    {
        createMesh(quadMesh, quadVertices, 6, NULL, 0);
    }
    
    // Clean up
    dfree(quadVertices->positions, 6 * sizeof(Vec3), MEM_TAG_MESH);
    dfree(quadVertices->texCoords, 6 * sizeof(Vec2), MEM_TAG_MESH);
    dfree(quadVertices->normals, 6 * sizeof(Vec3), MEM_TAG_MESH);
    dfree(quadVertices, sizeof(Vertices), MEM_TAG_MESH);
    
    return quadMesh;
}

Mesh *createPlaneMesh()
{
    Vertices *v = (Vertices *)dalloc(sizeof(Vertices), MEM_TAG_MESH);
    if (!v) return NULL;

    v->ammount = 4;
    v->positions = (Vec3 *)dalloc(4 * sizeof(Vec3), MEM_TAG_MESH);
    v->texCoords = (Vec2 *)dalloc(4 * sizeof(Vec2), MEM_TAG_MESH);
    v->normals   = (Vec3 *)dalloc(4 * sizeof(Vec3), MEM_TAG_MESH);

    // Unit plane on XZ, centered at origin, facing Y+
    v->positions[0] = (Vec3){-1.0f, 0.0f,  1.0f};
    v->positions[1] = (Vec3){ 1.0f, 0.0f,  1.0f};
    v->positions[2] = (Vec3){ 1.0f, 0.0f, -1.0f};
    v->positions[3] = (Vec3){-1.0f, 0.0f, -1.0f};

    v->texCoords[0] = (Vec2){0.0f, 0.0f};
    v->texCoords[1] = (Vec2){1.0f, 0.0f};
    v->texCoords[2] = (Vec2){1.0f, 1.0f};
    v->texCoords[3] = (Vec2){0.0f, 1.0f};

    for (u32 i = 0; i < 4; i++)
        v->normals[i] = (Vec3){0.0f, 1.0f, 0.0f};

    u32 indices[] = {0, 1, 2, 0, 2, 3};

    Mesh *mesh = (Mesh *)dalloc(sizeof(Mesh), MEM_TAG_MESH);
    if (mesh)
        createMesh(mesh, v, 4, indices, 6);

    dfree(v->positions, 4 * sizeof(Vec3), MEM_TAG_MESH);
    dfree(v->texCoords, 4 * sizeof(Vec2), MEM_TAG_MESH);
    dfree(v->normals, 4 * sizeof(Vec3), MEM_TAG_MESH);
    dfree(v, sizeof(Vertices), MEM_TAG_MESH);
    return mesh;
}

Mesh *createSphereMesh()
{
    // UV sphere: use higher tessellation so reflected env-map vectors
    // interpolate smoothly and do not appear blocky.
    const u32 slices = 128;
    const u32 stacks = 64;
    const u32 vertCount = (slices + 1) * (stacks + 1);
    const u32 idxCount  = slices * stacks * 6;

    Vertices *v = (Vertices *)dalloc(sizeof(Vertices), MEM_TAG_MESH);
    if (!v) return NULL;

    v->ammount   = vertCount;
    v->positions = (Vec3 *)dalloc(vertCount * sizeof(Vec3), MEM_TAG_MESH);
    v->texCoords = (Vec2 *)dalloc(vertCount * sizeof(Vec2), MEM_TAG_MESH);
    v->normals   = (Vec3 *)dalloc(vertCount * sizeof(Vec3), MEM_TAG_MESH);

    u32 vi = 0;
    for (u32 st = 0; st <= stacks; st++)
    {
        f32 phi = (f32)st / (f32)stacks * 3.14159265f;
        f32 sinP = sinf(phi);
        f32 cosP = cosf(phi);

        for (u32 sl = 0; sl <= slices; sl++)
        {
            f32 theta = (f32)sl / (f32)slices * 2.0f * 3.14159265f;
            f32 sinT = sinf(theta);
            f32 cosT = cosf(theta);

            Vec3 n = {sinP * cosT, cosP, sinP * sinT};
            v->positions[vi] = n;
            v->normals[vi]   = n;
            v->texCoords[vi] = (Vec2){(f32)sl / (f32)slices, (f32)st / (f32)stacks};
            vi++;
        }
    }

    u32 *indices = (u32 *)dalloc(idxCount * sizeof(u32), MEM_TAG_MESH);
    u32 ii = 0;
    for (u32 st = 0; st < stacks; st++)
    {
        for (u32 sl = 0; sl < slices; sl++)
        {
            u32 a = st * (slices + 1) + sl;
            u32 b = a + slices + 1;
            // Keep front faces outward (CCW) for standard back-face culling.
            indices[ii++] = a;
            indices[ii++] = a + 1;
            indices[ii++] = b;
            indices[ii++] = a + 1;
            indices[ii++] = b + 1;
            indices[ii++] = b;
        }
    }

    Mesh *mesh = (Mesh *)dalloc(sizeof(Mesh), MEM_TAG_MESH);
    if (mesh)
        createMesh(mesh, v, vertCount, indices, idxCount);

    dfree(v->positions, vertCount * sizeof(Vec3), MEM_TAG_MESH);
    dfree(v->texCoords, vertCount * sizeof(Vec2), MEM_TAG_MESH);
    dfree(v->normals, vertCount * sizeof(Vec3), MEM_TAG_MESH);
    dfree(v, sizeof(Vertices), MEM_TAG_MESH);
    dfree(indices, idxCount * sizeof(u32), MEM_TAG_MESH);
    return mesh;
}
