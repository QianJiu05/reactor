
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "http_handler.h"
// #include "connect_pool.h"

#define LIBHTTP_REQUEST_MAX_SIZE CONNECT_BUF_LEN

const char* body_200 = "<html><body>Hello from Reactor!</body></html>";
const char* body_400 = "<html><body>400 Bad Request</body></html>";
const char* body_405 = "<html><body>405 Method Not Allowed</body></html>";


const char* status_200 = "OK";
const char* status_400 = "BAD";
const char* status_405 = "NOTALLOWED";


int http_get_status_code (const char* request_buf) {
    int status_code = 200;

    if (request_buf == NULL || request_buf[0] == '\0') {
        status_code = 400;
    } else if (strncmp(request_buf, "GET ", 4) != 0) {
        status_code = 405;
    }
    return status_code;
}

/* 
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Content-Length: 45\r\n"
        "Connection: close\r\n"
        "\r\n"
        "<html><body>Hello from Reactor!</body></html>";
*/
int generate_http_response(struct connect* conn) {

    char* response_buf = conn->wbuf;
    char* request_buf = conn->rbuf;

      // 忽略 request_buf,直接返回图片
    const char* filepath = "resource/featured.jpg";
    
    int fd = open(filepath, O_RDONLY);
    if (fd < 0) {
        const char* body_404 = "Image not found";
        return snprintf(response_buf, CONNECT_BUF_LEN,
            "HTTP/1.1 404 Not Found\r\n"
            "Content-Length: %zu\r\n"
            "Connection: close\r\n\r\n%s",
            strlen(body_404), body_404);
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
        "Connection: close\r\n\r\n",
        file_size);

    conn->file_fd = fd;
    conn->remaining = file_size;
    conn->header_sent = 0;  // 新增标志:响应头是否已发送
    
    return conn->wlen;

    // 读取文件内容
    // if (header_len + file_size < CONNECT_BUF_LEN) {
    //     read(fd, response_buf + header_len, file_size);
    //     close(fd);
    //     return header_len + file_size;
    // } else {
    //     // 文件太大
    //     close(fd);
    //     const char* body_500 = "File too large";
    //     return snprintf(response_buf, CONNECT_BUF_LEN,
    //         "HTTP/1.1 500 Internal Server Error\r\n"
    //         "Content-Length: %zu\r\n\r\n%s",
    //         strlen(body_500), body_500);
    // }
    // int status_code = http_get_status_code(request_buf);
    
    // const char* status_response;
    // const char* body;
  
    // switch (status_code)
    // {
    //     case 200 :
    //         body = body_200;
    //         status_response = status_200;
    //         break;
    //     case 400 :
    //         body = body_400;
    //         status_response = status_400;
    //         break;
    //     case 405 :
    //         body = body_405;
    //         status_response = status_405;
    //         break;
    //     default:
    //         break;
    // }

    // int body_len = (int)strlen(body);

    // int n = snprintf(
    //     response_buf,
    //     CONNECT_BUF_LEN,
    //     "HTTP/1.1 %d %s\r\n"
    //     "Content-Type: text/html; charset=utf-8\r\n"
    //     "Content-Length: %d\r\n"
    //     "Connection: close\r\n"
    //     "\r\n"
    //     "%s",
    //     status_code,
    //     status_response,
    //     body_len,
    //     body
    // );
    // return n;
}



void http_fatal_error(char* message) {
  fprintf(stderr, "%s\n", message);
  exit(ENOBUFS);
}


/* "HTTP/1.1 200 OK\r\n" */
// void http_start_response(int fd, int status_code) {
//   dprintf(fd, "HTTP/1.1 %d %s\r\n", status_code, http_get_response_message(status_code));
// }

// void http_send_header(int fd, char* key, char* value) { 
//     dprintf(fd, "%s: %s\r\n", key, value); 
// }
// void http_content_type () {

// }

void http_end_headers(int fd) { dprintf(fd, "\r\n"); }


