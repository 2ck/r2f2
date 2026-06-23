#include "r2f2_metadata.h"
#include <stdbool.h>

entry_flags_t mark_entry_committed(entry_flags_t f) {
    return f &= ~ENTRY_COMMIT_MASK;
}

bool is_entry_committed(entry_flags_t f) {
    return (f & ENTRY_COMMIT_MASK) == 0;
}

bool is_fs_valid(r2f2_fs_t *fs, r2f2_fs_info_t *fs_info) {
    // check magic
    if (fs_info->global_metadata.magic != R2F2_MAGIC)
        return false;
    // TODO: check geometry etc.
    return true;
}
