#include "r2f2_defines.h"
#include "util/helpers.h"

#ifdef ECC_ON_METADATA

RESULT(uint32_t) get_flash_u32(struct bch_control *bch, flash_u32 u) {
    if (!bch) {
        return RESULT_ERR(uint32_t, RET_EINVAL);
    }

    uint32_t err_loc[ECC_BCH_U32_T] = {0};
    int dec_ret =
        decode_bch(bch, u.data, sizeof(u.data), u.ecc, NULL, NULL, err_loc);
    if (dec_ret < 0) {
        if (!is_all_one(&u, sizeof(flash_u32))) {
            return RESULT_ERR(uint32_t, RET_ECC_ERR);
        }
        /* we ignore errors due to uninitialized flash */
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

#endif
