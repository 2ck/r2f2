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
    (void)argc;
    (void)argv;
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
        int ret = r2f2_close(&fs, fd);
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
        }
    }

    const char *write_buf = "Here we have some test data to be written.";
    /* write */
    {
        const char *filename = "/testfile";
        int ret = r2f2_write(&fs, fd, write_buf, strlen(write_buf) + 1);
        if (ret != RET_OK) {
            printf("R2F2 write '%s' to file '%s' failed (%d)\n", write_buf,
                   filename, ret);
            return ret;
        } else {
            printf("R2F2 write '%s' to file '%s' okay\n", write_buf, filename);
        }
    }

    /* fsync */
    {
        int ret = r2f2_fsync(&fs, fd);
        if (ret != RET_OK) {
            printf("R2F2 fsync fd %d failed (%d)\n", fd, ret);
            return ret;
        } else {
            printf("R2F2 fsync fd %d okay\n", fd);
        }
    }

    /* read back */
    {
        const char *filename = "/testfile";
        uint8_t read_buf[FLASH_PAGE_SIZE];
        memset(read_buf, 0, sizeof(read_buf));
        int ret = r2f2_read(&fs, fd, read_buf, strlen(write_buf) + 1);
        if (ret != RET_OK) {
            printf("R2F2 read %zu B from file '%s' failed (%d)\n",
                   strlen(write_buf) + 1, filename, ret);
            return ret;
        } else {
            printf("R2F2 read '%s' (%zu B) from file '%s' okay\n", read_buf,
                   strlen(write_buf) + 1, filename);
        }
    }

    return 0;
}
