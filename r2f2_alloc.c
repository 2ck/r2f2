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

alloc_flags_t mark_alloc_used(alloc_flags_t f) {
    return f &= ~ALLOC_USED_MASK;
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
    alloc_flags_t flags;
    mark_alloc_used(flags);

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

// all free block indices are stored in alloc blocks
//
// an alloc pointer points to the first alloc block entry
// allocation of a block consumes the entry and increments the alloc pointer
// a second pointer points past the last alloc entry
// freeing a block writes the index to this location and increments it
//
// there are n+1 alloc blocks, in the first 128? 64? (TODO) blocks of flash
// here, n is the block count divided by the entry size. in our case: 32
// to reduce wear on the alloc blocks, they move along with
// allocs/frees, until at some point they return their starting spot

union alloc_block_entry {
    uint32_t raw;
    struct {
        // flags
        uint32_t chunk_unused : 1;
        uint32_t : 7;
        // 3B of block index, enough for 64GiB with 4096B blocks
        // realistically, we could spare some bits for parity
        // for the issi we need 15 bits
        // 20 should be enough for any nor flash on the market
        uint32_t idx : 24;
    } bits;
};
struct __attribute__((packed)) alloc_block_t {
    alloc_block_entry entries[BLOCK_SIZE / 4];
};
static_assert(sizeof(alloc_block_t) == BLOCK_SIZE);

// block_idx _alloc_block_num(size_t entry_idx) {
//     block_idx num = first_alloc_block;
//     num += entry_idx / ()
// }

void prepare_block_allocator(logfs_fs *fs) {
    _alloc_region_start_ptr = first_alloc_block * fs->cfg->geom.block_size;
    _alloc_region_alloc_ptr = first_alloc_block * fs->cfg->geom.block_size;
    alloc_block_entry entry = {.bits.chunk_unused = 0};
    for (size_t bnum = first_allocable_block; bnum < fs->cfg->geom.num_blocks;
         bnum++) {
        size_t entry_idx = bnum - first_allocable_block;
        entry.bits.idx = bnum;
        fs->cfg->flash_write(
            fs, _alloc_region_start_ptr + sizeof(alloc_block_entry) * entry_idx,
            sizeof(alloc_block_entry), &entry);
    }
    _alloc_region_past_end_ptr =
        _alloc_region_start_ptr +
        sizeof(alloc_block_entry) * fs->cfg->geom.num_blocks;
}

block_idx first_alloc_block = 8;   // TODO!
uint8_t num_alloc_blocks = 32 + 1; // TODO!
int32_t next_alloc_idx = 0;

uint32_t _alloc_region_start_ptr = 0;
uint32_t _alloc_region_alloc_ptr = 0;
uint32_t _alloc_region_past_end_ptr = 0;

RESULT(block_idx) allocate_block(logfs_fs *fs) {
    // trivial case, take a block
    alloc_block_entry entry;
    block_idx b;
    logfs_ret ret = fs->cfg->flash_read(fs, _alloc_region_alloc_ptr,
                                        sizeof(alloc_block_entry), &entry);
    if (ret != RET_OK) {
        LOG_ERR("alloc: read block_entry at 0x%x (off %u) failed",
                _alloc_region_alloc_ptr,
                _alloc_region_alloc_ptr - _alloc_region_start_ptr);
        return RET_ERR;
    }
    if (entry.bits.chunk_unused) {
        LOG_ERR("alloc: block_entry unused, entry %u, unused %u, idx %u",
                entry.raw, entry.bits.chunk_unused, entry.bits.idx);
        LOG_ERR("start %u alloc %u past_end %u", _alloc_region_start_ptr,
                _alloc_region_alloc_ptr, _alloc_region_past_end_ptr);
        return RET_ERR;
    }

    b = entry.bits.idx;

    // invalidate entry and move pointer along
    entry.raw = 0;
    fs->cfg->flash_write(fs, _alloc_region_alloc_ptr, sizeof(alloc_block_entry),
                         &entry);

    _alloc_region_alloc_ptr += sizeof(alloc_block_entry);
    return b;
}
#endif
