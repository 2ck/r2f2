#include "helpers.h"
#include <string.h>

const char *get_basename(const char *path) {
    const char *last_slash = strrchr(path, '/');
    if (!last_slash) {
        return NULL;
    }
    /* move past slash */
    return last_slash + 1;
}
