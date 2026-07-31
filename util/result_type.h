#pragma once

#include <stdint.h>

typedef enum {
    RET_OK = 0,
    RET_ERR = -1,
    RET_EINVAL = -2,
    RET_NOT_FOUND = -3,
    RET_NOMEM = -4,
    RET_OOB = -5,
    RET_DIR_NOT_FOUND = -6,
    RET_FILE_NOT_FOUND = -7,
    RET_ECC_ERR = -8,
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

#define CHECK_OK_RETURN(res)                                                   \
    do {                                                                       \
        ret_code_t _code = (res).code;                                         \
        if (_code != RET_OK) {                                                 \
            return _code;                                                      \
        }                                                                      \
    } while (0)

#define CHECK_OK_PROPAGATE(res, type)                                          \
    do {                                                                       \
        ret_code_t _code = (res).code;                                         \
        if (_code != RET_OK) {                                                 \
            return RESULT_ERR(type, _code);                                    \
        }                                                                      \
    } while (0)

#define CHECK_OK_BASIC(code)                                                   \
    do {                                                                       \
        ret_code_t _code = (code);                                             \
        if (_code != RET_OK) {                                                 \
            return _code;                                                      \
        }                                                                      \
    } while (0)
