#pragma once

#include "r2f2.h"
#include <stdbool.h>

/**
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
    flash_block_idx b;
    /* TODO: can we get rid of this */
    uint8_t padding;
} alloc_block_entry_t;

#ifdef __cplusplus
extern "C" {
#endif

r2f2_ret prepare_block_allocator(r2f2_fs_t *fs);

RESULT(block_idx) allocate_block(r2f2_fs_t *fs);

r2f2_ret free_block(r2f2_fs_t *fs, block_idx b);

#ifdef __cplusplus
}
#endif
