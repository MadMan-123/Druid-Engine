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

static b8 readCMakeCacheGenerator(const c8 *buildDir, c8 *outGen, u32 outSize)
{
    if (!buildDir || !outGen || outSize == 0)
        return false;

    outGen[0] = '\0';

    c8 cachePath[MAX_PATH_LENGTH];
    snprintf(cachePath, sizeof(cachePath), "%s/CMakeCache.txt", buildDir);

    FILE *f = fopen(cachePath, "r");
    if (!f)
        return false;

    c8 line[512];
    while (fgets(line, sizeof(line), f))
    {
        const c8 *k1 = "CMAKE_GENERATOR:INTERNAL=";
        const c8 *k2 = "CMAKE_GENERATOR:STRING=";
        const c8 *val = NULL;

        if (strncmp(line, k1, strlen(k1)) == 0)
            val = line + strlen(k1);
        else if (strncmp(line, k2, strlen(k2)) == 0)
            val = line + strlen(k2);

        if (!val)
            continue;

        strncpy(outGen, val, outSize - 1);
        outGen[outSize - 1] = '\0';

        u32 len = (u32)strlen(outGen);
        while (len > 0 && (outGen[len - 1] == '\n' || outGen[len - 1] == '\r'))
        {
            outGen[len - 1] = '\0';
            len--;
        }

        fclose(f);
        return (outGen[0] != '\0');
    }

    fclose(f);
    return false;
}

static void buildConfigureCommand(const c8 *projectDir, c8 *cmd, u32 cmdSize)
{
    c8 buildDir[MAX_PATH_LENGTH];
    snprintf(buildDir, sizeof(buildDir), "%s/build", projectDir);

    c8 generator[128];
    if (readCMakeCacheGenerator(buildDir, generator, sizeof(generator)))
    {
        snprintf(cmd, cmdSize,
                 "cd /d \"%s/build\" && cmake .. -G \"%s\" 2>&1",
                 projectDir, generator);
    }
    else
    {
        // No prior generator cached: let CMake select the local default.
        snprintf(cmd, cmdSize,
                 "cd /d \"%s/build\" && cmake .. 2>&1",
                 projectDir);
    }
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
static const c8 *TPL_GAME_CPP =
    "#include \"game.h\"\n"
    "\n"
    "static void gameInit(const c8 *projectDir)\n"
    "{\n"
    "    runtimeCreate(projectDir, runtimeDefaultConfig());\n"
    "}\n"
    "\n"
    "static void gameUpdate(f32 dt)\n"
    "{\n"
    "    runtimeUpdate(runtime, dt);\n"
    "}\n"
    "\n"
    "static void gameRender(f32 dt)\n"
    "{\n"
    "    runtimeBeginScenePass(runtime, dt);\n"
    "    runtimeEndScenePass(runtime);\n"
    "}\n"
    "\n"
    "static void gameDestroy(void)\n"
    "{\n"
    "    runtimeDestroy(runtime);\n"
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
// standalone launcher — all setup is handled by runtimeCreate inside plugin.init
static const c8 *TPL_MAIN_CPP =
    "#include \"game.h\"\n"
    "\n"
    "#ifndef DRUID_APP_TITLE\n"
    "#define DRUID_APP_TITLE \"Game\"\n"
    "#endif\n"
    "\n"
    "static GamePlugin   plugin = {0};\n"
    "static Application *g_app  = NULL;\n"
    "\n"
    "static void _init(void)        { plugin.init(\".\"); }\n"
    "static void _update(f32 dt)    { plugin.update(dt); }\n"
    "static void _render(f32 dt)    { plugin.render(dt); }\n"
    "static void _destroy(void)     { plugin.destroy(); }\n"
    "\n"
    "int main(int argc, char **argv)\n"
    "{\n"
    "    (void)argc; (void)argv;\n"
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
    "out mat3 TBN;\n"
    "\n"
    "void main()\n"
    "{\n"
    "    vec4 worldPos = model * vec4(position, 1.0);\n"
    "    FragPos = worldPos.xyz;\n"
    "\n"
    "    mat3 normalMatrix = transpose(inverse(mat3(model)));\n"
    "    Normal = normalize(normalMatrix * normal);\n"
    "\n"
    "    vec3 N = Normal;\n"
    "    vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);\n"
    "    vec3 T = normalize(cross(up, N));\n"
    "    vec3 B = cross(N, T);\n"
    "    TBN = mat3(T, B, N);\n"
    "\n"
    "    tc = texCoord;\n"
    "    gl_Position = projection * view * worldPos;\n"
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
    "foreach(_cfg Debug Release RelWithDebInfo MinSizeRel)\n"
    "    string(TOUPPER ${_cfg} _CFG)\n"
    "    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${_CFG} ${CMAKE_SOURCE_DIR}/bin)\n"
    "    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${_CFG} ${CMAKE_SOURCE_DIR}/bin)\n"
    "    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${_CFG} ${CMAKE_SOURCE_DIR}/bin)\n"
    "endforeach()\n"
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
    buildConfigureCommand(projectDir, cmd, sizeof(cmd));

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
    buildConfigureCommand(projectDir, cmd, sizeof(cmd));

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

    // Try ninja at the engine root first (preferred), fall back to cmake build dir
    c8 cmd[2048];
    c8 ninjaCheck[MAX_PATH_LENGTH];
    snprintf(ninjaCheck, sizeof(ninjaCheck), "%s/build.ninja", engineRoot);
    FILE *nf = fopen(ninjaCheck, "r");
    if (nf)
    {
        fclose(nf);
        // Use full path to ninja in case it's not in PATH
        snprintf(cmd, sizeof(cmd),
                 "cd /d \"%s\" && C:\\msys64\\mingw64\\bin\\ninja.exe 2>&1", engineRoot);
    }
    else
    {
        snprintf(cmd, sizeof(cmd),
                 "cd /d \"%s/build\" && cmake --build . 2>&1", engineRoot);
    }

    FILE *pipe = (FILE *)platformPipeOpen(cmd);
    if (!pipe)
    {
        snprintf(outLog, logSize, "Failed to open build pipe\n");
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

    // Write full log to a file for debugging
    FILE *debugLog = fopen("build_log.txt", "w");
    if (debugLog)
    {
        fprintf(debugLog, "Build command: %s\n", cmd);
        fprintf(debugLog, "Exit code: %d\n", ret);
        fprintf(debugLog, "Output:\n%s\n", outLog);
        fclose(debugLog);
        INFO("Build log written to build_log.txt");
    }

    if (ret != 0)
    {
        u32 len = (u32)strlen(outLog);
        snprintf(outLog + len, logSize - len,
                 "\n--- Engine build failed (exit code %d) ---\nCheck build_log.txt for details\n", ret);
        g_buildInProgress = false;
        return false;
    }

    // copy updated files
    b8 ok = copyEngineFiles(projectDir);
    if (!ok)
    {
        snprintf(outLog, logSize, "Failed to copy engine files to project\n");
    }
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