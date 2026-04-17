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

b8 generateArchetypeFiles(const c8 *projectDir,
                           const c8 *archetypeName,
                           const FieldInfo *fields,
                           const c8 **typeNames,
                           u32 fieldCount,
                           b8 isSingle,
                           b8 isBuffered,
                           u32 poolCapacity,
                           b8 isPhysicsBody,
                           b8 useCpp)
{
    if (!projectDir || !archetypeName || !fields || !typeNames || fieldCount == 0)
    {
        ERROR("generateArchetypeFiles: invalid parameters");
        return false;
    }

    // "Bullet" → "bullet"
    c8 camelCaseName[256];
    snprintf(camelCaseName, sizeof(camelCaseName), "%c%s",
             tolower((unsigned char)archetypeName[0]), archetypeName + 1);

    // "Bullet" → "BULLET"  (used as enum prefix)
    c8 upperName[256];
    for (u32 i = 0; archetypeName[i] != '\0' && i + 1 < sizeof(upperName); i++)
        upperName[i] = (c8)toupper((unsigned char)archetypeName[i]);
    upperName[strlen(archetypeName)] = '\0';

    // Physics fields auto-injected for physics body archetypes
    static const c8 *physFieldNames[] = {
        "LinearVelocityX", "LinearVelocityY", "LinearVelocityZ",
        "ForceX", "ForceY", "ForceZ",
        "PhysicsBodyType",
        "Mass", "InvMass", "Restitution", "LinearDamping",
        "SphereRadius",
        "ColliderHalfX", "ColliderHalfY", "ColliderHalfZ"
    };
    static const c8 *physFieldTypes[] = {
        "f32", "f32", "f32",
        "f32", "f32", "f32",
        "u32",
        "f32", "f32", "f32", "f32",
        "f32",
        "f32", "f32", "f32"
    };
    b8 physFieldSkip[15] = {0};
    u32 physFieldCount = 0;
    if (isPhysicsBody)
    {
        for (u32 p = 0; p < 15; p++)
        {
            b8 dup = false;
            for (u32 u = 0; u < fieldCount; u++)
            {
                if (strcmp(fields[u].name, physFieldNames[p]) == 0) { dup = true; break; }
            }
            physFieldSkip[p] = dup;
            if (!dup) physFieldCount++;
        }
    }

    //=========================================================================
    // Write .h file

    c8 headerPath[MAX_PATH_LENGTH];
    snprintf(headerPath, sizeof(headerPath), "%s/src/%s.h", projectDir, archetypeName);
    FILE *hf = fopen(headerPath, "w");
    if (!hf) { ERROR("generateArchetypeFiles: cannot open %s", headerPath); return false; }

    fprintf(hf, "#pragma once\n");
    fprintf(hf, "// Auto-generated by the Druid editor — safe to edit below the stubs.\n");
    fprintf(hf, "#include <druid.h>\n\n");
    fprintf(hf, "#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n");

    // FIELDS macro — single source of truth for enum and FieldInfo array
    // Total entries: [Alive if buffered] + user fields + auto physics fields
    u32 totalFields = (isBuffered ? 1u : 0u) + fieldCount + physFieldCount;
    u32 entryIdx = 0;

    fprintf(hf, "#define %s_FIELDS(FIELD) \\\n", upperName);

    if (isBuffered)
    {
        entryIdx++;
        const c8 *cont = (entryIdx < totalFields) ? " \\" : "";
        fprintf(hf, "    FIELD(%s_ALIVE, \"Alive\", b8, COLD)%s\n", upperName, cont);
    }

    for (u32 i = 0; i < fieldCount; i++)
    {
        entryIdx++;
        const c8 *tempStr = (fields[i].temperature == FIELD_TEMP_HOT) ? "HOT" : "COLD";
        c8 snake[256];
        camelToUpperSnake(fields[i].name, snake, sizeof(snake));
        const c8 *cont = (entryIdx < totalFields) ? " \\" : "";
        fprintf(hf, "    FIELD(%s_%s, \"%s\", %s, %s)%s\n",
                upperName, snake, fields[i].name, typeNames[i], tempStr, cont);
    }

    for (u32 i = 0; i < 15; i++)
    {
        if (!isPhysicsBody || physFieldSkip[i]) continue;
        entryIdx++;
        c8 snake[256];
        camelToUpperSnake(physFieldNames[i], snake, sizeof(snake));
        const c8 *cont = (entryIdx < totalFields) ? " \\" : "";
        fprintf(hf, "    FIELD(%s_%s, \"%s\", %s, HOT)%s\n",
                upperName, snake, physFieldNames[i], physFieldTypes[i], cont);
    }

    fprintf(hf, "\nDECLARE_ARCHETYPE(%s, %s_FIELDS)\n\n", archetypeName, upperName);

    fprintf(hf, "DSAPI void %sInit(void);\n", camelCaseName);
    fprintf(hf, "DSAPI void %sUpdate(Archetype *arch, f32 dt);\n", camelCaseName);
    fprintf(hf, "DSAPI void %sRender(Archetype *arch, Renderer *r);\n", camelCaseName);
    fprintf(hf, "DSAPI void %sDestroy(void);\n\n", camelCaseName);
    fprintf(hf, "DSAPI void druidGetECSSystem_%s(ECSSystemPlugin *out);\n\n", archetypeName);
    fprintf(hf, "#ifdef __cplusplus\n}\n#endif\n");
    fclose(hf);

    //=========================================================================
    // Write .c / .cpp file

    c8 sourcePath[MAX_PATH_LENGTH];
    snprintf(sourcePath, sizeof(sourcePath), "%s/src/%s.%s",
             projectDir, archetypeName, useCpp ? "cpp" : "c");
    FILE *sf = fopen(sourcePath, "w");
    if (!sf) { ERROR("generateArchetypeFiles: cannot open %s", sourcePath); return false; }

    // DRUID_SYSTEM_EXPORT belongs in the .c — controls dllexport/dllimport
    fprintf(sf, "#define DRUID_SYSTEM_EXPORT\n");
    fprintf(sf, "#include \"%s.h\"\n\n", archetypeName);

    if (isBuffered)
        fprintf(sf, "#define POOL_CAPACITY %u\n\n", poolCapacity);

    // Canonical flags marker for the scanner
    u8 generatedFlags = 0;
    if (isSingle)     FLAG_SET(generatedFlags, ARCH_SINGLE);
    if (isBuffered)   FLAG_SET(generatedFlags, ARCH_BUFFERED);
    if (isPhysicsBody) FLAG_SET(generatedFlags, ARCH_PHYSICS_BODY);
    fprintf(sf, "// DRUID_FLAGS 0x%02X\n", generatedFlags);
    if (isBuffered)   fprintf(sf, "// isBuffered\n");
    if (isSingle)     fprintf(sf, "// isSingle\n");
    if (isPhysicsBody) fprintf(sf, "// isPhysicsBody\n");
    fprintf(sf, "\n");

    fprintf(sf, "DEFINE_ARCHETYPE(%s, %s_FIELDS)\n\n", archetypeName, upperName);

    fprintf(sf, "static u32 s_ibSlot = (u32)-1;\n\n");

    fprintf(sf, "void %sInit(void)\n{\n", camelCaseName);
    fprintf(sf, "    s_ibSlot = rendererAcquireInstanceBuffer(renderer, 1024);\n");
    fprintf(sf, "}\n\n");

    // Build a helper lambda for emitting a typed field pointer using an enum name.
    // Emits: "    TYPE *varName = (TYPE *)fields[ENUM_NAME];\n"
    // The variable name is lowercased field name (plain camelCase as given).
#define EMIT_FIELD_PTR(indent, TYPE, varName, ENUM) \
    fprintf(sf, indent TYPE " *" varName " = (" TYPE " *)fields[" ENUM "];\n")

    fprintf(sf, "void %sUpdate(Archetype *arch, f32 dt)\n{\n", camelCaseName);
    if (isPhysicsBody)
        fprintf(sf, "    (void)dt;\n");

    if (isSingle)
    {
        fprintf(sf, "    void **fields = getArchetypeFields(arch, 0);\n");
        fprintf(sf, "    if (!fields || arch->arena[0].count == 0) return;\n\n");

        if (isBuffered)
        {
            c8 enumName[512];
            snprintf(enumName, sizeof(enumName), "%s_ALIVE", upperName);
            fprintf(sf, "    b8 *alive = (b8 *)fields[%s];\n", enumName);
        }
        for (u32 i = 0; i < fieldCount; i++)
        {
            c8 snake[256], enumName[512];
            camelToUpperSnake(fields[i].name, snake, sizeof(snake));
            snprintf(enumName, sizeof(enumName), "%s_%s", upperName, snake);
            fprintf(sf, "    %s *%s = (%s *)fields[%s];\n",
                    typeNames[i], fields[i].name, typeNames[i], enumName);
        }
        if (isPhysicsBody)
        {
            fprintf(sf, "    // physics SoA fields (auto-injected)\n");
            for (u32 i = 0; i < 15; i++)
            {
                if (physFieldSkip[i]) continue;
                c8 snake[256], enumName[512];
                camelToUpperSnake(physFieldNames[i], snake, sizeof(snake));
                snprintf(enumName, sizeof(enumName), "%s_%s", upperName, snake);
                fprintf(sf, "    %s *%s = (%s *)fields[%s];\n",
                        physFieldTypes[i], physFieldNames[i], physFieldTypes[i], enumName);
            }
        }
    }
    else
    {
        fprintf(sf, "    for (u32 _ch = 0; _ch < arch->activeChunkCount; _ch++)\n");
        fprintf(sf, "    {\n");
        fprintf(sf, "        void **fields = getArchetypeFields(arch, _ch);\n");
        fprintf(sf, "        if (!fields) continue;\n");
        fprintf(sf, "        u32 count = arch->arena[_ch].count;\n\n");

        if (isBuffered)
        {
            c8 enumName[512];
            snprintf(enumName, sizeof(enumName), "%s_ALIVE", upperName);
            fprintf(sf, "        b8 *alive = (b8 *)fields[%s];\n", enumName);
        }
        for (u32 i = 0; i < fieldCount; i++)
        {
            c8 snake[256], enumName[512];
            camelToUpperSnake(fields[i].name, snake, sizeof(snake));
            snprintf(enumName, sizeof(enumName), "%s_%s", upperName, snake);
            fprintf(sf, "        %s *%s = (%s *)fields[%s];\n",
                    typeNames[i], fields[i].name, typeNames[i], enumName);
        }
        if (isPhysicsBody)
        {
            fprintf(sf, "        // physics SoA fields (auto-injected)\n");
            for (u32 i = 0; i < 15; i++)
            {
                if (physFieldSkip[i]) continue;
                c8 snake[256], enumName[512];
                camelToUpperSnake(physFieldNames[i], snake, sizeof(snake));
                snprintf(enumName, sizeof(enumName), "%s_%s", upperName, snake);
                fprintf(sf, "        %s *%s = (%s *)fields[%s];\n",
                        physFieldTypes[i], physFieldNames[i], physFieldTypes[i], enumName);
            }
        }
        fprintf(sf, "\n");
        fprintf(sf, "        for (u32 i = 0; i < count; i++)\n");
        fprintf(sf, "        {\n");
        if (isBuffered)
            fprintf(sf, "            if (!alive[i]) continue;\n");
        fprintf(sf, "        }\n");
        fprintf(sf, "    }\n");
    }
    fprintf(sf, "}\n\n");
#undef EMIT_FIELD_PTR

    fprintf(sf, "// Optional: implement custom rendering for this archetype.\n");
    fprintf(sf, "// When render is NULL (see druidGetECSSystem below), the engine uses a\n");
    fprintf(sf, "// default forward pass based on Position/Rotation/Scale/ModelID fields.\n");
    fprintf(sf, "// Uncomment and set out->render = %sRender to take full control.\n\n", camelCaseName);

    fprintf(sf, "/*\nvoid %sRender(Archetype *arch, Renderer *r)\n{\n", camelCaseName);
    fprintf(sf, "    (void)r;\n");
    fprintf(sf, "    for (u32 _ch = 0; _ch < arch->activeChunkCount; _ch++)\n");
    fprintf(sf, "    {\n");
    fprintf(sf, "        void **fields = getArchetypeFields(arch, _ch);\n");
    fprintf(sf, "        if (!fields) continue;\n");
    fprintf(sf, "        u32 count = arch->arena[_ch].count;\n\n");
    if (isBuffered)
    {
        c8 enumName[512];
        snprintf(enumName, sizeof(enumName), "%s_ALIVE", upperName);
        fprintf(sf, "        b8 *alive = (b8 *)fields[%s];\n", enumName);
    }
    for (u32 i = 0; i < fieldCount; i++)
    {
        c8 snake[256], enumName[512];
        camelToUpperSnake(fields[i].name, snake, sizeof(snake));
        snprintf(enumName, sizeof(enumName), "%s_%s", upperName, snake);
        fprintf(sf, "        %s *%s = (%s *)fields[%s];\n",
                typeNames[i], fields[i].name, typeNames[i], enumName);
    }
    fprintf(sf, "\n        for (u32 i = 0; i < count; i++)\n");
    fprintf(sf, "        {\n        }\n");
    fprintf(sf, "    }\n");
    fprintf(sf, "}\n*/\n\n");

    fprintf(sf, "void %sDestroy(void)\n{\n", camelCaseName);
    fprintf(sf, "    if (s_ibSlot != (u32)-1) { rendererReleaseInstanceBuffer(renderer, s_ibSlot); s_ibSlot = (u32)-1; }\n");
    fprintf(sf, "}\n\n");

    fprintf(sf, "void druidGetECSSystem_%s(ECSSystemPlugin *out)\n{\n", archetypeName);
    fprintf(sf, "    out->init    = %sInit;\n", camelCaseName);
    fprintf(sf, "    out->update  = %sUpdate;\n", camelCaseName);
    fprintf(sf, "    out->render  = NULL;  // default forward pass; set to %sRender for custom\n", camelCaseName);
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
