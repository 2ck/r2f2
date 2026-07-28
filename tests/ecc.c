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

    return 0;
}
