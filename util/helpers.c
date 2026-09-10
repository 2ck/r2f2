#include "helpers.h"
#include "util/logger.h"
#include <string.h>

const char *get_basename(const char *path) {
    const char *last_slash = strrchr(path, '/');
    if (!last_slash) {
        return NULL;
    }
    /* move past slash */
    return last_slash + 1;
}

bool is_all_zero(const void *buf, size_t size) {
    const unsigned char *p = buf;

    for (size_t i = 0; i < size; i++) {
        if (p[i] != 0) {
            return false;
        }
    }

    return true;
}

bool is_all_one(const void *buf, size_t size) {
    const unsigned char *p = buf;

    for (size_t i = 0; i < size; i++) {
        if (p[i] != 0xFF) {
            return false;
        }
    }

    return true;
}

bool is_close_to_all_one(const void *buf, size_t size, unsigned max_flips) {
    const uint8_t *p = buf;
    unsigned zeros = 0;

    for (size_t i = 0; i < size; i++) {
        zeros += __builtin_popcount((unsigned)(uint8_t)~p[i]);
    }

    return zeros <= max_flips;
}

void r2f2_hexdump(const uint8_t *buf, size_t len) {
    size_t linenr = 0;
    const size_t breakafter = 32;
    for (size_t i = 0; i < len; i++) {
        R2F2_PRINTF("%.2X ", buf[i]);
        if ((i + 1) % breakafter == 0 || (i + 1) == len) {
            size_t remainder = (i + 1) % breakafter;
            if (remainder != 0) {
                for (size_t k = 0; k < breakafter - remainder; k++) {
                    R2F2_PRINTF("   ");
                }
            }
            R2F2_PRINTF("    ");
            for (size_t j = linenr * breakafter; j <= i; j++) {
                if (buf[j] >= 0x20 && buf[j] <= 0x7e) {
                    R2F2_PRINTF("%c", buf[j]);
                } else {
                    R2F2_PRINTF(".");
                }
            }
            R2F2_PRINTF("\n");
            linenr++;
        }
    }
}

r2f2_ret set_path_to_basename_zeroed(char *restrict dst,
                                     const char *restrict src) {
    const char *b = get_basename(src);
    if (!b) {
        R2F2_LOG_ERR("could not get basename for path '%s'", src);
        return RET_ERR;
    }
    size_t len = strlen(b);
    if (len >= MAX_PATH_LEN) {
        R2F2_LOG_ERR("basename for path '%s' too long", src);
        return RET_ERR;
    }
    memset(dst, 0, MAX_PATH_LEN);
    memcpy(dst, b, len);
    return RET_OK;
}
