#pragma once

#include "r2f2.h"
#include "r2f2_defines.h"
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
        uint32_t magic;
        uint8_t version;

        struct __attribute__((packed)) {
            uint32_t page_size;
            uint32_t block_size;
            uint32_t num_blocks;

            uint32_t path_len;
        } fs_config;

    } global_metadata;

    block_idx root_dir_block;
};

struct __attribute__((packed)) r2f2_superblock {
    r2f2_fs_info_t fs_info;
};

typedef uint8_t entry_flags_t;
#define ENTRY_COMMIT_MASK (1U << 0)
#define ENTRY_USED_MASK (1U << 1)
#define ENTRY_INDIRECT_MASK (1U << 2)

typedef struct __attribute__((packed)) dir_meta_entry {
    entry_flags_t f;
    char path[MAX_PATH_LEN];
    block_idx next_block[NUM_NEXT_PTRS];
} dir_meta_entry_t;

typedef struct dir_meta_block {
    struct dir_meta_entry entries[NUM_DIR_META_ENTRIES];
    block_idx next[NUM_NEXT_PTRS];
    uint8_t _[24];
} dir_meta_block_t;

typedef struct __attribute__((packed)) file_indir_entry {
    entry_flags_t f;

    block_idx seq_block[NUM_NEXT_PTRS];
} file_indir_entry_t;

typedef struct __attribute__((packed)) file_indir_block {
    char path[MAX_PATH_LEN];

    struct file_indir_entry entries[NUM_FILE_INDIR_ENTRIES];

    block_idx rnd_updates[NUM_NEXT_PTRS];

    uint8_t padding[506];

    block_idx next_block[NUM_NEXT_PTRS];
} file_indir_block_t;

typedef struct __attribute__((packed)) file_seq_entry {
    entry_flags_t f;

    block_idx data_block;
    uint32_t data_block_fill_level;

    uint32_t data_block_offset_in_file;

    uint32_t current_file_size;
} file_seq_entry_t;

typedef struct __attribute__((packed)) file_seq_block {
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
RESULT(block_idx) get_valid_next_block(r2f2_fs_t *fs, block_idx *indices);

/* flip the corresponding bit to 0 (flash is 0xFF by default) */
void mark_entry_committed(entry_flags_t *f);
void mark_entry_used(entry_flags_t *f);
void mark_entry_indirect(entry_flags_t *f);
/* check if the corresponding bit is 0 */
bool is_entry_committed(entry_flags_t f);
bool is_entry_used(entry_flags_t f);
bool is_entry_indirect(entry_flags_t f);

r2f2_ret read_dir_meta_entry(r2f2_fs_t *fs, block_idx b, uint32_t idx,
                             void *buf);
r2f2_ret write_dir_meta_entry(r2f2_fs_t *fs, block_idx b, uint32_t idx,
                              void *buf);
r2f2_ret write_dir_meta_entry_flags(r2f2_fs_t *fs, block_idx b, uint32_t idx,
                                    void *buf);

r2f2_ret read_file_indir_entry(r2f2_fs_t *fs, block_idx b, uint32_t idx,
                               void *buf);
r2f2_ret write_file_indir_entry(r2f2_fs_t *fs, block_idx b, uint32_t idx,
                                void *buf);
r2f2_ret read_file_indir_entry_flags(r2f2_fs_t *fs, block_idx b, uint32_t idx,
                                     void *buf);
r2f2_ret write_file_indir_entry_flags(r2f2_fs_t *fs, block_idx b, uint32_t idx,
                                      void *buf);

r2f2_ret read_file_seq_entry(r2f2_fs_t *fs, block_idx b, uint32_t idx,
                             void *buf);
r2f2_ret write_file_seq_entry(r2f2_fs_t *fs, block_idx b, uint32_t idx,
                              void *buf);
r2f2_ret read_file_seq_entry_flags(r2f2_fs_t *fs, block_idx b, uint32_t idx,
                                   void *buf);
r2f2_ret write_file_seq_entry_flags(r2f2_fs_t *fs, block_idx b, uint32_t idx,
                                    void *buf);

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

void dump_fs_dot(r2f2_fs_t *fs, const char *filename);
#ifdef __cplusplus
}
#endif
