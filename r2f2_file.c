#include "r2f2_file.h"
#include "r2f2_metadata.h"
#include "util/logger.h"
#include <string.h>

r2f2_ret r2f2_find_file(r2f2_fs_t *fs, const char *path) {
    RESULT(block_idx) ret = r2f2_traverse_dirs(fs, path);
    if (ret.code != RET_OK) {
        R2F2_LOG_ERR("dir traversal failed (%d) for path '%s'", ret.code, path);
        return ret.code;
    }

    block_idx file_dir_block_idx = ret.value;

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

    dir_meta_entry_t dme;
    for (size_t i = 0; i < NUM_DIR_META_ENTRIES; i++) {
        read_dir_entry(fs, file_dir_block_idx, i, &dme);
        if (memcmp(dme.path, file_basename, MAX_PATH_LEN) == 0) {
            R2F2_LOG_INFO("return %d", dme.next_block);
            return dme.next_block;
        }
    }

    return RET_ERR;
}

r2f2_ret r2f2_create_file(r2f2_fs_t *fs, const char *path) {
    RESULT(block_idx) ret = r2f2_traverse_dirs(fs, path);
    if (ret.code != RET_OK) {
        R2F2_LOG_ERR("dir traversal failed (%d) for path '%s'", ret.code, path);
        return ret.code;
    }
    /*
     * we assume the directory exists already
     * then, traverse_dirs returned the corresponding dir block
     */
    block_idx file_dir_block_idx = ret.value;
    RESULT(uint32_t) num = get_free_dir_meta_entry(fs, file_dir_block_idx);
    if (num.code != RET_OK) {
        R2F2_LOG_WARN(
            "TODO (unhandled): failed (%d) to get dir_entry in block %u",
            num.code, file_dir_block_idx);
        return RET_ERR;
    }

    /* create dir entry for file */
    dir_meta_entry_t dme;
    memset(&dme, 0xFF, sizeof(dir_meta_entry_t));
}

RESULT(block_idx) r2f2_traverse_dirs(r2f2_fs_t *fs, const char *path) {
    return RESULT_ERR(block_idx, RET_NOT_FOUND);
}
