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
    const char* file_name = texture_path.data;
    const char* last_slash = strrchr(file_name, '/');
    if (!last_slash) last_slash = strrchr(file_name, '\\');
    if (last_slash) file_name = last_slash + 1;

    const char* try_extensions[] = { "", ".png", ".jpg", ".jpeg", ".tga", ".bmp" };
    const u32 num_exts = sizeof(try_extensions) / sizeof(try_extensions[0]);

    for (u32 i = 0; i < num_exts; i++)
    {
        char try_path[512];

        if (try_extensions[i][0] == '\0') 
        {
            //try original 
            snprintf(try_path, sizeof(try_path), "%s/%s", base_path, file_name);
        }
        else 
        {
            //strip original extension and add new one
            char base_name[256];
            strncpy(base_name, file_name, sizeof(base_name));
            char* dot = strrchr(base_name, '.');
            if (dot) *dot = '\0';

            snprintf(try_path, sizeof(try_path), "%s/%s%s", base_path, base_name, try_extensions[i]);
        }

        FILE* f = fopen(try_path, "rb");
        if (f) 
        {
            fclose(f);
            return initTexture(try_path);
        }
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
    else {
        out->roughness = 1.0f;
    }
}   
