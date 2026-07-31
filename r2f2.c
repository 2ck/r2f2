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

    SET_FLASH_U32(fs_info.global_metadata.magic, R2F2_MAGIC);
    RESULT(block_idx) b = allocate_block(fs);
    if (b.code != RET_OK) {
        return b.code;
    }
    /* TODO: remove this and just search for it in block 1/2 or something */
    /* alternatively, make this have several next_block-pointers */

    SET_FLASH_BLOCK_IDX(fs_info.root_dir_block, b.value);
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
    R2F2_ASSERT(fs->cfg->geom.block_size % fs->cfg->geom.page_size, ==, 0,
                "%u");

    /* data structure size checks */
    R2F2_ASSERT(sizeof(struct r2f2_fs_info), <=, fs->cfg->geom.page_size,
                "%zu");
    R2F2_ASSERT(sizeof(struct r2f2_superblock), <=, fs->cfg->geom.block_size,
                "%zu");
    R2F2_ASSERT(sizeof(struct dir_meta_block), <=, fs->cfg->geom.block_size,
                "%zu");
    R2F2_ASSERT(sizeof(struct file_indir_block), <=, fs->cfg->geom.block_size,
                "%zu");
    R2F2_ASSERT(sizeof(struct file_seq_block), <=, fs->cfg->geom.block_size,
                "%zu");

#ifdef ECC_ON_METADATA
    fs->bch = init_bch(ECC_BCH_M, ECC_BCH_T, 0);
    R2F2_ASSERT((void *)fs->bch, !=, NULL, "%p");
    R2F2_ASSERT(fs->bch->ecc_bytes, ==, ECC_BCH_LEN, "%u");
#endif

    r2f2_fs_info_t fs_info;
    /* read root block to see if there is logfs on flash */
    fs->cfg->flash_read(fs, R2F2_SUPERBLOCK_IDX * fs->cfg->geom.block_size,
                        sizeof(r2f2_fs_info_t), &fs_info);

    /* TODO: check if base block contents match cfg */
    if (!is_fs_valid(fs, &fs_info)) {
        /* we need to format */
        r2f2_format(fs);
    } else {
        RESULT(block_idx) b = GET_FLASH_BLOCK_IDX(fs_info.root_dir_block);
        CHECK_OK_RETURN(b);
        fs->root_dir_block = b.value;
    }

    /* we are mounted */
    /* TODO: set up buffers/caches or something, idk */
    for (size_t i = 0; i < MAX_NUM_FDS; i++) {
        fs->fds[i].active = false;
    }
    return RET_OK;
}

r2f2_fd r2f2_open(r2f2_fs_t *fs, const char *path, int oflag) {
    bool creat = oflag & O_CREAT;

    if (!path) {
        return RET_ERR;
    }

    dir_meta_entry_t dme;
    struct dir_traversal_ret dir_ret;
    r2f2_ret ret = r2f2_get_file_dir_entry(fs, path, &dme, &dir_ret);

    if (ret == RET_OK) {
        /* file exists already */
        flash_block_idx next[NUM_NEXT_PTRS];
        memcpy(next, dme.next_block, sizeof(next));
        RESULT(block_idx) next_block = get_valid_next_block(fs, next);
        if (next_block.code != RET_OK) {
            return next_block.code;
        }

        block_idx indir_block_idx = 0;
        block_idx seq_block_idx = 0;

        /*
         * The dir_meta_entry could point directly to a file_seq_block, or do so
         * via a file_indir_block, and the flags tell us how it is.
         */
        if (is_entry_indirect(dir_ret.dme_flags) == ENTRY_FLAG_SET) {
            indir_block_idx = next_block.value;
            RESULT(uint32_t) last_fie =
                get_last_file_indir_entry(fs, indir_block_idx);
            if (last_fie.code != RET_OK) {
                return last_fie.code;
            }

            file_indir_entry_t fie;
            r2f2_ret ret = read_file_indir_entry(fs, indir_block_idx,
                                                 last_fie.value, &fie);
            if (ret != RET_OK) {
                return ret;
            }

            flash_block_idx next[NUM_NEXT_PTRS];
            memcpy(next, fie.seq_block, sizeof(next));
            RESULT(block_idx) seq_block = get_valid_next_block(fs, next);
            if (seq_block.code != RET_OK) {
                return seq_block.code;
            }
            seq_block_idx = seq_block.value;
        } else {
            seq_block_idx = next_block.value;
        }

        RESULT(uint32_t) last_fse = get_last_file_seq_entry(fs, seq_block_idx);
        /*
         * Our file already existed, so the last file_seq_entry normally
         * contains the information we need. However, if the file was
         * freshly created and has no contents yet, we don't even have such
         * an entry yet.
         */

        if (last_fse.code == RET_NOT_FOUND) {
            RESULT(r2f2_fd) fd = r2f2_create_fd(fs, path);
            if (fd.code != RET_OK) {
                R2F2_LOG_ERR("failed (%d) to create fd for path '%s'", fd.code,
                             path);
                return fd.code;
            }
            fildes_t *f = &fs->fds[fd.value];
            f->file_offset = 0;
            f->file_size = 0;
            f->meta.dir.block = dir_ret.dmb_idx;
            f->meta.dir.entry = dir_ret.dme_idx;
            f->meta.indir.block = indir_block_idx;
            f->meta.seq.last_block = seq_block_idx;
            f->meta.seq.next_entry = 0;
            f->meta.data.last_block = 0;
            f->meta.data.last_block_fill = 0;
            return fd.value;
        } else if (last_fse.code != RET_OK) {
            return last_fse.code;
        }

        /* we do have a file_seq_entry already */

        file_seq_entry_t fse;
        r2f2_ret ret =
            read_file_seq_entry(fs, seq_block_idx, last_fse.value, &fse);
        if (ret != RET_OK) {
            return ret;
        }

        RESULT(r2f2_fd) fd = r2f2_create_fd(fs, path);
        if (fd.code != RET_OK) {
            R2F2_LOG_ERR("failed (%d) to create fd for path '%s'", fd.code,
                         path);
            return fd.code;
        }
        fildes_t *f = &fs->fds[fd.value];
        f->file_offset = 0;
        RESULT(uint32_t) file_size = GET_FLASH_U32(fse.current_file_size);
        CHECK_OK_RETURN(file_size);
        f->file_size = file_size.value;

        f->meta.dir.block = dir_ret.dmb_idx;
        f->meta.dir.entry = dir_ret.dme_idx;
        f->meta.indir.block = indir_block_idx;
        f->meta.seq.last_block = seq_block_idx;
        f->meta.seq.next_entry = last_fse.value + 1;
        RESULT(block_idx) last_data_block = GET_FLASH_BLOCK_IDX(fse.data_block);
        CHECK_OK_RETURN(last_data_block);
        f->meta.data.last_block = last_data_block.value;
        RESULT(uint32_t) last_fill = GET_FLASH_U32(fse.data_block_fill_level);
        CHECK_OK_RETURN(last_fill);
        f->meta.data.last_block_fill = last_fill.value;

        return fd.value;
    } else if (ret == RET_NOT_FOUND && creat) {
        /* file doesn't exist but we're supposed to create it */
        RESULT(r2f2_fd) fd = r2f2_register_file(fs, path);
        if (fd.code != RET_OK) {
            R2F2_LOG_ERR("file '%s' creation failed (%d)", path, fd.code);
            return fd.code;
        }
        return fd.value;
    }

    R2F2_LOG_ERR("file '%s' doesn't exist. O_CREAT=%s", path,
                 creat ? "y" : "n");
    return RET_ERR;
}

r2f2_ret r2f2_close(r2f2_fs_t *fs, r2f2_fd fd) {
    R2F2_FD_VALID_CHECK(fs, fd);

    r2f2_fsync(fs, fd);

    fs->fds[fd].active = false;
    fs->fds[fd].block_buffer.count = 0;
    R2F2_FREE(fs->fds[fd].block_buffer.data);
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
        R2F2_LOG_ERR(
            "invalid read of size %zu for file '%s' with size %zu, offset "
            "%zu, fd buffer size %zu",
            count, f->path, f->file_size, f->file_offset,
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
        RESULT(block_idx) b;
        if (f->meta.indir.block != 0) {
            b = find_data_block_for_off(fs, f->meta.indir.block,
                                        f->file_offset);
        } else if (f->meta.seq.last_block != 0) {
            b = find_data_block_for_off_direct(fs, f->meta.seq.last_block,
                                               f->file_offset);
        } else {
            b = RESULT_ERR(block_idx, RET_ERR);
        }

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

    /* do we even have a data block yet? */
    if (f->meta.data.last_block == 0) {
        RESULT(block_idx) data_block_idx = allocate_block(fs);
        if (data_block_idx.code != RET_OK) {
            return data_block_idx.code;
        }
        f->meta.data.last_block = data_block_idx.value;
    }

    size_t total_written = 0;
    while (total_written < f->block_buffer.count) {
        size_t last_data_block_cap =
            fs->cfg->geom.block_size - f->meta.data.last_block_fill;

        if (last_data_block_cap == 0) {
            RESULT(block_idx) b = allocate_block(fs);
            if (b.code != RET_OK) {
                return b.code;
            }

            f->meta.data.last_block = b.value;
            f->meta.data.last_block_fill = 0;
            last_data_block_cap = fs->cfg->geom.block_size;
        }

        size_t to_write = MIN(f->block_buffer.count, last_data_block_cap);

        R2F2_ASSERT(total_written + to_write, <=, fs->cfg->geom.block_size,
                    "%zu");
        r2f2_ret ret = fs->cfg->flash_write(
            fs,
            f->meta.data.last_block * fs->cfg->geom.block_size +
                f->meta.data.last_block_fill,
            to_write, f->block_buffer.data + total_written);
        if (ret != RET_OK) {
            R2F2_LOG_ERR("failed (%d) to write %zu B to flash in block %u", ret,
                         to_write, f->meta.data.last_block);
            return ret;
        }

        /* update file information in fd and "empty" its block buffer */
        f->meta.data.last_block_fill += to_write;
        f->file_size += to_write;
        f->block_buffer.count -= to_write;

        /* we've written our data, time for the necessary metadata */

        if (f->meta.seq.next_entry >= NUM_FILE_SEQ_ENTRIES) {
            if (f->meta.indir.block == 0) {
                /* we didn't have a file_indir_block yet, but now we need one */
                r2f2_ret ret = r2f2_migrate_file_to_indir_block(fs, fd);
                if (ret != RET_OK) {
                    return ret;
                }
            }

            RESULT(block_idx) seq_block_idx = allocate_block(fs);
            if (seq_block_idx.code != RET_OK) {
                return seq_block_idx.code;
            }

            RESULT(uint32_t) new_indir_entry_num =
                get_free_file_indir_entry(fs, f->meta.indir.block);
            if (new_indir_entry_num.code != RET_OK) {
                return new_indir_entry_num.code;
            }

            file_indir_entry_t fie;
            memset(&fie, 0xFF, sizeof(file_indir_entry_t));
            SET_FLASH_BLOCK_IDX(fie.seq_block[0], seq_block_idx.value);
            fie.f = mark_entry_used(fie.f);

            r2f2_ret ie_ret = write_file_indir_entry(
                fs, f->meta.indir.block, new_indir_entry_num.value, &fie);

            if (ie_ret != RET_OK) {
                return ie_ret;
            }
            fie.f = mark_entry_committed(fie.f);
            entry_flags_t new_f = fie.f;
            ie_ret = write_file_indir_entry_flags(
                fs, f->meta.indir.block, new_indir_entry_num.value, &new_f);
            if (ie_ret != RET_OK) {
                return ie_ret;
            }

            f->meta.seq.last_block = seq_block_idx.value;
            f->meta.seq.next_entry = 0;
        }

        file_seq_entry_t fse;
        memset(&fse, 0xFF, sizeof(file_seq_entry_t));

        SET_FLASH_BLOCK_IDX(fse.data_block, f->meta.data.last_block);
        SET_FLASH_U32(fse.data_block_fill_level, f->meta.data.last_block_fill);
        R2F2_ASSERT(f->file_size, >, 0, "%zu");
        SET_FLASH_U32(fse.data_block_offset_in_file,
                      fs->cfg->geom.block_size *
                          ((f->file_size - 1) / fs->cfg->geom.block_size));
        SET_FLASH_U32(fse.current_file_size, f->file_size);
        fse.f = mark_entry_used(fse.f);

        /* write the entry, then persist via flags */
        write_file_seq_entry(fs, f->meta.seq.last_block, f->meta.seq.next_entry,
                             &fse);

        fse.f = mark_entry_committed(fse.f);
        entry_flags_t new_f = fse.f;
        write_file_seq_entry_flags(fs, f->meta.seq.last_block,
                                   f->meta.seq.next_entry, &new_f);

        f->meta.seq.next_entry++;

        total_written += to_write;
    }

    return RET_OK;
}

r2f2_ret r2f2_remove(r2f2_fs_t *fs, const char *path) {
    if (!path) {
        return RET_EINVAL;
    }

    dir_meta_entry_t dme;
    struct dir_traversal_ret dir_ret;
    r2f2_ret ret = r2f2_get_file_dir_entry(fs, path, &dme, &dir_ret);
    if (ret != RET_OK) {
        return ret;
    }

    entry_flags_t new_f = mark_entry_reclaimable(dir_ret.dme_flags);
    ret = write_dir_meta_entry_flags(fs, dir_ret.dmb_idx, dir_ret.dme_idx,
                                     &new_f);
    return ret;
}
