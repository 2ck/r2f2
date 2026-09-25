#pragma once

#include "r2f2_defines.h"

typedef struct r2f2_dir {
    block_idx first_block;
    block_idx current_block;
    uint16_t entry_index;
} r2f2_dir_t;

typedef enum {
	ENT_DIR,
	ENT_FILE,
} ent_type_t;
typedef struct r2f2_dirent {
    char name[MAX_PATH_LEN];
    ent_type_t type;
} r2f2_dirent_t;
