#include "r2f2_defines.h"
#include "r2f2.h"
#include "util/helpers.h"
#include <string.h>

#ifdef ECC_ON_METADATA

RESULT(uint32_t) get_flash_u32(struct bch_control *bch, flash_u32 u) {
    if (!bch) {
        return RESULT_ERR(uint32_t, RET_EINVAL);
    }

    uint32_t err_loc[ECC_BCH_U32_T] = {0};
    int dec_ret =
        decode_bch(bch, u.data, sizeof(u.data), u.ecc, NULL, NULL, err_loc);
    if (dec_ret < 0) {
        if (is_close_to_all_one(&u, sizeof(flash_u32), ECC_BCH_U32_T)) {
            /*
             * This was probably uninitialized flash, it may even have a few
             * bits flipped, so we'll return 0xFFFFFFFF
             */
            uint32_t retval = UINT32_MAX;
            return RESULT_OK(uint32_t, retval);
        }
        return RESULT_ERR(uint32_t, RET_ECC_ERR);
    } else {
        for (int i = 0; i < dec_ret; i++) {
            uint32_t loc = err_loc[i];
            if (loc >= 8 * sizeof(u.data)) {
                /* error in ecc, can be ignored */
                continue;
            }
            u.data[loc / 8] ^= (1 << (loc % 8));
        }
    }
    uint32_t retval = ((uint32_t)u.data[0] << 0) | ((uint32_t)u.data[1] << 8) |
                      ((uint32_t)u.data[2] << 16) | ((uint32_t)u.data[3] << 24);
    return RESULT_OK(uint32_t, retval);
}

r2f2_ret correct_path(r2f2_fs_t *fs, char *path, void *path_ecc) {
    uint32_t path_err_loc[fs->path_bch->t];
    memset(path_err_loc, 0, sizeof(path_err_loc));
    int dec_ret = decode_bch(fs->path_bch, (uint8_t *)path, MAX_PATH_LEN,
                             path_ecc, NULL, NULL, path_err_loc);
    if (dec_ret < 0) {
        //return RET_ECC_ERR;
    } else {
        /* TODO: log corrected errors somewhere */
        /* TODO: write back correct value? */
        for (int i = 0; i < dec_ret; i++) {
            uint32_t loc = path_err_loc[i];
            if (loc >= 8 * MAX_PATH_LEN) {
                /* error in ecc, can be ignored */
                continue;
            }
            path[loc / 8] ^= (1 << (loc % 8));
        }
    }
    return RET_OK;
}

#endif
