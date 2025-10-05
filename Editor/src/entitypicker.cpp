#include "entitypicker.h"
#include "editor.h"
#include "scene.h"

// Use the new Framebuffer API
static Framebuffer idFB = {0};
u32 idShader = 0;
u32 idLocation = 0;

void initIDFramebuffer()
{
    idShader = createGraphicsProgram("../res/idShader.vert", "../res/idShader.frag");

    u32 width = (viewportWidth > 0) ? viewportWidth : 1920;
    u32 height = (viewportHeight > 0) ? viewportHeight : 1080;

    idFB = createFramebuffer(width, height, GL_RGB8, true);

    idLocation = glGetUniformLocation(idShader, "entityID");
}

u32 count = 0;

void renderIDPass()
{
    // Ensure ID framebuffer exists
    if (idFB.fbo == 0)
    {
        WARN("ID framebuffer not initialized, skipping ID pass");
        return;
    }

    bindFramebuffer(&idFB);
    // clear the screen
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    // use the id shader
    glUseProgram(idShader);
    // for all entities render
    /*
        -render the entity regularly
        -encode the id to rgb
        -send information to shader
        -draw
        */

    for (u32 i = 0; i < entitySizeCache; i++)
    {
        if (!isActive[i])
            continue;
        Transform t = {positions[i], rotations[i], scales[i]};

        u32 objectID = (i + 1) | ID_TYPE_ENTITY;

        // encode the id to rgb
        // move 16 bits to right
        f32 r = ((objectID >> 16) & 0xFF) / 255.0f;
        // move 8 bits to right
        f32 g = ((objectID >> 8) & 0xFF) / 255.0f;
        // get to start
        f32 b = (objectID & 0xFF) / 255.0f;

        // update shader
        glUniform3f(idLocation, r, g, b);
        
        u32 index = modelIDs[i];

        if (index == (u32)-1)
        {
            // Entity has no model assigned, skip rendering
            continue;
        }
        if (index < resources->modelUsed)
        {
            Model *model = (&resources->modelBuffer[index]);
            if (model)
            {
                updateShaderMVP(idShader, t, sceneCam);

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

    if (manipulateTransform)
    {
        Vec3 pos = positions[inspectorEntityID];
        const f32 scaleSize = 0.1f;
        const f32 scaleLength = 1.1f;

        // glBindFramebuffer(GL_FRAMEBUFFER, idFBO);

        Transform X = {v3Add(pos, v3Right), quatIdentity(),
                       (Vec3){scaleLength, scaleSize, scaleSize}};
        Transform Y = {v3Add(pos, v3Up), quatIdentity(),
                       (Vec3){scaleSize, scaleLength, scaleSize}};

        Transform Z = {v3Add(pos, v3Back), quatIdentity(),
                       (Vec3){scaleSize, scaleSize, scaleLength}};

        updateShaderMVP(idShader, X, sceneCam);
        glUniform3f(idLocation, 1.0f / 255.0f, 0.0f,
                    0.0f); // ID_TYPE_GIZMO_X = 0x010000
        drawMeshIDPass(cubeMesh);

        updateShaderMVP(idShader, Y, sceneCam);
        glUniform3f(idLocation, 2.0f / 255.0f, 0.0f,
                    0.0f); // ID_TYPE_GIZMO_Y = 0x020000
        drawMeshIDPass(cubeMesh);

        updateShaderMVP(idShader, Z, sceneCam);
        glUniform3f(idLocation, 3.0f / 255.0f, 0.0f,
                    0.0f); // ID_TYPE_GIZMO_Z = 0x030000
        drawMeshIDPass(cubeMesh);
    }

    unbindFramebuffer();
}

PickResult getEntityAtMouse(ImVec2 mouse, ImVec2 viewportTopLeft)
{
    // convert to viewport-relative coords
    u32 relativeX = (u32)(mouse.x - viewportTopLeft.x);
    u32 relativeY = (u32)(mouse.y - viewportTopLeft.y);

    // flip Y for OpenGL (origin is bottom-left)
    u32 flippedY = (viewportHeight - relativeY) - 2;

    // read from ID buffer
    u8 pixel[3];
    bindFramebuffer(&idFB);
    glReadPixels(relativeX, flippedY, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, pixel);
    unbindFramebuffer();

    // reconstruct ID
    u32 id = (pixel[0] << 16) | (pixel[1] << 8) | (pixel[2]);
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
    default:
        result.type = PICK_NONE;
        break;
    }

    INFO("Decoded id: %d to %f %f %f\n", realID, (f32)pixel[0], (f32)pixel[1],
         (f32)pixel[2]);

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
    
    // bind the mesh
    glBindVertexArray(mesh->vao);
    // draw the mesh elements
    glDrawElements(GL_TRIANGLES, mesh->drawCount, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

void destroyIDFramebuffer()
{
    if (idFB.fbo != 0)
    {
        destroyFramebuffer(&idFB);
        idFB = (Framebuffer){0};
    }
    if (idShader != 0)
    {
        freeShader(idShader);
        idShader = 0;
    }
}

void resizeIDFramebuffer(u32 width, u32 height)
{
    if (idFB.fbo == 0)
        return;
    resizeFramebuffer(&idFB, width, height);
}
