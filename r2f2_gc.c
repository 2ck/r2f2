#include "r2f2_gc.h"
#include "r2f2.h"
#include "r2f2_alloc.h"
#include "r2f2_metadata.h"
#include "util/logger.h"
#include <string.h>

RESULT(uint32_t) r2f2_gc_entry(r2f2_fs_t *fs, size_t target) {
    uint32_t freed = 0;

    block_idx dir_block = fs->root_dir_block;

    dir_meta_entry_t dme;
    bool block_reclaimable = true;
    for (int d = NUM_DIR_META_ENTRIES - 1; d >= 0; d--) {
        read_dir_meta_entry(fs, dir_block, d, &dme);
        if (is_all_zero(&dme, sizeof(dir_meta_entry_t))) {
            continue;
        }

        if (is_entry_reclaimable(dme.f)) {
            uint32_t freed_blocks = r2f2_reclaim_file_blocks(fs, &dme);
            /* overwrite dir entry */
            memset(&dme, 0, sizeof(dir_meta_entry_t));
            write_dir_meta_entry(fs, dir_block, d, &dme);
            freed += freed_blocks;
            if (freed >= target) {
                return RESULT_OK(uint32_t, freed);
            }
        } else {
            block_reclaimable = false;
        }
    }

    if (block_reclaimable) {
        if (fs->root_dir_block == dir_block) {
            r2f2_ret ret = r2f2_migrate_root_dir_block(fs);
            if (ret != RET_OK) {
                return RESULT_ERR(uint32_t, ret);
            }
        }
        free_block(fs, dir_block);
        freed++;
    }

    if (freed > 0) {
        return RESULT_OK(uint32_t, freed);
    }
    return RESULT_ERR(uint32_t, RET_NOMEM);
}

uint32_t r2f2_reclaim_file_blocks(r2f2_fs_t *fs, dir_meta_entry_t *dme) {
    uint32_t freed = 0;
    for (size_t i = 0; i < NUM_NEXT_PTRS; i++) {
        if (dme->next_block[i] == 0 ||
            dme->next_block[i] >= fs->cfg->geom.num_blocks) {
            continue;
        }

        /*
         * entry 0 is always a seq_block, which is only upgraded to an
         * indir_block starting from the next entry
         */
        if (is_entry_indirect(dme->f) && i != 0) {
            freed += r2f2_reclaim_indir_block(fs, dme->next_block[i]);
        } else {
            freed += r2f2_reclaim_seq_block(fs, dme->next_block[i]);
        }

        free_block(fs, dme->next_block[i]);
        freed++;
    }
    return freed;
}

uint32_t r2f2_reclaim_indir_block(r2f2_fs_t *fs, block_idx file_indir_block) {
    uint32_t freed = 0;
    file_indir_entry_t fie;
    for (size_t i = 0; i < NUM_FILE_INDIR_ENTRIES; i++) {
        read_file_indir_entry(fs, file_indir_block, i, &fie);
        for (size_t i = 0; i < NUM_NEXT_PTRS; i++) {
            if (fie.seq_block[i] == 0 ||
                fie.seq_block[i] >= fs->cfg->geom.num_blocks) {
                continue;
            }
            /* R2F2_LOG_DEBUG( */
            /*     "found reclaimable seq_block %u in indir_block %u entry %zu",
             */
            /*     fie.seq_block[i], file_indir_block, i); */

            freed += r2f2_reclaim_seq_block(fs, fie.seq_block[i]);

            block_idx seq_block = fie.seq_block[i];
            memset(&fie, 0, sizeof(file_indir_entry_t));
            r2f2_ret ret =
                write_file_indir_entry(fs, file_indir_block, i, &fie);
            if (ret != RET_OK) {
                R2F2_LOG_ERR(
                    "failed (%d) to overwrite entry %zu in indir_block %u", ret,
                    i, file_indir_block);
                return freed;
            }
            free_block(fs, seq_block);
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
        if (fse.data_block == 0 || fse.data_block >= fs->cfg->geom.num_blocks) {
            continue;
        }
        /* R2F2_LOG_DEBUG( */
        /*     "found reclaimable data_block %u in indir_block %u entry %zu", */
        /*     fse.data_block, file_seq_block, i); */

        /* overwrite data_block to 0 */
        block_idx data_block = fse.data_block;
        memset(&fse, 0, sizeof(file_seq_entry_t));
        r2f2_ret ret = write_file_seq_entry(fs, file_seq_block, i, &fse);
        if (ret != RET_OK) {
            R2F2_LOG_ERR("failed (%d) to overwrite entry %zu in seq_block %u",
                         ret, i, file_seq_block);
            return freed;
        }
        free_block(fs, data_block);
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

        /* we have to copy at least one entry over */
        if (!alloced) {
            b = allocate_block(fs);
            if (b.code != RET_OK) {
                return b.code;
            }
            alloced = true;
        }

        /* write valid entry to write index in new block */
        ret = write_dir_meta_entry(fs, b.value, i_w, &dme);
        if (ret != RET_OK) {
            return ret;
        }
        i_w++;
    }

    block_idx next[NUM_NEXT_PTRS];
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
            fs->root_dir_block = b.value;
        }
    }

    /* TODO: update superblock with new root block */

    return RET_OK;
}
