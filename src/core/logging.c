#include "../../include/druid.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

const c8* colorCodes[LOG_MAX] = {
		"\x1b[31m",   //FATAL - red
		"\x1b[1;31m", //ERROR - bright red
		"\x1b[33m",   //WARNING - yellow
		"\x1b[32m",   //INFO - green
		"\x1b[36m",   //DEBUG - cyan
		"\x1b[37m"    //TRACE - white
};
const c8* colorReset = "\x1b[0m";

DAPI b8 useCustomOutputSrc = false;
DAPI void (*logOutputSrc)(LogLevel level, const c8* msg) = NULL;
b8 initLogging() 
{
	//TODO: Log file 
	return true;
}

void shutdownLogging() 
{
	
}

void logOutput (LogLevel level, const c8* message, ...)
{
	const c8* levelStrings[LOG_MAX] = {"[FATAL]","[ERROR]","[WARNING]","[INFO]","[DEBUG]","[TRACE]" };
	
	c8 outBuffer[LOG_BUFFER_SIZE];
	c8 buffer[LOG_BUFFER_SIZE];

	va_list bufferArgs;
	va_start(bufferArgs, message);
	vsnprintf(buffer, sizeof(buffer), message, bufferArgs);

	va_end(bufferArgs);

	snprintf(outBuffer, sizeof(outBuffer), "%s %s", levelStrings[level], buffer);

	if (useCustomOutputSrc)
	{
		logOutputSrc(level, outBuffer);
		return; 
	}

	//in future we can later add a different output for things like the imgui console
	if (level < LOG_WARNING)
	{
		fprintf(stderr, "%s%s%s\n", colorCodes[level], outBuffer, colorReset);
	}
	else
	{
		printf("%s%s%s\n", colorCodes[level], outBuffer, colorReset);
	}
}
