#include "r2f2.h"
#include "r2f2_alloc.h"
#include "r2f2_file.h"
#include "r2f2_metadata.h"
#include "util/logger.h"
#include <string.h>

r2f2_ret r2f2_format(r2f2_fs_t *fs) {
    r2f2_fs_info_t fs_info;
    memset(&fs_info, 0xFF, sizeof(r2f2_fs_info_t));

    /* TODO: optionally replace with chip erase because that's much faster */
    for (block_idx b = 0; b < fs->cfg->geom.num_blocks; b++) {
        fs->cfg->flash_erase(fs, b * fs->cfg->geom.block_size,
                             fs->cfg->geom.block_size);
    }

    prepare_block_allocator(fs);

    SET_FLASH_U32(fs_info.global_metadata.magic, R2F2_MAGIC);
    RESULT(block_idx) b = allocate_block(fs);
    if (b.code != RET_OK) {
        return b.code;
    }

    r2f2_ret ret = write_block_header(fs, b.value, BLOCK_TYPE_DIR);
    RETURN_ON_ERR(ret);
    /* TODO: remove this and just search for it in block 1/2 or something */
    /* alternatively, make this have several next_block-pointers */

    SET_FLASH_BLOCK_IDX(fs_info.root_dir_block, b.value);
    SET_FLASH_BLOCK_IDX(fs_info.root_dir_block_copy, b.value);
    fs->root_dir_block = b.value;

    ret = fs->cfg->flash_write(fs,
                               R2F2_SUPERBLOCK_IDX * fs->cfg->geom.block_size +
                                   offsetof(r2f2_superblock_t, fs_info),
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
    fs->u32_bch = init_bch(ECC_BCH_U32_M, ECC_BCH_U32_T, 0);
    R2F2_ASSERT((void *)fs->u32_bch, !=, NULL, "%p");
    R2F2_ASSERT(fs->u32_bch->ecc_bytes, ==, ECC_BCH_U32_ECCLEN, "%u");
    fs->path_bch = init_bch(ECC_BCH_PATH_M, ECC_BCH_PATH_T, 0);
    R2F2_ASSERT((void *)fs->path_bch, !=, NULL, "%p");
    R2F2_ASSERT(fs->path_bch->ecc_bytes, ==, ECC_BCH_PATH_ECCLEN, "%u");
#endif

#ifdef ECC_ON_DATA
    fs->data_bch = init_bch(ECC_BCH_DATA_M, ECC_BCH_DATA_T, 0);
    R2F2_ASSERT((void *)fs->data_bch, !=, NULL, "%p");
    R2F2_ASSERT(fs->data_bch->ecc_bytes, ==, ECC_BCH_DATA_ECCLEN, "%u");
    /* data is stored in n-1 page-sized chunks, with parity in the last chunk */
    size_t pages_per_block = fs->cfg->geom.block_size / fs->cfg->geom.page_size;
    R2F2_ASSERT((pages_per_block - ECC_BCH_DATA_RESV_PG) * ECC_BCH_DATA_ECCLEN,
                <=, ECC_BCH_DATA_RESV_PG * fs->cfg->geom.page_size, "%zu");
#endif

#ifdef BCH_COUNTERS
    reset_bch_counts();
#endif

    r2f2_fs_info_t fs_info;
    /* read root block to see if there is logfs on flash */
    fs->cfg->flash_read(fs,
                        R2F2_SUPERBLOCK_IDX * fs->cfg->geom.block_size +
                            offsetof(r2f2_superblock_t, fs_info),
                        sizeof(r2f2_fs_info_t), &fs_info);

    /* TODO: check if base block contents match cfg */
    if (!is_fs_valid(fs, &fs_info)) {
        /* we need to format */
        r2f2_format(fs);
    } else {
        RESULT(block_idx) b = GET_FLASH_BLOCK_IDX(fs_info.root_dir_block);
        if (b.code != RET_OK) {
            b = GET_FLASH_BLOCK_IDX(fs_info.root_dir_block_copy);
            RETURN_ON_ERR(b.code);
        }
        fs->root_dir_block = b.value;
        r2f2_ret ret = restore_block_allocator(fs);
        RETURN_ON_ERR(ret);
    }

    /* we are mounted */
    /* TODO: set up buffers/caches or something, idk */
    for (size_t i = 0; i < MAX_NUM_FDS; i++) {
        fs->fds[i].active = false;
    }
    return RET_OK;
}

r2f2_ret r2f2_unmount(r2f2_fs_t *fs) {
    r2f2_ret any_ret = RET_OK;
    for (size_t i = 0; i < MAX_NUM_FDS; i++) {
        fildes_t *fd = &fs->fds[i];
        if (fd->active) {
            r2f2_ret ret = r2f2_close(fs, i);
            /*
             * Don't return on failed attempts, try to at least close the other
             * fds, but return the (first) error at the end.
             */
            if (ret != RET_OK && any_ret == RET_OK) {
                any_ret = ret;
            }
        }
    }

#ifdef ECC_ON_METADATA
    free_bch(fs->u32_bch);
    free_bch(fs->path_bch);
#endif

#ifdef ECC_ON_DATA
    free_bch(fs->data_bch);
#endif

    return any_ret;
}

r2f2_fd r2f2_open(r2f2_fs_t *fs, const char *path, int oflag) {
    bool creat = oflag & O_CREAT;

    if (!path) {
        return RET_ERR;
    }

    dir_meta_entry_t dme;
    struct dir_traversal_ret dir_ret;
    r2f2_ret ret = r2f2_get_dir_entry(fs, path, &dme, &dir_ret);

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
         * via a file_indir_block, and the next block's header tells us.
         */
        RESULT(uint32_t) block_type = get_block_type(fs, next_block.value);
        RETURN_ON_ERR(block_type.code);
        if (block_type.value == BLOCK_TYPE_INDIR) {
            indir_block_idx = next_block.value;
            /* maybe our indir_block has next_block entries */
            RESULT(block_idx) last_indir_block =
                find_last_indir_block_in_chain(fs, next_block.value);
            RETURN_ON_ERR(last_indir_block.code);

            RESULT(uint32_t) last_fie =
                get_last_file_indir_entry(fs, last_indir_block.value);
            RETURN_ON_ERR(last_fie.code);

            file_indir_entry_t fie;
            r2f2_ret ret = read_file_indir_entry(fs, last_indir_block.value,
                                                 last_fie.value, &fie);
            RETURN_ON_ERR(ret);

            flash_block_idx next[NUM_NEXT_PTRS];
            memcpy(next, fie.seq_block, sizeof(next));
            RESULT(block_idx) seq_block = get_valid_next_block(fs, next);
            RETURN_ON_ERR(seq_block.code);
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
            f->meta.data.last_block_offset_in_file = 0;
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
        RETURN_ON_ERR(file_size.code);
        f->file_size = file_size.value;

        f->meta.dir.block = dir_ret.dmb_idx;
        f->meta.dir.entry = dir_ret.dme_idx;
        f->meta.indir.block = indir_block_idx;
        f->meta.seq.last_block = seq_block_idx;
        f->meta.seq.next_entry = last_fse.value + 1;
        RESULT(block_idx) last_data_block = GET_FLASH_BLOCK_IDX(fse.data_block);
        RETURN_ON_ERR(last_data_block.code);
        f->meta.data.last_block = last_data_block.value;
        RESULT(uint32_t) last_fill = GET_FLASH_U32(fse.data_block_fill_level);
        RETURN_ON_ERR(last_fill.code);
        f->meta.data.last_block_fill = last_fill.value;
        RESULT(uint32_t) last_offset =
            GET_FLASH_U32(fse.data_block_offset_in_file);
        RETURN_ON_ERR(last_offset.code);
        f->meta.data.last_block_offset_in_file = last_offset.value;

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

    r2f2_ret ret = r2f2_fsync(fs, fd);
    RETURN_ON_ERR(ret);

    fs->fds[fd].active = false;
    fs->fds[fd].block_buffer.count = 0;
    R2F2_FREE(fs->fds[fd].block_buffer.data);
    fs->fds[fd].block_buffer.data = NULL;
    return RET_OK;
}

r2f2_ret r2f2_mkdir(r2f2_fs_t *fs, const char *path) {
    if (!path) {
        return RET_ERR;
    }

    size_t path_len = strlen(path);
    if (path_len >= MAX_PATH_LEN) {
        R2F2_LOG_ERR("path '%s' too long", path);
        return RET_ERR;
    }

    /* trim trailing slash(es) in path */
    char path_copy[MAX_PATH_LEN];
    memset(path_copy, 0, MAX_PATH_LEN);
    memcpy(path_copy, path, path_len);
    char *end = path_copy + path_len;

    while (end > path_copy && end[-1] == '/') {
        *--end = '\0';
    }

    dir_meta_entry_t dme;
    struct dir_traversal_ret dir_ret;
    r2f2_ret ret = r2f2_get_dir_entry(fs, path_copy, &dme, &dir_ret);

    if (ret == RET_NOT_FOUND) {
        RESULT(block_idx) dir_meta_block_idx =
            r2f2_find_last_dir_meta_block(fs, path_copy);
        RETURN_ON_ERR(dir_meta_block_idx.code);

        RESULT(uint32_t) dme_num =
            get_free_dir_meta_entry(fs, dir_meta_block_idx.value);
        if (dme_num.code == RET_NOMEM) {
            RESULT(block_idx) new_dir_meta_block =
                r2f2_create_next_dir_meta_block(fs, dir_meta_block_idx.value);
            RETURN_ON_ERR(new_dir_meta_block.code);
            dir_meta_block_idx = new_dir_meta_block;
            dme_num.value = 0;
        } else if (dme_num.code != RET_OK) {
            return dme_num.code;
        }

        dir_meta_entry_t dme;
        memset(&dme, 0xFF, sizeof(dir_meta_entry_t));

        r2f2_ret ret = set_path_to_basename_zeroed(dme.path, path_copy);
        RETURN_ON_ERR(ret);
#ifdef ECC_ON_METADATA
        memset(dme.path_ecc, 0, ECC_BCH_PATH_ECCLEN);
        encode_bch(fs->path_bch, (uint8_t *)dme.path, MAX_PATH_LEN,
                   dme.path_ecc);
        bch_counts[BCHC_PATH_IDX].encodes++;
#endif

        memset(dme.next_block, 0xFF, sizeof(dme.next_block));
        RESULT(block_idx) next_block = allocate_block(fs);
        RETURN_ON_ERR(next_block.code);
        ret = write_block_header(fs, next_block.value, BLOCK_TYPE_DIR);
        RETURN_ON_ERR(ret);
        SET_FLASH_BLOCK_IDX(dme.next_block[0], next_block.value);

        entry_flags_t flags;
        memset(&flags, 0xFF, sizeof(entry_flags_t));
        flags = mark_entry_used(flags);
        ret = write_dir_meta_entry_flags(fs, dir_meta_block_idx.value,
                                         dme_num.value, &flags);
        RETURN_ON_ERR(ret);

        ret = write_dir_meta_entry(fs, dir_meta_block_idx.value, dme_num.value,
                                   &dme);
        RETURN_ON_ERR(ret);

        flags = mark_entry_committed(flags);
        ret = write_dir_meta_entry_flags(fs, dir_meta_block_idx.value,
                                         dme_num.value, &flags);
        RETURN_ON_ERR(ret);
        return RET_OK;
    } else if (ret == RET_OK) {
        return RET_EXIST;
    }

    return RET_ERR;
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
        new_offset = f->file_size + f->block_buffer.count + offset;
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
        struct db_ret db_ret;
        if (f->meta.indir.block != 0) {
            r2f2_ret ret = find_data_block_for_off(fs, f->meta.indir.block,
                                                   f->file_offset, &db_ret);
            RETURN_ON_ERR(ret);
        } else if (f->meta.seq.last_block != 0) {
            r2f2_ret ret = find_data_block_for_off_direct(
                fs, f->meta.seq.last_block, f->file_offset, &db_ret);
            RETURN_ON_ERR(ret);
        } else {
            return RET_ERR;
        }

        r2f2_ret ret = r2f2_read_data(
            fs, db_ret.data_block_idx,
            f->file_offset - db_ret.data_block_offset_in_file, count, buf);
        RETURN_ON_ERR(ret);

        return count;
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

    /*
     * Is there space in our fd's block buffer?
     * If so, we write what we can into our block buffer. If it's full before
     * we're done, we need to flush/fsync, and write the rest into the block
     * buffer afterwards.
     */

#ifdef ECC_ON_DATA
    size_t data_block_capacity = fs->cfg->geom.block_size -
                                 ECC_BCH_DATA_RESV_PG * fs->cfg->geom.page_size;
#else
    size_t data_block_capacity = fs->cfg->geom.block_size;
#endif

    size_t total_written = 0;

    while (total_written < count) {
        if (f->block_buffer.count == data_block_capacity) {
            r2f2_ret ret = r2f2_fsync(fs, fd);
            if (ret != RET_OK) {
                return ret;
            }

            /* fsync has reset our block_buffer count to 0 */
            R2F2_ASSERT(f->block_buffer.count, ==, 0, "%zu");
        }

        size_t remaining_in_fd_buf =
            data_block_capacity - f->block_buffer.count;
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
}

r2f2_ret r2f2_fsync(r2f2_fs_t *fs, r2f2_fd fd) {
    R2F2_FD_VALID_CHECK(fs, fd);

    fildes_t *f = &fs->fds[fd];
    if (f->block_buffer.count == 0) {
        return RET_OK;
    }

    /* do we even have a data block yet? */
    if (f->meta.data.last_block == 0) {
        /* TODO: can early abort or crash permanently lose a block? */
        RESULT(block_idx) data_block_idx = allocate_block(fs);
        RETURN_ON_ERR(data_block_idx.code);
        f->meta.data.last_block = data_block_idx.value;
    }

    size_t total_written = 0;
    /* Store the value because we modify the block buffer count in the loop */
    size_t total_to_write = f->block_buffer.count;
    while (total_written < total_to_write) {
#ifdef ECC_ON_DATA
        size_t data_block_capacity =
            fs->cfg->geom.block_size -
            ECC_BCH_DATA_RESV_PG * fs->cfg->geom.page_size;
#else
        size_t data_block_capacity = fs->cfg->geom.block_size;
#endif
        size_t last_data_block_capacity =
            data_block_capacity - f->meta.data.last_block_fill;

        if (last_data_block_capacity == 0) {
            RESULT(block_idx) b = allocate_block(fs);
            if (b.code != RET_OK) {
                return b.code;
            }

            f->meta.data.last_block = b.value;
            f->meta.data.last_block_fill = 0;
            f->meta.data.last_block_offset_in_file = f->file_size;
            last_data_block_capacity = data_block_capacity;
        }

        size_t to_write = MIN(f->block_buffer.count, last_data_block_capacity);

        R2F2_ASSERT(total_written + to_write, <=, data_block_capacity, "%zu");
        r2f2_ret ret = r2f2_write_data(fs, f->meta.data.last_block,
                                       f->meta.data.last_block_fill, to_write,
                                       f->block_buffer.data + total_written);
        if (ret != RET_OK) {
            R2F2_LOG_ERR("failed (%d) to write %zu B to flash in block %u", ret,
                         to_write, f->meta.data.last_block);
            return ret;
        }

        /* update file information in fd and "empty" its block buffer */
#ifdef ECC_ON_DATA
        /* page-alignment in case to_write < page_size */
        size_t physical_written = ((to_write + fs->cfg->geom.page_size - 1) /
                                   fs->cfg->geom.page_size) *
                                  fs->cfg->geom.page_size;
        f->meta.data.last_block_fill += physical_written;
#else
        f->meta.data.last_block_fill += to_write;
#endif
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
            ret = write_block_header(fs, seq_block_idx.value, BLOCK_TYPE_SEQ);
            RETURN_ON_ERR(ret);

            RESULT(uint32_t) new_indir_entry_num =
                get_free_file_indir_entry(fs, f->meta.indir.block);
            if (new_indir_entry_num.code == RET_NOMEM) {
                RESULT(block_idx) new_file_indir_block =
                    r2f2_create_next_file_indir_block(fs, f->meta.indir.block);
                RETURN_ON_ERR(new_file_indir_block.code);
                f->meta.indir.block = new_file_indir_block.value;
                new_indir_entry_num.value = 0;
            } else if (new_indir_entry_num.code != RET_OK) {
                return new_indir_entry_num.code;
            }

            entry_flags_t fie_flags;
            memset(&fie_flags, 0xFF, sizeof(entry_flags_t));
            ret = read_file_indir_entry_flags(
                fs, f->meta.indir.block, new_indir_entry_num.value, &fie_flags);
            RETURN_ON_ERR(ret);
            fie_flags = mark_entry_used(fie_flags);
            ret = write_file_indir_entry_flags(
                fs, f->meta.indir.block, new_indir_entry_num.value, &fie_flags);
            RETURN_ON_ERR(ret);

            file_indir_entry_t fie;
            memset(&fie, 0xFF, sizeof(file_indir_entry_t));
            SET_FLASH_BLOCK_IDX(fie.seq_block[0], seq_block_idx.value);
            ret = write_file_indir_entry(fs, f->meta.indir.block,
                                         new_indir_entry_num.value, &fie);
            RETURN_ON_ERR(ret);

            fie_flags = mark_entry_committed(fie_flags);
            ret = write_file_indir_entry_flags(
                fs, f->meta.indir.block, new_indir_entry_num.value, &fie_flags);
            RETURN_ON_ERR(ret);

            f->meta.seq.last_block = seq_block_idx.value;
            f->meta.seq.next_entry = 0;
        }

        entry_flags_t fse_flags;
        memset(&fse_flags, 0xFF, sizeof(entry_flags_t));
        fse_flags = mark_entry_used(fse_flags);
        ret = write_file_seq_entry_flags(fs, f->meta.seq.last_block,
                                         f->meta.seq.next_entry, &fse_flags);
        RETURN_ON_ERR(ret);

        file_seq_entry_t fse;
        memset(&fse, 0xFF, sizeof(file_seq_entry_t));
        SET_FLASH_BLOCK_IDX(fse.data_block, f->meta.data.last_block);
        SET_FLASH_U32(fse.data_block_fill_level, f->meta.data.last_block_fill);
        R2F2_ASSERT(f->file_size, >, 0, "%zu");
        SET_FLASH_U32(fse.data_block_offset_in_file,
                      f->meta.data.last_block_offset_in_file);
        SET_FLASH_U32(fse.current_file_size, f->file_size);

        /* write the entry, then persist via flags */
        write_file_seq_entry(fs, f->meta.seq.last_block, f->meta.seq.next_entry,
                             &fse);

        fse_flags = mark_entry_committed(fse_flags);
        ret = write_file_seq_entry_flags(fs, f->meta.seq.last_block,
                                         f->meta.seq.next_entry, &fse_flags);
        RETURN_ON_ERR(ret);

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
    r2f2_ret ret = r2f2_get_dir_entry(fs, path, &dme, &dir_ret);
    if (ret != RET_OK) {
        return ret;
    }

    entry_flags_t new_f = mark_entry_reclaimable(dir_ret.dme_flags);
    ret = write_dir_meta_entry_flags(fs, dir_ret.dmb_idx, dir_ret.dme_idx,
                                     &new_f);
    return ret;
}
