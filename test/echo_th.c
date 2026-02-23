#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <pthread.h>
#define SERVER_PORT 8888
#define BUFFER_SIZE 512*1024

#define NUM_OF_FD   800

#define NUM_OF_TH   5


void* pfunc(void) {
    int fds[NUM_OF_FD];

    for (int i = 0; i < NUM_OF_FD; i++) {
        fds[i] = socket(AF_INET, SOCK_STREAM, 0);
        if (fds[i] < 0) {
            perror("socket");
            return NULL;
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
            return NULL;
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
}
int main(int argc, char* argv[]) {
    pthread_t ths[NUM_OF_TH];

    for (int i = 0; i < NUM_OF_TH; i++) {
        pthread_create(&ths[i], NULL, pfunc, NULL);
    }
    
    printf("create\n");

    for (int i = 0; i < NUM_OF_TH; i++) {
        pthread_join(ths[i], NULL);
    }

    printf("done\n");
    return 0;
}


