#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#define SERVER_PORT 8888
#define BUFFER_SIZE 512*1024

int main(int argc, char* argv[]) {
    if (argc != 2) {
        printf("Usage: %s <dest_http_fd>\n", argv[0]);
        printf("Example: %s 5\n", argv[0]);
        return 1;
    }
    
    int dest_fd = atoi(argv[1]);
    
    // 1. 连接到服务器
    int cfd = socket(AF_INET, SOCK_STREAM, 0);
    if (cfd < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in caddr;
    memset(&caddr, 0, sizeof(struct sockaddr_in));
    caddr.sin_family = AF_INET;
    caddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    caddr.sin_port = htons(SERVER_PORT);
    
    if (connect(cfd, (struct sockaddr*)&caddr, sizeof(caddr)) == -1) {
        perror("connect");
        close(cfd);
        return 1;
    }
    
    printf("Connected to server, cam fd: %d\n", cfd);
    
    // 2. 发送 SEND 命令，注册为相机客户端
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "SEND %d\r\n", dest_fd);
    if (send(cfd, cmd, strlen(cmd), 0) < 0) {
        perror("send command");
        close(cfd);
        return 1;
    }
    printf("Sent: %s", cmd);
    
    // 短暂等待，让服务器处理命令
    usleep(1000000);  // 100ms
    #define BUFLEN 1024*412
    char picture[BUFLEN];
    char pictur2[BUFLEN];
    char buffer[BUFLEN];
    char buffer2[BUFLEN];


    int file_fd = open("../resource/featured.jpg",O_RDONLY);
    struct stat st;
    fstat(file_fd, &st);
    off_t file_size = st.st_size;
    read(file_fd, picture, file_size);

    int file_fd2 = open("../resource/pic2.jpg",O_RDONLY);
    struct stat st2;
    fstat(file_fd2, &st2);
    off_t file_size2 = st2.st_size;
    read(file_fd2, pictur2, file_size2);  

    if (file_fd <= 2 || file_fd2 <= 2) {
        printf("fd:%d,%d\n",file_fd,file_fd2);
        return 0;
    }


    memset(buffer, BUFLEN, 0);
    int len1 = snprintf(buffer, BUFLEN, 
                        "--frame\r\n"
                        "Content-Type: image/jpeg\r\n"
                        "Content-Length: %d\r\n\r\n", 
                        (int)file_size);

    
    if (len1 > 0 && len1 + file_size + 2 <= BUFLEN) {
        memcpy(buffer + len1, picture, file_size);
        len1 += file_size;
        memcpy(buffer + len1, "\r\n", 2);
        len1 += 2;
    }

    int len2 = snprintf(buffer2, BUFLEN, 
                    "--frame\r\n"
                    "Content-Type: image/jpeg\r\n"
                    "Content-Length: %d\r\n\r\n", 
                    (int)file_size2);
    if (len2 > 0 && len2 + file_size2 + 2 <= BUFLEN) {
        memcpy(buffer2 + len2, pictur2, file_size2);
        len2 += file_size2;
        memcpy(buffer2 + len2, "\r\n", 2);
        len2 += 2;
    }



    int send_1, send_2;
    while(1) {
        send_1 = send(cfd, buffer, len1, 0);
        usleep(50000);
        send_2 = send(cfd,buffer2,len2,0);
        usleep(50000);
        printf("send1=%d,sen2=%d\n",send_1,send_2);
    }
    close(cfd);

    
    return 0;
}



/*  // 3. 打开图片文件
    const char* filepath = "../resource/featured.jpg";
    int file_fd = open(filepath, O_RDONLY);
    if (file_fd < 0) {
        perror("open file");
        close(cfd);
        return 1;
    }
    
    // 获取文件大小
    struct stat st;
    fstat(file_fd, &st);
    off_t file_size = st.st_size;
    printf("File size: %ld bytes\n", file_size);
    
    // 4. 读取并发送文件内容
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;
    ssize_t total_sent = 0;
    
    while ((bytes_read = read(file_fd, buffer, BUFFER_SIZE)) > 0) {
        ssize_t bytes_sent = 0;
        while (bytes_sent < bytes_read) {
            ssize_t ret = send(cfd, buffer + bytes_sent, bytes_read - bytes_sent, 0);
            if (ret < 0) {
                perror("send data");
                close(file_fd);
                close(cfd);
                return 1;
            }
            bytes_sent += ret;
            total_sent += ret;
        }
        printf("Progress: %ld / %ld bytes (%.1f%%)\r", 
               total_sent, file_size, (total_sent * 100.0) / file_size);
        fflush(stdout);
    }
    
    printf("\nFile sent successfully: %ld bytes\n", total_sent);
    
    // 5. 清理资源
    close(file_fd);
    close(cfd);


*/