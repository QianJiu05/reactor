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

#define NUM_OF_FD   1000
int main(int argc, char* argv[]) {
    // 1. 连接到服务器
    int fds[NUM_OF_FD];

    for (int i = 0; i < NUM_OF_FD; i++) {
        fds[i] = socket(AF_INET, SOCK_STREAM, 0);
        if (fds[i] < 0) {
            perror("socket");
            return 1;
        }
    
        struct sockaddr_in caddr;
        memset(&caddr, 0, sizeof(struct sockaddr_in));
        caddr.sin_family = AF_INET;
        // caddr.sin_addr.s_addr = inet_addr("127.0.0.1");
        caddr.sin_addr.s_addr = inet_addr("68.64.177.182");
        caddr.sin_port = htons(SERVER_PORT);
        
        if (connect(fds[i], (struct sockaddr*)&caddr, sizeof(caddr)) == -1) {
            perror("connect");
            close(fds[i]);
            return 1;
        }
    }
    
    printf("alloc 100 fd\n");

    int send_1;
    char buffer[] = "hello!\n";
    int len = strlen(buffer);
    while(1) {
        for (int i = 0; i < NUM_OF_FD; i++) {
            send_1 = send(fds[i], buffer, len, 0);
            // printf("send1=%d,fd=%d\n",send_1,fds[i]);
        }
        sleep(100);
        // close(fds[i]);
    }

    return 0;
}

