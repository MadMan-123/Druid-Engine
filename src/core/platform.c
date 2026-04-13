#include "../../include/druid.h"
#include <stdio.h>
#include <string.h>

#if PLATFORM_WINDOWS
// suppress macro conflicts with our logging macros
#pragma push_macro("ERROR")
#undef ERROR
#include <windows.h>
#pragma pop_macro("ERROR")
#endif

#if PLATFORM_LINUX || PLATFORM_UNIX || PLATFORM_APPLE
#include <dlfcn.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#endif

//=====================================================================================================================
// Shared library load / free / symbol
//=====================================================================================================================

void *platformLibraryLoad(const c8 *path)
{
    if (!path) return NULL;

#if PLATFORM_WINDOWS
    HMODULE mod = LoadLibraryA(path);
    if (!mod)
    {
        ERROR("platformLibraryLoad: LoadLibraryA failed for %s (err %lu)", path, GetLastError());
    }
    return (void *)mod;
#else
    void *mod = dlopen(path, RTLD_NOW);
    if (!mod)
    {
        ERROR("platformLibraryLoad: dlopen failed: %s", dlerror());
    }
    return mod;
#endif
}

void platformLibraryFree(void *handle)
{
    if (!handle) return;

#if PLATFORM_WINDOWS
    FreeLibrary((HMODULE)handle);
#else
    dlclose(handle);
#endif
}

void *platformLibrarySymbol(void *handle, const c8 *name)
{
    if (!handle || !name) return NULL;

#if PLATFORM_WINDOWS
    return (void *)GetProcAddress((HMODULE)handle, name);
#else
    return dlsym(handle, name);
#endif
}

//=====================================================================================================================
// File operations
//=====================================================================================================================

b8 platformFileCopy(const c8 *src, const c8 *dst)
{
    if (!src || !dst) return false;

#if PLATFORM_WINDOWS
    return CopyFileA(src, dst, FALSE) != 0;
#else
    FILE *in = fopen(src, "rb");
    if (!in) return false;
    FILE *out = fopen(dst, "wb");
    if (!out) { fclose(in); return false; }
    c8 buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
        fwrite(buf, 1, n, out);
    fclose(in);
    fclose(out);
    return true;
#endif
}

b8 platformFileDelete(const c8 *path)
{
    if (!path) return false;

#if PLATFORM_WINDOWS
    return DeleteFileA(path) != 0;
#else
    return (remove(path) == 0);
#endif
}

void platformDirCopyRecursive(const c8 *src, const c8 *dst)
{
    if (!src || !dst) return;

#if PLATFORM_WINDOWS
    CreateDirectoryA(dst, NULL);

    c8 pattern[MAX_PATH_LENGTH];
    snprintf(pattern, sizeof(pattern), "%s\\*", src);

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
            continue;

        c8 srcPath[MAX_PATH_LENGTH], dstPath[MAX_PATH_LENGTH];
        snprintf(srcPath, sizeof(srcPath), "%s\\%s", src, fd.cFileName);
        snprintf(dstPath, sizeof(dstPath), "%s\\%s", dst, fd.cFileName);

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            platformDirCopyRecursive(srcPath, dstPath);
        else
            CopyFileA(srcPath, dstPath, FALSE);
    } while (FindNextFileA(h, &fd));

    FindClose(h);
#else
    mkdir(dst, 0755);

    DIR *dir = opendir(src);
    if (!dir) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        c8 srcPath[MAX_PATH_LENGTH], dstPath[MAX_PATH_LENGTH];
        snprintf(srcPath, sizeof(srcPath), "%s/%s", src, entry->d_name);
        snprintf(dstPath, sizeof(dstPath), "%s/%s", dst, entry->d_name);

        struct stat st;
        if (stat(srcPath, &st) == 0 && S_ISDIR(st.st_mode))
            platformDirCopyRecursive(srcPath, dstPath);
        else
            platformFileCopy(srcPath, dstPath);
    }
    closedir(dir);
#endif
}

//=====================================================================================================================
// Process / pipe
//=====================================================================================================================

void *platformPipeOpen(const c8 *command)
{
    if (!command) return NULL;

#if PLATFORM_WINDOWS
    return (void *)_popen(command, "r");
#else
    return (void *)popen(command, "r");
#endif
}

i32 platformPipeClose(void *pipe)
{
    if (!pipe) return -1;

#if PLATFORM_WINDOWS
    return _pclose((FILE *)pipe);
#else
    return pclose((FILE *)pipe);
#endif
}

//=====================================================================================================================
// Executable info
//=====================================================================================================================

void platformGetExePath(c8 *out, u32 size)
{
    if (!out || size == 0) return;
    out[0] = '\0';

#if PLATFORM_WINDOWS
    GetModuleFileNameA(NULL, out, size);
#elif PLATFORM_LINUX
    ssize_t len = readlink("/proc/self/exe", out, size - 1);
    if (len > 0)
        out[len] = '\0';
    else
        out[0] = '\0';
#elif PLATFORM_APPLE
    // _NSGetExecutablePath could be used here
    if (getcwd(out, size) == NULL)
        out[0] = '\0';
#else
    // fallback
    if (getcwd(out, size) == NULL)
        out[0] = '\0';
#endif
}
