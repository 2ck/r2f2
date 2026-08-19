#pragma once

#include "r2f2.h"
#include "r2f2_defines.h"
#include "r2f2_file.h"
#include "util/helpers.h"
#include <stdbool.h>

typedef struct r2f2_fs_info r2f2_fs_info_t;
typedef struct r2f2_superblock r2f2_superblock_t;

#define R2F2_MAGIC (0x9922FF22)
#define R2F2_MAGIC_LEN (4U)

#define R2F2_SUPERBLOCK_IDX (0U)

/*
 * Information for identifying R2F2 on disk.
 */
struct __attribute__((packed)) r2f2_fs_info {
    struct __attribute__((packed)) {
        flash_u32 magic;
        flash_u32 version;

        struct __attribute__((packed)) {
            flash_u32 page_size;
            flash_u32 block_size;
            flash_u32 num_blocks;

            flash_u32 path_len;
        } fs_config;

    } global_metadata;

    flash_block_idx root_dir_block;
};

struct __attribute__((packed)) r2f2_superblock {
    r2f2_fs_info_t fs_info;
};

#ifdef ECC_ON_METADATA

/*
 * 15       12 11          8 7         4 3         0
 * +----------+-------------+-----------+-----------+
 * | indirect | reclaimable |   used    |  commit   |
 * +----------+-------------+-----------+-----------+
 */
typedef uint16_t entry_flags_t;
#  define ENTRY_COMMIT_SHIFT 0U
#  define ENTRY_USED_SHIFT 4U
#  define ENTRY_RECLAIMABLE_SHIFT 8U
#  define ENTRY_INDIRECT_SHIFT 12U

#  define ENTRY_COMMIT_MASK (0xFU << ENTRY_COMMIT_SHIFT)
#  define ENTRY_USED_MASK (0xFU << ENTRY_USED_SHIFT)
#  define ENTRY_RECLAIMABLE_MASK (0xFU << ENTRY_RECLAIMABLE_SHIFT)
#  define ENTRY_INDIRECT_MASK (0xFU << ENTRY_INDIRECT_SHIFT)

#else

typedef uint8_t entry_flags_t;
#  define ENTRY_COMMIT_MASK (1U << 0)
#  define ENTRY_USED_MASK (1U << 1)
#  define ENTRY_RECLAIMABLE_MASK (1U << 2)
#  define ENTRY_INDIRECT_MASK (1U << 3)
#endif

typedef enum {
    ENTRY_FLAG_SET,
    ENTRY_FLAG_UNSET,
    ENTRY_FLAG_INVALID
} entry_flag_state_t;

/* flip the corresponding bit(s) to 0 (flash is 0xFF by default) */
entry_flags_t mark_entry_committed(entry_flags_t f);
entry_flags_t mark_entry_used(entry_flags_t f);
entry_flags_t mark_entry_reclaimable(entry_flags_t f);
entry_flags_t mark_entry_indirect(entry_flags_t f);
/* check state of the corresponding bit(s) */
entry_flag_state_t is_entry_committed(entry_flags_t f);
entry_flag_state_t is_entry_used(entry_flags_t f);
entry_flag_state_t is_entry_reclaimable(entry_flags_t f);
entry_flag_state_t is_entry_indirect(entry_flags_t f);

typedef struct __attribute__((packed)) dir_meta_entry {
    char path[MAX_PATH_LEN];
    flash_block_idx next_block[NUM_NEXT_PTRS];
} dir_meta_entry_t;

typedef struct dir_meta_block {
    entry_flags_t f[NUM_DIR_META_ENTRIES];
    struct dir_meta_entry entries[NUM_DIR_META_ENTRIES];
    flash_block_idx next[NUM_NEXT_PTRS];
} dir_meta_block_t;

typedef struct __attribute__((packed)) file_indir_entry {
    flash_block_idx seq_block[NUM_NEXT_PTRS];
} file_indir_entry_t;

typedef struct __attribute__((packed)) file_indir_block {
    char path[MAX_PATH_LEN];

    entry_flags_t f[NUM_FILE_INDIR_ENTRIES];

    struct file_indir_entry entries[NUM_FILE_INDIR_ENTRIES];

    flash_block_idx next_block[NUM_NEXT_PTRS];

    flash_block_idx rnd_updates[NUM_NEXT_PTRS];
} file_indir_block_t;

typedef struct __attribute__((packed)) file_seq_entry {
    flash_block_idx data_block;
    flash_u32 data_block_fill_level;

    flash_u32 data_block_offset_in_file;

    flash_u32 current_file_size;
} file_seq_entry_t;

typedef struct __attribute__((packed)) file_seq_block {
    entry_flags_t f[NUM_FILE_SEQ_ENTRIES];
    struct file_seq_entry entries[NUM_FILE_SEQ_ENTRIES];
} file_seq_block_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * next_block entries are actually arrays, with the length NUM_NEXT_PTRS (2 by
 * default). A value can be all 1 (unused), all 0 (no longer valid/overridden)
 * or some valid block_idx. Later entries always override previous entries.
 *
 * returns the value of the last valid next_block entry
 */
RESULT(block_idx) get_valid_next_block(r2f2_fs_t *fs, flash_block_idx *indices);

r2f2_ret read_dir_meta_entry(r2f2_fs_t *fs, block_idx b, uint32_t idx,
                             dir_meta_entry_t *buf);
r2f2_ret write_dir_meta_entry(r2f2_fs_t *fs, block_idx b, uint32_t idx,
                              dir_meta_entry_t *buf);
r2f2_ret read_dir_meta_entry_flags(r2f2_fs_t *fs, block_idx b, uint32_t idx,
                                   entry_flags_t *buf);
r2f2_ret write_dir_meta_entry_flags(r2f2_fs_t *fs, block_idx b, uint32_t idx,
                                    entry_flags_t *buf);

r2f2_ret read_file_indir_entry(r2f2_fs_t *fs, block_idx b, uint32_t idx,
                               file_indir_entry_t *buf);
r2f2_ret write_file_indir_entry(r2f2_fs_t *fs, block_idx b, uint32_t idx,
                                file_indir_entry_t *buf);
r2f2_ret read_file_indir_entry_flags(r2f2_fs_t *fs, block_idx b, uint32_t idx,
                                     entry_flags_t *buf);
r2f2_ret write_file_indir_entry_flags(r2f2_fs_t *fs, block_idx b, uint32_t idx,
                                      entry_flags_t *buf);

r2f2_ret read_file_seq_entry(r2f2_fs_t *fs, block_idx b, uint32_t idx,
                             file_seq_entry_t *buf);
r2f2_ret write_file_seq_entry(r2f2_fs_t *fs, block_idx b, uint32_t idx,
                              file_seq_entry_t *buf);
r2f2_ret read_file_seq_entry_flags(r2f2_fs_t *fs, block_idx b, uint32_t idx,
                                   entry_flags_t *buf);
r2f2_ret write_file_seq_entry_flags(r2f2_fs_t *fs, block_idx b, uint32_t idx,
                                    entry_flags_t *buf);

bool is_fs_valid(r2f2_fs_t *fs, r2f2_fs_info_t *fs_info);

RESULT(uint32_t) get_free_dir_meta_entry(r2f2_fs_t *fs,
                                         block_idx dir_block_idx);
RESULT(uint32_t) get_free_file_indir_entry(r2f2_fs_t *fs,
                                           block_idx file_indir_block_idx);
RESULT(uint32_t) get_free_file_seq_entry(r2f2_fs_t *fs,
                                         block_idx file_seq_block_idx);

RESULT(uint32_t) get_last_file_indir_entry(r2f2_fs_t *fs,
                                           block_idx file_indir_block_idx);

RESULT(uint32_t) get_last_file_seq_entry(r2f2_fs_t *fs,
                                         block_idx file_seq_block_idx);

RESULT(block_idx) find_data_block_for_off(r2f2_fs_t *fs,
                                          block_idx file_indir_block_idx,
                                          size_t off);
RESULT(block_idx) find_data_block_for_off_direct(r2f2_fs_t *fs,
                                                 block_idx file_seq_block_idx,
                                                 size_t off);

struct dir_traversal_ret {
    block_idx dmb_idx;
    uint32_t dme_idx;
    entry_flags_t dme_flags;
};

r2f2_ret r2f2_get_dir_entry(r2f2_fs_t *fs, const char *path,
                            dir_meta_entry_t *buf,
                            struct dir_traversal_ret *ret);

void dump_fs_dot(r2f2_fs_t *fs, const char *filename);
#ifdef __cplusplus
}
#endif
