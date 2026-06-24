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
            uint16_t page_size;
            uint16_t block_size;
            uint32_t num_blocks;

            uint16_t path_len;
        } fs_config;

    } global_metadata;

    block_idx root_dir_block;

    uint8_t padding[PAGE_SIZE - 19];
};
STATIC_ASSERT(sizeof(struct r2f2_fs_info) == PAGE_SIZE);

struct __attribute__((packed)) r2f2_superblock {
    r2f2_fs_info_t fs_info;
    uint8_t padding[BLOCK_SIZE - PAGE_SIZE];
};
STATIC_ASSERT(sizeof(struct r2f2_superblock) == BLOCK_SIZE);

typedef uint8_t entry_flags_t;
#define ENTRY_COMMIT_MASK (1U << 0)
#define ENTRY_USED_MASK (1U << 1)

typedef struct __attribute__((packed)) dir_meta_entry {
    entry_flags_t f;
    char path[MAX_PATH_LEN];
    block_idx next_block;
} dir_meta_entry_t;

typedef struct dir_meta_block {
    struct dir_meta_entry entries[NUM_DIR_META_ENTRIES];
} dir_meta_block_t;
STATIC_ASSERT(sizeof(struct dir_meta_block) == BLOCK_SIZE);

struct __attribute__((packed)) file_meta_entry {
    entry_flags_t f;

    uint8_t padding[8];

    block_idx indir_block;
};

struct __attribute__((packed)) file_meta_block {
    char path[MAX_PATH_LEN];

    struct file_meta_entry entries[NUM_FILE_META_ENTRIES];

    uint8_t padding[641];

    block_idx next_block;
};
STATIC_ASSERT(sizeof(struct file_meta_block) == BLOCK_SIZE);

struct __attribute__((packed)) file_indir_entry {
    entry_flags_t flags;

    block_idx data_block;
    uint16_t data_block_fill_level;

    uint32_t data_block_offset_in_file;

    uint32_t current_file_size;

    uint8_t padding[17];
};

struct __attribute__((packed)) file_indir_block {
    struct file_indir_entry entries[NUM_FILE_INDIR_ENTRIES];
};
STATIC_ASSERT(sizeof(struct file_indir_block) == BLOCK_SIZE);

#ifdef __cplusplus
extern "C" {
#endif

/* flip the corresponding bit to 0 (flash is 0xFF by default) */
entry_flags_t mark_entry_committed(entry_flags_t f);
entry_flags_t mark_entry_used(entry_flags_t f);
/* check if the corresponding bit is 0 */
bool is_entry_committed(entry_flags_t f);
bool is_entry_used(entry_flags_t f);

r2f2_ret read_dir_entry(r2f2_fs_t *fs, block_idx dir_block_idx, uint32_t idx,
                        void *buf);

bool is_fs_valid(r2f2_fs_t *fs, r2f2_fs_info_t *fs_info);

RESULT(uint32_t)
get_free_dir_meta_entry(r2f2_fs_t *fs, block_idx dir_block_idx);

#ifdef __cplusplus
}
#endif
