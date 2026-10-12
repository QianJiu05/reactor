/*
 *  服务器转发核心：把采集端的帧广播给【同一账号】下的查看端
 *
 *  数据流：
 *      采集端 --(自定义协议帧)--> http_callback 解析
 *                                   ├── recorder_write_frame  落盘
 *                                   └── viewer_broadcast      转发
 *                                            └── 每个查看端 outbuf 里塞一段 MJPEG
 *                                                    再用 EPOLLOUT 事件发出去
 *
 *  账号隔离与锁粒度：
 *      查看端集合由 account 模块按账号维护，受 g_account_lock 保护。
 *      转发是【每帧都要走】的热路径，因此这里严格遵循
 *      「持锁取快照 → 放锁 → 逐个推送」：锁内只做一次 fd 拷贝，
 *      数据搬运、磁盘 I/O、epoll 通知全在锁外。
 *      代价是快照期间新加入的查看端会漏掉当前这一帧（下一帧就能收到），
 *      视频流本来也不保证首帧必达，这个取舍可以接受。
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
#include "stream_source.h"
#include "protocol.h"
#include "account.h"
#include "config.h"

/* ===================== 查看端注册 ===================== */

void viewer_register(struct connect* conn)
{
    if (conn->account_idx < 0) {
        fprintf(stderr, "[viewer] fd=%d 尚未归属账号，无法注册为查看端\n",
                conn->fd);
        return;
    }

    if (account_viewer_add(conn->account_idx, conn->fd) < 0) {
        /* 上限已满：这里不关连接，让 http_handler 回 503 更麻烦，
         * 直接断开并说明原因即可（账号模块已打印详细日志） */
        fprintf(stderr, "[viewer] fd=%d 注册失败，连接将被关闭\n", conn->fd);
        conn->close(conn);
        return;
    }

#ifdef DEBUG
    printf("[viewer] 注册 fd=%d 到账号 \"%s\"（该账号现有 %d 个查看端）\n",
           conn->fd, account_name(conn->account_idx),
           account_viewer_count(conn->account_idx));
#endif
}

void viewer_unregister(struct connect* conn)
{
    if (conn->account_idx < 0)
        return;

    account_viewer_remove(conn->fd);

#ifdef DEBUG
    printf("[viewer] 注销 fd=%d（账号 \"%s\" 剩余 %d 个查看端）\n",
           conn->fd, account_name(conn->account_idx),
           account_viewer_count(conn->account_idx));
#endif
}

int viewer_count(void)
{
    /* 所有账号的查看端总数，仅用于状态展示 */
    int total = 0;

    for (int i = 0; i < MAX_ACCOUNTS; i++)
        total += account_viewer_count(i);

    return total;
}

/* ===================== 转发 ===================== */

/*
 * 把一帧包成 MJPEG 分段塞进某个查看端的 outbuf。
 *
 * 采用「塞得下就写、塞不下就丢」：
 * 查看端网络慢时不能反压采集链路，直播场景下丢帧比整体卡顿更可接受。
 */
static void push_frame_to_viewer(int fd, const uint8_t* jpeg, uint32_t jpeg_len)
{
    struct connect* dest = get_connector(fd);
    char head[128];
    int  head_len;
    int  need;
    int  space;

    if (dest == NULL || dest->state != STATE_USING || dest->fd != fd)
        return;

    head_len = snprintf(head, sizeof(head),
                        "--frame\r\n"
                        "Content-Type: image/jpeg\r\n"
                        "Content-Length: %u\r\n\r\n",
                        jpeg_len);
    if (head_len <= 0)
        return;

    need  = head_len + (int)jpeg_len + 2;      /* +2 是结尾的 \r\n */
    space = CONNECT_BUF_LEN - dest->outlen;

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

    /* 通知该查看端所在的 reactor 可以发数据了。
     * 查看端可能不在当前线程的 reactor 上，epoll_ctl 对 epoll 实例
     * 是线程安全的，所以这里跨线程调用是允许的。 */
    set_epoll(dest->sub, EPOLLOUT, EPOLL_CTL_MOD, fd);
}

/*
 * 把一帧广播给【指定账号】下的所有查看端。
 *
 * idx 由调用方（handle_camera_frame）从 conn->account_idx 直接取得，
 * 不必由 fd 反查 —— 那条连接就在我们手上，读它的字段是 O(1)。
 */
void viewer_broadcast(int account_idx, const struct frame_header* hdr,
                      const uint8_t* jpeg)
{
    int fds[MAX_VIEWERS_PER];
    int n;

    if (hdr == NULL || jpeg == NULL || hdr->jpeg_len == 0)
        return;
    if (account_idx < 0)
        return;

    /* ---- 持锁取快照，随即放锁 ---- */
    n = account_viewer_snapshot(account_idx, fds, MAX_VIEWERS_PER);
    if (n <= 0)
        return;                     /* 该账号暂时没有查看端，只落盘即可 */

#ifdef DEBUG
    if (hdr->pir)
        printf("[viewer] 账号 \"%s\" 人体感应帧 seq=%u，转发给 %d 个查看端\n",
               account_name(account_idx), hdr->seq, n);
#endif

    /* ---- 锁外逐个推送 ---- */
    for (int i = 0; i < n; i++)
        push_frame_to_viewer(fds[i], jpeg, hdr->jpeg_len);

    account_add_frames_out(account_idx, (unsigned long)n);
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

    /*
     * 采集端上线了：该账号下正在等设备的查看端可以停止推占位图了。
     * 不必给它发任何"开始"信号 —— 下一帧真实数据自然会把占位图顶掉。
     */
    stream_source_stop_idle_for_account(idx);

    printf("[cam] 采集端接入: fd=%d 账号 \"%s\"\n", conn->fd, name);
    return 0;
}
