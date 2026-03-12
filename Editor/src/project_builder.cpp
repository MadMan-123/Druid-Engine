#include "project_builder.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

// state
GameDLL g_gameDLL         = {0};
b8      g_gameRunning     = false;
c8      g_buildLog[4096]  = "";
b8      g_buildInProgress = false;

// write a string to projectDir/relPath
static b8 writeTemplate(const c8 *dir, const c8 *relPath, const c8 *content)
{
    c8 full[MAX_PATH_LENGTH];
    snprintf(full, sizeof(full), "%s/%s", dir, relPath);
    return writeFile(full, (const u8 *)content, (u32)strlen(content));
}

static void stripLastComponent(c8 *path)
{
    c8 *last = path;
    for (c8 *p = path; *p; p++)
        if (*p == '\\' || *p == '/') last = p;
    if (last != path) *last = '\0';
}

// copyFileSingle / copyDirRecursive now delegate to platform.c
static b8 copyFileSingle(const c8 *src, const c8 *dst)
{
    return platformFileCopy(src, dst);
}

static void getProjectBaseName(const c8 *projectDir, c8 *out, u32 size)
{
    if (!out || size == 0)
        return;

    out[0] = '\0';
    if (!projectDir || !projectDir[0])
        return;

    const c8 *name = projectDir;
    for (const c8 *p = projectDir; *p; p++)
    {
        if (*p == '/' || *p == '\\')
            name = p + 1;
    }

    strncpy(out, name, size - 1);
    out[size - 1] = '\0';
}

static void getFileNameOnly(const c8 *path, c8 *out, u32 size)
{
    if (!out || size == 0)
        return;

    out[0] = '\0';
    if (!path || !path[0])
        return;

    const c8 *name = path;
    for (const c8 *p = path; *p; p++)
    {
        if (*p == '/' || *p == '\\')
            name = p + 1;
    }

    strncpy(out, name, size - 1);
    out[size - 1] = '\0';
}

static void writeStartupSceneConfig(const c8 *dir, const c8 *scenePath)
{
    if (!dir || !dir[0])
        return;

    c8 sceneFile[256] = "scene.drsc";
    if (scenePath && scenePath[0])
    {
        getFileNameOnly(scenePath, sceneFile, sizeof(sceneFile));
        if (sceneFile[0] == '\0')
            strncpy(sceneFile, "scene.drsc", sizeof(sceneFile) - 1);
    }

    c8 cfgPath[MAX_PATH_LENGTH];
    snprintf(cfgPath, sizeof(cfgPath), "%s/startup_scene.txt", dir);
    writeFile(cfgPath, (const u8 *)sceneFile, (u32)strlen(sceneFile));
}

// ---- template: src/game.h ----
static const c8 *TPL_GAME_H =
    "#pragma once\n"
    "#include <druid.h>\n"
    "\n"
    "#ifdef GAME_EXPORT\n"
    "  #ifdef _WIN32\n"
    "    #define GAME_API __declspec(dllexport)\n"
    "  #else\n"
    "    #define GAME_API __attribute__((visibility(\"default\")))\n"
    "  #endif\n"
    "#else\n"
    "  #ifdef _WIN32\n"
    "    #define GAME_API __declspec(dllimport)\n"
    "  #else\n"
    "    #define GAME_API\n"
    "  #endif\n"
    "#endif\n"
    "\n"
    "// plugin interface\n"
    "typedef void (*PluginInitFn)(const c8 *projectDir);\n"
    "typedef void (*PluginUpdateFn)(f32 dt);\n"
    "typedef void (*PluginRenderFn)(f32 dt);\n"
    "typedef void (*PluginDestroyFn)(void);\n"
    "\n"
    "typedef struct GamePlugin {\n"
    "    PluginInitFn    init;\n"
    "    PluginUpdateFn  update;\n"
    "    PluginRenderFn  render;\n"
    "    PluginDestroyFn destroy;\n"
    "} GamePlugin;\n"
    "\n"
    "#ifdef __cplusplus\n"
    "extern \"C\" {\n"
    "#endif\n"
    "\n"
    "GAME_API void druidGetPlugin(GamePlugin *out);\n"
    "\n"
    "#ifdef __cplusplus\n"
    "}\n"
    "#endif\n";

// ---- template: src/game.cpp ----
// demonstrates loading scenes made in the editor and rendering entities
static const c8 *TPL_GAME_CPP =
    "#include \"game.h\"\n"
    "#include <stdio.h>\n"
    "#include <string.h>\n"
    "\n"
    "// ---- game state ----\n"
    "static c8       g_projectDir[512] = {0};\n"
    "static Camera  *g_cam = NULL;  // pointer to renderer's active camera\n"
    "static u32      g_defaultShader = 0;\n"
    "static f32      g_time = 0.0f;\n"
    "\n"
    "// scene data loaded from .drsc files\n"
    "static SceneData g_scene = {0};\n"
    "static b8        g_sceneLoaded = false;\n"
    "static b8        g_standaloneMode = false;\n"
    "\n"
    "// entity field pointers (extracted from the loaded scene archetype)\n"
    "static Vec3 *g_positions  = NULL;\n"
    "static Vec4 *g_rotations  = NULL;\n"
    "static Vec3 *g_scales     = NULL;\n"
    "static b8   *g_isActive   = NULL;\n"
    "static u32  *g_modelIDs   = NULL;\n"
    "static u32  *g_shaderH    = NULL;\n"
    "static u32  *g_matIDs     = NULL;\n"
    "static b8   *g_sceneCameraFlags = NULL;\n"
    "static u32   g_entityCount = 0;\n"
    "\n"
    "static i32 findFieldIndex(const StructLayout *layout, const c8 *name)\n"
    "{\n"
    "    if (!layout || !layout->fields || !name) return -1;\n"
    "    for (u32 i = 0; i < layout->count; i++)\n"
    "    {\n"
    "        if (strcmp(layout->fields[i].name, name) == 0)\n"
    "            return (i32)i;\n"
    "    }\n"
    "    return -1;\n"
    "}\n"
    "\n"
    "// helper: build a full path from the project dir\n"
    "static void scenePath(c8 *out, u32 sz, const c8 *name)\n"
    "{\n"
    "    snprintf(out, sz, \"%s/scenes/%s\", g_projectDir, name);\n"
    "}\n"
    "\n"
    "// Load a .drsc scene file and bind entity field pointers.\n"
    "// Returns true on success.\n"
    "static b8 loadGameScene(const c8 *name)\n"
    "{\n"
    "    c8 path[512];\n"
    "    scenePath(path, sizeof(path), name);\n"
    "\n"
    "    SceneData sd = loadScene(path);\n"
    "    if (sd.archetypeCount == 0 || !sd.archetypes)\n"
    "    {\n"
    "        ERROR(\"Failed to load scene: %s\", path);\n"
    "        return false;\n"
    "    }\n"
    "\n"
    "    g_scene = sd;\n"
    "    g_sceneLoaded = true;\n"
    "\n"
    "    // grab SOA field pointers from the first archetype\n"
    "    void **fields = getArchetypeFields(&g_scene.archetypes[0], 0);\n"
    "    StructLayout *layout = g_scene.archetypes[0].layout;\n"
    "    i32 posIdx = findFieldIndex(layout, \"position\");\n"
    "    i32 rotIdx = findFieldIndex(layout, \"rotation\");\n"
    "    i32 scaleIdx = findFieldIndex(layout, \"scale\");\n"
    "    i32 activeIdx = findFieldIndex(layout, \"isActive\");\n"
    "    i32 modelIdx = findFieldIndex(layout, \"modelID\");\n"
    "    i32 shaderIdx = findFieldIndex(layout, \"shaderHandle\");\n"
    "    i32 matIdx = findFieldIndex(layout, \"materialID\");\n"
    "    i32 camIdx = findFieldIndex(layout, \"isSceneCamera\");\n"
    "    if (posIdx < 0 || rotIdx < 0 || scaleIdx < 0 || activeIdx < 0 || modelIdx < 0)\n"
    "    {\n"
    "        ERROR(\"Scene is missing required fields\");\n"
    "        return false;\n"
    "    }\n"
    "    g_positions  = (Vec3 *)fields[posIdx];\n"
    "    g_rotations  = (Vec4 *)fields[rotIdx];\n"
    "    g_scales     = (Vec3 *)fields[scaleIdx];\n"
    "    g_isActive   = (b8 *)  fields[activeIdx];\n"
    "    g_modelIDs   = (u32 *) fields[modelIdx];\n"
    "    g_shaderH    = (shaderIdx >= 0) ? (u32 *)fields[shaderIdx] : NULL;\n"
    "    g_matIDs     = (matIdx >= 0) ? (u32 *)fields[matIdx] : NULL;\n"
    "    g_sceneCameraFlags = (camIdx >= 0) ? (b8 *)fields[camIdx] : NULL;\n"
    "    g_entityCount = g_scene.archetypes[0].arena[0].count;\n"
    "\n"
    "    if (g_cam && g_sceneCameraFlags)\n"
    "    {\n"
    "        for (u32 i = 0; i < g_entityCount; i++)\n"
    "        {\n"
    "            if (g_isActive[i] && g_sceneCameraFlags[i])\n"
    "            {\n"
    "                g_cam->pos = g_positions[i];\n"
    "                g_cam->orientation = g_rotations[i];\n"
    "                break;\n"
    "            }\n"
    "        }\n"
    "    }\n"
    "\n"
    "    // apply loaded materials to the resource manager\n"
    "    if (sd.materialCount > 0 && sd.materials)\n"
    "    {\n"
    "        u32 count = sd.materialCount;\n"
    "        if (count > resources->materialCount)\n"
    "            count = resources->materialCount;\n"
    "        memcpy(resources->materialBuffer, sd.materials,\n"
    "               sizeof(Material) * count);\n"
    "        resources->materialUsed = count;\n"
    "    }\n"
    "\n"
    "    INFO(\"Loaded scene '%s' with %u entities\", name, g_entityCount);\n"
    "    return true;\n"
    "}\n"
    "\n"
    "static b8 loadStartupScene(void)\n"
    "{\n"
    "    c8 cfgPath[512];\n"
    "    snprintf(cfgPath, sizeof(cfgPath), \"%s/startup_scene.txt\", g_projectDir);\n"
    "\n"
    "    FILE *cfg = fopen(cfgPath, \"rb\");\n"
    "    if (cfg)\n"
    "    {\n"
    "        c8 sceneName[256] = {0};\n"
    "        size_t read = fread(sceneName, 1, sizeof(sceneName) - 1, cfg);\n"
    "        fclose(cfg);\n"
    "        sceneName[read] = '\\0';\n"
    "        for (size_t i = 0; sceneName[i]; i++)\n"
    "        {\n"
    "            if (sceneName[i] == '\\r' || sceneName[i] == '\\n')\n"
    "            {\n"
    "                sceneName[i] = '\\0';\n"
    "                break;\n"
    "            }\n"
    "        }\n"
    "        if (sceneName[0])\n"
    "            return loadGameScene(sceneName);\n"
    "    }\n"
    "\n"
    "    c8 scenesDir[512];\n"
    "    snprintf(scenesDir, sizeof(scenesDir), \"%s/scenes\", g_projectDir);\n"
    "    u32 fileCount = 0;\n"
    "    c8 **files = listFilesInDirectory(scenesDir, &fileCount);\n"
    "    if (files)\n"
    "    {\n"
    "        for (u32 i = 0; i < fileCount; i++)\n"
    "        {\n"
    "            u32 len = (u32)strlen(files[i]);\n"
    "            if (len > 5 && strcmp(files[i] + len - 5, \".drsc\") == 0)\n"
    "            {\n"
    "                const c8 *name = files[i];\n"
    "                const c8 *slash = strrchr(name, '/');\n"
    "                if (!slash) slash = strrchr(name, '\\\\');\n"
    "                b8 ok = loadGameScene(slash ? slash + 1 : name);\n"
    "                for (u32 j = 0; j < fileCount; j++) free(files[j]);\n"
    "                free(files);\n"
    "                return ok;\n"
    "            }\n"
    "        }\n"
    "\n"
    "        for (u32 i = 0; i < fileCount; i++) free(files[i]);\n"
    "        free(files);\n"
    "    }\n"
    "\n"
    "    return loadGameScene(\"scene.drsc\");\n"
    "}\n"
    "\n"
    "// ---- plugin callbacks ----\n"
    "\n"
    "static void gameInit(const c8 *projectDir)\n"
    "{\n"
    "    strncpy(g_projectDir, projectDir, sizeof(g_projectDir) - 1);\n"
    "    g_standaloneMode = (strcmp(projectDir, \".\") == 0);\n"
    "\n"
    "    // Get the renderer's active camera.\n"
    "    // In the editor, this is the scene camera at play start.\n"
    "    // In standalone, the main.cpp creates the renderer + camera first.\n"
    "    if (renderer)\n"
    "        g_cam = (Camera *)bufferGet(&renderer->cameras, renderer->activeCamera);\n"
    "\n"
    "    u32 idx = 0;\n"
    "    findInMap(&resources->shaderIDs, \"default\", &idx);\n"
    "    g_defaultShader = resources->shaderHandles[idx];\n"
    "\n"
    "    loadStartupScene();\n"
    "}\n"
    "\n"
    "static void gameUpdate(f32 dt)\n"
    "{\n"
    "    g_time += dt;\n"
    "\n"
    "    // Example: spin all active entities on the Y axis\n"
    "    // for (u32 i = 0; i < g_entityCount; i++)\n"
    "    // {\n"
    "    //     if (!g_isActive[i]) continue;\n"
    "    //     g_rotations[i] = quatFromAxisAngle(\n"
    "    //         (Vec3){0.0f, 1.0f, 0.0f}, g_time);\n"
    "    // }\n"
    "}\n"
    "\n"
    "static void gameRender(f32 dt)\n"
    "{\n"
    "    if (!g_sceneLoaded) return;\n"
    "    if (!g_standaloneMode) return;\n"
    "\n"
    "    // Camera is managed via the renderer (rendererBeginFrame pushes it to UBO).\n"
    "    // To move the camera in your game, use:\n"
    "    //   moveForward(g_cam, speed * dt);\n"
    "    //   rotateY(g_cam, angle);\n"
    "    // The editor calls rendererBeginFrame() each frame to upload it.\n"
    "\n"
    "    glUseProgram(g_defaultShader);\n"
    "\n"
    "    for (u32 id = 0; id < g_entityCount; id++)\n"
    "    {\n"
    "        if (!g_isActive[id]) continue;\n"
    "        u32 modelID = g_modelIDs[id];\n"
    "        if (modelID >= resources->modelUsed) continue;\n"
    "\n"
    "        Model *model = &resources->modelBuffer[modelID];\n"
    "        Transform t = {g_positions[id], g_rotations[id], g_scales[id]};\n"
    "        u32 shaderToUse = (g_shaderH && g_shaderH[id] != 0) ? g_shaderH[id] : g_defaultShader;\n"
    "        glUseProgram(shaderToUse);\n"
    "        updateShaderModel(shaderToUse, t);\n"
    "\n"
    "        for (u32 m = 0; m < model->meshCount; m++)\n"
    "        {\n"
    "            u32 mi = model->meshIndices[m];\n"
    "            if (mi >= resources->meshUsed) continue;\n"
    "\n"
    "            u32 matIdx = model->materialIndices[m];\n"
    "            if (g_matIDs && g_matIDs[id] != (u32)-1)\n"
    "                matIdx = g_matIDs[id];\n"
    "            if (matIdx < resources->materialUsed)\n"
    "            {\n"
    "                MaterialUniforms unis = getMaterialUniforms(shaderToUse);\n"
    "                updateMaterial(&resources->materialBuffer[matIdx], &unis);\n"
    "            }\n"
    "            drawMesh(&resources->meshBuffer[mi]);\n"
    "        }\n"
    "    }\n"
    "}\n"
    "\n"
    "static void gameDestroy(void)\n"
    "{\n"
    "    // SceneData archetypes were malloc'd by loadScene – free them\n"
    "    if (g_sceneLoaded && g_scene.archetypes)\n"
    "    {\n"
    "        for (u32 i = 0; i < g_scene.archetypeCount; i++)\n"
    "            destroyArchetype(&g_scene.archetypes[i]);\n"
    "        free(g_scene.archetypes);\n"
    "        g_scene.archetypes = NULL;\n"
    "    }\n"
    "    if (g_scene.materials)\n"
    "    {\n"
    "        free(g_scene.materials);\n"
    "        g_scene.materials = NULL;\n"
    "    }\n"
    "    g_sceneLoaded = false;\n"
    "}\n"
    "\n"
    "void druidGetPlugin(GamePlugin *out)\n"
    "{\n"
    "    out->init    = gameInit;\n"
    "    out->update  = gameUpdate;\n"
    "    out->render  = gameRender;\n"
    "    out->destroy = gameDestroy;\n"
    "}\n";

// ---- template: src/main.cpp ----
// standalone launcher for running the game without the editor
static const c8 *TPL_MAIN_CPP =
    "#include \"game.h\"\n"
    "#include <stdio.h>\n"
    "\n"
    "#ifndef DRUID_APP_TITLE\n"
    "#define DRUID_APP_TITLE \"Game\"\n"
    "#endif\n"
    "\n"
    "static GamePlugin plugin = {0};\n"
    "static Application *g_app = NULL;\n"
    "static Mesh *g_skyboxMesh = NULL;\n"
    "static u32   g_skyboxTex = 0;\n"
    "static u32   g_skyboxShader = 0;\n"
    "\n"
    "static void loadStandaloneSkybox(void)\n"
    "{\n"
    "    const c8 *suffixes[6] = {\"right.jpg\", \"left.jpg\", \"top.jpg\", \"bottom.jpg\", \"front.jpg\", \"back.jpg\"};\n"
    "    c8 paths[6][512];\n"
    "    const c8 *faces[6];\n"
    "    for (u32 i = 0; i < 6; i++)\n"
    "    {\n"
    "        snprintf(paths[i], sizeof(paths[i]), \"./res/Textures/Skybox/%s\", suffixes[i]);\n"
    "        if (!fileExists(paths[i]))\n"
    "            return;\n"
    "        faces[i] = paths[i];\n"
    "    }\n"
    "\n"
    "    g_skyboxTex = createCubeMapTexture(faces, 6);\n"
    "    g_skyboxMesh = createSkyboxMesh();\n"
    "    g_skyboxShader = createGraphicsProgram(\"./res/Skybox.vert\", \"./res/Skybox.frag\");\n"
    "}\n"
    "\n"
    "static void _init(void)\n"
    "{\n"
    "    // Create the renderer (editor normally does this)\n"
    "    if (!renderer && g_app && g_app->display)\n"
    "    {\n"
    "        Renderer *r = createRenderer(g_app->display, 70.0f, 0.1f, 100.0f, 8, 16, 8);\n"
    "        if (r)\n"
    "        {\n"
    "            u32 idx = 0;\n"
    "            findInMap(&resources->shaderIDs, \"default\", &idx);\n"
    "            r->defaultShader = resources->shaderHandles[idx];\n"
    "            // Acquire a camera for the game\n"
    "            u32 camSlot = rendererAcquireCamera(r, (Vec3){0.0f, 2.0f, 8.0f}, 70.0f,\n"
    "                                                16.0f / 9.0f, 0.1f, 100.0f);\n"
    "            rendererSetActiveCamera(r, camSlot);\n"
    "        }\n"
    "    }\n"
    "    loadStandaloneSkybox();\n"
    "    plugin.init(\".\");\n"
    "}\n"
    "static void _update(f32 dt)\n"
    "{\n"
    "    plugin.update(dt);\n"
    "    if (renderer) rendererBeginFrame(renderer, dt);\n"
    "}\n"
    "static void _render(f32 dt)\n"
    "{\n"
    "    if (g_skyboxMesh && g_skyboxTex && g_skyboxShader)\n"
    "    {\n"
    "        glDepthFunc(GL_LEQUAL);\n"
    "        glDepthMask(GL_FALSE);\n"
    "        glUseProgram(g_skyboxShader);\n"
    "        glBindVertexArray(g_skyboxMesh->vao);\n"
    "        glBindTexture(GL_TEXTURE_CUBE_MAP, g_skyboxTex);\n"
    "        glDrawArrays(GL_TRIANGLES, 0, 36);\n"
    "        glBindVertexArray(0);\n"
    "        glDepthMask(GL_TRUE);\n"
    "        glDepthFunc(GL_LESS);\n"
    "    }\n"
    "    plugin.render(dt);\n"
    "}\n"
    "static void _destroy(void)\n"
    "{\n"
    "    plugin.destroy();\n"
    "    if (g_skyboxMesh) freeMesh(g_skyboxMesh);\n"
    "    if (g_skyboxTex) freeTexture(g_skyboxTex);\n"
    "    if (g_skyboxShader) freeShader(g_skyboxShader);\n"
    "}\n"
    "\n"
    "int main(int argc, char **argv)\n"
    "{\n"
    "    druidGetPlugin(&plugin);\n"
    "    g_app = createApplication(DRUID_APP_TITLE, _init, _update, _render, _destroy);\n"
    "    g_app->width  = 1280;\n"
    "    g_app->height = 720;\n"
    "    run(g_app);\n"
    "    return 0;\n"
    "}\n";

// ---- template: res/shader.vert ----
// matches the engine interleaved layout: pos(0) texcoord(1) normal(2)
static const c8 *TPL_SHADER_VERT =
    "#version 420\n"
    "\n"
    "layout (location = 0) in vec3 position;\n"
    "layout (location = 1) in vec2 texCoord;\n"
    "layout (location = 2) in vec3 normal;\n"
    "\n"
    "layout (std140, binding = 0) uniform CoreShaderData\n"
    "{\n"
    "    vec3  camPos;\n"
    "    float time;\n"
    "    mat4  view;\n"
    "    mat4  projection;\n"
    "};\n"
    "\n"
    "uniform mat4 model;\n"
    "\n"
    "out vec2 tc;\n"
    "out vec3 Normal;\n"
    "out vec3 FragPos;\n"
    "\n"
    "void main()\n"
    "{\n"
    "    FragPos = vec3(model * vec4(position, 1.0));\n"
    "    Normal  = normal;\n"
    "    tc      = texCoord;\n"
    "    gl_Position = projection * view * model * vec4(position, 1.0);\n"
    "}\n";

// ---- template: res/shader.frag ----
// uses the engine material uniforms so updateMaterial() works out of the box
static const c8 *TPL_SHADER_FRAG =
    "#version 420\n"
    "\n"
    "in vec2 tc;\n"
    "in vec3 Normal;\n"
    "in vec3 FragPos;\n"
    "\n"
    "uniform sampler2D albedoTexture;\n"
    "uniform sampler2D metallicTexture;\n"
    "uniform sampler2D roughnessTexture;\n"
    "uniform sampler2D normalTexture;\n"
    "uniform float roughness;\n"
    "uniform float metallic;\n"
    "uniform float transparency;\n"
    "uniform vec3  colour;\n"
    "\n"
    "out vec4 FragColour;\n"
    "\n"
    "void main()\n"
    "{\n"
    "    vec3 diffuse = texture(albedoTexture, tc).rgb;\n"
    "\n"
    "    vec3 finalColor = diffuse;\n"
    "    if (!(colour.r == 1.0 && colour.g == 1.0 && colour.b == 1.0))\n"
    "        finalColor = diffuse * colour;\n"
    "\n"
    "    FragColour = vec4(finalColor, transparency);\n"
    "}\n";

// ---- template: CMakeLists.txt ----
static const c8 *TPL_CMAKELISTS =
    "cmake_minimum_required(VERSION 3.15)\n"
    "project(Game C CXX)\n"
    "get_filename_component(DRUID_PROJECT_NAME \"${CMAKE_SOURCE_DIR}\" NAME)\n"
    "\n"
    "set(CMAKE_C_STANDARD 11)\n"
    "set(CMAKE_CXX_STANDARD 17)\n"
    "set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS OFF)\n"
    "\n"
    "set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)\n"
    "set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)\n"
    "set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)\n"
    "\n"
    "set(DEPS_DIR \"${CMAKE_SOURCE_DIR}/deps\")\n"
    "\n"
    "include_directories(\n"
    "    ${DEPS_DIR}/include\n"
    "    ${DEPS_DIR}/include/GL\n"
    "    ${DEPS_DIR}/include/SDL3\n"
    ")\n"
    "\n"
    "file(GLOB GAME_SOURCES CONFIGURE_DEPENDS\n"
    "    ${CMAKE_SOURCE_DIR}/src/*.cpp\n"
    "    ${CMAKE_SOURCE_DIR}/src/*.c\n"
    ")\n"
    "list(FILTER GAME_SOURCES EXCLUDE REGEX \"main\\\\.cpp$\")\n"
    "\n"
    "add_library(game SHARED ${GAME_SOURCES})\n"
    "target_compile_definitions(game PRIVATE GAME_EXPORT)\n"
    "\n"
    "if(MSVC)\n"
    "    target_link_libraries(game PRIVATE\n"
    "        ${DEPS_DIR}/lib/druid.lib\n"
    "        ${DEPS_DIR}/lib/glew32.lib\n"
    "        ${DEPS_DIR}/lib/libSDL3.dll.a\n"
    "        opengl32 gdi32 user32 winmm\n"
    "    )\n"
    "else()\n"
    "    target_link_libraries(game PRIVATE\n"
    "        ${DEPS_DIR}/lib/libdruid.dll.a\n"
    "        ${DEPS_DIR}/lib/libglew.dll.a\n"
    "        ${DEPS_DIR}/lib/libSDL3.dll.a\n"
    "        opengl32 gdi32 user32 winmm\n"
    "    )\n"
    "endif()\n"
    "\n"
    "add_executable(game_standalone ${CMAKE_SOURCE_DIR}/src/main.cpp)\n"
    "target_link_libraries(game_standalone PRIVATE\n"
    "    game\n"
    "    ${DEPS_DIR}/lib/libdruid.dll.a\n"
    "    ${DEPS_DIR}/lib/libglew.dll.a\n"
    "    ${DEPS_DIR}/lib/libSDL3.dll.a\n"
    "    opengl32 gdi32 user32 winmm\n"
    ")\n"
    "target_compile_definitions(game_standalone PRIVATE DRUID_APP_TITLE=\\\"${DRUID_PROJECT_NAME}\\\")\n"
    "set_target_properties(game_standalone PROPERTIES OUTPUT_NAME \"${DRUID_PROJECT_NAME}\")\n"
    "\n"
    "add_custom_command(TARGET game POST_BUILD\n"
    "    COMMAND ${CMAKE_COMMAND} -E copy_if_different\n"
    "        ${DEPS_DIR}/bin/libdruid.dll       $<TARGET_FILE_DIR:game>\n"
    "    COMMAND ${CMAKE_COMMAND} -E copy_if_different\n"
    "        ${DEPS_DIR}/bin/glew32.dll         $<TARGET_FILE_DIR:game>\n"
    "    COMMAND ${CMAKE_COMMAND} -E copy_if_different\n"
    "        ${DEPS_DIR}/bin/SDL3.dll           $<TARGET_FILE_DIR:game>\n"
    "    COMMAND ${CMAKE_COMMAND} -E copy_if_different\n"
    "        ${DEPS_DIR}/bin/libassimp-6.dll    $<TARGET_FILE_DIR:game>\n"
    ")\n";

// ============================================================================

b8 generateProjectFiles(const c8 *projectDir)
{
    if (!projectDir || projectDir[0] == '\0')
        return false;

    if (!writeTemplate(projectDir, "src/game.h",      TPL_GAME_H))      return false;
    if (!writeTemplate(projectDir, "src/game.cpp",    TPL_GAME_CPP))    return false;
    if (!writeTemplate(projectDir, "src/main.cpp",    TPL_MAIN_CPP))    return false;
    if (!writeTemplate(projectDir, "res/shader.vert", TPL_SHADER_VERT)) return false;
    if (!writeTemplate(projectDir, "res/shader.frag", TPL_SHADER_FRAG)) return false;
    if (!writeTemplate(projectDir, "CMakeLists.txt",  TPL_CMAKELISTS))  return false;

    if (!copyEngineFiles(projectDir))
    {
        ERROR("Failed to copy engine files to project");
        return false;
    }

    INFO("Generated project files in %s", projectDir);
    return true;
}

// ============================================================================

b8 buildProject(const c8 *projectDir, c8 *outLog, u32 logSize)
{
    if (!projectDir || projectDir[0] == '\0')
        return false;

    g_buildInProgress = true;
    outLog[0] = '\0';

    c8 buildDir[MAX_PATH_LENGTH];
    snprintf(buildDir, sizeof(buildDir), "%s/build", projectDir);
    createDir(buildDir);

    // configure
    c8 cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "cd /d \"%s/build\" && cmake .. -G \"MinGW Makefiles\" 2>&1",
             projectDir);

    FILE *pipe = (FILE *)platformPipeOpen(cmd);
    if (!pipe)
    {
        snprintf(outLog, logSize, "Failed to run cmake configure\n");
        g_buildInProgress = false;
        return false;
    }

    u32 off = 0;
    c8 line[512];
    while (fgets(line, sizeof(line), pipe))
    {
        u32 len = (u32)strlen(line);
        if (off + len < logSize - 1) { memcpy(outLog + off, line, len); off += len; }
    }
    outLog[off] = '\0';
    i32 ret = platformPipeClose(pipe);
    if (ret != 0) { g_buildInProgress = false; return false; }

    // build
    snprintf(cmd, sizeof(cmd),
             "cd /d \"%s/build\" && cmake --build . 2>&1", projectDir);

    pipe = (FILE *)platformPipeOpen(cmd);
    if (!pipe)
    {
        const c8 *msg = "Failed to run cmake build\n";
        u32 len = (u32)strlen(msg);
        if (off + len < logSize - 1) { memcpy(outLog + off, msg, len); off += len; }
        outLog[off] = '\0';
        g_buildInProgress = false;
        return false;
    }

    while (fgets(line, sizeof(line), pipe))
    {
        u32 len = (u32)strlen(line);
        if (off + len < logSize - 1) { memcpy(outLog + off, line, len); off += len; }
    }
    outLog[off] = '\0';
    ret = platformPipeClose(pipe);

    g_buildInProgress = false;
    return (ret == 0);
}

// ============================================================================

b8 buildStandalone(const c8 *projectDir, const c8 *scenePath, c8 *outLog, u32 logSize)
{
    if (!projectDir || projectDir[0] == '\0')
        return false;

    g_buildInProgress = true;
    outLog[0] = '\0';

    c8 buildDir[MAX_PATH_LENGTH];
    snprintf(buildDir, sizeof(buildDir), "%s/build", projectDir);
    createDir(buildDir);

    // configure (same as normal build)
    c8 cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "cd /d \"%s/build\" && cmake .. -G \"MinGW Makefiles\" 2>&1",
             projectDir);

    FILE *pipe = (FILE *)platformPipeOpen(cmd);
    if (!pipe)
    {
        snprintf(outLog, logSize, "Failed to run cmake configure\n");
        g_buildInProgress = false;
        return false;
    }

    u32 off = 0;
    c8 line[512];
    while (fgets(line, sizeof(line), pipe))
    {
        u32 len = (u32)strlen(line);
        if (off + len < logSize - 1) { memcpy(outLog + off, line, len); off += len; }
    }
    outLog[off] = '\0';
    i32 ret = platformPipeClose(pipe);
    if (ret != 0) { g_buildInProgress = false; return false; }

    // build the standalone target
    snprintf(cmd, sizeof(cmd),
             "cd /d \"%s/build\" && cmake --build . --target game_standalone 2>&1",
             projectDir);

    pipe = (FILE *)platformPipeOpen(cmd);
    if (!pipe)
    {
        const c8 *msg = "Failed to run cmake build\n";
        u32 len = (u32)strlen(msg);
        if (off + len < logSize - 1) { memcpy(outLog + off, msg, len); off += len; }
        outLog[off] = '\0';
        g_buildInProgress = false;
        return false;
    }

    while (fgets(line, sizeof(line), pipe))
    {
        u32 len = (u32)strlen(line);
        if (off + len < logSize - 1) { memcpy(outLog + off, line, len); off += len; }
    }
    outLog[off] = '\0';
    ret = platformPipeClose(pipe);
    if (ret != 0) { g_buildInProgress = false; return false; }

    // also build the shared lib (game_standalone links against it)
    snprintf(cmd, sizeof(cmd),
             "cd /d \"%s/build\" && cmake --build . --target game 2>&1",
             projectDir);
    pipe = (FILE *)platformPipeOpen(cmd);
    if (pipe)
    {
        while (fgets(line, sizeof(line), pipe))
        {
            u32 len = (u32)strlen(line);
            if (off + len < logSize - 1) { memcpy(outLog + off, line, len); off += len; }
        }
        outLog[off] = '\0';
        platformPipeClose(pipe);
    }

    // ── Package into projectDir/export/ ──
    c8 exportDir[MAX_PATH_LENGTH];
    snprintf(exportDir, sizeof(exportDir), "%s/export", projectDir);
    createDir(exportDir);

    writeStartupSceneConfig(projectDir, scenePath);
    writeStartupSceneConfig(exportDir, scenePath);

    c8 projectName[256];
    getProjectBaseName(projectDir, projectName, sizeof(projectName));

    // copy the standalone exe
    struct { const c8 *src; const c8 *dst; } exeFiles[] = {
#ifdef _WIN32
        {"bin/libgame.dll",         "export/libgame.dll"},
        {"deps/bin/libdruid.dll",   "export/libdruid.dll"},
        {"deps/bin/glew32.dll",     "export/glew32.dll"},
        {"deps/bin/SDL3.dll",       "export/SDL3.dll"},
        {"deps/bin/libassimp-6.dll","export/libassimp-6.dll"},
#else
        {"bin/game_standalone",     "export/game_standalone"},
        {"bin/libgame.so",          "export/libgame.so"},
        {"deps/bin/libdruid.so",    "export/libdruid.so"},
#endif
    };

    u32 fileCount = sizeof(exeFiles) / sizeof(exeFiles[0]);
    u32 copied = 0;

    {
        c8 srcPath[MAX_PATH_LENGTH], dstPath[MAX_PATH_LENGTH];
#ifdef _WIN32
        snprintf(srcPath, sizeof(srcPath), "%s/bin/%s.exe", projectDir, projectName);
        if (!fileExists(srcPath))
            snprintf(srcPath, sizeof(srcPath), "%s/bin/game_standalone.exe", projectDir);
        snprintf(dstPath, sizeof(dstPath), "%s/export/%s.exe", projectDir, projectName);
#else
        snprintf(srcPath, sizeof(srcPath), "%s/bin/%s", projectDir, projectName);
        if (!fileExists(srcPath))
            snprintf(srcPath, sizeof(srcPath), "%s/bin/game_standalone", projectDir);
        snprintf(dstPath, sizeof(dstPath), "%s/export/%s", projectDir, projectName);
#endif
        if (copyFileSingle(srcPath, dstPath))
            copied++;
    }

    for (u32 i = 0; i < fileCount; i++)
    {
        c8 srcPath[MAX_PATH_LENGTH], dstPath[MAX_PATH_LENGTH];
        snprintf(srcPath, sizeof(srcPath), "%s/%s", projectDir, exeFiles[i].src);
        snprintf(dstPath, sizeof(dstPath), "%s/%s", projectDir, exeFiles[i].dst);
        if (copyFileSingle(srcPath, dstPath))
            copied++;
    }

    // copy res/ and scenes/ directories
    {
        c8 src[MAX_PATH_LENGTH], dst[MAX_PATH_LENGTH];
        snprintf(src, sizeof(src), "%s/res", projectDir);
        snprintf(dst, sizeof(dst), "%s/export/res", projectDir);
        createDir(dst);
        platformDirCopyRecursive(src, dst);

        snprintf(src, sizeof(src), "%s/scenes", projectDir);
        snprintf(dst, sizeof(dst), "%s/export/scenes", projectDir);
        createDir(dst);
        platformDirCopyRecursive(src, dst);
    }

    INFO("Standalone build packaged to %s/export/ (%u files copied)", projectDir, copied);
    g_buildInProgress = false;
    return true;
}

// ============================================================================

void getEngineRoot(c8 *out, u32 size)
{
    platformGetExePath(out, size);
    // strip filename -> Editor/bin/
    stripLastComponent(out);
    // strip bin -> Editor/
    stripLastComponent(out);
    // strip Editor -> engine root
    stripLastComponent(out);
    normalizePath(out);
}

b8 copyEngineFiles(const c8 *projectDir)
{
    if (!projectDir || projectDir[0] == '\0')
        return false;

    c8 engineRoot[MAX_PATH_LENGTH];
    getEngineRoot(engineRoot, sizeof(engineRoot));

    // create target directories
    c8 dir[MAX_PATH_LENGTH];
    snprintf(dir, sizeof(dir), "%s/deps/include", projectDir);
    createDir(dir);
    snprintf(dir, sizeof(dir), "%s/deps/lib", projectDir);
    createDir(dir);
    snprintf(dir, sizeof(dir), "%s/deps/bin", projectDir);
    createDir(dir);

    // copy header directories recursively
    {
        c8 src[MAX_PATH_LENGTH], dst[MAX_PATH_LENGTH];
        snprintf(src, sizeof(src), "%s/include", engineRoot);
        snprintf(dst, sizeof(dst), "%s/deps/include", projectDir);
        platformDirCopyRecursive(src, dst);
    }

    // individual file copies: {engine_relative_src, project_relative_dst}
    struct { const c8 *src; const c8 *dst; } files[] = {
        // runtime DLLs
        {"bin/libdruid.dll",           "deps/bin/libdruid.dll"},
        {"bin/libassimp-6.dll",        "deps/bin/libassimp-6.dll"},
        {"deps/glew32.dll",            "deps/bin/glew32.dll"},
        {"deps/SDL3.dll",              "deps/bin/SDL3.dll"},
        {"deps/assimp-vc143-mt.dll",   "deps/bin/assimp-vc143-mt.dll"},
        // mingw import libs
        {"bin/libdruid.dll.a",         "deps/lib/libdruid.dll.a"},
        {"deps/libglew.dll.a",         "deps/lib/libglew.dll.a"},
        {"deps/libSDL3.dll.a",         "deps/lib/libSDL3.dll.a"},
        {"deps/libassimp.dll.a",       "deps/lib/libassimp.dll.a"},
        // msvc import libs + def files
        {"bin/druid.lib",              "deps/lib/druid.lib"},
        {"bin/druid.exp",              "deps/lib/druid.exp"},
        {"bin/libdruid.def",           "deps/lib/libdruid.def"},
        {"deps/glew32.lib",            "deps/lib/glew32.lib"},
        {"deps/glew32.def",            "deps/lib/glew32.def"},
    };

    u32 fileCount = sizeof(files) / sizeof(files[0]);
    u32 copied = 0;
    for (u32 i = 0; i < fileCount; i++)
    {
        c8 srcPath[MAX_PATH_LENGTH], dstPath[MAX_PATH_LENGTH];
        snprintf(srcPath, sizeof(srcPath), "%s/%s", engineRoot, files[i].src);
        snprintf(dstPath, sizeof(dstPath), "%s/%s", projectDir, files[i].dst);
        if (copyFileSingle(srcPath, dstPath))
            copied++;
    }

    INFO("Copied engine files to %s (%u/%u files)", projectDir, copied, fileCount);
    return true;
}

b8 updateProject(const c8 *projectDir, c8 *outLog, u32 logSize)
{
    if (!projectDir || projectDir[0] == '\0')
        return false;

    outLog[0] = '\0';
    g_buildInProgress = true;

    // build the engine first
    c8 engineRoot[MAX_PATH_LENGTH];
    getEngineRoot(engineRoot, sizeof(engineRoot));

    c8 cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "cd /d \"%s/build\" && cmake --build . 2>&1", engineRoot);

    FILE *pipe = (FILE *)platformPipeOpen(cmd);
    if (!pipe)
    {
        snprintf(outLog, logSize, "Failed to build engine\n");
        g_buildInProgress = false;
        return false;
    }

    u32 off = 0;
    c8 line[512];
    while (fgets(line, sizeof(line), pipe))
    {
        u32 len = (u32)strlen(line);
        if (off + len < logSize - 1) { memcpy(outLog + off, line, len); off += len; }
    }
    outLog[off] = '\0';
    i32 ret = platformPipeClose(pipe);

    if (ret != 0)
    {
        g_buildInProgress = false;
        return false;
    }

    // copy updated files
    b8 ok = copyEngineFiles(projectDir);
    g_buildInProgress = false;
    return ok;
}

// ============================================================================

b8 loadGameDLL(const c8 *dllPath, GameDLL *out)
{
    if (!dllPath || !out) return false;
    memset(out, 0, sizeof(GameDLL));

    // dllLoad copies the file to a temp path so it isn't locked during rebuilds
    if (!dllLoad(dllPath, &out->dll))
    {
        ERROR("Failed to load game DLL: %s", dllPath);
        return false;
    }

    GetPluginFn getPlugin = (GetPluginFn)dllSymbol(&out->dll, "druidGetPlugin");
    if (!getPlugin)
    {
        ERROR("DLL missing druidGetPlugin export");
        unloadGameDLL(out);
        return false;
    }

    getPlugin(&out->plugin);

    if (!out->plugin.init || !out->plugin.update ||
        !out->plugin.render || !out->plugin.destroy)
    {
        ERROR("Plugin returned NULL function pointers");
        unloadGameDLL(out);
        return false;
    }

    out->loaded = true;
    INFO("Loaded game DLL: %s", dllPath);
    return true;
}

void unloadGameDLL(GameDLL *dll)
{
    if (!dll || !dll->dll.loaded) return;

    dllUnload(&dll->dll);
    memset(dll, 0, sizeof(GameDLL));
}