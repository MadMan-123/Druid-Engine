#include "../../../include/druid.h"
#include <stdlib.h>
#include <string.h>

//=====================================================================================================================
// IndirectBuffer — glMultiDrawElementsIndirect support
//
// Pre-builds a command buffer with all draw commands for one frame, then dispatches
// them in a single glMultiDrawElementsIndirect call. Reduces GPU submission CPU cost
// from ~200 cycles per call × N models to ~100 cycles for the entire batch.
//
// GL indirect command struct (tightly packed, 20 bytes):
//   count:         index count per draw
//   instanceCount: typically 1 (we use baseInstance for model matrix batching)
//   firstIndex:    offset into global index buffer
//   baseVertex:    offset into global vertex buffer
//   baseInstance:  offset for gl_BaseInstance (used in shader to index SSBO)
//=====================================================================================================================

STATIC_ASSERT(sizeof(IndirectCommand) == 20, "IndirectCommand must be exactly 20 bytes");

IndirectBuffer *indirectBufferCreate(u32 maxCommands)
{
    if (maxCommands == 0) maxCommands = 256;

    IndirectBuffer *buf = (IndirectBuffer *)dalloc(sizeof(IndirectBuffer), MEM_TAG_RENDERER);
    if (!buf) return NULL;

    memset(buf, 0, sizeof(IndirectBuffer));
    buf->maxCommands = maxCommands;

    // CPU-side command staging (will be uploaded to GPU)
    buf->commands = (IndirectCommand *)dalloc(sizeof(IndirectCommand) * maxCommands, MEM_TAG_RENDERER);
    if (!buf->commands)
    {
        dfree(buf, sizeof(IndirectBuffer), MEM_TAG_RENDERER);
        return NULL;
    }

    // GPU-side indirect command buffer
    u32 bufferSize = maxCommands * sizeof(IndirectCommand);
    glGenBuffers(1, &buf->buffer);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, buf->buffer);
    glBufferData(GL_DRAW_INDIRECT_BUFFER, bufferSize, NULL, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);

    DEBUG("IndirectBuffer created (maxCommands=%u, bufferSize=%u bytes)", maxCommands, bufferSize);

    return buf;
}

void indirectBufferDestroy(IndirectBuffer *buf)
{
    if (!buf) return;

    if (buf->buffer != 0)
        glDeleteBuffers(1, &buf->buffer);

    if (buf->commands)
        dfree(buf->commands, sizeof(IndirectCommand) * buf->maxCommands, MEM_TAG_RENDERER);

    dfree(buf, sizeof(IndirectBuffer), MEM_TAG_RENDERER);
    DEBUG("IndirectBuffer destroyed");
}

void indirectBufferReset(IndirectBuffer *buf)
{
    if (!buf) return;
    buf->commandCount = 0;
}

b8 indirectBufferAddCommand(IndirectBuffer *buf, u32 modelID,
                             u32 firstIndex, u32 indexCount,
                             u32 baseVertex, u32 baseInstance)
{
    if (!buf || buf->commandCount >= buf->maxCommands) return false;

    IndirectCommand *cmd = &buf->commands[buf->commandCount];
    cmd->count = indexCount;
    cmd->instanceCount = 1;
    cmd->firstIndex = firstIndex;
    cmd->baseVertex = (i32)baseVertex;
    cmd->baseInstance = baseInstance;

    buf->commandCount++;
    return true;
}

void indirectBufferUpload(IndirectBuffer *buf)
{
    if (!buf || buf->commandCount == 0) return;

    u32 uploadSize = buf->commandCount * sizeof(IndirectCommand);
    glBindBuffer(GL_COPY_WRITE_BUFFER, buf->buffer);
    glBufferSubData(GL_COPY_WRITE_BUFFER, 0, uploadSize, buf->commands);
    glBindBuffer(GL_COPY_WRITE_BUFFER, 0);

    TRACE("IndirectBuffer uploaded (%u commands)", buf->commandCount);
}

void indirectBufferDispatch(IndirectBuffer *buf, u32 shaderProgram)
{
    if (!buf || buf->commandCount == 0 || shaderProgram == 0) return;

    glUseProgram(shaderProgram);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, buf->buffer);

    // Single multi-draw call: replaces N individual draws with 1 GPU submission
    // glMultiDrawElementsIndirect(mode, type, indirect, drawcount, stride)
    glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, (const void *)0, buf->commandCount, 0);

    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
    TRACE("IndirectBuffer dispatched (%u commands)", buf->commandCount);
}

