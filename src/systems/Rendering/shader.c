#include "../../../include/druid.h"
#include <iostream>
#include <fstream>

//Utility functions
std::string loadShader(const std::string& fileName)
{
    std::ifstream file;
    file.open((fileName).c_str());

    std::string output;
    std::string line;

    if (file.is_open())
    {
        while (file.good())
        {
            getline(file, line);
            output.append(line + "\n");
        }
    }
    else
    {
        std::cerr << "Unable to load shader: " << fileName << std::endl;
    }

    return output;
}

void checkShaderError(GLuint shader, GLuint flag, bool isProgram, const std::string& errorMessage)
{
    GLint success = 0;
    GLchar error[1024] = { 0 };

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

        std::cerr << errorMessage << ": '" << error << "'" << std::endl;
    }
}

u32 createShader(const std::string& text, unsigned int type)
{
    u32 shader = glCreateShader(type);

    if (shader == 0)
        std::cerr << "Error type creation failed " << type << '\n';

    const GLchar* stringSource[1];
    stringSource[0] = text.c_str();
    GLint lengths[1];
    lengths[0] = text.length();

    glShaderSource(shader, 1, stringSource, lengths);
    glCompileShader(shader);

    checkShaderError(
        shader,
        GL_COMPILE_STATUS,
        false,
        "Error compiling shader!"
    );

    return shader;
}

//Main shader functions
u32 createProgram(u32 shader)
{
    u32 program = glCreateProgram();
    glAttachShader(program, shader);
    return program;
}

u32 createComputeProgram(const std::string& computePath)
{
    std::string code = loadShader(computePath);
    u32 shader = createShader(code, GL_COMPUTE_SHADER);

    if(shader == 0)
    {
        return 0;
    }

    u32 program = glCreateProgram();
    glAttachShader(program, shader);
    glLinkProgram(program);
    
    checkShaderError(program, GL_LINK_STATUS, true, "ERROR: Compute Program linking failed");
    glValidateProgram(program);
    checkShaderError(
     program,
     GL_VALIDATE_STATUS,
     true,
     "Error: Shader program not valid");

 
    //Clean up the shader as it's now linked to the program
    glDetachShader(program, shader);
    glDeleteShader(shader);
    
    return program;
}



u32 createGraphicsProgram(const std::string& vertPath, const std::string& fragPath)
{
    u32 program = glCreateProgram();
    
    u32 vertexShader = createShader(loadShader(vertPath), GL_VERTEX_SHADER);
    u32 fragmentShader = createShader(loadShader(fragPath), GL_FRAGMENT_SHADER);

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
    checkShaderError(
        program,
        GL_LINK_STATUS,
        true,
        "Error: Shader program linking failed");

    glValidateProgram(program);
    checkShaderError(
        program,
        GL_VALIDATE_STATUS,
        true,
        "Error: Shader program not valid");

    //Clean up the shaders as they're now linked to the program
    glDetachShader(program, vertexShader);
    glDetachShader(program, fragmentShader);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return program;
}


void freeShader(u32 shader)
{
	glDeleteProgram(shader); // delete the program

}



void updateShaderMVP(u32 shaderProgram, const Transform& transform, const Camera& camera)
{
    auto model = getModel(&transform);
    auto mvp = mat4Mul(getViewProjection(&camera) , model);
    
    u32 modelUniform = glGetUniformLocation(shaderProgram, "model");
    u32 transformUniform = glGetUniformLocation(shaderProgram, "transform");
    
    glUniformMatrix4fv(modelUniform, 1, GL_FALSE, &model.m[0][0]);
    glUniformMatrix4fv(transformUniform, 1, GL_FALSE, &mvp.m[0][0]);
}

