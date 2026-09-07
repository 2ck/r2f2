#pragma once

#include "r2f2_defines.h"
#include "r2f2_file.h"

#include <sys/types.h>

struct r2f2_cfg {
    void *user_ctx;

    /* must return RET_OK (0) on success */
    r2f2_ret (*flash_read)(r2f2_fs_t *fs, uint32_t addr, uint32_t len,
                           void *buf);
    /* must return RET_OK (0) on success */
    r2f2_ret (*flash_write)(r2f2_fs_t *fs, uint32_t addr, uint32_t len,
                            void *buf);
    /* must return RET_OK (0) on success */
    r2f2_ret (*flash_erase)(r2f2_fs_t *fs, uint32_t addr, uint32_t len);

    struct {
        uint32_t page_size;
        uint32_t block_size;
        uint32_t num_blocks;
    } geom;

    uint8_t placeholder;
};

struct r2f2_fs {
    r2f2_cfg_t *cfg;
    block_idx root_dir_block;
#ifdef ECC_ON_METADATA
    struct bch_control *u32_bch;
    struct bch_control *path_bch;
#endif
#ifdef ECC_ON_DATA
    struct bch_control *data_bch;
#endif
    fildes_t fds[MAX_NUM_FDS];
};

#ifdef __cplusplus
extern "C" {
#endif

r2f2_ret r2f2_format(r2f2_fs_t *fs);
r2f2_ret r2f2_mount(r2f2_fs_t *fs);
r2f2_ret r2f2_unmount(r2f2_fs_t *fs);

r2f2_fd r2f2_open(r2f2_fs_t *fs, const char *path, int oflag);
r2f2_ret r2f2_close(r2f2_fs_t *fs, r2f2_fd fd);
r2f2_ret r2f2_mkdir(r2f2_fs_t *fs, const char *path);
off_t r2f2_lseek(r2f2_fs_t *fs, r2f2_fd fd, off_t offset, int whence);
ssize_t r2f2_read(r2f2_fs_t *fs, r2f2_fd fd, void *buf, size_t count);
ssize_t r2f2_write(r2f2_fs_t *fs, r2f2_fd fd, const void *buf, size_t count);
r2f2_ret r2f2_fsync(r2f2_fs_t *fs, r2f2_fd fd);
r2f2_ret r2f2_remove(r2f2_fs_t *fs, const char *path);

#ifdef __cplusplus
}
#endif
