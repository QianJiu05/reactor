
#include <sys/types.h>          /* See NOTES */
#include <stdio.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
       #include <sys/stat.h>
       #include <fcntl.h>
       #include <sys/socket.h>
#include <errno.h>


#define SERVER_PORT 8888
#define BACKLOG     2048

struct sockaddr_in server_addr;
int iAddrLen = sizeof(struct sockaddr);

void server_bind(int serverfd, struct sockaddr* server_addr);
void set_sockaddr_in(struct sockaddr_in* server_addr);
int server_init (int serverfd);

int main (void) {

    int serverfd;
    serverfd = server_init(serverfd);
    set_sockaddr_in(&server_addr);
    server_bind(serverfd, (struct sockaddr *) &server_addr);
        
        /* 3.开始监听 */
        if (listen(serverfd, BACKLOG) == -1)
            printf("listen");

    int new_fd = accept(serverfd, (struct sockaddr *)&server_addr, &iAddrLen);
    if (new_fd == -1) {
        printf("get bad new_fd\n");
        return -1;
    }

#define BUFLEN 1024*412
    char picture[BUFLEN];
    char buffer[BUFLEN];


    int file_fd = open("featured.jpg",O_RDONLY);
    struct stat st;
    fstat(file_fd, &st);
    off_t file_size = st.st_size;
    read(file_fd, picture, file_size);

    int response_len = snprintf(buffer,BUFLEN,    
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
            "Connection: keep-alive\r\n"
            "Cache-Control: no-cache\r\n\r\n");

    send(new_fd, buffer, response_len, 0);


    memset(buffer, BUFLEN, 0);
    int len = snprintf(buffer, BUFLEN, 
                        "--frame\r\n"
                        "Content-Type: image/jpeg\r\n"
                        "Content-Length: %d\r\n\r\n", 
                        (int)file_size);

    if (len > 0 && len + file_size + 2 <= BUFLEN) {
        memcpy(buffer + len, picture, file_size);
        len += file_size;
        memcpy(buffer + len, "\r\n", 2);
        len += 2;
    }


    while(1) {
        send(new_fd, buffer, len, 0);

        usleep(50000);
    }


}




int server_init (int serverfd) {
    serverfd = socket(AF_INET, SOCK_STREAM, 0);
    if (-1 == serverfd)
	{
		printf("socket error!\n");
		return -1;
	}
    return serverfd;
}

void set_sockaddr_in(struct sockaddr_in* server_addr) {
    memset(server_addr, 0, sizeof(struct sockaddr_in));
    server_addr->sin_family = AF_INET;
    server_addr->sin_addr.s_addr = INADDR_ANY;
    server_addr->sin_port = htons(SERVER_PORT); /* host to net, short 大小端序的转换*/
}

/* 2.绑定socket与端口号 */
void server_bind(int serverfd, struct sockaddr* server_addr) {
    if (bind(serverfd, server_addr,sizeof(struct sockaddr)) == -1) {
        printf("bind");
    }
}





// int jpeg_end = -1;
    // for (int i = 0; i < BUFLEN; i++) {
    //     if ((unsigned char)buffer[i] == 0xFF && 
    //         (unsigned char)buffer[i+1] == 0xD9) {
    //         jpeg_end = i + 2;  // JPEG 结束位置
    //         break;
    //     }
    // }

    //     if (jpeg_end > 0) {
    //         // MJPEG 格式: --frame\r\nContent-Type: image/jpeg\r\nContent-Length: XXX\r\n\r\n[数据]
    //         printf("Found complete JPEG frame: %d bytes\n", jpeg_end);

    //         char frame_header[256];
    //         int header_len = snprintf(frame_header, sizeof(frame_header),
    //                     "--frame\r\n"
    //                     "Content-Type: image/jpeg\r\n"
    //                     "Content-Length: %d\r\n\r\n",
    //                     jpeg_end);
            
    //         // A. 发送帧头
    //         int sent_header = 0;
    //         while(sent_header < header_len) {
    //             int n = send(new_fd, frame_header + sent_header, header_len - sent_header, 0);
    //             if (n < 0) {
    //                 if (errno == EAGAIN) { usleep(1000); continue; } // 忙等待
    //                 return -1;
    //             }
    //             sent_header += n;
    //         }

    //         // B. 发送图片体
    //         int sent_body = 0;
    //         while (sent_body < jpeg_end) {
    //             int n = send(new_fd, buffer + sent_body, jpeg_end - sent_body, 0);
    //             if (n < 0) {
    //                 if (errno == EAGAIN) { usleep(1000); continue; }
    //                 return -1;
    //             }
    //             sent_body += n;
    //         }

    //         // C. 发送结尾换行
    //         int sent_end = 0;
    //         while (sent_end < 2) {
    //             int n = send(new_fd, "\r\n" + sent_end, 2 - sent_end, 0);
    //             if (n < 0) {
    //                 if (errno == EAGAIN) { usleep(1000); continue; }
    //                 break;
    //             }
    //             sent_end += n;
    //         }