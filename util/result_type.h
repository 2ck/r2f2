#pragma once

#include <stdint.h>

typedef enum {
    RET_OK = 0,
    RET_ERR = -1,
    RET_INVALID_ARG = -2,
    RET_NOT_FOUND = -3,
    RET_NOMEM = -4,
} ret_code_t;

#define RESULT(name) result_##name##_t

#define RESULT_DECL(name, T)                                                   \
    typedef struct {                                                           \
        ret_code_t code;                                                       \
        T value;                                                               \
    } RESULT(name)

#define RESULT_DECL_TRIVIAL(T) RESULT_DECL(T, T)

#define RESULT_OK(name, v) ((RESULT(name)){.code = RET_OK, .value = (v)})

#define RESULT_ERR(name, e) ((RESULT(name)){.code = (e)})
