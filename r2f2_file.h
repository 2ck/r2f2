#pragma once

#include "r2f2_defines.h"
#include <stdbool.h>

struct fildes {
    char path[MAX_PATH_LEN];
    bool active;

    size_t file_offset;
    size_t file_size;

    block_idx file_indir_block;

    block_idx last_meta_block;
    uint32_t next_meta_entry_idx;

    block_idx last_data_block;
    size_t last_data_block_fill;

#if R2F2_USE_WRITE_BUFFER
    struct {
        size_t count;
        /* TODO: should be malloc'ed on mount */
        uint8_t data[BLOCK_SIZE];
    } block_buffer;
#endif
};

#ifdef __cplusplus
extern "C" {
#endif

/**
 * returns the dir_meta_block in which the file resides, or a DIR_NOT_FOUND
 * error in case no matching dir_meta_block was found
 */
RESULT(block_idx) r2f2_find_dir_meta_block(r2f2_fs_t *fs, const char *path);
/**
 * returns the file's first file_indir_block as pointed to by the
 * dir_meta_entry, or a FILE_NOT_FOUND error in case no matching entry was found
 */
RESULT(block_idx) r2f2_find_file_indir_block(r2f2_fs_t *fs, const char *path);

/**
 * creates the in-flash metadata for a file. the directory the file resides in
 * must already exist
 */
r2f2_ret r2f2_register_file(r2f2_fs_t *fs, const char *path, r2f2_fd fd);

/**
 * create an in-RAM file descriptor
 */
RESULT(r2f2_fd) r2f2_create_fd(r2f2_fs_t *fs, const char *path);
r2f2_ret r2f2_fd_valid(r2f2_fs_t *fs, r2f2_fd fd);

#define R2F2_FD_VALID_CHECK(fs, fd)                                            \
    do {                                                                       \
        r2f2_ret ret = r2f2_fd_valid(fs, fd);                                  \
        if (ret != RET_OK) {                                                   \
            R2F2_LOG_ERR("invalid (%d) fd %d", ret, fd);                       \
            return ret;                                                        \
        }                                                                      \
    } while (0)

#ifdef __cplusplus
}
#endif
