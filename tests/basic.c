#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "r2f2.h"

#define FLASH_NUM_BLOCKS (32768U)
#define FLASH_BLOCK_SIZE (4096U)
#define FLASH_PAGE_SIZE (256U)

r2f2_ret flash_read(r2f2_fs_t *fs, uint32_t addr, uint32_t len, void *buf) {
    void *src = (uint8_t *)fs->cfg->user_ctx + addr;
    memcpy(buf, src, len);
    return RET_OK;
}
r2f2_ret flash_write(r2f2_fs_t *fs, uint32_t addr, uint32_t len, void *buf) {
    void *dst = (uint8_t *)fs->cfg->user_ctx + addr;
    memcpy(dst, buf, len);
    return RET_OK;
}
r2f2_ret flash_erase(r2f2_fs_t *fs, uint32_t addr, uint32_t len) {
    void *dst = (uint8_t *)fs->cfg->user_ctx + addr;
    memset(dst, 0xFF, len);
    return RET_OK;
}

int main(int argc, char **argv) {
    void *flash = malloc(FLASH_NUM_BLOCKS * FLASH_BLOCK_SIZE);
    if (!flash) {
        printf("Error during malloc.");
        return -1;
    }
    memset(flash, 0xFF, FLASH_NUM_BLOCKS * FLASH_BLOCK_SIZE);

    r2f2_cfg_t cfg = {
        .user_ctx = flash,
        .flash_read = flash_read,
        .flash_write = flash_write,
        .flash_erase = flash_erase,
        .geom =
            {
                .page_size = FLASH_PAGE_SIZE,
                .block_size = FLASH_BLOCK_SIZE,
                .num_blocks = FLASH_NUM_BLOCKS,
            },
    };

    r2f2_fs_t fs = {
        .cfg = &cfg,
    };

    {
        int ret = r2f2_mount(&fs);
        if (ret != RET_OK) {
            printf("R2F2 mount failed (%d)\n", ret);
        } else {
            printf("R2F2 mount okay\n");
        }
    }

    int fd;
    /* should succeed */
    {
        const char *filename = "/testfile";
        int ret = r2f2_open(&fs, filename, O_CREAT | O_RDWR);
        if (ret != RET_OK) {
            printf("R2F2 open file '%s' failed (%d)\n", filename, ret);
            return ret;
        } else {
            printf("R2F2 open file '%s' okay\n", filename);
            fd = ret;
        }
    }

    /* should succeed */
    {
        int ret = r2f2_close(&fs, ret);
        if (ret != RET_OK) {
            printf("R2F2 close fd %d failed (%d)\n", fd, ret);
            return ret;
        } else {
            printf("R2F2 close fd %d okay\n", fd);
        }
    }

    /* should succeed */
    {
        const char *filename = "/testfile";
        int ret = r2f2_open(&fs, filename, O_RDWR);
        if (ret != RET_OK) {
            printf("R2F2 open file '%s' failed (%d)\n", filename, ret);
            return ret;
        } else {
            printf("R2F2 open file '%s' okay\n", filename);
            fd = ret;
        }
    }

    /* should fail */
    {
        const char *filename = "/foo/bar/testfile";
        int ret = r2f2_open(&fs, filename, O_CREAT | O_RDWR);
        if (ret == RET_OK) {
            printf("R2F2 open file '%s' succeeded when it shouldn't have "
                   "(missing directory)\n",
                   filename);
            return RET_ERR;
        } else {
            printf(
                "R2F2 open file '%s' in non-existing dir failed as it should\n",
                filename);
            fd = ret;
        }
    }

    return 0;
}
