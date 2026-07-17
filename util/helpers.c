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
