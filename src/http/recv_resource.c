/*
 *  服务器转发核心：把相机客户端的帧广播给所有查看端
 *
 *  数据流：
 *      相机 --(自定义协议帧)--> http_callback 解析
 *                                 ├── recorder_write_frame  落盘
 *                                 └── viewer_broadcast      转发
 *                                          └── 每个查看端 outbuf 里塞一段 MJPEG
 *                                                  再用 EPOLLOUT 事件发出去
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "recv_resource.h"
#include "http_handler.h"
#include "reactor.h"
#include "connect_pool.h"
#include "epoll.h"
#include "protocol.h"
#include "config.h"

/* ===================== 查看端注册表 ===================== */

#define MAX_VIEWERS  NUM_OF_CONNECTOR

static int g_viewers[MAX_VIEWERS];   /* 存查看端 fd，-1 表示空槽 */
static int g_viewers_inited;

static void viewers_init(void)
{
    if (g_viewers_inited)
        return;
    for (int i = 0; i < MAX_VIEWERS; i++)
        g_viewers[i] = -1;
    g_viewers_inited = 1;
}

void viewer_register(struct connect* conn)
{
    viewers_init();

    for (int i = 0; i < MAX_VIEWERS; i++) {
        if (g_viewers[i] == conn->fd)
            return;                     /* 已注册 */

        if (g_viewers[i] < 0) {
            g_viewers[i] = conn->fd;
#ifdef DEBUG
            printf("[viewer] 注册 fd=%d (当前 %d 个查看端)\n",
                   conn->fd, viewer_count());
#endif
            return;
        }
    }
    fprintf(stderr, "[viewer] 查看端数量已达上限 %d，拒绝 fd=%d\n",
            MAX_VIEWERS, conn->fd);
}

void viewer_unregister(struct connect* conn)
{
    viewers_init();

    for (int i = 0; i < MAX_VIEWERS; i++) {
        if (g_viewers[i] == conn->fd) {
            g_viewers[i] = -1;
#ifdef DEBUG
            printf("[viewer] 注销 fd=%d (剩余 %d 个查看端)\n",
                   conn->fd, viewer_count());
#endif
            return;
        }
    }
}

int viewer_count(void)
{
    int n = 0;
    for (int i = 0; i < MAX_VIEWERS; i++) {
        if (g_viewers[i] >= 0)
            n++;
    }
    return n;
}

/* ===================== 转发 ===================== */

/*
 * 把一帧包成 MJPEG 分段塞进某个查看端的 outbuf。
 * 采用「尽量塞满、塞不下就丢」的策略：
 * 查看端网络慢时不能反压相机链路，直播场景下丢帧比卡顿更可接受。
 */
static void push_frame_to_viewer(int fd, const uint8_t* jpeg, uint32_t jpeg_len)
{
    struct connect* dest = get_connector(fd);
    if (dest == NULL || dest->state != STATE_USING || dest->fd != fd)
        return;

    char head[128];
    int head_len = snprintf(head, sizeof(head),
                            "--frame\r\n"
                            "Content-Type: image/jpeg\r\n"
                            "Content-Length: %u\r\n\r\n",
                            jpeg_len);
    if (head_len <= 0)
        return;

    int need = head_len + (int)jpeg_len + 2;      /* +2 是结尾的 \r\n */
    int space = CONNECT_BUF_LEN - dest->outlen;

    if (need > space) {
#ifdef DEBUG
        static int warn_cnt;
        if (++warn_cnt % 100 == 1)
            printf("[viewer] fd=%d 缓冲不足 (需 %d, 余 %d)，丢弃该帧\n",
                   fd, need, space);
#endif
        return;
    }

    memcpy(dest->outbuf + dest->outlen, head, head_len);
    dest->outlen += head_len;
    memcpy(dest->outbuf + dest->outlen, jpeg, jpeg_len);
    dest->outlen += (int)jpeg_len;
    memcpy(dest->outbuf + dest->outlen, "\r\n", 2);
    dest->outlen += 2;

    /* 通知该查看端所在的 reactor 可以发数据了 */
    set_epoll(dest->sub, EPOLLOUT, EPOLL_CTL_MOD, fd);
}

void viewer_broadcast(const struct frame_header* hdr, const uint8_t* jpeg)
{
    viewers_init();

    if (viewer_count() == 0)
        return;                 /* 没有查看端，只落盘即可 */

#ifdef DEBUG
    /* PIR 命中的帧单独提示，便于演示「人体感应」联动 */
    if (hdr->pir)
        printf("[viewer] 人体感应帧 seq=%u，转发给 %d 个查看端\n",
               hdr->seq, viewer_count());
#endif

    for (int i = 0; i < MAX_VIEWERS; i++) {
        if (g_viewers[i] >= 0)
            push_frame_to_viewer(g_viewers[i], jpeg, hdr->jpeg_len);
    }
}

/* ===================== 相机客户端握手 ===================== */

int handle_resource(struct connect* conn)
{
    /* 格式: "CAM\r\n" —— 声明本连接是相机客户端 */
    if (strncmp(conn->inbuf, PROTO_CMD_CAM, strlen(PROTO_CMD_CAM)) != 0) {
        printf("[cam] 非法握手报文 fd=%d\n", conn->fd);
        return -1;
    }

    conn->app.cam.dest       = conn->fd;   /* >=0 即标记为相机连接 */
    conn->app.cam.last_seq   = 0;
    conn->app.cam.seq_inited = 0;

    /* 清空握手报文，避免它被当成帧数据参与魔数搜索 */
    memset(conn->inbuf, 0, conn->inlen);
    conn->inlen = 0;

    printf("[cam] 相机客户端接入: fd=%d\n", conn->fd);
    return 0;
}
