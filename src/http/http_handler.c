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


#define LIBHTTP_REQUEST_MAX_SIZE CONNECT_BUF_LEN

static int http_response_status(struct connect* conn, int status_code);
static void init_send_stream(struct connect* conn);

int http_get_status_code (const char* request_buf) {
    int status_code = 200;

    if (request_buf == NULL || request_buf[0] == '\0') {
        status_code = 400;
    } 

    return status_code;
}

/*  第一次连接的时候，初始化coon->app.http，发送响应头  */
int generate_http_response(struct connect* conn) 
{
    int status_code = http_get_status_code(conn->inbuf);
    // printf("status code = %d\n",status_code);
    if (status_code != 200) { return http_response_status(conn, status_code); }

    conn->outlen = snprintf(conn->outbuf, CONNECT_BUF_LEN,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
            "Connection: keep-alive\r\n"
            "Cache-Control: no-cache\r\n\r\n");

    init_send_stream(conn);

    // 循环发送，直到发完或出错
    int total_sent = 0;
    int data_len = conn->outlen;

    while (total_sent < data_len) {
        int n = send(conn->fd, conn->outbuf + total_sent, data_len - total_sent, 0);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 缓冲区满了，简单忙等待，确保头必须发出去
                usleep(1000); 
                continue;
            }
            // 真正的发送错误
            perror("send header error");
            return -1;
        }
        total_sent += n;
    }

    // 发送完毕，清理状态
    conn->outlen = 0;
    memset(conn->outbuf, 0, CONNECT_BUF_LEN);
    conn->app.http.header_sent = true;

    // 空 inbuf，丢弃 "GET / HTTP/1.1..." 这些请求头数据
    memset(conn->inbuf, 0, CONNECT_BUF_LEN);
    conn->inlen = 0;

    return 0;
}

/* 
 *  让cam端直接封装好 jpg帧头帧尾
 *  http——callback只把数据从inbuf --> outbuf
 *  outbuf --> send
 */
int http_callback(struct connect* conn) 
{
    int to_copy = conn->inlen;
    int res = CONNECT_BUF_LEN - conn->outlen;
    if (to_copy > res) to_copy = res;

    if (to_copy > 0) {
        memcpy(conn->outbuf + conn->outlen, conn->inbuf, to_copy);
        conn->outlen += to_copy;
        conn->inlen -= to_copy;

        if (conn->inlen > 0) {
            memmove(conn->inbuf, conn->inbuf + conn->inlen, conn->inlen);
            conn->inlen = 0;
        }
    }

    if (conn->outlen > 0) {/* 有数据可发 */
        int send_cnt = send(conn->fd, conn->outbuf, conn->outlen, 0);
        if (send_cnt < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 发送缓冲区满，等待 EPOLLOUT 事件
                set_epoll(conn->sub, EPOLLOUT, EPOLL_CTL_MOD, conn->fd);
                return 0;
            }
            // 发送错误
            perror("send error");
            return -1;
        }

        conn->outlen -= send_cnt;

        if (conn->outlen > 0) {/* 没发完，设置EPOLLOUT事件，下次接着发outbuf剩下的 */
            /* 发了send_cnt， 还剩outlen， 前移outlen个 */
            memmove(conn->outbuf, conn->outbuf + send_cnt, conn->outlen);
            set_epoll(conn->sub, EPOLLOUT, EPOLL_CTL_MOD, conn->fd);
        } else {/* 全都发完了，设置EPOLLIN，继续接收数据进行发送 */
            memset(conn->outbuf, 0, CONNECT_BUF_LEN);
            set_epoll(conn->sub, EPOLLIN, EPOLL_CTL_MOD, conn->fd);
        }
    } else {/* 无数据可发，设置epollin，等待新数据 */
        set_epoll(conn->sub, EPOLLIN, EPOLL_CTL_MOD, conn->fd);
    }

}


/************ helper *************/
static void init_send_stream(struct connect* conn) {
    conn->app.http.file_fd = -1;  // 标记为不从文件读取
    conn->app.http.remain = -1;   // 表示大小未知
    conn->app.http.header_sent = false;
    conn->app.http.stream_mode = true;
}

/* "HTTP/1.1 200 OK\r\n" */
static int http_response_status(struct connect* conn, int status_code) {
    const char* status_response;
    const char* body;
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
            printf("status code not impled:%d\n",status_code);
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
            // type = type_image_jpg;
            type = type_multipart;
            break;
        default:
            printf("type not impled:%d\n",conn->app.http.content_type);
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
    return ret;
}