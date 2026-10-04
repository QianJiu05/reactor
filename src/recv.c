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

/* 根据首行报文判断这条连接的用途 */
int parse_serve_type(struct connect* conn)
{
    if (strncmp(conn->inbuf, "GET ", 4) == 0)
        return SERVE_HTTP;              /* 查看端：HTTP 请求 */
    if (strncmp(conn->inbuf, PROTO_CMD_CAM, strlen(PROTO_CMD_CAM)) == 0)
        return SERVE_GET_RESOURCE;      /* 相机客户端：自定义协议帧 */

    return SERVE_ECHO;
}

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

    if (conn->serve_type == SERVE_NOT_INIT || conn->serve_type == SERVE_HTTP) {
        // 解析首行报文：GET --> 查看端 / CAM --> 相机客户端 / 其它 --> echo
        int serve_type = parse_serve_type(conn);
        conn->serve_type = serve_type;
        switch(conn->serve_type) {
            case SERVE_HTTP:
                if (generate_http_response(conn) < 0) {
                    conn->close(conn);
                    return -1;
                }
                conn->send_cb = http_callback;
                break;

            case SERVE_GET_RESOURCE:
                if (handle_resource(conn) < 0) {
                    conn->close(conn);
                    return -1;
                }
                conn->send_cb = http_callback;
                break;

            case SERVE_ECHO:
                conn->send_cb = echo_callback;
                break;

            default:
                printf("err serve type:%d\n", serve_type);
        }
    }

    conn->send_cb(conn);
    return 0;
}
