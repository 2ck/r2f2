#include "r2f2_file.h"
#include "r2f2_alloc.h"
#include "r2f2_metadata.h"
#include "util/logger.h"
#include <string.h>

RESULT(block_idx) r2f2_find_dir_meta_block(r2f2_fs_t *fs, const char *path) {
    if (!path) {
        return RESULT_ERR(block_idx, RET_EINVAL);
    }

    /*
     * The path is of the form `/foo/bar/baz`, where the root dir block contains
     * the entry for the `bar` directory, and of course any other files that may
     * be in the root directory.
     */

    /* Therefore, we first have to sanity check that the path is valid */
    if (*path != '/') {
        R2F2_LOG_ERR("invalid path '%s'", path);
        return RESULT_ERR(block_idx, RET_EINVAL);
    }

    /* find number of directories in path, one for each '/' */
    const char *path_copy = path + 1;
    int32_t dir_depth = 0;
    while (*path_copy != '\0') {
        if (*path_copy == '/') {
            dir_depth++;

            /* we can't have two slashes after another */
            if (*(path_copy + 1) == '/') {
                R2F2_LOG_ERR("invalid path '%s'", path);
                return RESULT_ERR(block_idx, RET_EINVAL);
            }
        }

        path_copy++;
    }

    /*
     * we start in the root bock and also return it by default, in case our
     * depth is 0 so our file lies in the root directory
     */
    block_idx current_block = fs->root_dir_block;

    const char *seg_start = path + 1;
    const char *seg_end = seg_start;

    /*
     * Then we find the first directory and continue until there are no more
     * directories (until last `/`). If we are in the root directory, dir_depth
     * is = 0 and there is nothing to do here.
     */
    int32_t current_depth = dir_depth;
    while (current_depth-- > 0) {
        /* if we happen to iterate outside our valid path, we have hit a bug */
        R2F2_ASSERT(seg_end - path, <, MAX_PATH_LEN, "%ld");
        /* figure out this directory's path segment */
        while (*seg_end != '\0') {
            /* slash found */
            if (*seg_end == '/') {
                break;
            }

            seg_end++;
        }

        size_t seg_len = seg_end - seg_start;
        if (seg_len == 0 || seg_len >= MAX_PATH_LEN) {
            R2F2_LOG_ERR("path segment length %zu invalid", seg_len);
            return RESULT_ERR(block_idx, RET_EINVAL);
        }

        char segment[MAX_PATH_LEN];
        /* the path we persist to flash has 0 instead of 0xFF in unused space */
        memset(segment, 0, sizeof(segment));
        memcpy(segment, seg_start, seg_len);

        dir_meta_entry_t dme;
        memset(&dme, 0xFF, sizeof(dir_meta_entry_t));
        entry_flags_t flags;
        memset(&flags, 0xFF, sizeof(entry_flags_t));

        bool found_entry = false;
        for (size_t i = 0; i < NUM_DIR_META_ENTRIES; i++) {
            r2f2_ret flags_ret =
                read_dir_meta_entry_flags(fs, current_block, i, &flags);
            if (flags_ret != RET_OK) {
                return RESULT_ERR(block_idx, flags_ret);
            }
            /*
             * abort as soon as we find our first unused entry (no more valid
             * ones can come after)
             */
            if (is_entry_used(flags) == ENTRY_FLAG_UNSET) {
                break;
            }

            read_dir_meta_entry(fs, current_block, i, &dme);

#ifdef ECC_ON_METADATA
            r2f2_ret ecc_ret = correct_path(fs, dme.path, dme.path_ecc);
            if (ecc_ret != RET_OK) {
				R2F2_LOG_ERR("failed (%d) to correct path '%.*s'", ecc_ret, MAX_PATH_LEN, dme.path);
            }
#endif
            bool match = memcmp(dme.path, segment, MAX_PATH_LEN) == 0;
            if (match) {
                flash_block_idx next[NUM_NEXT_PTRS];
                memcpy(next, dme.next_block, sizeof(next));
                RESULT(block_idx) next_block = get_valid_next_block(fs, next);
                if (next_block.code != RET_OK) {
                    return next_block;
                }
                current_block = next_block.value;
                found_entry = true;
                break;
            }
        }

        if (!found_entry) {
            R2F2_LOG_ERR("could not find matching dir_meta_entry for path '%s' "
                         "segment '%s'",
                         path, segment);
            return RESULT_ERR(block_idx, RET_DIR_NOT_FOUND);
        }

        seg_end++;
        seg_start = seg_end;
    }
    return RESULT_OK(block_idx, current_block);
}

RESULT(block_idx) r2f2_find_last_dir_meta_block(r2f2_fs_t *fs,
                                                const char *path) {
    RESULT(block_idx) dir_meta_block_idx = r2f2_find_dir_meta_block(fs, path);
    if (dir_meta_block_idx.code != RET_OK) {
        return dir_meta_block_idx;
    }

    /*
     * iterate through the next block pointers until we find one with free
     * entries
     */
    block_idx dmb = dir_meta_block_idx.value;
    bool has_next_block = true;
    do {
        flash_block_idx next[NUM_NEXT_PTRS];

        r2f2_ret ret = fs->cfg->flash_read(fs,
                                           dmb * fs->cfg->geom.block_size +
                                               offsetof(dir_meta_block_t, next),
                                           sizeof(next), next);
        if (ret != RET_OK) {
            return RESULT_ERR(block_idx, ret);
        }

        RESULT(block_idx) next_block = get_valid_next_block(fs, next);
        if (next_block.code == RET_OK) {
            dmb = next_block.value;
        } else {
            has_next_block = false;
        }
    } while (has_next_block);

    return RESULT_OK(block_idx, dmb);
}

RESULT(block_idx) r2f2_create_next_dir_meta_block(r2f2_fs_t *fs,
                                                  block_idx dmb) {
    flash_block_idx next[NUM_NEXT_PTRS];

    r2f2_ret ret = fs->cfg->flash_read(
        fs, dmb * fs->cfg->geom.block_size + offsetof(dir_meta_block_t, next),
        sizeof(next), next);
    if (ret != RET_OK) {
        return RESULT_ERR(block_idx, ret);
    }

    /* verify that there is no entry yet */
    RESULT(block_idx) next_block = get_valid_next_block(fs, next);
    if (next_block.code == RET_OK) {
        R2F2_LOG_ERR("asked to create next block for dir_meta_block %u but it "
                     "already exists (%u)",
                     dmb, next_block.value);
        return RESULT_ERR(block_idx, RET_EINVAL);
    }

    RESULT(block_idx) new_block = allocate_block(fs);
    if (new_block.code != RET_OK) {
        return new_block;
    }
    ret = write_block_header(fs, new_block.value, BLOCK_TYPE_DIR);
    if (ret != RET_OK) {
        return RESULT_ERR(block_idx, ret);
    }

    SET_FLASH_BLOCK_IDX(next[0], new_block.value);

    ret = fs->cfg->flash_write(
        fs, dmb * fs->cfg->geom.block_size + offsetof(dir_meta_block_t, next),
        sizeof(next), next);
    if (ret != RET_OK) {
        return RESULT_ERR(block_idx, ret);
    }

    return new_block;
}

RESULT(block_idx) r2f2_create_next_file_indir_block(r2f2_fs_t *fs,
                                                    block_idx indir_block) {
    flash_block_idx next[NUM_NEXT_PTRS];

    r2f2_ret ret =
        fs->cfg->flash_read(fs,
                            indir_block * fs->cfg->geom.block_size +
                                offsetof(file_indir_block_t, next_block),
                            sizeof(next), next);
    if (ret != RET_OK) {
        return RESULT_ERR(block_idx, ret);
    }

    /* verify that there is no entry yet */
    RESULT(block_idx) next_block = get_valid_next_block(fs, next);
    if (next_block.code == RET_OK) {
        R2F2_LOG_ERR(
            "asked to create next block for file_indir_block %u but it "
            "already exists (%u)",
            indir_block, next_block.value);
        return RESULT_ERR(block_idx, RET_EINVAL);
    }

    RESULT(block_idx) new_block = allocate_block(fs);
    if (new_block.code != RET_OK) {
        return new_block;
    }
    ret = write_block_header(fs, new_block.value, BLOCK_TYPE_INDIR);
    if (ret != RET_OK) {
        return RESULT_ERR(block_idx, ret);
    }

    SET_FLASH_BLOCK_IDX(next[0], new_block.value);

    ret = fs->cfg->flash_write(fs,
                               indir_block * fs->cfg->geom.block_size +
                                   offsetof(file_indir_block_t, next_block),
                               sizeof(next), next);
    if (ret != RET_OK) {
        return RESULT_ERR(block_idx, ret);
    }

    return new_block;
}

r2f2_ret r2f2_migrate_file_to_indir_block(r2f2_fs_t *fs, r2f2_fd fd) {
    R2F2_FD_VALID_CHECK(fs, fd);

    fildes_t *f = &fs->fds[fd];

    RESULT(block_idx) indir_block_idx = allocate_block(fs);
    RETURN_ON_ERR(indir_block_idx.code);
    r2f2_ret ret =
        write_block_header(fs, indir_block_idx.value, BLOCK_TYPE_INDIR);
    RETURN_ON_ERR(ret);

    /* create initial indir_entry in our new indir_block */

    entry_flags_t fie_flags;
    memset(&fie_flags, 0xFF, sizeof(entry_flags_t));
    fie_flags = mark_entry_used(fie_flags);
    ret =
        write_file_indir_entry_flags(fs, indir_block_idx.value, 0, &fie_flags);
    RETURN_ON_ERR(ret);

    file_indir_entry_t fie;
    memset(&fie, 0xFF, sizeof(file_indir_entry_t));
    SET_FLASH_BLOCK_IDX(fie.seq_block[0], f->meta.seq.last_block);
    ret = write_file_indir_entry(fs, indir_block_idx.value, 0, &fie);
    RETURN_ON_ERR(ret);

    fie_flags = mark_entry_committed(fie_flags);
    ret =
        write_file_indir_entry_flags(fs, indir_block_idx.value, 0, &fie_flags);
    RETURN_ON_ERR(ret);

    /* update our dir_meta_entry */

    dir_meta_entry_t dme;
    ret = read_dir_meta_entry(fs, f->meta.dir.block, f->meta.dir.entry, &dme);
    RETURN_ON_ERR(ret);

    uint32_t free_next_ptr = NUM_NEXT_PTRS;
    for (size_t i = 0; i < NUM_NEXT_PTRS; i++) {
        RESULT(block_idx) n = GET_FLASH_BLOCK_IDX(dme.next_block[i]);
        RETURN_ON_ERR(n.code);
        if (n.value >= fs->cfg->geom.num_blocks) {
            free_next_ptr = i;
            break;
        }
    }
    if (free_next_ptr == NUM_NEXT_PTRS) {
        R2F2_LOG_ERR("unhandled (TODO), out of next_block ptrs, "
                     "need new dir_entry for file '%s'",
                     f->path);
        return RET_ERR;
    }
    SET_FLASH_BLOCK_IDX(dme.next_block[free_next_ptr], indir_block_idx.value);

    entry_flags_t dme_flags;
    memset(&dme_flags, 0xFF, sizeof(entry_flags_t));
    ret = read_dir_meta_entry_flags(fs, f->meta.dir.block, f->meta.dir.entry,
                                    &dme_flags);
    RETURN_ON_ERR(ret);

    ret = write_dir_meta_entry(fs, f->meta.dir.block, f->meta.dir.entry, &dme);
    RETURN_ON_ERR(ret);

    f->meta.indir.block = indir_block_idx.value;

    return RET_OK;
}

RESULT(r2f2_fd) r2f2_register_file(r2f2_fs_t *fs, const char *path) {
    RESULT(block_idx) dir_meta_block_idx =
        r2f2_find_last_dir_meta_block(fs, path);
    if (dir_meta_block_idx.code != RET_OK) {
        R2F2_LOG_ERR("dir traversal failed (%d) for path '%s'",
                     dir_meta_block_idx.code, path);
        return RESULT_ERR(r2f2_fd, dir_meta_block_idx.code);
    }

    RESULT(uint32_t) dme_num =
        get_free_dir_meta_entry(fs, dir_meta_block_idx.value);
    if (dme_num.code == RET_NOMEM) {
        RESULT(block_idx) new_dir_meta_block =
            r2f2_create_next_dir_meta_block(fs, dir_meta_block_idx.value);
        if (new_dir_meta_block.code != RET_OK) {
            return RESULT_ERR(r2f2_fd, new_dir_meta_block.code);
        }
        dir_meta_block_idx = new_dir_meta_block;
        dme_num.value = 0;
    } else if (dme_num.code != RET_OK) {
        R2F2_LOG_WARN("failed (%d) to get dir_entry in block %u", dme_num.code,
                      dir_meta_block_idx.value);
        return RESULT_ERR(r2f2_fd, dme_num.code);
    }

    /*
     * We allocate a file_seq_block, but no data_block. Then we let our
     * dir_meta_entry directly point to the file_seq_block. A file_indir_block
     * is only inserted inbetween at a later point, when the file_seq_block is
     * full.
     *
     * ┌─────────┐
     * │dir_block│
     * │entry ─┐ │
     * └───────┼─┘
     *         │
     * ┌───────▼──────┐
     * │file_seq_block│
     * └──────────────┘
     */

    RESULT(block_idx) file_seq_block_idx = allocate_block(fs);
    if (file_seq_block_idx.code != RET_OK) {
        return RESULT_ERR(r2f2_fd, file_seq_block_idx.code);
    }
    r2f2_ret ret =
        write_block_header(fs, file_seq_block_idx.value, BLOCK_TYPE_SEQ);
    if (ret != RET_OK) {
        return RESULT_ERR(r2f2_fd, ret);
    }

    dir_meta_entry_t dme;
    memset(&dme, 0xFF, sizeof(dir_meta_entry_t));

    ret = set_path_to_basename_zeroed(dme.path, path);
    if (ret != RET_OK) {
        return RESULT_ERR(r2f2_fd, ret);
    }
#ifdef ECC_ON_METADATA
    memset(dme.path_ecc, 0, ECC_BCH_PATH_ECCLEN);
    encode_bch(fs->path_bch, (uint8_t *)dme.path, MAX_PATH_LEN, dme.path_ecc);
    bch_counts[BCHC_PATH_IDX].encodes++;
#endif

    memset(dme.next_block, 0xFF, sizeof(dme.next_block));
    SET_FLASH_BLOCK_IDX(dme.next_block[0], file_seq_block_idx.value);

    entry_flags_t flags;
    memset(&flags, 0xFF, sizeof(entry_flags_t));
    flags = mark_entry_used(flags);
    ret = write_dir_meta_entry_flags(fs, dir_meta_block_idx.value,
                                     dme_num.value, &flags);
    if (ret != RET_OK) {
        return RESULT_ERR(r2f2_fd, ret);
    }

    ret =
        write_dir_meta_entry(fs, dir_meta_block_idx.value, dme_num.value, &dme);
    if (ret != RET_OK) {
        return RESULT_ERR(r2f2_fd, ret);
    }

    flags = mark_entry_committed(flags);
    ret = write_dir_meta_entry_flags(fs, dir_meta_block_idx.value,
                                     dme_num.value, &flags);
    if (ret != RET_OK) {
        return RESULT_ERR(r2f2_fd, ret);
    }

    RESULT(r2f2_fd) fd = r2f2_create_fd(fs, path);
    if (fd.code != RET_OK) {
        R2F2_LOG_ERR("failed (%d) to create fd for path '%s'", fd.code, path);
        return fd;
    }
    fildes_t *f = &fs->fds[fd.value];
    f->file_offset = 0;
    f->file_size = 0;
    f->meta.dir.block = dir_meta_block_idx.value;
    f->meta.dir.entry = dme_num.value;
    /* no indir block so far */
    f->meta.indir.block = 0;
    f->meta.seq.last_block = file_seq_block_idx.value;
    f->meta.seq.next_entry = 0;
    /* no data block so far */
    f->meta.data.last_block = 0;
    f->meta.data.last_block_fill = 0;
    f->meta.data.last_block_offset_in_file = 0;

    return fd;
}

RESULT(r2f2_fd) r2f2_create_fd(r2f2_fs_t *fs, const char *path) {
    for (size_t i = 0; i < MAX_NUM_FDS; i++) {
        if (fs->fds[i].active) {
            continue;
        }
        fs->fds[i].active = true;
        size_t len = strlen(path);
        if (len >= MAX_PATH_LEN) {
            return RESULT_ERR(r2f2_fd, RET_EINVAL);
        }
        memset(fs->fds[i].path, 0, MAX_PATH_LEN);
        memcpy(fs->fds[i].path, path, len);

#ifdef ECC_ON_DATA
        void *mem = R2F2_MALLOC(fs->cfg->geom.block_size -
                                ECC_BCH_DATA_RESV_PG * fs->cfg->geom.page_size);
#else
        void *mem = R2F2_MALLOC(fs->cfg->geom.block_size);
#endif
        R2F2_ASSERT(mem, !=, NULL, "%p");

        fs->fds[i].block_buffer.data = mem;
        fs->fds[i].block_buffer.count = 0;
        return RESULT_OK(r2f2_fd, i);
    }
    return RESULT_ERR(r2f2_fd, RET_NOMEM);
}

r2f2_ret r2f2_fd_valid(r2f2_fs_t *fs, r2f2_fd fd) {
    if (fd < 0 || fd >= MAX_NUM_FDS) {
        R2F2_LOG_ERR("Out-of-bounds fd %d", fd);
        return RET_OOB;
    }
    if (fs->fds[fd].active != true) {
        R2F2_LOG_ERR("file descriptor %d did not exist", fd);
        return RET_ERR;
    }
    return RET_OK;
}
