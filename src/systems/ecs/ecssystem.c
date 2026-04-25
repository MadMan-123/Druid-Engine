#include "../../../include/druid.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

b8 loadECSSystemDLL(const c8 *dllPath, ECSSystemDLL *out)
{
    if (!dllPath || !out) return false;
    memset(out, 0, sizeof(ECSSystemDLL));

    if (!dllLoad(dllPath, &out->dll))
    {
        ERROR("loadECSSystemDLL: failed to load %s", dllPath);
        return false;
    }

    // Try the new per-archetype naming convention first (druidGetECSSystem_<Name>)
    // Fall back to the legacy single-export name (druidGetECSSystem)
    GetECSSystemFn getSystem = NULL;
    if (out->name[0] != '\0')
    {
        c8 symbolName[256];
        snprintf(symbolName, sizeof(symbolName), "druidGetECSSystem_%s", out->name);
        getSystem = (GetECSSystemFn)dllSymbol(&out->dll, symbolName);
    }
    if (!getSystem)
        getSystem = (GetECSSystemFn)dllSymbol(&out->dll, "druidGetECSSystem");
    if (!getSystem)
    {
        ERROR("loadECSSystemDLL: missing druidGetECSSystem export in %s", dllPath);
        unloadECSSystemDLL(out);
        return false;
    }

    getSystem(&out->plugin);
    out->loaded = true;
    INFO("Loaded ECS system DLL: %s", dllPath);
    return true;
}

void unloadECSSystemDLL(ECSSystemDLL *dll)
{
    if (!dll) return;
    dllUnload(&dll->dll);
    dll->loaded = false;
}

b8 loadECSSystemFromHandle(DLLHandle *dll, const c8 *archetypeName, ECSSystemPlugin *out)
{
    if (!dll || !archetypeName || !out) return false;

    c8 symbolName[256];
    snprintf(symbolName, sizeof(symbolName), "druidGetECSSystem_%s", archetypeName);
    GetECSSystemFn getSystem = (GetECSSystemFn)dllSymbol(dll, symbolName);
    if (!getSystem)
    {
        // try legacy name
        getSystem = (GetECSSystemFn)dllSymbol(dll, "druidGetECSSystem");
    }
    if (!getSystem) return false;

    getSystem(out);
    return true;
}

//=====================================================================================================================
// Archetype code file generation

// Convert "CamelCase" or "camelCase" to "UPPER_SNAKE_CASE".
// e.g. "PositionX" → "POSITION_X", "InvMass" → "INV_MASS", "ModelID" → "MODEL_ID"
static void camelToUpperSnake(const c8 *src, c8 *dst, u32 dstSize)
{
    u32 j = 0;
    for (u32 i = 0; src[i] != '\0' && j + 2 < dstSize; i++)
    {
        c8 c    = src[i];
        c8 prev = (i > 0) ? src[i - 1] : '\0';
        c8 next = src[i + 1];

        if (isupper((unsigned char)c) && i > 0)
        {
            // insert underscore when: lowercase→UPPER or UPPER→Upper (start of new word)
            if (islower((unsigned char)prev) ||
                (isupper((unsigned char)prev) && next != '\0' && islower((unsigned char)next)))
            {
                dst[j++] = '_';
            }
        }
        dst[j++] = (c8)toupper((unsigned char)c);
    }
    dst[j] = '\0';
}

// Physics field tables — shared by buildGenBlock and the physics-field-skip logic.
static const c8 *s_physFieldNames[] = {
    "LinearVelocityX", "LinearVelocityY", "LinearVelocityZ",
    "ForceX", "ForceY", "ForceZ",
    "PhysicsBodyType",
    "Mass", "InvMass", "Restitution", "LinearDamping",
    "SphereRadius",
    "ColliderHalfX", "ColliderHalfY", "ColliderHalfZ",
    "ColliderOffsetX", "ColliderOffsetY", "ColliderOffsetZ"
};
static const c8 *s_physFieldTypes[] = {
    "f32", "f32", "f32",
    "f32", "f32", "f32",
    "u32",
    "f32", "f32", "f32", "f32",
    "f32",
    "f32", "f32", "f32",
    "f32", "f32", "f32"
};
static const u32 s_physFieldTableCount = 18;

static c8 *readFileToString(const c8 *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 8 * 1024 * 1024) { fclose(f); return NULL; }
    c8 *out = (c8 *)malloc((u32)sz + 1);
    if (!out) { fclose(f); return NULL; }
    u32 nread = (u32)fread(out, 1, (u32)sz, f);
    out[nread] = '\0';
    fclose(f);
    return out;
}

// Find the existing enum name for a field by its displayName inside an existing
// FIELDS macro block. Returns true and fills enumNameOut if found.
static b8 findExistingEnumName(const c8 *existingBlock, const c8 *displayName,
                                c8 *enumNameOut, u32 enumNameSize)
{
    if (!existingBlock || !displayName) return false;
    const c8 *p = existingBlock;
    while ((p = strstr(p, "FIELD(")) != NULL)
    {
        p += 6;
        c8 eName[256] = {0};
        u32 ei = 0;
        while (*p && *p != ',' && *p != ')' && !isspace((unsigned char)*p) && ei + 1 < sizeof(eName))
            eName[ei++] = *p++;
        while (*p && *p != '"' && *p != ')' && *p != '\n') p++;
        if (*p != '"') continue;
        p++;
        c8 dName[256] = {0};
        u32 di = 0;
        while (*p && *p != '"' && di + 1 < sizeof(dName))
            dName[di++] = *p++;
        if (eName[0] != '\0' && strcmp(dName, displayName) == 0)
        {
            strncpy(enumNameOut, eName, enumNameSize - 1);
            enumNameOut[enumNameSize - 1] = '\0';
            return true;
        }
    }
    return false;
}

// Builds and returns (malloc'd) the sentinel-wrapped generated block for insertion
// into the .h file.  Includes DRUID_FLAGS, POOL_CAPACITY, the FIELDS macro, and
// DECLARE_ARCHETYPE — everything the scanner needs to reconstruct the archetype.
// existingBlock: the current content between the sentinel markers (or NULL for fresh).
//   Used to preserve user-defined enum names for existing fields.
static c8 *buildGenBlock(const c8 *archetypeName, const c8 *upperName,
                          const FieldInfo *fields, const c8 **typeNames, u32 fieldCount,
                          b8 isSingle, b8 isBuffered, u32 poolCapacity,
                          b8 isPhysicsBody, b8 isPersistent, b8 uniformScale,
                          const b8 *physFieldSkip, u32 physFieldCount,
                          const c8 *existingBlock)
{
    u32 cap = 8192, len = 0;
    c8 *blk = (c8 *)malloc(cap);
    if (!blk) return NULL;
    blk[0] = '\0';

#define BA(fmt, ...) do { \
    c8 _t[512]; int _n = snprintf(_t, sizeof(_t), fmt, ##__VA_ARGS__); \
    if (_n > 0) { u32 _nl = (u32)_n; \
        if (len + _nl + 1 > cap) { cap = (len + _nl + 1) * 2; blk = (c8*)realloc(blk, cap); } \
        memcpy(blk + len, _t, _nl); len += _nl; blk[len] = '\0'; } \
} while(0)

    BA("// <DRUID_GEN_BEGIN %s>\n", archetypeName);

    u8 genFlags = 0;
    if (isSingle)      FLAG_SET(genFlags, ARCH_SINGLE);
    if (isPersistent)  FLAG_SET(genFlags, ARCH_PERSISTENT);
    if (isPhysicsBody) FLAG_SET(genFlags, ARCH_PHYSICS_BODY);
    if (isBuffered)    FLAG_SET(genFlags, ARCH_BUFFERED);
    if (uniformScale)  FLAG_SET(genFlags, ARCH_FILE_UNIFORM_SCALE);
    BA("// DRUID_FLAGS 0x%02X\n", genFlags);
    if (isBuffered)    BA("// isBuffered\n");
    if (isSingle)      BA("// isSingle\n");
    if (isPhysicsBody) BA("// isPhysicsBody\n");
    if (isBuffered && poolCapacity > 0) BA("#define POOL_CAPACITY %u\n", poolCapacity);
    BA("\n");

    u32 totalFields = (isBuffered ? 1u : 0u) + fieldCount + physFieldCount;
    u32 entryIdx    = 0;
    BA("#define %s_FIELDS(FIELD) \\\n", upperName);

    if (isBuffered)
    {
        entryIdx++;
        BA("    FIELD(%s_ALIVE, \"Alive\", b8, COLD)%s\n", upperName,
           (entryIdx < totalFields) ? " \\" : "");
    }
    for (u32 i = 0; i < fieldCount; i++)
    {
        entryIdx++;
        c8 snake[256];
        camelToUpperSnake(fields[i].name, snake, sizeof(snake));
        c8 enumName[256];
        snprintf(enumName, sizeof(enumName), "%s_%s", upperName, snake);
        BA("    FIELD(%s, \"%s\", %s, %s)%s\n",
           enumName, fields[i].name, typeNames[i],
           (fields[i].temperature == FIELD_TEMP_HOT) ? "HOT" : "COLD",
           (entryIdx < totalFields) ? " \\" : "");
    }
    for (u32 i = 0; i < s_physFieldTableCount; i++)
    {
        if (!isPhysicsBody || physFieldSkip[i]) continue;
        entryIdx++;
        c8 snake[256];
        camelToUpperSnake(s_physFieldNames[i], snake, sizeof(snake));
        c8 enumName[256];
        snprintf(enumName, sizeof(enumName), "%s_%s", upperName, snake);
        BA("    FIELD(%s, \"%s\", %s, HOT)%s\n",
           enumName, s_physFieldNames[i], s_physFieldTypes[i],
           (entryIdx < totalFields) ? " \\" : "");
    }

    BA("\nDECLARE_ARCHETYPE(%s, %s_FIELDS)\n", archetypeName, upperName);
    BA("// <DRUID_GEN_END %s>\n", archetypeName);

#undef BA
    return blk;
}

b8 generateArchetypeFiles(const c8 *projectDir,
                           const c8 *archetypeName,
                           const FieldInfo *fields,
                           const c8 **typeNames,
                           u32 fieldCount,
                           b8 isSingle,
                           b8 isBuffered,
                           u32 poolCapacity,
                           b8 isPhysicsBody,
                           b8 isPersistent,
                           b8 uniformScale,
                           b8 useCpp)
{
    if (!projectDir || !archetypeName || !fields || !typeNames || fieldCount == 0)
    {
        ERROR("generateArchetypeFiles: invalid parameters");
        return false;
    }

    c8 camelCaseName[256];
    snprintf(camelCaseName, sizeof(camelCaseName), "%c%s",
             tolower((unsigned char)archetypeName[0]), archetypeName + 1);

    c8 upperName[256];
    for (u32 i = 0; archetypeName[i] != '\0' && i + 1 < sizeof(upperName); i++)
        upperName[i] = (c8)toupper((unsigned char)archetypeName[i]);
    upperName[strlen(archetypeName)] = '\0';

    b8  physFieldSkip[32] = {0};  // must be >= s_physFieldTableCount
    u32 physFieldCount    = 0;
    if (isPhysicsBody)
    {
        for (u32 p = 0; p < s_physFieldTableCount; p++)
        {
            b8 dup = false;
            for (u32 u = 0; u < fieldCount; u++)
                if (strcmp(fields[u].name, s_physFieldNames[p]) == 0) { dup = true; break; }
            physFieldSkip[p] = dup;
            if (!dup) physFieldCount++;
        }
    }

    //=========================================================================
    // .h file — splice into existing file between sentinels, or create fresh.

    c8 headerPath[MAX_PATH_LENGTH];
    snprintf(headerPath, sizeof(headerPath), "%s/src/%s.h", projectDir, archetypeName);

    c8 *existingHeader = readFileToString(headerPath);

    // Extract the existing sentinel block so buildGenBlock can preserve enum names.
    const c8 *existingBlock = NULL;
    const c8 *beginPtr = NULL, *endPtr = NULL;
    c8 beginTag[256], endTag[256];
    if (existingHeader)
    {
        snprintf(beginTag, sizeof(beginTag), "// <DRUID_GEN_BEGIN %s>", archetypeName);
        snprintf(endTag,   sizeof(endTag),   "// <DRUID_GEN_END %s>",   archetypeName);
        beginPtr = strstr(existingHeader, beginTag);
        endPtr   = strstr(existingHeader, endTag);
        if (beginPtr && endPtr && endPtr > beginPtr)
            existingBlock = beginPtr;
    }

    c8 *genBlock = buildGenBlock(archetypeName, upperName, fields, typeNames, fieldCount,
                                  isSingle, isBuffered, poolCapacity,
                                  isPhysicsBody, isPersistent, uniformScale,
                                  physFieldSkip, physFieldCount, existingBlock);
    if (!genBlock) { free(existingHeader); return false; }

    if (existingHeader)
    {
        if (beginPtr && endPtr && endPtr > beginPtr)
        {
            // Advance past the end-tag line
            const c8 *afterEnd = endPtr + strlen(endTag);
            if (*afterEnd == '\r') afterEnd++;
            if (*afterEnd == '\n') afterEnd++;

            FILE *hf = fopen(headerPath, "w");
            if (!hf)
            {
                ERROR("generateArchetypeFiles: cannot open %s for writing", headerPath);
                free(existingHeader); free(genBlock); return false;
            }
            fwrite(existingHeader, 1, (u32)(beginPtr - existingHeader), hf);
            fputs(genBlock, hf);
            fputs(afterEnd, hf);
            fclose(hf);
        }
        else
        {
            WARN("generateArchetypeFiles: %s has no DRUID_GEN markers. "
                 "Wrap your FIELDS macro and DECLARE_ARCHETYPE with "
                 "// <DRUID_GEN_BEGIN %s> ... // <DRUID_GEN_END %s> "
                 "to enable field regeneration. Header not modified.",
                 headerPath, archetypeName, archetypeName);
        }
        free(existingHeader);
    }
    else
    {
        // First-time creation: write full skeleton with sentinel block embedded.
        FILE *hf = fopen(headerPath, "w");
        if (!hf)
        {
            ERROR("generateArchetypeFiles: cannot create %s", headerPath);
            free(genBlock); return false;
        }
        fprintf(hf, "#pragma once\n");
        fprintf(hf, "#include <druid.h>\n\n");
        fprintf(hf, "#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n");
        fputs(genBlock, hf);
        fprintf(hf, "\nDSAPI void %sInit(void);\n", camelCaseName);
        fprintf(hf, "DSAPI void %sUpdate(Archetype *arch, f32 dt);\n", camelCaseName);
        fprintf(hf, "DSAPI void %sRender(Archetype *arch, Renderer *r);\n", camelCaseName);
        fprintf(hf, "DSAPI void %sDestroy(void);\n\n", camelCaseName);
        fprintf(hf, "DSAPI void druidGetECSSystem_%s(ECSSystemPlugin *out);\n\n", archetypeName);
        fprintf(hf, "#ifdef __cplusplus\n}\n#endif\n");
        fclose(hf);
    }

    free(genBlock);

    //=========================================================================
    // .c / .cpp file — only write if it does not already exist.

    c8 sourcePath[MAX_PATH_LENGTH];
    snprintf(sourcePath, sizeof(sourcePath), "%s/src/%s.%s",
             projectDir, archetypeName, useCpp ? "cpp" : "c");

    FILE *sfCheck = fopen(sourcePath, "r");
    if (sfCheck)
    {
        fclose(sfCheck);
        INFO("generateArchetypeFiles: %s exists — source not overwritten", sourcePath);
        INFO("Generated archetype header: %s.h in %s/src/", archetypeName, projectDir);
        return true;
    }

    FILE *sf = fopen(sourcePath, "w");
    if (!sf) { ERROR("generateArchetypeFiles: cannot create %s", sourcePath); return false; }

    fprintf(sf, "#define DRUID_SYSTEM_EXPORT\n");
    fprintf(sf, "#include \"%s.h\"\n\n", archetypeName);
    fprintf(sf, "DEFINE_ARCHETYPE(%s, %s_FIELDS)\n\n", archetypeName, upperName);
    fprintf(sf, "static u32 s_ibSlot = (u32)-1;\n\n");

    fprintf(sf, "void %sInit(void)\n{\n", camelCaseName);
    if (isBuffered && poolCapacity > 0)
        fprintf(sf, "    s_ibSlot = rendererAcquireInstanceBuffer(renderer, POOL_CAPACITY);\n");
    else
        fprintf(sf, "    s_ibSlot = rendererAcquireInstanceBuffer(renderer, 256);\n");
    fprintf(sf, "}\n\n");

    fprintf(sf, "void %sUpdate(Archetype *arch, f32 dt)\n{\n", camelCaseName);
    if (!isSingle)
    {
        fprintf(sf, "    for (u32 _ch = 0; _ch < arch->activeChunkCount; _ch++)\n");
        fprintf(sf, "    {\n");
        fprintf(sf, "        void **fields = getArchetypeFields(arch, _ch);\n");
        fprintf(sf, "        if (!fields) continue;\n");
        fprintf(sf, "        u32 count = arch->arena[_ch].count;\n");
        if (isBuffered) fprintf(sf, "        b8 *alive = (b8 *)fields[%s_ALIVE];\n", upperName);
        fprintf(sf, "        for (u32 i = 0; i < count; i++)\n        {\n");
        if (isBuffered) fprintf(sf, "            if (!alive[i]) continue;\n");
        fprintf(sf, "        }\n");
        fprintf(sf, "    }\n");
    }
    else
    {
        fprintf(sf, "    void **fields = getArchetypeFields(arch, 0);\n");
        fprintf(sf, "    if (!fields || arch->arena[0].count == 0) return;\n");
        fprintf(sf, "    (void)dt;\n");
    }
    fprintf(sf, "}\n\n");

    fprintf(sf, "// Optional custom render — set out->render = %sRender to use.\n", camelCaseName);
    fprintf(sf, "/*\nvoid %sRender(Archetype *arch, Renderer *r) { (void)arch; (void)r; }\n*/\n\n");

    fprintf(sf, "void %sDestroy(void)\n{\n", camelCaseName);
    fprintf(sf, "    if (s_ibSlot != (u32)-1) { rendererReleaseInstanceBuffer(renderer, s_ibSlot); s_ibSlot = (u32)-1; }\n");
    fprintf(sf, "}\n\n");

    fprintf(sf, "void druidGetECSSystem_%s(ECSSystemPlugin *out)\n{\n", archetypeName);
    fprintf(sf, "    out->init    = %sInit;\n", camelCaseName);
    fprintf(sf, "    out->update  = %sUpdate;\n", camelCaseName);
    fprintf(sf, "    out->render  = NULL;\n");
    fprintf(sf, "    out->destroy = %sDestroy;\n", camelCaseName);
    fprintf(sf, "}\n");

    fclose(sf);

    INFO("Generated archetype files: %s.h / %s.%s in %s/src/",
         archetypeName, archetypeName, useCpp ? "cpp" : "c", projectDir);
    return true;
}

//=====================================================================================================================
// Build all archetype system DLLs

b8 buildArchetypeSystems(const c8 *projectDir, c8 *outLog, u32 logSize)
{
    if (!projectDir || !outLog) return false;
    outLog[0] = '\0';

    // the main project CMakeLists already globs src/*.c — the archetype .c
    // files are compiled into the game DLL automatically. This function
    // triggers a rebuild so the latest system code is picked up.

    c8 buildDir[MAX_PATH_LENGTH];
    snprintf(buildDir, sizeof(buildDir), "%s/build", projectDir);
    createDir(buildDir);

    c8 cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "cd /d \"%s/build\" && cmake --build . 2>&1", projectDir);

    FILE *pipe = (FILE *)platformPipeOpen(cmd);
    if (!pipe)
    {
        snprintf(outLog, logSize, "Failed to run cmake build for archetype systems\n");
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

    return (ret == 0);
}
