#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef R2F2_PRINTF
#  define R2F2_PRINTF printf
#endif

#ifndef R2F2_LOG_LEVEL
#  define R2F2_LOG_LEVEL LOGLV_DEBUG
#endif

typedef enum {
    LOGLV_ERROR = 0,
    LOGLV_WARN = 1,
    LOGLV_INFO = 2,
    LOGLV_DEBUG = 3
} LogLevel;

#define R2F2_LOG(lvl, fmt, ...)                                                \
    do {                                                                       \
        if ((lvl) <= R2F2_LOG_LEVEL) {                                         \
            const char *lvlstring = lvl == LOGLV_ERROR   ? "ERR"               \
                                    : lvl == LOGLV_WARN  ? "WRN"               \
                                    : lvl == LOGLV_INFO  ? "INF"               \
                                    : lvl == LOGLV_DEBUG ? "DBG"               \
                                                         : "***";              \
            R2F2_PRINTF("[%s] %s:%d@%s: " fmt "\n", lvlstring, __FILE__,       \
                        __LINE__, __func__, ##__VA_ARGS__);                    \
        }                                                                      \
    } while (0)

#define R2F2_LOG_ERR(...) R2F2_LOG(LOGLV_ERROR, __VA_ARGS__)
#define R2F2_LOG_INFO(...) R2F2_LOG(LOGLV_INFO, __VA_ARGS__)
#define R2F2_LOG_WARN(...) R2F2_LOG(LOGLV_WARN, __VA_ARGS__)
#define R2F2_LOG_DEBUG(...) R2F2_LOG(LOGLV_DEBUG, __VA_ARGS__)

#ifndef R2F2_ABORT
#  define R2F2_ABORT abort
#endif

#define R2F2_ASSERT(thing, cond, otherthing, fmt)                              \
    do {                                                                       \
        __auto_type _thing = (thing);                                          \
        __typeof__(_thing) _otherthing = (otherthing);                         \
        if (!(_thing cond _otherthing)) {                                      \
            R2F2_LOG_ERR(#thing " is " fmt ", expected " fmt, _thing,          \
                         _otherthing);                                         \
            R2F2_ABORT();                                                      \
        }                                                                      \
    } while (0)
