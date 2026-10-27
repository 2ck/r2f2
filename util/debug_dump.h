#pragma once

#ifdef TEST_ENVIRONMENT

#  include "util/logger.h"
#  include <stdio.h>
#  include <string.h>

static inline void dump_file_seq_block(FILE *f, r2f2_fs_t *fs,
                                       block_idx prev_block, size_t prev_entry,
                                       block_idx file_seq_block, bool direct) {
    fprintf(f,
            "\nfile_seq_block_%d [shape=record, fillcolor=\"#eadcf8\", "
            "\nlabel=\"{file_seq_block %u | { ",
            file_seq_block, file_seq_block);

    entry_flags_t fse_flags;
    memset(&fse_flags, 0xFF, sizeof(entry_flags_t));
    file_seq_entry_t fse;
    for (size_t i = 0; i < NUM_FILE_SEQ_ENTRIES; i++) {
        read_file_seq_entry(fs, file_seq_block, i, &fse);
        read_file_seq_entry_flags(fs, file_seq_block, i, &fse_flags);
        if (!is_all_one(&fse, sizeof(file_seq_entry_t))) {
            fprintf(f,
                    "{entries[%zu]: | used=%d,comm=%d,recl=%d | fill %u | offs "
                    "%u | file size %u | data block %u} | ",
                    i, is_entry_used(fse_flags) == ENTRY_FLAG_SET,
                    is_entry_committed(fse_flags) == ENTRY_FLAG_SET,
                    is_entry_reclaimable(fse_flags) == ENTRY_FLAG_SET,
                    GET_FLASH_U32(fse.data_block_fill_level).value,
                    GET_FLASH_U32(fse.data_block_offset_in_file).value,
                    GET_FLASH_U32(fse.current_file_size).value,
                    GET_FLASH_BLOCK_IDX(fse.data_block).value);
        }
    }

    fprintf(f, " }}\"\n];\n");

    if (direct) {
        fprintf(f, "\ndir_block_%u:e%zu -> file_seq_block_%u", prev_block,
                prev_entry, file_seq_block);
    } else {
        fprintf(f, "\nfile_indir_block_%u:e%zu -> file_seq_block_%u",
                prev_block, prev_entry, file_seq_block);
    }
}

static inline void dump_file_indir_block(FILE *f, r2f2_fs_t *fs,
                                         block_idx dir_block, size_t dir_entry,
                                         block_idx file_indir_block) {
    fprintf(f,
            "\nfile_indir_block_%u [shape=record, fillcolor=\"#cfe2f3\", "
            "\nlabel=\"{file_indir_block %u | { ",
            file_indir_block, file_indir_block);

    file_indir_entry_t fie;
    for (size_t i = 0; i < NUM_FILE_INDIR_ENTRIES; i++) {
        read_file_indir_entry(fs, file_indir_block, i, &fie);
        if (!is_all_one(&fie, sizeof(file_indir_entry_t))) {
            flash_block_idx next[NUM_NEXT_PTRS];
            memcpy(next, fie.seq_block, sizeof(next));
            RESULT(block_idx) next_block = get_valid_next_block(fs, next);
            if (next_block.code != RET_OK) {
                R2F2_LOG_ERR("no valid next block");
                return;
            }
            fprintf(f, "<e%zu> %u | ", i, next_block.value);
        }
    }

    fprintf(f, " }}\"\n];\n");

    fprintf(f, "\ndir_block_%u:e%zu -> file_indir_block_%u", dir_block,
            dir_entry, file_indir_block);

    for (size_t i = 0; i < NUM_FILE_INDIR_ENTRIES; i++) {
        read_file_indir_entry(fs, file_indir_block, i, &fie);
        if (!is_all_one(&fie, sizeof(file_indir_entry_t))) {
            flash_block_idx next[NUM_NEXT_PTRS];
            memcpy(next, fie.seq_block, sizeof(next));
            RESULT(block_idx) next_block = get_valid_next_block(fs, next);
            if (next_block.code != RET_OK) {
                R2F2_LOG_ERR("no valid next block");
                return;
            }
            dump_file_seq_block(f, fs, file_indir_block, i, next_block.value,
                                0);
        }
    }
}

static inline void dump_dir_block(FILE *f, r2f2_fs_t *fs, block_idx dir_block) {
    fprintf(f,
            "\ndir_block_%d [shape=record, fillcolor=\"#ffe599\", "
            "\nlabel=\"{<db%u> dir_block %u | { ",
            dir_block, dir_block, dir_block);

    dir_meta_entry_t dme;
    for (size_t d = 0; d < NUM_DIR_META_ENTRIES; d++) {
        read_dir_meta_entry(fs, dir_block, d, &dme);
        if (!is_all_zero(&dme, sizeof(dir_meta_entry_t)) &&
            !is_all_one(&dme, sizeof(dir_meta_entry_t))) {
            flash_block_idx next[NUM_NEXT_PTRS];
            memcpy(next, dme.next_block, sizeof(next));
            RESULT(block_idx) next_block = get_valid_next_block(fs, next);
            if (next_block.code != RET_OK) {
                R2F2_LOG_ERR("no valid next block");
                next_block.value = 0xdeadbeef;
                /* return; */
            }
            entry_flags_t flags;
            read_dir_meta_entry_flags(fs, dir_block, d, &flags);
            fprintf(f,
                    "{ \\\"%.*s\\\" | used=%d,comm=%d,\\\nrecl=%d | "
                    "<e%zu> %u | ",
                    MAX_PATH_LEN, dme.path,
                    is_entry_used(flags) == ENTRY_FLAG_SET,
                    is_entry_committed(flags) == ENTRY_FLAG_SET,
                    is_entry_reclaimable(flags) == ENTRY_FLAG_SET, d,
                    next_block.value);

            fprintf(f, " [%u, %u] } | ",
                    GET_FLASH_BLOCK_IDX(dme.next_block[0]).value,
                    GET_FLASH_BLOCK_IDX((dme.next_block[1])).value);
        }
    }

    fprintf(f, " }}\"\n];\n");

    for (size_t d = 0; d < NUM_DIR_META_ENTRIES; d++) {
        read_dir_meta_entry(fs, dir_block, d, &dme);
        if (!is_all_zero(&dme, sizeof(dir_meta_entry_t)) &&
            !is_all_one(&dme, sizeof(dir_meta_entry_t))) {
            flash_block_idx next[NUM_NEXT_PTRS];
            memcpy(next, dme.next_block, sizeof(next));
            RESULT(block_idx) next_block = get_valid_next_block(fs, next);
            if (next_block.code != RET_OK) {
                R2F2_LOG_ERR("no valid next block");
                return;
            }
            RESULT(uint32_t) block_type = get_block_type(fs, next_block.value);
            if (block_type.code != RET_OK) {
                return;
            }
            if (block_type.value == BLOCK_TYPE_DIR) {
                dump_dir_block(f, fs, next_block.value);
                fprintf(f, "\ndir_block_%u:e%zu -> dir_block_%u", dir_block, d,
                        next_block.value);
            } else if (block_type.value == BLOCK_TYPE_INDIR) {
                dump_file_indir_block(f, fs, dir_block, d, next_block.value);
            } else {
                dump_file_seq_block(f, fs, dir_block, d, next_block.value, 1);
            }
        }
    }

    flash_block_idx next[NUM_NEXT_PTRS];

    r2f2_ret read_ret = fs->cfg->flash_read(
        fs,
        dir_block * fs->cfg->geom.block_size + offsetof(dir_meta_block_t, next),
        sizeof(next), next);
    if (read_ret != RET_OK) {
        return;
    }

    RESULT(block_idx) next_block = get_valid_next_block(fs, next);
    if (next_block.code == RET_OK) {
        dump_dir_block(f, fs, next_block.value);
        fprintf(f, "\ndir_block_%u:db%u -> dir_block_%u", dir_block, dir_block,
                next_block.value);
    }
}

static inline void dump_fs_dot(r2f2_fs_t *fs, const char *filename) {
    FILE *f = fopen(filename, "w");
    fprintf(f, "digraph r2f2 {\n");
    fprintf(f, "graph [rankdir=TB, compound=true, labelloc=t, label=\"R2F2 "
               "tree\"];\n");
    fprintf(f, "node [style=\"filled\"];\n");

    block_idx dir_block = fs->root_dir_block;

    dump_dir_block(f, fs, dir_block);

    fprintf(f, "}");
    fclose(f);
}

#  define DEBUG_DUMP_FS_DOT(fs, filename) dump_fs_dot(fs, filename);

#else

#  define DEBUG_DUMP_FS_DOT(fs, filename) ((void)0)

#endif
