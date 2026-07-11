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

r2f2_fd r2f2_open(r2f2_fs_t *fs, const char *path, int oflag) {
    bool creat = oflag & O_CREAT;

    if (!path) {
        return RET_ERR;
    }

    RESULT(r2f2_fd) fd = r2f2_create_fd(fs, path);
    if (fd.code != RET_OK) {
        R2F2_LOG_ERR("failed (%d) to create fd for path '%s'", fd.code, path);
        return fd.code;
    }

    /*
     * look for the file
     * if it exists, open and return fd
     * if it doesn't exist, check oflags for O_CREAT and create file
     * otherwise, error
     */

    RESULT(block_idx) fib_ret = r2f2_find_file_indir_block(fs, path);
    if (fib_ret.code == RET_FILE_NOT_FOUND) {
        if (creat) {
            r2f2_ret ret = r2f2_register_file(fs, path, fd.code);
            if (ret != RET_OK) {
                R2F2_LOG_ERR("file '%s' creation failed (%d)", path, ret);
                return ret;
            }
        } else {
            R2F2_LOG_ERR("file '%s' doesn't exist. O_CREAT=%s", path,
                         creat ? "y" : "n");
            return RET_ERR;
        }
    } else if (fib_ret.code != RET_OK) {
        return fib_ret.code;
    }

    if (!creat) {
        /*
         * Our file already existed, so the last seq_entry contains the
         * information we need
         */

        RESULT(uint32_t) last_fie =
            get_last_file_indir_entry(fs, fib_ret.value);
        if (last_fie.code != RET_OK) {
            return last_fie.code;
        }

        file_indir_entry_t fie;
        r2f2_ret ret =
            read_file_indir_entry(fs, fib_ret.value, last_fie.value, &fie);
        if (ret != RET_OK) {
            return ret;
        }

        RESULT(uint32_t) last_fse = get_last_file_seq_entry(fs, fie.seq_block);
        if (last_fse.code != RET_OK) {
            return last_fse.code;
        }

        file_seq_entry_t fse;
        ret = read_file_seq_entry(fs, fie.seq_block, last_fse.value, &fse);
        if (ret != RET_OK) {
            return ret;
        }

        fildes_t *f = &fs->fds[fd.value];
        f->file_offset = 0;
        f->file_size = fse.current_file_size;
        f->file_indir_block = fib_ret.value;
        f->last_seq_block = fie.seq_block;
        f->next_seq_entry_idx = last_fse.value + 1;
        f->last_data_block = fse.data_block;
        f->last_data_block_fill = fse.data_block_fill_level;
    }
    return fd.value;
}

r2f2_ret r2f2_close(r2f2_fs_t *fs, r2f2_fd fd) {
    R2F2_FD_VALID_CHECK(fs, fd);

    r2f2_fsync(fs, fd);

    fs->fds[fd].active = false;
    return RET_OK;
}

off_t r2f2_lseek(r2f2_fs_t *fs, r2f2_fd fd, off_t offset, int whence) {
    R2F2_FD_VALID_CHECK(fs, fd);

    fildes_t *f = &fs->fds[fd];

    off_t new_offset = -1;

    if (whence == SEEK_SET) {
        new_offset = offset;
    } else if (whence == SEEK_CUR) {
        new_offset = f->file_offset + offset;
    } else if (whence == SEEK_END) {
        new_offset = f->file_size + offset;
    } else {
        return RET_EINVAL;
    }

    if (new_offset < 0) {
        return RET_OOB;
    }

    f->file_offset = new_offset;

    return new_offset;
}

ssize_t r2f2_read(r2f2_fs_t *fs, r2f2_fd fd, void *buf, size_t count) {
    R2F2_FD_VALID_CHECK(fs, fd);

    if (!buf) {
        return RET_EINVAL;
    }

    fildes_t *f = &fs->fds[fd];

    /* sanity check if this read is possible */
    if (f->file_offset + count > f->file_size + f->block_buffer.count) {
        R2F2_LOG_ERR("invalid read of size %zu for file with size %zu, offset "
                     "%zu, fd buffer size %zu",
                     count, f->file_size, f->file_offset,
                     f->block_buffer.count);
        return RET_OOB;
    }

    /*
     * if our offset falls into the block buffer, we read at least partially
     * from RAM
     */

    size_t can_read_from_storage = f->file_size - f->file_offset;
    if (can_read_from_storage >= count) {
        /* we have to find the appropriate data block to read from */
        RESULT(uint32_t) b =
            find_data_block_for_off(fs, f->file_indir_block, f->file_offset);
        if (b.code != RET_OK) {
            return b.code;
        }

        r2f2_ret ret =
            fs->cfg->flash_read(fs,
                                b.value * fs->cfg->geom.block_size +
                                    (f->file_offset % fs->cfg->geom.block_size),
                                count, buf);
        if (ret == RET_OK) {
            return count;
        } else {
            return ret;
        }
    } else {
        size_t to_read_from_fd_buf = count - can_read_from_storage;
        R2F2_LOG_ERR("have to read %zu B from fd buf", to_read_from_fd_buf);
        R2F2_LOG_ERR("unimplemented for now!");
        return RET_ERR;
    }

    return RET_ERR;
}

ssize_t r2f2_write(r2f2_fs_t *fs, r2f2_fd fd, const void *buf, size_t count) {
    R2F2_FD_VALID_CHECK(fs, fd);

    if (!buf) {
        return RET_EINVAL;
    }

    fildes_t *f = &fs->fds[fd];

#if R2F2_USE_WRITE_BUFFER
    /*
     * Is there space in our fd's block buffer?
     * If so, we write what we can into our block buffer. If it's full before
     * we're done, we need to flush/fsync, and write the rest into the block
     * buffer afterwards.
     */

    size_t total_written = 0;
    while (total_written < count) {
        if (f->block_buffer.count == fs->cfg->geom.block_size) {
            r2f2_ret ret = r2f2_fsync(fs, fd);
            if (ret != RET_OK) {
                return ret;
            }

            /* fsync has reset our block_buffer count to 0 */
            R2F2_ASSERT(f->block_buffer.count, ==, 0, "%zu");
        }

        size_t remaining_in_fd_buf =
            fs->cfg->geom.block_size - f->block_buffer.count;
        size_t remaining = count - total_written;
        size_t to_write = MIN(remaining_in_fd_buf, remaining);

        R2F2_ASSERT(to_write, >, 0, "%zu");

        /* write leftover data into the block buffer */
        uint8_t *write_pos = f->block_buffer.data + f->block_buffer.count;
        const uint8_t *src_pos = (const uint8_t *)buf + total_written;
        memcpy(write_pos, src_pos, to_write);

        f->block_buffer.count += to_write;
        total_written += to_write;
    }
    return total_written;
#endif

    return RET_ERR;
}

r2f2_ret r2f2_fsync(r2f2_fs_t *fs, r2f2_fd fd) {
    R2F2_FD_VALID_CHECK(fs, fd);

    fildes_t *f = &fs->fds[fd];
    if (f->block_buffer.count == 0) {
        return RET_OK;
    }

    size_t total_written = 0;
    while (total_written < f->block_buffer.count) {
        size_t last_data_block_cap =
            fs->cfg->geom.block_size - f->last_data_block_fill;

        if (last_data_block_cap == 0) {
            RESULT(block_idx) b = allocate_block(fs);
            if (b.code != RET_OK) {
                return b.code;
            }

            f->last_data_block = b.value;
            f->last_data_block_fill = 0;
            last_data_block_cap = fs->cfg->geom.block_size;
        }

        size_t to_write = MIN(f->block_buffer.count, last_data_block_cap);

        R2F2_ASSERT(total_written + to_write, <=, fs->cfg->geom.block_size,
                    "%zu");
        r2f2_ret ret = fs->cfg->flash_write(
            fs,
            f->last_data_block * fs->cfg->geom.block_size +
                f->last_data_block_fill,
            to_write, f->block_buffer.data + total_written);
        if (ret != RET_OK) {
            R2F2_LOG_ERR("failed (%d) to write %zu B to flash in block %u", ret,
                         to_write, f->last_data_block);
            return ret;
        }

        /* update file information in fd and "empty" its block buffer */
        f->last_data_block_fill += to_write;
        f->file_size += to_write;
        f->block_buffer.count -= to_write;

        /* we've written our data, time for the necessary metadata */

        if (f->next_seq_entry_idx >= NUM_FILE_SEQ_ENTRIES) {
            RESULT(block_idx) b = allocate_block(fs);
            if (b.code != RET_OK) {
                return b.code;
            }
            f->last_seq_block = b.value;
            f->next_seq_entry_idx = 0;
        }

        file_seq_entry_t fse;
        memset(&fse, 0xFF, sizeof(file_seq_entry_t));

        fse.data_block = f->last_data_block;
        fse.data_block_fill_level = f->last_data_block_fill;
        R2F2_ASSERT(f->file_size, >, 0, "%zu");
        fse.data_block_offset_in_file =
            fs->cfg->geom.block_size *
            ((f->file_size - 1) / fs->cfg->geom.block_size);
        fse.current_file_size = f->file_size;
        mark_entry_used(&fse.f);

        /* write the entry, then persist via flags */
        write_file_seq_entry(fs, f->last_seq_block, f->next_seq_entry_idx,
                             &fse);

        mark_entry_committed(&fse.f);
        write_file_seq_entry_flags(fs, f->last_seq_block, f->next_seq_entry_idx,
                                   &fse.f);

        f->next_seq_entry_idx++;

        total_written += to_write;
    }

    return RET_OK;
}
