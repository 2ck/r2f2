#pragma once

#include "r2f2.h"
#include <stdbool.h>

#define R2F2_ALLOC_BITFIELD 0x1
#define R2F2_ALLOC_CIRCULAR_BUFFER 0x2

#define R2F2_ALLOC_METHOD R2F2_ALLOC_BITFIELD
#ifndef R2F2_ALLOC_METHOD
#  define R2F2_ALLOC_METHOD R2F2_ALLOC_CIRCULAR_BUFFER
#endif

typedef uint8_t alloc_flags_t;
#define ALLOC_USED_MASK (1U << 0)

#define FIRST_ALLOCABLE_BLOCK (128U)

#ifdef __cplusplus
extern "C" {
#endif

/* flip the corresponding bit to 0 (flash is 0xFF by default) */
alloc_flags_t mark_alloc_used(alloc_flags_t f);
/* check if the corresponding bit is 0 */
bool is_alloc_used(alloc_flags_t f);

r2f2_ret prepare_block_allocator(r2f2_fs_t *fs);

r2f2_ret allocate_block(r2f2_fs_t *fs);

#ifdef __cplusplus
}
#endif
