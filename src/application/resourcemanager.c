
#include "../../include/druid.h"
#if PLATFORM_WINDOWS
#define NOGDI
#define NOERROR
#include <windows.h>
#else
#include <dirent.h>
#endif
#include <stdio.h>


u32 djb2Hash(const void* inStr, u32 capacity)
{
    const char* str = (const char*)inStr;
    u32 hash = 5381;
    i32 c;

    while ((c = *str++))
    {
        hash = ((hash << 5) + hash) + c;
    }

    return hash % capacity;
}

b8 equals(const void* a, const void* b)
{
    return strcmp((const char*)a, (const char*)b) == 0;
}

ResourceManager* createResourceManager(u32 materialCount, u32 meshCount, u32 modelCount, u32 shaderCount)
{
    return NULL;
}

void readResources(ResourceManager* manager, const char* filename)
{
    // This function should read resources from a file and populate the ResourceManager
	DEBUG("Reading resources from %s\n", filename);
	//do a recursive search for all files in the RES_FOLDER directory
	const u32 bufferSize = 1024;
    char* output = (char*)malloc(bufferSize * bufferSize);

	//list all files in the directory and take it to output
    if (listFilesInDirectory(RES_FOLDER, output, bufferSize))
    {
        DEBUG("Files in directory: %s\n", output);
    }
    else
    {
        WARN("Failed to list files in directory %s\n", RES_FOLDER);
	}
}

char* listFilesInDirectory(const char* directory, char* output,u32 outputSize)
{
    


#if PLATFORM_WINDOWS == 1
	//use windows code to list files
    WIN32_FIND_DATA findFileData; //store the result 
    char searchPath[MAX_PATH];//buffer to store where we are looking
    snprintf(searchPath, MAX_PATH, "%s\\*", directory); //formant the data and return it back to search path

    //try and find anything
    HANDLE hFind = FindFirstFile(searchPath, &findFileData);
    if (hFind == INVALID_HANDLE_VALUE)
    {
        ERROR("Could not open directory: %s",directory); 
        return NULL;
    }

    do {
        DEBUG("%s",findFileData.cFileName);
    } while (FindNextFile(hFind, &findFileData) != 0);
    FindClose(hFind);
#elif PLATFORM_LINUX == 1
    u32 fileCount = 0;

	struct dirent* entry;

	DIR* dir = opendir(directory);
    if (dir == NULL)
    {
		WARN("Could not open directory: %s\n", directory);
        return &fileNames;
    }
	// Read all entries in the directory and add them to the output array
    while ((entry = readdir(dir)) != NULL)
    {
        //print the files
        
    }
    
    closedir(dir);
#endif

    if (output == NULL)
    {
        ERROR("Failed to allocate memory for output array");
		return NULL;
    }


    return output;
}

