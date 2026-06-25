#include "r2f2_file.h"
#include "r2f2_alloc.h"
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
        read_dir_meta_entry(fs, file_dir_block_idx, i, &dme);
        if (memcmp(dme.path, file_basename, MAX_PATH_LEN) == 0) {
            R2F2_LOG_INFO("return %d", dme.next_block);
            return dme.next_block;
        }
    }

    return RET_ERR;
}

RESULT(r2f2_fd) r2f2_create_file(r2f2_fs_t *fs, const char *path) {
    RESULT(block_idx) dir_meta_block_idx = r2f2_traverse_dirs(fs, path);
    if (dir_meta_block_idx.code != RET_OK) {
        R2F2_LOG_ERR("dir traversal failed (%d) for path '%s'",
                     dir_meta_block_idx.code, path);
        return RESULT_ERR(r2f2_fd, dir_meta_block_idx.code);
    }
    /*
     * we assume the directory exists already
     * then, traverse_dirs returned the corresponding dir block
     */
    RESULT(uint32_t) dme_num =
        get_free_dir_meta_entry(fs, dir_meta_block_idx.value);
    if (dme_num.code != RET_OK) {
        R2F2_LOG_WARN(
            "TODO (unhandled): failed (%d) to get dir_entry in block %u",
            dme_num.code, dir_meta_block_idx.value);
        return RESULT_ERR(r2f2_fd, dme_num.code);
    }

    /*
     * we allocate a data block, which is pointed to by an indir entry,
     * in a block which in turn is pointed to by a file meta entry, in a block
     * which is finally pointed to by the dir entry
     */

    RESULT(block_idx) data_block_idx = allocate_block(fs);
    if (data_block_idx.code != RET_OK) {
        return RESULT_ERR(r2f2_fd, data_block_idx.code);
    }

    file_indir_entry_t fie;
    fie.data_block = data_block_idx.value;
    mark_entry_used(&fie.f);

    RESULT(block_idx) file_indir_block_idx = allocate_block(fs);
    if (file_indir_block_idx.code != RET_OK) {
        return RESULT_ERR(r2f2_fd, file_indir_block_idx.code);
    }
    r2f2_ret ret =
        write_file_indir_entry(fs, file_indir_block_idx.value, 0, &fie);
    if (ret != RET_OK) {
        return RESULT_ERR(r2f2_fd, ret);
    }

    file_meta_entry_t fme;
    fme.indir_block = file_indir_block_idx.value;
    mark_entry_used(&fme.f);

    RESULT(block_idx) file_meta_block_idx = allocate_block(fs);
    if (file_meta_block_idx.code != RET_OK) {
        return RESULT_ERR(r2f2_fd, file_meta_block_idx.code);
    }
    ret = write_file_meta_entry(fs, file_meta_block_idx.value, 0, &fme);
    if (ret != RET_OK) {
        return RESULT_ERR(r2f2_fd, ret);
    }

    dir_meta_entry_t dme;
    memset(&dme, 0xFF, sizeof(dir_meta_entry_t));

    memset(dme.path, 0, MAX_PATH_LEN);
    const char *b = get_basename(path);
    if (!b) {
        R2F2_LOG_ERR("could not get basename for path '%s'", path);
        return RESULT_ERR(r2f2_fd, RET_ERR);
    }
    /*
     * make sure to also copy '\0' terminator, important in case we don't have a
     * zeroed buffer at some point
     */
    memcpy(dme.path, b, strlen(b) + 1);

    dme.next_block = file_indir_block_idx.value;

    mark_entry_used(&dme.f);

    ret =
        write_dir_meta_entry(fs, dir_meta_block_idx.value, dme_num.value, &dme);
    if (ret != RET_OK) {
        return RESULT_ERR(r2f2_fd, ret);
    }

    /* commit, starting from the leaf to the root */
    mark_entry_committed(&fie.f);
    ret =
        write_file_indir_entry_flags(fs, file_indir_block_idx.value, 0, &fie.f);
    if (ret != RET_OK) {
        return RESULT_ERR(r2f2_fd, ret);
    }
    mark_entry_committed(&fme.f);
    ret = write_file_meta_entry_flags(fs, file_meta_block_idx.value, 0, &fme.f);
    if (ret != RET_OK) {
        return RESULT_ERR(r2f2_fd, ret);
    }
    mark_entry_committed(&dme.f);
    ret = write_dir_meta_entry_flags(fs, dir_meta_block_idx.value, 0, &dme.f);
    if (ret != RET_OK) {
        return RESULT_ERR(r2f2_fd, ret);
    }

    RESULT(r2f2_fd) fd = r2f2_create_fd(fs, path);
    if (fd.code != RET_OK) {
        R2F2_LOG_ERR("failed (%d) to get fd for path '%s'", fd.code, path);
        return RESULT_ERR(r2f2_fd, fd.code);
    }
    return RESULT_ERR(r2f2_fd, fd.value);
}

RESULT(block_idx) r2f2_traverse_dirs(r2f2_fs_t *fs, const char *path) {
    R2F2_LOG_WARN("TODO: unimplemented, always returns root_dir_block");
    return RESULT_OK(block_idx, fs->root_dir_block);
    /* return RESULT_ERR(block_idx, RET_NOT_FOUND); */
}

RESULT(r2f2_fd) r2f2_create_fd(r2f2_fs_t *fs, const char *path) {
    for (size_t i = 0; i < MAX_NUM_FDS; i++) {
        if (fds[i].active) {
            continue;
        }
        fds[i].active = true;
        memcpy(fds[i].path, path, MAX_PATH_LEN);
        return RESULT_OK(r2f2_fd, i);
    }
    return RESULT_ERR(r2f2_fd, RET_NOMEM);
}
