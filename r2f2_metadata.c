#include "r2f2_metadata.h"
#include "util/logger.h"
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

void mark_entry_committed(entry_flags_t *f) {
    *f &= ~ENTRY_COMMIT_MASK;
}

void mark_entry_used(entry_flags_t *f) {
    *f &= ~ENTRY_USED_MASK;
}

bool is_entry_committed(entry_flags_t f) {
    return (f & ENTRY_COMMIT_MASK) == 0;
}

bool is_entry_used(entry_flags_t f) {
    return (f & ENTRY_USED_MASK) == 0;
}

r2f2_ret read_dir_meta_entry(r2f2_fs_t *fs, block_idx b, uint32_t idx,
                             void *buf) {
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
            idx, b, buf);
    }
    return ret;
}

r2f2_ret write_dir_meta_entry(r2f2_fs_t *fs, block_idx b, uint32_t idx,
                              void *buf) {
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
            idx, b, buf);
    }
    return ret;
}

r2f2_ret write_dir_meta_entry_flags(r2f2_fs_t *fs, block_idx b, uint32_t idx,
                                    void *buf) {
    if (b > fs->cfg->geom.num_blocks || idx > NUM_DIR_META_ENTRIES) {
        R2F2_LOG_ERR("out of bounds block %u or entry %u", b, idx);
        return RET_OOB;
    }

    r2f2_ret ret = fs->cfg->flash_write(
        fs,
        b * fs->cfg->geom.block_size + offsetof(dir_meta_block_t, entries) +
            offsetof(dir_meta_entry_t, f) + idx * sizeof(dir_meta_entry_t),
        sizeof(entry_flags_t), buf);
    if (ret != RET_OK) {
        R2F2_LOG_ERR(
            "failed (%d) to write dir_meta_entry %u flags in block %u, buf %p",
            ret, idx, b, buf);
    }
    return ret;
}

r2f2_ret read_file_indir_entry(r2f2_fs_t *fs, block_idx b, uint32_t idx,
                               void *buf) {
    if (b > fs->cfg->geom.num_blocks || idx > NUM_FILE_INDIR_ENTRIES) {
        R2F2_LOG_ERR("out of bounds block %u or entry %u", b, idx);
        return RET_OOB;
    }

    /* R2F2_LOG_DEBUG("read file_indir_entry %u in block %u, buf %p", idx, b,
     * buf); */
    /* R2F2_LOG_DEBUG("read at b %u * size %u + offset %zu + idx %u * size %zu",
     * b, */
    /*                fs->cfg->geom.block_size, */
    /*                offsetof(file_indir_block_t, entries), idx, */
    /*                sizeof(file_indir_entry_t)); */

    r2f2_ret ret = fs->cfg->flash_read(
        fs,
        b * fs->cfg->geom.block_size + offsetof(file_indir_block_t, entries) +
            idx * sizeof(file_indir_entry_t),
        sizeof(file_indir_entry_t), buf);
    if (ret != RET_OK) {
        R2F2_LOG_ERR(
            "failed (%d) to read file_indir_entry %u in block %u, buf %p", ret,
            idx, b, buf);
    }
    return ret;
}
r2f2_ret write_file_indir_entry(r2f2_fs_t *fs, block_idx b, uint32_t idx,
                                void *buf) {
    if (b > fs->cfg->geom.num_blocks || idx > NUM_FILE_INDIR_ENTRIES) {
        R2F2_LOG_ERR("out of bounds block %u or entry %u", b, idx);
        return RET_OOB;
    }

    /* R2F2_LOG_DEBUG("write file_indir_entry %u in block %u, buf %p", idx, b,
     */
    /*                buf); */
    /* R2F2_LOG_DEBUG("write at b %u * size %u + offset %zu + idx %u * size
     * %zu", */
    /*                b, fs->cfg->geom.block_size, */
    /*                offsetof(file_indir_block_t, entries), idx, */
    /*                sizeof(file_indir_entry_t)); */

    r2f2_ret ret = fs->cfg->flash_write(
        fs,
        b * fs->cfg->geom.block_size + offsetof(file_indir_block_t, entries) +
            idx * sizeof(file_indir_entry_t),
        sizeof(file_indir_entry_t), buf);
    if (ret != RET_OK) {
        R2F2_LOG_ERR(
            "failed (%d) to write file_indir_entry %u in block %u, buf %p", ret,
            idx, b, buf);
    }
    return ret;
}
r2f2_ret read_file_indir_entry_flags(r2f2_fs_t *fs, block_idx b, uint32_t idx,
                                     void *buf) {
    if (b > fs->cfg->geom.num_blocks || idx > NUM_FILE_INDIR_ENTRIES) {
        R2F2_LOG_ERR("out of bounds block %u or entry %u", b, idx);
        return RET_OOB;
    }

    r2f2_ret ret = fs->cfg->flash_read(
        fs,
        b * fs->cfg->geom.block_size + offsetof(file_indir_block_t, entries) +
            offsetof(file_indir_entry_t, f) + idx * sizeof(file_indir_entry_t),
        sizeof(entry_flags_t), buf);
    if (ret != RET_OK) {
        R2F2_LOG_ERR("failed (%d) to read file_indir_entry %u flags in block "
                     "%u, buf %p",
                     ret, idx, b, buf);
    }
    return ret;
}
r2f2_ret write_file_indir_entry_flags(r2f2_fs_t *fs, block_idx b, uint32_t idx,
                                      void *buf) {
    if (b > fs->cfg->geom.num_blocks || idx > NUM_FILE_INDIR_ENTRIES) {
        R2F2_LOG_ERR("out of bounds block %u or entry %u", b, idx);
        return RET_OOB;
    }

    r2f2_ret ret = fs->cfg->flash_write(
        fs,
        b * fs->cfg->geom.block_size + offsetof(file_indir_block_t, entries) +
            offsetof(file_indir_entry_t, f) + idx * sizeof(file_indir_entry_t),
        sizeof(entry_flags_t), buf);
    if (ret != RET_OK) {
        R2F2_LOG_ERR("failed (%d) to write file_indir_entry %u flags in block "
                     "%u, buf %p",
                     ret, idx, b, buf);
    }
    return ret;
}

r2f2_ret read_file_meta_entry(r2f2_fs_t *fs, block_idx b, uint32_t idx,
                              void *buf) {
    if (b > fs->cfg->geom.num_blocks || idx > NUM_FILE_META_ENTRIES) {
        R2F2_LOG_ERR("out of bounds block %u or entry %u", b, idx);
        return RET_OOB;
    }

    r2f2_ret ret = fs->cfg->flash_read(
        fs,
        b * fs->cfg->geom.block_size + offsetof(file_meta_block_t, entries) +
            idx * sizeof(file_meta_entry_t),
        sizeof(file_meta_entry_t), buf);
    if (ret != RET_OK) {
        R2F2_LOG_ERR(
            "failed (%d) to read file_meta_entry %u in block %u, buf %p", ret,
            idx, b, buf);
    }
    return ret;
}
r2f2_ret write_file_meta_entry(r2f2_fs_t *fs, block_idx b, uint32_t idx,
                               void *buf) {
    if (b > fs->cfg->geom.num_blocks || idx > NUM_FILE_META_ENTRIES) {
        R2F2_LOG_ERR("out of bounds block %u or entry %u", b, idx);
        return RET_OOB;
    }

    r2f2_ret ret = fs->cfg->flash_write(
        fs,
        b * fs->cfg->geom.block_size + offsetof(file_meta_block_t, entries) +
            idx * sizeof(file_meta_entry_t),
        sizeof(file_meta_entry_t), buf);
    if (ret != RET_OK) {
        R2F2_LOG_ERR(
            "failed (%d) to write file_meta_entry %u in block %u, buf %p", ret,
            idx, b, buf);
    }
    return ret;
}
r2f2_ret read_file_meta_entry_flags(r2f2_fs_t *fs, block_idx b, uint32_t idx,
                                    void *buf) {
    if (b > fs->cfg->geom.num_blocks || idx > NUM_FILE_META_ENTRIES) {
        R2F2_LOG_ERR("out of bounds block %u or entry %u", b, idx);
        return RET_OOB;
    }

    r2f2_ret ret = fs->cfg->flash_read(
        fs,
        b * fs->cfg->geom.block_size + offsetof(file_meta_block_t, entries) +
            offsetof(file_meta_entry_t, f) + idx * sizeof(file_meta_entry_t),
        sizeof(entry_flags_t), buf);
    if (ret != RET_OK) {
        R2F2_LOG_ERR(
            "failed (%d) to read file_meta_entry %u flags in block %u, buf %p",
            ret, idx, b, buf);
    }
    return ret;
}
r2f2_ret write_file_meta_entry_flags(r2f2_fs_t *fs, block_idx b, uint32_t idx,
                                     void *buf) {
    if (b > fs->cfg->geom.num_blocks || idx > NUM_FILE_META_ENTRIES) {
        R2F2_LOG_ERR("out of bounds block %u or entry %u", b, idx);
        return RET_OOB;
    }

    r2f2_ret ret = fs->cfg->flash_write(
        fs,
        b * fs->cfg->geom.block_size + offsetof(file_meta_block_t, entries) +
            offsetof(file_meta_entry_t, f) + idx * sizeof(file_meta_entry_t),
        sizeof(entry_flags_t), buf);
    if (ret != RET_OK) {
        R2F2_LOG_ERR(
            "failed (%d) to write file_meta_entry %u flags in block %u, buf %p",
            ret, idx, b, buf);
    }
    return ret;
}

bool is_fs_valid(r2f2_fs_t *fs, r2f2_fs_info_t *fs_info) {
    /* check magic */
    if (fs_info->global_metadata.magic != R2F2_MAGIC) {
        return false;
    }
    /* TODO: check geometry etc. */
    (void)fs;
    return true;
}

RESULT(uint32_t) get_free_dir_meta_entry(r2f2_fs_t *fs,
                                         block_idx dir_block_idx) {
    dir_meta_entry_t dme;
    /* initialize as an unused entry (0xFF in flash) */
    memset(&dme, 0xFF, sizeof(dir_meta_entry_t));

    for (size_t i = 0; i < NUM_DIR_META_ENTRIES; i++) {
        r2f2_ret ret = read_dir_meta_entry(fs, dir_block_idx, i, &dme);
        if (ret != RET_OK) {
            R2F2_LOG_ERR("dir meta entry read failed");
            return RESULT_ERR(uint32_t, ret);
        }
        if (!is_entry_used(dme.f)) {
            return RESULT_OK(uint32_t, i);
        }
    }
    return RESULT_ERR(uint32_t, RET_NOMEM);
}
RESULT(uint32_t) get_free_file_indir_entry(r2f2_fs_t *fs,
                                           block_idx file_indir_block_idx) {
    file_indir_entry_t fie;
    /* initialize as an unused entry (0xFF in flash) */
    memset(&fie, 0xFF, sizeof(file_indir_entry_t));

    for (size_t i = 0; i < NUM_FILE_INDIR_ENTRIES; i++) {
        r2f2_ret ret = read_file_indir_entry(fs, file_indir_block_idx, i, &fie);
        if (ret != RET_OK) {
            R2F2_LOG_ERR("file indir entry read failed");
            return RESULT_ERR(uint32_t, ret);
        }
        if (!is_entry_used(fie.f)) {
            return RESULT_OK(uint32_t, i);
        }
    }
    return RESULT_ERR(uint32_t, RET_NOMEM);
}
RESULT(uint32_t) get_free_file_meta_entry(r2f2_fs_t *fs,
                                          block_idx file_meta_block_idx) {
    file_meta_entry_t fme;
    /* initialize as an unused entry (0xFF in flash) */
    memset(&fme, 0xFF, sizeof(file_meta_entry_t));

    for (size_t i = 0; i < NUM_FILE_META_ENTRIES; i++) {
        r2f2_ret ret = read_file_meta_entry(fs, file_meta_block_idx, i, &fme);
        if (ret != RET_OK) {
            R2F2_LOG_ERR("file meta entry read failed");
            return RESULT_ERR(uint32_t, ret);
        }
        if (!is_entry_used(fme.f)) {
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
        if (!is_entry_used(f)) {
            break;
        }

        if (!is_entry_committed(f)) {
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

RESULT(uint32_t) get_last_file_meta_entry(r2f2_fs_t *fs,
                                          block_idx file_meta_block_idx) {
    entry_flags_t f;
    uint32_t last_valid_entry = NUM_FILE_META_ENTRIES;
    for (size_t i = 0; i < NUM_FILE_META_ENTRIES; i++) {
        r2f2_ret ret =
            read_file_meta_entry_flags(fs, file_meta_block_idx, i, &f);
        if (ret != RET_OK) {
            return RESULT_ERR(uint32_t, ret);
        }

        /* after the first unused entry, no more valid entries can come */
        if (!is_entry_used(f)) {
            break;
        }

        if (!is_entry_committed(f)) {
            continue;
        }

        last_valid_entry = i;
    }
    if (last_valid_entry < NUM_FILE_META_ENTRIES) {
        return RESULT_OK(uint32_t, last_valid_entry);
    } else {
        return RESULT_ERR(uint32_t, RET_NOT_FOUND);
    }
}

RESULT(uint32_t) find_data_block_for_off(r2f2_fs_t *fs,
                                         block_idx file_indir_block_idx,
                                         size_t off) {
    /*
     * For sequential writes, the first meta_entry is for file creation.
     * Each subsequent 4096B are one data block aka meta_entry further, so
     * 128*4096B is one meta_block aka one indir_entry further.
     *
     * In case we have fsynced after writing < 4096B, we have more metadata
     * entries than expected, so we may need to search ahead from our expected
     * entries.
     */

    size_t expected_indir_entry =
        (off + fs->cfg->geom.block_size - 1) /
        (fs->cfg->geom.block_size * (NUM_FILE_META_ENTRIES));
    size_t expected_meta_entry =
        ((off + fs->cfg->geom.block_size - 1) %
         (fs->cfg->geom.block_size * (NUM_FILE_META_ENTRIES))) /
        fs->cfg->geom.block_size;

    size_t start_from_indir_entry = expected_indir_entry;
    size_t start_from_meta_entry = expected_meta_entry;

    for (size_t i = start_from_indir_entry; i < NUM_FILE_INDIR_ENTRIES; i++) {
        file_indir_entry_t fie;
        read_file_indir_entry(fs, file_indir_block_idx, i, &fie);
        if (is_entry_used(fie.f) && is_entry_committed(fie.f)) {
            /* check the expected and subsequent indir entries */
            for (size_t j = start_from_meta_entry; j < NUM_FILE_META_ENTRIES;
                 j++) {
                file_meta_entry_t fme;
                read_file_meta_entry(fs, fie.meta_block, j, &fme);
                if (is_entry_used(fme.f) && is_entry_committed(fme.f)) {
                    /* R2F2_LOG_DEBUG( */
                    /*     "looking for offset %zu, block covers range %u - %u",
                     */
                    /*     off, fme.data_block_offset_in_file, */
                    /*     fme.data_block_offset_in_file + */
                    /*         fme.data_block_fill_level); */
                    if (fme.data_block_offset_in_file == off ||
                        (fme.data_block_offset_in_file <= off &&
                         fme.data_block_offset_in_file +
                                 fme.data_block_fill_level >=
                             off)) {
                        return RESULT_OK(uint32_t, fme.data_block);
                    }
                }
            }
        }
        /*
         * after the first meta_block, we search the next one starting from
         * entry 0, because the expected entry was only valid for the
         * previous block
         */
        start_from_meta_entry = 0;
    }

    R2F2_LOG_ERR(
        "expected indir_entry %zu or expected meta_entry %zu not correct, and "
        "could not find correct entries",
        expected_indir_entry, expected_meta_entry);
    return RESULT_ERR(uint32_t, RET_NOT_FOUND);
}
