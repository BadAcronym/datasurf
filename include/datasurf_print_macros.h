#ifndef DATASURF_PRINT_MACROS
#define DATASURF_PRINT_MACROS

// (Paulo):
// __LINE__ is a number, so we need to transform it into a string in the
// pre-processor with some funky stuff.
#define __STRINGIFY(x) #x
#define __TO_STRING(x) __STRINGIFY(x)
#define __LINE_STR __TO_STRING(__LINE__)
#define __LOCATION__ "[" __FILE__ ":" __LINE_STR "]"

#define DS_ERROR(MESSAGE, ...) \
        fprintf(stderr, "\n\033[31;1m" __LOCATION__ "\n\033[31;1;7mERROR: " \
                MESSAGE "\033[0m\n", ##__VA_ARGS__)

#define DS_WARN(MESSAGE, ...) \
        fprintf(stdout, "\033[33;1m" __LOCATION__ "\n\033[33;1;7mWARNING: " \
                MESSAGE "\033[0m\n", ##__VA_ARGS__)

#ifdef DEBUG
    #define DS_DEBUG(MESSAGE, ...) \
            fprintf(stdout, MESSAGE "\n", ##__VA_ARGS__)
#else
    #define DS_DEBUG(MESSAGE, ...)
#endif

#endif
