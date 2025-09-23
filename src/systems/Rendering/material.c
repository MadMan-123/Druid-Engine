#include <assimp/cimport.h>
#include <assimp/material.h>
#include <assimp/scene.h>

#include "../../../include/druid.h"
#include <string.h>

u32 loadMaterialTexture(struct aiMaterial *mat, enum aiTextureType type,
                        const char *base_path)
{
    if (aiGetMaterialTextureCount(mat, type) == 0)
    {
        return 0;
    }

    struct aiString texture_path;
    if (aiGetMaterialTexture(mat, type, 0, &texture_path, NULL, NULL, NULL,
                             NULL, NULL, NULL) != AI_SUCCESS)
    {
        return 0;
    }

    // strip to just the filename
    const char *fileName = texture_path.data;
    const char *lastSlash = strrchr(fileName, '/');
    if (!lastSlash)
        lastSlash = strrchr(fileName, '\\');
    if (lastSlash)
        fileName = lastSlash + 1;

    const char *tryExtensions[] = {".png",  ".psd", ".jpg",
                                   ".jpeg", ".tga", ".bmp"};
    const u32 num_exts = sizeof(tryExtensions) / sizeof(tryExtensions[0]);

    for (u32 i = 0; i < num_exts; i++)
    {
        char tryPath[512];

        // strip original extension and add new one
        char baseName[256];
        strncpy(baseName, fileName, sizeof(baseName));
        char *dot = strrchr(baseName, '.');
        if (dot)
            *dot = '\0';

        snprintf(tryPath, sizeof(tryPath), "%s/%s%s", base_path, baseName,
                 tryExtensions[i]);

        FILE *f = fopen(tryPath, "rb");
        if (f)
        {
            fclose(f);
            return initTexture(tryPath);
        }
        else
        {
            printf("file: %s not found for material\n", tryPath);
        }

    }

    // No texture found with any extension
    return 0;
}

// reads material and populates the struct
void readMaterial(Material *out, struct aiMaterial *mat, const char *basePath)
{

    if (mat == NULL)
    {
        ERROR("Material input pointer is NULL\n");
        return;
    }

    out->albedoTex = loadMaterialTexture(mat, aiTextureType_DIFFUSE, basePath);
    out->normalTex = loadMaterialTexture(mat, aiTextureType_NORMALS, basePath);
    out->metallicTex =
        loadMaterialTexture(mat, aiTextureType_METALNESS, basePath);
    out->roughnessTex =
        loadMaterialTexture(mat, aiTextureType_DIFFUSE_ROUGHNESS, basePath);

    ai_real value;
    if (aiGetMaterialFloat(mat, AI_MATKEY_METALLIC_FACTOR, &value) ==
        AI_SUCCESS)
    {
        out->metallic = (f32)value;
    }
    else
    {
        out->metallic = 0.0f;
    }

    if (aiGetMaterialFloat(mat, AI_MATKEY_ROUGHNESS_FACTOR, &value) ==
        AI_SUCCESS)
    {
        out->roughness = (f32)value;
    }
    else
    {
        out->roughness = 1.0f;
    }

    out->colour = (Vec3){255.0f, 255.0f, 255.0f};
    out->transparency = 1.0f;
}

MaterialUniforms getMaterialUniforms(u32 shader)
{
    MaterialUniforms uniforms = {0};
    uniforms.albedoTex = glGetUniformLocation(shader, "albedoTexture");
    uniforms.metallicTex = glGetUniformLocation(shader, "metallicTexture");
    uniforms.roughnessTex = glGetUniformLocation(shader, "roughnessTexture");
    uniforms.normalTex = glGetUniformLocation(shader, "normalTexture");
    uniforms.roughness = glGetUniformLocation(shader, "roughness");
    uniforms.metallic = glGetUniformLocation(shader, "metallic");
    uniforms.colour = glGetUniformLocation(shader, "colour");
    uniforms.transparancy = glGetUniformLocation(shader, "transparancy");
    return uniforms;
}

void updateMaterial(Material *material)
{
    MaterialUniforms *uniforms = &material->unifroms;

    // bind textures to texture units
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, material->albedoTex);
    glUniform1i(uniforms->albedoTex, 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, material->normalTex);
    glUniform1i(uniforms->normalTex, 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, material->metallicTex);
    glUniform1i(uniforms->metallicTex, 2);

    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, material->roughnessTex);
    glUniform1i(uniforms->roughnessTex, 3);

    // Set scalar material properties
    glUniform1f(uniforms->metallic, material->metallic);
    glUniform1f(uniforms->roughness, material->roughness);

    glUniform1f(uniforms->transparancy, material->transparency);
    Vec3 col = material->colour;
    glUniform3f(uniforms->colour, col.x, col.y, col.z);
}

Material *loadMaterialFromAssimp(struct aiScene *scene, u32 *count)
{
    if (!scene)
    {
        ERROR("Failed to load material: %s\n", aiGetErrorString());
        return NULL;
    }

    // check if there are any materials
    if (scene->mNumMaterials == 0)
    {
        ERROR("Model has no materials\n");
        return NULL;
    }

    *count = scene->mNumMaterials;

    Material *materials = (Material *)malloc(sizeof(Material) * scene->mNumMaterials);
    
    if (!materials) {
        ERROR("Failed to allocate memory for materials\n");
        return NULL;
    }
    
    // Initialize and populate materials
    for (u32 i = 0; i < scene->mNumMaterials; i++)
    {
        // Initialize material to zero
        materials[i] = (Material){0};
        
        // Get the Assimp material
        struct aiMaterial *aimat = scene->mMaterials[i];
        
        // Read material properties using the existing readMaterial function
        readMaterial(&materials[i], aimat, "../" TEXTURE_FOLDER);
    }
    
    return materials;
}
