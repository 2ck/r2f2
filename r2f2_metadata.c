#include "r2f2_metadata.h"
#include "util/logger.h"
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

RESULT(block_idx) get_valid_next_block(r2f2_fs_t *fs,
                                       flash_block_idx *indices) {
    if (!indices) {
        return RESULT_ERR(block_idx, RET_EINVAL);
    }

    block_idx valid = 0;

    for (size_t i = 0; i < NUM_NEXT_PTRS; i++) {
        RESULT(block_idx) b = GET_FLASH_BLOCK_IDX(indices[i]);
        CHECK_OK_PROPAGATE(b, block_idx);
        /* this entry was unused, so no more valid entry can come */
        if (b.value >= fs->cfg->geom.num_blocks) {
            break;
        }

        if (b.value == 0) {
            continue;
        }

        valid = b.value;
    }
    if (valid != 0) {
        return RESULT_OK(block_idx, valid);
    } else {
        return RESULT_ERR(block_idx, RET_NOT_FOUND);
    }
}

entry_flags_t mark_entry_committed(entry_flags_t f) {
    return f & ~ENTRY_COMMIT_MASK;
}

entry_flags_t mark_entry_used(entry_flags_t f) {
    return f & ~ENTRY_USED_MASK;
}

entry_flags_t mark_entry_reclaimable(entry_flags_t f) {
    return f & ~ENTRY_RECLAIMABLE_MASK;
}

entry_flags_t mark_entry_indirect(entry_flags_t f) {
    return f & ~ENTRY_INDIRECT_MASK;
}

static entry_flag_state_t entry_flag_state(entry_flags_t f,
                                           entry_flags_t mask) {
#ifdef ECC_ON_METADATA
    uint32_t ones = __builtin_popcount((unsigned int)(f & mask));
    if (ones <= 1) {
        return ENTRY_FLAG_SET;
    } else if (ones >= 3) {
        return ENTRY_FLAG_UNSET;
    } else {
        return ENTRY_FLAG_INVALID;
    }
#else
    return ((f & mask) == 0) ? ENTRY_FLAG_SET : ENTRY_FLAG_CLEAR;
#endif
}

entry_flag_state_t is_entry_committed(entry_flags_t f) {
    return entry_flag_state(f, ENTRY_COMMIT_MASK);
}

entry_flag_state_t is_entry_used(entry_flags_t f) {
    return entry_flag_state(f, ENTRY_USED_MASK);
}

entry_flag_state_t is_entry_reclaimable(entry_flags_t f) {
    return entry_flag_state(f, ENTRY_RECLAIMABLE_MASK);
}

entry_flag_state_t is_entry_indirect(entry_flags_t f) {
    return entry_flag_state(f, ENTRY_INDIRECT_MASK);
}

r2f2_ret read_dir_meta_entry(r2f2_fs_t *fs, block_idx b, uint32_t idx,
                             dir_meta_entry_t *buf) {
    if (b > fs->cfg->geom.num_blocks || idx > NUM_DIR_META_ENTRIES) {
        R2F2_LOG_ERR("out of bounds block %u or entry %u", b, idx);
        return RET_OOB;
    }

    r2f2_ret ret = fs->cfg->flash_read(fs,
                                       b * fs->cfg->geom.block_size +
                                           offsetof(dir_meta_block_t, entries) +
                                           idx * sizeof(dir_meta_entry_t),
                                       sizeof(dir_meta_entry_t), buf);
    if (ret != RET_OK) {
        R2F2_LOG_ERR(
            "failed (%d) to read dir_meta_entry %u in block %u, buf %p", ret,
            idx, b, (void *)buf);
    }
    return ret;
}
r2f2_ret write_dir_meta_entry(r2f2_fs_t *fs, block_idx b, uint32_t idx,
                              dir_meta_entry_t *buf) {
    if (b > fs->cfg->geom.num_blocks || idx > NUM_DIR_META_ENTRIES) {
        R2F2_LOG_ERR("out of bounds block %u or entry %u", b, idx);
        return RET_OOB;
    }

    r2f2_ret ret = fs->cfg->flash_write(
        fs,
        b * fs->cfg->geom.block_size + offsetof(dir_meta_block_t, entries) +
            idx * sizeof(dir_meta_entry_t),
        sizeof(dir_meta_entry_t), buf);
    if (ret != RET_OK) {
        R2F2_LOG_ERR(
            "failed (%d) to write dir_meta_entry %u in block %u, buf %p", ret,
            idx, b, (void *)buf);
    }
    return ret;
}
r2f2_ret read_dir_meta_entry_flags(r2f2_fs_t *fs, block_idx b, uint32_t idx,
                                   entry_flags_t *buf) {
    if (b > fs->cfg->geom.num_blocks || idx > NUM_DIR_META_ENTRIES) {
        R2F2_LOG_ERR("out of bounds block %u or entry %u", b, idx);
        return RET_OOB;
    }

    r2f2_ret ret = fs->cfg->flash_read(fs,
                                       b * fs->cfg->geom.block_size +
                                           offsetof(dir_meta_block_t, f) +
                                           idx * sizeof(entry_flags_t),
                                       sizeof(entry_flags_t), buf);
    if (ret != RET_OK) {
        R2F2_LOG_ERR(
            "failed (%d) to read dir_meta_entry %u flags in block %u, buf %p",
            ret, idx, b, (void *)buf);
    }
    return ret;
}
r2f2_ret write_dir_meta_entry_flags(r2f2_fs_t *fs, block_idx b, uint32_t idx,
                                    entry_flags_t *buf) {
    if (b > fs->cfg->geom.num_blocks || idx > NUM_DIR_META_ENTRIES) {
        R2F2_LOG_ERR("out of bounds block %u or entry %u", b, idx);
        return RET_OOB;
    }

    r2f2_ret ret = fs->cfg->flash_write(fs,
                                        b * fs->cfg->geom.block_size +
                                            offsetof(dir_meta_block_t, f) +
                                            idx * sizeof(entry_flags_t),
                                        sizeof(entry_flags_t), buf);
    if (ret != RET_OK) {
        R2F2_LOG_ERR(
            "failed (%d) to write dir_meta_entry %u flags in block %u, buf %p",
            ret, idx, b, (void *)buf);
    }
    return ret;
}

r2f2_ret read_file_indir_entry(r2f2_fs_t *fs, block_idx b, uint32_t idx,
                               file_indir_entry_t *buf) {
    if (b > fs->cfg->geom.num_blocks || idx > NUM_FILE_INDIR_ENTRIES) {
        R2F2_LOG_ERR("out of bounds block %u or entry %u", b, idx);
        return RET_OOB;
    }

    r2f2_ret ret = fs->cfg->flash_read(
        fs,
        b * fs->cfg->geom.block_size + offsetof(file_indir_block_t, entries) +
            idx * sizeof(file_indir_entry_t),
        sizeof(file_indir_entry_t), buf);
    if (ret != RET_OK) {
        R2F2_LOG_ERR(
            "failed (%d) to read file_indir_entry %u in block %u, buf %p", ret,
            idx, b, (void *)buf);
    }
    return ret;
}
r2f2_ret write_file_indir_entry(r2f2_fs_t *fs, block_idx b, uint32_t idx,
                                file_indir_entry_t *buf) {
    if (b > fs->cfg->geom.num_blocks || idx > NUM_FILE_INDIR_ENTRIES) {
        R2F2_LOG_ERR("out of bounds block %u or entry %u", b, idx);
        return RET_OOB;
    }

    r2f2_ret ret = fs->cfg->flash_write(
        fs,
        b * fs->cfg->geom.block_size + offsetof(file_indir_block_t, entries) +
            idx * sizeof(file_indir_entry_t),
        sizeof(file_indir_entry_t), buf);
    if (ret != RET_OK) {
        R2F2_LOG_ERR(
            "failed (%d) to write file_indir_entry %u in block %u, buf %p", ret,
            idx, b, (void *)buf);
    }
    return ret;
}
r2f2_ret read_file_indir_entry_flags(r2f2_fs_t *fs, block_idx b, uint32_t idx,
                                     entry_flags_t *buf) {
    if (b > fs->cfg->geom.num_blocks || idx > NUM_FILE_INDIR_ENTRIES) {
        R2F2_LOG_ERR("out of bounds block %u or entry %u", b, idx);
        return RET_OOB;
    }

    r2f2_ret ret = fs->cfg->flash_read(fs,
                                       b * fs->cfg->geom.block_size +
                                           offsetof(file_indir_block_t, f) +
                                           idx * sizeof(file_indir_entry_t),
                                       sizeof(entry_flags_t), buf);
    if (ret != RET_OK) {
        R2F2_LOG_ERR("failed (%d) to read file_indir_entry %u flags in block "
                     "%u, buf %p",
                     ret, idx, b, (void *)buf);
    }
    return ret;
}
r2f2_ret write_file_indir_entry_flags(r2f2_fs_t *fs, block_idx b, uint32_t idx,
                                      entry_flags_t *buf) {
    if (b > fs->cfg->geom.num_blocks || idx > NUM_FILE_INDIR_ENTRIES) {
        R2F2_LOG_ERR("out of bounds block %u or entry %u", b, idx);
        return RET_OOB;
    }

    r2f2_ret ret = fs->cfg->flash_write(fs,
                                        b * fs->cfg->geom.block_size +
                                            offsetof(file_indir_block_t, f) +
                                            idx * sizeof(file_indir_entry_t),
                                        sizeof(entry_flags_t), buf);
    if (ret != RET_OK) {
        R2F2_LOG_ERR("failed (%d) to write file_indir_entry %u flags in block "
                     "%u, buf %p",
                     ret, idx, b, (void *)buf);
    }
    return ret;
}

r2f2_ret read_file_seq_entry(r2f2_fs_t *fs, block_idx b, uint32_t idx,
                             file_seq_entry_t *buf) {
    if (b > fs->cfg->geom.num_blocks || idx > NUM_FILE_SEQ_ENTRIES) {
        R2F2_LOG_ERR("out of bounds block %u or entry %u", b, idx);
        return RET_OOB;
    }

    r2f2_ret ret = fs->cfg->flash_read(fs,
                                       b * fs->cfg->geom.block_size +
                                           offsetof(file_seq_block_t, entries) +
                                           idx * sizeof(file_seq_entry_t),
                                       sizeof(file_seq_entry_t), buf);
    if (ret != RET_OK) {
        R2F2_LOG_ERR(
            "failed (%d) to read file_seq_entry %u in block %u, buf %p", ret,
            idx, b, (void *)buf);
    }
    return ret;
}
r2f2_ret write_file_seq_entry(r2f2_fs_t *fs, block_idx b, uint32_t idx,
                              file_seq_entry_t *buf) {
    if (b > fs->cfg->geom.num_blocks || idx > NUM_FILE_SEQ_ENTRIES) {
        R2F2_LOG_ERR("out of bounds block %u or entry %u", b, idx);
        return RET_OOB;
    }

    r2f2_ret ret = fs->cfg->flash_write(
        fs,
        b * fs->cfg->geom.block_size + offsetof(file_seq_block_t, entries) +
            idx * sizeof(file_seq_entry_t),
        sizeof(file_seq_entry_t), buf);
    if (ret != RET_OK) {
        R2F2_LOG_ERR(
            "failed (%d) to write file_seq_entry %u in block %u, buf %p", ret,
            idx, b, (void *)buf);
    }
    return ret;
}
r2f2_ret read_file_seq_entry_flags(r2f2_fs_t *fs, block_idx b, uint32_t idx,
                                   entry_flags_t *buf) {
    if (b > fs->cfg->geom.num_blocks || idx > NUM_FILE_SEQ_ENTRIES) {
        R2F2_LOG_ERR("out of bounds block %u or entry %u", b, idx);
        return RET_OOB;
    }

    r2f2_ret ret = fs->cfg->flash_read(
        fs,
        b * fs->cfg->geom.block_size + offsetof(file_seq_block_t, entries) +
            offsetof(file_seq_entry_t, f) + idx * sizeof(file_seq_entry_t),
        sizeof(entry_flags_t), buf);
    if (ret != RET_OK) {
        R2F2_LOG_ERR(
            "failed (%d) to read file_seq_entry %u flags in block %u, buf %p",
            ret, idx, b, (void *)buf);
    }
    return ret;
}
r2f2_ret write_file_seq_entry_flags(r2f2_fs_t *fs, block_idx b, uint32_t idx,
                                    entry_flags_t *buf) {
    if (b > fs->cfg->geom.num_blocks || idx > NUM_FILE_SEQ_ENTRIES) {
        R2F2_LOG_ERR("out of bounds block %u or entry %u", b, idx);
        return RET_OOB;
    }

    r2f2_ret ret = fs->cfg->flash_write(
        fs,
        b * fs->cfg->geom.block_size + offsetof(file_seq_block_t, entries) +
            offsetof(file_seq_entry_t, f) + idx * sizeof(file_seq_entry_t),
        sizeof(entry_flags_t), buf);
    if (ret != RET_OK) {
        R2F2_LOG_ERR(
            "failed (%d) to write file_seq_entry %u flags in block %u, buf %p",
            ret, idx, b, (void *)buf);
    }
    return ret;
}

bool is_fs_valid(r2f2_fs_t *fs, r2f2_fs_info_t *fs_info) {
    /* check magic */
    RESULT(uint32_t) magic = GET_FLASH_U32(fs_info->global_metadata.magic);
    if (magic.code != RET_OK) {
        return false;
    }
    if (magic.value != R2F2_MAGIC) {
        return false;
    }
    /* TODO: check geometry etc. */
    (void)fs;
    return true;
}

RESULT(uint32_t) get_free_dir_meta_entry(r2f2_fs_t *fs,
                                         block_idx dir_block_idx) {
    entry_flags_t dme_flags;
    /* initialize as unused (0xFF in flash) */
    memset(&dme_flags, 0xFF, sizeof(entry_flags_t));

    for (size_t i = 0; i < NUM_DIR_META_ENTRIES; i++) {
        r2f2_ret ret =
            read_dir_meta_entry_flags(fs, dir_block_idx, i, &dme_flags);
        if (ret != RET_OK) {
            R2F2_LOG_ERR("dir meta entry flags read failed");
            return RESULT_ERR(uint32_t, ret);
        }
        if (is_entry_used(dme_flags) == ENTRY_FLAG_UNSET) {
            return RESULT_OK(uint32_t, i);
        }
    }
    return RESULT_ERR(uint32_t, RET_NOMEM);
}
RESULT(uint32_t) get_free_file_indir_entry(r2f2_fs_t *fs,
                                           block_idx file_indir_block_idx) {
    entry_flags_t fie_flags;
    /* initialize as unused (0xFF in flash) */
    memset(&fie_flags, 0xFF, sizeof(entry_flags_t));

    for (size_t i = 0; i < NUM_FILE_INDIR_ENTRIES; i++) {
        r2f2_ret ret = read_file_indir_entry_flags(fs, file_indir_block_idx, i,
                                                   &fie_flags);
        if (ret != RET_OK) {
            R2F2_LOG_ERR("file indir entry flags read failed");
            return RESULT_ERR(uint32_t, ret);
        }
        if (is_entry_used(fie_flags) == ENTRY_FLAG_UNSET) {
            return RESULT_OK(uint32_t, i);
        }
    }
    return RESULT_ERR(uint32_t, RET_NOMEM);
}
RESULT(uint32_t) get_free_file_seq_entry(r2f2_fs_t *fs,
                                         block_idx file_seq_block_idx) {
    file_seq_entry_t fse;
    /* initialize as an unused entry (0xFF in flash) */
    memset(&fse, 0xFF, sizeof(file_seq_entry_t));

    for (size_t i = 0; i < NUM_FILE_SEQ_ENTRIES; i++) {
        r2f2_ret ret = read_file_seq_entry(fs, file_seq_block_idx, i, &fse);
        if (ret != RET_OK) {
            R2F2_LOG_ERR("file_seq_entry read failed");
            return RESULT_ERR(uint32_t, ret);
        }
        if (is_entry_used(fse.f) == ENTRY_FLAG_UNSET) {
            return RESULT_OK(uint32_t, i);
        }
    }
    return RESULT_ERR(uint32_t, RET_NOMEM);
}

RESULT(uint32_t) get_last_file_indir_entry(r2f2_fs_t *fs,
                                           block_idx file_indir_block_idx) {
    entry_flags_t f;
    uint32_t last_valid_entry = NUM_FILE_INDIR_ENTRIES;
    for (size_t i = 0; i < NUM_FILE_INDIR_ENTRIES; i++) {
        r2f2_ret ret =
            read_file_indir_entry_flags(fs, file_indir_block_idx, i, &f);
        if (ret != RET_OK) {
            return RESULT_ERR(uint32_t, ret);
        }

        /* after the first unused entry, no more valid entries can come */
        if (is_entry_used(f) == ENTRY_FLAG_UNSET) {
            break;
        }

        if (is_entry_committed(f) == ENTRY_FLAG_UNSET) {
            continue;
        }

        last_valid_entry = i;
    }
    if (last_valid_entry < NUM_FILE_INDIR_ENTRIES) {
        return RESULT_OK(uint32_t, last_valid_entry);
    } else {
        return RESULT_ERR(uint32_t, RET_NOT_FOUND);
    }
}

RESULT(uint32_t) get_last_file_seq_entry(r2f2_fs_t *fs,
                                         block_idx file_seq_block_idx) {
    entry_flags_t f;
    uint32_t last_valid_entry = NUM_FILE_SEQ_ENTRIES;
    for (size_t i = 0; i < NUM_FILE_SEQ_ENTRIES; i++) {
        r2f2_ret ret = read_file_seq_entry_flags(fs, file_seq_block_idx, i, &f);
        if (ret != RET_OK) {
            return RESULT_ERR(uint32_t, ret);
        }

        /* after the first unused entry, no more valid entries can come */
        if (is_entry_used(f) == ENTRY_FLAG_UNSET) {
            break;
        }

        if (is_entry_committed(f) == ENTRY_FLAG_UNSET) {
            continue;
        }

        last_valid_entry = i;
    }
    if (last_valid_entry < NUM_FILE_SEQ_ENTRIES) {
        return RESULT_OK(uint32_t, last_valid_entry);
    } else {
        return RESULT_ERR(uint32_t, RET_NOT_FOUND);
    }
}

RESULT(block_idx) find_data_block_for_off(r2f2_fs_t *fs,
                                          block_idx file_indir_block_idx,
                                          size_t off) {
    /*
     * For sequential writes, each subsequent 4096B are one data block aka
     * seq_entry further, so 128*4096B is one seq_block aka one indir_entry
     * further.
     *
     * In case we have fsynced after writing < 4096B, we have more metadata
     * entries than expected, so we may need to search ahead from our
     * expected entries.
     */

    size_t expected_indir_entry =
        off / (fs->cfg->geom.block_size * (NUM_FILE_SEQ_ENTRIES));
    size_t expected_seq_entry =
        (off % (fs->cfg->geom.block_size * (NUM_FILE_SEQ_ENTRIES))) /
        fs->cfg->geom.block_size;

    size_t start_from_indir_entry = expected_indir_entry;
    size_t start_from_seq_entry = expected_seq_entry;

    for (size_t i = start_from_indir_entry; i < NUM_FILE_INDIR_ENTRIES; i++) {
        entry_flags_t fie_flags;
        memset(&fie_flags, 0xFF, sizeof(entry_flags_t));
        r2f2_ret ret = read_file_indir_entry_flags(fs, file_indir_block_idx, i,
                                                   &fie_flags);
        if (ret != RET_OK) {
            return RESULT_ERR(block_idx, ret);
        }
        if (is_entry_used(fie_flags) == ENTRY_FLAG_SET &&
            is_entry_committed(fie_flags) == ENTRY_FLAG_SET) {
            file_indir_entry_t fie;
            ret = read_file_indir_entry(fs, file_indir_block_idx, i, &fie);
            if (ret != RET_OK) {
                return RESULT_ERR(block_idx, ret);
            }
            /* check the expected and subsequent indir entries */
            for (size_t j = start_from_seq_entry; j < NUM_FILE_SEQ_ENTRIES;
                 j++) {
                file_seq_entry_t fse;
                flash_block_idx next[NUM_NEXT_PTRS];
                memcpy(next, fie.seq_block, sizeof(next));
                RESULT(block_idx) seq_block = get_valid_next_block(fs, next);
                if (seq_block.code != RET_OK) {
                    return RESULT_ERR(block_idx, seq_block.code);
                }
                read_file_seq_entry(fs, seq_block.value, j, &fse);
                if (is_entry_used(fse.f) == ENTRY_FLAG_SET &&
                    is_entry_committed(fse.f) == ENTRY_FLAG_SET) {
                    /* R2F2_LOG_DEBUG( */
                    /*     "looking for offset %zu, block covers range %u -
                     * %u",
                     */
                    /*     off, fse.data_block_offset_in_file, */
                    /*     fse.data_block_offset_in_file + */
                    /*         fse.data_block_fill_level); */
                    RESULT(uint32_t) fse_off =
                        GET_FLASH_U32(fse.data_block_offset_in_file);
                    CHECK_OK_PROPAGATE(fse_off, block_idx);
                    RESULT(uint32_t) fse_fill =
                        GET_FLASH_U32(fse.data_block_fill_level);
                    CHECK_OK_PROPAGATE(fse_fill, block_idx);
                    if (fse_off.value == off ||
                        (fse_off.value <= off &&
                         fse_off.value + fse_fill.value >= off)) {
                        RESULT(block_idx) data_block =
                            GET_FLASH_BLOCK_IDX(fse.data_block);
                        return data_block;
                    }
                }
            }
        }
        /*
         * after the first seq_block, we search the next one starting from
         * entry 0, because the expected entry was only valid for the
         * previous block
         */
        start_from_seq_entry = 0;
    }

    R2F2_LOG_ERR("expected indir_entry %zu or expected seq_entry %zu not "
                 "correct, and "
                 "could not find correct entries",
                 expected_indir_entry, expected_seq_entry);
    return RESULT_ERR(block_idx, RET_NOT_FOUND);
}

RESULT(block_idx) find_data_block_for_off_direct(r2f2_fs_t *fs,
                                                 block_idx file_seq_block_idx,
                                                 size_t off) {
    size_t expected_seq_entry =
        (off % (fs->cfg->geom.block_size * (NUM_FILE_SEQ_ENTRIES))) /
        fs->cfg->geom.block_size;

    size_t start_from_seq_entry = expected_seq_entry;
    for (size_t j = start_from_seq_entry; j < NUM_FILE_SEQ_ENTRIES; j++) {
        file_seq_entry_t fse;
        read_file_seq_entry(fs, file_seq_block_idx, j, &fse);
        if (is_entry_used(fse.f) == ENTRY_FLAG_SET &&
            is_entry_committed(fse.f) == ENTRY_FLAG_SET) {
            RESULT(uint32_t) fse_off =
                GET_FLASH_U32(fse.data_block_offset_in_file);
            CHECK_OK_PROPAGATE(fse_off, block_idx);
            RESULT(uint32_t) fse_fill =
                GET_FLASH_U32(fse.data_block_fill_level);
            CHECK_OK_PROPAGATE(fse_fill, block_idx);
            if (fse_off.value == off ||
                (fse_off.value <= off &&
                 fse_off.value + fse_fill.value >= off)) {
                RESULT(block_idx) data_block =
                    GET_FLASH_BLOCK_IDX(fse.data_block);
                return data_block;
            }
        }
    }

    R2F2_LOG_ERR("expected direct seq_entry %zu not correct, and could not "
                 "find correct entries",
                 expected_seq_entry);
    return RESULT_ERR(block_idx, RET_NOT_FOUND);
}

r2f2_ret r2f2_get_file_dir_entry(r2f2_fs_t *fs, const char *path,
                                 dir_meta_entry_t *buf,
                                 struct dir_traversal_ret *ret) {
    RESULT(block_idx) dmb_ret = r2f2_find_dir_meta_block(fs, path);
    if (dmb_ret.code != RET_OK) {
        R2F2_LOG_ERR("traversal to dir_meta_block failed (%d) for path '%s'",
                     dmb_ret.code, path);
        return dmb_ret.code;
    }

    char file_basename[MAX_PATH_LEN];
    memset(file_basename, 0, MAX_PATH_LEN);
    const char *b = get_basename(path);
    if (!b) {
        R2F2_LOG_ERR("could not get basename for path '%s'", path);
        return RET_ERR;
    }
    /*
     * make sure to also copy '\0' terminator, important in case we don't have a
     * zeroed buffer at some point
     */
    memcpy(file_basename, b, strlen(b) + 1);

    /*
     * optionally iterate through the next block pointers until we find one with
     * a matching entry
     */
    bool has_next_block = true;
    block_idx dmb = dmb_ret.value;
    do {
        /* search through the entries of the current block */
        for (size_t i = 0; i < NUM_DIR_META_ENTRIES; i++) {
            /* first read and check the flags */
            entry_flags_t flags;
            r2f2_ret flags_ret = read_dir_meta_entry_flags(fs, dmb, i, &flags);
            if (flags_ret != RET_OK) {
                return flags_ret;
            }

            /* no more valid entries can come after this one */
            if (is_entry_used(flags) == ENTRY_FLAG_UNSET) {
                break;
            }
            if (is_entry_committed(flags) == ENTRY_FLAG_UNSET) {
                continue;
            }
            /*
             * the file in this entry is unlinked/removed, but it may have been
             * recreated, so keep on searching
             */
            if (is_entry_reclaimable(flags) == ENTRY_FLAG_SET) {
                continue;
            }

            /* now read the entry */
            r2f2_ret read_ret = read_dir_meta_entry(fs, dmb, i, buf);
            if (read_ret != RET_OK) {
                return read_ret;
            }

            if (memcmp(buf->path, file_basename, MAX_PATH_LEN) == 0) {
                ret->dme_idx = i;
                ret->dme_flags = flags;
                ret->dmb_idx = dmb;
                return RET_OK;
            }
        }

        /* get the next block, if it exists */
        flash_block_idx next[NUM_NEXT_PTRS];

        r2f2_ret read_ret = fs->cfg->flash_read(
            fs,
            dmb * fs->cfg->geom.block_size + offsetof(dir_meta_block_t, next),
            sizeof(next), next);
        if (read_ret != RET_OK) {
            return read_ret;
        }

        RESULT(block_idx) next_block = get_valid_next_block(fs, next);
        if (next_block.code == RET_OK) {
            dmb = next_block.value;
        } else {
            has_next_block = false;
        }
    } while (has_next_block);

    return RET_NOT_FOUND;
}

void dump_file_seq_block(FILE *f, r2f2_fs_t *fs, block_idx prev_block,
                         size_t prev_entry, block_idx file_seq_block,
                         bool direct) {
    fprintf(f,
            "\nfile_seq_block_%d [shape=record, fillcolor=\"#eadcf8\", "
            "\nlabel=\"{file_seq_block %u | { ",
            file_seq_block, file_seq_block);

    file_seq_entry_t fse;
    for (size_t i = 0; i < NUM_FILE_SEQ_ENTRIES; i++) {
        read_file_seq_entry(fs, file_seq_block, i, &fse);
        if (is_entry_used(fse.f) == ENTRY_FLAG_SET &&
            is_entry_committed(fse.f) == ENTRY_FLAG_SET) {
            fprintf(f, "{fill %u | offs %u | file size %u | data block %u} | ",
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

void dump_file_indir_block(FILE *f, r2f2_fs_t *fs, block_idx dir_block,
                           size_t dir_entry, block_idx file_indir_block) {
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

void dump_dir_block(FILE *f, r2f2_fs_t *fs, block_idx dir_block) {
    fprintf(f,
            "\ndir_block_%d [shape=record, fillcolor=\"#ffe599\", "
            "\nlabel=\"{dir_block %u | { ",
            dir_block, dir_block);

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
                return;
            }
            entry_flags_t flags;
            read_dir_meta_entry_flags(fs, dir_block, d, &flags);
            fprintf(f,
                    "{ \\\"%s\\\" | used=%d,comm=%d,\\\nindir=%d,recl=%d | "
                    "<e%zu> %u} | ",
                    dme.path, is_entry_used(flags) == ENTRY_FLAG_SET,
                    is_entry_committed(flags) == ENTRY_FLAG_SET,
                    is_entry_indirect(flags) == ENTRY_FLAG_SET,
                    is_entry_reclaimable(flags) == ENTRY_FLAG_SET, d,
                    next_block.value);
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
            entry_flags_t flags;
            read_dir_meta_entry_flags(fs, dir_block, d, &flags);
            if (is_entry_indirect(flags) == ENTRY_FLAG_SET) {
                dump_file_indir_block(f, fs, dir_block, d, next_block.value);
            } else {
                dump_file_seq_block(f, fs, dir_block, d, next_block.value, 1);
            }
        }
    }
}

void dump_fs_dot(r2f2_fs_t *fs, const char *filename) {
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
