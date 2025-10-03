#include <assimp/cimport.h>
#include <assimp/material.h>
#include <assimp/scene.h>

#include "../../../include/druid.h"
#include <string.h>

u32 loadMaterialTexture(struct aiMaterial *mat, enum aiTextureType type)
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

    TRACE("Looking up texture key: '%s'", fileName);

    if (!resources)
    {
        ERROR("Resource manager is not initialized");
        return 0;
    }

    // Look up texture in resource manager
    u32 textureIndex = 0;
    if (findInMap(&resources->textureIDs, fileName, &textureIndex))
    {
        // We found the texture, now we need to get the handle
        if (textureIndex < resources->textureUsed)
        {
            return resources->textureHandles[textureIndex];
        }
        else
        {
            ERROR("Texture index out of bounds!");
            return 0;
        }
    }

    // If not found, try with different extensions
    char baseName[256];
    strncpy(baseName, fileName, sizeof(baseName) - 1);
    baseName[sizeof(baseName) - 1] = '\0';
    char *dot = strrchr(baseName, '.');
    if (dot)
        *dot = '\0';

    const char *extensions[] = {".png", ".jpg", ".jpeg", ".bmp", ".tga"};
    for (int i = 0; i < sizeof(extensions) / sizeof(extensions[0]); i++)
    {
        char tempName[256];
        snprintf(tempName, sizeof(tempName), "%s%s", baseName, extensions[i]);
        if (findInMap(&resources->textureIDs, tempName, &textureIndex))
        {
            // Found it!
            if (textureIndex < resources->textureUsed)
            {
                return resources->textureHandles[textureIndex];
            }
        }
    }

    WARN("Texture '%s' not found in resource manager.", fileName);
    return 0;
}

// reads material and populates the struct
void readMaterial(Material *out, struct aiMaterial *mat)
{

    if (mat == NULL)
    {
        ERROR("Material input pointer is NULL\n");
        return;
    }

    out->albedoTex = loadMaterialTexture(mat, aiTextureType_DIFFUSE);
    out->normalTex = loadMaterialTexture(mat, aiTextureType_NORMALS);
    out->metallicTex =
        loadMaterialTexture(mat, aiTextureType_METALNESS);
    out->roughnessTex =
        loadMaterialTexture(mat, aiTextureType_DIFFUSE_ROUGHNESS);

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

void updateMaterial(Material *material, const MaterialUniforms *uniforms)
{
    const bool shouldDebug = false;

    if (shouldDebug)
    {
        DEBUG("Material Uniforms:\n");
        DEBUG("albedoTex: %d\n", uniforms->albedoTex);
        DEBUG("normalTex: %d\n", uniforms->normalTex);
        DEBUG("metallicTex: %d\n", uniforms->metallicTex);
        DEBUG("roughnessTex: %d\n", uniforms->roughnessTex);
        DEBUG("metallic: %d\n", uniforms->metallic);
        DEBUG("roughness: %d\n", uniforms->roughness);
        DEBUG("transparancy: %d\n", uniforms->transparancy);
        DEBUG("colour: %d\n", uniforms->colour);
    }

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
        readMaterial(&materials[i], aimat);
    }
    
    return materials;
}
