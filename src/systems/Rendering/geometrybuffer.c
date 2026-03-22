#include "../../../include/druid.h"
#include <stdlib.h>
#include <string.h>

// One global VAO+VBO+EBO for all static mesh geometry.
// Vertex layout (interleaved, GEO_VERTEX_STRIDE = 32 bytes):
//   offset  0 : Vec3 position (12 bytes)
//   offset 12 : Vec2 texCoord ( 8 bytes)
//   offset 20 : Vec3 normal   (12 bytes)

GeometryBuffer *geometryBufferCreate(u32 maxVertices, u32 maxIndices)
{
    if (maxVertices == 0 || maxIndices == 0)
    {
        ERROR("geometryBufferCreate: maxVertices and maxIndices must be > 0");
        return NULL;
    }

    GeometryBuffer *gb = (GeometryBuffer *)malloc(sizeof(GeometryBuffer));
    if (!gb) { ERROR("geometryBufferCreate: malloc failed"); return NULL; }
    memset(gb, 0, sizeof(GeometryBuffer));

    gb->maxVertices = maxVertices;
    gb->maxIndices  = maxIndices;

    GLsizeiptr vboSize = (GLsizeiptr)maxVertices * GEO_VERTEX_STRIDE;
    GLsizeiptr eboSize = (GLsizeiptr)maxIndices  * sizeof(u32);

    glGenVertexArrays(1, &gb->vao);
    glGenBuffers(1, &gb->vbo);
    glGenBuffers(1, &gb->ebo);
    glBindVertexArray(gb->vao);

    glBindBuffer(GL_ARRAY_BUFFER, gb->vbo);
    glBufferData(GL_ARRAY_BUFFER, vboSize, NULL, GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gb->ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, eboSize, NULL, GL_DYNAMIC_DRAW);

    const GLsizei stride = (GLsizei)GEO_VERTEX_STRIDE;
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void *)(sizeof(Vec3)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride, (void *)(sizeof(Vec3) + sizeof(Vec2)));

    glBindVertexArray(0);

    INFO("GeometryBuffer created (verts=%u [%.1f MB], indices=%u [%.1f MB])",
         maxVertices, (f32)vboSize / (1024.0f * 1024.0f),
         maxIndices,  (f32)eboSize / (1024.0f * 1024.0f));
    return gb;
}

void geometryBufferDestroy(GeometryBuffer *buf)
{
    if (!buf) return;
    if (buf->vao) glDeleteVertexArrays(1, &buf->vao);
    if (buf->vbo) glDeleteBuffers(1, &buf->vbo);
    if (buf->ebo) glDeleteBuffers(1, &buf->ebo);
    free(buf);
}

b8 geometryBufferUpload(GeometryBuffer *buf, Mesh *mesh,
                         const void *interleavedVertices, u32 vertexCount,
                         const u32  *indices,             u32 indexCount)
{
    if (!buf || !mesh || !interleavedVertices || vertexCount == 0)
    {
        ERROR("geometryBufferUpload: invalid arguments");
        return false;
    }

    // GeometryBuffer only supports indexed geometry (glDrawElementsBaseVertex).
    // Non-indexed meshes (skybox, quad, etc.) must use standalone VAOs.
    if (indexCount == 0 || !indices)
        return false;

    if (buf->vertexCount + vertexCount > buf->maxVertices)
    {
        WARN("geometryBufferUpload: vertex buffer full (%u/%u)",
             buf->vertexCount + vertexCount, buf->maxVertices);
        return false;
    }
    if (indexCount > 0 && buf->indexCount + indexCount > buf->maxIndices)
    {
        WARN("geometryBufferUpload: index buffer full (%u/%u)",
             buf->indexCount + indexCount, buf->maxIndices);
        return false;
    }

    GLintptr   vOffset = (GLintptr)  ((u64)buf->vertexCount * GEO_VERTEX_STRIDE);
    GLsizeiptr vSize   = (GLsizeiptr)((u64)vertexCount      * GEO_VERTEX_STRIDE);

    glBindBuffer(GL_ARRAY_BUFFER, buf->vbo);
    glBufferSubData(GL_ARRAY_BUFFER, vOffset, vSize, interleavedVertices);
    profileCountBufferUpload((u64)vSize);

    if (indexCount > 0 && indices)
    {
        GLintptr   iOffset = (GLintptr)  ((u64)buf->indexCount * sizeof(u32));
        GLsizeiptr iSize   = (GLsizeiptr)((u64)indexCount      * sizeof(u32));
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buf->ebo);
        glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, iOffset, iSize, indices);
        profileCountBufferUpload((u64)iSize);
    }

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    mesh->baseVertex = buf->vertexCount;
    mesh->firstIndex = buf->indexCount;
    mesh->drawCount  = indexCount;
    mesh->vao        = buf->vao;
    mesh->vbo        = 0;
    mesh->ebo        = 0;
    mesh->buffered   = true;

    buf->vertexCount += vertexCount;
    buf->indexCount  += indexCount;

    return true;
}
