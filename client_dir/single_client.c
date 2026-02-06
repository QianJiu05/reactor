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

char resource_buf[1024];


int main (void) {

    int cfd = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in caddr;
    int addinlen = sizeof(caddr);

    memset(&caddr, 0, sizeof(struct sockaddr_in));
    caddr.sin_family = AF_INET;
    caddr.sin_addr.s_addr = inet_addr("68.64.177.182");
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
        return 0;
}
