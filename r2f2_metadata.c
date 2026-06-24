#include "r2f2_metadata.h"
#include "util/logger.h"
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

entry_flags_t mark_entry_committed(entry_flags_t f) {
    return f &= ~ENTRY_COMMIT_MASK;
}

entry_flags_t mark_entry_used(entry_flags_t f) {
    return f &= ~ENTRY_USED_MASK;
}

bool is_entry_committed(entry_flags_t f) {
    return (f & ENTRY_COMMIT_MASK) == 0;
}

bool is_entry_used(entry_flags_t f) {
    return (f & ENTRY_USED_MASK) == 0;
}

r2f2_ret read_dir_entry(r2f2_fs_t *fs, block_idx dir_block_idx, uint32_t idx,
                        void *buf) {
    return fs->cfg->flash_read(fs,
                               dir_block_idx * fs->cfg->geom.block_size +
                                   offsetof(dir_meta_block_t, entries) +
                                   idx * sizeof(dir_meta_entry_t),
                               sizeof(dir_meta_entry_t), buf);
}

bool is_fs_valid(r2f2_fs_t *fs, r2f2_fs_info_t *fs_info) {
    /* check magic */
    if (fs_info->global_metadata.magic != R2F2_MAGIC)
        return false;
    /* TODO: check geometry etc. */
    return true;
}

RESULT(uint32_t)
get_free_dir_meta_entry(r2f2_fs_t *fs, block_idx dir_block_idx) {
    dir_meta_entry_t dme;
    /* initialize as an unused entry (0xFF in flash) */
    memset(&dme, 0xFF, sizeof(dir_meta_entry_t));

    for (size_t i = 0; i < NUM_DIR_META_ENTRIES; i++) {
        r2f2_ret ret = read_dir_entry(fs, dir_block_idx, i, &dme);
        if (ret != RET_OK) {
            R2F2_LOG_ERR("dir entry read failed");
            return RESULT_ERR(uint32_t, ret);
        }
        if (!is_entry_used(dme.f)) {
            return RESULT_OK(uint32_t, i);
        }
    }
    return RESULT_ERR(uint32_t, RET_NOT_FOUND);
}
