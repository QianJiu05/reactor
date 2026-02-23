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

int parse_serve_type(struct connect* conn) {

    if (strncmp(conn->inbuf, "GET ", 4) == 0) {
        return SERVE_HTTP;
    } else if (strncmp(conn->inbuf, "SEND ", 5) == 0) {
        return SERVE_GET_RESOURCE;
    } 

    return SERVE_ECHO;
}

int recv_callback(struct connect* conn) {
    /* 把数据接收到inbuf */
    int to_copy = CONNECT_BUF_LEN - conn->inlen;
    conn->inlen += recv(conn->fd, conn->inbuf + conn->inlen , to_copy, 0);

    /* inlen == 0 表示对端正常关闭，应直接 close，不要看 errno。
        只有 inlen < 0 才检查 errno == EAGAIN/EWOULDBLOCK。 */
    if (conn->inlen == 0){
        conn->close(conn);
        return -1;
    } else if (conn->inlen < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;  // 继续等待
        } else {
            conn->close(conn);
            return -1;
        }
    }
    // conn->inbuf[conn->inlen] = '\0';

    if (conn->serve_type == SERVE_NOT_INIT || conn->serve_type == SERVE_HTTP) {
        // 解析inlen，GET-->HTTP/ SEND-->recv from cam/ echo
        // 生成 HTTP 响应(初始化 file_fd 和 remaining)
        int serve_type = parse_serve_type(conn);
        conn->serve_type = serve_type;
        switch(conn->serve_type) {
            case SERVE_HTTP:
                generate_http_response(conn);
                conn->send_cb = http_callback;
                break;
    
            case SERVE_GET_RESOURCE:
                handle_resource(conn);
                conn->send_cb = get_resource_callback;
                break;

            case SERVE_ECHO:
                // conn->send_cb = echo_callback;
                conn->send_cb = print_callback;

                break;

            default:
                printf("err serve type:%d\n",serve_type);
        }
    }
    
    conn->send_cb(conn);
    return 0;
    // printf("fd:%d msg:%s\n",conn->fd,conn->inbuf);
}