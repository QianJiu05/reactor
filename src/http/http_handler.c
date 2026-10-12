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
#include "reactor.h"
#include "epoll.h"
#include "recv_resource.h"
#include "protocol.h"
#include "recorder.h"

#define LIBHTTP_REQUEST_MAX_SIZE CONNECT_BUF_LEN

static int  http_response_status(struct connect* conn, int status_code);
static void init_send_stream(struct connect* conn);
static int  send_outbuf(struct connect* conn);
static int  handle_camera_frame(struct connect* conn);

/* ===================== HTTP 响应头 ===================== */

int http_get_status_code (const char* request_buf) {
    if (request_buf == NULL || request_buf[0] == '\0')
        return 400;
    return 200;
}

/*
 * 查看端连上来（GET 请求）后：
 *   1. 回一个 multipart/x-mixed-replace 响应头，浏览器按 MJPEG 流持续渲染；
 *   2. 把本连接登记为查看端，后续相机推来的帧由转发逻辑写入它的 outbuf。
 */
int generate_http_response(struct connect* conn)
{
    int status_code = http_get_status_code(conn->inbuf);

    if (status_code != 200)
        return http_response_status(conn, status_code);

    conn->outlen = snprintf(conn->outbuf, CONNECT_BUF_LEN,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
            "Connection: keep-alive\r\n"
            "Cache-Control: no-cache\r\n\r\n");

    init_send_stream(conn);

    if (send_outbuf(conn) < 0)
        return -1;

    /* 注意：首行与请求头由 recv.c 统一消费（只移出首行、保留后续字节），
     * 这里【不要】再 memset 整个 inbuf —— 那会丢掉紧跟请求头的数据。 */

    viewer_register(conn);

#ifdef DEBUG
    printf("[http] 查看端接入: fd=%d\n", conn->fd);
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
 * 处理来自相机客户端的一帧。
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

        /* 6. 丢帧统计：seq 不连续说明网络或客户端队列丢了帧 */
        if (conn->app.cam.seq_inited && hdr.seq != conn->app.cam.last_seq + 1) {
            printf("[http] fd=%d 帧序号跳变: 期望 %u, 收到 %u (丢 %u 帧)\n",
                   conn->fd, conn->app.cam.last_seq + 1, hdr.seq,
                   hdr.seq - conn->app.cam.last_seq - 1);
        }
        conn->app.cam.last_seq  = hdr.seq;
        conn->app.cam.seq_inited = 1;

        uint8_t *jpeg = base + hdr_len;

        /* 7a. 落盘存储（课题要求 4）—— 旁路，失败不影响转发 */
        recorder_write_frame(conn->fd, &hdr, jpeg, hdr.jpeg_len);

        /* 7b. 转发给所有查看端（课题要求 5 的后半段） */
        viewer_broadcast(&hdr, jpeg);

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

    if (space <= 0) {
        /* 查看端消费不过来：直接丢弃，不能阻塞相机链路 */
#ifdef DEBUG
        printf("[http] 查看端 fd=%d 来不及接收，丢弃 %d 字节\n",
               conn->fd, conn->inlen);
#endif
        conn->inlen = 0;
        return 0;
    }

    int to_copy = (conn->inlen < space) ? conn->inlen : space;
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
 *   · 相机客户端：inbuf 里是自定义协议帧 → 解析、落盘、广播
 *   · 查看端：把转发进来的帧包成 MJPEG 分段写出去
 */
int http_callback(struct connect* conn)
{
    if (!conn->app.http.header_sent) {
        /* 响应头还没发出去，先补发 */
        return send_outbuf(conn);
    }

    if (conn->app.cam.dest < 0) {
        /* 查看端：outbuf 里是待发送的 MJPEG 分段 */
        if (conn->outlen > 0)
            return send_outbuf(conn);
        set_epoll(conn->sub, EPOLLIN, EPOLL_CTL_MOD, conn->fd);
        return 0;
    }

    return handle_camera_frame(conn);
}

/* ===================== helper ===================== */

static void init_send_stream(struct connect* conn) {
    conn->app.http.file_fd = -1;  // 标记为不从文件读取
    conn->app.http.remain = -1;   // 表示大小未知
    conn->app.http.header_sent = true;
    conn->app.http.stream_mode = true;
}

/* "HTTP/1.1 200 OK\r\n" */
static int http_response_status(struct connect* conn, int status_code) {
    const char* status_response = status_200;
    const char* body = body_200;
    const char* connection;
    const char* type;

    switch (status_code)
    {
        case 200:
            status_response = status_200;
            body = body_200;
            break;
        case 400:
            status_response = status_400;
            body = body_400;
            break;
        case 404:
            status_response = status_404;
            body = body_404;
            break;
        default:
            printf("status code not impled:%d\n", status_code);
            break;
    }

    if (conn->app.http.keep_alive == true) {
        connection = connect_keep_alive;
    } else {
        connection = connect_close;
    }

    switch (conn->app.http.content_type)
    {
        case TYPE_NO_NEED:
            type = type_no_need;
            break;
        case TYPE_TEXT_HTML:
            type = type_text_html;
            break;
        case TYPE_IMG_JPEG:
            type = type_multipart;
            break;
        default:
            type = type_no_need;
            printf("type not impled:%d\n", conn->app.http.content_type);
            break;
    }

    int body_len = strlen(body);
    int ret = snprintf(conn->outbuf, CONNECT_BUF_LEN,
                    "HTTP/1.1 %d %s\r\n"    /* status_code, status_response */
                    "%s\r\n"                /* type */
                    "Content-Length: %d\r\n" /* body_len,  */
                    "%s\r\n\r\n"            /* connection */
                    "%s",                   /* body */
                    status_code, status_response, type, body_len, connection, body);
    conn->app.http.header_sent = true;
    return ret;
}
