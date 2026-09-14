#pragma once

#include "util/result_type.h"
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>

#define R2F2_MALLOC malloc
#define R2F2_FREE free

typedef struct r2f2_fs r2f2_fs_t;
typedef struct r2f2_cfg r2f2_cfg_t;

typedef uint32_t r2f2_fd;

/* for compatibility. TODO: change naming? */
typedef ret_code_t r2f2_ret;

typedef uint32_t block_idx;

RESULT_DECL(voidp, void *);
RESULT_DECL_TRIVIAL(uint32_t);
RESULT_DECL_TRIVIAL(block_idx);
RESULT_DECL_TRIVIAL(r2f2_fd);

#ifndef ECC_ON_METADATA
#  define ECC_ON_METADATA
#endif

#ifdef ECC_ON_METADATA
#  include "bch.h"
#  define ECC_BCH_U32_M 6
#  define ECC_BCH_U32_T 4
#  define ECC_BCH_U32_ECCLEN 3

#  define ECC_BCH_PATH_M 10
#  define ECC_BCH_PATH_T 12
#  define ECC_BCH_PATH_ECCLEN 15

#  define MAX_PATH_LEN (95U)

/* TODO: actually read the second metadata entries if necessary */
/* #  ifndef DOUBLE_METADATA */
/* #    define DOUBLE_METADATA */
/* #  endif */

#  ifdef DOUBLE_METADATA
#    define NUM_DIR_META_ENTRIES (15U)
#    define NUM_FILE_INDIR_ENTRIES (123U)
#    define NUM_FILE_SEQ_ENTRIES (64U)
#  else
#    define NUM_DIR_META_ENTRIES (31U)
#    define NUM_FILE_INDIR_ENTRIES (224U)
#    define NUM_FILE_SEQ_ENTRIES (127U)
#  endif

#  define MAX_NUM_FDS (32U)

#  ifndef NUM_NEXT_PTRS
#    define NUM_NEXT_PTRS 2
#  endif

typedef struct __attribute__((packed)) flash_u32_t {
    uint8_t data[4];
    uint8_t ecc[ECC_BCH_U32_ECCLEN];
} flash_u32;

RESULT(uint32_t) get_flash_u32(struct bch_control *bch, flash_u32 u);
#  define GET_FLASH_U32(u) get_flash_u32(fs->u32_bch, u)

#  define SET_FLASH_U32(u, val)                                                \
      do {                                                                     \
          R2F2_ASSERT((void *)fs->u32_bch, !=, NULL, "%p");                    \
          uint32_t _v = (uint32_t)val;                                         \
          memset(&u, 0, sizeof(flash_u32));                                    \
          memcpy(u.data, &_v, sizeof(u.data));                                 \
          encode_bch(fs->u32_bch, u.data, sizeof(u.data), u.ecc);              \
          bch_counts[BCHC_U32_IDX].encodes++;                                  \
      } while (0)

typedef flash_u32 flash_block_idx;

r2f2_ret correct_path(r2f2_fs_t *fs, char *path, void *path_ecc);

#else

#  define MAX_PATH_LEN (118U)

#  define NUM_DIR_META_ENTRIES (31U)
#  define NUM_FILE_INDIR_ENTRIES (384U)
#  define NUM_FILE_SEQ_ENTRIES (128U)

#  define MAX_NUM_FDS (32U)

typedef uint32_t flash_u32;

#  define GET_FLASH_U32(u) (RESULT_OK(uint32_t, u))

#  define SET_FLASH_U32(u, val)                                                \
      do {                                                                     \
          u = val;                                                             \
      } while (0)

typedef block_idx flash_block_idx;

#endif

#ifndef NUM_NEXT_PTRS
#  define NUM_NEXT_PTRS 2
#endif

#define DECL_FLASH_BLOCK_IDX(name) flash_block_idx name = {0};
/* FIXME: get rid of block_idx and just use uint32_t? */
#define GET_FLASH_BLOCK_IDX(bix)                                               \
    ({                                                                         \
        RESULT(uint32_t) r = GET_FLASH_U32(bix);                               \
        RESULT(block_idx) b;                                                   \
        b.code = r.code;                                                       \
        b.value = r.value;                                                     \
        b;                                                                     \
    })
#define SET_FLASH_BLOCK_IDX(bix, val) SET_FLASH_U32(bix, val)

#ifndef ECC_ON_DATA
#  define ECC_ON_DATA
#endif

#ifdef ECC_ON_DATA
#  define ECC_BCH_DATA_M 12
#  define ECC_BCH_DATA_T 16
#  define ECC_BCH_DATA_ECCLEN 24
#  define ECC_BCH_DATA_RES_PG 2
#endif

/* bch operation counters */
typedef struct bch_c {
    size_t encodes;
    size_t decodes;
} bch_c_t;
#ifdef ECC_ON_METADATA
#  define BCHC_U32_IDX 0
#  define BCHC_PATH_IDX 1

#  ifdef ECC_ON_DATA
#    define BCHC_DATA_IDX 2
#    define BCHC_SIZE 3
#  else
#    define BCHC_SIZE 2
#  endif

extern bch_c_t bch_counts[BCHC_SIZE];

#endif

#ifdef __cplusplus
extern "C" {
#endif
void reset_bch_counts(void);
void print_bch_counts(void);
#ifdef __cplusplus
}
#endif
