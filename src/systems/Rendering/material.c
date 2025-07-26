#include <assimp/material.h>

#include <string.h>
#include "../../../include/druid.h"

u32 loadMaterialTexture(struct aiMaterial* mat, enum aiTextureType type, const char* base_path)
{
    if (aiGetMaterialTextureCount(mat, type) == 0)
    {
        return 0;
    }

    struct aiString texture_path;
    if (aiGetMaterialTexture(mat, type, 0, &texture_path, NULL, NULL, NULL, NULL, NULL, NULL) != AI_SUCCESS)
    {
        return 0;
    }

    //strip to just the filename
    const char* fileName = texture_path.data;
    const char* lastSlash = strrchr(fileName, '/');
    if (!lastSlash) lastSlash = strrchr(fileName, '\\');
    if (lastSlash) fileName = lastSlash + 1;

    const char* tryExtensions[] = {".png",".psd", ".jpg", ".jpeg", ".tga", ".bmp"};
    const u32 num_exts = sizeof(tryExtensions) / sizeof(tryExtensions[0]);

    for (u32 i = 0; i < num_exts; i++)
    {
        char tryPath[512];

        //strip original extension and add new one
        char baseName[256];
        strncpy(baseName, fileName, sizeof(baseName));
        char* dot = strrchr(baseName, '.');
        if (dot) *dot = '\0';

        snprintf(tryPath, sizeof(tryPath), "%s/%s%s", base_path, baseName, tryExtensions[i]);

        FILE* f = fopen(tryPath, "rb");
        if (f) 
        {
            fclose(f);
            return initTexture(tryPath);
        }
        else
        {
            printf("file: %s not found for material\n",tryPath);
        }

        return 0;
    }

}

//reads material and populates your struct
void readMaterial(Material* out, struct aiMaterial* mat, const char* basePath) 
{
    out->albedoTex = loadMaterialTexture(mat, aiTextureType_DIFFUSE, basePath);
    out->normalTex = loadMaterialTexture(mat, aiTextureType_NORMALS, basePath);
    out->metallicTex = loadMaterialTexture(mat, aiTextureType_METALNESS, basePath);
    out->roughnessTex = loadMaterialTexture(mat, aiTextureType_DIFFUSE_ROUGHNESS, basePath);

    ai_real value;
    if (aiGetMaterialFloat(mat, AI_MATKEY_METALLIC_FACTOR, &value) == AI_SUCCESS)
    {
        out->metallic = (f32)value;
    }
    else 
    {
        out->metallic = 0.0f;
    }

    if (aiGetMaterialFloat(mat, AI_MATKEY_ROUGHNESS_FACTOR, &value) == AI_SUCCESS) 
    {
        out->roughness = (f32)value;
    }
    else 
    {
        out->roughness = 1.0f;
    }

    out->colour = (Vec3){ 255.0f,255.0f,255.0f };
    out->transparency = 1.0f;
}   

MaterialUniforms getMaterialUniforms(u32 shader)
{
    MaterialUniforms uniforms = { 0 };
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


