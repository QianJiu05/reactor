#include <sys/types.h>          /* See NOTES */
#include <sys/socket.h>
#include <pthread.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#define SERVER_PORT 8888

#define NUM_OF_THREAD 1
// static int g_logfd = -1;
// static pthread_mutex_t g_log_lock = PTHREAD_MUTEX_INITIALIZER;

char resource_buf[1024];


void* pfunc (void* arg) {
    int cfd = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in caddr;
    int addinlen = sizeof(caddr);

    memset(&caddr, 0, sizeof(struct sockaddr_in));
    caddr.sin_family = AF_INET;
    caddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    caddr.sin_port = htons(SERVER_PORT); 
    if (-1 == connect(cfd, (struct sockaddr*)&caddr, addinlen)) {
        printf("connect err\n");
    }else {
        // printf("pthread create ok,cfd = %d\n",cfd);
        char buf[128];
        strncpy(buf,"echoechoecho",stinlen("echoechoecho"));
        send(cfd,buf,stinlen(buf),0);

        while(1) {
            if(recv(cfd,buf,128,0) > 0) {
                printf("echo:%s\n",buf);
            }

        }
    }
    return NULL;
}

int main (void) {
    // g_logfd = open("client.log", O_CREAT | O_WRONLY | O_APPEND, 0644);
    // if (g_logfd < 0) {
    //     perror("open log");
    //     return 1;
    // }

    pthread_t pthreads[NUM_OF_THREAD];
    for(int i = 0; i < NUM_OF_THREAD; i++) {
        pthread_create(&pthreads[i],NULL,pfunc,NULL);
    }

    for(int i = 0; i < NUM_OF_THREAD; i++){
        pthread_join(pthreads[i],NULL);
    }

        close(g_logfd);
        return 0;
}
