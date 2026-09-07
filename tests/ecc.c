#include "bch.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define U32_LE(a)                                                              \
    ((uint32_t)(a)[0] | (uint32_t)(a)[1] << 8 | (uint32_t)(a)[2] << 16 |       \
     (uint32_t)(a)[3] << 24)

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    printf("======== BCH variant one (u32). ========\n");

    struct bch_control *bch;
    int m = 6;
    int t = 4;
    bch = init_bch(m, t, 0);
    assert(bch);
    printf("ecc bits %d (bytes %d)\n", bch->ecc_bits, bch->ecc_bytes);

    uint8_t data_buf[4];
    uint32_t block_idx = 420;
    memcpy(data_buf, &block_idx, sizeof(data_buf));

    uint8_t ecc_buf[bch->ecc_bytes];
    memset(ecc_buf, 0, sizeof(ecc_buf));

    printf("before encode: data %u ecc [0x%x, 0x%x, 0x%x]\n", U32_LE(data_buf),
           ecc_buf[0], ecc_buf[1], ecc_buf[2]);
    encode_bch(bch, data_buf, sizeof(data_buf), ecc_buf);
    printf("after encode: data %u ecc [0x%x, 0x%x, 0x%x]\n", U32_LE(data_buf),
           ecc_buf[0], ecc_buf[1], ecc_buf[2]);

    uint32_t err_loc[bch->t];
    memset(err_loc, 0, sizeof(err_loc));
    data_buf[0] ^= (1 << 3);
    data_buf[1] ^= (1 << 6);
    data_buf[1] ^= (1 << 2);
    data_buf[3] ^= (1 << 1);
    printf("introduced some errors, data now %u\n", U32_LE(data_buf));
    int dec_ret = decode_bch(bch, data_buf, sizeof(data_buf), ecc_buf, NULL,
                             NULL, err_loc);
    if (dec_ret < 0) {
        printf("decode failed (%d)\n", dec_ret);
        return dec_ret;
    }

    printf("decode ret %d, err_loc [%u, %u, %u, %u]\n", dec_ret, err_loc[0],
           err_loc[1], err_loc[2], err_loc[3]);

    for (int i = 0; i < dec_ret; i++) {
        uint32_t loc = err_loc[i];
        data_buf[loc / 8] ^= (1 << (loc % 8));
    }
    printf("corrected data: %u\n", U32_LE(data_buf));
    free_bch(bch);

    printf("======== BCH variant two (str). ========\n");

    m = 10;
    t = 12;
    bch = init_bch(m, t, 0);
    assert(bch);
    printf("ecc bits %d (bytes %d)\n", bch->ecc_bits, bch->ecc_bytes);

    const int PATH_LEN = 95;
    uint8_t path_buf[PATH_LEN];
    memset(path_buf, 0, sizeof(path_buf));
    for (size_t i = 0; i < sizeof(path_buf); i++) {
        path_buf[i] = 65 + (i % 26);
    }

    uint8_t path_ecc_buf[bch->ecc_bytes];
    memset(path_ecc_buf, 0, sizeof(path_ecc_buf));

    printf("before encode: data '%.*s' ecc [", (int)sizeof(path_buf), path_buf);
    for (size_t i = 0; i < bch->ecc_bytes; i++) {
        if (i != 0) {
            printf(", ");
        }
        printf("0x%x", path_ecc_buf[i]);
    }
    printf("]\n");

    encode_bch(bch, path_buf, sizeof(path_buf), path_ecc_buf);

    printf("after encode:  data '%.*s' ecc [", (int)sizeof(path_buf), path_buf);
    for (size_t i = 0; i < bch->ecc_bytes; i++) {
        if (i != 0) {
            printf(", ");
        }
        printf("0x%x", path_ecc_buf[i]);
    }
    printf("]\n");

    uint32_t path_err_loc[bch->t];
    memset(path_err_loc, 0, sizeof(path_err_loc));
    path_buf[0] ^= (1 << 0);
    path_buf[1] ^= (1 << 6);
    path_buf[11] ^= (1 << 2);
    path_buf[3] ^= (1 << 1);
    path_buf[4] ^= (1 << 4);
    path_buf[25] ^= (1 << 7);
    path_buf[6] ^= (1 << 3);
    path_buf[6] ^= (1 << 2);
    path_buf[7] ^= (1 << 2);
    path_buf[8] ^= (1 << 5);
    path_buf[20] ^= (1 << 1);
    path_buf[48] ^= (1 << 0);
    printf("introduced some errors, data now '%.*s'\n", (int)sizeof(path_buf),
           path_buf);
    dec_ret = decode_bch(bch, path_buf, sizeof(path_buf), path_ecc_buf, NULL,
                         NULL, path_err_loc);
    if (dec_ret < 0) {
        printf("decode failed (%d)\n", dec_ret);
        return dec_ret;
    }

    printf("decode ret %d, err_loc [", dec_ret);
    for (int i = 0; i < dec_ret; i++) {
        if (i != 0) {
            printf(", ");
        }
        printf("%u", path_err_loc[i]);
    }
    printf("]\n");

    for (int i = 0; i < dec_ret; i++) {
        uint32_t loc = path_err_loc[i];
        path_buf[loc / 8] ^= (1 << (loc % 8));
    }
    printf("corrected data: '%.*s'\n", (int)sizeof(path_buf), path_buf);
    free_bch(bch);

    return 0;
}
