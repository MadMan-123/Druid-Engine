#include "../../include/druid.h"
#include <string.h>

//=====================================================================================================================
// DLL Loader — unified API
//
// Wraps platformLibrary* functions with the temp-copy trick so the original
// DLL file isn't locked by the OS during rebuilds. Used by both the game
// plugin system and the ECS system DLLs.
//=====================================================================================================================

b8 dllLoad(const c8 *dllPath, DLLHandle *out)
{
    if (!dllPath || !out) return false;
    memset(out, 0, sizeof(DLLHandle));

    // copy the DLL to a temp path so the original isn't locked
    c8 tempPath[MAX_PATH_LENGTH];
    snprintf(tempPath, sizeof(tempPath), "%s.running.dll", dllPath);

    platformFileDelete(tempPath);

    if (!platformFileCopy(dllPath, tempPath))
    {
        ERROR("dllLoad: failed to copy %s to temp path", dllPath);
        return false;
    }

    void *handle = platformLibraryLoad(tempPath);
    if (!handle)
    {
        ERROR("dllLoad: failed to load library %s", tempPath);
        return false;
    }

    out->handle = handle;
    strncpy(out->loadedPath, tempPath, MAX_PATH_LENGTH - 1);
    out->loadedPath[MAX_PATH_LENGTH - 1] = '\0';
    out->loaded = true;

    return true;
}

void *dllSymbol(DLLHandle *handle, const c8 *name)
{
    if (!handle || !handle->handle || !name) return NULL;
    return platformLibrarySymbol(handle->handle, name);
}

void dllUnload(DLLHandle *handle)
{
    if (!handle) return;

    if (handle->handle)
    {
        platformLibraryFree(handle->handle);
        handle->handle = NULL;
    }

    // clean up the temp copy
    if (handle->loadedPath[0] != '\0')
    {
        platformFileDelete(handle->loadedPath);
        handle->loadedPath[0] = '\0';
    }

    handle->loaded = false;
}
