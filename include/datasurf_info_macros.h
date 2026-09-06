#ifndef DATASURF_INFO_MACROS
#define DATASURF_INFO_MACROS

//__LINE__ is a number, so we need to transform it into a string in the pre-processor with some funky stuff.
#define __STRINGIFY(x) #x
#define __TO_STRING(x) __STRINGIFY(x)
#define __LINE_STR __TO_STRING(__LINE__)
#define __LOCATION__ "<" __FILE__ ":" __LINE_STR ">"

#ifdef DATASURF_DEBUG_MODE
	#include <stdio.h>
	#define DATASURF_DEBUG(STREAM, MESSAGE, ...) fprintf(STREAM, "DEBUG " __LOCATION__ " : " MESSAGE "\n", ##__VA_ARGS__)
#else
	#define DATASURF_DEBUG(STREAM, MESSAGE, ...)
#endif

#ifdef DATASURF_INFO_MODE
	#define DATASURF_INFO(STREAM, MESSAGE, ...) fprintf(STREAM, "\033[32;1mINFO " __LOCATION__ " : " MESSAGE "\033[0m\n", ##__VA_ARGS__)
#else
	#define DATASURF_INFO(STREAM, MESSAGE, ...)
#endif

#define DATASURF_ERROR(STREAM, MESSAGE, ...) fprintf(STREAM, "\033[31;1mERROR " __LOCATION__ " : " MESSAGE "\033[0m\n", ##__VA_ARGS__)
#define DATASURF_WARNING(STREAM, MESSAGE, ...) fprintf(STREAM, "\033[33;1mWARNING " __LOCATION__ " : " MESSAGE "\033[0m\n", ##__VA_ARGS__)

#endif
