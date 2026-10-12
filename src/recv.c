#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <errno.h>
#include <string.h>

#include "connect_pool.h" //这个要声明在callback h前面，不然会警告cb类型不兼容
#include "callback.h"
#include "recv_resource.h"
#include "http_handler.h"
#include "recv.h"
#include "protocol.h"
#include "config.h"

/* ===================== 首行解析 ===================== */

/*
 * 在缓冲区里查找 "\r\n"。
 * 找到返回 \r 的下标（即首行长度，不含 CRLF）；没找到返回 -1。
 */
static int find_line_end(const char *buf, int len)
{
    for (int i = 0; i + 1 < len; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n')
            return i;
    }
    return -1;
}

/*
 * 根据首行判断这条连接的用途。
 * 只有在拿到【完整首行】之后才应该调用本函数。
 */
static int parse_serve_type(const char *line)
{
    if (strncmp(line, "GET ", 4) == 0)
        return SERVE_HTTP;              /* 查看端：HTTP 请求 */
    if (strncmp(line, PROTO_CMD_CAM, strlen(PROTO_CMD_CAM)) == 0)
        return SERVE_GET_RESOURCE;      /* 采集端：CAM <account> */

    return SERVE_ECHO;
}

/* ===================== 接收主流程 ===================== */

int recv_callback(struct connect* conn) {
    /* 把数据接收到inbuf */
    int space = CONNECT_BUF_LEN - conn->inlen;
    if (space <= 0) {
        /* 缓冲区满了，说明对端推得太快而本端没消费完，丢弃这批数据 */
        fprintf(stderr, "fd:%d 接收缓冲已满，丢弃 %d 字节\n", conn->fd, conn->inlen);
        conn->inlen = 0;
        space = CONNECT_BUF_LEN;
    }

    int n = recv(conn->fd, conn->inbuf + conn->inlen, space, 0);

    /* n == 0 表示对端正常关闭，应直接 close，不要看 errno。
       只有 n < 0 才检查 errno == EAGAIN/EWOULDBLOCK。 */
    if (n == 0) {
        conn->close(conn);
        return -1;
    } else if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return 0;  // 继续等待
        }
        perror("recv");
        conn->close(conn);
        return -1;
    }

    conn->inlen += n;

    /*
     * 首行解析：必须在【拿到完整一行】之后再做判断。
     *
     * 原来是在 inlen >= 4 时就 parse_serve_type()，一旦 TCP 把首行分片
     * （例如先到 "CAM "），就会把采集端误判成 echo 连接。这里改为找 "\r\n"，
     * 找不到就退回等下一包（设计文档 D6）。
     */
    if (conn->phase == PHASE_PRE_AUTH) {
        int line_len = find_line_end(conn->inbuf, conn->inlen);
        int consume;

        if (line_len < 0) {
            /* 还没收到完整首行：超过上限说明对端在发畸形数据，断开 */
            if (conn->inlen > MAX_HANDSHAKE_LEN) {
                fprintf(stderr, "fd:%d 首行超过 %d 字节仍无 CRLF，断开\n",
                        conn->fd, MAX_HANDSHAKE_LEN);
                conn->close(conn);
                return -1;
            }
            return 0;               /* 等下一包 */
        }

        conn->serve_type = parse_serve_type(conn->inbuf);
        consume = line_len + 2;     /* 含 CRLF */

        switch (conn->serve_type) {
            case SERVE_HTTP:
                conn->phase = PHASE_VIEWER;
                if (generate_http_response(conn) < 0) {
                    conn->close(conn);
                    return -1;
                }
                conn->send_cb = http_callback;
                break;

            case SERVE_GET_RESOURCE:
                conn->phase = PHASE_CAM;
                if (handle_resource(conn) < 0) {
                    conn->close(conn);
                    return -1;
                }
                conn->send_cb = http_callback;
                break;

            case SERVE_ECHO:
            default:
                conn->phase = PHASE_VIEWER;
                conn->send_cb = echo_callback;
                break;
        }

        /*
         * 只移出首行，【绝不 memset 整个缓冲区】（设计文档 D7）。
         *
         * 对端完全可能把握手行和第一帧放在同一个 send() 里发出，
         * 原来的 memset(inbuf, 0, CONNECT_BUF_LEN) 会把那一帧直接抹掉。
         */
        if (consume < conn->inlen) {
            int remain = conn->inlen - consume;
            memmove(conn->inbuf, conn->inbuf + consume, remain);
            conn->inlen = remain;
        } else {
            conn->inlen = 0;
        }
    }

    if (conn->send_cb != NULL)
        conn->send_cb(conn);
    return 0;
}
