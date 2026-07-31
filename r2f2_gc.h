#pragma once

#include "r2f2_defines.h"
#include "r2f2_metadata.h"

RESULT(uint32_t) r2f2_gc_entry(r2f2_fs_t *fs, size_t target);

uint32_t r2f2_reclaim_file_blocks(r2f2_fs_t *fs, dir_meta_entry_t *dme,
                                  entry_flags_t flags);

uint32_t r2f2_reclaim_indir_block(r2f2_fs_t *fs, block_idx file_indir_block);

uint32_t r2f2_reclaim_seq_block(r2f2_fs_t *fs, block_idx file_seq_block);

r2f2_ret r2f2_migrate_root_dir_block(r2f2_fs_t *fs);
