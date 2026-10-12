#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>

#include "http_handler.h"
#include "http_helper.h"
#include "http_parse.h"
#include "reactor.h"
#include "epoll.h"
#include "recv_resource.h"
#include "stream_source.h"
#include "protocol.h"
#include "account.h"
#include "recorder.h"

static void init_send_stream(struct connect* conn);
static int  send_outbuf(struct connect* conn);
static int  handle_camera_frame(struct connect* conn);
static int  flush_to_viewer(struct connect* conn);
static int  send_simple_response(struct connect* conn, int status_code);

/* ===================== 一次性响应（400/404/405/503） ===================== */

/* 按状态码取状态行文本与说明页 */
static void status_text(int code, const char** status, const char** body)
{
    switch (code) {
        case 400: *status = status_400; *body = body_400; break;
        case 404: *status = status_404; *body = body_404; break;
        case 405: *status = status_405; *body = body_405; break;
        case 503: *status = status_503; *body = BODY_NO_ACCOUNT; break;
        default:  *status = status_400; *body = body_400; break;
    }
}

/*
 * 回一个「一次性」响应，并【同步发送完毕】。
 *
 * 为什么同步发：调用方紧接着就会关闭连接。如果只把响应写进 outbuf
 * 然后等 EPOLLOUT 事件，连接一关这些字节就永远发不出去了。
 * 响应体只有几百字节，同步补发完全可接受。
 *
 * 统一返回 -1，表示"已处理完毕、调用方请关闭连接"。
 */
static int send_simple_response(struct connect* conn, int status_code)
{
    const char *status, *body;
    int body_len;
    int total;
    int off = 0;

    status_text(status_code, &status, &body);
    body_len = (int)strlen(body);

    total = snprintf(conn->outbuf, CONNECT_BUF_LEN,
                     "HTTP/1.1 %d %s\r\n"
                     "%s"
                     "Content-Length: %d\r\n"
                     "Connection: close\r\n"
                     "\r\n"
                     "%s",
                     status_code, status,
                     type_text_html,
                     body_len,
                     body);

    if (total <= 0 || total >= CONNECT_BUF_LEN) {
        conn->outlen = 0;
        return -1;
    }

    while (off < total) {
        int n = send(conn->fd, conn->outbuf + off, total - off, 0);

        if (n > 0) {
            off += n;
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            usleep(1000);           /* 对端刚连上，稍等就有发送空间 */
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            break;                  /* 对端已断开，没必要再发 */
        }
    }

    conn->outlen = 0;
    return -1;
}

/* ===================== 查看端路由 ===================== */

/*
 * 查看端连上来（GET 请求）后：
 *   1. 解析请求行，切出方法与路径；
 *   2. 按路径第一段查账号，决定回什么状态码；
 *   3. 通过则回 multipart/x-mixed-replace 响应头，浏览器按 MJPEG 流渲染。
 *
 * 返回 0  已通过（连接保持，进入流模式）
 * 返回 -1 已回错误响应（调用方负责关闭连接）
 */
int generate_http_response(struct connect* conn)
{
    char method[16];
    char path[256];
    char account[ACCOUNT_NAME_LEN];
    int  idx;

    if (!http_parse_request_line(conn->inbuf, method, sizeof(method),
                                 path, sizeof(path))) {
        printf("[http] fd=%d 请求行格式错误\n", conn->fd);
        return send_simple_response(conn, 400);
    }

    /* 请求路径存进连接上下文，便于日志与后续扩展（如 /status） */
    snprintf(conn->app.http.path, sizeof(conn->app.http.path), "%s", path);

    /* 只接受 GET */
    if (strcmp(method, "GET") != 0) {
        printf("[http] fd=%d 不支持的方法: %s\n", conn->fd, method);
        return send_simple_response(conn, 405);
    }

    /* 从路径切账号："/"、"/alice/x"、"/favicon.ico" 等都会在这里被挡下 */
    if (!http_path_to_account(path, account, sizeof(account))) {
        if (strcmp(path, "/") == 0) {
            printf("[http] fd=%d 未指定账号（访问了 /）\n", conn->fd);
            return send_simple_response(conn, 503);
        }
        printf("[http] fd=%d 路径无效: %s\n", conn->fd, path);
        return send_simple_response(conn, 404);
    }

    idx = account_lookup(account);
    if (idx < 0) {
        printf("[http] fd=%d 账号不存在: \"%s\"\n", conn->fd, account);
        return send_simple_response(conn, 404);
    }

    conn->account_idx = idx;

    /* ---- 通过：回 MJPEG 流响应头 ---- */
    conn->outlen = snprintf(conn->outbuf, CONNECT_BUF_LEN,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
            "Connection: keep-alive\r\n"
            "Cache-Control: no-cache\r\n\r\n");

    init_send_stream(conn);

    if (send_outbuf(conn) < 0) {
        conn->close(conn);
        return -1;
    }

    /* 注意：首行与请求头由 recv.c 统一消费（只移出首行、保留后续字节），
     * 这里【不要】再 memset 整个 inbuf —— 那会丢掉紧跟请求头的数据。 */

    viewer_register(conn);

    /*
     * 采集端还没上线的话，先推「等待设备」占位图。
     * 这里按需启动定时器；采集端上线时由 handle_resource() 负责解除。
     */
    stream_source_arm_idle_timer(conn, !account_cam_online(idx));

#ifdef DEBUG
    printf("[http] 查看端接入: fd=%d 账号 \"%s\"%s\n",
           conn->fd, account,
           account_cam_online(idx) ? "" : "（采集端尚未上线，先推占位图）");
#endif
    return 0;
}

/* ===================== 帧解析 + 转发 ===================== */

/* 发送 outbuf 里已排好的数据。返回 0 正常；-1 出错 */
static int send_outbuf(struct connect* conn)
{
    while (conn->outlen > 0) {
        int n = send(conn->fd, conn->outbuf, conn->outlen, 0);

        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                /* 发送缓冲区满：剩下的留给 EPOLLOUT 事件继续发 */
                set_epoll(conn->sub, EPOLLOUT, EPOLL_CTL_MOD, conn->fd);
                return 0;
            }
            if (errno == EINTR)
                continue;
            perror("[http] send");
            return -1;
        }
        if (n == 0)
            return -1;

        conn->outlen -= n;
        if (conn->outlen > 0)
            memmove(conn->outbuf, conn->outbuf + n, conn->outlen);
    }

    conn->outlen = 0;
    set_epoll(conn->sub, EPOLLIN, EPOLL_CTL_MOD, conn->fd);
    return 0;
}

/*
 * 处理来自采集端的一帧。
 *
 * inbuf 里可能同时存在半帧、一帧或多帧（粘包），
 * 这里按「找魔数 → 判头部 → 判长度 → 校验 → 取走一帧」的循环反复消费，
 * 剩余不足一帧的字节留在缓冲里等下次 recv 补齐（拆包）。
 */
static int handle_camera_frame(struct connect* conn)
{
    const int    hdr_len = (int)sizeof(struct frame_header);
    struct frame_header hdr;
    int consumed = 0;

    while (conn->inlen - consumed >= hdr_len) {
        uint8_t *base = (uint8_t*)conn->inbuf + consumed;
        int      avail = conn->inlen - consumed;

        /* 1. 定位魔数，丢掉之前的垃圾字节 */
        int magic_pos = -1;
        for (int i = 0; i + 1 < avail; i++) {
            if (*(uint16_t*)(base + i) == PROTO_MAGIC) {
                magic_pos = i;
                break;
            }
        }
        if (magic_pos < 0) {
            /* 整段都没有魔数：保留最后 1 字节（魔数可能跨包） */
            consumed = conn->inlen - 1;
            break;
        }
        if (magic_pos > 0) {
#ifdef DEBUG
            printf("[http] fd=%d 丢弃 %d 字节无效数据\n", conn->fd, magic_pos);
#endif
            consumed += magic_pos;
            continue;
        }

        /* 2. 帧头是否完整 */
        if (avail < hdr_len)
            break;

        memcpy(&hdr, base, sizeof(hdr));

        /* 3. jpeg_len 合法性：不合法说明是伪魔数，跳过 2 字节继续找 */
        if (hdr.version != PROTO_VERSION ||
            hdr.jpeg_len == 0 || hdr.jpeg_len > PROTO_MAX_PAYLOAD ||
            hdr.jpeg_len > (uint32_t)CONNECT_BUF_LEN) {
#ifdef DEBUG
            printf("[http] fd=%d 非法帧头 (ver=%u len=%u)，跳过\n",
                   conn->fd, hdr.version, hdr.jpeg_len);
#endif
            consumed += 2;
            continue;
        }

        int total_frame = hdr_len + (int)hdr.jpeg_len;

        /* 4. 载荷是否收全（拆包：没收全就等下一次 recv） */
        if (avail < total_frame)
            break;

        /* 5. 校验和 */
        if (!proto_check(base, (size_t)avail, &hdr)) {
#ifdef DEBUG
            printf("[http] fd=%d 校验失败，跳过 2 字节重找\n", conn->fd);
#endif
            consumed += 2;
            continue;
        }

        /* 6. 丢帧统计：seq 不连续说明网络或采集端队列丢了帧 */
        if (conn->app.cam.seq_inited && hdr.seq != conn->app.cam.last_seq + 1) {
            printf("[http] fd=%d 帧序号跳变: 期望 %u, 收到 %u (丢 %u 帧)\n",
                   conn->fd, conn->app.cam.last_seq + 1, hdr.seq,
                   hdr.seq - conn->app.cam.last_seq - 1);
        }
        conn->app.cam.last_seq   = hdr.seq;
        conn->app.cam.seq_inited = 1;

        uint8_t *jpeg = base + hdr_len;

        /* 7a. 更新该账号的帧活动时间与计数（僵死判定依赖它） */
        account_touch_frame(conn->account_idx);

        /* 7b. 落盘存储（课题要求 4）—— 旁路，失败不影响转发 */
        recorder_write_frame(conn->fd, &hdr, jpeg, hdr.jpeg_len);

        /* 7c. 转发给该账号下的查看端（课题要求 5 的后半段）
         *     注意：这里传的是 conn->account_idx —— 连接就在手上，
         *     读字段是 O(1)，不必再由 fd 反查账号表 */
        viewer_broadcast(conn->account_idx, &hdr, jpeg);

        consumed += total_frame;
    }

    /* 把已消费的字节移出缓冲 */
    if (consumed > 0) {
        int remain = conn->inlen - consumed;
        if (remain > 0)
            memmove(conn->inbuf, conn->inbuf + consumed, remain);
        conn->inlen = remain > 0 ? remain : 0;
    }

    return 0;
}

/* 把 inbuf 里排好的帧数据搬到 outbuf（转发的最后一步） */
static int flush_to_viewer(struct connect* conn)
{
    int space = CONNECT_BUF_LEN - conn->outlen;
    int to_copy;

    if (space <= 0) {
        /* 查看端消费不过来：直接丢弃，不能阻塞采集链路 */
#ifdef DEBUG
        printf("[http] 查看端 fd=%d 来不及接收，丢弃 %d 字节\n",
               conn->fd, conn->inlen);
#endif
        conn->inlen = 0;
        return 0;
    }

    to_copy = (conn->inlen < space) ? conn->inlen : space;
    memcpy(conn->outbuf + conn->outlen, conn->inbuf, to_copy);
    conn->outlen += to_copy;

    if (to_copy < conn->inlen) {
        int remain = conn->inlen - to_copy;
        memmove(conn->inbuf, conn->inbuf + to_copy, remain);
        conn->inlen = remain;
    } else {
        conn->inlen = 0;
    }

    return send_outbuf(conn);
}

/* ===================== 连接主回调 ===================== */

/*
 * 同一份发送回调服务两类连接：
 *   · 采集端：inbuf 里是自定义协议帧 → 解析、落盘、广播
 *   · 查看端：把转发进来的帧包成 MJPEG 分段写出去
 *
 * 用 conn->phase 判定角色，不再依赖 app.cam.dest（app 是 union，
 * 在查看端连接上读 cam.dest 是未定义行为）。
 */
int http_callback(struct connect* conn)
{
    if (conn->phase == PHASE_CAM)
        return handle_camera_frame(conn);

    /* ---- 查看端 ---- */
    if (conn->outlen > 0)
        return send_outbuf(conn);

    if (conn->inlen > 0)
        return flush_to_viewer(conn);

    set_epoll(conn->sub, EPOLLIN, EPOLL_CTL_MOD, conn->fd);
    return 0;
}

/* ===================== helper ===================== */

static void init_send_stream(struct connect* conn) {
    conn->app.http.file_fd = -1;    /* 标记为不从文件读取 */
    conn->app.http.remain = -1;     /* 大小未知 */
    conn->app.http.header_sent = true;
    conn->app.http.stream_mode = true;
}
