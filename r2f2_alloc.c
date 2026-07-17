#include "r2f2_alloc.h"
#include "util/helpers.h"
#include "util/logger.h"

static uint32_t _alloc_region_start_ptr;
static uint32_t _alloc_region_alloc_ptr;
static uint32_t _alloc_region_next_free_ptr;

/* blocks 0+1 as superblock(s) */
static const uint32_t alloc_region_first_block = 2;
static uint32_t alloc_region_last_block;
static uint32_t first_allocable_block;

static inline r2f2_ret advance_alloc_ptr(r2f2_fs_t *fs) {
    uint32_t new_alloc_ptr =
        _alloc_region_alloc_ptr + sizeof(alloc_block_entry_t);

    /* our alloc pointer must not move outside of our alloc block range */
    if (new_alloc_ptr >=
        (alloc_region_last_block + 1) * fs->cfg->geom.block_size) {
        /*
         * Once we have allocated the necessary number of blocks to reach this
         * condition, the first alloc block should contain some free blocks
         * again, unless we have not freed enough blocks again. Let's check.
         */
        uint8_t pg_buf[fs->cfg->geom.page_size];
        r2f2_ret ret = fs->cfg->flash_read(fs, _alloc_region_start_ptr,
                                           fs->cfg->geom.page_size, pg_buf);
        if (ret != RET_OK) {
            return ret;
        }
        if (is_all_zero(pg_buf, sizeof(pg_buf))) {
            R2F2_LOG_ERR("no more free blocks");
            return RET_NOMEM;
        }

        new_alloc_ptr = _alloc_region_start_ptr;
    }

    /* we also have a problem if our alloc_ptr catches up to our new_free_ptr */
    if (new_alloc_ptr == _alloc_region_next_free_ptr) {
        R2F2_LOG_ERR("no more free blocks");
        return RET_NOMEM;
    }

    _alloc_region_alloc_ptr = new_alloc_ptr;

    return RET_OK;
}

static inline r2f2_ret advance_next_free_ptr(r2f2_fs_t *fs) {
    uint32_t new_next_free_ptr =
        _alloc_region_next_free_ptr + sizeof(alloc_block_entry_t);

    /* our next_free pointer must not move outside of our alloc block range */
    if (new_next_free_ptr >=
        (alloc_region_last_block + 1) * fs->cfg->geom.block_size) {
        /*
         * Once we have freed the necessary number of blocks to reach this
         * condition, we need to wrap over to the first alloc block. All entries
         * in this block must be consumed, i.e. the block 0, because we can only
         * free blocks that have been allocated previously. Let's check just in
         * case.
         */
        for (size_t pg = 0;
             pg < fs->cfg->geom.block_size / fs->cfg->geom.page_size; pg++) {
            uint8_t pg_buf[fs->cfg->geom.page_size];
            r2f2_ret ret = fs->cfg->flash_read(
                fs, _alloc_region_start_ptr + pg * fs->cfg->geom.page_size,
                fs->cfg->geom.page_size, pg_buf);
            if (ret != RET_OK) {
                return ret;
            }
            if (!is_all_zero(pg_buf, sizeof(pg_buf))) {
                R2F2_LOG_ERR("unexpected value in first alloc_block (should be "
                             "empty), dumping: ");
                r2f2_hexdump(pg_buf, sizeof(pg_buf));
                return RET_ERR;
            }
        }

        /* everything is fine, erase the block */
        r2f2_ret ret = fs->cfg->flash_erase(fs, _alloc_region_start_ptr,
                                            fs->cfg->geom.block_size);
        if (ret != RET_OK) {
            return ret;
        }

        new_next_free_ptr = _alloc_region_start_ptr;
    }

    _alloc_region_next_free_ptr = new_next_free_ptr;

    return RET_OK;
}

r2f2_ret prepare_block_allocator(r2f2_fs_t *fs) {
    size_t alloc_region_size =
        fs->cfg->geom.num_blocks * sizeof(alloc_block_entry_t);
    alloc_region_last_block = alloc_region_first_block +
                              (alloc_region_size / fs->cfg->geom.block_size) +
                              1;
    first_allocable_block = alloc_region_last_block + 1;

    _alloc_region_start_ptr =
        alloc_region_first_block * fs->cfg->geom.block_size;
    _alloc_region_alloc_ptr = _alloc_region_start_ptr;

    /* fill our alloc blocks with the indices of all non-reserved blocks */
    size_t entry_idx = 0;
    for (block_idx b = first_allocable_block; b < fs->cfg->geom.num_blocks;
         b++) {
        alloc_block_entry_t entry = {.b = b};
        r2f2_ret ret = fs->cfg->flash_write(
            fs,
            _alloc_region_start_ptr + sizeof(alloc_block_entry_t) * entry_idx,
            sizeof(alloc_block_entry_t), &entry);
        if (ret != RET_OK) {
            R2F2_LOG_ERR("write block idx %u failed (%d)", b, ret);
            return ret;
        }

        entry_idx++;
    }
    _alloc_region_next_free_ptr =
        _alloc_region_start_ptr + sizeof(alloc_block_entry_t) * entry_idx;

    R2F2_LOG_DEBUG("valid alloc region from block %u to %u (0x%x to 0x%x), "
                   "entry size %zu, num_entries %u, actual entries from block "
                   "%u to %u (0x%x to 0x%x)",
                   alloc_region_first_block, alloc_region_last_block,
                   alloc_region_first_block * fs->cfg->geom.block_size,
                   alloc_region_last_block * fs->cfg->geom.block_size,
                   sizeof(alloc_block_entry_t),
                   fs->cfg->geom.num_blocks - first_allocable_block,
                   _alloc_region_start_ptr / fs->cfg->geom.block_size,
                   _alloc_region_next_free_ptr / fs->cfg->geom.block_size,
                   _alloc_region_start_ptr, _alloc_region_next_free_ptr);

    R2F2_ASSERT(_alloc_region_next_free_ptr, <,
                alloc_region_last_block * fs->cfg->geom.block_size, "%u");

    return RET_OK;
}

RESULT(block_idx) allocate_block(r2f2_fs_t *fs) {
    alloc_block_entry_t entry;
    r2f2_ret ret = fs->cfg->flash_read(fs, _alloc_region_alloc_ptr,
                                       sizeof(alloc_block_entry_t), &entry);
    if (ret != RET_OK) {
        R2F2_LOG_ERR("alloc: read block_entry at 0x%x (off %u) failed",
                     _alloc_region_alloc_ptr,
                     _alloc_region_alloc_ptr - _alloc_region_start_ptr);
        return RESULT_ERR(block_idx, ret);
    }

    if (entry.b == 0) {
        R2F2_LOG_ERR("alloc: block_entry at alloc pointer 0x%x  (off %u) "
                     "already used",
                     _alloc_region_alloc_ptr,
                     _alloc_region_alloc_ptr - _alloc_region_start_ptr);
        return RESULT_ERR(block_idx, RET_ERR);
    }

    block_idx b = entry.b;

    // invalidate entry and move pointer along
    entry.b = 0;
    fs->cfg->flash_write(fs, _alloc_region_alloc_ptr,
                         sizeof(alloc_block_entry_t), &entry);

    ret = advance_alloc_ptr(fs);
    if (ret != RET_OK) {
        return RESULT_ERR(block_idx, ret);
    }

    return RESULT_OK(block_idx, b);
}

r2f2_ret free_block(r2f2_fs_t *fs, block_idx b) {
    if (b < first_allocable_block || b >= fs->cfg->geom.num_blocks) {
        R2F2_LOG_ERR("invalid block to be freed %u, not in bounds [%u, %u[", b,
                     first_allocable_block, fs->cfg->geom.num_blocks);
        return RET_ERR;
    }

    r2f2_ret erase_ret = fs->cfg->flash_erase(fs, b * fs->cfg->geom.block_size,
                                              fs->cfg->geom.block_size);
    if (erase_ret != RET_OK) {
        R2F2_LOG_ERR("failed (%d) to erase block %u", erase_ret, b);
        return erase_ret;
    }

    alloc_block_entry_t entry = {.b = b};
    fs->cfg->flash_write(fs, _alloc_region_next_free_ptr,
                         sizeof(alloc_block_entry_t), &entry);

    r2f2_ret ret = advance_next_free_ptr(fs);
    if (ret != RET_OK) {
        return ret;
    }

    return RET_OK;
}
