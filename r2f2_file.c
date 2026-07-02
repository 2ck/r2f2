#include "r2f2_file.h"
#include "r2f2_alloc.h"
#include "r2f2_metadata.h"
#include "util/logger.h"
#include <assert.h>
#include <string.h>

RESULT(block_idx) r2f2_find_dir_meta_block(r2f2_fs_t *fs, const char *path) {
    if (!path) {
        return RESULT_ERR(block_idx, RET_EINVAL);
    }

    /*
     * The path is of the form `/foo/bar/baz`, where the root dir block contains
     * the entry for the `bar` directory, and of course any other files that may
     * be in the root directory.
     */

    /* Therefore, we first have to sanity check that the path is valid */
    if (*path != '/') {
        R2F2_LOG_ERR("invalid path '%s'", path);
        return RESULT_ERR(block_idx, RET_EINVAL);
    }

    /* find number of directories in path, one for each '/' */
    const char *path_copy = path + 1;
    int32_t dir_depth = 0;
    while (*path_copy != '\0') {
        if (*path_copy == '/') {
            dir_depth++;

            /* we can't have two slashes after another */
            if (*(path_copy + 1) == '/') {
                R2F2_LOG_ERR("invalid path '%s'", path);
                return RESULT_ERR(block_idx, RET_EINVAL);
            }
        }

        path_copy++;
    }

    /*
     * we start in the root bock and also return it by default, in case our
     * depth is 0 so our file lies in the root directory
     */
    block_idx current_block = fs->root_dir_block;

    const char *seg_start = path + 1;
    const char *seg_end = seg_start;

    /*
     * Then we find the first directory and continue until there are no more
     * directories (until last `/`). If we are in the root directory, dir_depth
     * is = 0 and there is nothing to do here.
     */
    int32_t current_depth = dir_depth;
    while (current_depth-- > 0) {
        /* if we happen to iterate outside our valid path, we have hit a bug */
        assert(seg_end - path < MAX_PATH_LEN);
        /* figure out this directory's path segment */
        while (*seg_end != '\0') {
            /* slash found */
            if (*seg_end == '/') {
                break;
            }

            seg_end++;
        }

        size_t seg_len = seg_end - seg_start;
        if (seg_len == 0 || seg_len >= MAX_PATH_LEN) {
            R2F2_LOG_ERR("path segment length %zu invalid", seg_len);
            return RESULT_ERR(block_idx, RET_EINVAL);
        }

        char segment[MAX_PATH_LEN];
        /* the path we persist to flash has 0 instead of 0xFF in unused space */
        memset(segment, 0, sizeof(segment));
        memcpy(segment, seg_start, seg_len);

        dir_meta_entry_t dme;
        memset(&dme, 0xFF, sizeof(dir_meta_entry_t));

        bool found_entry = false;
        for (size_t i = 0; i < NUM_DIR_META_ENTRIES; i++) {
            read_dir_meta_entry(fs, current_block, i, &dme);

            /*
             * abort as soon as we find our first unused entry (no more valid
             * ones can come after)
             */
            if (!is_entry_used(dme.f)) {
                break;
            }

            if (memcmp(dme.path, segment, MAX_PATH_LEN) == 0) {
                current_block = dme.next_block;
                found_entry = true;
            }
        }

        if (!found_entry) {
            R2F2_LOG_ERR("could not find matching dir_meta_entry for path '%s' "
                         "segment '%s'",
                         path, segment);
            return RESULT_ERR(block_idx, RET_DIR_NOT_FOUND);
        }

        seg_end++;
        seg_start = seg_end;
    }
    return RESULT_OK(block_idx, current_block);
}

RESULT(block_idx) r2f2_find_file_meta_block(r2f2_fs_t *fs, const char *path) {
    RESULT(block_idx) ret = r2f2_find_dir_meta_block(fs, path);
    if (ret.code != RET_OK) {
        R2F2_LOG_ERR("traversal to dir_meta_block failed (%d) for path '%s'",
                     ret.code, path);
        return ret;
    }

    block_idx file_dir_block_idx = ret.value;

    char file_basename[MAX_PATH_LEN];
    memset(file_basename, 0, MAX_PATH_LEN);
    const char *b = get_basename(path);
    if (!b) {
        R2F2_LOG_ERR("could not get basename for path '%s'", path);
        return RESULT_ERR(block_idx, RET_ERR);
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
            return RESULT_OK(block_idx, dme.next_block);
        }
    }

    return RESULT_ERR(block_idx, RET_FILE_NOT_FOUND);
}

r2f2_ret r2f2_register_file(r2f2_fs_t *fs, const char *path) {
    RESULT(block_idx) dir_meta_block_idx = r2f2_find_dir_meta_block(fs, path);
    if (dir_meta_block_idx.code != RET_OK) {
        R2F2_LOG_ERR("dir traversal failed (%d) for path '%s'",
                     dir_meta_block_idx.code, path);
        return dir_meta_block_idx.code;
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
        return dme_num.code;
    }

    /*
     * we allocate a data block, which is pointed to by an indir entry,
     * in a block which in turn is pointed to by a file meta entry, in a block
     * which is finally pointed to by the dir entry
     */

    RESULT(block_idx) data_block_idx = allocate_block(fs);
    if (data_block_idx.code != RET_OK) {
        return data_block_idx.code;
    }

    file_indir_entry_t fie;
    fie.data_block = data_block_idx.value;
    mark_entry_used(&fie.f);

    RESULT(block_idx) file_indir_block_idx = allocate_block(fs);
    if (file_indir_block_idx.code != RET_OK) {
        return file_indir_block_idx.code;
    }
    r2f2_ret ret =
        write_file_indir_entry(fs, file_indir_block_idx.value, 0, &fie);
    if (ret != RET_OK) {
        return ret;
    }

    file_meta_entry_t fme;
    fme.indir_block = file_indir_block_idx.value;
    mark_entry_used(&fme.f);

    RESULT(block_idx) file_meta_block_idx = allocate_block(fs);
    if (file_meta_block_idx.code != RET_OK) {
        return file_meta_block_idx.code;
    }
    ret = write_file_meta_entry(fs, file_meta_block_idx.value, 0, &fme);
    if (ret != RET_OK) {
        return ret;
    }

    dir_meta_entry_t dme;
    memset(&dme, 0xFF, sizeof(dir_meta_entry_t));

    memset(dme.path, 0, MAX_PATH_LEN);
    const char *b = get_basename(path);
    if (!b) {
        R2F2_LOG_ERR("could not get basename for path '%s'", path);
        return RET_ERR;
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
        return ret;
    }

    /* commit, starting from the leaf to the root */
    mark_entry_committed(&fie.f);
    ret =
        write_file_indir_entry_flags(fs, file_indir_block_idx.value, 0, &fie.f);
    if (ret != RET_OK) {
        return ret;
    }
    mark_entry_committed(&fme.f);
    ret = write_file_meta_entry_flags(fs, file_meta_block_idx.value, 0, &fme.f);
    if (ret != RET_OK) {
        return ret;
    }
    mark_entry_committed(&dme.f);
    ret = write_dir_meta_entry_flags(fs, dir_meta_block_idx.value, 0, &dme.f);
    if (ret != RET_OK) {
        return ret;
    }

    return RET_OK;
}

RESULT(r2f2_fd) r2f2_create_fd(r2f2_fs_t *fs, const char *path) {
    (void)fs;
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
