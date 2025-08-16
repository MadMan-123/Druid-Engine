#include "../../include/druid.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

const char* colorCodes[LOG_MAX] = {
		"\x1b[31m",   //FATAL - red
		"\x1b[1;31m", //ERROR - bright red
		"\x1b[33m",   //WARNING - yellow
		"\x1b[32m",   //INFO - green
		"\x1b[36m",   //DEBUG - cyan
		"\x1b[37m"    //TRACE - white
};
const char* colorReset = "\x1b[0m";


bool initLogging() 
{
	//TODO: Log file 
	return true;
}

void shutdownLogging() 
{
	
}

void logOutput (LogLevel level, const char* message, ...)
{
	const char* levelStrings[LOG_MAX] = {"[FATAL]","[ERROR]","[WARNING]","[INFO]","[DEBUG]","[TRACE]" };
	
	char outBuffer[LOG_BUFFER_SIZE];
	//buffer to hold the formatted message
	char buffer[LOG_BUFFER_SIZE];
	
	va_list bufferArgs;
	va_start(bufferArgs, message);	
	//format the message
	vsnprintf(buffer, sizeof(buffer), message, bufferArgs);

	va_end(bufferArgs);

	//format the final output with the level and message
	snprintf(outBuffer, sizeof(outBuffer), "%s %s", levelStrings[level], buffer);


	//in future we can later add a different output for things like the imgui console
	if (level < LOG_WARNING) 
	{
		//print to stderr for fatal and error
		fprintf(stderr, "%s%s%s\n", colorCodes[level], outBuffer, colorReset);
	}
	else 
	{
		//print to stdout for other levels
		printf("%s%s%s\n", colorCodes[level], outBuffer, colorReset);
	}
}
