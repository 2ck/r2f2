#include "r2f2_gc.h"
#include "r2f2.h"
#include "r2f2_alloc.h"
#include "r2f2_metadata.h"
#include "util/logger.h"
#include <string.h>

RESULT(uint32_t) r2f2_gc_entry(r2f2_fs_t *fs, size_t target) {
    uint32_t freed =
        r2f2_reclaim_dir_block(fs, fs->root_dir_block, 0, 0, target);

    /* TODO: what if >0 but <target ? */
    if (freed > 0) {
        return RESULT_OK(uint32_t, freed);
    }
    return RESULT_ERR(uint32_t, RET_NOMEM);
}

uint32_t r2f2_reclaim_dir_block(r2f2_fs_t *fs, block_idx dir_block,
                                block_idx parent_dir_block,
                                uint32_t parent_dir_entry, size_t target) {
    uint32_t freed = 0;
    dir_meta_entry_t dme;
    entry_flags_t flags;
    flash_block_idx next[NUM_NEXT_PTRS];
    /* false as soon as there is an entry we can't get rid of */
    bool this_dir_block_reclaimable = true;

    /*
     * There are multiple options for each entry:
     *  1. entry marked reclaimable
     *   a) entry is directory -> fail if non-empty, otherwise recover directory
     *      block and "pull down" entry to 0
     *   b) entry is file -> recover file blocks, "pull down" entry to 0
     *
     *  2. entry not marked reclaimable
     *   a) entry is directory -> recurse down to entry's next_block
     *   b) entry is file -> skip
     *
     *  3. entry is all zero -> has been deleted previously, continue with the
     *     next one
     *
     * If all entries in a block have been "pulled down" to 0, the block itself
     * is no longer usable. Thus, we need to replace the parent entry pointing
     * to it with a fresh one (or the dir_block's next ptr if it exists. see
     * migrate_dir_block_in_entry). Finally, we can recover our old dir block.
     */
    for (int d = NUM_DIR_META_ENTRIES - 1; d >= 0; d--) {
        read_dir_meta_entry(fs, dir_block, d, &dme);
        /* 3. -- skip entries that have been deleted previously */
        if (is_all_zero(&dme, sizeof(dir_meta_entry_t))) {
            continue;
        }

        r2f2_ret ret = read_dir_meta_entry_flags(fs, dir_block, d, &flags);
        if (ret != RET_OK) {
            R2F2_LOG_ERR("error (%d) reading dir_block %u entry %u flags", ret,
                         dir_block, d);
            this_dir_block_reclaimable = false;
            continue;
        }

        memcpy(next, dme.next_block, sizeof(next));
        RESULT(block_idx) next_block = get_valid_next_block(fs, next);
        if (next_block.code != RET_OK) {
            this_dir_block_reclaimable = false;
            continue; // TODO?
        }
        RESULT(uint32_t) block_type = get_block_type(fs, next_block.value);
        if (block_type.code != RET_OK) {
            this_dir_block_reclaimable = false;
            continue; // TODO?
        }

        if (is_entry_reclaimable(flags) == ENTRY_FLAG_SET) {
            if (block_type.value == BLOCK_TYPE_DIR) {
                /* 1.a) -- is our next_block another dir block? */
                R2F2_LOG_WARN("TODO: handle directory reclaim!");
                /* as long as this path is a TODO */
                this_dir_block_reclaimable = false;
            } else {
                /* 1.b) -- free an entire file's metadata tree */
                freed += r2f2_reclaim_file_blocks(fs, &dme);

                /* overwrite dir entry for the deleted file to 0 */
                memset(&dme, 0, sizeof(dir_meta_entry_t));
                r2f2_ret ret = write_dir_meta_entry(fs, dir_block, d, &dme);
                if (ret != RET_OK) {
                    R2F2_LOG_ERR("failed (%d) to overwrite entry %d in "
                                 "dir_block %u",
                                 ret, d, dir_block);
                }

                if (freed >= target) {
                    return freed;
                }
            }
        } else {
            if (block_type.value == BLOCK_TYPE_DIR) {
                R2F2_ASSERT(target, >, freed, "%zu");
                freed += r2f2_reclaim_dir_block(fs, next_block.value, dir_block,
                                                d, target - freed);
                if (freed >= target) {
                    return freed;
                }
            } else {
                this_dir_block_reclaimable = false;
            }
        }
    }

    if (this_dir_block_reclaimable) {
        if (dir_block == fs->root_dir_block) {
            r2f2_ret ret = r2f2_migrate_root_dir_block(fs);
            if (ret != RET_OK) {
                R2F2_LOG_ERR("error (%d) migrating root_dir_block", ret);
                return freed;
            }
        } else {
            if (parent_dir_block != 0) {
                r2f2_ret ret = r2f2_migrate_dir_block_in_entry(
                    fs, dir_block, parent_dir_block, parent_dir_entry);
                if (ret != RET_OK) {
                    R2F2_LOG_ERR("error (%d) migrating dir_block %u", ret,
                                 dir_block);
                }
            }
        }

        free_block(fs, dir_block);
        freed++;
    }

    return freed;
}

uint32_t r2f2_reclaim_file_blocks(r2f2_fs_t *fs, dir_meta_entry_t *dme) {
    uint32_t freed = 0;
    flash_block_idx next[NUM_NEXT_PTRS];
    memcpy(next, dme->next_block, sizeof(next));
    RESULT(block_idx) next_block = get_valid_next_block(fs, next);
    if (next_block.code == RET_OK) {
        /*
         * entry 0 is always a seq_block, which is only upgraded to an
         * indir_block starting from the next entry
         */
        RESULT(uint32_t) block_type = get_block_type(fs, next_block.value);
        CHECK_OK_RETURN(block_type);
        if (block_type.value == BLOCK_TYPE_INDIR) {
            freed += r2f2_reclaim_indir_block(fs, next_block.value);
        } else if (block_type.value == BLOCK_TYPE_SEQ) {
            freed += r2f2_reclaim_seq_block(fs, next_block.value);
        } else {
            R2F2_LOG_ERR("unexpected dir_block in gc");
        }

        free_block(fs, next_block.value);
        freed++;
    }
    return freed;
}

uint32_t r2f2_reclaim_indir_block(r2f2_fs_t *fs, block_idx file_indir_block) {
    uint32_t freed = 0;
    file_indir_entry_t fie;
    for (size_t i = 0; i < NUM_FILE_INDIR_ENTRIES; i++) {
        read_file_indir_entry(fs, file_indir_block, i, &fie);
        flash_block_idx next[NUM_NEXT_PTRS];
        memcpy(next, fie.seq_block, sizeof(next));
        RESULT(block_idx) seq_block = get_valid_next_block(fs, next);
        if (seq_block.code == RET_OK) {
            freed += r2f2_reclaim_seq_block(fs, seq_block.value);
            memset(&fie, 0, sizeof(file_indir_entry_t));
            r2f2_ret ret =
                write_file_indir_entry(fs, file_indir_block, i, &fie);
            if (ret != RET_OK) {
                R2F2_LOG_ERR(
                    "failed (%d) to overwrite entry %zu in indir_block %u", ret,
                    i, file_indir_block);
                return freed;
            }
            free_block(fs, seq_block.value);
            freed++;
        }
    }
    return freed;
}

uint32_t r2f2_reclaim_seq_block(r2f2_fs_t *fs, block_idx file_seq_block) {
    uint32_t freed = 0;
    file_seq_entry_t fse;
    for (size_t i = 0; i < NUM_FILE_SEQ_ENTRIES; i++) {
        read_file_seq_entry(fs, file_seq_block, i, &fse);
        RESULT(block_idx) data_block = GET_FLASH_BLOCK_IDX(fse.data_block);
        if (data_block.code != RET_OK) {
            break;
        }
        if (data_block.value == 0 ||
            data_block.value >= fs->cfg->geom.num_blocks) {
            continue;
        }
        /* R2F2_LOG_DEBUG( */
        /*     "found reclaimable data_block %u in indir_block %u entry %zu", */
        /*     data_block, file_seq_block, i); */

        /* overwrite entry to 0 */
        memset(&fse, 0, sizeof(file_seq_entry_t));
        r2f2_ret ret = write_file_seq_entry(fs, file_seq_block, i, &fse);
        if (ret != RET_OK) {
            R2F2_LOG_ERR("failed (%d) to overwrite entry %zu in seq_block %u",
                         ret, i, file_seq_block);
            return freed;
        }
        free_block(fs, data_block.value);
        freed++;
    }
    return freed;
}

r2f2_ret r2f2_migrate_root_dir_block(r2f2_fs_t *fs) {
    RESULT(block_idx) b;
    bool alloced = false;

    /*
     * We have several possible paths here:
     *
     * 1) Our root_dir_block r contains valid (non-zero) entries, so we allocate
     * a new block b to copy them to.
     *   a) r has a valid next_block ptr, which we copy to b
     *   b) r has no valid next_block ptr, we're done
     *
     * 2) r contains no valid (non-zero) entries
     *   a) r has a valid next_block ptr n, so we discard r and set n as our new
     *      root_dir_block
     *   b) r has no valid next_block ptr, we allocate a new block b and set
     *      that as our new, empty, root_dir_block
     */

    dir_meta_entry_t dme;
    size_t i_w = 0;
    for (size_t i_r = 0; i_r < NUM_DIR_META_ENTRIES; i_r++) {
        /* read entry from read index */
        r2f2_ret ret = read_dir_meta_entry(fs, fs->root_dir_block, i_r, &dme);
        if (ret != RET_OK) {
            return ret;
        }

        /* skip invalidated entries */
        if (is_all_zero(&dme, sizeof(dir_meta_entry_t))) {
            continue;
        }

        /* we reach this point if we have to copy at least one entry over */
        if (!alloced) {
            b = allocate_block(fs);
            if (b.code != RET_OK) {
                return b.code;
            }
            r2f2_ret ret = write_block_header(fs, b.value, BLOCK_TYPE_DIR);
            CHECK_OK_BASIC(ret);
            alloced = true;
        }

        /* write valid entry to write index in new block */
        ret = write_dir_meta_entry(fs, b.value, i_w, &dme);
        if (ret != RET_OK) {
            return ret;
        }
        i_w++;
    }

    flash_block_idx next[NUM_NEXT_PTRS];
    r2f2_ret ret =
        fs->cfg->flash_read(fs,
                            fs->root_dir_block * fs->cfg->geom.block_size +
                                offsetof(dir_meta_block_t, next),
                            sizeof(next), next);
    if (ret != RET_OK) {
        return ret;
    }
    RESULT(block_idx) next_block = get_valid_next_block(fs, next);

    if (i_w > 0) {
        /* path 1) */
        if (next_block.code == RET_OK) {
            r2f2_ret ret =
                fs->cfg->flash_write(fs,
                                     b.value * fs->cfg->geom.block_size +
                                         offsetof(dir_meta_block_t, next),
                                     sizeof(block_idx), &next_block.value);
            if (ret != RET_OK) {
                return ret;
            }
        }
        /* a) and b) */
        fs->root_dir_block = b.value;
    } else {
        /* path 2) */
        if (next_block.code == RET_OK) {
            /* a) */
            fs->root_dir_block = next_block.value;
        } else {
            /* b) */
            R2F2_ASSERT(alloced, ==, false, "%d");
            b = allocate_block(fs);
            if (b.code != RET_OK) {
                return b.code;
            }
            r2f2_ret ret = write_block_header(fs, b.value, BLOCK_TYPE_DIR);
            CHECK_OK_BASIC(ret);
            fs->root_dir_block = b.value;
        }
    }

    /* TODO: update superblock with new root block */

    return RET_OK;
}

r2f2_ret r2f2_migrate_dir_block_in_entry(r2f2_fs_t *fs, block_idx dir_block,
                                         block_idx parent_block,
                                         uint32_t parent_entry) {
    dir_meta_entry_t dme;
    r2f2_ret ret = read_dir_meta_entry(fs, parent_block, parent_entry, &dme);
    CHECK_OK_BASIC(ret);

    /*
     * All entries in our dir_block are used up. Likely, our dir_block has a
     * next_ptr, so we can replace the parent entry with that.
     */

    /* get this dir_block's next_ptr (if it exists) */
    flash_block_idx next[NUM_NEXT_PTRS];
    ret = fs->cfg->flash_read(fs,
                              dir_block * fs->cfg->geom.block_size +
                                  offsetof(dir_meta_block_t, next),
                              sizeof(next), next);
    CHECK_OK_BASIC(ret);
    RESULT(block_idx) dir_next = get_valid_next_block(fs, next);
    if (dir_next.code == RET_NOT_FOUND) {
        /* recovered dir_block had no next_ptr. TODO */
        R2F2_LOG_WARN("unhandled (TODO): migrating dir_block without next_ptr");
        return dir_next.code;
    } else {
        CHECK_OK_RETURN(dir_next);
    }

    /* write next_ptr into the parent dir_entry, if there is space */
    memcpy(next, dme.next_block, sizeof(next));
    RESULT(uint32_t) free_next_ptr = get_unused_next_ptr_idx(fs, next);
    if (free_next_ptr.code == RET_NOMEM) {
        /* parent dir_entry has no more space, we need a new one. TODO */
        R2F2_LOG_WARN("unhandled (TODO): migrating dir_block but need new "
                      "entry in parent");
        return free_next_ptr.code;
    } else {
        CHECK_OK_RETURN(free_next_ptr);
    }
    SET_FLASH_BLOCK_IDX(dme.next_block[free_next_ptr.value], dir_next.value);
    ret = write_dir_meta_entry(fs, parent_block, parent_entry, &dme);
    return ret;
}
