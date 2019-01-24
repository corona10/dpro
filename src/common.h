#include <stdlib.h>

#ifndef PYSTOL_COMMON_H
#define PYSTOL_COMMON_H

#define _STRINGIFY(N) #N
#define STRINGIFY(N) _STRINGIFY(N)
#define RELEASE_ASSERT(condition, fmt, ...)                                                                            \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            ::fprintf(stderr, __FILE__ ":" STRINGIFY(__LINE__) ": %s: Assertion `" #condition "' failed: " fmt "\n",   \
                      __PRETTY_FUNCTION__, ##__VA_ARGS__);                                                             \
            ::abort();                                                                                                 \
        }                                                                                                              \
    } while (false)

#endif
