#pragma once

#include "r2f2_defines.h"

typedef struct r2f2_fs r2f2_fs_t;
typedef struct r2f2_cfg r2f2_cfg_t;

struct r2f2_cfg {
    void *user_ctx;

    r2f2_ret (*flash_read)(r2f2_fs_t *fs, uint32_t addr, uint32_t len,
                           void *buf);
    r2f2_ret (*flash_write)(r2f2_fs_t *fs, uint32_t addr, uint32_t len,
                            void *buf);
    r2f2_ret (*flash_erase)(r2f2_fs_t *fs, uint32_t addr, uint32_t len);

    struct {
        uint16_t page_size;
        uint16_t block_size;
        uint32_t num_blocks;
    } geom;

    uint8_t placeholder;
};

struct r2f2_fs {
    r2f2_cfg_t *cfg;
    block_idx root_dir_block;
};

#ifdef __cplusplus
extern "C" {
#endif

r2f2_ret r2f2_format(r2f2_fs_t *fs);
r2f2_ret r2f2_mount(r2f2_fs_t *fs);

r2f2_ret r2f2_open(r2f2_fs_t *fs, const char *path, int oflag);
r2f2_ret r2f2_close(r2f2_fs_t *fs, r2f2_fd fd);
r2f2_ret r2f2_lseek(r2f2_fs_t *fs, r2f2_fd fd, off_t offset, int whence);
r2f2_ret r2f2_read(r2f2_fs_t *fs, int fd, void *buf, size_t count);
r2f2_ret r2f2_write(r2f2_fs_t *fs, r2f2_fd fd, const void *buf, size_t count);
r2f2_ret r2f2_fsync(r2f2_fs_t *fs, r2f2_fd fd);

#ifdef __cplusplus
}
#endif
