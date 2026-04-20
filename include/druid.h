#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include <GL/glew.h>

#include <assimp/material.h>

// forward declarations for assimp
struct aiScene;
struct aiMaterial;

//=====================================================================================================================
// unsigned int types
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

// signed int types
typedef signed char i8;
typedef signed short i16;
typedef signed int i32;
typedef signed long long i64;

// character types
typedef char c8;

// floating point types
typedef float f32;
typedef double f64;

// boolean types
typedef int b32;
typedef bool b8;



// compile-time assertion macro
#ifdef __cplusplus
#define STATIC_ASSERT(COND, MSG) static_assert(COND, MSG)
#else
#define STATIC_ASSERT(COND, MSG) _Static_assert(COND, MSG)
#endif

// ensure all types are of the correct size
STATIC_ASSERT(sizeof(u8) == 1, "Expected u8 to be 1 byte.");
STATIC_ASSERT(sizeof(u16) == 2, "Expected u16 to be 2 bytes.");
STATIC_ASSERT(sizeof(u32) == 4, "Expected u32 to be 4 bytes.");
STATIC_ASSERT(sizeof(u64) == 8, "Expected u64 to be 8 bytes.");

STATIC_ASSERT(sizeof(i8) == 1, "Expected i8 to be 1 byte.");
STATIC_ASSERT(sizeof(i16) == 2, "Expected i16 to be 2 bytes.");
STATIC_ASSERT(sizeof(i32) == 4, "Expected i32 to be 4 bytes.");
STATIC_ASSERT(sizeof(i64) == 8, "Expected i64 to be 8 bytes.");

STATIC_ASSERT(sizeof(f32) == 4, "Expected f32 to be 4 bytes.");
STATIC_ASSERT(sizeof(f64) == 8, "Expected f64 to be 8 bytes.");

// platform detection
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__)
#define PLATFORM_WINDOWS 1
#ifndef _WIN32
#error "64-bit is required on Windows!"
#endif
#elif defined(__linux__) || defined(__gnu_linux__)
// linux
#define PLATFORM_LINUX 1
#if defined(__ANDROID__)
#define PLATFORM_ANDROID 1
#endif
#elif defined(__unix__)
// unix fallback
#define PLATFORM_UNIX 1
#elif defined(_POSIX_VERSION)
// posix
#define PLATFORM_POSIX 1
#elif __APPLE__
// apple platforms
#define PLATFORM_APPLE 1
#include <TargetConditionals.h>
#if TARGET_IPHONE_SIMULATOR
// iOS simulator
#define PLATFORM_IOS 1
#define PLATFORM_IOS_SIMULATOR 1
#elif TARGET_OS_IPHONE
#define PLATFORM_IOS 1
// iOS device
#elif TARGET_OS_MAC
// macOS
#else
#error "Unknown Apple platform"
#endif
#else
#error "Unknown platform!"
#endif

#ifdef _WIN32
#ifdef DRUID_EXPORT
#define DAPI __declspec(dllexport)
#else
#define DAPI __declspec(dllimport)
#endif
#else
#ifdef DRUID_EXPORT
#define DAPI __attribute__((visibility("default")))
#else
#define DAPI
#endif
#endif

// system DLL export macro, used by generated archetype code
#ifdef DRUID_SYSTEM_EXPORT
  #ifdef _WIN32
    #define DSAPI __declspec(dllexport)
  #else
    #define DSAPI __attribute__((visibility("default")))
  #endif
#else
  #ifdef _WIN32
    #define DSAPI __declspec(dllimport)
  #else
    #define DSAPI
  #endif
#endif

#ifndef RELEASE_BUILD
#define RELEASE_BUILD 0
#endif

#ifndef DEBUG_RESOURCES
#define DEBUG_RESOURCES 0
#endif


#ifdef __cplusplus
extern "C"
{
#endif

//=====================================================================================================================
// Logging system
#define LOG_BUFFER_SIZE 1024
#define LOG_INFO_ENABLED 1
#define LOG_WARNING_ENABLED 1
#define LOG_ERROR_ENABLED 1
#define LOG_DEBUG_ENABLED 1
#define LOG_TRACE_ENABLED 1

#if RELEASE_BUILD == 1
#define LOG_DEBUG_ENABLED 0
#define LOG_TRACE_ENABLED 0
#endif

    typedef enum LogLevel
    {
        LOG_FATAL = 0,
        LOG_ERROR,
        LOG_WARNING,
        LOG_INFO,
        LOG_DEBUG,
        LOG_TRACE,
        LOG_MAX
    } LogLevel;

    b8 initLogging();
    void shutdownLogging();

    DAPI extern void (*logOutputSrc)(LogLevel level, const c8 *msg);
    DAPI extern b8 useCustomOutputSrc;
    DAPI void logOutput(LogLevel level, const c8 *message, ...);

#define FATAL(message, ...) logOutput(LOG_FATAL, message, ##__VA_ARGS__)
#define ERROR(message, ...) logOutput(LOG_ERROR, message, ##__VA_ARGS__)
#define WARN(message, ...) logOutput(LOG_WARNING, message, ##__VA_ARGS__)
#define INFO(message, ...) logOutput(LOG_INFO, message, ##__VA_ARGS__)
#define DEBUG(message, ...) logOutput(LOG_DEBUG, message, ##__VA_ARGS__)
#define TRACE(message, ...) logOutput(LOG_TRACE, message, ##__VA_ARGS__)

    //=====================================================================================================================
    // bitwise flag utilities
    #define FLAG_SET(value,flagIndex) ((value) |= (1 << (flagIndex)))
    #define FLAG_CLEAR(value,flagIndex) ((value) &= ~(1 << (flagIndex)))
    #define FLAG_CHECK(value,flagIndex) (((value) >> (flagIndex)) & 1)



    //=====================================================================================================================
    // maths
    typedef struct
    {
        i32 x, y;
    } Vec2i;

    typedef struct
    {
        i32 x, y, z;
    } Vec3i;

    typedef struct
    {
        f32 x, y;
    } Vec2;

    typedef struct
    {
        f32 x, y, z;
    } Vec3;

    typedef struct
    {
        f32 x, y, z, w;
    } Vec4;

    typedef struct
    {
        f32 m[4][4];
    } Mat4;

    typedef struct
    {
        f32 m[3][3];
    } Mat3;

    // 2d vector methods
    DAPI Vec2 v2Add(Vec2 a, Vec2 b);
    DAPI Vec2 v2Sub(Vec2 a, Vec2 b);
    DAPI Vec2 v2Scale(Vec2 a, f32 b);
    DAPI Vec2 v2Mul(Vec2 a, Vec2 b);
    DAPI f32 v2Mag(Vec2 a);
    DAPI f32 v2Dis(Vec2 a, Vec2 b);
    DAPI Vec2i v2Tov2i(Vec2 a);
    DAPI Vec2 v2iTov2(Vec2i a);
    DAPI Vec2 v2Div(Vec2 a, f32 b);
    DAPI f32 v2Dot(Vec2 a, Vec2 b);

    DAPI b8 v2Equal(Vec2 a, Vec2 b);

    // 3d vector methods

    DAPI Vec3 v3Add(Vec3 a, Vec3 b);
    DAPI Vec3 v3Sub(Vec3 a, Vec3 b);
    DAPI Vec3 v3Scale(Vec3 a, f32 b);
    DAPI Vec3 v3Mul(Vec3 a, Vec3 b);
    DAPI f32 v3Mag(Vec3 a);
    DAPI f32 v3Dis(Vec3 a, Vec3 b);
    DAPI Vec3i v3Tov3i(Vec3 a);
    DAPI Vec3 v3iTov3(Vec3i a);
    DAPI Vec3 v3Div(Vec3 a, f32 b);
    DAPI Vec3 v3Norm(Vec3 a);
    DAPI Vec3 v3Cross(Vec3 a, Vec3 b);
    DAPI b8 v3Equal(Vec3 a, Vec3 b);
    DAPI f32 v3Dot(Vec3 a, Vec3 b);

    // quaternion operations
    DAPI Vec4 quatIdentity();
    DAPI Vec4 quatFromAxisAngle(Vec3 axis, f32 angle);
    DAPI Vec4 quatMul(Vec4 q1, Vec4 q2);
    DAPI Vec4 quatNormalize(Vec4 q);
    DAPI Vec3 quatRotateVec3(Vec4 q, Vec3 v);
    DAPI Mat4 quatToRotationMatrix(Vec4 q);
    DAPI Vec3 quatTransform(Vec4 q, Vec3 v);

    DAPI Vec4 quatConjugate(const Vec4 q);
    DAPI Vec4 quatFromEuler(const Vec3 axis);
    DAPI Vec3 eulerFromQuat(Vec4 quat);

    // matrix methods
    DAPI void matAdd(f32 **a, f32 **b, Vec2i aSize);
    DAPI void matSub(f32 **a, f32 **b, Vec2i aSize);
    DAPI void matDiv(f32 **a, f32 **b, Vec2i aSize);
    DAPI void matMul(f32 **a, f32 **b, Vec2i aSize);
    DAPI void matScale(f32 **a, f32 b, Vec2i aSize);

    // creates a heap allocated matrix
    // TODO: refactor this
    DAPI f32 **matCreate(Vec2i size);
    DAPI void freeMat(f32 **mat, Vec2i size);

    DAPI Mat4 mat4LookAt(Vec3 eye, Vec3 target, Vec3 up);

    DAPI Mat4 mat4Perspective(f32 fovRadians, f32 aspect, f32 nearZ, f32 farZ);
    DAPI Mat4 mat4Ortho(f32 left, f32 right, f32 bottom, f32 top, f32 nearClip, f32 farClip);
    // identity and zero
    DAPI Mat4 mat4Identity(void);
    DAPI Mat4 mat4Zero(void);

    // transformation matrices
    DAPI Mat4 mat4Translate(Mat4 in, Vec3 translation);
    DAPI Mat4 mat4Scale(f32 scale);
    DAPI Mat4 mat4ScaleVec(Vec3 scale);
    DAPI Mat4 mat4ScaleVal(Mat4 a, f32 scale);
    DAPI Mat4 mat4RotateX(f32 angleRadians);
    DAPI Mat4 mat4RotateY(f32 angleRadians);
    DAPI Mat4 mat4Rotate(f32 angleRadians, Vec3 axis);

    // matrix math
    DAPI Mat4 mat4Mul(Mat4 a, Mat4 b);
    DAPI Mat4 mat4Add(Mat4 a, Mat4 b);
    DAPI Mat4 mat4Sub(Mat4 a, Mat4 b);
    DAPI Mat4 mat4ScaleMatrix(Mat4 a, f32 scale);

    // vector transformation
    DAPI Vec4 mat4TransformVec4(Mat4 m, Vec4 v);
    DAPI Vec3 mat4TransformPoint(Mat4 m, Vec3 p);
    DAPI Vec3 mat4TransformDirection(Mat4 m, Vec3 d);

    // determinant and inverse
    DAPI f32 mat4Determinant(Mat4 m);
    DAPI Mat4 mat4Inverse(Mat4 m);

    DAPI Mat4 mat4Perspective(f32 fovRadians, f32 aspect, f32 nearZ, f32 farZ);
    DAPI Mat4 mat4Ortho(f32 left, f32 right, f32 bottom, f32 top, f32 nearClip, f32 farClip);
    DAPI Mat3 mat4ToMat3(const Mat4 m4);
    DAPI Mat4 mat3ToMat4(const Mat3 m3);
    // helper tools
    DAPI f32 clamp(f32 value, f32 minVal, f32 maxVal);
    DAPI f32 degrees(f32 radians);
    DAPI f32 radians(f32 degrees);

    DAPI f32 lerp(f32 a, f32 b, f32 t);
    // constants
    #define PI 3.14159265358979323846f
    #define PI_HALFRES 3.14159265f
    #define PI_QUATERRES 3.14f
    static const Vec3 v3Zero = {0.0f, 0.0f, 0.0f};
    static const Vec3 v3One = {1.0f, 1.0f, 1.0f};

    static const Vec3 v3Up = {0.0f, 1.0f, 0.0f};
    static const Vec3 v3Down = {0.0f, -1.0f, 0.0f};

    static const Vec3 v3Right = {1.0f, 0.0f, 0.0f};
    static const Vec3 v3Left = {-1.0f, 0.0f, 0.0f};

    static const Vec3 v3Forward = {0.0f, 0.0f, -1.0f};
    static const Vec3 v3Back = {0.0f, 0.0f, 1.0f};

    //=====================================================================================================================
    // simd, generic SoA f32 array operations, out may alias in, SSE2 internally

    DAPI void simdAdd(const f32 *a, const f32 *b, f32 *out, u32 count);
    DAPI void simdSub(const f32 *a, const f32 *b, f32 *out, u32 count);
    DAPI void simdMul(const f32 *a, const f32 *b, f32 *out, u32 count);
    DAPI void simdDiv(const f32 *a, const f32 *b, f32 *out, u32 count);
    DAPI void simdMadd(const f32 *a, const f32 *b, const f32 *c, f32 *out, u32 count);
    DAPI void simdNeg(const f32 *a, f32 *out, u32 count);
    DAPI void simdAbs(const f32 *a, f32 *out, u32 count);
    DAPI void simdMin(const f32 *a, const f32 *b, f32 *out, u32 count);
    DAPI void simdMax(const f32 *a, const f32 *b, f32 *out, u32 count);
    DAPI void simdSqrt(const f32 *a, f32 *out, u32 count);
    DAPI void simdRsqrt(const f32 *a, f32 *out, u32 count);
    DAPI void simdRcp(const f32 *a, f32 *out, u32 count);

    DAPI void simdAddScalar(const f32 *a, f32 s, f32 *out, u32 count);
    DAPI void simdMulScalar(const f32 *a, f32 s, f32 *out, u32 count);
    DAPI void simdClampScalar(const f32 *a, f32 lo, f32 hi, f32 *out, u32 count);
    DAPI void simdLerp(const f32 *a, const f32 *b, f32 t, f32 *out, u32 count);

    DAPI void simdDot3(const f32 *ax, const f32 *ay, const f32 *az,
                       const f32 *bx, const f32 *by, const f32 *bz, f32 *out, u32 count);
    DAPI void simdCross3(const f32 *ax, const f32 *ay, const f32 *az,
                         const f32 *bx, const f32 *by, const f32 *bz,
                         f32 *outX, f32 *outY, f32 *outZ, u32 count);
    DAPI void simdNormalize3(const f32 *inX, const f32 *inY, const f32 *inZ,
                             f32 *outX, f32 *outY, f32 *outZ, u32 count);
    DAPI void simdLengthSq3(const f32 *ax, const f32 *ay, const f32 *az, f32 *out, u32 count);
    DAPI void simdLength3(const f32 *ax, const f32 *ay, const f32 *az, f32 *out, u32 count);

    DAPI f32 simdSum(const f32 *a, u32 count);
    DAPI f32 simdMinReduce(const f32 *a, u32 count);
    DAPI f32 simdMaxReduce(const f32 *a, u32 count);

    //=====================================================================================================================
    // file IO
    #define MAX_PATH_LENGTH 512
    typedef struct{
    c8 path[MAX_PATH_LENGTH];
    u8 *data;
    u32 size;
    } FileData;

    DAPI FileData* loadFile(const c8 *filePath);
    DAPI void freeFileData(FileData* fileData);
    DAPI b8 writeFile(const c8 *filePath, const u8 *data, u32 size);
    DAPI b8 fileExists(const c8 *filePath);
    DAPI c8** listFilesInDirectory(const c8 *directory, u32 *outCount);
    DAPI void normalizePath(c8 *path);

    // returns true if the path is an existing directory
    DAPI b8 dirExists(const c8 *path);
    // creates a directory and all intermediate parents
    DAPI b8 createDir(const c8 *path);
   
    //=====================================================================================================================
    // platform OS layer

    // shared library
    DAPI void *platformLibraryLoad(const c8 *path);
    DAPI void  platformLibraryFree(void *handle);
    DAPI void *platformLibrarySymbol(void *handle, const c8 *name);

    // file operations that need OS calls
    DAPI b8   platformFileCopy(const c8 *src, const c8 *dst);
    DAPI b8   platformFileDelete(const c8 *path);
    DAPI void platformDirCopyRecursive(const c8 *src, const c8 *dst);

    // process / pipe
    DAPI void *platformPipeOpen(const c8 *command);
    DAPI i32   platformPipeClose(void *pipe);

    // executable info
    DAPI void platformGetExePath(c8 *out, u32 size);

    //=====================================================================================================================
    // DLL loader

    typedef struct
    {
        void *handle;
        c8    loadedPath[MAX_PATH_LENGTH];
        b8    loaded;
    } DLLHandle;

    DAPI b8   dllLoad(const c8 *dllPath, DLLHandle *out);
    DAPI void *dllSymbol(DLLHandle *handle, const c8 *name);
    DAPI void  dllUnload(DLLHandle *handle);



    //=====================================================================================================================
    // arenas

    typedef struct
    {
        void *data;
        u64 size;
        u64 used;
    } Arena;

    DAPI b8 arenaCreate(Arena *arena, u64 maxSize);
    DAPI void *aalloc(Arena *arena, u64 size);
    DAPI void arenaDestroy(Arena *arena);

    //=====================================================================================================================
    // memory system

    typedef enum
    {
        MEM_TAG_UNKNOWN,
        MEM_TAG_ARRAY,
        MEM_TAG_ARENA,
        MEM_TAG_BUFFER,
        MEM_TAG_STRING,
        MEM_TAG_ECS,
        MEM_TAG_ARCHETYPE,
        MEM_TAG_SCENE,
        MEM_TAG_RENDERER,
        MEM_TAG_TEXTURE,
        MEM_TAG_MESH,
        MEM_TAG_SHADER,
        MEM_TAG_MATERIAL,
        MEM_TAG_PHYSICS,
        MEM_TAG_TEMP,
        MEM_TAG_EDITOR,
        MEM_TAG_GAME,
        MEM_TAG_MODEL,
        MEM_TAG_GEOMETRY_BUFFER,
        MEM_TAG_MAX
    } MemTag;

    // top-level arena identifiers
    typedef enum
    {
        MEM_ARENA_GENERAL,
        MEM_ARENA_ECS,
        MEM_ARENA_RENDERER,
        MEM_ARENA_PHYSICS,
        MEM_ARENA_FRAME,
        MEM_ARENA_COUNT
    } MemArenaID;

    #define MEM_ALIGNMENT 16

    // configurable arena sizes
    typedef struct
    {
        u64 totalMB;
        u64 arenaMB[MEM_ARENA_COUNT];
    } MemoryConfig;

    typedef struct
    {
        u64    totalSize;
        void  *block;
        Arena  arenas[MEM_ARENA_COUNT];

        // per-tag stats
        u64 taggedAllocs[MEM_TAG_MAX];
        u64 taggedAllocsFrame[MEM_TAG_MAX];
        u64 taggedFreesFrame[MEM_TAG_MAX];
        u64 totalAllocated;
        u32 allocCount;
    } MemorySystem;

    // lifecycle
    DAPI MemoryConfig memDefaultConfig(void);
    DAPI b8   memorySystemInit(MemoryConfig *config);
    DAPI void memorySystemShutdown(void);
    DAPI void memorySystemReset(void);
    DAPI void memArenaReset(MemArenaID arena);

    // allocator
    DAPI void *dalloc(u64 size, MemTag tag);
    DAPI void  dfree(void *block, u64 size, MemTag tag);

    // frame allocator
    DAPI void *frameAlloc(u64 size);
    DAPI void  frameReset(void);

    // convenience macros
    #define DALLOC_TYPE(type, tag)             (type *)dalloc(sizeof(type), tag)
    #define DFREE_TYPE(ptr, type, tag)         dfree(ptr, sizeof(type), tag)
    #define DALLOC_ARRAY(type, count, tag)     (type *)dalloc(sizeof(type) * (count), tag)
    #define DFREE_ARRAY(ptr, type, count, tag) dfree(ptr, sizeof(type) * (count), tag)

    // stats / editor queries
    DAPI const c8 *memGetTagName(MemTag tag);
    DAPI const c8 *memGetArenaName(MemArenaID arena);
    DAPI u64  memGetTagUsage(MemTag tag);
    DAPI u64  memGetArenaUsed(MemArenaID arena);
    DAPI u64  memGetArenaSize(MemArenaID arena);
    DAPI u64  memGetTotalUsed(void);
    DAPI u64  memGetTotalSize(void);
    DAPI u32  memGetAllocCount(void);
    DAPI void memResetFrameStats(void);

    // config file
    DAPI b8 memSaveConfig(const c8 *path, const MemoryConfig *cfg);
    DAPI b8 memLoadConfig(const c8 *path, MemoryConfig *cfg);

    DAPI extern MemorySystem *g_memory;

    //=====================================================================================================================
    // hash map

#define MAX__NAME 256

    typedef struct
    {
        void *key;
        void *value;
        b8 occupied;
    } Pair;

    typedef struct
    {
        u32 capacity;
        u32 count;
        Pair *pairs;
        u32 keySize;
        u32 valueSize;
        Arena *arena;

        u32 (*hashFunc)(const void *key, u32 capacity);
        b8 (*equalsFunc)(const void *keyA, const void *keyB);
    } HashMap;

    DAPI b8 createMap(HashMap *map, u32 capacity, u32 keySize, u32 valueSize,
                        u32 (*hashFunc)(const void *, u32),
                        b8 (*equalsFunc)(const void *, const void *));
    DAPI u32 hash(c8 *name, u32 mapSize);
    DAPI void printMap(HashMap *map);
    DAPI b8 insertMap(HashMap *map, const void *key, const void *value);
    DAPI void destroyMap(HashMap *map);

    DAPI b8 findInMap(HashMap *map, const void *key, void *outValue);

    //=====================================================================================================================
    // SoA ECS
    typedef enum
    {
        FIELD_TEMP_COLD = 0,
        FIELD_TEMP_HOT  = 1,
    } FieldTemperature;

    typedef struct
    {
        const c8 *name;
        u32 size;
        FieldTemperature temperature;
    } FieldInfo;

    typedef struct
    {
        const c8 *name;
        FieldInfo *fields;
        u32 count;
    } StructLayout;

    typedef struct
    {
        u32 count;
        u32 entityCount;
        u32 entitySize;
        u32 fieldCount;
        StructLayout *layout;
        void *data;
        void **fields;
        b8   ownsData;
    } EntityArena;

    // chunk system constants
    #define CHUNK_SIMD_ALIGN 4
    #define CHUNK_DEFAULT_SIZE (128 * 1024)

    // chunked entity arena functions
    DAPI EntityArena createEntityArenaChunk(StructLayout *layout, u32 chunkCapacity);
    DAPI void freeEntityArenaChunk(EntityArena *chunk);

    // create Entity Arena
    DAPI EntityArena *createEntityArena(StructLayout *layout, u32 entityCount,
                                        u32 *outArenas);

    DAPI void printEntityArena(EntityArena *arena);

    // free the arena
    DAPI b8 freeEntityArena(EntityArena *arena, u32 arenaCount);
    DAPI u32 createEntity(EntityArena *arena);

    // remove entity at index from an arena
    DAPI b8 removeEntityFromArena(EntityArena *arena, u32 index);

    // calculate Entity Size based of a struct layout
    DAPI u32 getEntitySize(StructLayout *layout);

    // entity handle packing macro
    #define ENTITY_PACK(archId, chunkId, idx) \
        (((u64)(archId) << 32) | ((u64)(chunkId) << 16) | (u64)(idx))

#define FIELD_OF(Type, field)                                                  \
    {                                                                          \
        #field, sizeof(((Type *)0)->field), FIELD_TEMP_COLD                    \
    }

#define FIELD(type, name)                                                      \
    {                                                                          \
        #name, sizeof(type), FIELD_TEMP_COLD                                   \
    }

// hot/cold explicit macros
#define FIELD_HOT(type, name)                                                  \
    {                                                                          \
        #name, sizeof(type), FIELD_TEMP_HOT                                    \
    }

#define FIELD_COLD(type, name)                                                 \
    {                                                                          \
        #name, sizeof(type), FIELD_TEMP_COLD                                   \
    }

#define VEC3_FIELDS(name)                                                      \
    {#name "X", sizeof(f32), FIELD_TEMP_COLD},                                 \
    {#name "Y", sizeof(f32), FIELD_TEMP_COLD},                                 \
    {#name "Z", sizeof(f32), FIELD_TEMP_COLD}

#define VEC3_FIELDS_HOT(name)                                                  \
    {#name "X", sizeof(f32), FIELD_TEMP_HOT},                                  \
    {#name "Y", sizeof(f32), FIELD_TEMP_HOT},                                  \
    {#name "Z", sizeof(f32), FIELD_TEMP_HOT}

#define VEC4_FIELDS(name)                                                      \
    {#name "X", sizeof(f32), FIELD_TEMP_COLD},                                 \
    {#name "Y", sizeof(f32), FIELD_TEMP_COLD},                                 \
    {#name "Z", sizeof(f32), FIELD_TEMP_COLD},                                 \
    {#name "W", sizeof(f32), FIELD_TEMP_COLD}

#define VEC4_FIELDS_HOT(name)                                                  \
    {#name "X", sizeof(f32), FIELD_TEMP_HOT},                                  \
    {#name "Y", sizeof(f32), FIELD_TEMP_HOT},                                  \
    {#name "Z", sizeof(f32), FIELD_TEMP_HOT},                                  \
    {#name "W", sizeof(f32), FIELD_TEMP_HOT}

#ifdef DRUID_H
#define DEFINE_ARCHETYPE(name, ...)                                            \
    FieldInfo name##_fields[] = {__VA_ARGS__};                                 \
    StructLayout name = {#name, name##_fields,                                 \
                         (u32)(sizeof(name##_fields) / sizeof(FieldInfo))};
#else
#define DEFINE_ARCHETYPE(name, ...)                                            \
    extern FieldInfo name##_fields[];                                          \
    extern StructLayout name;
#endif

    //=====================================================================================================================
    // archetypes

    typedef struct PhysicsWorld PhysicsWorld;

    // archetype flags
    #define ARCH_SINGLE       0
    #define ARCH_PERSISTENT   1
    #define ARCH_BUFFERED     2
    #define ARCH_PHYSICS_BODY 3
    #define ARCH_NO_SPLIT     4

    typedef struct
    {
        StructLayout *layout;
        EntityArena  *arena;
        void         *hotData;
        void         *coldData;
        u32 id;
        u32 arenaCount;
        u32 hotEntitySize;
        u32 coldEntitySize;
        u32 capacity;
        u32 poolCapacity;
        u32 activeChunkCount;
        u32 chunkCapacity;
        u32 chunkSizeBytes;
        u32 *deadIndices;
        u32 deadCount;
        u32 cachedEntityCount;
        u8  flags;
    } Archetype;

    // archetype API
    DAPI b8 createArchetype(StructLayout *layout, u32 capacity,
                            Archetype *outArchetype);
    DAPI b8 destroyArchetype(Archetype *arch);

    // create an entity in the archetype, returns packed handle via outEntity
    DAPI u8 createEntityInArchetype(Archetype *arch, u64 *outEntity);

    // remove an entity from the archetype
    DAPI b8 removeEntityFromArchetype(Archetype *arch, u32 arenaIndex,
                                      u32 index);

    // get the archetype SoA field pointers for the given arena index
    DAPI void **getArchetypeFields(Archetype *arch, u32 arenaIndex);

    // pool API for buffered archetypes
    DAPI u32  archetypePoolSpawn(Archetype *arch);
    DAPI void archetypePoolDespawn(Archetype *arch, u32 index);
    DAPI b8   archetypePoolIsAlive(Archetype *arch, u32 index);

    // chunk query helpers
    DAPI u32 archetypeEntityCount(Archetype *arch);
    DAPI u32 archetypeChunkCount(Archetype *arch);
    DAPI void archetypeSetChunkSize(Archetype *arch, u32 sizeBytes);

    // hot/cold contiguous data accessors
    DAPI void *archetypeGetHotData(Archetype *arch);
    DAPI void *archetypeGetColdData(Archetype *arch);
    DAPI u32   archetypeGetHotEntitySize(Archetype *arch);
    DAPI u32   archetypeGetColdEntitySize(Archetype *arch);

    //=====================================================================================================================

    typedef void (*SystemFn)(Archetype, f32);

    //=====================================================================================================================
    // ECS system DLL

    #ifndef MAX_SCENE_NAME
    #define MAX_SCENE_NAME 128
    #endif

    typedef void (*ECSSystemInitFn)(void);
    typedef void (*ECSSystemUpdateFn)(Archetype *arch, f32 dt);
    typedef struct Renderer Renderer;
    typedef void (*ECSSystemRenderFn)(Archetype *arch, Renderer *r);
    typedef void (*ECSSystemDestroyFn)(void);

    typedef struct
    {
        ECSSystemInitFn     init;
        ECSSystemUpdateFn   update;
        ECSSystemRenderFn   render;
        ECSSystemDestroyFn  destroy;
    } ECSSystemPlugin;

    // the DLL must export this
    typedef void (*GetECSSystemFn)(ECSSystemPlugin *out);

    typedef struct
    {
        DLLHandle       dll;
        ECSSystemPlugin plugin;
        b8              loaded;
        c8              name[MAX_SCENE_NAME];
    } ECSSystemDLL;

    // load / unload an ECS system DLL
    DAPI b8   loadECSSystemDLL(const c8 *dllPath, ECSSystemDLL *out);
    DAPI void unloadECSSystemDLL(ECSSystemDLL *dll);

    // look up an archetype's ECS system entry point from a loaded DLL handle
    DAPI b8 loadECSSystemFromHandle(DLLHandle *dll, const c8 *archetypeName,
                                     ECSSystemPlugin *out);

    //=====================================================================================================================
    // archetype code file tracking

    #define MAX_ARCHETYPE_SYSTEMS 32

    #define ARCH_FILE_UNIFORM_SCALE 4

    typedef struct
    {
        c8 name[MAX_SCENE_NAME];
        c8 headerPath[MAX_PATH_LENGTH];
        c8 sourcePath[MAX_PATH_LENGTH];
        StructLayout layout;
        u32 poolCapacity;
        u8  flags;
    } ArchetypeFileEntry;

    typedef struct
    {
        ArchetypeFileEntry entries[MAX_ARCHETYPE_SYSTEMS];
        u32 count;
    } ArchetypeRegistry;

    // generate archetype source files for a project
    DAPI b8 generateArchetypeFiles(const c8 *projectDir,
                                    const c8 *archetypeName,
                                    const FieldInfo *fields,
                                    const c8 **typeNames,
                                    u32 fieldCount,
                                    b8 isSingle,
                                    b8 isBuffered,
                                    u32 poolCapacity,
                                    b8 isPhysicsBody,
                                    b8 useCpp);

    // compile all archetype system DLLs in the project
    DAPI b8 buildArchetypeSystems(const c8 *projectDir, c8 *outLog, u32 logSize);

    // Entity manager
    typedef struct
    {
        u32 archetypeCount;
        Archetype *archetypes;
        SystemFn *systems;
        HashMap indexMap;
    } EntityManager;

    //=====================================================================================================================
    // scenes

#define MAX_SCENE_ARCHETYPES 32
#define MAX_FIELD_NAME 64
#define SCENE_MAGIC 0x43535244
#define SCENE_VERSION 5

// name size constant
#ifndef MAX_NAME_SIZE
#define MAX_NAME_SIZE 256
#endif

    typedef struct
    {
        EntityManager manager;
    } Scene;

    typedef struct Material Material;

    typedef struct
    {
        u32 archetypeCount;
        c8 archetypeNames[MAX_SCENE_ARCHETYPES][MAX_SCENE_NAME];
        Archetype *archetypes;
        u32 materialCount;
        Material *materials;
        u32 modelRefCount;
        c8 (*modelRefs)[MAX_NAME_SIZE];
    } SceneData;

    typedef struct
    {
        u32 magic;
        u32 version;
        u32 archetypeCount;
    } SceneFileHeader;

    typedef struct
    {
        c8 name[MAX_FIELD_NAME];
        u32 size;
    } FieldFileHeader;

    typedef struct
    {
        Arena *data;
        SceneData *scenes;
        Scene *currentScene;
        u32 sceneCount;
        u32 sceneCapacity;
    } SceneManager;

    DAPI extern SceneManager *sceneManager;

    //=====================================================================================================================
    // scene runtime

    typedef struct
    {
        SceneData  data;
        Vec3 *positions;
        Vec4 *rotations;
        Vec3 *scales;
        b8   *isActive;
        u32  *modelIDs;
        u32  *shaderHandles;
        u32  *materialIDs;
        b8   *sceneCameraFlags;
        u32  *archetypeIDs;
        u32  *ecsSlotIDs;
        u32   entityCount;
        b8    loaded;
    } SceneRuntime;

    DAPI extern SceneRuntime *sceneRuntime;

    // load and activate the startup scene
    DAPI b8   sceneRuntimeInit(const c8 *projectDir);
    // switch to a different scene at runtime
    DAPI b8   sceneRuntimeLoad(const c8 *filePath);
    // free all runtime scene data
    DAPI void sceneRuntimeDestroy(void);
    // get current SceneRuntime pointer
    DAPI SceneRuntime *sceneRuntimeGetData(void);

    // scene transition callback
    typedef void (*SceneTransitionFn)(void *userData);

    // register callbacks for scene load/unload
    DAPI void sceneRuntimeSetCallbacks(SceneTransitionFn onBeforeUnload,
                                       SceneTransitionFn onAfterLoad,
                                       void *userData);
    // scene manager API
    DAPI SceneManager *createSceneManager(u32 sceneCapacity);
    DAPI void destroySceneManager(SceneManager *manager);
    DAPI u32 addScene(SceneManager *manager, SceneData *sceneData);
    DAPI void removeScene(SceneManager *manager, u32 sceneIndex);
    DAPI void switchScene(SceneManager *manager, u32 sceneIndex);

    // persist/load scene data to disk
    DAPI b8 saveScene(const c8 *filePath, SceneData *data);
    DAPI SceneData loadScene(const c8 *filePath);
    // remap SceneEntity modelID values from saved modelRefs
    DAPI u32 sceneRemapModelIDs(SceneData *data);
    DAPI SceneData bakeScene(Scene *scene);

    //=====================================================================================================================
    // OBJ loading

    typedef struct
    {
        u32 v;
        u32 vt;
        u32 vn;
    } OBJKey;

    // obj index structure
    typedef struct
    {
        u32 vertexIndex;
        u32 uvIndex;
        u32 normalIndex;
    } OBJIndex;

    // indexed model
    typedef struct
    {
        Vec3 *positions;
        Vec2 *texCoords;
        Vec3 *normals;
        u32 *indices;

        u32 positionsCount;
        u32 texCoordsCount;
        u32 normalsCount;
        u32 indicesCount;

        u32 positionsCapacity;
        u32 texCoordsCapacity;
        u32 normalsCapacity;
        u32 indicesCapacity;
    } IndexedModel;

    // obj model
    typedef struct
    {
        OBJIndex *objIndices;
        Vec3 *vertices;
        Vec2 *uvs;
        Vec3 *normals;
        b8 hasUVs;
        b8 hasNormals;

        u32 objIndicesCount;
        u32 verticesCount;
        u32 uvsCount;
        u32 normalsCount;

        u32 objIndicesCapacity;
        u32 verticesCapacity;
        u32 uvsCapacity;
        u32 normalsCapacity;
    } OBJModel;

    DAPI void indexedModelCalcNormals(IndexedModel *model);
    DAPI OBJModel *objModelCreate(const c8 *fileName);
    DAPI void objModelDestroy(OBJModel *model);
    DAPI IndexedModel *objModelToIndexedModel(OBJModel *objModel);

    // helpers
    DAPI void objModelCreateFace(OBJModel *model, const c8 *line);
    DAPI OBJIndex objModelParseOBJIndex(const c8 *token, b8 *hasUVs,
                                        b8 *hasNormals);
    DAPI Vec2 objModelParseVec2(const c8 *line);
    DAPI Vec3 objModelParseVec3(const c8 *line);
    DAPI u32 FindNextChar(u32 start, const c8 *str, u32 length, c8 token);
    DAPI u32 parseOBJIndexValue(const c8 *token, u32 start, u32 end);
    DAPI f32 parseOBJFloatValue(const c8 *token, u32 start, u32 end);
    DAPI c8 **SplitString(const c8 *s, c8 delim, u32 *count);
    DAPI u32 CompareOBJIndexPtr(const void *a, const void *b);
    //=====================================================================================================================
    // transform

    typedef struct
    {
        Vec3 pos;
        Vec4 rot;
        Vec3 scale;
    } Transform;

    DAPI Mat4 getModel(const Transform *transform);

    // Camera
    typedef struct
    {
        Mat4 projection;
        Vec3 pos;
        Vec4 orientation;
    } Camera;

    DAPI Mat4 getViewProjection(const Camera *camera);

    DAPI void initCamera(Camera *camera, const Vec3 pos, f32 fov, f32 aspect,
                         f32 nearClip, f32 farClip);

    DAPI void moveForward(Camera *camera, f32 amt);

    DAPI void moveRight(Camera *camera, f32 amt);

    DAPI void pitch(Camera *camera, f32 angle);

    DAPI void rotateY(Camera *camera, f32 angle);

    DAPI Mat4 getView(const Camera *camera, b8 removeTranslation);

    DAPI void cameraSetPerspective(Camera *cam, f32 fovDeg, f32 aspect, f32 nearClip, f32 farClip);
    DAPI void cameraSetOrthographic(Camera *cam, f32 left, f32 right, f32 bottom, f32 top, f32 nearClip, f32 farClip);

    //=====================================================================================================================
    // buffer

    typedef struct
    {
        void *data;
        b8   *occupied;
        u32   elemSize;
        u32   capacity;
        u32   count;
        u32  *freeStack;
        u32   freeCount;
    } Buffer;

    DAPI b8   bufferCreate(Buffer *buf, u32 elemSize, u32 capacity);
    DAPI u32  bufferAcquire(Buffer *buf);
    DAPI void bufferRelease(Buffer *buf, u32 index);
    DAPI void *bufferGet(Buffer *buf, u32 index);
    DAPI b8   bufferIsOccupied(Buffer *buf, u32 index);
    DAPI void bufferDestroy(Buffer *buf);

    //=====================================================================================================================
    // model SSBO

    typedef struct
    {
        Mat4 *data;
        u32   buffer;
        u32   capacity;
        u32   count;
    } ModelSSBO;

    DAPI ModelSSBO *modelSSBOCreate(u32 capacity);
    DAPI void       modelSSBODestroy(ModelSSBO *ssbo);
    DAPI void       modelSSBOBeginFrame(ModelSSBO *ssbo);
    DAPI u32        modelSSBOWrite(ModelSSBO *ssbo, const Transform *t);
    DAPI void       modelSSBOUpload(ModelSSBO *ssbo);
    DAPI void       modelSSBOEndFrame(ModelSSBO *ssbo);  // Flushes buffered writes (GL_MAP_FLUSH_EXPLICIT_BIT)
    DAPI void       modelSSBOBind(ModelSSBO *ssbo, u32 bindingPoint);

    //=====================================================================================================================
    // Display — SDL window + OpenGL context wrapper (owned by Renderer)

    typedef struct
    {
        // open gl context (using SDL)
        SDL_GLContext glContext;
        // window handle
        SDL_Window *sdlWindow;
        // size dimensions
        f32 screenWidth;
        f32 screenHeight;
    } Display;

    // Display functions
    DAPI void initDisplay(const c8* title, Display *display, f32 width, f32 height);
    DAPI void swapBuffer(const Display *display);
    DAPI void clearDisplay(f32 r, f32 g, f32 b, f32 a);
    DAPI void returnError(const c8 *errorString);
    DAPI void onDestroy(Display *display);
    // VSync: 0 = off, 1 = on, -1 = adaptive (if supported)
    DAPI void setVSync(i32 interval);

    //=====================================================================================================================
    // Renderer
    // Global rendering context that owns Buffer-based collections of cameras,
    // instance buffers and GBuffers.  Pass desired capacities to createRenderer;
    // individual slots are acquired/released through the convenience helpers
    // below, which delegate to the underlying Buffer.

    //=====================================================================================================================
    // IndirectBuffer — multi-draw indirect rendering (glMultiDrawElementsIndirect)
    //
    // Pre-built command buffer containing all draw commands. Reduces GPU submission overhead
    // from N individual draw calls to 1 indirect dispatch.
    //
    // GL indirect command layout (5 u32s per command, 20 bytes total):
    //   count:         indices per draw (typically vertices * 3 for triangles)
    //   instanceCount: number of instances per draw (typically 1, but can be >1)
    //   firstIndex:    byte offset into the index buffer
    //   baseVertex:    vertex array offset (signed i32)
    //   baseInstance:  gl_BaseInstance for model matrix SSBO indexing
    //
    // Usage: Build commands incrementally, upload to GPU, dispatch once per frame.
    // This single glMultiDrawElementsIndirect call replaces N individual glDrawElements calls.

    typedef struct
    {
        u32 count;
        u32 instanceCount;
        u32 firstIndex;
        i32 baseVertex;
        u32 baseInstance;
    } IndirectCommand;

    typedef struct
    {
        u32 buffer;            // GL buffer object for indirect commands
        IndirectCommand *commands;  // CPU-side command staging area
        u32 commandCount;      // number of commands built this frame
        u32 maxCommands;       // allocated capacity
    } IndirectBuffer;

    #define RENDERER_MAX_INSTANCE  1048576   // 1M entities @ 64B each = 64 MB SSBO

    // Frustum culling — Gribb-Hartmann plane extraction (cached per frame)
    typedef struct { f32 a, b, c, d; } FrustumPlane;
    typedef struct { FrustumPlane p[6]; } Frustum;

    typedef struct Renderer
    {
        // pointers (8-byte aligned)
        Display     *display;
        ModelSSBO   *modelSSBO;
        IndirectBuffer *indirectBuffer;

        // Buffers (8-byte aligned, 32 bytes each)
        Buffer cameras;
        Buffer instanceBuffers;
        Buffer gBuffers;

        // Cached frustum (extracted once per frame in rendererBeginFrame)
        Frustum frustum;
        b8      hasFrustum;

        // 4-byte fields (packed tightly)
        u32 activeCamera;
        u32 activeGBuffer;      // slot index in gBuffers, or (u32)-1 for forward
        u32 defaultIBuffer;     // slot index of default instance buffer
        u32 coreUBO;
        u32 defaultShader;
        u32 envMapTex;
        f32 time;

        // 1-byte fields
        b8  useDeferredRendering;

        // per-entity visibility from last frustum cull, frame-allocated
        b8     *frameVisible;
        u32     frameVisibleCount;
    } Renderer;

    DAPI extern Renderer *renderer;

    // create the global renderer (called once after display is alive)
    // maxCameras / maxIBuffers / maxGBuffers control buffer sizes (heap-allocated)
    DAPI Renderer *createRenderer(Display *display, f32 fov, f32 nearClip, f32 farClip,
                                  u32 maxCameras, u32 maxIBuffers, u32 maxGBuffers);
    // per-frame: push active camera + time into the core UBO
    DAPI void rendererBeginFrame(Renderer *r, f32 dt);
    // per-frame: flush instanced draws from a specific instance buffer
    DAPI void rendererFlushInstances(Renderer *r, u32 ibufferIndex, u32 shaderProgram);
    DAPI void destroyRenderer(Renderer *r);

    // Default forward-pass render for ECS archetypes whose ECSSystemPlugin.render is NULL.
    // Looks up Position (Vec3), Rotation (Vec4), Scale (Vec3), ModelID (u32) fields by name;
    // optionally skips dead entities via an Alive (b8) field.
    DAPI void rendererDefaultArchetypeRender(Archetype *arch, Renderer *r);

    // Direct instanced mesh submission — use for models you know will be drawn many times
    // (particles, asteroids, foliage, etc.) without needing a full archetype.
    //
    //   rendererSubmitInstance(r, modelID, &transform);   // call once per entity
    //   rendererFlushInstancedModels(r);                  // call once per frame to issue draw
    //
    // Internally groups submissions by modelID and issues one glDrawElementsInstanced
    // per unique model, so order of submission doesn't matter.
    DAPI void rendererSubmitInstance(Renderer *r, u32 modelID, const Transform *t);
    DAPI void rendererFlushInstancedModels(Renderer *r);

    // convenience acquire / release wrappers
    // Each returns an index into the buffer, or (u32)-1 on failure.

    // cameras
    DAPI u32  rendererAcquireCamera(Renderer *r, Vec3 pos, f32 fov, f32 aspect,
                                     f32 nearClip, f32 farClip);
    DAPI void rendererReleaseCamera(Renderer *r, u32 index);

    // instance buffers
    DAPI u32  rendererAcquireInstanceBuffer(Renderer *r, u32 capacity);
    DAPI void rendererReleaseInstanceBuffer(Renderer *r, u32 index);

    // GBuffers
    DAPI u32  rendererAcquireGBuffer(Renderer *r, u32 width, u32 height);
    DAPI void rendererReleaseGBuffer(Renderer *r, u32 index);

    // deferred rendering
    DAPI b8   rendererEnableDeferred(Renderer *r, u32 width, u32 height);
    DAPI void rendererDisableDeferred(Renderer *r);
    DAPI void rendererBeginDeferredPass(Renderer *r);
    DAPI void rendererEndDeferredPass(Renderer *r);
    DAPI void rendererLightingPass(Renderer *r, u32 lightingShader);

    // convenience: set which camera rendererBeginFrame uploads to the UBO
    DAPI void rendererSetActiveCamera(Renderer *r, u32 index);

    DAPI Camera *rendererGetCamera(Renderer *r, u32 index);
    DAPI u32     rendererGetActiveCamera(Renderer *r);


    //=====================================================================================================================
    // Shaders
    DAPI u32 initShader(const c8 *filename);

    // takes the code of a shader and creates said shader
    DAPI u32 createShader(const c8 *text, u32 type);
    DAPI u32 createProgram(u32 shader);

    // creates a program with two shaders, a vertex and fragment shader used to
    // render meshes with open gl
    DAPI u32 createGraphicsProgram(const c8 *vertPath, const c8 *fragPath);

    // creates a program with three shaders, a vertex , geometry and fragment
    // shader used to render meshes with open gl but with a geometry shader in
    // between to control the primitives
    DAPI u32 createGraphicsProgramWithGeometry(const c8 *vertPath,
                                               const c8 *geomPath,
                                               const c8 *fragPath);
    // craetes a compute shader program
    DAPI u32 createComputeProgram(const c8 *computePath);
    // error tool
    DAPI void checkShaderError(u32 shader, u32 flag, b8 isProgram,
                               const c8 *errorMessage);
    DAPI void freeShader(u32 shader);

    DAPI void updateShaderMVP(const u32 shader, const Transform transform,
                              const Camera camera);
    // New optimized functions for UBO rendering
    DAPI void updateShaderModel(u32 shaderProgram, const Transform transform);  
    DAPI void updateFrameUBOData(const Camera* camera, f32 deltaTime);

    //=====================================================================================================================
    // Uniform Buffer Object
    DAPI u32  createUBO(u32 size, const void *data, GLenum usage);
    DAPI void updateUBO(u32 ubo, u32 offset, u32 size, const void *data);
    DAPI void bindUBOBase(u32 ubo, u32 bindingPoint);
    DAPI void freeUBO(u32 ubo);
    DAPI u32  createCoreShaderUBO(void);
    DAPI void updateCoreShaderUBO(f32 timeSeconds, const Vec3 *camPos, const Mat4 *view, const Mat4 *projection);

    //=====================================================================================================================
    // Shader Storage Buffer Object
    DAPI u32  createSSBO(u32 size, const void *data, GLenum usage);
    DAPI void updateSSBO(u32 ssbo, u32 offset, u32 size, const void *data);
    DAPI void bindSSBOBase(u32 ssbo, u32 bindingPoint);
    DAPI void destroySSBO(u32 ssbo);

    // Textures
    // 32 textures MAX
    DAPI void bindTexture(u32 texture, u32 unit, GLenum type);
    // return the texture handle
    DAPI u32 initTexture(const c8 *fileName);
    // free texture from memory
    DAPI void freeTexture(u32 texture);

    DAPI u32 createCubeMapTexture(const c8 **faces, u32 count);
    // Terrain stuff

    typedef struct
    {
        f32 *heights;
        i32 width;
        i32 height;
    } HeightMap;

    //====================================================================================================================
    // Materials
    #define RES_FOLDER "res/"
    #define MODEL_FOLDER "res/models/"
    #define TEXTURE_FOLDER "res/textures/"

    typedef struct
    {
        u32 albedoTex;
        u32 normalTex;
        u32 metallicTex;
        u32 roughnessTex;
        u32 roughness;
        u32 metallic;
        u32 transparency;
        u32 colour;
        u32 emissive;
    } MaterialUniforms;

    typedef struct Material
    {
        u32 albedoTex;
        u32 normalTex;
        u32 metallicTex;
        u32 roughnessTex;
        f32 roughness;
        f32 metallic;
        f32 transparency;
        Vec3 colour;
        f32 emissive;
    } Material;

    //=====================================================================================================================
    //vertices and meshes
    typedef struct
    {
        Vec3 *positions; 
        Vec3 *normals;
        Vec2 *texCoords;
        //32 bytes per vertex
        u32 ammount;
    } Vertices;

    Vertices createVertices(const Vec3 pos, const Vec2 texCoord);

    typedef enum
    {
        POSITION_VERTEXBUFFER,
        TEXCOORD_VB,
        NORMAL_VB,
        INDEX_VB,
        TEXID_VB,
        NUM_BUFFERS
    } MeshType;

    typedef struct
    {
        // vertex array object (0 when buffered — uses GeometryBuffer's VAO)
        u32 vao;
        // interleaved vertex buffer object (0 when buffered)
        u32 vbo;
        // index buffer object (0 when buffered)
        u32 ebo;

        u32 subMeshCount;
        u32 drawCount;    // index count (or vertex count for non-indexed)

        // GeometryBuffer fields — only valid when buffered == true
        u32 baseVertex;   // first vertex in the global GeometryBuffer
        u32 firstIndex;   // first index  in the global GeometryBuffer
        b8  buffered;     // true = GPU data lives in resources->geoBuffer
    } Mesh;

    DAPI u32 loadMaterialTexture(struct aiMaterial *mat,
                                 enum aiTextureType type);

    DAPI void readMaterial(Material *out, struct aiMaterial *mat);
    DAPI MaterialUniforms getMaterialUniforms(u32 shader);
    DAPI Mesh *loadMeshFromAssimp(const c8 *filename, u32 *meshCount);
    DAPI Mesh *loadMeshFromAssimpScene(const struct aiScene *scene,
                                       u32 *meshCount);
    DAPI Material *loadMaterialFromAssimp(struct aiScene *scene, u32 *count);

    DAPI void updateMaterial(Material *material,
                             const MaterialUniforms *uniforms);
    // draws a given mesh
    DAPI void drawMesh(Mesh *mesh);
    // creates a mesh from vertices and indices
    DAPI b8 createMesh(Mesh *mesh, const Vertices *vertices, u32 numVertices,
                         const u32 *indices, u32 numIndices);
    // loads a mesh from a mesh file

    // DAPI Mesh* loadMesh(const char* filename, u32* outMeshCount);

    DAPI void initMeshFromModel(Mesh *mesh, const IndexedModel model);
    // free the mesh from memory
    DAPI void freeMesh(Mesh *mesh);

    // creates a plane essentially
    DAPI Mesh *createTerrainMeshWithHeight(u32 cellsX, u32 cellsZ, f32 cellSize,
                                           f32 heightScale,
                                           const c8 *computeShaderPath,
                                           HeightMap *output);
    DAPI Mesh *createTerrainMesh(u32 cellsX, u32 cellsZ,
                                 f32 cellSize);
    DAPI Mesh *createBoxMesh();
    DAPI Mesh *createPlaneMesh();
    DAPI Mesh *createSphereMesh();
    DAPI Mesh *createSkyboxMesh();
    DAPI Mesh *createQuadMesh();


    // Double-buffered instance SSBO: CPU writes to one buffer while GPU reads
    // from the other.  glFenceSync prevents overwriting data the GPU still needs.
    #define INST_BUF_COUNT 2
    typedef struct
    {
        Mat4 *data;      // points to maps[writeIdx] — write here each frame
        u32   buffer[INST_BUF_COUNT];   // two GL buffer objects
        Mat4 *maps[INST_BUF_COUNT];     // persistent mappings per buffer
        void *fences[INST_BUF_COUNT];   // GLsync fences (void* to avoid GL header here)
        u32   capacity;  // fixed at creation (MAX_ENTITY)
        u32   count;     // instances written this frame; reset to 0 before writing
        u32   writeIdx;  // 0 or 1: which buffer the CPU writes to this frame
        b8    ready;
    } InstanceBuffer;

    DAPI void instanceBufferCreate    (InstanceBuffer* buf, u32 capacity);
    DAPI void instanceBufferDestroy   (InstanceBuffer* buf);
    DAPI void instanceBufferFlushRange(InstanceBuffer* buf, u32 offset, u32 count);
    DAPI void instanceBufferAdvance   (InstanceBuffer* buf);  // rotate to next buffer + wait on fence

    //=====================================================================================================================
    // Indirect rendering API — continued from struct definition above (see IndirectBuffer near Renderer)
    //=====================================================================================================================

    DAPI IndirectBuffer *indirectBufferCreate(u32 maxCommands);
    DAPI void           indirectBufferDestroy(IndirectBuffer *buf);
    DAPI void           indirectBufferReset(IndirectBuffer *buf);  // clear for next frame
    DAPI b8             indirectBufferAddCommand(IndirectBuffer *buf, u32 modelID,
                                                  u32 firstIndex, u32 indexCount,
                                                  u32 baseVertex, u32 baseInstance);
    DAPI void           indirectBufferUpload(IndirectBuffer *buf);  // push CPU data to GPU
    DAPI void           indirectBufferDispatch(IndirectBuffer *buf, u32 shaderProgram);  // issue draw

    //=====================================================================================================================
    // GeometryBuffer — single GPU VBO+EBO mega-buffer for all static mesh geometry
    //
    // All mesh vertex and index data is packed into one large allocation on the GPU.
    // Each Mesh records its baseVertex / firstIndex offsets so drawMesh() can issue
    // glDrawElementsBaseVertex without switching buffers.
    //
    // Vertex layout (interleaved, 32 bytes per vertex):
    //   [0..11]  Vec3 position
    //   [12..19] Vec2 texCoord
    //   [20..31] Vec3 normal
    //
    // Usage:
    //   resources->geoBuffer is created automatically in initSystems().
    //   When the buffer is non-NULL, initMeshFromModel() uploads into it
    //   instead of creating standalone VAO/VBO/EBO objects.

    #define GEO_VERTEX_STRIDE 32u   // sizeof(Vec3)+sizeof(Vec2)+sizeof(Vec3)

    typedef struct
    {
        u32  vao;           // single global VAO — shared by all buffered meshes
        u32  vbo;           // vertex buffer (all mesh vertices, interleaved)
        u32  ebo;           // index  buffer (all mesh indices, u32)
        u32  maxVertices;   // capacity in vertex count
        u32  maxIndices;    // capacity in index  count
        u32  vertexCount;   // vertices uploaded so far
        u32  indexCount;    // indices  uploaded so far
    } GeometryBuffer;

    // Create the geometry buffer with room for maxVertices vertices and maxIndices indices.
    // Must be called AFTER the OpenGL context is alive (i.e. after initDisplay).
    DAPI GeometryBuffer *geometryBufferCreate(u32 maxVertices, u32 maxIndices);
    DAPI void            geometryBufferDestroy(GeometryBuffer *buf);

    // Upload one mesh's geometry into the buffer.
    // Fills mesh->baseVertex, mesh->firstIndex, mesh->drawCount, mesh->buffered.
    // Returns false if the buffer is full.
    DAPI b8 geometryBufferUpload(GeometryBuffer *buf, Mesh *mesh,
                                  const void *interleavedVertices, u32 vertexCount,
                                  const u32  *indices,             u32 indexCount);

    // framebuffer
    typedef struct Framebuffer
    {
        u32 fbo;
        u32 texture;
        u32 rbo;
        u32 width;
        u32 height;
        GLenum internalFormat;
        b8 hasDepth;

    } Framebuffer;

    DAPI Framebuffer createFramebuffer(u32 width, u32 height,
                                       GLenum internalFormat, b8 hasDepth);
    DAPI void resizeFramebuffer(Framebuffer *fb, u32 width, u32 height);
    DAPI void bindFramebuffer(Framebuffer *fb);
    DAPI void unbindFramebuffer(void);
    DAPI void destroyFramebuffer(Framebuffer *fb);
   
    typedef struct GBuffer {
        u32 fbo;
        u32 positionTex;
        u32 normalTex;
        u32 albedoSpecTex;
        u32 depthTex;
        u32 width;
        u32 height;
    } GBuffer;
  
    DAPI GBuffer createGBuffer(u32 width, u32 height);

    typedef struct
    {
        c8 *name;           // the name of the model
        u32 *meshIndices;     // buffer of indices that point to the meshes
        u32 *materialIndices; // materials to use for the mesh
        u32 meshCount;        // how many meshes are in the buffer
        u32 materialCount;    // how many materials are in the buffer
        f32 boundingRadius;   // bounding sphere radius at scale=1 (for frustum culling)
    } Model;
    DAPI void draw(Model *model, u32 shader, b8 shouldUpdateMaterials);


    // resource manager
    typedef struct
    {
        Material *materialBuffer;
        Mesh *meshBuffer;
        Model *modelBuffer;
        u32 *textureHandles;
        u32 *shaderHandles;

        // Global geometry pool — owned by the resource manager
        GeometryBuffer *geoBuffer;
        HashMap textureIDs;
        HashMap shaderIDs;
        HashMap mesheIDs;
        HashMap modelIDs;
        HashMap materialIDs;
        // TODO: create seperate meta data struct
        //  meta data
        u32 materialCount;
        u32 meshCount;
        u32 modelCount;
        u32 textureCount;
        u32 shaderCount;

        u32 materialUsed;
        u32 meshUsed;
        u32 modelUsed;
        u32 textureUsed;
        u32 shaderUsed;
    } ResourceManager;

    DAPI extern ResourceManager *resources;

    DAPI ResourceManager *createResourceManager(u32 materialCount,
                                                u32 textureCount, u32 meshCount,
                                                u32 modelCount,
                                                u32 shaderCount);
    void cleanUpResourceManager(ResourceManager *manager);
    DAPI void readResources(ResourceManager *manager, const c8 *filename);

    // typed resource getters
    // Bounds-checked access into the resource manager buffers.
    // Return NULL / 0 when the index is out of range.
    DAPI Mesh     *resGetMesh(u32 index);
    DAPI Model    *resGetModel(u32 index);
    DAPI Material *resGetMaterial(u32 index);
    DAPI u32       resGetTexture(u32 index);
    DAPI u32       resGetShader(u32 index);

    // Name-based lookup: resolves the name through the hash map, then returns
    // the buffer element.  Returns NULL / 0 when not found.
    DAPI Mesh     *resGetMeshByName(const c8 *name);
    DAPI Model    *resGetModelByName(const c8 *name);
    DAPI Material *resGetMaterialByName(const c8 *name);
    DAPI u32       resGetTextureByName(const c8 *name);
    DAPI u32       resGetShaderByName(const c8 *name);

    // shader


    DAPI void loadModelFromAssimp(ResourceManager *manager,
                                  const c8 *filename);
    DAPI void resRegisterPrimitive(ResourceManager *manager,
                                   const c8 *name, Mesh *mesh);

    // keys
    // keyboard keys enum
    typedef enum
    {
        // alphabetical keys
        KEY_A = SDL_SCANCODE_A,
        KEY_B = SDL_SCANCODE_B,
        KEY_C = SDL_SCANCODE_C,
        KEY_D = SDL_SCANCODE_D,
        KEY_E = SDL_SCANCODE_E,
        KEY_F = SDL_SCANCODE_F,
        KEY_G = SDL_SCANCODE_G,
        KEY_H = SDL_SCANCODE_H,
        KEY_I = SDL_SCANCODE_I,
        KEY_J = SDL_SCANCODE_J,
        KEY_K = SDL_SCANCODE_K,
        KEY_L = SDL_SCANCODE_L,
        KEY_M = SDL_SCANCODE_M,
        KEY_N = SDL_SCANCODE_N,
        KEY_O = SDL_SCANCODE_O,
        KEY_P = SDL_SCANCODE_P,
        KEY_Q = SDL_SCANCODE_Q,
        KEY_R = SDL_SCANCODE_R,
        KEY_S = SDL_SCANCODE_S,
        KEY_T = SDL_SCANCODE_T,
        KEY_U = SDL_SCANCODE_U,
        KEY_V = SDL_SCANCODE_V,
        KEY_W = SDL_SCANCODE_W,
        KEY_X = SDL_SCANCODE_X,
        KEY_Y = SDL_SCANCODE_Y,
        KEY_Z = SDL_SCANCODE_Z,

        // Number keys
        KEY_1 = SDL_SCANCODE_1,
        KEY_2 = SDL_SCANCODE_2,
        KEY_3 = SDL_SCANCODE_3,
        KEY_4 = SDL_SCANCODE_4,
        KEY_5 = SDL_SCANCODE_5,
        KEY_6 = SDL_SCANCODE_6,
        KEY_7 = SDL_SCANCODE_7,
        KEY_8 = SDL_SCANCODE_8,
        KEY_9 = SDL_SCANCODE_9,
        KEY_0 = SDL_SCANCODE_0,

        // Function keys
        KEY_F1 = SDL_SCANCODE_F1,
        KEY_F2 = SDL_SCANCODE_F2,
        KEY_F3 = SDL_SCANCODE_F3,
        KEY_F4 = SDL_SCANCODE_F4,
        KEY_F5 = SDL_SCANCODE_F5,
        KEY_F6 = SDL_SCANCODE_F6,
        KEY_F7 = SDL_SCANCODE_F7,
        KEY_F8 = SDL_SCANCODE_F8,
        KEY_F9 = SDL_SCANCODE_F9,
        KEY_F10 = SDL_SCANCODE_F10,
        KEY_F11 = SDL_SCANCODE_F11,
        KEY_F12 = SDL_SCANCODE_F12,
        KEY_F13 = SDL_SCANCODE_F13,
        KEY_F14 = SDL_SCANCODE_F14,
        KEY_F15 = SDL_SCANCODE_F15,
        KEY_F16 = SDL_SCANCODE_F16,
        KEY_F17 = SDL_SCANCODE_F17,
        KEY_F18 = SDL_SCANCODE_F18,
        KEY_F19 = SDL_SCANCODE_F19,
        KEY_F20 = SDL_SCANCODE_F20,
        KEY_F21 = SDL_SCANCODE_F21,
        KEY_F22 = SDL_SCANCODE_F22,
        KEY_F23 = SDL_SCANCODE_F23,
        KEY_F24 = SDL_SCANCODE_F24,

        // Special keys
        KEY_ESCAPE = SDL_SCANCODE_ESCAPE,
        KEY_TAB = SDL_SCANCODE_TAB,
        KEY_CAPSLOCK = SDL_SCANCODE_CAPSLOCK,
        KEY_LSHIFT = SDL_SCANCODE_LSHIFT,
        KEY_RSHIFT = SDL_SCANCODE_RSHIFT,
        KEY_LCTRL = SDL_SCANCODE_LCTRL,
        KEY_RCTRL = SDL_SCANCODE_RCTRL,
        KEY_LALT = SDL_SCANCODE_LALT,
        KEY_RALT = SDL_SCANCODE_RALT,
        KEY_LGUI = SDL_SCANCODE_LGUI, // Windows/Command key
        KEY_RGUI = SDL_SCANCODE_RGUI, // Windows/Command key
        KEY_SPACE = SDL_SCANCODE_SPACE,
        KEY_RETURN = SDL_SCANCODE_RETURN,
        KEY_BACKSPACE = SDL_SCANCODE_BACKSPACE,
        KEY_INSERT = SDL_SCANCODE_INSERT,
        KEY_DELETE = SDL_SCANCODE_DELETE,
        KEY_HOME = SDL_SCANCODE_HOME,
        KEY_END = SDL_SCANCODE_END,
        KEY_PAGEUP = SDL_SCANCODE_PAGEUP,
        KEY_PAGEDOWN = SDL_SCANCODE_PAGEDOWN,

        // Arrow keys
        KEY_RIGHT = SDL_SCANCODE_RIGHT,
        KEY_LEFT = SDL_SCANCODE_LEFT,
        KEY_DOWN = SDL_SCANCODE_DOWN,
        KEY_UP = SDL_SCANCODE_UP,

        // Numpad
        KEY_NUMLOCK = SDL_SCANCODE_NUMLOCKCLEAR,
        KEY_NUMPAD_DIVIDE = SDL_SCANCODE_KP_DIVIDE,
        KEY_NUMPAD_MULTIPLY = SDL_SCANCODE_KP_MULTIPLY,
        KEY_NUMPAD_MINUS = SDL_SCANCODE_KP_MINUS,
        KEY_NUMPAD_PLUS = SDL_SCANCODE_KP_PLUS,
        KEY_NUMPAD_ENTER = SDL_SCANCODE_KP_ENTER,
        KEY_NUMPAD_1 = SDL_SCANCODE_KP_1,
        KEY_NUMPAD_2 = SDL_SCANCODE_KP_2,
        KEY_NUMPAD_3 = SDL_SCANCODE_KP_3,
        KEY_NUMPAD_4 = SDL_SCANCODE_KP_4,
        KEY_NUMPAD_5 = SDL_SCANCODE_KP_5,
        KEY_NUMPAD_6 = SDL_SCANCODE_KP_6,
        KEY_NUMPAD_7 = SDL_SCANCODE_KP_7,
        KEY_NUMPAD_8 = SDL_SCANCODE_KP_8,
        KEY_NUMPAD_9 = SDL_SCANCODE_KP_9,
        KEY_NUMPAD_0 = SDL_SCANCODE_KP_0,
        KEY_NUMPAD_PERIOD = SDL_SCANCODE_KP_PERIOD,

        // Additional keys
        KEY_GRAVE = SDL_SCANCODE_GRAVE, // ` key
        KEY_MINUS = SDL_SCANCODE_MINUS,
        KEY_EQUALS = SDL_SCANCODE_EQUALS,
        KEY_LEFTBRACKET = SDL_SCANCODE_LEFTBRACKET,
        KEY_RIGHTBRACKET = SDL_SCANCODE_RIGHTBRACKET,
        KEY_BACKSLASH = SDL_SCANCODE_BACKSLASH,
        KEY_SEMICOLON = SDL_SCANCODE_SEMICOLON,
        KEY_APOSTROPHE = SDL_SCANCODE_APOSTROPHE,
        KEY_COMMA = SDL_SCANCODE_COMMA,
        KEY_PERIOD = SDL_SCANCODE_PERIOD,
        KEY_SLASH = SDL_SCANCODE_SLASH,

        // Media and application keys
        KEY_PRINTSCREEN = SDL_SCANCODE_PRINTSCREEN,
        KEY_SCROLLLOCK = SDL_SCANCODE_SCROLLLOCK,
        KEY_PAUSE = SDL_SCANCODE_PAUSE,
        KEY_MENU = SDL_SCANCODE_MENU,
        KEY_MUTE = SDL_SCANCODE_MUTE,
        KEY_VOLUMEUP = SDL_SCANCODE_VOLUMEUP,
        KEY_VOLUMEDOWN = SDL_SCANCODE_VOLUMEDOWN,

        // Last key indicator (useful for array sizes)
        KEY_COUNT = SDL_SCANCODE_COUNT,

    } KeyCode;

    typedef enum
    {
        // joystick and gamepad inputs
        BUTTON_X = SDL_GAMEPAD_BUTTON_EAST,
        BUTTON_Y = SDL_GAMEPAD_BUTTON_NORTH,
        BUTTON_A = SDL_GAMEPAD_BUTTON_SOUTH,
        BUTTON_B = SDL_GAMEPAD_BUTTON_WEST,
        BUTTON_SQUARE = SDL_GAMEPAD_BUTTON_WEST,
        BUTTON_CIRCLE = SDL_GAMEPAD_BUTTON_EAST,
        BUTTON_TRIANGLE = SDL_GAMEPAD_BUTTON_NORTH,
        BUTTON_CROSS = SDL_GAMEPAD_BUTTON_SOUTH,

        BUTTON_BACK = SDL_GAMEPAD_BUTTON_BACK,
        BUTTON_GUIDE = SDL_GAMEPAD_BUTTON_GUIDE,
        BUTTON_START = SDL_GAMEPAD_BUTTON_START,
        BUTTON_LEFTSTICK = SDL_GAMEPAD_BUTTON_LEFT_STICK,
        BUTTON_RIGHTSTICK = SDL_GAMEPAD_BUTTON_RIGHT_STICK,
        BUTTON_LEFTSHOULDER = SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,
        BUTTON_RIGHTSHOULDER = SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
        // D-pad buttons
        BUTTON_DPAD_UP = SDL_GAMEPAD_BUTTON_DPAD_UP,
        BUTTON_DPAD_DOWN = SDL_GAMEPAD_BUTTON_DPAD_DOWN,
        BUTTON_DPAD_LEFT = SDL_GAMEPAD_BUTTON_DPAD_LEFT,
        BUTTON_DPAD_RIGHT = SDL_GAMEPAD_BUTTON_DPAD_RIGHT
    } ControllerCode;

    typedef enum
    {
        JOYSTICK_LEFT_X = SDL_GAMEPAD_AXIS_LEFTX,
        JOYSTICK_RIGHT_X = SDL_GAMEPAD_AXIS_RIGHTX,
        JOYSTICK_LEFT_Y = SDL_GAMEPAD_AXIS_LEFTY,
        JOYSTICK_RIGHT_Y = SDL_GAMEPAD_AXIS_RIGHTY,
        JOYSTICK_TRIGGER_LEFT = SDL_GAMEPAD_AXIS_LEFT_TRIGGER,
        JOYSTICK_TRIGGER_RIGHT = SDL_GAMEPAD_AXIS_RIGHT_TRIGGER
    } JoystickCode;

    // Mouse buttons enum
    enum MouseButton
    {
        MOUSE_LEFT = 0,
        MOUSE_RIGHT = 1,
        MOUSE_MIDDLE = 2,
        MOUSE_X1 = 3,
        MOUSE_X2 = 4,
        MOUSE_BUTTON_COUNT = 5
    };

    // APPLICATION

    enum ApplicationState
    {
        RUN,
        EXIT
    };

    //=====================================================================================================================
    // Profiler — per-frame scoped timing, GPU timing, draw/geometry/state counters
    //
    // Usage:
    //   PROFILE_SCOPE("label");           // auto-timed scope (cleanup attribute)
    //   PROFILE_CYCLES_BEGIN(var);         // manual cycle range start
    //   PROFILE_CYCLES_END(var, out);      // manual cycle range end
    //
    // Enabled when DRUID_PROFILE is defined (-DDRUID_PROFILE in CMake).
    //=====================================================================================================================

#define PROFILE_MAX_ENTRIES 128

    // Inline RDTSC — ~5 cycles, no function call overhead
    static inline u64 profileRDTSC_(void)
    {
    #if defined(_MSC_VER)
        return __rdtsc();
    #elif defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
        u32 lo, hi;
        __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
        return ((u64)hi << 32) | lo;
    #else
        return SDL_GetPerformanceCounter();
    #endif
    }

    typedef struct
    {
        const c8 *name;
        u64       cycles;
        f64       elapsed_us;
    } ProfileEntry;

    typedef struct
    {
        // scoped timing entries
        ProfileEntry entries[PROFILE_MAX_ENTRIES];
        u32          count;

        // CPU frame timing (RDTSC)
        u64          frameCycles;
        f64          frameTime_us;

        // GPU frame timing (GL_TIME_ELAPSED, double-buffered)
        f64          gpuFrameTime_us;

        // draw call / geometry counters
        u32          drawCalls;
        u32          triangles;
        u32          vertices;
        u32          entityCount;

        // GL state change counters (per frame)
        u32          shaderBinds;       // glUseProgram calls
        u32          textureBinds;      // glBindTexture calls
        u32          vaoBinds;          // glBindVertexArray calls
        u32          bufferBinds;       // glBindBuffer calls (VBO/EBO/SSBO)
        u32          uniformUploads;    // uniform set calls (glUniform*)
        u32          fboBinds;          // glBindFramebuffer calls

        // buffer upload stats
        u32          bufferUploadsCount;    // number of glBufferSubData / glBufferData calls
        u64          bufferUploadBytes;     // total bytes uploaded to GPU this frame

        // memory allocation tracking (malloc/free)
        u64          heapAllocBytes;    // total bytes allocated this frame
        u32          heapAllocCount;    // number of malloc calls this frame
        u64          heapFreeCount;     // number of free calls this frame
        u64          heapLiveBytes;     // running total of live heap (alloc - free)

        // GPU pipeline queries (GL_PRIMITIVES_GENERATED)
        u64          primitivesGenerated;

        // Cache analysis (computed per-test, constant for a given entity count + layout)
        u64          cacheWorkingSetBytes;    // total bytes accessed per frame (hot path)
        u64          cacheLinesAccessed;      // unique 64B cache lines touched
        u64          cacheUsefulBytes;        // bytes actually read/written from those lines
        u64          cacheWastedBytes;        // bytes loaded but not used (pollution)
        f64          cacheUtilisation;        // useful / fetched as 0-100%
        u64          estL1Misses;             // estimated L1 misses (working set > L1)
        u64          estL2Misses;             // estimated L2 misses (working set > L2)
        u64          estL3Misses;             // estimated L3 misses → RAM accesses
    } ProfileFrame;

    //=====================================================================================================================
    // Cache topology — detected at startup via OS API (Windows: GetLogicalProcessorInformation)
    //=====================================================================================================================
    typedef struct
    {
        u32 l1dSize;       // L1 data cache size in bytes
        u32 l2Size;        // L2 cache size in bytes
        u32 l3Size;        // L3 cache size in bytes
        u32 lineSize;      // cache line size in bytes (typically 64)
    } CacheInfo;

    // Detect CPU cache topology at startup. Fills CacheInfo with L1/L2/L3 sizes.
    DAPI void profileDetectCaches(CacheInfo *out);

    // Get the detected cache info (call profileDetectCaches first)
    DAPI const CacheInfo *profileGetCacheInfo(void);

    // Estimate cache performance for a known access pattern.
    // entityCount: number of entities iterated
    // bytesPerEntity: total bytes fetched per entity (e.g. sizeof(SpaceBodyAoS) = 116)
    // usefulBytesPerEntity: bytes actually used per entity (e.g. 56 for physics hot fields)
    // numArrays: number of separate contiguous arrays (SoA = 14, AoS = 1)
    // Results written into the current ProfileFrame cache fields.
    DAPI void profileEstimateCache(u32 entityCount, u32 bytesPerEntity,
                                   u32 usefulBytesPerEntity, u32 numArrays);

    typedef struct { const c8 *name; u64 startCycles; } ProfileScope_;

    DAPI void profileScopeEnd_(ProfileScope_ *s);

    DAPI void               profileCalibrate(void);
    DAPI void               profileBeginFrame(void);
    DAPI void               profileEndFrame(void);
    DAPI void               profileRecordEntry(const c8 *name, u64 cycles, f64 elapsed_us);
    DAPI const ProfileFrame *profileGetCurrentFrame(void);

    // geometry counters (called from drawMesh, etc.)
    DAPI void               profileAddTriangles(u32 count);
    DAPI void               profileAddVertices(u32 count);
    DAPI void               profileAddEntities(u32 count);

    // GL state change tracking (call from wrappers or manually)
    DAPI void               profileCountShaderBind(void);
    DAPI void               profileCountTextureBind(void);
    DAPI void               profileCountVAOBind(void);
    DAPI void               profileCountBufferBind(void);
    DAPI void               profileCountUniformUpload(void);
    DAPI void               profileCountFBOBind(void);
    DAPI void               profileCountBufferUpload(u64 bytes);

    // memory tracking
    DAPI void               profileCountAlloc(u64 bytes);
    DAPI void               profileCountFree(u64 bytes);

    DAPI extern u32 g_drawCalls;
    DAPI extern u32 g_triangles;
    DAPI extern u32 g_vertices;

#ifdef DRUID_PROFILE
#define PROFILE_SCOPE(label) \
    ProfileScope_ _pscope_##__LINE__ __attribute__((cleanup(profileScopeEnd_))) = \
        { (label), profileRDTSC_() }
#define PROFILE_CYCLES_BEGIN(var) u64 var = profileRDTSC_()
#define PROFILE_CYCLES_END(var, out) (out) = profileRDTSC_() - (var)
#else
#define PROFILE_SCOPE(label) ((void)0)
#define PROFILE_CYCLES_BEGIN(var) ((void)0)
#define PROFILE_CYCLES_END(var, out) ((void)0)
#endif

    //=====================================================================================================================

    extern double FPS;
    typedef struct
    {
        // function pointers
        void (*init)();
        void (*update)(f32);
        void (*render)(f32);
        void (*destroy)();
        void (*inputProcess)(void *);
        c8 title[MAX_PATH_LENGTH];
        // open gl context with sdl within the display
        
        Display *display;
        enum ApplicationState state;
        f32 width;
        f32 height;
        f64 fps;

    } Application;

    // function pointer typedef
    typedef void (*FncPtrFloat)(f32);
    typedef void (*FncPtr)();

    DAPI Application *createApplication(const c8* title,FncPtr init, FncPtrFloat update, FncPtrFloat render, FncPtr destroy);
    DAPI void run(Application *app);
    DAPI void destroyApplication(Application *app);

    // methods to make the application run
    DAPI void initSystems(const Application *app);
    DAPI void startApplication(Application *app);

    DAPI void applicationRenderStep(Application *app, f32 dt);

    // Input

    void initInput();
    void destroyInput();

    DAPI void processInput(Application *app);

    DAPI b8 isKeyDown(KeyCode key);
    DAPI b8 isKeyUp(KeyCode key);

    DAPI b8 isButtonDown(u32 controllerID, ControllerCode button);
    DAPI b8 isButtonUp(u32 controllerID, ControllerCode button);

    DAPI b8 isMouseDown(u32 button);

    DAPI void getMouseDelta(f32 *x, f32 *y);

    #define GAMEPAD_MAX 4
    DAPI extern SDL_Gamepad *gamepads[GAMEPAD_MAX];

    DAPI void checkForGamepadConnection(SDL_Event *event);
    DAPI void checkForGamepadRemoved(SDL_Event *event);

    DAPI Vec2 getKeyboardAxis();
    DAPI Vec2 getJoystickAxis(u32 controllerID, JoystickCode axis1,
                              JoystickCode axis2);
    DAPI void updateInputAxes(void);
    DAPI Vec2 getInputAxis(void);
    DAPI Vec2 getLookAxis(void);
    DAPI f32  getInputAxisX(void);
    DAPI f32  getInputAxisY(void);
    DAPI f32  getLookAxisX(void);
    DAPI f32  getLookAxisY(void);
    DAPI void setInputCaptureState(b8 captured);

    DAPI extern f32 xInputAxis;
    DAPI extern f32 yInputAxis;
    DAPI extern f32 xLookAxis;
    DAPI extern f32 yLookAxis;

    //=====================================================================================================================
    // Colliders

    typedef enum
    {
        COLLIDER_CIRCLE   = 0,
        COLLIDER_BOX_2D   = 1,
        COLLIDER_SPHERE   = 2,
        COLLIDER_BOX      = 3,
        COLLIDER_CYLINDER = 4,
        COLLIDER_MESH     = 5
    } ColliderType;

    typedef struct { Vec3 min; Vec3 max; } AABB;

    typedef struct
    {
        void *state;
        void (*response)(u32 self, u32 other);
        void (*onTrigger)(u32 self, u32 other);
        ColliderType type;
        i32 layer;
        b8 isColliding;
        b8 isTrigger;
    } Collider;

    // Legacy 2D
    DAPI Collider *createCircleCollider(f32 radius);
    DAPI Collider *createBoxCollider(Vec2 scale);
    DAPI b8 isCircleColliding(Vec2 posA, f32 radA, Vec2 posB, f32 radB);
    DAPI b8 isBoxColliding(Vec2 posA, Vec2 scaleA, Vec2 posB, Vec2 scaleB);

    // 3D creation
    DAPI Collider *createSphereCollider(f32 radius);
    DAPI Collider *createCubeCollider(Vec3 halfExtents);
    DAPI Collider *createCylinderCollider(f32 radius, f32 halfHeight);
    DAPI Collider *createMeshCollider3D(const Vec3 *vertices, const u32 *indices,
                                        u32 vertexCount, u32 indexCount);
    DAPI Collider *createMeshCollider(Mesh *mesh, Transform *transform);

    // Utilities
    DAPI b8   cleanCollider(Collider *col);
    DAPI f32  getRadius(Collider *col);
    DAPI Vec2 getScale(Collider *col);
    DAPI Vec3 getHalfExtents(Collider *col);
    DAPI b8   setBoxScale(Collider *col, Vec2 scale);
    DAPI AABB colliderComputeAABB(Collider *col, Vec3 pos);
    DAPI b8   isAABBOverlapping(AABB a, AABB b);

    // 3D collision detection
    DAPI b8 isSphereVsSphere(Vec3 posA, f32 radA, Vec3 posB, f32 radB);
    DAPI b8 isBoxVsBox(Vec3 posA, Vec3 halfA, Vec3 posB, Vec3 halfB);
    DAPI b8 isSphereVsBox(Vec3 spherePos, f32 radius, Vec3 boxPos, Vec3 boxHalf);
    DAPI b8 isSphereVsCylinder(Vec3 spherePos, f32 sphereRad,
                                Vec3 cylPos, f32 cylRad, f32 cylHalfH);
    DAPI b8 isCylinderVsCylinder(Vec3 posA, f32 radA, f32 halfHA,
                                  Vec3 posB, f32 radB, f32 halfHB);
    DAPI b8 isBoxVsCylinder(Vec3 boxPos, Vec3 boxHalf,
                             Vec3 cylPos, f32 cylRad, f32 cylHalfH);
    DAPI b8 isMeshVsSphere(Collider *meshCol, Vec3 meshPos,
                            Vec3 spherePos, f32 sphereRad);
    DAPI b8 isMeshVsBox(Collider *meshCol, Vec3 meshPos,
                         Vec3 boxPos, Vec3 boxHalf);

    // Generic dispatch
    DAPI b8 collidersOverlap(Collider *a, Vec3 posA, Collider *b, Vec3 posB);

    //=====================================================================================================================
    // Physics Engine

    typedef enum { PHYS_BODY_STATIC = 0, PHYS_BODY_DYNAMIC = 1, PHYS_BODY_KINEMATIC = 2 } PhysBodyType;
    typedef enum { PHYS_SHAPE_SPHERE, PHYS_SHAPE_BOX, PHYS_SHAPE_CYLINDER, PHYS_SHAPE_MESH, PHYS_SHAPE_COUNT } PhysShapeType;

    typedef struct { f32 friction; f32 restitution; f32 density; } PhysMaterial;
    typedef struct { Vec3 point; Vec3 normal; f32 depth; } ContactPoint;

    #define MAX_CONTACT_POINTS 4

    typedef struct
    {
        u32          bodyA;
        u32          bodyB;
        ContactPoint points[MAX_CONTACT_POINTS];
        u32          pointCount;
    } ContactManifold;

    typedef void (*CollisionFn)(u32 self, u32 other, const ContactManifold *manifold);
    typedef void (*TriggerFn)(u32 self, u32 other);

    typedef struct { Vec3 origin; Vec3 direction; f32 maxDistance; u32 layerMask; } PhysRay;
    typedef struct { Vec3 point; Vec3 normal; f32 distance; u32 bodyIndex; b8 hit; } PhysRayHit;

    // Opaque — defined in physics.c (PhysicsWorld forward-declared before Archetype)
    typedef struct SpatialHashGrid SpatialHashGrid;

    // Include in any archetype definition to make it a physics body
    #define PHYSICS_BODY_FIELDS            \
    FIELD(u32, PhysicsBodyType),           \
    FIELD(u32, ColliderHandle),            \
    VEC3_FIELDS(LinearVelocity),           \
    VEC3_FIELDS(AngularVelocity),          \
    VEC3_FIELDS(Force),                    \
    VEC3_FIELDS(Torque),                   \
    FIELD(f32, Mass),                      \
    FIELD(f32, InvMass),                   \
    FIELD(f32, LinearDamping),             \
    FIELD(f32, AngularDamping),            \
    FIELD(u32, CollisionLayer),            \
    FIELD(u32, CollisionMask)

    // Physics DLL plugin
    typedef void (*PhysicsInitFn)(PhysicsWorld *world);
    typedef void (*PhysicsStepFn)(PhysicsWorld *world, f32 dt);
    typedef void (*PhysicsShutdownFn)(PhysicsWorld *world);

    typedef struct
    {
        PhysicsInitFn     init;
        PhysicsStepFn     step;
        PhysicsShutdownFn shutdown;
    } PhysicsPlugin;

    typedef void (*GetPhysicsPluginFn)(PhysicsPlugin *out);

    typedef struct
    {
        DLLHandle     dll;
        PhysicsPlugin plugin;
        b8            loaded;
    } PhysicsDLL;

    // World
    DAPI PhysicsWorld *physWorldCreate(Vec3 gravity, f32 timestep);
    DAPI void          physWorldDestroy(PhysicsWorld *world);
    DAPI void          physWorldStep(PhysicsWorld *world, f32 dt);
    DAPI void          physWorldSetGravity(PhysicsWorld *world, Vec3 gravity);
    DAPI void          physWorldSetIterations(PhysicsWorld *world, u32 iterations);
    DAPI void          physWorldSetSubsteps(PhysicsWorld *world, u32 substeps);
    DAPI void          physWorldSetCollisionCallback(PhysicsWorld *world, CollisionFn fn);
    DAPI void          physWorldSetTriggerCallback(PhysicsWorld *world, TriggerFn fn);

    // feed frustum cull results to physics for LOD skipping
    DAPI void          physWorldSetVisibility(PhysicsWorld *world, const b8 *visible,
                                              u32 count, u32 lodInterval);

    // Entity management — engine tracks physics archetypes
    DAPI void          physRegisterArchetype(PhysicsWorld *world, Archetype *arch);
    DAPI u32           physGetBodyArchetypeCount(PhysicsWorld *world);
    DAPI Archetype    *physGetBodyArchetype(PhysicsWorld *world, u32 index);

    // Colliders (handle-based)
    DAPI u32  physColliderCreateSphere(PhysicsWorld *world, f32 radius);
    DAPI u32  physColliderCreateBox(PhysicsWorld *world, Vec3 halfExtents);
    DAPI u32  physColliderCreateCylinder(PhysicsWorld *world, f32 radius, f32 halfHeight);
    DAPI u32  physColliderCreateMesh(PhysicsWorld *world, const Vec3 *verts, const u32 *indices,
                                     u32 vertCount, u32 idxCount);
    DAPI void physColliderDestroy(PhysicsWorld *world, u32 handle);
    DAPI void physColliderSetOffset(PhysicsWorld *world, u32 handle, Vec3 offset);
    DAPI void physColliderSetTrigger(PhysicsWorld *world, u32 handle, b8 isTrigger);
    DAPI void physColliderSetMaterial(PhysicsWorld *world, u32 handle, PhysMaterial mat);
    DAPI AABB physColliderComputeAABB(PhysicsWorld *world, u32 handle, Vec3 pos, Vec4 rot);

    // Rigidbody
    DAPI void physBodyApplyForce(PhysicsWorld *world, Archetype *arch, u32 index, Vec3 force);
    DAPI void physBodyApplyTorque(PhysicsWorld *world, Archetype *arch, u32 index, Vec3 torque);
    DAPI void physBodyApplyImpulse(PhysicsWorld *world, Archetype *arch, u32 index, Vec3 impulse);
    DAPI void physBodyApplyImpulseAt(PhysicsWorld *world, Archetype *arch, u32 index,
                                     Vec3 impulse, Vec3 point);

    // Broadphase
    DAPI void physBroadphaseRebuild(PhysicsWorld *world);
    DAPI u32  physBroadphaseQuery(PhysicsWorld *world, AABB query, u32 *outBodies, u32 maxBodies);

    // Raycasting
    DAPI PhysRayHit physRaycast(PhysicsWorld *world, PhysRay ray);
    DAPI u32        physRaycastAll(PhysicsWorld *world, PhysRay ray, PhysRayHit *hits, u32 maxHits);

    // Overlap queries
    DAPI u32 physOverlapSphere(PhysicsWorld *world, Vec3 center, f32 radius,
                               u32 *outBodies, u32 maxBodies);
    DAPI u32 physOverlapBox(PhysicsWorld *world, Vec3 center, Vec3 halfExtents,
                            u32 *outBodies, u32 maxBodies);

    // Physics DLL
    DAPI b8   loadPhysicsDLL(const c8 *dllPath, PhysicsDLL *out);
    DAPI void unloadPhysicsDLL(PhysicsDLL *dll);

    // Global physics world (like renderer — created once, accessible everywhere)
    DAPI extern PhysicsWorld *physicsWorld;

    // Engine-side physics lifecycle — call from your app/editor loop
    DAPI PhysicsWorld *physInit(Vec3 gravity, f32 timestep);
    DAPI void          physShutdown(void);
    DAPI void          physTick(f32 dt);

    // Auto-registers all archetypes with isPhysicsBody == true from a scene
    DAPI void physAutoRegisterScene(SceneData *scene);

    //=====================================================================================================================
    // Debug Gizmos — immediate-mode debug drawing

    typedef struct { f32 r, g, b, a; } GizmoColor;

    #define GIZMO_RED    (GizmoColor){1, 0, 0, 1}
    #define GIZMO_GREEN  (GizmoColor){0, 1, 0, 1}
    #define GIZMO_BLUE   (GizmoColor){0, 0, 1, 1}
    #define GIZMO_YELLOW (GizmoColor){1, 1, 0, 1}
    #define GIZMO_CYAN   (GizmoColor){0, 1, 1, 1}
    #define GIZMO_WHITE  (GizmoColor){1, 1, 1, 1}

    DAPI void gizmoInit(void);
    DAPI void gizmoShutdown(void);
    DAPI void gizmoBeginFrame(void);
    DAPI void gizmoEndFrame(Mat4 viewProj);

    // Primitives
    DAPI void gizmoDrawLine(Vec3 from, Vec3 to, GizmoColor color);
    DAPI void gizmoDrawRay(Vec3 origin, Vec3 direction, f32 length, GizmoColor color);
    DAPI void gizmoDrawArrow(Vec3 from, Vec3 to, f32 headSize, GizmoColor color);

    // Shapes
    DAPI void gizmoDrawSphere(Vec3 center, f32 radius, GizmoColor color);
    DAPI void gizmoDrawBox(Vec3 center, Vec3 halfExtents, GizmoColor color);
    DAPI void gizmoDrawCylinder(Vec3 center, f32 radius, f32 halfHeight, GizmoColor color);
    DAPI void gizmoDrawAABB(AABB box, GizmoColor color);
    DAPI void gizmoDrawCircle(Vec3 center, Vec3 normal, f32 radius, GizmoColor color);

    // Grids & axes
    DAPI void gizmoDrawGrid(Vec3 center, f32 size, u32 divisions, GizmoColor color);
    DAPI void gizmoDrawAxes(Vec3 origin, f32 length);
    DAPI void gizmoDrawTransform(Transform *t, f32 length);

    // Physics debug
    DAPI void gizmoDrawCollider(Collider *col, Vec3 pos, GizmoColor color);
    DAPI void gizmoDrawContactManifold(const ContactManifold *manifold, GizmoColor color);

#ifdef __cplusplus
}
#endif
