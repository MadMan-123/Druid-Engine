#include "../../../include/druid.h"

// Utility functions
void checkShaderError(u32 shader, u32 flag, b8 isProgram,const c8 *errorMessage)
{
    GLint success = 0;
    GLchar error[1024] = {0};

    if (isProgram)
        glGetProgramiv(shader, flag, &success);
    else
        glGetShaderiv(shader, flag, &success);

    if (success == GL_FALSE)
    {
        if (isProgram)
            glGetProgramInfoLog(shader, sizeof(error), NULL, error);
        else
            glGetShaderInfoLog(shader, sizeof(error), NULL, error);

        ERROR("s\n Error: %s \n", errorMessage, error);
    }
}

u32 createShader(const c8 *text, u32 type)
{
    u32 shader = glCreateShader(type);

    if (shader == 0)
        ERROR("Error type creation failed %s\n", type);

    const GLchar *stringSource[1];
    stringSource[0] = text;
    GLint lengths[1];
    lengths[0] = strlen(text);

    glShaderSource(shader, 1, stringSource, lengths);
    glCompileShader(shader);

    checkShaderError(shader, GL_COMPILE_STATUS, false, "Error compiling shader!");

    return shader;
}

// Main shader functions
u32 createProgram(u32 shader)
{
    u32 program = glCreateProgram();
    glAttachShader(program, shader);
    return program;
}

u32 createComputeProgram(const c8 *computePath)
{
    FileData* fileData = loadFile(computePath);
    if (!fileData)
    {
        ERROR("Failed to load compute shader file: %s\n", computePath);
        return 0;
    }
    c8 *code = (c8 *)fileData->data;
    if (!code)
    {
        ERROR("failed to load Compute Shader");
        return 0;
    }
    u32 shader = createShader((const c8 *)code, GL_COMPUTE_SHADER);
    free(code);
    if (shader == 0)
    {
        return 0;
    }

    u32 program = glCreateProgram();
    glAttachShader(program, shader);
    glLinkProgram(program);

    checkShaderError(program, GL_LINK_STATUS, true, "Compute Program linking failed");
    glValidateProgram(program);
    checkShaderError(program, GL_VALIDATE_STATUS, true, "Shader program not valid");

    // Clean up the shader as it's now linked to the program
    glDetachShader(program, shader);
    glDeleteShader(shader);

    return program;
}

u32 createGraphicsProgram(const c8 *vertPath, const c8 *fragPath)
{
    u32 program = glCreateProgram();

    /*
    u8 *vertexShaderText = loadFile((const u8 *)vertPath);
    u8 *fragShaderText = loadFile((const u8 *)fragPath);
    */

    FileData* vertexFileData = loadFile(vertPath);
    if (!vertexFileData)
    {
        ERROR("Failed to load vertex shader: %s", vertPath);
        glDeleteProgram(program);
        return 0;
    }
    u64 vertSize = vertexFileData->size;
    c8 *vertexShaderText = (c8 *)dalloc(vertSize + 1, MEM_TAG_SHADER);
    if (vertexShaderText)
    {
        memcpy(vertexShaderText, vertexFileData->data, vertSize);
        vertexShaderText[vertSize] = '\0';
    }
    freeFileData(vertexFileData);

    FileData* fragFileData = loadFile(fragPath);
    if (!fragFileData)
    {
        ERROR("Failed to load fragment shader: %s", fragPath);
        if (vertexShaderText) dfree(vertexShaderText, vertSize + 1, MEM_TAG_SHADER);
        glDeleteProgram(program);
        return 0;
    }
    u64 fragSize = fragFileData->size;
    c8 *fragShaderText = (c8 *)dalloc(fragSize + 1, MEM_TAG_SHADER);
    if (fragShaderText)
    {
        memcpy(fragShaderText, fragFileData->data, fragSize);
        fragShaderText[fragSize] = '\0';
    }
    freeFileData(fragFileData);

    if (!fragShaderText || !vertexShaderText)
    {
        ERROR("Failed to load vertex or frag shader");
    }

    u32 vertexShader = createShader(vertexShaderText, GL_VERTEX_SHADER);
    u32 fragmentShader = createShader(fragShaderText, GL_FRAGMENT_SHADER);

    dfree(vertexShaderText, vertSize + 1, MEM_TAG_SHADER);
    dfree(fragShaderText, fragSize + 1, MEM_TAG_SHADER);

    if (vertexShader == 0 || fragmentShader == 0)
    {
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        glDeleteProgram(program);
        return 0;
    }

    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);

    glBindAttribLocation(program, 0, "position");
    glBindAttribLocation(program, 1, "texCoord");
    glBindAttribLocation(program, 2, "normal");

    glLinkProgram(program);
    checkShaderError(program, GL_LINK_STATUS, true, "Shader program linking failed");
    
    glValidateProgram(program);
    checkShaderError(program, GL_VALIDATE_STATUS, true, "Shader program not valid");

    // Clean up the shaders as they're now linked to the program
    glDetachShader(program, vertexShader);
    glDetachShader(program, fragmentShader);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    // after link/validate bind the CoreShaderData uniform block (if present)
    {
        GLuint blockIndex = glGetUniformBlockIndex(program, "CoreShaderData");
        if (blockIndex != GL_INVALID_INDEX)
        {
            const GLuint CORE_UBO_BINDING = 0;
            glUniformBlockBinding(program, blockIndex, CORE_UBO_BINDING);
        }
    }

    return program;
}


void freeUBO(u32 ubo)
{
    if (ubo == 0)
        return;
    GLuint b = (GLuint)ubo;
    glDeleteBuffers(1, &b);
}

u32 createGraphicsProgramWithGeometry(const c8 *vertPath,
                                      const c8 *geomPath,
                                      const c8 *fragPath)
{
    u32 program = glCreateProgram();


    /*
    u8 *vertexShaderText = loadFileText((const u8 *)vertPath);
    u8 *geomShaderText = loadFileText((const u8 *)geomPath);
    u8 *fragShaderText = loadFileText((const u8 *)fragPath);
    */


    FileData* vertexFileData = loadFile(vertPath);
    if (!vertexFileData)
    {
        ERROR("Failed to load vertex shader: %s", vertPath);
        glDeleteProgram(program);
        return 0;
    }
    u64 vertSize2 = vertexFileData->size;
    c8 *vertexShaderText = (c8 *)dalloc(vertSize2 + 1, MEM_TAG_SHADER);
    if (vertexShaderText)
    {
        memcpy(vertexShaderText, vertexFileData->data, vertSize2);
        vertexShaderText[vertSize2] = '\0';
    }

    FileData* geomFileData = loadFile(geomPath);
    if (!geomFileData)
    {
        ERROR("Failed to load geometry shader: %s", geomPath);
        if (vertexShaderText) dfree(vertexShaderText, vertSize2 + 1, MEM_TAG_SHADER);
        freeFileData(vertexFileData);
        glDeleteProgram(program);
        return 0;
    }
    u64 geomSize = geomFileData->size;
    c8 *geomShaderText = (c8 *)dalloc(geomSize + 1, MEM_TAG_SHADER);
    if (geomShaderText)
    {
        memcpy(geomShaderText, geomFileData->data, geomSize);
        geomShaderText[geomSize] = '\0';
    }

    FileData* fragFileData = loadFile(fragPath);
    if (!fragFileData)
    {
        ERROR("Failed to load fragment shader: %s", fragPath);
        if (vertexShaderText) dfree(vertexShaderText, vertSize2 + 1, MEM_TAG_SHADER);
        if (geomShaderText) dfree(geomShaderText, geomSize + 1, MEM_TAG_SHADER);
        freeFileData(vertexFileData);
        freeFileData(geomFileData);
        glDeleteProgram(program);
        return 0;
    }
    u64 fragSize2 = fragFileData->size;
    c8 *fragShaderText = (c8 *)dalloc(fragSize2 + 1, MEM_TAG_SHADER);
    if (fragShaderText)
    {
        memcpy(fragShaderText, fragFileData->data, fragSize2);
        fragShaderText[fragSize2] = '\0';
    }

    freeFileData(vertexFileData);
    freeFileData(geomFileData);
    freeFileData(fragFileData);

    if (!fragShaderText || !vertexShaderText || !geomShaderText)
    {
        ERROR("Failed to load vertex, geometry or frag shader");
    }

    u32 vertexShader = createShader(vertexShaderText, GL_VERTEX_SHADER);
    u32 geomShader = createShader(geomShaderText, GL_GEOMETRY_SHADER);
    u32 fragmentShader = createShader(fragShaderText, GL_FRAGMENT_SHADER);

    dfree(vertexShaderText, vertSize2 + 1, MEM_TAG_SHADER);
    dfree(geomShaderText, geomSize + 1, MEM_TAG_SHADER);
    dfree(fragShaderText, fragSize2 + 1, MEM_TAG_SHADER);

    if (vertexShader == 0 || fragmentShader == 0 || geomShader == 0)
    {
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        glDeleteShader(geomShader);
        glDeleteProgram(program);
        return 0;
    }

    glAttachShader(program, vertexShader);
    glAttachShader(program, geomShader);
    glAttachShader(program, fragmentShader);

    glBindAttribLocation(program, 0, "position");
    glBindAttribLocation(program, 1, "texCoord");
    glBindAttribLocation(program, 2, "normal");

    glLinkProgram(program);
    checkShaderError(program, GL_LINK_STATUS, true,
                     "Shader program linking failed");

    glValidateProgram(program);
    checkShaderError(program, GL_VALIDATE_STATUS, true,
                     "Shader program not valid");

    // Clean up the shaders as they're now linked to the program
    glDetachShader(program, vertexShader);
    glDetachShader(program, geomShader);
    glDetachShader(program, fragmentShader);
    glDeleteShader(vertexShader);
    glDeleteShader(geomShader);
    glDeleteShader(fragmentShader);

    // after link/validate bind the CoreShaderData uniform block (if present)
    u32 blockIndex = glGetUniformBlockIndex(program, "CoreShaderData");
    if (blockIndex != GL_INVALID_INDEX)
    {
        const u32 CORE_UBO_BINDING = 0;
        glUniformBlockBinding(program, blockIndex, CORE_UBO_BINDING);
    }

    return program;
}


void freeShader(u32 shader)
{
    glDeleteProgram(shader); // delete the program
}

//TODO: change this to only update the models to a SSBO and have the view/projection in a separate UBO that only needs to be updated when the camera moves or changes projection
void updateShaderMVP(u32 shaderProgram, const Transform transform,
                     const Camera camera)
{
    // View/Projection are handled by UBO
    updateShaderModel(shaderProgram, transform);
}

void updateShaderModel(u32 shaderProgram, const Transform transform)
{
    // Fast path: write into the global ModelSSBO and set u_modelIndex.
    // The SSBO must already be bound to slot 2 (done in rendererBeginFrame).
    if (renderer && renderer->modelSSBO)
    {
        i32 modelIndexLoc = glGetUniformLocation(shaderProgram, "u_modelIndex");
        if (modelIndexLoc != -1)
        {
            u32 slotIndex = modelSSBOWrite(renderer->modelSSBO, &transform);
            if (slotIndex != (u32)-1)
            {
                glUniform1i(modelIndexLoc, (i32)slotIndex);
                return;
            }
            // SSBO full — fall through to legacy upload below
        }
    }

    // Legacy path: upload the model matrix as a plain uniform.
    // Used when the shader has no u_modelIndex, the SSBO is unavailable,
    // or the buffer is full for this frame.
    Mat4 model = getModel(&transform);
    i32 modelUniform = glGetUniformLocation(shaderProgram, "model");
    if (modelUniform != -1)
    {
        glUniformMatrix4fv(modelUniform, 1, GL_FALSE, &model.m[0][0]);
    }
}


// Set a global time uniform on the shader program if it exists
void updateShaderTime(u32 shaderProgram, f32 time)
{
    if (shaderProgram == 0)
        return;
    i32 timeLoc = glGetUniformLocation(shaderProgram, "time");
    if (timeLoc != -1)
    {
        glUniform1f(timeLoc, time);
    }
}
