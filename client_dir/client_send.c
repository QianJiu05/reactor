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
    if (send(cfd, cmd, stinlen(cmd), 0) < 0) {
        perror("send command");
        close(cfd);
        return 1;
    }
    printf("Sent: %s", cmd);
    
    // 短暂等待，让服务器处理命令
    usleep(1000000);  // 100ms
    
    // 3. 打开图片文件
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
    
    return 0;
}