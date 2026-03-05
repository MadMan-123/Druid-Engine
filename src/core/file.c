//generate the functions for file handling but dont actually implement them here, just the declarations.
#include "druid.h"

#if PLATFORM_WINDOWS
#define NOGDI
#define NOERROR
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#endif


void listFilesRecursive(const u8 *directory, u8 ***fileList, u32 *count, u32 *capacity);


FileData* loadFile(const u8 *filePath)
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
    // go back to start
    rewind(file);

    // allocate enough memory
    u8 *buffer = (u8 *)malloc(length + 1);

    if (!buffer)
    {
        ERROR("Failed to allocate memory");
        fclose(file);
        return NULL;
    }

    // read the file to the buffer
    u32 readSize = fread(buffer, 1, length, file);

    // add null terminator
    buffer[readSize] = '\0';

    fclose(file);

    // allocate a FileData struct and fill it out
    FileData *fileData = (FileData *)malloc(sizeof(FileData));
    assert(fileData != NULL && "Failed to allocate FileData");
    
    // copy the path, data and size to the struct
    strncpy((char *)fileData->path, (const char *)filePath, MAX_PATH_LENGTH - 1);
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



b8 writeFile(const u8 *filePath, const u8 *data, u32 size)
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

b8 fileExists(const u8 *filePath)
{
    FILE *file = fopen(filePath, "r");
    if (file == NULL)
    {
        return false;
    }
    fclose(file);
    return true;
}

b8 dirExists(const u8 *path)
{
#if PLATFORM_WINDOWS
    DWORD attr = GetFileAttributesA((LPCSTR)path);
    return (attr != INVALID_FILE_ATTRIBUTES) && (attr & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return (stat((const char *)path, &st) == 0) && S_ISDIR(st.st_mode);
#endif
}

// Creates a directory and all intermediate parents (mkdir -p).
b8 createDir(const u8 *path)
{
    u8 tmp[MAX_PATH_LENGTH];
    strncpy((char *)tmp, (const char *)path, MAX_PATH_LENGTH - 1);
    tmp[MAX_PATH_LENGTH - 1] = '\0';
    normalizePath(tmp);

    u32 len = (u32)strlen((char *)tmp);
    // strip trailing slash
    if (len > 0 && tmp[len - 1] == '/')
        tmp[--len] = '\0';

    // walk forward and create each intermediate dir
    for (u32 i = 1; i <= len; i++)
    {
        if (tmp[i] == '/' || tmp[i] == '\0')
        {
            u8 saved = tmp[i];
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
                if (mkdir((const char *)tmp, 0755) != 0 && errno != EEXIST)
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



u8** listFilesInDirectory(const u8 *directory, u32 *outCount)
{
    u32 capacity = 256;
    u8 **fileList = (u8 **)malloc(sizeof(u8 *) * capacity);
    *outCount = 0;

    listFilesRecursive(directory, &fileList, outCount, &capacity);

    return fileList;
}

void normalizePath(u8 *path)
{
    u8 *src = path;
    u8 *dst = path;

    while (*src)
    {
        // convert backslash to slash
        u8 c = (*src == '\\') ? '/' : *src;

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

u8 *loadFileText(const u8 *fileName)
{
    FileData *fd = loadFile(fileName);
    if (!fd)
        return NULL;
    // Detach the data buffer so the caller owns it; freeFileData would double-free
    u8 *text = fd->data;
    fd->data = NULL;
    freeFileData(fd);
    return text;
}


#if PLATFORM_WINDOWS
void listFilesRecursive(const u8 *directory, u8 ***fileList, u32 *count, u32 *capacity)
{
    WIN32_FIND_DATA findFileData;
    u8 searchPath[MAX_PATH];
    snprintf(searchPath, MAX_PATH, "%s/*", directory);

    HANDLE hFind = FindFirstFile(searchPath, &findFileData);
    if (hFind == INVALID_HANDLE_VALUE)
        return;

    do
    {
        const char *name = findFileData.cFileName;

        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
            continue;

        u8 fullPath[MAX_PATH];
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