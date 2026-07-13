#pragma once

#include "r2f2.h"
#include <stdbool.h>

typedef uint8_t alloc_flags_t;
#define ALLOC_FLAGS_INITIAL 0xFF
#define ALLOC_USED_MASK (1U << 0)

#define FIRST_ALLOCABLE_BLOCK (128U)

#ifdef __cplusplus
extern "C" {
#endif

r2f2_ret prepare_block_allocator(r2f2_fs_t *fs);

RESULT(block_idx) allocate_block(r2f2_fs_t *fs);

r2f2_ret free_block(r2f2_fs_t *fs, block_idx b);

#ifdef __cplusplus
}
#endif
