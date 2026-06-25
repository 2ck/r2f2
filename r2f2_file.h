#pragma once

#include "r2f2.h"
#include "r2f2_defines.h"
#include <stdbool.h>

struct fildes {
    char path[MAX_PATH_LEN];
    bool active;
    /* block_idx meta_block; */
    /* block_idx dir_block; */
    /* // block_idx start_block = 0; */
    /* block_idx last_block = 0; */
    /* size_t file_offset = 0; */
    /* size_t file_size = 0; */
    /* size_t last_block_fill_level = 0; */

    /* // should optimize sequential read and write */
    /* // for the latter, last_block also works, but only when appending */
    /* block_idx recently_used_block = 0; */

    /* block_idx recently_used_indir_block = 0; */
    /* uint8_t next_indir_entry = 0; */

    struct {
        size_t count;
        uint8_t data[BLOCK_SIZE];
    } block_buffer;
} fds[MAX_NUM_FDS];

#ifdef __cplusplus
extern "C" {
#endif

r2f2_ret r2f2_find_file(r2f2_fs_t *fs, const char *path);
RESULT(r2f2_fd) r2f2_create_file(r2f2_fs_t *fs, const char *path);
RESULT(block_idx) r2f2_traverse_dirs(r2f2_fs_t *fs, const char *path);

RESULT(r2f2_fd) r2f2_create_fd(r2f2_fs_t *fs, const char *path);

#ifdef __cplusplus
}
#endif
