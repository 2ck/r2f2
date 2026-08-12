#include "r2f2_alloc.h"
#include "r2f2_gc.h"
#include "util/helpers.h"
#include "util/logger.h"
#include <string.h>

static uint32_t _alloc_ptr;
static uint32_t _next_free_ptr;

/* blocks 0+1 as superblock(s) */
static const uint32_t _allocator_first_block = 2;
static uint32_t _allocator_num_blocks;
static uint32_t _first_allocable_block;

static size_t _total_free = 0;

static inline uint32_t get_num_allocator_blocks(r2f2_fs_t *fs) {
    /*
     * We have enough space with ceil(N/K) + 1 blocks
     * where N is the number of allocable blocks
     * and K is the number of alloc_block_entries that fit in a block
     *
     * Here we overprovision by using the total number of flash blocks for N.
     */

    uint32_t N = fs->cfg->geom.num_blocks;
    uint32_t K = fs->cfg->geom.block_size / sizeof(alloc_block_entry_t);
    uint32_t ceil = (N - 1) / K + 1;
    return ceil + 1;
}

r2f2_ret prepare_block_allocator(r2f2_fs_t *fs) {
    R2F2_ASSERT(fs->cfg->geom.block_size % sizeof(alloc_block_entry_t), ==, 0,
                "%zu");

    _allocator_num_blocks = get_num_allocator_blocks(fs);
    _first_allocable_block = _allocator_first_block + _allocator_num_blocks;

    _alloc_ptr = _allocator_first_block * fs->cfg->geom.block_size;

    /* fill our allocator blocks with the indices of all non-reserved blocks */
    size_t entry_idx = 0;
    for (block_idx b = _first_allocable_block; b < fs->cfg->geom.num_blocks;
         b++) {
        alloc_block_entry_t entry;
        SET_FLASH_BLOCK_IDX(entry.b, b);
        r2f2_ret ret = fs->cfg->flash_write(
            fs,
            _allocator_first_block * fs->cfg->geom.block_size +
                entry_idx * sizeof(alloc_block_entry_t),
            sizeof(alloc_block_entry_t), &entry);
        if (ret != RET_OK) {
            R2F2_LOG_ERR("write block idx %u failed (%d)", b, ret);
            return ret;
        }

        entry_idx++;
        _total_free++;
    }

    _next_free_ptr = _alloc_ptr + sizeof(alloc_block_entry_t) * entry_idx;

    R2F2_LOG_DEBUG("valid alloc region from block %u to %u (0x%x to 0x%x), "
                   "entry size %zu, num_entries %u, actual entries from block "
                   "%u to %u (0x%x to 0x%x)",
                   _allocator_first_block,
                   _allocator_first_block + _allocator_num_blocks - 1,
                   _allocator_first_block * fs->cfg->geom.block_size,
                   (_allocator_first_block + _allocator_num_blocks - 1) *
                       fs->cfg->geom.block_size,
                   sizeof(alloc_block_entry_t),
                   fs->cfg->geom.num_blocks - _first_allocable_block,
                   _alloc_ptr / fs->cfg->geom.block_size,
                   _next_free_ptr / fs->cfg->geom.block_size, _alloc_ptr,
                   _next_free_ptr);

    R2F2_ASSERT(_next_free_ptr, <,
                (_allocator_first_block + _allocator_num_blocks) *
                    fs->cfg->geom.block_size,
                "%u");

    return RET_OK;
}

static inline void advance_ptr(r2f2_fs_t *fs, uint32_t *_ptr) {
    uint32_t ptr = *_ptr;
    ptr += sizeof(alloc_block_entry_t);
    if (ptr >= (_allocator_first_block + _allocator_num_blocks) *
                   fs->cfg->geom.block_size) {
        ptr = _allocator_first_block * fs->cfg->geom.block_size;
    }
    *_ptr = ptr;
}

RESULT(block_idx) allocate_block(r2f2_fs_t *fs) {
    if (_alloc_ptr == _next_free_ptr) {
        RESULT(uint32_t) gc_ret = r2f2_gc_entry(fs, 1);
        if (gc_ret.code != RET_OK) {
            R2F2_LOG_ERR(
                "no more free blocks and garbage collection failed (%d)",
                gc_ret.code);
            dump_fs_dot(fs, "gc_fail.dot");
            return RESULT_ERR(block_idx, gc_ret.code);
        } else {
            R2F2_LOG_DEBUG("garbage collection recovered %u blocks",
                           gc_ret.value);
        }
    }

    alloc_block_entry_t entry;
    r2f2_ret ret = fs->cfg->flash_read(fs, _alloc_ptr,
                                       sizeof(alloc_block_entry_t), &entry);
    if (ret != RET_OK) {
        R2F2_LOG_ERR(
            "alloc: read block_entry at 0x%x (off %u) failed", _alloc_ptr,
            _alloc_ptr - (_allocator_first_block * fs->cfg->geom.block_size));
        return RESULT_ERR(block_idx, ret);
    }

    RESULT(block_idx) bix = GET_FLASH_BLOCK_IDX(entry.b);
    CHECK_OK_PROPAGATE(bix, block_idx);
    block_idx b = bix.value;

    if (b < _first_allocable_block || b >= fs->cfg->geom.num_blocks) {
        R2F2_LOG_ERR("alloc: block_entry %u at alloc pointer 0x%x  (off %u) "
                     "invalid",
                     b, _alloc_ptr,
                     _alloc_ptr -
                         (_allocator_first_block * fs->cfg->geom.block_size));
        return RESULT_ERR(block_idx, RET_ERR);
    }

    // invalidate entry and move pointer along
    memset(&entry, 0, sizeof(alloc_block_entry_t));
    ret = fs->cfg->flash_write(fs, _alloc_ptr, sizeof(alloc_block_entry_t),
                               &entry);
    if (ret != RET_OK) {
        return RESULT_ERR(block_idx, ret);
    }

    advance_ptr(fs, &_alloc_ptr);

    _total_free--;
    /* R2F2_LOG_INFO("allocate block %u, total free %zu", b, _total_free); */
    return RESULT_OK(block_idx, b);
}

r2f2_ret free_block(r2f2_fs_t *fs, block_idx b) {
    if (b < _first_allocable_block || b >= fs->cfg->geom.num_blocks) {
        R2F2_LOG_ERR("invalid block to be freed %u, not in bounds [%u, %u[", b,
                     _first_allocable_block, fs->cfg->geom.num_blocks);
        return RET_ERR;
    }

    /* we have entered a new block. do we need to erase? */
    if (_next_free_ptr % fs->cfg->geom.block_size == 0) {
        /* FIXME: use smaller buffer and check step by step */
        uint8_t block_buf[fs->cfg->geom.block_size];
        r2f2_ret ret = fs->cfg->flash_read(fs, _next_free_ptr,
                                           fs->cfg->geom.block_size, block_buf);
        if (ret != RET_OK) {
            return ret;
        }

        /*
         * Our new block can either be erased (all 1), or fully used (all
         * entries set to 0), although the former only happens for the first
         * free into an allocator block that was never used.
         */
        if (!is_all_zero(block_buf, sizeof(block_buf))) {
            if (!is_all_one(block_buf, sizeof(block_buf))) {
                R2F2_LOG_ERR("block %u should have been empty (all 0)",
                             _next_free_ptr / fs->cfg->geom.block_size);
                r2f2_hexdump(block_buf, sizeof(block_buf));
                return RET_ERR;
            }
            /* all 0xFF, nothing to do */
        } else {
            /* all 0, we have to erase */
            ret = fs->cfg->flash_erase(fs, _next_free_ptr,
                                       fs->cfg->geom.block_size);
            if (ret != RET_OK) {
                return ret;
            }
        }
    }

    r2f2_ret ret = fs->cfg->flash_erase(fs, b * fs->cfg->geom.block_size,
                                        fs->cfg->geom.block_size);
    if (ret != RET_OK) {
        R2F2_LOG_ERR("failed (%d) to erase block %u", ret, b);
        return ret;
    }

    alloc_block_entry_t entry = {.b = b};
    ret = fs->cfg->flash_write(fs, _next_free_ptr, sizeof(alloc_block_entry_t),
                               &entry);
    if (ret != RET_OK) {
        return ret;
    }

    advance_ptr(fs, &_next_free_ptr);

    _total_free++;
    /* R2F2_LOG_INFO("free block %u, total free now %zu", b, _total_free); */

    return RET_OK;
}
