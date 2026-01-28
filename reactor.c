//gcc -o reactor reactor.c

#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>          /* See NOTES */
#include <sys/socket.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <sys/epoll.h>

#include "reactor.h"

#define MAX_EVENTS 10

#define SERVER_PORT 8888
#define BACKLOG     10

void handle_error (char* string);

int recv_callback(struct connect*);
int send_callback(struct connect*);


/* 1.创建socket   AF_INET -> 允许远程通信
                 SOCK_STREAM -> TCP协议  */
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
    if (bind(serverfd, server_addr,sizeof(struct sockaddr)) == -1)
        handle_error("bind");
}


void connect_init(struct connect* conn, int fd) {
    conn->fd = fd;
    conn->rlen = 0;
    conn->wlen = 0;
    conn->recv_cb = recv_callback;
    conn->send_cb = send_callback;
}

int main (void) {

    int serverfd;
    serverfd = server_init(serverfd);

    struct sockaddr_in server_addr;
    set_sockaddr_in(&server_addr);

    server_bind(serverfd, (struct sockaddr *) &server_addr);
    
    /* 3.开始监听 */
    if (listen(serverfd, BACKLOG) == -1)
        handle_error("listen");

    struct connect connector[10];
    struct epoll_event ev;
    struct epoll_event events[10]; 

    int iAddrLen = sizeof(struct sockaddr);

    int epfd = epoll_create(1);
    ev.events = EPOLLIN;
    ev.data.fd = serverfd;
    epoll_ctl(epfd,EPOLL_CTL_ADD,serverfd,&ev);

    // printf("start\n");

    while (1)
    {
        int nready;//就绪了多少个事件
        nready = epoll_wait(epfd,events,10,-1);

        printf("epollwait:nready = %d\n",nready);

        for (int i = 0; i < nready; i++) {
            // if (events.data.fd )
            printf("gain fd:%d\n",events->data.fd);

            if (serverfd == events[i].data.fd) {
                /* 建立新连接 */
                int new_fd = accept(serverfd, (struct sockaddr *)&server_addr, &iAddrLen);
                if (new_fd == -1) {
                    printf("get bad new_fd\n");
                    break;
                }
                printf("get new fd:%d\n",new_fd);
                connect_init(&connector[new_fd],new_fd);
                ev.events = EPOLLIN;
                ev.data.fd = new_fd;
                epoll_ctl(epfd,EPOLL_CTL_ADD,new_fd,&ev);
            } else {
                /* events[i].data.fd为已有的fd */
                struct connect* this = &connector[events[i].data.fd];
                this->recv_cb(this);
            }
        }
        
    }	

    return 0;
}


void handle_error (char* string) {
    printf("err: %s\n",string);
}

int recv_callback(struct connect* coon) {
    coon->rlen = recv(coon->fd,coon->rbuf,999,0);
    if (coon->rlen <= 0)
    {
        close(coon->fd);
        return -1;
    }

    coon->send_cb(coon);
}

int send_callback(struct connect* coon) {
    strncpy(coon->wbuf,coon->rbuf,coon->rlen);
    coon->wlen = coon->rlen;
    printf("Get Msg: %s\n",  coon->wbuf);

    int send_cnt = send(coon->fd,coon->wbuf,coon->wlen,0);
    coon->wlen -= send_cnt;

    if (coon->wlen == 0) {
        memset(coon->wbuf, 0, sizeof(coon->wbuf));
    }
}
