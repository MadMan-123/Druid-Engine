#include "../../include/druid.h"

#ifdef _WIN32
#include <windows.h>
#endif

//=====================================================================================================================
// Profiler - RDTSC scope timing, GPU timer queries, state change + geometry counters
//=====================================================================================================================

static ProfileFrame g_profFrame;
static ProfileFrame g_prevFrame;

// per-frame counters (externally incremented by drawMesh, etc.)
u32 g_drawCalls  = 0;
u32 g_triangles  = 0;
u32 g_vertices   = 0;

// state change counters (reset each frame)
static u32 g_shaderBinds     = 0;
static u32 g_textureBinds    = 0;
static u32 g_vaoBinds        = 0;
static u32 g_bufferBinds     = 0;
static u32 g_uniformUploads  = 0;
static u32 g_fboBinds        = 0;
static u32 g_bufferUploadsCount = 0;
static u64 g_bufferUploadBytes  = 0;

// memory tracking
static u64 g_heapAllocBytes = 0;
static u32 g_heapAllocCount = 0;
static u64 g_heapFreeCount  = 0;
static u64 g_heapLiveBytes  = 0;

// RDTSC → microseconds calibration
static f64 g_cyclesPerUs = 0.0;
static u64 g_frameStartCycles = 0;

// GPU timing (double-buffered to avoid pipeline stalls)
static u32 g_gpuQueries[2]    = {0, 0};
static u32 g_gpuQueryIdx      = 0;
static b8  g_gpuQueriesReady  = false;
static u32 g_gpuFrameCount    = 0;

// GL_PRIMITIVES_GENERATED query (double-buffered)
static u32 g_primQueries[2]     = {0, 0};
static u32 g_primQueryIdx       = 0;
static b8  g_primQueriesReady   = false;
static u32 g_primQueryFrameCount = 0;

void profileCalibrate(void)
{
    f64 freq = (f64)SDL_GetPerformanceFrequency();
    u64 qpcStart = SDL_GetPerformanceCounter();
    u64 tscStart = profileRDTSC_();
    SDL_Delay(50);
    u64 qpcEnd = SDL_GetPerformanceCounter();
    u64 tscEnd = profileRDTSC_();

    f64 elapsedUs = (f64)(qpcEnd - qpcStart) / freq * 1e6;
    if (elapsedUs > 0.0)
        g_cyclesPerUs = (f64)(tscEnd - tscStart) / elapsedUs;

    INFO("Profiler calibrated: %.1f cycles/us (%.0f MHz effective TSC)",
         g_cyclesPerUs, g_cyclesPerUs);
}

void profileBeginFrame(void)
{
    // snapshot last frame's counters into the frame struct
    g_profFrame.drawCalls   = g_drawCalls;
    g_profFrame.triangles   = g_triangles;
    g_profFrame.vertices    = g_vertices;

    // state change counters
    g_profFrame.shaderBinds      = g_shaderBinds;
    g_profFrame.textureBinds     = g_textureBinds;
    g_profFrame.vaoBinds         = g_vaoBinds;
    g_profFrame.bufferBinds      = g_bufferBinds;
    g_profFrame.uniformUploads   = g_uniformUploads;
    g_profFrame.fboBinds         = g_fboBinds;
    g_profFrame.bufferUploadsCount = g_bufferUploadsCount;
    g_profFrame.bufferUploadBytes  = g_bufferUploadBytes;

    // memory tracking
    g_profFrame.heapAllocBytes = g_heapAllocBytes;
    g_profFrame.heapAllocCount = g_heapAllocCount;
    g_profFrame.heapFreeCount  = g_heapFreeCount;
    g_profFrame.heapLiveBytes  = g_heapLiveBytes;

    // publish completed frame to external readers
    g_prevFrame = g_profFrame;

    // reset per-frame counters
    g_drawCalls         = 0;
    g_triangles         = 0;
    g_vertices          = 0;
    g_shaderBinds       = 0;
    g_textureBinds      = 0;
    g_vaoBinds          = 0;
    g_bufferBinds       = 0;
    g_uniformUploads    = 0;
    g_fboBinds          = 0;
    g_bufferUploadsCount = 0;
    g_bufferUploadBytes  = 0;
    g_heapAllocBytes    = 0;
    g_heapAllocCount    = 0;
    g_heapFreeCount     = 0;
    g_profFrame.count       = 0;
    g_profFrame.entityCount = 0;

    g_frameStartCycles = profileRDTSC_();

    // GPU timer queries: create on first use
    if (!g_gpuQueriesReady)
    {
        glGenQueries(2, g_gpuQueries);
        g_gpuQueriesReady = true;
        g_gpuFrameCount = 0;
    }

    // only read back after both query slots have completed at least once
    if (g_gpuFrameCount >= 2)
    {
        u32 readIdx = 1 - g_gpuQueryIdx;
        GLint available = 0;
        glGetQueryObjectiv(g_gpuQueries[readIdx], GL_QUERY_RESULT_AVAILABLE, &available);
        if (available)
        {
            GLuint64 gpuNs = 0;
            glGetQueryObjectui64v(g_gpuQueries[readIdx], GL_QUERY_RESULT, &gpuNs);
            g_profFrame.gpuFrameTime_us = (f64)gpuNs / 1000.0;
        }
    }

    glBeginQuery(GL_TIME_ELAPSED, g_gpuQueries[g_gpuQueryIdx]);

    // primitives generated query
    if (!g_primQueriesReady)
    {
        glGenQueries(2, g_primQueries);
        g_primQueriesReady = true;
        g_primQueryFrameCount = 0;
    }

    if (g_primQueryFrameCount >= 2)
    {
        u32 readIdx = 1 - g_primQueryIdx;
        GLint available = 0;
        glGetQueryObjectiv(g_primQueries[readIdx], GL_QUERY_RESULT_AVAILABLE, &available);
        if (available)
        {
            GLuint64 prims = 0;
            glGetQueryObjectui64v(g_primQueries[readIdx], GL_QUERY_RESULT, &prims);
            g_profFrame.primitivesGenerated = prims;
        }
    }

    glBeginQuery(GL_PRIMITIVES_GENERATED, g_primQueries[g_primQueryIdx]);
}

void profileEndFrame(void)
{
    glEndQuery(GL_TIME_ELAPSED);
    g_gpuQueryIdx = 1 - g_gpuQueryIdx;
    if (g_gpuFrameCount < 2) g_gpuFrameCount++;

    glEndQuery(GL_PRIMITIVES_GENERATED);
    g_primQueryIdx = 1 - g_primQueryIdx;
    if (g_primQueryFrameCount < 2) g_primQueryFrameCount++;

    u64 endCycles = profileRDTSC_();
    g_profFrame.frameCycles = endCycles - g_frameStartCycles;
    if (g_cyclesPerUs > 0.0)
        g_profFrame.frameTime_us = (f64)g_profFrame.frameCycles / g_cyclesPerUs;
}

void profileRecordEntry(const c8 *name, u64 cycles, f64 elapsed_us)
{
    if (g_profFrame.count >= PROFILE_MAX_ENTRIES) return;
    u32 i = g_profFrame.count++;
    g_profFrame.entries[i].name       = name;
    g_profFrame.entries[i].cycles     = cycles;
    g_profFrame.entries[i].elapsed_us = elapsed_us;
}

const ProfileFrame *profileGetCurrentFrame(void)
{
    return &g_prevFrame;
}

void profileScopeEnd_(ProfileScope_ *s)
{
    u64 cycles = profileRDTSC_() - s->startCycles;
    f64 us = (g_cyclesPerUs > 0.0) ? (f64)cycles / g_cyclesPerUs : 0.0;
    profileRecordEntry(s->name, cycles, us);
}

// geometry counters
void profileAddTriangles(u32 count) { g_triangles += count; }
void profileAddVertices(u32 count)  { g_vertices  += count; }
void profileAddEntities(u32 count)  { g_profFrame.entityCount += count; }

// GL state change counters
void profileCountShaderBind(void)    { g_shaderBinds++; }
void profileCountTextureBind(void)   { g_textureBinds++; }
void profileCountVAOBind(void)       { g_vaoBinds++; }
void profileCountBufferBind(void)    { g_bufferBinds++; }
void profileCountUniformUpload(void) { g_uniformUploads++; }
void profileCountFBOBind(void)       { g_fboBinds++; }
void profileCountBufferUpload(u64 bytes) { g_bufferUploadsCount++; g_bufferUploadBytes += bytes; }

// memory tracking
void profileCountAlloc(u64 bytes)    { g_heapAllocBytes += bytes; g_heapAllocCount++; g_heapLiveBytes += bytes; }
void profileCountFree(u64 bytes)     { g_heapFreeCount++; if (g_heapLiveBytes >= bytes) g_heapLiveBytes -= bytes; else g_heapLiveBytes = 0; }

//=====================================================================================================================
// Cache topology detection + estimation
//=====================================================================================================================

static CacheInfo g_cacheInfo = {0};
static b8        g_cacheDetected = false;

void profileDetectCaches(CacheInfo *out)
{
    // Defaults (conservative, modern desktop)
    out->l1dSize  = 32 * 1024;       // 32 KB
    out->l2Size   = 256 * 1024;      // 256 KB
    out->l3Size   = 8 * 1024 * 1024; // 8 MB
    out->lineSize = 64;

#ifdef _WIN32
    DWORD bufSize = 0;
    GetLogicalProcessorInformation(NULL, &bufSize);
    if (bufSize > 0)
    {
        SYSTEM_LOGICAL_PROCESSOR_INFORMATION *buf =
            (SYSTEM_LOGICAL_PROCESSOR_INFORMATION *)malloc(bufSize);
        if (buf && GetLogicalProcessorInformation(buf, &bufSize))
        {
            DWORD count = bufSize / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
            for (DWORD i = 0; i < count; i++)
            {
                if (buf[i].Relationship == RelationCache)
                {
                    CACHE_DESCRIPTOR *c = &buf[i].Cache;
                    if (c->LineSize > 0) out->lineSize = c->LineSize;
                    switch (c->Level)
                    {
                        case 1:
                            if (c->Type == CacheData || c->Type == CacheUnified)
                                out->l1dSize = c->Size;
                            break;
                        case 2: out->l2Size = c->Size; break;
                        case 3: out->l3Size = c->Size; break;
                    }
                }
            }
        }
        free(buf);
    }
#endif

    g_cacheInfo = *out;
    g_cacheDetected = true;
    INFO("Cache topology: L1d=%uKB  L2=%uKB  L3=%uMB  line=%uB",
         out->l1dSize / 1024, out->l2Size / 1024,
         out->l3Size / (1024 * 1024), out->lineSize);
}

const CacheInfo *profileGetCacheInfo(void)
{
    return &g_cacheInfo;
}

void profileEstimateCache(u32 entityCount, u32 bytesPerEntity,
                          u32 usefulBytesPerEntity, u32 numArrays)
{
    if (!g_cacheDetected) return;
    if (entityCount == 0 || bytesPerEntity == 0) return;

    u32 lineSize = g_cacheInfo.lineSize > 0 ? g_cacheInfo.lineSize : 64;

    // Total bytes fetched from memory (includes padding/cold data)
    u64 totalFetched = (u64)entityCount * bytesPerEntity;
    // Bytes actually used by the hot loop
    u64 totalUseful  = (u64)entityCount * usefulBytesPerEntity;

    // Cache lines touched depends on layout:
    //   AoS (numArrays==1): each entity strides bytesPerEntity, so lines = totalFetched / lineSize
    //   SoA (numArrays>1):  each array is contiguous f32s, lines = numArrays * ceil(entityCount * fieldSize / lineSize)
    u64 linesAccessed;
    if (numArrays <= 1)
    {
        // AoS: sequential stride through interleaved struct
        linesAccessed = (totalFetched + lineSize - 1) / lineSize;
    }
    else
    {
        // SoA: each field array is contiguous. Assume f32 (4 bytes) per field per entity.
        // numArrays = number of separate arrays touched by the hot loop.
        u64 bytesPerArray = (u64)entityCount * 4; // f32 per entity per field
        u64 linesPerArray = (bytesPerArray + lineSize - 1) / lineSize;
        linesAccessed = linesPerArray * numArrays;
    }

    u64 fetchedBytes = linesAccessed * lineSize;
    // clamp useful to fetched
    if (totalUseful > fetchedBytes) totalUseful = fetchedBytes;
    u64 wastedBytes  = fetchedBytes - totalUseful;
    f64 utilisation  = (fetchedBytes > 0) ? (f64)totalUseful / (f64)fetchedBytes * 100.0 : 0.0;

    // Estimate cache misses based on working set vs cache level sizes
    // Working set = total unique bytes that need to be in cache simultaneously
    u64 workingSet = (numArrays <= 1) ? totalFetched : ((u64)entityCount * 4 * numArrays);

    u64 l1Size = g_cacheInfo.l1dSize;
    u64 l2Size = g_cacheInfo.l2Size;
    u64 l3Size = g_cacheInfo.l3Size;

    // If working set fits in a cache level, all accesses hit that level.
    // If it overflows, the excess lines miss to the next level.
    u64 l1Misses = (workingSet > l1Size) ? (workingSet - l1Size) / lineSize : 0;
    u64 l2Misses = (workingSet > l1Size + l2Size) ? (workingSet - l1Size - l2Size) / lineSize : 0;
    u64 l3Misses = (workingSet > l1Size + l2Size + l3Size) ?
                   (workingSet - l1Size - l2Size - l3Size) / lineSize : 0;

    // Write to current frame
    g_profFrame.cacheWorkingSetBytes = workingSet;
    g_profFrame.cacheLinesAccessed   = linesAccessed;
    g_profFrame.cacheUsefulBytes     = totalUseful;
    g_profFrame.cacheWastedBytes     = wastedBytes;
    g_profFrame.cacheUtilisation     = utilisation;
    g_profFrame.estL1Misses          = l1Misses;
    g_profFrame.estL2Misses          = l2Misses;
    g_profFrame.estL3Misses          = l3Misses;
}
