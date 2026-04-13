#include "../../include/druid.h"

#if PLATFORM_WINDOWS
#define NOGDI
#define NOERROR
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#endif


void listFilesRecursive(const c8 *directory, c8 ***fileList, u32 *count, u32 *capacity);


FileData* loadFile(const c8 *filePath)
{
    // open the file to read
    FILE *file = fopen(filePath, "r");

    // null check
    if (file == NULL)
    {
        ERROR("The File %s has not opened\n", filePath);
        return NULL;
    }

    // determine how big the file is
    fseek(file, 0, SEEK_END);
    u64 length = ftell(file);
    rewind(file);

    // allocate enough memory
    u8 *buffer = (u8 *)malloc(length + 1);

    if (!buffer)
    {
        ERROR("Failed to allocate memory");
        fclose(file);
        return NULL;
    }

    u32 readSize = fread(buffer, 1, length, file);

    buffer[readSize] = '\0';

    fclose(file);

    // allocate a FileData struct and fill it out
    FileData *fileData = (FileData *)malloc(sizeof(FileData));
    assert(fileData != NULL && "Failed to allocate FileData");
    
    strncpy((c8 *)fileData->path, (const c8 *)filePath, MAX_PATH_LENGTH - 1);
    fileData->path[MAX_PATH_LENGTH - 1] = '\0';
    
    fileData->data = buffer;
    fileData->size = readSize;



    return fileData;
}

void freeFileData(FileData* fileData)
{
    if (fileData)
    {
        free(fileData->data);
        free(fileData);
    }
}



b8 writeFile(const c8 *filePath, const u8 *data, u32 size)
{
    FILE *file = fopen(filePath, "w");
    if (file == NULL)
    {
        ERROR("Failed to open file for writing: %s\n", filePath);
        return false;
    }

    u32 written = fwrite(data, 1, size, file);
    if (written != size)
    {
        ERROR("Failed to write all data to file: %s\n", filePath);
        fclose(file);
        return false;
    }

    fclose(file);
    return true;
}

b8 fileExists(const c8 *filePath)
{
    FILE *file = fopen(filePath, "r");
    if (file == NULL)
    {
        return false;
    }
    fclose(file);
    return true;
}

b8 dirExists(const c8 *path)
{
#if PLATFORM_WINDOWS
    DWORD attr = GetFileAttributesA((LPCSTR)path);
    return (attr != INVALID_FILE_ATTRIBUTES) && (attr & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return (stat((const c8 *)path, &st) == 0) && S_ISDIR(st.st_mode);
#endif
}

// Creates a directory and all intermediate parents (mkdir -p).
b8 createDir(const c8 *path)
{
    c8 tmp[MAX_PATH_LENGTH];
    strncpy((c8 *)tmp, (const c8 *)path, MAX_PATH_LENGTH - 1);
    tmp[MAX_PATH_LENGTH - 1] = '\0';
    normalizePath(tmp);

    u32 len = (u32)strlen((c8 *)tmp);
    // strip trailing slash
    if (len > 0 && tmp[len - 1] == '/')
        tmp[--len] = '\0';

    // walk forward and create each intermediate dir
    for (u32 i = 1; i <= len; i++)
    {
        if (tmp[i] == '/' || tmp[i] == '\0')
        {
            c8 saved = tmp[i];
            tmp[i] = '\0';

            if (!dirExists(tmp))
            {
#if PLATFORM_WINDOWS
                if (!CreateDirectoryA((LPCSTR)tmp, NULL))
                {
                    DWORD err = GetLastError();
                    if (err != ERROR_ALREADY_EXISTS)
                    {
                        ERROR("createDir: failed to create '%s' (err %lu)\n", tmp, err);
                        return false;
                    }
                }
#else
                if (mkdir((const c8 *)tmp, 0755) != 0 && errno != EEXIST)
                {
                    ERROR("createDir: failed to create '%s'\n", tmp);
                    return false;
                }
#endif
            }

            tmp[i] = saved;
        }
    }
    return true;
}



c8** listFilesInDirectory(const c8 *directory, u32 *outCount)
{
    u32 capacity = 256;
    c8 **fileList = (c8 **)malloc(sizeof(c8 *) * capacity);
    *outCount = 0;

    listFilesRecursive(directory, &fileList, outCount, &capacity);

    return fileList;
}

void normalizePath(c8 *path)
{
    c8 *src = path;
    c8 *dst = path;

    while (*src)
    {
        // convert backslash to slash
        c8 c = (*src == '\\') ? '/' : *src;

        // collapse multiple '/'
        if (c == '/' && dst > path && dst[-2] == '/')
        {
            src++;
            continue;
        }

        *dst++ = c;
        src++;
    }

    *dst = '\0';
}

c8 *loadFileText(const c8 *fileName)
{
    FileData *fd = loadFile(fileName);
    if (!fd)
        return NULL;
    // Detach the data buffer so the caller owns it; freeFileData would double-free
    c8 *text = (c8 *)fd->data;
    fd->data = NULL;
    freeFileData(fd);
    return text;
}


#if PLATFORM_WINDOWS
void listFilesRecursive(const c8 *directory, c8 ***fileList, u32 *count, u32 *capacity)
{
    WIN32_FIND_DATA findFileData;
    c8 searchPath[MAX_PATH];
    snprintf(searchPath, MAX_PATH, "%s/*", directory);

    HANDLE hFind = FindFirstFile(searchPath, &findFileData);
    if (hFind == INVALID_HANDLE_VALUE)
        return;

    do
    {
        const c8 *name = findFileData.cFileName;

        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
            continue;

        c8 fullPath[MAX_PATH];
        snprintf(fullPath, MAX_PATH, "%s/%s", directory, name);
        normalizePath(fullPath);

        if (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            // recurse into subdir
            listFilesRecursive(fullPath, fileList, count, capacity);
        }
        else
        {
            // store file
            if (*count >= *capacity)
            {
                WARN("File list capacity exceeded");
                break;
            }
            (*fileList)[*count] = _strdup(fullPath);
            if(DEBUG_RESOURCES)
                TRACE("Found file: %s", fullPath);
            (*count)++;
        }
    } while (FindNextFile(hFind, &findFileData));

    FindClose(hFind);
}
#elif PLATFORM_LINUX || PLATFORM_MAC

#endif