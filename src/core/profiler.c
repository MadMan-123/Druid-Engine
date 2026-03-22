#include "../../include/druid.h"

//=====================================================================================================================
// Profiler — RDTSC scope timing, GPU timer queries, state change + geometry counters
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
            g_prevFrame.gpuFrameTime_us = (f64)gpuNs / 1000.0;
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
            g_prevFrame.primitivesGenerated = prims;
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
void profileCountFree(void)          { g_heapFreeCount++; }
