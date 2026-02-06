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
int generate_http_response(struct connect* conn) {

    int status_code = http_get_status_code(conn->inbuf);
    printf("status code = %d\n",status_code);
    if (status_code != 200) { return http_response_status(conn, status_code); }

    // 生成响应头
    // conn->outlen = snprintf(conn->outbuf, CONNECT_BUF_LEN,
    //                 "HTTP/1.1 200 OK\r\n"
    //                 "Content-Type: image/jpeg\r\n"
    //                 // "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
    //                 "Connection: close\r\n"
    //                 // "Connection: keep-alive\r\n"
    //                 "Cache-Control: no-cache\r\n\r\n");

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
                // 缓冲区满了，稍微休息一下再重试（简单忙等待，确保头必须发出去）
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
    printf("Headers sent. Cleaning inbuf/outbuf for stream data.\n");
    memset(conn->inbuf, 0, CONNECT_BUF_LEN);
    conn->inlen = 0;

    return 0;
}



int http_callback(struct connect* conn) {
    /* 响应头 */
    if (conn->app.http.header_sent == false) {
        printf("didn't send header\n");
    }

    /* 2. 发送文件内容 */
    struct http_context* http = &conn->app.http;
    /* 如果是 chunked 模式且有数据在 outbuf，发送 chunk 
        chunk 格式: <size-in-hex>\r\n<data>\r\n  */
    if (http->stream_mode == true){
        if (conn->inlen > 0) {
            printf("copy inbuf-->outbuf (inlen=%d)\n", conn->inlen);

            if (conn->outlen + conn->inlen > CONNECT_BUF_LEN) {
                printf("ERROR: Buffer overflow! outlen=%d + inlen=%d > %d\n",
                    conn->outlen, conn->inlen, CONNECT_BUF_LEN);
                return 0;
            }

            // 追加而不是覆盖
            memcpy(conn->outbuf + conn->outlen, conn->inbuf, conn->inlen);
            conn->outlen += conn->inlen;
            conn->inlen = 0;
            memset(conn->inbuf, 0, CONNECT_BUF_LEN);
        }

        // 检查是否有完整的 JPEG（查找 FF D9 结束标记）
        int jpeg_end = -1;
        for (int i = 0; i < conn->outlen - 1; i++) {
            if ((unsigned char)conn->outbuf[i] == 0xFF && 
                (unsigned char)conn->outbuf[i+1] == 0xD9) {
                jpeg_end = i + 2;  // JPEG 结束位置
                break;
            }
        }

        if (jpeg_end > 0) {
            // MJPEG 格式: --frame\r\nContent-Type: image/jpeg\r\nContent-Length: XXX\r\n\r\n[数据]
            printf("Found complete JPEG frame: %d bytes\n", jpeg_end);

            //             // 1. 直接发送图片数据
            // int sent_body = 0;
            // while (sent_body < jpeg_end) {
            //     int n = send(conn->fd, conn->outbuf + sent_body, jpeg_end - sent_body, 0);
            //     if (n < 0) {
            //         if (errno == EAGAIN) { usleep(1000); continue; }
            //         conn->close(conn); return -1;
            //     }
            //     sent_body += n;
            // }

            // printf("Sent JPEG image: %d bytes to fd:%d\n", jpeg_end, conn->fd);
            // close(conn->fd);
            char frame_header[256];
            int header_len = snprintf(frame_header, sizeof(frame_header),
                        "--frame\r\n"
                        "Content-Type: image/jpeg\r\n"
                        "Content-Length: %d\r\n\r\n",
                        jpeg_end);
            
            // A. 发送帧头
            int sent_header = 0;
            while(sent_header < header_len) {
                int n = send(conn->fd, frame_header + sent_header, header_len - sent_header, 0);
                if (n < 0) {
                    if (errno == EAGAIN) { usleep(1000); continue; } // 忙等待
                    conn->close(conn); return -1;
                }
                sent_header += n;
            }

            // B. 发送图片体
            int sent_body = 0;
            while (sent_body < jpeg_end) {
                int n = send(conn->fd, conn->outbuf + sent_body, jpeg_end - sent_body, 0);
                if (n < 0) {
                    if (errno == EAGAIN) { usleep(1000); continue; }
                    conn->close(conn); return -1;
                }
                sent_body += n;
            }

            // C. 发送结尾换行
            int sent_end = 0;
            while (sent_end < 2) {
                int n = send(conn->fd, "\r\n" + sent_end, 2 - sent_end, 0);
                if (n < 0) {
                    if (errno == EAGAIN) { usleep(1000); continue; }
                    break;
                }
                sent_end += n;
            }

            // 发送下一帧的边界，强制浏览器渲染当前帧
            // Chrome 等浏览器常需要看到下一个 boundary 才会显示上一帧
            send(conn->fd, "--frame\r\n", 9, 0);

            printf("Sent MJPEG frame: %d bytes to fd:%d\n", jpeg_end, conn->fd);

            // 4. 移除缓冲区中已发送的数据
            if (jpeg_end < conn->outlen) {
                memmove(conn->outbuf, conn->outbuf + jpeg_end, conn->outlen - jpeg_end);
                conn->outlen -= jpeg_end;
                printf("Remaining data: %d\n", conn->outlen);
            } else {
                conn->outlen = 0;
            }
        } else {
            printf("jpeg end not found\n");
        }
        
        // 等待更多数据
        set_epoll(EPOLLIN, EPOLL_CTL_MOD, conn->fd);
        return 0;

    } 
    
    // /* 3. 全部发送完毕,保持连接，等待下一个请求 */
    // if (http->keep_alive) {
    //     // conn->serve_type = SERVE_NOT_INIT;  // 重置状态
    //     //修改监听状态，告诉 epoll，现在只关心这个连接是否有新数据到达（不再关心是否可写）
    //     set_epoll(EPOLLIN, EPOLL_CTL_MOD, conn->fd);
    // } 

    return 0;
}



/************ helper *************/

static void init_send_stream(struct connect* conn) {
    conn->app.http.file_fd = -1;  // 标记为不从文件读取
    conn->app.http.remain = -1;   // 表示大小未知
    conn->app.http.header_sent = false;
    conn->app.http.stream_mode = true;

    // conn->app.http.file_fd = fd;
    // conn->app.http.remain = file_size;
    // conn->app.http.header_sent = false;  // 新增标志:响应头是否已发送
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