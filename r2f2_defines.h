#pragma once

#include "util/result_type.h"
#include <fcntl.h>
#include <stdint.h>

#define PAGE_SIZE (256U)
#define BLOCK_SIZE (4096U)
#define MAX_PATH_LEN (123U)

#define NUM_DIR_META_ENTRIES (32U)
#define NUM_FILE_INDIR_ENTRIES (256U)
#define NUM_FILE_META_ENTRIES (128U)

#define MAX_NUM_FDS (32U)

#ifndef R2F2_USE_WRITE_BUFFER
#  define R2F2_USE_WRITE_BUFFER 1
#endif

typedef struct r2f2_fs r2f2_fs_t;
typedef struct r2f2_cfg r2f2_cfg_t;
typedef struct fildes fildes_t;

typedef uint32_t block_idx;
typedef uint32_t r2f2_fd;

/* for compatibility. TODO: change naming? */
typedef ret_code_t r2f2_ret;

RESULT_DECL(voidp, void *);
RESULT_DECL_TRIVIAL(uint32_t);
RESULT_DECL_TRIVIAL(block_idx);
RESULT_DECL_TRIVIAL(r2f2_fd);
