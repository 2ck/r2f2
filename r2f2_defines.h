#pragma once

#include <fcntl.h>
#include <stdint.h>

#define PAGE_SIZE (256U)
#define BLOCK_SIZE (4096U)
#define MAX_PATH_LEN (123U)

#define NUM_DIR_META_ENTRIES (32U)
#define NUM_FILE_META_ENTRIES (256U)
#define NUM_FILE_INDIR_ENTRIES (128U)

typedef uint32_t block_idx;
typedef int32_t r2f2_fd;

typedef enum {
    RET_OK = 0,
    RET_ERR = -1,
} r2f2_ret;
