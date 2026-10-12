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
#include "http_parse.h"
#include "reactor.h"
#include "connect_pool.h"
#include "epoll.h"
#include "protocol.h"
#include "account.h"
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

/* ===================== 采集端握手 ===================== */

/*
 * 往 outbuf 写一行回应并立刻发出去。
 *
 * 为什么直接 send() 而不等 EPOLLOUT：
 * 回应只有几个字节，而连接刚建立、发送缓冲区一定是空的，
 * 同步发出去最简单，也让采集端能立刻拿到结果。发不出去（EAGAIN）
 * 就交给 epoll 后续处理，不影响正确性。
 */
static void send_handshake_reply(struct connect* conn, const char *line)
{
    int len = (int)strlen(line);
    int n;

    n = send(conn->fd, line, len, 0);
    if (n == len)
        return;

    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        /* 发送缓冲满：把剩下的排进 outbuf，由 EPOLLOUT 补发 */
        memcpy(conn->outbuf, line, len);
        conn->outlen = len;
        set_epoll(conn->sub, EPOLLOUT, EPOLL_CTL_MOD, conn->fd);
        return;
    }

    fprintf(stderr, "fd:%d 发送握手回应失败: %s\n", conn->fd, strerror(errno));
}

int handle_resource(struct connect* conn)
{
    char name[ACCOUNT_NAME_LEN];
    int  idx;
    int  rc;
    int  stale_fd = -1;

    /* 格式: "CAM <account>\r\n"；解析逻辑见 inc/http/http_parse.h */
    if (!http_parse_cam_account(conn->inbuf, name, sizeof(name))) {
        printf("[cam] fd=%d 握手报文缺少账号名或前缀不匹配\n", conn->fd);
        send_handshake_reply(conn, "ERR AUTH\r\n");
        return -1;
    }

    if (account_name_valid(name) != 0) {
        printf("[cam] fd=%d 账号名非法: \"%s\"\n", conn->fd, name);
        send_handshake_reply(conn, "ERR AUTH\r\n");
        return -1;
    }

    idx = account_lookup(name);
    if (idx < 0) {
        printf("[cam] fd=%d 账号不存在: \"%s\"\n", conn->fd, name);
        send_handshake_reply(conn, "ERR AUTH\r\n");
        return -1;
    }

    rc = account_cam_bind(idx, conn->fd, &stale_fd);
    if (rc == ACCOUNT_BIND_BUSY) {
        printf("[cam] fd=%d 账号 \"%s\" 已被占用\n", conn->fd, name);
        send_handshake_reply(conn, "ERR BUSY\r\n");
        return -1;
    }
    if (rc != ACCOUNT_BIND_OK) {
        printf("[cam] fd=%d 绑定账号 \"%s\" 失败\n", conn->fd, name);
        send_handshake_reply(conn, "ERR AUTH\r\n");
        return -1;
    }

    /* 顶替僵死连接：这里只负责关闭旧连接，账号已经指向新 fd */
    if (stale_fd >= 0) {
        struct connect *old = get_connector(stale_fd);
        if (old != NULL && old->state == STATE_USING && old->fd == stale_fd) {
            printf("[cam] 关闭僵死的旧采集端 fd=%d\n", stale_fd);
            old->close(old);
        }
    }

    conn->account_idx        = idx;
    conn->app.cam.dest       = conn->fd;
    conn->app.cam.last_seq   = 0;
    conn->app.cam.seq_inited = 0;

    /* 注意：首行由 recv.c 统一消费（只移出首行、保留后续字节），
     * 这里【不要】再清空 inbuf —— 对端可能把握手行和第一帧放在同一个
     * send() 里发出，清空会把那一帧丢掉（设计文档 D7）。 */

    send_handshake_reply(conn, "OK\r\n");

    printf("[cam] 采集端接入: fd=%d 账号 \"%s\"\n", conn->fd, name);
    return 0;
}
