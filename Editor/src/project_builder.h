#pragma once
#include <druid.h>

/*
    project_builder.h
    
    Scaffolds new projects, builds them into DLLs, and loads them at runtime.
    
    A game DLL exports a single function: druidGetPlugin() which fills in a
    GamePlugin struct with init/update/render/destroy pointers. The editor
    calls those at the right times.
*/

// the game DLL exports this struct
typedef void (*PluginInitFn)(const c8 *projectDir);
typedef void (*PluginUpdateFn)(f32 dt);
typedef void (*PluginRenderFn)(f32 dt);
typedef void (*PluginDestroyFn)(void);

typedef struct GamePlugin
{
    PluginInitFn    init;
    PluginUpdateFn  update;
    PluginRenderFn  render;
    PluginDestroyFn destroy;
} GamePlugin;

// the DLL must export this function
typedef void (*GetPluginFn)(GamePlugin *out);

// loaded DLL handle
typedef struct GameDLL
{
    DLLHandle   dll;      // unified DLL handle (platform.c)
    GamePlugin  plugin;
    b8          loaded;
} GameDLL;

// resolve the engine root from the editor exe path
void getEngineRoot(c8 *out, u32 size);

// write default project files into projectDir (src/, res/, CMakeLists.txt)
b8 generateProjectFiles(const c8 *projectDir);

// copy engine headers, libs, and DLLs into projectDir/deps/
b8 copyEngineFiles(const c8 *projectDir);

// build the engine then copy updated files into the project
b8 updateProject(const c8 *projectDir, c8 *outLog, u32 logSize);

// cmake configure + build. log goes into outLog.
b8 buildProject(const c8 *projectDir, c8 *outLog, u32 logSize);

// load/unload game DLL
b8   loadGameDLL(const c8 *dllPath, GameDLL *out);
void unloadGameDLL(GameDLL *dll);

// editor state
extern GameDLL g_gameDLL;
extern b8      g_gameRunning;
extern c8      g_buildLog[4096];
extern b8      g_buildInProgress;
