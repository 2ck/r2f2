#include "r2f2.h"
#include "r2f2_alloc.h"
#include "r2f2_file.h"
#include "r2f2_metadata.h"
#include "util/logger.h"
#include <string.h>

r2f2_ret r2f2_format(r2f2_fs_t *fs) {
    r2f2_fs_info_t fs_info;
    memset(&fs_info, 0xFF, sizeof(r2f2_fs_info_t));

    /* TODO: chip erase */

    prepare_block_allocator(fs);

    fs_info.global_metadata.magic = R2F2_MAGIC;
    RESULT(block_idx) b = allocate_block(fs);
    if (b.code != RET_OK) {
        return b.code;
    }
    fs_info.root_dir_block = b.value;
    fs->root_dir_block = b.value;

    r2f2_ret ret =
        fs->cfg->flash_write(fs, R2F2_SUPERBLOCK_IDX * fs->cfg->geom.block_size,
                             sizeof(r2f2_fs_info_t), &fs_info);

    if (ret != RET_OK) {
        R2F2_LOG_ERR("flash write failed at block %u", R2F2_SUPERBLOCK_IDX);
        return ret;
    }

    return RET_OK;
}

r2f2_ret r2f2_mount(r2f2_fs_t *fs) {
    /* TODO: all sorts of validity checks */

    r2f2_fs_info_t fs_info;
    /* read root block to see if there is logfs on flash */
    fs->cfg->flash_read(fs, R2F2_SUPERBLOCK_IDX * fs->cfg->geom.block_size,
                        sizeof(r2f2_fs_info_t), &fs_info);

    /* TODO: check if base block contents match cfg */
    if (!is_fs_valid(fs, &fs_info)) {
        /* we need to format */
        r2f2_format(fs);
    } else {
        fs->root_dir_block = fs_info.root_dir_block;
    }

    /* we are mounted */
    /* TODO: set up buffers/caches or something, idk */
    return RET_OK;
}

r2f2_ret r2f2_open(r2f2_fs_t *fs, const char *path, int oflag) {
    bool creat = oflag & O_CREAT;

    if (!path) {
        return RET_ERR;
    }

    /*
     * look for the file
     * if it exists, open and return fd
     * if it doesn't exist, check oflags for O_CREAT and create file
     * otherwise, error
     */

    RESULT(block_idx) fmb_ret = r2f2_find_file_meta_block(fs, path);
    if (fmb_ret.code == RET_FILE_NOT_FOUND) {
        if (creat) {
            r2f2_ret ret = r2f2_register_file(fs, path);
            if (ret != RET_OK) {
                R2F2_LOG_ERR("file '%s' creation failed (%d)", path, ret);
                return ret;
            }
        } else {
            R2F2_LOG_ERR("file '%s' doesn't exist. O_CREAT=%s", path,
                         creat ? "y" : "n");
            return RET_ERR;
        }
    } else if (fmb_ret.code != RET_OK) {
        return fmb_ret.code;
    }

    RESULT(r2f2_fd) fd = r2f2_create_fd(fs, path);
    if (fd.code != RET_OK) {
        R2F2_LOG_ERR("failed (%d) to create fd for path '%s'", fd.code, path);
        return fd.code;
    }
    return fd.value;
}

r2f2_ret r2f2_close(r2f2_fs_t *fs, r2f2_fd fd) {
    r2f2_ret ret = r2f2_fd_valid(fs, fd);
    if (ret != RET_OK) {
        R2F2_LOG_ERR("invalid (%d) fd %d", ret, fd);
        return ret;
    }

    fds[fd].active = false;
    return RET_OK;
}
