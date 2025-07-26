#include "entitypicker.h"
#include "editor.h"
#include "MeshMap.h"
#include "scene.h"
u32 idFBO = 0;
u32 idTexture = 0;
u32 idShader = 0;

u32 idLocation = 0;
u32 idDepthRB = 0;

void initIDFramebuffer()
{
    idShader = createGraphicsProgram("../res/idShader.vert", "../res/idShader.frag");

    glGenTextures(1, &idTexture);
    glBindTexture(GL_TEXTURE_2D, idTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, viewportWidth, viewportHeight, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glGenFramebuffers(1, &idFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, idFBO);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, idTexture, 0);

    glGenRenderbuffers(1, &idDepthRB);
    glBindRenderbuffer(GL_RENDERBUFFER, idDepthRB);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, viewportWidth, viewportHeight);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, idDepthRB);

    // Make sure to set the draw buffer
    glDrawBuffer(GL_COLOR_ATTACHMENT0);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        printf("ID Framebuffer is not complete! Status: 0x%X\n", status);
    }

    idLocation = glGetUniformLocation(idShader, "entityID");

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}


void renderIDPass()
{
    //bind the fbo 
    glBindFramebuffer(GL_FRAMEBUFFER, idFBO);
    
    //set the viewport up 
    glViewport(0, 0, viewportWidth, viewportHeight);
    //clear the screen 
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    //use the id shader
    glUseProgram(idShader);
    //for all entities render 
    /*
        -render the entity regularly
        -encode the id to rgb
        -send information to shader
        -draw
        */

    for (u32 i = 0; i < entitySizeCache; i++)
    {
        if (!isActive[i])continue;
        Transform t = { positions[i], rotations[i],scales[i] };
        updateShaderMVP(idShader,t,sceneCam);

        u32 objectID = i + 1;
        //encode the id to rgb
        //move 16 bits to right
        f32 r = ((objectID >> 16) & 0xFF) / 255.0f;
        //move 8 bits to right
        f32 g = ((objectID >> 8) & 0xFF) / 255.0f;
        //get to start 
        f32 b = (objectID & 0xFF) / 255.0f;

        //update shader
        glUniform3f(idLocation, r, g, b);
        
        char* entityMeshName = &meshNames[i * MAX_MESH_NAME_SIZE];

        if (entityMeshName[0] != '\0' && strlen(entityMeshName) > 0)
        {
            Mesh* meshToDraw = getMesh(entityMeshName);

            if (meshToDraw)
            {
                draw(meshToDraw);
            }
        }
        
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);


}

u32 getEntityAtMouse(ImVec2 mouse, ImVec2 viewportTopLeft)
{
    //convert to viewport-relative coords
    u32 relativeX = (u32)(mouse.x - viewportTopLeft.x);
    u32 relativeY = (u32)(mouse.y - viewportTopLeft.y);

    //flip Y for OpenGL (origin is bottom-left)
    u32 flippedY = viewportHeight - relativeY;

    //read from ID buffer
    unsigned char pixel[3];
    glBindFramebuffer(GL_FRAMEBUFFER, idFBO);
    glReadPixels(relativeX, flippedY, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, pixel);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    //reconstruct ID
    u32 id = (pixel[0] << 16) | (pixel[1] << 8) | (pixel[2]);
    return id;
}

