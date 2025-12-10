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

// Forward declarations for Assimp structures
struct aiScene;
struct aiMaterial;

//=====================================================================================================================
// Unsigned int types.
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

// Signed int types.
typedef signed char i8;
typedef signed short i16;
typedef signed int i32;
typedef signed long long i64;

// Floating point types
typedef float f32;
typedef double f64;

// Boolean types
typedef int b32;
typedef bool b8;

// Compile-time assertion macro
#ifdef __cplusplus
#define STATIC_ASSERT(COND, MSG) static_assert(COND, MSG)
#else
#define STATIC_ASSERT(COND, MSG) _Static_assert(COND, MSG)
#endif

// Ensure all types are of the correct size.
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

// Platform detection
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__)
#define PLATFORM_WINDOWS 1
#ifndef _WIN32
#error "64-bit is required on Windows!"
#endif
#elif defined(__linux__) || defined(__gnu_linux__)
// Linux OS
#define PLATFORM_LINUX 1
#if defined(__ANDROID__)
#define PLATFORM_ANDROID 1
#endif
#elif defined(__unix__)
// Catch anything not caught by the above.
#define PLATFORM_UNIX 1
#elif defined(_POSIX_VERSION)
// Posix
#define PLATFORM_POSIX 1
#elif __APPLE__
// Apple platforms
#define PLATFORM_APPLE 1
#include <TargetConditionals.h>
#if TARGET_IPHONE_SIMULATOR
// iOS Simulator
#define PLATFORM_IOS 1
#define PLATFORM_IOS_SIMULATOR 1
#elif TARGET_OS_IPHONE
#define PLATFORM_IOS 1
// iOS device
#elif TARGET_OS_MAC
// Other kinds of Mac OS
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

#define RELEASE_BUILD 0

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

    bool initLogging();
    void shutdownLogging();

    DAPI extern void (*logOutputSrc)(LogLevel level, const char *msg);
    DAPI extern b8 useCustomOutputSrc;
    DAPI void logOutput(LogLevel level, const char *message, ...);

#define FATAL(message, ...) logOutput(LOG_FATAL, message, ##__VA_ARGS__)
#define ERROR(message, ...) logOutput(LOG_ERROR, message, ##__VA_ARGS__)
#define WARN(message, ...) logOutput(LOG_WARNING, message, ##__VA_ARGS__)
#define INFO(message, ...) logOutput(LOG_INFO, message, ##__VA_ARGS__)
#define DEBUG(message, ...) logOutput(LOG_DEBUG, message, ##__VA_ARGS__)
#define TRACE(message, ...) logOutput(LOG_TRACE, message, ##__VA_ARGS__)

    //=====================================================================================================================
    // MATHS
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

    // 2D vector methods
    DAPI Vec2 v2Add(Vec2 a, Vec2 b);
    DAPI Vec2 v2Sub(Vec2 a, Vec2 b);
    DAPI Vec2 v2Scale(Vec2 a, f32 b);
    DAPI Vec2 v2Mul(Vec2 a, Vec2 b);
    DAPI f32 v2Mag(Vec2 a);
    DAPI f32 v2Dis(Vec2 a, Vec2 b);
    DAPI Vec2i v2Tov2i(Vec2 a);
    DAPI Vec2 v2iTov2(Vec2i a);
    DAPI Vec2 v2Div(Vec2 a, f32 b);

    DAPI bool v2Equal(Vec2 a, Vec2 b);

    // 3D vector methods

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
    DAPI bool v3Equal(Vec3 a, Vec3 b);
    DAPI f32 v3Dot(Vec3 a, Vec3 b);

    // Quaternions
    // Quaternion operations
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

    // Matrix methods
    DAPI void matAdd(f32 **a, f32 **b, Vec2i aSize);
    DAPI void matSub(f32 **a, f32 **b, Vec2i aSize);
    DAPI void matDiv(f32 **a, f32 **b, Vec2i aSize);
    DAPI void matMul(f32 **a, f32 **b, Vec2i aSize);
    DAPI void matScale(f32 **a, f32 b, Vec2i aSize);

    // creates a heap allocated matrix, try avoid using this
    // TODO: refactor this
    DAPI f32 **matCreate(Vec2i size);
    DAPI void freeMat(f32 **mat, Vec2i size);

    DAPI Mat4 mat4LookAt(Vec3 eye, Vec3 target, Vec3 up);

    DAPI Mat4 mat4Perspective(f32 fovRadians, f32 aspect, f32 nearZ, f32 farZ);
    // Identity and Zero
    DAPI Mat4 mat4Identity(void);
    DAPI Mat4 mat4Zero(void);

    // Transformation matrices
    DAPI Mat4 mat4Translate(Mat4 in, Vec3 translation);
    DAPI Mat4 mat4Scale(f32 scale);
    DAPI Mat4 mat4ScaleVec(Vec3 scale);
    DAPI Mat4 mat4ScaleVal(Mat4 a, float scale);
    DAPI Mat4 mat4RotateX(f32 angleRadians);
    DAPI Mat4 mat4RotateY(f32 angleRadians);
    DAPI Mat4 mat4Rotate(f32 angleRadians, Vec3 axis);

    // Matrix math
    DAPI Mat4 mat4Mul(Mat4 a, Mat4 b);
    DAPI Mat4 mat4Add(Mat4 a, Mat4 b);
    DAPI Mat4 mat4Sub(Mat4 a, Mat4 b);
    DAPI Mat4 mat4ScaleMatrix(Mat4 a, f32 scale);

    // Vector transformation
    DAPI Vec4 mat4TransformVec4(Mat4 m, Vec4 v);
    DAPI Vec3 mat4TransformPoint(Mat4 m, Vec3 p);
    DAPI Vec3 mat4TransformDirection(Mat4 m, Vec3 d);

    // Determinant and Inverse
    DAPI f32 mat4Determinant(Mat4 m);
    DAPI Mat4 mat4Inverse(Mat4 m);

    DAPI Mat4 mat4Perspective(f32 fovRadians, f32 aspect, f32 nearZ, f32 farZ);
    DAPI Mat3 mat4ToMat3(const Mat4 m4);
    DAPI Mat4 mat3ToMat4(const Mat3 m3);
    // helper tools
    DAPI f32 clamp(f32 value, f32 minVal, f32 maxVal);
    DAPI f32 degrees(f32 radians);
    DAPI f32 radians(f32 degrees);

    DAPI f32 lerp(f32 a, f32 b, f32 t);
// constants
#define PI 3.14159265358979323846f
    static const Vec3 v3Zero = {0.0f, 0.0f, 0.0f};
    static const Vec3 v3One = {1.0f, 1.0f, 1.0f};

    static const Vec3 v3Up = {0.0f, 1.0f, 0.0f};
    static const Vec3 v3Down = {0.0f, -1.0f, 0.0f};

    static const Vec3 v3Right = {1.0f, 0.0f, 0.0f};
    static const Vec3 v3Left = {-1.0f, 0.0f, 0.0f};

    static const Vec3 v3Forward = {0.0f, 0.0f, -1.0f};
    static const Vec3 v3Back = {0.0f, 0.0f, 1.0f};
    //=====================================================================================================================
    // Arenas

    typedef struct
    {
        u32 size;
        u32 used;
    } Arena;

    DAPI bool arenaCreate(Arena *arena, u32 maxSize);
    DAPI void *aalloc(Arena *arena, u32 size);
    DAPI void arenaDestroy(Arena *arena);

    //=====================================================================================================================
    // Hash Map

    // Thank you Jacob Sorber for your video on hash tables in C

#define MAX__NAME 256

    typedef struct
    {
        void *key;     // pointer to key data
        void *value;   // pointer to value data
        bool occupied; // whether this slot is in use
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
        bool (*equalsFunc)(const void *keyA, const void *keyB);
    } HashMap;

    DAPI bool createMap(HashMap *map, u32 capacity, u32 keySize, u32 valueSize,
                        u32 (*hashFunc)(const void *, u32),
                        bool (*equalsFunc)(const void *, const void *));
    DAPI u32 hash(char *name, u32 mapSize);
    DAPI void printMap(HashMap *map);
    DAPI bool insertMap(HashMap *map, const void *key, const void *value);
    DAPI void destroyMap(HashMap *map);

    DAPI bool findInMap(HashMap *map, const void *key, void *outValue);

    //=====================================================================================================================
    // SOA ECS
    typedef struct
    {
        const char *name;
        u32 size;
    } FieldInfo;

    typedef struct
    {
        const char *name;
        FieldInfo *fields;
        u32 count;
    } StructLayout;

    typedef struct
    {
        u32 count;
        u32 entityCount;
        StructLayout *layout;
        void *data;
        void **fields;
    } EntityArena;

    // create Entity Arena
    DAPI EntityArena *createEntityArena(StructLayout *layout, u32 entityCount,
                                        u32 *outArenas);

    DAPI void printEntityArena(EntityArena *arena);

    // free the arena
    DAPI bool freeEntityArena(EntityArena *arena, u32 arenaCount);
    DAPI u32 createEntity(EntityArena *arena);

    // Low-level arena API: remove entity at index from an arena. Returns true
    // on success.
    DAPI b8 removeEntityFromArena(EntityArena *arena, u32 index);

    // calculate Entity Size based of a struct layout
    DAPI u32 getEntitySize(StructLayout *layout);

#define FIELD_OF(Type, field)                                                  \
    {                                                                          \
        #field, sizeof(((Type *)0)->field)                                     \
    }

#define FIELD(type, name)                                                      \
    {                                                                          \
        #name, sizeof(type)                                                    \
    }

#define VEC3_FIELDS(name)                                                      \
    {#name "X", sizeof(float)}, {#name "Y", sizeof(float)},                    \
    {                                                                          \
        #name "Z", sizeof(float)                                               \
    }

#define VEC4_FIELDS(name)                                                      \
    {#name "X", sizeof(float)}, {#name "Y", sizeof(float)},                    \
        {#name "Z", sizeof(float)},                                            \
    {                                                                          \
        #name "W", sizeof(float)                                               \
    }

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
    // Archetypes

    typedef struct Archetype
    {
        u32 id;
        StructLayout *layout;
        EntityArena *arena;
        u32 arenaCount;
        u32 capacity;
    } Archetype;

    // Archetype API (declarations only) - implementations live in world model
    DAPI b8 createArchetype(StructLayout *layout, u32 capacity,
                            Archetype *outArchetype);
    DAPI b8 destroyArchetype(Archetype *arch);

    // Create an entity in the given archetype. Returns a packed u64 handle
    // (arch id + index) via outEntity. Returns true on success.
    DAPI b8 removeEntityFromArchetype(Archetype *arch, u32 arenaIndex,
                                      u32 index);

    // Get the archetype soa field pointers for the given arena index.
    DAPI void **getArchetypeFields(Archetype *arch, u32 arenaIndex);

    //=====================================================================================================================

    typedef void (*SystemFn)(Archetype, f32);

    // Entity manager
    typedef struct
    {
        u32 archetypeCount;
        Archetype *archetypes;
        SystemFn *systems;
        HashMap indexMap;
    } EntityManager;

    // ------------------------------------------------------------------
    // Scenes

#define MAX_SCENE_NAME 128
    typedef struct
    {
        EntityManager manager;

    } Scene;

    typedef struct
    {
        u32 archetypeCount;
        char *archetypeNames[MAX_SCENE_NAME];
        Archetype *archetypes;
    } SceneData;

    typedef struct
    {
        Arena *data;
        SceneData *scenes;
        Scene *currentScene;
        u32 sceneCount;    // number of scenes currently stored
        u32 sceneCapacity; // allocated capacity
    } SceneManager;

    DAPI extern SceneManager *sceneManager;
// Define an editor-visible name size constant if not present elsewhere
#ifndef MAX_NAME_SIZE
#define MAX_NAME_SIZE 256
#endif

    // Scene manager API
    DAPI SceneManager *createSceneManager(u32 sceneCapacity);
    DAPI void destroySceneManager(SceneManager *manager);
    DAPI u32 addScene(SceneManager *manager, SceneData *sceneData);
    DAPI void removeScene(SceneManager *manager, u32 sceneIndex);
    DAPI void switchScene(SceneManager *manager, u32 sceneIndex);

    // Persist/load SceneData to disk (implementation flexible)
    DAPI void saveScene(const char *filePath, SceneData *data);
    DAPI SceneData loadScene(const char *filePath);
    DAPI SceneData bakeScene(Scene *scene);
    // ------------------------------------------------------------------

    //=====================================================================================================================

    typedef struct
    {
        u32 v;  // vertex index
        u32 vt; // uv index or 0xFFFFFFFF if none
        u32 vn; // normal index or 0xFFFFFFFF if none
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
        bool hasUVs;
        bool hasNormals;

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
    DAPI OBJModel *objModelCreate(const char *fileName);
    DAPI void objModelDestroy(OBJModel *model);
    DAPI IndexedModel *objModelToIndexedModel(OBJModel *objModel);

    // helpers
    DAPI void objModelCreateFace(OBJModel *model, const char *line);
    DAPI OBJIndex objModelParseOBJIndex(const char *token, bool *hasUVs,
                                        bool *hasNormals);
    DAPI Vec2 objModelParseVec2(const char *line);
    DAPI Vec3 objModelParseVec3(const char *line);
    DAPI u32 FindNextChar(u32 start, const char *str, u32 length, char token);
    DAPI u32 parseOBJIndexValue(const char *token, u32 start, u32 end);
    DAPI f32 parseOBJFloatValue(const char *token, u32 start, u32 end);
    DAPI char **SplitString(const char *s, char delim, u32 *count);
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

    DAPI Mat4 getView(const Camera *camera, bool removeTranslation);

    // Display
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

    DAPI void initDisplay(Display *display, f32 width, f32 height);
    DAPI void swapBuffer(const Display *display);
    DAPI void clearDisplay(f32 r, f32 g, f32 b, f32 a);

    DAPI void returnError(const char *errorString);
    DAPI void onDestroy(Display *display);

    // Graphics state
    /*
    typedef struct{
        Display display;
    }GraphicsState;


    //create
    DAPI GraphicsState* createGraphicsState();
    DAPI void cleanUpGraphicsState(GraphicsState* state);
    */

    // Shaders
    DAPI u32 initShader(const char *filename);

    // takes the code of a shader and creates said shader
    DAPI u32 createShader(const char *text, u32 type);
    DAPI u32 createProgram(u32 shader);

    // creates a program with two shaders, a vertex and fragment shader used to
    // render meshes with open gl
    DAPI u32 createGraphicsProgram(const char *vertPath, const char *fragPath);

    // creates a program with three shaders, a vertex , geometry and fragment
    // shader used to render meshes with open gl but with a geometry shader in
    // between to control the primitives
    DAPI u32 createGraphicsProgramWithGeometry(const char *vertPath,
                                               const char *geomPath,
                                               const char *fragPath);
    // craetes a compute shader program
    DAPI u32 createComputeProgram(const char *computePath);
    // error tool
    DAPI void checkShaderError(u32 shader, u32 flag, bool isProgram,
                               const char *errorMessage);
    DAPI void freeShader(u32 shader);

    DAPI void updateShaderMVP(const u32 shader, const Transform transform,
                              const Camera camera);

    // Uniform Buffer Object (UBO) helper API (simple wrapper similar to FBO
    // API)
    DAPI u32 createUBO(u32 size, const void *data,
                       GLenum usage); // returns buffer handle
    DAPI void updateUBO(u32 ubo, u32 offset, u32 size, const void *data);
    DAPI void bindUBOBase(u32 ubo, u32 bindingPoint);
    DAPI void freeUBO(u32 ubo);
    // CoreShaderData UBO helpers (create once, update each frame)
    DAPI u32 createCoreShaderUBO();

    DAPI void updateCoreShaderUBO(f32 timeSeconds, const Vec3 *camPos);

    // Textures
    // 32 textures MAX
    DAPI void bindTexture(u32 texture, unsigned int unit, GLenum type);
    // return the texture handle
    DAPI u32 initTexture(const char *fileName);
    // free texture from memory
    DAPI void freeTexture(u32 texture);

    DAPI u32 createCubeMapTexture(const char **faces, u32 count);
    // Terrain stuff

    typedef struct
    {
        f32 *heights;
        int width;
        int height;
    } HeightMap;

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
    } MaterialUniforms;

    // Mesh
    typedef struct
    {
        u32 albedoTex;
        u32 normalTex;
        u32 metallicTex;
        u32 roughnessTex;
        f32 roughness;
        f32 metallic;
        f32 transparency;
        Vec3 colour;
    } Material;

    typedef struct
    {
        u32 ammount;
        Vec3 *positions;
        Vec2 *texCoords;
        Vec3 *normals;
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
        // vertex array object
        u32 vao;
        // array of buffers
        u32 vab[NUM_BUFFERS];
        // todo: remove this , replace for index
        u32 subMeshCount;
        u32 drawCount; // how much of the vertexArrayObject do we want to draw
    } Mesh;
    DAPI u32 loadMaterialTexture(struct aiMaterial *mat,
                                 enum aiTextureType type);

    DAPI void readMaterial(Material *out, struct aiMaterial *mat);
    DAPI MaterialUniforms getMaterialUniforms(u32 shader);
    DAPI Mesh *loadMeshFromAssimp(const char *filename, u32 *meshCount);
    DAPI Mesh *loadMeshFromAssimpScene(const struct aiScene *scene,
                                       u32 *meshCount);
    DAPI Material *loadMaterialFromAssimp(struct aiScene *scene, u32 *count);

    DAPI void updateMaterial(Material *material,
                             const MaterialUniforms *uniforms);
    // draws a given mesh
    DAPI void drawMesh(Mesh *mesh);
    // creates a mesh from vertices and indices
    DAPI bool createMesh(Mesh *mesh, const Vertices *vertices, u32 numVertices,
                         const u32 *indices, u32 numIndices);
    // loads a mesh from a mesh file

    // DAPI Mesh* loadMesh(const char* filename, u32* outMeshCount);

    DAPI void initModel(Mesh *mesh, const IndexedModel model);
    // free the mesh from memory
    DAPI void freeMesh(Mesh *mesh);

    // creates a plane essentially
    DAPI Mesh *createTerrainMeshWithHeight(u32 cellsX, u32 cellsZ, f32 cellSize,
                                           f32 heightScale,
                                           const char *computeShaderPath,
                                           HeightMap *output);
    DAPI Mesh *createTerrainMesh(unsigned int cellsX, unsigned int cellsZ,
                                 float cellSize);
    DAPI Mesh *createBoxMesh();
    DAPI Mesh *createSkyboxMesh();
    DAPI Mesh *createQuadMesh();

    // Simple framebuffer abstraction (for editor viewport and ID picking)
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

    typedef struct
    {
        char *name;           // the name of the model
        u32 *meshIndices;     // buffer of indices that point to the meshes
        u32 *materialIndices; // materials to use for the mesh
        u32 meshCount;        // how many meshes are in the buffer
        u32 materialCount;    // how many materials are in the buffer
    } Model;
    DAPI void draw(Model *model, u32 shader);

    // resource manager
    typedef struct
    {
        Material *materialBuffer;
        Mesh *meshBuffer;
        Model *modelBuffer;
        u32 *textureHandles;
        HashMap textureIDs;
        HashMap shaderIDs;
        HashMap mesheIDs;
        HashMap modelIDs;
        HashMap materialIDs;
        u32 *shaderHandles;

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
    void readResources(ResourceManager *manager, const char *filename);

    // file utils
    // returns the text of a file
    // TODO: make file.c and remove these functions from resource manager and
    // shader
    DAPI char *loadFileText(const char *fileName);
    DAPI char **listFilesInDirectory(const char *directory, u32 *outCount);
    void listFilesRecursive(const char *directory, char ***fileList, u32 *count,
                            u32 *capacity);
    void normalizePath(char *path);

    DAPI void loadModelFromAssimp(ResourceManager *manager,
                                  const char *filename);

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
        BUTTON_X = SDL_GAMEPAD_BUTTON_LABEL_X,
        BUTTON_Y = SDL_GAMEPAD_BUTTON_LABEL_Y,
        BUTTON_A = SDL_GAMEPAD_BUTTON_LABEL_A,
        BUTTON_B = SDL_GAMEPAD_BUTTON_LABEL_B,
        BUTTON_SQUARE = SDL_GAMEPAD_BUTTON_LABEL_SQUARE,
        BUTTON_CIRCLE = SDL_GAMEPAD_BUTTON_LABEL_CIRCLE,
        BUTTON_TRIANGLE = SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE,
        BUTTON_CROSS = SDL_GAMEPAD_BUTTON_LABEL_CROSS,

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

    extern double FPS;
    typedef struct
    {
        // function pointers
        void (*init)();
        void (*update)(f32);
        void (*render)(f32);
        void (*destroy)();
        void (*inputProcess)(void *);
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

    DAPI Application *createApplication(FncPtr init, FncPtrFloat update,
                                        FncPtrFloat render, FncPtr destroy);
    DAPI void run(Application *app);
    DAPI void destroyApplication(Application *app);

    // methods to make the application run
    DAPI void initSystems(const Application *app);
    DAPI void startApplication(Application *app);

    DAPI void render(Application *app, f32 dt);

    // Input

    void initInput();
    void destroyInput();

    DAPI void processInput(Application *app);

    DAPI bool isKeyDown(KeyCode key);
    DAPI bool isKeyUp(KeyCode key);

    DAPI bool isButtonDown(u32 controllerID, ControllerCode button);
    DAPI bool isButtonUp(u32 controllerID, ControllerCode button);

    DAPI bool isMouseDown(u32 button);

    DAPI void getMouseDelta(f32 *x, f32 *y);

#define GAMEPAD_MAX 4
    extern SDL_Gamepad *gamepads[GAMEPAD_MAX];

    DAPI void checkForGamepadConnection(SDL_Event *event);
    DAPI void checkForGamepadRemoved(SDL_Event *event);

    DAPI Vec2 getKeyboardAxis();
    DAPI Vec2 getJoystickAxis(u32 controllerID, JoystickCode axis1,
                              JoystickCode axis2);

    DAPI extern f32 xInputAxis;
    DAPI extern f32 yInputAxis;

    //=====================================================================================================================

    // Collier
    typedef enum
    {
        Circle,
        Box,
        Cube,
        MeshCollider
    } ColliderType;

    typedef struct
    {
        ColliderType type;
        bool isColliding;
        int layer;
        void *state;
        void (*response)(uint32_t self, uint32_t other);
    } Collider;

    // Functions
    DAPI Collider *createCircleCollider(f32 radius);
    DAPI Collider *createBoxCollider(Vec2 scale);
    DAPI bool cleanCollider(Collider *col);
    DAPI bool isCircleColliding(Vec2 posA, f32 radA, Vec2 posB, f32 radB);
    DAPI bool isBoxColliding(Vec2 posA, Vec2 scaleA, Vec2 posB, Vec2 scaleB);
    DAPI f32 getRadius(Collider *col);
    DAPI Vec2 getScale(Collider *col);
    DAPI bool setBoxScale(Collider *col, Vec2 scale);

    DAPI Collider *createCubeCollider(Vec3 scale);
    DAPI Collider *createMeshCollider(Mesh *mesh, Transform *transform);

    DAPI Collider *createMeshCollider(Mesh *mesh, Transform *transform);

#ifdef __cplusplus
}
#endif
