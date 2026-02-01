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
#include "reactor.h"

#define LIBHTTP_REQUEST_MAX_SIZE CONNECT_BUF_LEN

const char* body_200 = "<html><body>Hello from Reactor!</body></html>";
const char* body_400 = "<html><body>400 Bad Request</body></html>";
const char* body_404 = "<html><body>404 Not found</body></html>";
const char* body_405 = "<html><body>405 Method Not Allowed</body></html>";

const char* status_200 = "OK";
const char* status_400 = "BAD";
const char* status_404 = "NOTFOUND";
const char* status_405 = "NOTALLOWED";

const char* connect_close      = "Connection: close";
const char* connect_keep_alive = "Connection: keep-alive";

const char* type_no_need        = " ";
const char* type_image_jpg      = "Content-Type: image/jpeg";
const char* type_text_html      = "Content-Type: text/html; charset=utf-8";

int http_response_status(struct connect* conn, int status_code);

int http_get_status_code (const char* request_buf) {
    int status_code = 200;

    if (request_buf == NULL || request_buf[0] == '\0') {
        status_code = 400;
    } 
    // else if (strncmp(request_buf, "GET ", 4) != 0) {
    //     status_code = 405;
    // }
    return status_code;
}

/*      "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Content-Length: 45\r\n"
        "Connection: close\r\n"
        "\r\n"
        "<html><body>Hello from Reactor!</body></html>";
        第一次连接的时候发送响应头     */
int generate_http_response(struct connect* conn) {
    char* response_buf = conn->wbuf;
    char* request_buf = conn->rbuf;
    // printf("HTTPMSG:\n%s\n",request_buf);

    int status_code = http_get_status_code(conn->rbuf);
    printf("status code = %d\n",status_code);
    if (status_code != 200) {
        return http_response_status(conn, status_code);
    }
    // 忽略 request,直接返回图片
    const char* filepath = "resource/featured.jpg";
    
    int fd = open(filepath, O_RDONLY);
    if (fd < 0) {
        printf("open bad fd\n");
        return http_response_status(conn, 404);
    }


    // 获取文件大小
    struct stat st;
    fstat(fd, &st);
    off_t file_size = st.st_size;

    // 只准备响应头,不发送!
    conn->wlen = snprintf(conn->wbuf, CONNECT_BUF_LEN,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: image/jpeg\r\n"
        "Content-Length: %ld\r\n"
        // "Connection: close\r\n\r\n",
        "Connection: keep-alive\r\n\r\n",
        file_size);
    conn->app.http.file_fd = fd;
    conn->app.http.remain = file_size;
    conn->app.http.header_sent = false;  // 新增标志:响应头是否已发送
    
    return conn->wlen;
}

/* "HTTP/1.1 200 OK\r\n" */
int http_response_status(struct connect* conn, int status_code) {
    
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
            type = type_image_jpg;
            break;
        default:
            printf("type not impled:%d\n",conn->app.http.content_type);
            break;
    }

    int body_len = strlen(body);
    int ret = snprintf(conn->wbuf, CONNECT_BUF_LEN,
        "HTTP/1.1 %d %s\r\n"    /* status_code, status_response */
        "%s\r\n"                /* type */
        "Content-Length: %d\r\n" /* body_len,  */
        "%s\r\n\r\n"            /* connection */
        "%s",                   /* body */
        status_code, status_response, type, body_len, connection, body);
    return ret;
}


int http_callback(struct connect* conn) {
    // 1. 先发送响应头
    if (conn->wlen > 0) {
        int send_cnt = send(conn->fd, conn->wbuf, conn->wlen, 0);
        if (send_cnt < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                set_epoll(EPOLLOUT, EPOLL_CTL_MOD, conn->fd);
                return 0;
            }
            conn->close(conn);
            return -1;
        }
        conn->wlen -= send_cnt;
        /* 缓冲区还没发完，触发EPOLL_OUT表示本次发送发完了，等下次epoll事件触发发下一段内容 */
        if (conn->wlen > 0) {
            memmove(conn->wbuf, conn->wbuf + send_cnt, conn->wlen);
            set_epoll(EPOLLOUT, EPOLL_CTL_MOD, conn->fd);
            return 0;
        }
    }

    // 2. 再发送文件内容
    struct http_context* http = &conn->app.http;
    if (http->file_fd >= 0 && http->remain > 0) {
        int to_read = (http->remain < CONNECT_BUF_LEN) ? http->remain : CONNECT_BUF_LEN;
        int bytes_read = read(http->file_fd, conn->wbuf, to_read);
        
        if (bytes_read > 0) {
            int send_cnt = send(conn->fd, conn->wbuf, bytes_read, 0);
            if (send_cnt < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    set_epoll(EPOLLOUT, EPOLL_CTL_MOD, conn->fd);
                    return 0;
                }
                conn->close(conn);
                return -1;
            }
            http->remain -= send_cnt;
            
            // 还有数据,继续监听 EPOLLOUT
            if (http->remain > 0) {
                set_epoll(EPOLLOUT, EPOLL_CTL_MOD, conn->fd);
                return 0;
            }
        }
        
        // 发送完毕
        close(http->file_fd);
        http->file_fd = -1;
    }

    // 3. 全部发送完毕,关闭连接
    conn->close(conn);
    // conn->serve_type = SERVE_NOT_INIT;

    return 0;
}