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
    "#define GAME_EXPORT\n"
    "#include \"game.h\"\n"
    "#include <stdio.h>\n"
    "#include <string.h>\n"
    "\n"
    "// ---- game state ----\n"
    "static c8       g_projectDir[512] = {0};\n"
    "static Camera   g_cam = {0};\n"
    "static u32      g_defaultShader = 0;\n"
    "static f32      g_time = 0.0f;\n"
    "\n"
    "// scene data loaded from .drsc files\n"
    "static SceneData g_scene = {0};\n"
    "static b8        g_sceneLoaded = false;\n"
    "\n"
    "// entity field pointers (extracted from the loaded scene archetype)\n"
    "static Vec3 *g_positions  = NULL;\n"
    "static Vec4 *g_rotations  = NULL;\n"
    "static Vec3 *g_scales     = NULL;\n"
    "static b8   *g_isActive   = NULL;\n"
    "static u32  *g_modelIDs   = NULL;\n"
    "static u32  *g_shaderH    = NULL;\n"
    "static u32  *g_matIDs     = NULL;\n"
    "static u32   g_entityCount = 0;\n"
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
    "    g_positions  = (Vec3 *)fields[0];\n"
    "    g_rotations  = (Vec4 *)fields[1];\n"
    "    g_scales     = (Vec3 *)fields[2];\n"
    "    g_isActive   = (b8 *)  fields[3];\n"
    "    g_modelIDs   = (u32 *) fields[5];\n"
    "    g_shaderH    = (u32 *) fields[6];\n"
    "    g_matIDs     = (u32 *) fields[7];\n"
    "    g_entityCount = g_scene.archetypes[0].arena[0].count;\n"
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
    "// ---- plugin callbacks ----\n"
    "\n"
    "static void gameInit(const c8 *projectDir)\n"
    "{\n"
    "    strncpy(g_projectDir, projectDir, sizeof(g_projectDir) - 1);\n"
    "\n"
    "    initCamera(&g_cam, (Vec3){0.0f, 2.0f, 8.0f},\n"
    "               70.0f, 16.0f / 9.0f, 0.1f, 100.0f);\n"
    "    createCoreShaderUBO();\n"
    "\n"
    "    u32 idx = 0;\n"
    "    findInMap(&resources->shaderIDs, \"default\", &idx);\n"
    "    g_defaultShader = resources->shaderHandles[idx];\n"
    "\n"
    "    // Load the starting scene (change the filename to your scene)\n"
    "    loadGameScene(\"scene.drsc\");\n"
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
    "\n"
    "    Mat4 view = getView(&g_cam, false);\n"
    "    updateCoreShaderUBO(g_time, &g_cam.pos, &view, &g_cam.projection);\n"
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
    "        updateShaderModel(g_defaultShader, t);\n"
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
    "                MaterialUniforms unis = getMaterialUniforms(g_defaultShader);\n"
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
    "\n"
    "static GamePlugin plugin = {0};\n"
    "\n"
    "static void _init(void)    { plugin.init(\".\"); }\n"
    "static void _update(f32 dt){ plugin.update(dt);  }\n"
    "static void _render(f32 dt){ plugin.render(dt);  }\n"
    "static void _destroy(void) { plugin.destroy();   }\n"
    "\n"
    "int main(int argc, char **argv)\n"
    "{\n"
    "    druidGetPlugin(&plugin);\n"
    "    Application *app = createApplication(\"My Game\", _init, _update, _render, _destroy);\n"
    "    app->width  = 1280;\n"
    "    app->height = 720;\n"
    "    run(app);\n"
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
    "target_link_libraries(game_standalone PRIVATE game ${DEPS_DIR}/lib/libdruid.dll.a)\n"
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