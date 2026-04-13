#include "entitypicker.h"
#include "editor.h"
#include <druid.h>

// ID framebuffer is now part of the multi-FBO system
u32 idShader = 0;
u32 idLocation = 0;

void initIDFramebuffer()
{
    idShader = createGraphicsProgram("../res/idShader.vert", "../res/idShader.frag");
    idLocation = glGetUniformLocation(idShader, "entityID");
    // ID framebuffer is now part of the multi-FBO system at index ID_FBO_INDEX
}

u32 count = 0;

void renderIDPass()
{
    // Ensure ID framebuffer exists in multi-FBO system
    if (viewportFBs[ID_FBO_INDEX].fbo == 0)
    {
        WARN("ID framebuffer not initialized, skipping ID pass");
        return;
    }

    bindFramebuffer(&viewportFBs[ID_FBO_INDEX]);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(idShader);
    // for all entities render

    for (u32 i = 0; i < entityCount; i++)
    {
        if (!isActive[i])
            continue;
        Transform t = {positions[i], rotations[i], scales[i]};

        u32 objectID = (i + 1) | ID_TYPE_ENTITY;

        f32 r = ((objectID >> 16) & 0xFF) / 255.0f;
        f32 g = ((objectID >> 8) & 0xFF) / 255.0f;
        f32 b = (objectID & 0xFF) / 255.0f;

        glUniform3f(idLocation, r, g, b);
        
        u32 index = modelIDs[i];

        if (index == (u32)-1)
        {
            // Render a small cube marker so ALL entities are pickable, not just
            // those with models.  Camera entities get a slightly different shape.
            f32 sx = 0.2f, sy = 0.2f, sz = 0.2f;
            if (sceneCameraFlags && sceneCameraFlags[i])
            {
                sx = 0.2f; sy = 0.2f; sz = 0.35f;
            }
            Transform marker = {positions[i], rotations[i], {sx, sy, sz}};
            updateShaderModel(idShader, marker);
            drawMeshIDPass(cubeMesh);
            continue;
        }
        if (index < resources->modelUsed)
        {
            Model *model = (&resources->modelBuffer[index]);
            if (model)
            {
                updateShaderModel(idShader, t);  // Only set model matrix

                for (u32 j = 0; j < model->meshCount; j++)
                {
                    u32 meshIndex = model->meshIndices[j];
                    
                    // Check mesh index bounds
                    if (meshIndex >= resources->meshUsed)
                    {
                        ERROR("Invalid mesh index: mesh=%d/%d", meshIndex, resources->meshUsed);
                        continue;
                    }
                    
                    Mesh *mesh = &resources->meshBuffer[meshIndex];
                    if (mesh && mesh->vao != 0)
                    {
                        drawMeshIDPass(mesh);
                    }
                    else
                    {
                        ERROR("Invalid mesh at index %d (vao=%d)", meshIndex, mesh ? mesh->vao : 0);
                    }
                }  
            }
            else
            {
                ERROR("Model index %d out of bounds (modelUsed=%d)", index, resources->modelUsed);
            }
        }
    }

    // Render skybox to ID pass
    if (skyboxMesh)
    {
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_FALSE);
        glUseProgram(idShader);
        
        u32 skyboxID = ID_TYPE_SKYBOX;
        f32 r = ((skyboxID >> 16) & 0xFF) / 255.0f;
        f32 g = ((skyboxID >> 8) & 0xFF) / 255.0f;
        f32 b = (skyboxID & 0xFF) / 255.0f;
        glUniform3f(idLocation, r, g, b);
        
        Transform identity = {0};
        updateShaderModel(idShader, identity);
        glBindVertexArray(skyboxMesh->vao);
        glDrawArrays(GL_TRIANGLES, 0, 36);
        glBindVertexArray(0);
        
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
    }

    if (manipulateTransform)
    {
        Vec3 pos = positions[inspectorEntityID];

        // Match the visual gizmo distance-based scaling, but much larger for easier picking
        f32 dist = v3Mag(v3Sub(pos, sceneCam.pos));
        if (dist < 1.0f) dist = 1.0f;
        f32 gizmoScale = dist * 0.12f;

        const f32 scaleSize   = 0.18f * gizmoScale;   // 3x visual thickness for reliable clicking
        const f32 scaleLength = 1.2f  * gizmoScale;
        const f32 offset      = 0.5f  * gizmoScale;

        Vec3 offX = {offset, 0.0f,    0.0f};
        Vec3 offY = {0.0f,   offset,  0.0f};
        Vec3 offZ = {0.0f,   0.0f,   -offset};
        Transform X = {v3Add(pos, offX), quatIdentity(), {scaleLength, scaleSize, scaleSize}};
        Transform Y = {v3Add(pos, offY), quatIdentity(), {scaleSize, scaleLength, scaleSize}};
        Transform Z = {v3Add(pos, offZ), quatIdentity(), {scaleSize, scaleSize, scaleLength}};

        // Disable depth test so gizmos always win over entity meshes in the ID buffer
        // Disable face culling so all faces of the stretched cube are drawn
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);

        // Explicitly rebind ID shader to prevent stale program state
        glUseProgram(idShader);

        updateShaderModel(idShader, X);
        glUniform3f(idLocation, 1.0f / 255.0f, 0.0f, 0.0f);
        drawMeshIDPass(cubeMesh);

        updateShaderModel(idShader, Y);
        glUniform3f(idLocation, 2.0f / 255.0f, 0.0f, 0.0f);
        drawMeshIDPass(cubeMesh);

        updateShaderModel(idShader, Z);
        glUniform3f(idLocation, 3.0f / 255.0f, 0.0f, 0.0f);
        drawMeshIDPass(cubeMesh);

        glEnable(GL_CULL_FACE);
        glEnable(GL_DEPTH_TEST);
    }

    unbindFramebuffer();
}

PickResult getEntityAtMouse(ImVec2 mouse, ImVec2 viewportTopLeft)
{
    u32 relativeX = (u32)(mouse.x - viewportTopLeft.x);
    u32 relativeY = (u32)(mouse.y - viewportTopLeft.y);

    if (relativeX >= viewportWidth || relativeY >= viewportHeight)
    {
        PickResult result;
        result.type = PICK_NONE;
        return result;
    }

    // flip Y for OpenGL (origin is bottom-left)
    u32 flippedY = (viewportHeight - relativeY - 1);

    u8 pixel[3];
    bindFramebuffer(&viewportFBs[ID_FBO_INDEX]);
    glReadPixels(relativeX, flippedY, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, pixel);
    unbindFramebuffer();

    u32 id = (pixel[0] << 16) | (pixel[1] << 8) | (pixel[2]);

    DEBUG("PICK: mouse(%.0f,%.0f) vp(%.0f,%.0f) rel(%u,%u) flip(%u) px(%u,%u,%u) id=0x%06X vw=%u vh=%u",
          mouse.x, mouse.y, viewportTopLeft.x, viewportTopLeft.y,
          relativeX, relativeY, flippedY, pixel[0], pixel[1], pixel[2], id,
          viewportWidth, viewportHeight);

    PickResult result;
    if (id == 0)
    {
        result.type = PICK_NONE;
        return result;
    }

    result.type = PICK_NONE;
    u32 type = id & ID_MASK_TYPE;
    u32 realID = id & ID_MASK_ENTITY;

    switch (type)
    {
    case ID_TYPE_ENTITY:
        result.type = PICK_ENTITY;
        result.entityID = realID;
        break;
    case ID_TYPE_GIZMO_X:
        result.type = PICK_GIZMO_X;
        break;
    case ID_TYPE_GIZMO_Y:

        result.type = PICK_GIZMO_Y;
        break;
    case ID_TYPE_GIZMO_Z:
        result.type = PICK_GIZMO_Z;
        break;
    case ID_TYPE_SKYBOX:
        result.type = PICK_SKYBOX;
        break;
    default:
        result.type = PICK_NONE;
        break;
    }

    return result;
}

void drawMeshIDPass(Mesh *mesh)
{
    if (!mesh)
    {
        ERROR("drawMeshIDPass: mesh is NULL");
        return;
    }

    if (mesh->vao == 0)
    {
        ERROR("drawMeshIDPass: mesh VAO is 0 (uninitialized)");
        return;
    }
    if (mesh->drawCount == 0) return;

    glBindVertexArray(mesh->vao);

    // Buffered meshes live in the shared GeometryBuffer and need
    // baseVertex / firstIndex offsets — same draw call as drawMesh().
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
        GLint eboBound = 0;
        glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &eboBound);
        if (eboBound != 0)
            glDrawElements(GL_TRIANGLES, mesh->drawCount, GL_UNSIGNED_INT, 0);
        else
            glDrawArrays(GL_TRIANGLES, 0, mesh->drawCount);
    }

    glBindVertexArray(0);
}

void destroyIDFramebuffer()
{
    // ID framebuffer is now destroyed as part of destroyMultiFBOs()
    if (idShader != 0)
    {
        freeShader(idShader);
        idShader = 0;
    }
}

void resizeIDFramebuffer(u32 width, u32 height)
{
    // ID framebuffer is now resized as part of resizeViewportFramebuffers()
    // Nothing needed here since it's handled in the multi-FBO system
}
