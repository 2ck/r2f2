#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "r2f2.h"
#include "r2f2_metadata.h"

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
        printf("Error during malloc.\n");
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
        if (ret < 0) {
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
        if (ret < 0) {
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
        if (ret >= 0) {
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
    size_t write_len = strlen(write_buf) + 1;
    /* write */
    {
        const char *filename = "/testfile";
        int ret = r2f2_write(&fs, fd, write_buf, write_len);
        if (ret != (int)write_len) {
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
        uint8_t read_buf[write_len];
        memset(read_buf, 0, sizeof(read_buf));
        int ret = r2f2_lseek(&fs, fd, 0, SEEK_SET);
        if (ret != 0) {
            printf("R2F2 seek to position 0 in file '%s' failed (%d)", filename,
                   ret);
            return ret;
        }
        ret = r2f2_read(&fs, fd, read_buf, write_len);
        if (ret != (int)write_len) {
            printf("R2F2 read %zu B from file '%s' failed (%d)\n",
                   strlen(write_buf) + 1, filename, ret);
            return ret;
        } else {
            printf("R2F2 read '%s' (%zu B) from file '%s' okay\n", read_buf,
                   strlen(write_buf) + 1, filename);
        }
    }

    /* mkdir */
    {
        const char *dirname = "/foobar/";
        int ret = r2f2_mkdir(&fs, dirname);
        if (ret != RET_OK) {
            printf("R2F2 mkdir '%s' failed (%d)\n", dirname, ret);
        } else {
            printf("R2F2 mkdir '%s' okay\n", dirname);
        }

        const char *filename = "/foobar/subdirfile";
        int fd2;
        ret = r2f2_open(&fs, filename, O_CREAT | O_RDWR);
        if (ret < 0) {
            printf("R2F2 open file '%s' failed (%d)\n", filename, ret);
            return ret;
        } else {
            printf("R2F2 open file '%s' okay\n", filename);
            fd2 = ret;
        }
        ret = r2f2_close(&fs, fd2);
        if (ret != RET_OK) {
            printf("R2F2 close fd %d failed (%d)\n", fd2, ret);
            return ret;
        } else {
            printf("R2F2 close fd %d okay\n", fd2);
        }
    }

    /* write lots of data */
    {
        const char *filename = "/testfile";
        for (size_t i = 0; i < 4 * FLASH_BLOCK_SIZE + 83; i++) {
            uint8_t b = 97 + (i % 26);
            /* if (i < 900) { */
            /*     printf("file size=%lld\n", r2f2_lseek(&fs, fd, 0, SEEK_END));
             */
            /* } */
            if (i == 517) {
                int ret = r2f2_write(&fs, fd, write_buf, write_len);
                if (ret != (int)write_len) {
                    printf("R2F2 write '%s' to file '%s' failed (%d)\n",
                           write_buf, filename, ret);
                    return ret;
                }
            } else {
                size_t write_len = 1;
                int ret = r2f2_write(&fs, fd, &b, write_len);
                if (ret != (int)write_len) {
                    printf("R2F2 write '%c' to file '%s' failed (%d)\n", b,
                           filename, ret);
                    return ret;
                }
            }
        }
        int ret = r2f2_fsync(&fs, fd);
        if (ret != RET_OK) {
            printf("R2F2 fsync fd %d failed (%d)\n", fd, ret);
            return ret;
        }
        const off_t seek_pos = 517 + 43;
        ret = r2f2_lseek(&fs, fd, seek_pos, SEEK_SET);
        if (ret != seek_pos) {
            printf("R2F2 seek to position %lld in file '%s' failed (%d)",
                   seek_pos, filename, ret);
            return ret;
        }
        uint8_t read_buf[2 * FLASH_PAGE_SIZE + 13];
        ret = r2f2_read(&fs, fd, read_buf, sizeof(read_buf));
        if (ret != (ssize_t)sizeof(read_buf)) {
            printf("R2F2 read %zu B from file '%s' failed (%d)\n",
                   strlen(write_buf) + 1, filename, ret);
            return ret;
        }
        r2f2_hexdump(read_buf, 64);
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

    dump_fs_dot(&fs, "tests-basic.dot");

    return 0;
}
