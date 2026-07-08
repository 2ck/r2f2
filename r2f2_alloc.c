#include "r2f2_alloc.h"
#include "util/helpers.h"
#include "util/logger.h"

#if (R2F2_ALLOC_METHOD == R2F2_ALLOC_BITFIELD)
// one or more allocation blocks contain bitfields
// each bit corresponds linearly to the next blocks in flash
// after a certain offset
// default 1: free
// flip to 0: atomically allocate
//
// there is additionally a per-page header to be able to safely
// move the entries to a new block when an erase is needed

void mark_alloc_used(alloc_flags_t *f) {
    *f &= ~ALLOC_USED_MASK;
}
bool is_alloc_used(alloc_flags_t f) {
    return (f & ALLOC_USED_MASK) == 0;
}

struct __attribute__((packed)) alloc_bitmap_block {
    struct {
        alloc_flags_t flags;
        uint8_t state[255];
    } chunk[16];
};
STATIC_ASSERT(sizeof(struct alloc_bitmap_block) == BLOCK_SIZE);

block_idx alloc_bitmap_block_idx = FIRST_ALLOCABLE_BLOCK - 1;
r2f2_ret prepare_block_allocator(r2f2_fs_t *fs) {
    // mark all alloc bitmap chunks as valid
    alloc_flags_t flags = ALLOC_FLAGS_INITIAL;
    mark_alloc_used(&flags);

    for (size_t i = 0; i < 16; i++) {
        /* TODO get rid of magic number 256 with sizeof */
        r2f2_ret ret = fs->cfg->flash_write(
            fs, alloc_bitmap_block_idx * fs->cfg->geom.block_size + i * 256, 1,
            &flags);
        if (ret != RET_OK) {
            R2F2_LOG_ERR(
                "write flags failed (%d) at block %u, offset %zu * 256", ret,
                alloc_bitmap_block_idx, i);
            return ret;
        }
    }

    return RET_OK;
}

block_idx next_alloc = FIRST_ALLOCABLE_BLOCK;
RESULT(block_idx) allocate_block(r2f2_fs_t *fs) {
    const int32_t max_its = 100;
    int32_t its = 0;
    do {
        uint8_t chunk_idx = next_alloc / (255 * 8);
        if (chunk_idx > 15) {
            R2F2_LOG_ERR("chunk_idx overflow");
            RESULT_ERR(block_idx, RET_NOMEM);
        }

        alloc_flags_t flags;
        /* bitmap chunk valid? */
        r2f2_ret ret = fs->cfg->flash_read(
            fs,
            alloc_bitmap_block_idx * fs->cfg->geom.block_size + chunk_idx * 256,
            1, &flags);
        if (ret != RET_OK) {
            R2F2_LOG_ERR(
                "reading allocate bitmap flags failed (%d) at chunk %d", ret,
                chunk_idx);
            return RESULT_ERR(block_idx, ret);
        }
        /* TODO: helpers for these reads/writes */

        if (!is_alloc_used(flags)) {
            next_alloc += 255 * 8;
            continue;
        }

        uint8_t some_bits = 0;
        ret = fs->cfg->flash_read(fs,
                                  alloc_bitmap_block_idx *
                                          fs->cfg->geom.block_size +
                                      chunk_idx * 256
                                      /* bitmap starts after the flags */
                                      + 1
                                      /* byte to read */
                                      + (next_alloc / 8),
                                  1, &some_bits);
        if (ret != RET_OK) {
            R2F2_LOG_ERR(
                "reading allocate bitmap failed (%d) at chunk %d next_alloc %d",
                ret, chunk_idx, next_alloc);
            return RESULT_ERR(block_idx, ret);
        }

        if (some_bits & (1 << (next_alloc % 8))) {
            /* available, immediately flip to 0 before giving it out */
            some_bits ^= (1 << (next_alloc % 8));
            ret = fs->cfg->flash_write(fs,
                                       alloc_bitmap_block_idx *
                                               fs->cfg->geom.block_size +
                                           chunk_idx * 256
                                           /* bitmap starts after the flags */
                                           + 1
                                           /* byte to read */
                                           + (next_alloc / 8),
                                       1, &some_bits);

            if (ret != RET_OK) {
                R2F2_LOG_ERR("writing allocate bitmap failed (%d) at chunk %d "
                             "next_alloc %d",
                             ret, chunk_idx, next_alloc);
                return RESULT_ERR(block_idx, ret);
            }

            return RESULT_OK(block_idx, next_alloc++);
        }
    } while (its++ < max_its);
    R2F2_LOG_ERR("Couldn't find free block in %d iterations", max_its);
    return RESULT_ERR(block_idx, RET_NOMEM);
}

#elif (R2F2_ALLOC_METHOD == R2F2_ALLOC_CIRCULAR_BUFFER)

/*
 * The indices of all free blocks are stored in alloc blocks. If all flash block
 * indices fit in n blocks, there are n+k alloc blocks, where k>=1. The larger
 * the value of k, the more allocations we can perform without erasing a fully
 * used alloc block.
 *
 * An alloc block entry has three valid states:
 * - all 1s (default in flash): an empty slot
 * - 0 < value <= LAST_BLOCK_IDX: a free block index
 * - 0: an allocated/consumed block index
 *
 * An alloc pointer points to the first alloc block entry; allocation of a block
 * consumes the entry and increments the alloc pointer.
 *
 * A second pointer points past the last alloc entry; freeing a block writes its
 * index to this location and increments the pointer.
 *
 */

typedef struct __attribute__((packed)) alloc_block_entry {
    block_idx b;
} alloc_block_entry_t;

static uint32_t _alloc_region_start_ptr;
static uint32_t _alloc_region_alloc_ptr;
static uint32_t _alloc_region_next_free_ptr;

#  define ALLOC_REGION_FIRST_BLOCK (16U)
#  define ALLOC_REGION_LAST_BLOCK (48U)

static inline r2f2_ret advance_alloc_ptr(r2f2_fs_t *fs) {
    uint32_t new_alloc_ptr =
        _alloc_region_alloc_ptr + sizeof(alloc_block_entry_t);

    /* our alloc pointer must not move outside of our alloc block range */
    if (new_alloc_ptr >=
        (ALLOC_REGION_LAST_BLOCK + 1) * fs->cfg->geom.block_size) {
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
        (ALLOC_REGION_LAST_BLOCK + 1) * fs->cfg->geom.block_size) {
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
                hexdump(pg_buf, sizeof(pg_buf));
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
    _alloc_region_start_ptr =
        ALLOC_REGION_FIRST_BLOCK * fs->cfg->geom.block_size;
    _alloc_region_alloc_ptr = _alloc_region_start_ptr;

    /* fill our alloc blocks with the indices of all non-reserved blocks */
    size_t entry_idx = 0;
    for (block_idx b = FIRST_ALLOCABLE_BLOCK; b < fs->cfg->geom.num_blocks;
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
                   ALLOC_REGION_FIRST_BLOCK, ALLOC_REGION_LAST_BLOCK,
                   ALLOC_REGION_FIRST_BLOCK * fs->cfg->geom.block_size,
                   ALLOC_REGION_LAST_BLOCK * fs->cfg->geom.block_size,
                   sizeof(alloc_block_entry_t),
                   fs->cfg->geom.num_blocks - FIRST_ALLOCABLE_BLOCK,
                   _alloc_region_start_ptr / fs->cfg->geom.block_size,
                   _alloc_region_next_free_ptr / fs->cfg->geom.block_size,
                   _alloc_region_start_ptr, _alloc_region_next_free_ptr);

    R2F2_ASSERT(_alloc_region_next_free_ptr, <,
                ALLOC_REGION_LAST_BLOCK * fs->cfg->geom.block_size, "%u");

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
    if (b < FIRST_ALLOCABLE_BLOCK || b >= fs->cfg->geom.num_blocks) {
        R2F2_LOG_ERR("invalid block to be freed %u, not in bounds [%u, %u[", b,
                     FIRST_ALLOCABLE_BLOCK, fs->cfg->geom.num_blocks);
        return RET_ERR;
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

#endif
