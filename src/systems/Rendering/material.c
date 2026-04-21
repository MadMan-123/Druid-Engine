#include <assimp/cimport.h>
#include <assimp/material.h>
#include <assimp/scene.h>

#include "../../../include/druid.h"
#include <string.h>
#include <stdio.h>

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
    const c8 *fileName = texture_path.data;
    const c8 *lastSlash = strrchr(fileName, '/');
    if (!lastSlash)
        lastSlash = strrchr(fileName, '\\');
    if (lastSlash)
        fileName = lastSlash + 1;

    if(DEBUG_RESOURCES)
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
    c8 baseName[256];
    strncpy(baseName, fileName, sizeof(baseName) - 1);
    baseName[sizeof(baseName) - 1] = '\0';
    c8 *dot = strrchr(baseName, '.');
    if (dot)
        *dot = '\0';

    const c8 *extensions[] = {".png", ".jpg", ".jpeg", ".bmp", ".tga"};
    for (i32 i = 0; i < sizeof(extensions) / sizeof(extensions[0]); i++)
    {
        c8 tempName[256];
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

    out->colour = (Vec3){1.0f, 1.0f, 1.0f};
    out->transparency = 1.0f;
}

MaterialUniforms getMaterialUniforms(u32 shader)
{
    // Cache per shader — avoids 9 glGetUniformLocation calls every frame
    static u32 s_cachedShader = 0;
    static MaterialUniforms s_cached = {0};
    if (shader == s_cachedShader && shader != 0)
        return s_cached;

    MaterialUniforms uniforms = {0};
    uniforms.albedoTex = glGetUniformLocation(shader, "albedoTexture");
    uniforms.metallicTex = glGetUniformLocation(shader, "metallicTexture");
    uniforms.roughnessTex = glGetUniformLocation(shader, "roughnessTexture");
    uniforms.normalTex = glGetUniformLocation(shader, "normalTexture");
    uniforms.roughness = glGetUniformLocation(shader, "roughness");
    uniforms.metallic = glGetUniformLocation(shader, "metallic");
    uniforms.colour = glGetUniformLocation(shader, "colour");
    uniforms.transparency = glGetUniformLocation(shader, "transparency");
    uniforms.emissive = glGetUniformLocation(shader, "emissive");

    s_cachedShader = shader;
    s_cached = uniforms;
    return uniforms;
}

void updateMaterial(Material *material, const MaterialUniforms *uniforms)
{
    const b8 shouldDebug = false;

    if (shouldDebug)
    {
        
        DEBUG("Material Uniforms:\n");
        DEBUG("albedoTex: %d\n", uniforms->albedoTex);
        DEBUG("normalTex: %d\n", uniforms->normalTex);
        DEBUG("metallicTex: %d\n", uniforms->metallicTex);
        DEBUG("roughnessTex: %d\n", uniforms->roughnessTex);
        DEBUG("metallic: %d\n", uniforms->metallic);
        DEBUG("roughness: %d\n", uniforms->roughness);
        DEBUG("transparency: %d\n", uniforms->transparency);
        DEBUG("colour: %d\n", uniforms->colour);
    }

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

    glUniform1f(uniforms->transparency, material->transparency);
    Vec3 col = material->colour;
    glUniform3f(uniforms->colour, col.x, col.y, col.z);
    glUniform1f(uniforms->emissive, material->emissive);
}

// Reverse-lookup: find the name registered for a given GL texture handle.
// Returns true and writes into outName on success, false if not found.
static b8 texHandleToName(u32 handle, c8 *outName, u32 nameSize)
{
    if (!resources || handle == 0) return false;

    // Find the index in textureHandles that matches this GL handle
    u32 texIdx = (u32)-1;
    for (u32 i = 0; i < resources->textureUsed; i++)
    {
        if (resources->textureHandles[i] == handle)
        { texIdx = i; break; }
    }
    if (texIdx == (u32)-1) return false;

    // Walk the HashMap to find the key whose value == texIdx
    for (u32 i = 0; i < resources->textureIDs.capacity; i++)
    {
        if (!resources->textureIDs.pairs[i].occupied) continue;
        u32 storedIdx = *(u32 *)resources->textureIDs.pairs[i].value;
        if (storedIdx == texIdx)
        {
            strncpy(outName, (const c8 *)resources->textureIDs.pairs[i].key, nameSize - 1);
            outName[nameSize - 1] = '\0';
            return true;
        }
    }
    return false;
}

void applyMaterialPresets(ResourceManager *manager, const c8 *projectDir)
{
    if (!manager || !projectDir || projectDir[0] == '\0') return;

    for (u32 i = 0; i < manager->materialIDs.capacity; i++)
    {
        if (!manager->materialIDs.pairs[i].occupied) continue;

        const c8 *matName = (const c8 *)manager->materialIDs.pairs[i].key;
        u32 matIdx        = *(u32 *)manager->materialIDs.pairs[i].value;
        if (matIdx >= manager->materialUsed) continue;

        c8 filePath[MAX_NAME_SIZE * 2];
        snprintf(filePath, sizeof(filePath), "%s/materials/%s.drmt", projectDir, matName);

        FILE *probe = fopen(filePath, "rb");
        if (!probe) continue;
        fclose(probe);

        Material loaded = loadMaterial(filePath, NULL, 0);
        manager->materialBuffer[matIdx] = loaded;
        INFO("applyMaterialPresets: applied preset '%s' to slot %u", matName, matIdx);
    }
}

b8 saveMaterial(const c8 *filePath, const c8 *name, const Material *mat)
{
    if (!filePath || !name || !mat)
    {
        ERROR("saveMaterial: NULL argument");
        return false;
    }

    FILE *f = fopen(filePath, "wb");
    if (!f)
    {
        ERROR("saveMaterial: failed to open '%s' for writing", filePath);
        return false;
    }

    // Header
    MaterialFileHeader hdr;
    hdr.magic   = MATERIAL_MAGIC;
    hdr.version = MATERIAL_VERSION;
    fwrite(&hdr, sizeof(hdr), 1, f);

    // Name
    c8 nameBuf[MAX_NAME_SIZE];
    memset(nameBuf, 0, sizeof(nameBuf));
    strncpy(nameBuf, name, MAX_NAME_SIZE - 1);
    fwrite(nameBuf, sizeof(nameBuf), 1, f);

    // Texture names (4 slots: albedo, normal, metallic, roughness)
    u32 texHandles[4] = { mat->albedoTex, mat->normalTex, mat->metallicTex, mat->roughnessTex };
    for (u32 i = 0; i < 4; i++)
    {
        c8 texName[MAX_NAME_SIZE];
        memset(texName, 0, sizeof(texName));
        texHandleToName(texHandles[i], texName, MAX_NAME_SIZE);
        fwrite(texName, sizeof(texName), 1, f);
    }

    // Scalar properties
    fwrite(&mat->roughness,    sizeof(f32),  1, f);
    fwrite(&mat->metallic,     sizeof(f32),  1, f);
    fwrite(&mat->transparency, sizeof(f32),  1, f);
    fwrite(&mat->emissive,     sizeof(f32),  1, f);
    fwrite(&mat->colour,       sizeof(Vec3), 1, f);

    fclose(f);
    INFO("saveMaterial: saved '%s' to '%s'", name, filePath);
    return true;
}

Material loadMaterial(const c8 *filePath, c8 *outName, u32 nameSize)
{
    Material result;
    memset(&result, 0, sizeof(result));
    result.colour       = (Vec3){1.0f, 1.0f, 1.0f};
    result.roughness    = 0.5f;
    result.transparency = 1.0f;

    if (!filePath)
    {
        ERROR("loadMaterial: NULL filePath");
        return result;
    }

    FILE *f = fopen(filePath, "rb");
    if (!f)
    {
        ERROR("loadMaterial: failed to open '%s'", filePath);
        return result;
    }

    // Header
    MaterialFileHeader hdr;
    if (fread(&hdr, sizeof(hdr), 1, f) != 1 || hdr.magic != MATERIAL_MAGIC)
    {
        ERROR("loadMaterial: invalid magic in '%s'", filePath);
        fclose(f);
        return result;
    }
    if (hdr.version != MATERIAL_VERSION)
    {
        WARN("loadMaterial: version mismatch in '%s' (got %u, expected %u)",
             filePath, hdr.version, MATERIAL_VERSION);
    }

    // Name
    c8 nameBuf[MAX_NAME_SIZE];
    if (fread(nameBuf, sizeof(nameBuf), 1, f) != 1) { fclose(f); return result; }
    if (outName && nameSize > 0)
    {
        strncpy(outName, nameBuf, nameSize - 1);
        outName[nameSize - 1] = '\0';
    }

    // Texture names -> GL handles
    u32 *texDst[4] = { &result.albedoTex, &result.normalTex, &result.metallicTex, &result.roughnessTex };
    for (u32 i = 0; i < 4; i++)
    {
        c8 texName[MAX_NAME_SIZE];
        if (fread(texName, sizeof(texName), 1, f) != 1) { fclose(f); return result; }
        if (texName[0] == '\0') continue; // no texture for this slot

        u32 texIdx = 0;
        if (resources && findInMap(&resources->textureIDs, texName, &texIdx) &&
            texIdx < resources->textureUsed)
        {
            *texDst[i] = resources->textureHandles[texIdx];
        }
        else
        {
            WARN("loadMaterial: texture '%s' not found in resource manager, slot %u cleared", texName, i);
        }
    }

    // Scalars
    if (fread(&result.roughness,    sizeof(f32),  1, f) != 1) { fclose(f); return result; }
    if (fread(&result.metallic,     sizeof(f32),  1, f) != 1) { fclose(f); return result; }
    if (fread(&result.transparency, sizeof(f32),  1, f) != 1) { fclose(f); return result; }
    if (fread(&result.emissive,     sizeof(f32),  1, f) != 1) { fclose(f); return result; }
    if (fread(&result.colour,       sizeof(Vec3), 1, f) != 1) { fclose(f); return result; }

    fclose(f);
    INFO("loadMaterial: loaded '%s' from '%s'", nameBuf, filePath);
    return result;
}

Material *loadMaterialFromAssimp(struct aiScene *scene, u32 *count)
{
    if (!scene)
    {
        ERROR("Failed to load material: %s\n", aiGetErrorString());
        return NULL;
    }

    if (scene->mNumMaterials == 0)
    {
        ERROR("Model has no materials\n");
        return NULL;
    }

    *count = scene->mNumMaterials;

    Material *materials = (Material *)dalloc(sizeof(Material) * scene->mNumMaterials, MEM_TAG_MATERIAL);
    
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
