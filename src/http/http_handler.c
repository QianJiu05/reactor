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
#include "protocal.h"

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

#define MJPEG_SENDBUF_SIZE (512 * 1024)  // 512KB，与 CONNECT_BUF_LEN 保持一致
static uint8_t sendbuf[MJPEG_SENDBUF_SIZE];
struct frame_header local_frame;

static int pack_jpg(uint8_t* sendbuf, uint8_t* rawbuf, int buflen) {
    int len;
    len = snprintf((char*)sendbuf, MJPEG_SENDBUF_SIZE,
                        "--frame\r\n"
                        "Content-Type: image/jpeg\r\n"
                        "Content-Length: %d\r\n\r\n",
                        (int)buflen);
    printf("[pack_jpg] header_len=%d jpeg_len=%d total_need=%d buf_size=%d\n",
           len, buflen, len + buflen + 2, MJPEG_SENDBUF_SIZE);

    if (len <= 0) {
        printf("[pack_jpg] snprintf failed\n");
        return -1;
    }
    if (len + buflen + 2 > MJPEG_SENDBUF_SIZE) {
        printf("[pack_jpg] ERROR: buffer too small! need=%d have=%d\n",
               len + buflen + 2, MJPEG_SENDBUF_SIZE);
        return -1;
    }

    memcpy(sendbuf + len, rawbuf, buflen);
    len += buflen;
    memcpy(sendbuf + len, "\r\n", 2);
    len += 2;

    printf("[pack_jpg] packed total=%d\n", len);
    return len;
}

/** 解析cam端的frame，打包加上mjpg的帧头帧尾
 */
int http_callback(struct connect* conn) 
{
    // === 入口 debug ===
    printf("[http_cb] fd=%d inlen=%d outlen=%d header_sent=%d\n",
           conn->fd, conn->inlen, conn->outlen, conn->app.http.header_sent);

    if (!conn->app.http.header_sent) {
        printf("[http_cb] WARNING: header not sent yet, skip\n");
        set_epoll(conn->sub, EPOLLIN, EPOLL_CTL_MOD, conn->fd);
        return 0;
    }

    // === 第一步：在 inbuf 中搜索帧头魔数 ===
    int frame_hdr_len = sizeof(struct frame_header);
    int magic_pos = -1;

    for (int i = 0; i <= conn->inlen - 2; i++) {
        if (*(uint16_t*)(conn->inbuf + i) == PROTO_MAGIC) {
            magic_pos = i;
            break;
        }
    }

    if (magic_pos < 0) {
        if (conn->inlen > 1) {
            conn->inbuf[0] = conn->inbuf[conn->inlen - 1];
            conn->inlen = 1;
        }
        printf("[http_cb] magic not found, inlen=%d, discard\n", conn->inlen);
        set_epoll(conn->sub, EPOLLIN, EPOLL_CTL_MOD, conn->fd);
        return 0;
    }

    if (magic_pos > 0) {
        printf("[http_cb] skip %d garbage bytes before magic\n", magic_pos);
        memmove(conn->inbuf, conn->inbuf + magic_pos, conn->inlen - magic_pos);
        conn->inlen -= magic_pos;
    }
    printf("[http_cb] magic found at pos=%d inlen_after=%d\n", magic_pos, conn->inlen);

    // === 第二步：检查 frame_header 是否完整 ===
    if (conn->inlen < frame_hdr_len) {
        printf("[http_cb] incomplete header: inlen=%d need=%d, wait\n", conn->inlen, frame_hdr_len);
        set_epoll(conn->sub, EPOLLIN, EPOLL_CTL_MOD, conn->fd);
        return 0;
    }

    // === 第三步：peek jpeg_len ===
    struct frame_header *hdr = (struct frame_header*)conn->inbuf;
    uint32_t jpeg_len = hdr->jpeg_len;
    printf("[http_cb] frame_hdr_len=%d jpeg_len=%u total_need=%d\n",
           frame_hdr_len, jpeg_len, frame_hdr_len + (int)jpeg_len);

    if (jpeg_len == 0 || jpeg_len > MJPEG_SENDBUF_SIZE) {
        printf("[http_cb] INVALID jpeg_len=%u (max=%d), skip magic\n", jpeg_len, MJPEG_SENDBUF_SIZE);
        memmove(conn->inbuf, conn->inbuf + 2, conn->inlen - 2);
        conn->inlen -= 2;
        set_epoll(conn->sub, EPOLLIN, EPOLL_CTL_MOD, conn->fd);
        return 0;
    }

    int total_frame = frame_hdr_len + (int)jpeg_len;
    if (conn->inlen < total_frame) {
        printf("[http_cb] incomplete frame: inlen=%d need=%d, wait\n", conn->inlen, total_frame);
        set_epoll(conn->sub, EPOLLIN, EPOLL_CTL_MOD, conn->fd);
        return 0;
    }

    // === 第四步：校验帧 ===
    if (!depack_frame(conn->inbuf, &local_frame)) {
        printf("[http_cb] depack FAILED, skip 2 bytes and retry\n");
        memmove(conn->inbuf, conn->inbuf + 2, conn->inlen - 2);
        conn->inlen -= 2;
        set_epoll(conn->sub, EPOLLIN, EPOLL_CTL_MOD, conn->fd);
        return 0;
    }
    printf("[http_cb] depack OK jpeg_len=%u\n", local_frame.jpeg_len);

    // === 第五步：打包 MJPEG 帧 ===
    uint8_t* jpeg_data = (uint8_t*)conn->inbuf + frame_hdr_len;
    /* 检查 JPEG SOI/EOI 标记 */
    printf("[http_cb] jpeg first2=0x%02X%02X last2=0x%02X%02X\n",
           jpeg_data[0], jpeg_data[1],
           jpeg_data[local_frame.jpeg_len - 2], jpeg_data[local_frame.jpeg_len - 1]);

    int packed_len = pack_jpg(sendbuf, jpeg_data, local_frame.jpeg_len);
    if (packed_len <= 0) {
        printf("[http_cb] pack_jpg FAILED, skip frame\n");
        int remain_in = conn->inlen - total_frame;
        if (remain_in > 0) memmove(conn->inbuf, conn->inbuf + total_frame, remain_in);
        conn->inlen = remain_in > 0 ? remain_in : 0;
        set_epoll(conn->sub, EPOLLIN, EPOLL_CTL_MOD, conn->fd);
        return 0;
    }

    // === 第六步：拷贝到 outbuf ===
    int avail = CONNECT_BUF_LEN - conn->outlen;
    int to_copy = (packed_len > avail) ? avail : packed_len;
    printf("[http_cb] packed_len=%d avail_outbuf=%d to_copy=%d\n", packed_len, avail, to_copy);
    if (to_copy < packed_len) {
        printf("[http_cb] WARNING: outbuf full! frame will be truncated (will corrupt JPEG)\n");
    }
    memcpy(conn->outbuf + conn->outlen, sendbuf, to_copy);
    conn->outlen += to_copy;

    // === 第七步：消耗 inbuf 中这一帧数据 ===
    int remain_in = conn->inlen - total_frame;
    if (remain_in > 0) {
        memmove(conn->inbuf, conn->inbuf + total_frame, remain_in);
    }
    conn->inlen = remain_in > 0 ? remain_in : 0;

    // === 第八步：发送 outbuf ===
    printf("[http_cb] sending outlen=%d to fd=%d\n", conn->outlen, conn->fd);
    if (conn->outlen > 0) {
        int send_cnt = send(conn->fd, conn->outbuf, conn->outlen, 0);
        printf("[http_cb] send() returned %d (errno=%d)\n", send_cnt, send_cnt < 0 ? errno : 0);
        if (send_cnt < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                printf("[http_cb] EAGAIN set EPOLLOUT\n");
                set_epoll(conn->sub, EPOLLOUT, EPOLL_CTL_MOD, conn->fd);
                return 0;
            }
            perror("[http_cb] send error");
            return -1;
        }

        conn->outlen -= send_cnt;
        printf("[http_cb] after send: outlen remaining=%d\n", conn->outlen);

        if (conn->outlen > 0) {
            memmove(conn->outbuf, conn->outbuf + send_cnt, conn->outlen);
            set_epoll(conn->sub, EPOLLOUT, EPOLL_CTL_MOD, conn->fd);
        } else {
            memset(conn->outbuf, 0, CONNECT_BUF_LEN);
            set_epoll(conn->sub, EPOLLIN, EPOLL_CTL_MOD, conn->fd);
        }
    } else {
        set_epoll(conn->sub, EPOLLIN, EPOLL_CTL_MOD, conn->fd);
    }

    return 0;
}
// int http_callback(struct connect* conn) 
// {
//     if (*(uint16_t*)conn->inbuf != PROTO_MAGIC) {
//         return 0;
//     }
//     if (!depack_frame(conn->inbuf, &local_frame)) {
//         printf("===== depack failed =====\n");
//         return 0;
//     }
    
//     /* 把inbuf里面的frame抛弃掉，然后取frame.jpeg_len长度的jpg */
//     int frame_len = sizeof(struct frame_header);
//     memmove(conn->inbuf, conn->inbuf + frame_len, frame_len);
//     conn->inlen -= frame_len;

//     int to_copy = pack_jpg(sendbuf, conn->inbuf, local_frame.jpeg_len);
//     int res = CONNECT_BUF_LEN - conn->outlen;
//     /* TODO: 
//         这里要处理冗余，不然下一次找header找不到.或者直到找到header才去depack */
//     if (to_copy > res) to_copy = res;

//     if (to_copy > 0) {
//         memcpy(conn->outbuf + conn->outlen, conn->inbuf, to_copy);
//         conn->outlen += to_copy;
//         conn->inlen -= to_copy;

//         if (conn->inlen > 0) {
//             memmove(conn->inbuf, conn->inbuf + conn->inlen, conn->inlen);
//             conn->inlen = 0;
//         }
//     }

//     if (conn->outlen > 0) {/* 有数据可发 */
//         int send_cnt = send(conn->fd, conn->outbuf, conn->outlen, 0);
//         if (send_cnt < 0) {
//             if (errno == EAGAIN || errno == EWOULDBLOCK) {
//                 // 发送缓冲区满，等待 EPOLLOUT 事件
//                 set_epoll(conn->sub, EPOLLOUT, EPOLL_CTL_MOD, conn->fd);
//                 return 0;
//             }
//             // 发送错误
//             perror("send error");
//             return -1;
//         }

//         conn->outlen -= send_cnt;

//         if (conn->outlen > 0) {/* 没发完，设置EPOLLOUT事件，下次接着发outbuf剩下的 */
//             /* 发了send_cnt， 还剩outlen， 前移outlen个 */
//             memmove(conn->outbuf, conn->outbuf + send_cnt, conn->outlen);
//             set_epoll(conn->sub, EPOLLOUT, EPOLL_CTL_MOD, conn->fd);
//         } else {/* 全都发完了，设置EPOLLIN，继续接收数据进行发送 */
//             memset(conn->outbuf, 0, CONNECT_BUF_LEN);
//             set_epoll(conn->sub, EPOLLIN, EPOLL_CTL_MOD, conn->fd);
//         }
//     } else {/* 无数据可发，设置epollin，等待新数据 */
//         set_epoll(conn->sub, EPOLLIN, EPOLL_CTL_MOD, conn->fd);
//     }
// }


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