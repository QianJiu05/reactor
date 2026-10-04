#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "config.h"
#include "recorder.h"

/*
 * 每个相机连接一份上下文。
 * 用 fd 做键：服务器不对连接做 dup，fd 在连接存续期内唯一。
 */
#define RECORDER_MAX_CAM   NUM_OF_CONNECTOR

struct recorder_ctx {
    int           cam_fd;         /* -1 表示空槽 */
    int           file_fd;        /* 当前段文件，-1 表示尚未打开 */
    int           file_idx;       /* 当前第几个文件 */
    unsigned      frames_in_file;
    unsigned long frames_total;
};

static struct recorder_ctx g_ctx[RECORDER_MAX_CAM];
static char    g_dir[256] = RECORD_DIR;
static int     g_enabled;
static unsigned long g_total_frames;

/* ===================== 内部工具 ===================== */

static struct recorder_ctx *ctx_get(int cam_fd, int create)
{
    struct recorder_ctx *empty = NULL;

    for (int i = 0; i < RECORDER_MAX_CAM; i++) {
        if (g_ctx[i].cam_fd == cam_fd)
            return &g_ctx[i];
        if (empty == NULL && g_ctx[i].cam_fd < 0)
            empty = &g_ctx[i];
    }

    if (!create || empty == NULL)
        return NULL;

    empty->cam_fd         = cam_fd;
    empty->file_fd        = -1;
    empty->file_idx       = 0;
    empty->frames_in_file = 0;
    empty->frames_total   = 0;
    return empty;
}

/* 滚动到下一个段文件 */
static int ctx_rotate(struct recorder_ctx *ctx)
{
    char path[320];

    if (ctx->file_fd >= 0) {
        close(ctx->file_fd);
        ctx->file_fd = -1;
    }

    ctx->file_idx++;
    ctx->frames_in_file = 0;

    snprintf(path, sizeof(path), "%s/cam%d_%05d.jpg",
             g_dir, ctx->cam_fd, ctx->file_idx);

    ctx->file_fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (ctx->file_fd < 0) {
        fprintf(stderr, "[recorder] 创建 %s 失败: %s\n", path, strerror(errno));
        return -1;
    }
    return 0;
}

/* ===================== 对外接口 ===================== */

int recorder_init(const char *dir)
{
    struct stat st;

    if (dir != NULL && dir[0] != '\0')
        snprintf(g_dir, sizeof(g_dir), "%s", dir);

    if (stat(g_dir, &st) == 0) {
        if (!S_ISDIR(st.st_mode)) {
            fprintf(stderr, "[recorder] %s 已存在但不是目录，落盘已关闭\n", g_dir);
            g_enabled = 0;
            return -1;
        }
    } else if (mkdir(g_dir, 0755) < 0) {
        fprintf(stderr, "[recorder] 创建目录 %s 失败: %s，落盘已关闭\n",
                g_dir, strerror(errno));
        g_enabled = 0;
        return -1;
    }

    for (int i = 0; i < RECORDER_MAX_CAM; i++) {
        g_ctx[i].cam_fd  = -1;
        g_ctx[i].file_fd = -1;
    }

    g_enabled = 1;
    printf("[recorder] 落盘目录: %s (每 %d 帧滚动一个新文件)\n",
           g_dir, RECORD_FILE_FRAMES);
    return 0;
}

int recorder_write_frame(int cam_fd, const struct frame_header *hdr,
                         const uint8_t *jpeg, uint32_t jpeg_len)
{
    (void)hdr;      /* 帧头信息暂不落盘；需要时可在段文件里加索引 */

    if (!g_enabled || jpeg == NULL || jpeg_len == 0)
        return -1;

    struct recorder_ctx *ctx = ctx_get(cam_fd, 1);
    if (ctx == NULL) {
        fprintf(stderr, "[recorder] 相机连接数超出上限，停止落盘 fd=%d\n", cam_fd);
        return -1;
    }

    if (ctx->file_fd < 0 || ctx->frames_in_file >= RECORD_FILE_FRAMES) {
        if (ctx_rotate(ctx) < 0)
            return -1;
    }

    /* 直接追加原始 JPEG 字节流。
     * 离线按帧拆分时，用 SOI(FFD8) / EOI(FFD9) 标记即可切段。 */
    ssize_t n = write(ctx->file_fd, jpeg, jpeg_len);
    if (n != (ssize_t)jpeg_len) {
        fprintf(stderr, "[recorder] 写入不完整 (写 %ld / 需 %u): %s\n",
                (long)n, (unsigned)jpeg_len, strerror(errno));
        close(ctx->file_fd);        /* 下次写入重新开一个文件，避免写出半帧 */
        ctx->file_fd = -1;
        return -1;
    }

    ctx->frames_in_file++;
    ctx->frames_total++;
    g_total_frames++;
    return 0;
}

void recorder_close_camera(int cam_fd)
{
    struct recorder_ctx *ctx = ctx_get(cam_fd, 0);
    if (ctx == NULL)
        return;

    if (ctx->file_fd >= 0) {
        close(ctx->file_fd);
        ctx->file_fd = -1;
    }

    printf("[recorder] 相机 fd=%d 结束录制: %lu 帧 / %d 个文件\n",
           cam_fd, ctx->frames_total, ctx->file_idx);

    ctx->cam_fd         = -1;
    ctx->file_idx       = 0;
    ctx->frames_in_file = 0;
    ctx->frames_total   = 0;
}

unsigned long recorder_total_frames(void)
{
    return g_total_frames;
}
