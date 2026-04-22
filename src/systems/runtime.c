#include "../../include/druid.h"
#include <string.h>

GameRuntime *runtime = NULL;

DAPI RuntimeConfig runtimeDefaultConfig(void)
{
    RuntimeConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.gravity         = (Vec3){0.0f, -9.81f, 0.0f};
    cfg.physicsTimestep = 1.0f / 60.0f;
    cfg.camFov          = 70.0f;
    cfg.camNear         = 0.1f;
    cfg.camFar          = 1000.0f;
    cfg.camStartPos     = (Vec3){0.0f, 2.0f, 8.0f};
    cfg.camAspect       = 16.0f / 9.0f;
    return cfg;
}

static void runtimeLoadSkybox_(GameRuntime *rt)
{
    const c8 *suffixes[6] = {"right.jpg","left.jpg","top.jpg","bottom.jpg","front.jpg","back.jpg"};
    c8 paths[6][MAX_PATH_LENGTH];
    const c8 *faces[6];
    for (u32 i = 0; i < 6; i++)
    {
        snprintf(paths[i], sizeof(paths[i]), "./res/Textures/Skybox/%s", suffixes[i]);
        if (!fileExists(paths[i])) return;
        faces[i] = paths[i];
    }
    rt->skyboxTex    = createCubeMapTexture(faces, 6);
    rt->skyboxMesh   = createSkyboxMesh();
    rt->skyboxShader = createGraphicsProgram("./res/Skybox.vert", "./res/Skybox.frag");
}

static void runtimeDrawScene_(void)
{
    if (!sceneRuntime || !sceneRuntime->loaded || !renderer || !resources) return;

    u32 shader = renderer->defaultShader;
    for (u32 id = 0; id < sceneRuntime->entityCount; id++)
    {
        if (!sceneRuntime->isActive  || !sceneRuntime->isActive[id])  continue;
        if (!sceneRuntime->modelIDs) continue;
        u32 modelID = sceneRuntime->modelIDs[id];
        if (modelID >= resources->modelUsed) continue;

        Model    *model = &resources->modelBuffer[modelID];
        Vec3 pos = sceneRuntime->positions ? sceneRuntime->positions[id] : v3Zero;
        Vec4 rot = sceneRuntime->rotations ? sceneRuntime->rotations[id] : quatIdentity();
        Vec3 scl = sceneRuntime->scales    ? sceneRuntime->scales[id]    : v3One;
        Transform t = {pos, rot, scl};
        updateShaderModel(shader, t);

        for (u32 m = 0; m < model->meshCount; m++)
        {
            u32 mi   = model->meshIndices[m];
            if (mi >= resources->meshUsed) continue;
            u32 matI = model->materialIndices[m];
            if (sceneRuntime->materialIDs && sceneRuntime->materialIDs[id] != (u32)-1)
                matI = sceneRuntime->materialIDs[id];
            if (matI < resources->materialUsed)
            {
                MaterialUniforms unis = getMaterialUniforms(shader);
                updateMaterial(&resources->materialBuffer[matI], &unis);
            }
            drawMesh(&resources->meshBuffer[mi]);
        }
    }
}

DAPI GameRuntime *runtimeCreate(const c8 *projectDir, RuntimeConfig cfg)
{
    if (runtime) return runtime;

    runtime = (GameRuntime *)dalloc(sizeof(GameRuntime), MEM_TAG_GAME);
    if (!runtime) return NULL;
    memset(runtime, 0, sizeof(GameRuntime));

    strncpy(runtime->projectDir, projectDir, MAX_PATH_LENGTH - 1);
    runtime->config       = cfg;
    runtime->standaloneMode = (strcmp(projectDir, ".") == 0);

    // Scene — always load if not already available (editor play mode never pre-loads sceneRuntime)
    if (!sceneRuntime || !sceneRuntime->loaded)
        sceneRuntimeInit(projectDir);

    if (runtime->standaloneMode)
    {
        // Renderer — created by standalone launcher via the global display singleton
        if (!renderer && display)
        {
            f32 aspect = (cfg.camAspect > 0.0f) ? cfg.camAspect : 16.0f / 9.0f;
            Renderer *r = createRenderer(display, cfg.camFov, cfg.camNear, cfg.camFar, 8, 16, 8);
            if (r)
            {
                u32 idx = 0;
                findInMap(&resources->shaderIDs, "default", &idx);
                r->defaultShader = resources->shaderHandles[idx];
                u32 slot = rendererAcquireCamera(r, cfg.camStartPos, cfg.camFov,
                                                 aspect, cfg.camNear, cfg.camFar);
                rendererSetActiveCamera(r, slot);
                rendererEnableDeferred(r, (u32)display->screenWidth, (u32)display->screenHeight);
            }
        }

        // Physics
        physInit(cfg.gravity, cfg.physicsTimestep);
        runtime->world = physicsWorld;

        if (physicsWorld && sceneRuntime)
            physAutoRegisterScene(&sceneRuntime->data);

        // Deferred pipeline shaders
        runtime->gbufferShader  = createGraphicsProgram("./res/gbuffer.vert",
                                                         "./res/gbuffer.frag");
        runtime->lightingShader = createGraphicsProgram("./res/deferred_lighting.vert",
                                                         "./res/deferred_lighting.frag");

        // Skybox (optional — skipped silently if textures absent)
        runtimeLoadSkybox_(runtime);
    }

    // Always track renderer and scene references (editor or standalone)
    runtime->renderer = renderer;
    runtime->scene = sceneRuntime;
    if (renderer)
        runtime->camera = (Camera *)bufferGet(&renderer->cameras, renderer->activeCamera);

    return runtime;
}

DAPI void runtimeRegisterArchetype(GameRuntime *rt, Archetype *arch)
{
    if (!rt || !arch) return;
    b8 isArchetypePhysicsBody = FLAG_CHECK(arch->flags, ARCH_PHYSICS_BODY);
    if (physicsWorld && isArchetypePhysicsBody)
    {

        physRegisterArchetype(physicsWorld, arch);
        rt->physRegistered = true;
        return;
    }
    if (rt->pendingCount < RUNTIME_MAX_PENDING_ARCHETYPES)
        rt->pendingArchetypes[rt->pendingCount++] = arch;
}

DAPI void runtimeUpdate(GameRuntime *rt, f32 dt)
{
    if (!rt) return;
    rt->scene = sceneRuntime;

    // Flush pending physics registrations once physicsWorld is available (editor creates it after plugin.init)
    if (!rt->physRegistered && physicsWorld && rt->pendingCount > 0)
    {
        for (u32 i = 0; i < rt->pendingCount; i++)
            physRegisterArchetype(physicsWorld, rt->pendingArchetypes[i]);
        rt->pendingCount   = 0;
        rt->physRegistered = true;
    }

    if (!rt->standaloneMode) return;

    // Step physics
    if (physicsWorld)
        physWorldStep(physicsWorld, dt);

    // Push camera + time to UBO — called at end of gameUpdate so camera is already moved
    if (renderer)
        rendererBeginFrame(renderer, dt);
}

DAPI void runtimeBeginScenePass(GameRuntime *rt, f32 dt)
{
    (void)dt;
    if (!rt->standaloneMode || !renderer) return;

    if (renderer->useDeferredRendering && rt->gbufferShader)
    {
        rendererBeginDeferredPass(renderer);
        renderer->defaultShader = rt->gbufferShader;
        glUseProgram(rt->gbufferShader);
    }

    runtimeDrawScene_();
}

DAPI void runtimeEndScenePass(GameRuntime *rt)
{
    if (!rt->standaloneMode || !renderer) return;

    if (renderer->useDeferredRendering && rt->lightingShader)
    {
        rendererEndDeferredPass(renderer);
        rendererLightingPass(renderer, rt->lightingShader);

        // Restore default forward shader for anything drawn after (gizmos, UI, etc.)
        u32 idx = 0;
        findInMap(&resources->shaderIDs, "default", &idx);
        renderer->defaultShader = resources->shaderHandles[idx];
    }

    // Skybox
    if (rt->skyboxMesh && rt->skyboxTex && rt->skyboxShader)
    {
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_FALSE);
        glUseProgram(rt->skyboxShader);
        glBindVertexArray(rt->skyboxMesh->vao);
        glBindTexture(GL_TEXTURE_CUBE_MAP, rt->skyboxTex);
        glDrawArrays(GL_TRIANGLES, 0, 36);
        glBindVertexArray(0);
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LESS);
    }
}

DAPI void runtimeDestroy(GameRuntime *rt)
{
    if (!rt) return;

    if (rt->standaloneMode)
    {
        physShutdown();
        sceneRuntimeDestroy();
        if (rt->skyboxMesh)     { freeMesh(rt->skyboxMesh);      rt->skyboxMesh     = NULL; }
        if (rt->skyboxTex)      { freeTexture(rt->skyboxTex);    rt->skyboxTex      = 0;    }
        if (rt->skyboxShader)   { freeShader(rt->skyboxShader);  rt->skyboxShader   = 0;    }
        if (rt->gbufferShader)  { freeShader(rt->gbufferShader); rt->gbufferShader  = 0;    }
        if (rt->lightingShader) { freeShader(rt->lightingShader);rt->lightingShader = 0;    }
    }

    dfree(rt, sizeof(GameRuntime), MEM_TAG_GAME);
    runtime = NULL;
}

DAPI b8 changeScene(GameRuntime *rt, const c8 *sceneName)
{
    if (!rt || !sceneName) return false;

    c8 path[MAX_PATH_LENGTH];
    snprintf(path, sizeof(path), "%s/scenes/%s", rt->projectDir, sceneName);

    if (!sceneRuntimeLoad(path)) return false;

    rt->scene = sceneRuntime;

    if (physicsWorld && sceneRuntime)
        physAutoRegisterScene(&sceneRuntime->data);

    return true;
}

DAPI Camera *getActiveCamera(GameRuntime *rt)
{
    if (!rt || !rt->renderer) return NULL;
    rt->camera = (Camera *)bufferGet(&rt->renderer->cameras, rt->renderer->activeCamera);
    return rt->camera;
}
