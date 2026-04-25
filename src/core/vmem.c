
#include "../../include/druid.h"

#if PLATFORM_WINDOWS
#pragma push_macro("ERROR")
#undef ERROR
#include <windows.h>
#pragma pop_macro("ERROR")
#endif

#if PLATFORM_LINUX || PLATFORM_UNIX || PLATFORM_APPLE
#include <sys/mman.h>
#include <unistd.h>
#endif

//=====================================================================================================================
// Platform virtual memory
static void *platformVMemAlloc(u64 size)
{
#if PLATFORM_WINDOWS
    void *ptr = VirtualAlloc(NULL, (SIZE_T)size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!ptr)
    {
        ERROR("platformVMemAlloc: VirtualAlloc failed (size %llu, err %lu)",
              (unsigned long long)size, GetLastError());
    }
    return ptr;
#elif PLATFORM_LINUX || PLATFORM_UNIX || PLATFORM_APPLE
    void *ptr = mmap(NULL, size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED)
    {
        ERROR("platformVMemAlloc: mmap failed (size %llu)", (unsigned long long)size);
        return NULL;
    }
    return ptr;
#else
    return calloc(1, size);
#endif
}

static void platformVMemFree(void *ptr, u64 size)
{
    if (!ptr) return;
#if PLATFORM_WINDOWS
    (void)size;
    VirtualFree(ptr, 0, MEM_RELEASE);
#elif PLATFORM_LINUX || PLATFORM_UNIX || PLATFORM_APPLE
    munmap(ptr, size);
#else
    (void)size;
    free(ptr);
#endif
}

//=====================================================================================================================
// Tag → Arena mapping

static const MemArenaID tagToArena[MEM_TAG_MAX] = {
    MEM_ARENA_GENERAL,    // UNKNOWN
    MEM_ARENA_GENERAL,    // ARRAY
    MEM_ARENA_GENERAL,    // ARENA
    MEM_ARENA_GENERAL,    // BUFFER
    MEM_ARENA_GENERAL,    // STRING
    MEM_ARENA_ECS,        // ECS
    MEM_ARENA_ECS,        // ARCHETYPE
    MEM_ARENA_ECS,        // SCENE
    MEM_ARENA_RENDERER,   // RENDERER
    MEM_ARENA_RENDERER,   // TEXTURE
    MEM_ARENA_RENDERER,   // MESH
    MEM_ARENA_RENDERER,   // SHADER
    MEM_ARENA_RENDERER,   // MATERIAL
    MEM_ARENA_PHYSICS,    // PHYSICS
    MEM_ARENA_GENERAL,    // TEMP
    MEM_ARENA_GENERAL,    // EDITOR
    MEM_ARENA_GENERAL,    // GAME
    MEM_ARENA_RENDERER,   // MODEL
    MEM_ARENA_RENDERER,   // GEOMETRY_BUFFER
    MEM_ARENA_GENERAL,    // AUDIO
};

//=====================================================================================================================
// Global state

static MemorySystem memState = {0};
MemorySystem *g_memory = NULL;

static inline u64 alignUp(u64 value, u64 alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

//=====================================================================================================================
// Default config

MemoryConfig memDefaultConfig(void)
{
    MemoryConfig cfg = {0};
    cfg.totalMB = 896;
    cfg.arenaMB[MEM_ARENA_GENERAL]  = 256;
    cfg.arenaMB[MEM_ARENA_ECS]      = 256;
    cfg.arenaMB[MEM_ARENA_RENDERER] = 96;
    cfg.arenaMB[MEM_ARENA_PHYSICS]  = 128;
    cfg.arenaMB[MEM_ARENA_FRAME]    = 128;
    return cfg;
}

//=====================================================================================================================
// Init / Shutdown

b8 memorySystemInit(MemoryConfig *config)
{
    if (g_memory)
    {
        WARN("memorySystemInit: already initialized");
        return false;
    }

    if (!config)
    {
        ERROR("memorySystemInit: NULL config");
        return false;
    }

    // compute required total from arena sizes
    u64 requiredMB = 0;
    for (u32 i = 0; i < MEM_ARENA_COUNT; i++)
        requiredMB += config->arenaMB[i];

    if (requiredMB > config->totalMB)
    {
        WARN("memorySystemInit: arena sum (%llu MB) exceeds total (%llu MB), expanding total",
             (unsigned long long)requiredMB, (unsigned long long)config->totalMB);
        config->totalMB = requiredMB;
    }

    u64 totalBytes = config->totalMB * 1024ULL * 1024ULL;

    void *block = platformVMemAlloc(totalBytes);
    if (!block)
    {
        ERROR("memorySystemInit: failed to allocate %llu MB", (unsigned long long)config->totalMB);
        return false;
    }

    memset(&memState, 0, sizeof(MemorySystem));
    memState.totalSize = totalBytes;
    memState.block = block;

    // carve arenas sequentially from the block
    u64 offset = 0;
    for (u32 i = 0; i < MEM_ARENA_COUNT; i++)
    {
        u64 arenaBytes = config->arenaMB[i] * 1024ULL * 1024ULL;
        memState.arenas[i].data = (u8 *)block + offset;
        memState.arenas[i].size = arenaBytes;
        memState.arenas[i].used = 0;
        offset += arenaBytes;
    }

    g_memory = &memState;

    INFO("Memory system initialized: %llu MB total", (unsigned long long)config->totalMB);
    for (u32 i = 0; i < MEM_ARENA_COUNT; i++)
    {
        INFO("  %-10s %llu MB", memGetArenaName((MemArenaID)i),
             (unsigned long long)config->arenaMB[i]);
    }

    return true;
}

void memorySystemShutdown(void)
{
    if (!g_memory) return;

    if (memState.allocCount > 0)
    {
        WARN("memorySystemShutdown: %u allocations still live (%llu bytes)",
             memState.allocCount, (unsigned long long)memState.totalAllocated);
    }

    platformVMemFree(memState.block, memState.totalSize);
    memset(&memState, 0, sizeof(MemorySystem));
    g_memory = NULL;
}

void memorySystemReset(void)
{
    if (!g_memory) return;

    INFO("memorySystemReset: clearing all arenas (%llu bytes used)",
         (unsigned long long)memState.totalAllocated);

    // Reset all arena bump pointers to zero — all prior pointers become invalid
    for (u32 i = 0; i < MEM_ARENA_COUNT; i++)
        memState.arenas[i].used = 0;

    // Reset stats
    memState.totalAllocated = 0;
    memState.allocCount = 0;
    memset(memState.taggedAllocs, 0, sizeof(memState.taggedAllocs));
    memset(memState.taggedAllocsFrame, 0, sizeof(memState.taggedAllocsFrame));
    memset(memState.taggedFreesFrame, 0, sizeof(memState.taggedFreesFrame));
}

void memArenaReset(MemArenaID arena)
{
    if (!g_memory || arena >= MEM_ARENA_COUNT) return;

    u64 was = memState.arenas[arena].used;
    memState.arenas[arena].used = 0;

    // Adjust global stats — subtract the arena's contribution
    if (memState.totalAllocated >= was)
        memState.totalAllocated -= was;
    else
        memState.totalAllocated = 0;

    // Sync profiler live-bytes counter so CSV heap_live_bytes is accurate
    profileCountFree(was);

    DEBUG("memArenaReset: %s arena cleared (%llu bytes reclaimed)",
          memGetArenaName(arena), (unsigned long long)was);
}

//=====================================================================================================================
// Allocator bump into the appropriate arena

void *dalloc(u64 size, MemTag tag)
{
    if (size == 0) return NULL;

    // fallback to malloc if memory system not yet initialized
    if (!g_memory) return calloc(1, size);

    u64 aligned = alignUp(size, MEM_ALIGNMENT);
    MemArenaID aid = (tag < MEM_TAG_MAX) ? tagToArena[tag] : MEM_ARENA_GENERAL;
    Arena *arena = &memState.arenas[aid];

    if (arena->used + aligned > arena->size)
    {
        ERROR("dalloc: %s arena full (requested %llu, used %llu / %llu)",
              memGetArenaName(aid),
              (unsigned long long)aligned,
              (unsigned long long)arena->used,
              (unsigned long long)arena->size);
        return NULL;
    }

    void *ptr = (u8 *)arena->data + arena->used;
    arena->used += aligned;

    // stats
    memState.totalAllocated += aligned;
    memState.allocCount++;
    if (tag < MEM_TAG_MAX)
    {
        memState.taggedAllocs[tag] += aligned;
        memState.taggedAllocsFrame[tag] += aligned;
    }

    // profiler frame tracking
    profileCountAlloc(aligned);

    memset(ptr, 0, aligned);
    return ptr;
}

void dfree(void *block, u64 size, MemTag tag)
{
    if (!block || size == 0) return;

    // if memory system not initialized, this was a calloc fallback
    if (!g_memory) { free(block); return; }

    // validate pointer is within our block
    u8 *start = (u8 *)memState.block;
    u8 *end   = start + memState.totalSize;
    u8 *ptr   = (u8 *)block;

    if (ptr < start || ptr >= end)
    {
        // allocated before memory system, use free
        free(block);
        return;
    }

    u64 aligned = alignUp(size, MEM_ALIGNMENT);

    // stats only, arenas don't reclaim individual allocations
    memState.totalAllocated -= aligned;
    memState.allocCount--;
    if (tag < MEM_TAG_MAX)
    {
        memState.taggedAllocs[tag] -= aligned;
        memState.taggedFreesFrame[tag] += aligned;
    }

    // don't subtract live bytes, arena memory freed in bulk by memArenaReset
    profileCountFree(0);
}

//=====================================================================================================================
// Frame allocator

void *frameAlloc(u64 size)
{
    if (size == 0) return NULL;
    if (!g_memory) return calloc(1, size);

    u64 aligned = alignUp(size, MEM_ALIGNMENT);
    Arena *frame = &memState.arenas[MEM_ARENA_FRAME];

    if (frame->used + aligned > frame->size)
    {
        ERROR("frameAlloc: out of frame memory (%llu / %llu)",
              (unsigned long long)(frame->used + aligned),
              (unsigned long long)frame->size);
        return NULL;
    }

    void *ptr = (u8 *)frame->data + frame->used;
    frame->used += aligned;
    memset(ptr, 0, aligned);
    return ptr;
}

void frameReset(void)
{
    if (!g_memory) return;
    memState.arenas[MEM_ARENA_FRAME].used = 0;
}

//=====================================================================================================================
// Stats / editor queries

static const c8 *tagNames[MEM_TAG_MAX] = {
    "UNKNOWN",
    "ARRAY",
    "ARENA",
    "BUFFER",
    "STRING",
    "ECS",
    "ARCHETYPE",
    "SCENE",
    "RENDERER",
    "TEXTURE",
    "MESH",
    "SHADER",
    "MATERIAL",
    "PHYSICS",
    "TEMP",
    "EDITOR",
    "GAME",
    "MODEL",
    "GEOMETRY_BUFFER",
    "AUDIO"
};

static const c8 *arenaNames[MEM_ARENA_COUNT] = {
    "General",
    "ECS",
    "Renderer",
    "Physics",
    "Frame"
};

const c8 *memGetTagName(MemTag tag)
{
    if (tag >= MEM_TAG_MAX) return "INVALID";
    return tagNames[tag];
}

const c8 *memGetArenaName(MemArenaID arena)
{
    if (arena >= MEM_ARENA_COUNT) return "INVALID";
    return arenaNames[arena];
}

u64 memGetTagUsage(MemTag tag)
{
    if (!g_memory || tag >= MEM_TAG_MAX) return 0;
    return memState.taggedAllocs[tag];
}

u64 memGetArenaUsed(MemArenaID arena)
{
    if (!g_memory || arena >= MEM_ARENA_COUNT) return 0;
    return memState.arenas[arena].used;
}

u64 memGetArenaSize(MemArenaID arena)
{
    if (!g_memory || arena >= MEM_ARENA_COUNT) return 0;
    return memState.arenas[arena].size;
}

u64 memGetTotalUsed(void)
{
    return g_memory ? memState.totalAllocated : 0;
}

u64 memGetTotalSize(void)
{
    if (!g_memory) return 0;
    u64 total = 0;
    for (u32 i = 0; i < MEM_ARENA_COUNT; i++)
        total += memState.arenas[i].size;
    return total;
}

u32 memGetAllocCount(void)
{
    return g_memory ? memState.allocCount : 0;
}

void memResetFrameStats(void)
{
    if (!g_memory) return;
    memset(memState.taggedAllocsFrame, 0, sizeof(memState.taggedAllocsFrame));
    memset(memState.taggedFreesFrame, 0, sizeof(memState.taggedFreesFrame));
}

//=====================================================================================================================
// Config file simple key=value text

b8 memSaveConfig(const c8 *path, const MemoryConfig *cfg)
{
    if (!path || !cfg) return false;

    FILE *f = fopen(path, "w");
    if (!f)
    {
        ERROR("memSaveConfig: failed to open '%s'", path);
        return false;
    }

    fprintf(f, "total=%llu\n",    (unsigned long long)cfg->totalMB);
    fprintf(f, "general=%llu\n",  (unsigned long long)cfg->arenaMB[MEM_ARENA_GENERAL]);
    fprintf(f, "ecs=%llu\n",      (unsigned long long)cfg->arenaMB[MEM_ARENA_ECS]);
    fprintf(f, "renderer=%llu\n", (unsigned long long)cfg->arenaMB[MEM_ARENA_RENDERER]);
    fprintf(f, "physics=%llu\n",  (unsigned long long)cfg->arenaMB[MEM_ARENA_PHYSICS]);
    fprintf(f, "frame=%llu\n",    (unsigned long long)cfg->arenaMB[MEM_ARENA_FRAME]);

    fclose(f);
    return true;
}

b8 memLoadConfig(const c8 *path, MemoryConfig *cfg)
{
    if (!path || !cfg) return false;

    FILE *f = fopen(path, "r");
    if (!f) return false;

    // start from defaults
    *cfg = memDefaultConfig();

    c8 line[256];
    while (fgets(line, sizeof(line), f))
    {
        c8 key[64];
        u64 val;
        if (sscanf(line, "%63[^=]=%llu", key, (unsigned long long *)&val) == 2)
        {
            if      (strcmp(key, "total")    == 0) cfg->totalMB = val;
            else if (strcmp(key, "general")  == 0) cfg->arenaMB[MEM_ARENA_GENERAL]  = val;
            else if (strcmp(key, "ecs")      == 0) cfg->arenaMB[MEM_ARENA_ECS]      = val;
            else if (strcmp(key, "renderer") == 0) cfg->arenaMB[MEM_ARENA_RENDERER] = val;
            else if (strcmp(key, "physics")  == 0) cfg->arenaMB[MEM_ARENA_PHYSICS]  = val;
            else if (strcmp(key, "frame")    == 0) cfg->arenaMB[MEM_ARENA_FRAME]    = val;
        }
    }

    fclose(f);
    return true;
}
