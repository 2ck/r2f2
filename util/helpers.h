#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef MIN
#  define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#  define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

#if __STDC_VERSION__ >= 201112L
#  define STATIC_ASSERT(cond) _Static_assert(cond, "")
#else
#  define STATIC_ASSERT_GLUE(a, b) a##b
#  define STATIC_ASSERT_XGLUE(a, b) STATIC_ASSERT_GLUE(a, b)
#  define STATIC_ASSERT(cond)                                                  \
      typedef char STATIC_ASSERT_XGLUE(static_assert_,                         \
                                       __LINE__)[(cond) ? 1 : -1]
#endif

#ifdef __cplusplus
extern "C" {
#endif

const char *get_basename(const char *path);

bool is_all_zero(const void *buf, size_t size);

void hexdump(const uint8_t *buf, size_t len);

#ifdef __cplusplus
}
#endif
