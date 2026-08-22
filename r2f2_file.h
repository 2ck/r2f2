#pragma once

#include "r2f2_defines.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct fildes {
    char path[MAX_PATH_LEN];
    bool active;

    size_t file_offset;
    size_t file_size;

    struct {
        struct {
            block_idx block;
            uint32_t entry;
        } dir;

        struct {
            block_idx block;
        } indir;

        struct {
            block_idx last_block;
            uint32_t next_entry;
        } seq;

        struct {
            block_idx last_block;
            size_t last_block_fill;
        } data;
    } meta;

#if R2F2_USE_WRITE_BUFFER
    struct {
        size_t count;
        uint8_t *data;
    } block_buffer;
#endif
} fildes_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * returns the (first) dir_meta_block (of the linked block list) in which the
 * file resides, or a DIR_NOT_FOUND error in case no matching dir_meta_block was
 * found
 */
RESULT(block_idx) r2f2_find_dir_meta_block(r2f2_fs_t *fs, const char *path);

/**
 * returns the last dir_meta_block of the linked block list in which the
 * file resides
 */
RESULT(block_idx) r2f2_find_last_dir_meta_block(r2f2_fs_t *fs,
                                                const char *path);

/**
 * allocates a new dir_meta_block and sets the given block's next pointer
 * accordingly
 */
RESULT(block_idx) r2f2_create_next_dir_meta_block(r2f2_fs_t *fs, block_idx dmb);

/**
 * a file whose dir_meta_entry previously directly pointed to a file_seq_block
 * has now grown too large, so we insert a file_indir_block inbetween
 * updates the dir entry "in place" and updates the in-RAM fd contents
 *
  returns RET_OK on success or an error code on fail
 */
r2f2_ret r2f2_migrate_file_to_indir_block(r2f2_fs_t *fs, r2f2_fd fd);

/**
 * creates the in-flash metadata for a file. the directory the file resides in
 * must already exist
 */
RESULT(r2f2_fd) r2f2_register_file(r2f2_fs_t *fs, const char *path);

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
