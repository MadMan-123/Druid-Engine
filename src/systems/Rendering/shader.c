#include "../../../include/druid.h"

// Utility functions
char *loadFileText(const char *fileName)
{
    // open the file to read
    FILE *file = fopen(fileName, "r");

    // null check
    if (file == NULL)
    {
        ERROR("The File %s has not opened\n", fileName);
        return NULL;
    }

    // determine how big the file is
    fseek(file, 0, SEEK_END);
    u64 length = ftell(file);
    // go back to start
    rewind(file);

    // allocate enough memory
    char *buffer = (char *)malloc(length + 1);

    if (!buffer)
    {
        ERROR("Failed to allocate memory");
        fclose(file);
        return NULL;
    }

    // read the file to the buffer
    u32 readSize = fread(buffer, 1, length, file);

    // add null terminator
    buffer[readSize] = '\0';

    fclose(file);
    return buffer;
}

void checkShaderError(GLuint shader, GLuint flag, b8 isProgram,
                      const char *errorMessage)
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

        // error
        // TODO: Errors
        ERROR("[SHADER ERROR]: \n Message: \n%s\n Error: %s \n", errorMessage,
              error);
    }
}

u32 createShader(const char *text, u32 type)
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

    checkShaderError(shader, GL_COMPILE_STATUS, false,
                     "Error compiling shader!");

    return shader;
}

// Main shader functions
u32 createProgram(u32 shader)
{
    u32 program = glCreateProgram();
    glAttachShader(program, shader);
    return program;
}

u32 createComputeProgram(const char *computePath)
{
    char *code = loadFileText(computePath);
    if (!code)
    {
        ERROR("failed to load Compute Shader");
        return 0;
    }
    u32 shader = createShader(code, GL_COMPUTE_SHADER);
    free(code);
    if (shader == 0)
    {
        return 0;
    }

    u32 program = glCreateProgram();
    glAttachShader(program, shader);
    glLinkProgram(program);

    checkShaderError(program, GL_LINK_STATUS, true,
                     "ERROR: Compute Program linking failed");
    glValidateProgram(program);
    checkShaderError(program, GL_VALIDATE_STATUS, true,
                     "Error: Shader program not valid");

    // Clean up the shader as it's now linked to the program
    glDetachShader(program, shader);
    glDeleteShader(shader);

    return program;
}

u32 createGraphicsProgram(const char *vertPath, const char *fragPath)
{
    u32 program = glCreateProgram();

    char *vertexShaderText = loadFileText(vertPath);
    char *fragShaderText = loadFileText(fragPath);

    if (!fragShaderText || !vertexShaderText)
    {
        ERROR("Failed to load vertex or frag shader");
    }

    u32 vertexShader = createShader(vertexShaderText, GL_VERTEX_SHADER);
    u32 fragmentShader = createShader(fragShaderText, GL_FRAGMENT_SHADER);

    free(vertexShaderText);
    free(fragShaderText);

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
    checkShaderError(program, GL_LINK_STATUS, true,
                     "Error: Shader program linking failed");

    glValidateProgram(program);
    checkShaderError(program, GL_VALIDATE_STATUS, true,
                     "Error: Shader program not valid");

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

u32 createGraphicsProgramWithGeometry(const char *vertPath,
                                      const char *geomPath,
                                      const char *fragPath)
{
    u32 program = glCreateProgram();

    char *vertexShaderText = loadFileText(vertPath);
    char *geomShaderText = loadFileText(geomPath);
    char *fragShaderText = loadFileText(fragPath);

    if (!fragShaderText || !vertexShaderText || !geomShaderText)
    {
        ERROR("Failed to load vertex, geometry or frag shader");
    }

    u32 vertexShader = createShader(vertexShaderText, GL_VERTEX_SHADER);
    u32 geomShader = createShader(geomShaderText, GL_GEOMETRY_SHADER);
    u32 fragmentShader = createShader(fragShaderText, GL_FRAGMENT_SHADER);

    free(vertexShaderText);
    free(geomShaderText);
    free(fragShaderText);

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
                     "Error: Shader program linking failed");

    glValidateProgram(program);
    checkShaderError(program, GL_VALIDATE_STATUS, true,
                     "Error: Shader program not valid");

    // Clean up the shaders as they're now linked to the program
    glDetachShader(program, vertexShader);
    glDetachShader(program, geomShader);
    glDetachShader(program, fragmentShader);
    glDeleteShader(vertexShader);
    glDeleteShader(geomShader);
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

void freeShader(u32 shader)
{
    glDeleteProgram(shader); // delete the program
}

void updateShaderMVP(u32 shaderProgram, const Transform transform,
                     const Camera camera)
{
    // Legacy function - just set model matrix now
    // View/Projection are handled by UBO
    updateShaderModel(shaderProgram, transform);
}

// New optimized function that only sets model matrix
void updateShaderModel(u32 shaderProgram, const Transform transform)
{
    Mat4 model = getModel(&transform);
    u32 modelUniform = glGetUniformLocation(shaderProgram, "model");
    if (modelUniform != -1) {
        glUniformMatrix4fv(modelUniform, 1, GL_FALSE, &model.m[0][0]);
    }
}


// Set a global time uniform on the shader program if it exists
void updateShaderTime(u32 shaderProgram, f32 time)
{
    if (shaderProgram == 0)
        return;
    GLint timeLoc = glGetUniformLocation(shaderProgram, "time");
    if (timeLoc != -1)
    {
        glUniform1f(timeLoc, time);
    }
}
