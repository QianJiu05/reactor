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


#define LIBHTTP_REQUEST_MAX_SIZE CONNECT_BUF_LEN

int http_response_status(struct connect* conn, int status_code);

static void init_send_chunk(struct connect* conn);


int http_get_status_code (const char* request_buf) {
    int status_code = 200;

    if (request_buf == NULL || request_buf[0] == '\0') {
        status_code = 400;
    } 

    return status_code;
}

/*  第一次连接的时候，初始化coon->app.http，准备响应头给send_cb发送  */
int generate_http_response(struct connect* conn) {
    char* response_buf = conn->wbuf;
    char* request_buf = conn->rbuf;
    // printf("HTTPMSG:\n%s\n",request_buf);

    int status_code = http_get_status_code(conn->rbuf);
    printf("status code = %d\n",status_code);
    if (status_code != 200) {
        return http_response_status(conn, status_code);
    }


    // 只准备响应头,不发送!
    conn->wlen = snprintf(conn->wbuf, CONNECT_BUF_LEN,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: image/jpeg\r\n"
        // "Content-Length: %ld\r\n"
        "Transfer-Encoding: chunked\r\n"
        "Connection: keep-alive\r\n\r\n"
        // file_size
    );
    init_send_chunk(conn);
    
    printf("=====HTTP RESPONSE====\n");
    printf("%s",conn->wbuf);
    printf("====END HTTP RESPONSE====\n");
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
    /* 1. 先发送响应头 */
    if (conn->app.http.header_sent == false) {
        printf("send response header\n");
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
            conn->app.http.header_sent = true;
            if (conn->app.http.remain == -1) {
                printf("HTTP fd:%d waiting for camera data\n", conn->fd);
                set_epoll(EPOLLIN, EPOLL_CTL_MOD, conn->fd);
                return 0;
            }    
        } else {
            conn->app.http.header_sent = true;
        }

    }

    /* 2. 发送文件内容 */
    struct http_context* http = &conn->app.http;
    /* 普通模式：读取file_fd然后发送 */
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
        printf("send pic OK\n");
        // 发送完毕
        close(http->file_fd);
        http->file_fd = -1;
    }

    /* 
        如果是 chunked 模式且有数据在 wbuf，发送 chunk 
        chunk 格式: <size-in-hex>\r\n<data>\r\n
    */
    if (http->remain == -1 && conn->wlen > 0) {
        printf("chunk mode\n");

        if (conn->wlen > 0) {
            char chunk_buf[CONNECT_BUF_LEN + 64];  // 额外空间用于 chunk 头尾
            int chunk_header_len = snprintf(chunk_buf, 64, "%x\r\n", conn->wlen);
            // 复制数据
            memcpy(chunk_buf + chunk_header_len, conn->wbuf, conn->wlen);
            
            // 添加 chunk 结尾
            memcpy(chunk_buf + chunk_header_len + conn->wlen, "\r\n", 2);
            
            int total_len = chunk_header_len + conn->wlen + 2;
            
            // 发送整个 chunk
            int sent = 0;
            while (sent < total_len) {
                int send_cnt = send(conn->fd, chunk_buf + sent, total_len - sent, 0);
                if (send_cnt < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        // 保存未发送的数据
                        if (sent < chunk_header_len + conn->wlen) {
                            int remaining = conn->wlen - (sent - chunk_header_len);
                            if (remaining > 0) {
                                memmove(conn->wbuf, conn->wbuf + conn->wlen - remaining, remaining);
                                conn->wlen = remaining;
                            }
                        } else {
                            conn->wlen = 0;
                        }
                        set_epoll(EPOLLOUT, EPOLL_CTL_MOD, conn->fd);
                        return 0;
                    }
                    perror("send chunk");
                    conn->close(conn);
                    return -1;
                }
                sent += send_cnt;
            }
            conn->wlen = 0;
            memset(conn->wbuf, 0, CONNECT_BUF_LEN);
        }
        
        // printf("Sent chunk: %d bytes to fd:%d\n", conn->wlen, conn->fd);
        if (conn->rlen > 0) {
            printf("copy rbuf-->wbuf\n");
            memcpy(conn->wbuf, conn->rbuf, conn->rlen);
            memset(conn->rbuf, 0, conn->rlen);
            conn->wlen += conn->rlen;
            conn->rlen = 0;
        }
        
        // 等待更多数据
        set_epoll(EPOLLIN, EPOLL_CTL_MOD, conn->fd);
        return 0;
    }

    // 3. 全部发送完毕,关闭连接，如果不关闭，刷新浏览器会建立新的连接
    if (http->keep_alive &&  http->remain != -1) {
        // 保持连接，等待下一个请求
        conn->serve_type = SERVE_NOT_INIT;  // 重置状态
        //修改监听状态，告诉 epoll，现在只关心这个连接是否有新数据到达（不再关心是否可写）
        set_epoll(EPOLLIN, EPOLL_CTL_MOD, conn->fd);
    } 

    return 0;
}



/************ helper *************/

static void init_send_chunk(struct connect* conn) {
    conn->app.http.file_fd = -1;  // 标记为不从文件读取
    conn->app.http.remain = -1;   // 表示大小未知
    conn->app.http.header_sent = false;

    // conn->app.http.file_fd = fd;
    // conn->app.http.remain = file_size;
    // conn->app.http.header_sent = false;  // 新增标志:响应头是否已发送
}