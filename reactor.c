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


void connect_init(struct connect* conn) {
    conn->recv_cb = recv_callback;
    conn->send_cb = send_callback;
}
int main (void) {

    int serverfd;
    serverfd = server_init(serverfd);

    struct sockaddr_in server_addr;
    set_sockaddr_in(&server_addr);

    server_bind(serverfd, (struct sockaddr *) &server_addr);
    
    // struct connect channel[10];


    /* 3.开始监听 */
    if (listen(serverfd, BACKLOG) == -1)
        handle_error("listen");
    

// #define MAX_EVENTS 10
//            struct epoll_event ev, events[MAX_EVENTS];
//            int listen_sock, conn_sock, nfds, epollfd;

//            /* Code to set up listening socket, 'listen_sock',
//               (socket(), bind(), listen()) omitted */

//            epollfd = epoll_create1(0);
//            if (epollfd == -1) {
//                perror("epoll_create1");
//                exit(EXIT_FAILURE);
//            }

//            ev.events = EPOLLIN;
//            ev.data.fd = listen_sock;
//            if (epoll_ctl(epollfd, EPOLL_CTL_ADD, listen_sock, &ev) == -1) {
//                perror("epoll_ctl: listen_sock");
//                exit(EXIT_FAILURE);
//            }

//            for (;;) {
//                nfds = epoll_wait(epollfd, events, MAX_EVENTS, -1);
//                if (nfds == -1) {
//                    perror("epoll_wait");
//                    exit(EXIT_FAILURE);
//                }
//                 for (n = 0; n < nfds; ++n) {
//                    if (events[n].data.fd == listen_sock) {
//                        conn_sock = accept(listen_sock,
//                                           (struct sockaddr *) &addr, &addrlen);
//                        if (conn_sock == -1) {
//                            perror("accept");
//                            exit(EXIT_FAILURE);
//                        }
//                        setnonblocking(conn_sock);
//                        ev.events = EPOLLIN | EPOLLET;
//                        ev.data.fd = conn_sock;
//                        if (epoll_ctl(epollfd, EPOLL_CTL_ADD, conn_sock,
//                                    &ev) == -1) {
//                            perror("epoll_ctl: conn_sock");
//                            exit(EXIT_FAILURE);
//                        }
//                    } else {
//                        do_use_fd(events[n].data.fd);
//                    }
//                }
//            }

    struct connect test;
    // test = channel_init();
    // struct epoll_event ev;


    // int epfd = epoll_create(1);
    // epoll_ctl(epfd,EPOLL_CTL_ADD,test.fd,&ev);
	// while (1)
	// {

    int iAddrLen = sizeof(struct sockaddr);
    test.fd = accept(serverfd, (struct sockaddr *)&server_addr, &iAddrLen);
    if (-1 != test.fd)
    {
        connect_init(&test);

        while (1)
        {
            test.recv_cb(&test);
        }				

	}

    return 0;
}


void handle_error (char* string) {
    printf("err: %s\n",string);
}

int recv_callback(struct connect* test) {
    test->rlen = recv(test->fd,test->rbuf,999,0);
    if (test->rlen <= 0)
    {
        close(test->fd);
        return -1;
    }

    test->send_cb(test);

    
}
int send_callback(struct connect* test) {
    strncpy(test->wbuf,test->rbuf,test->rlen);
    test->wlen = test->rlen;
    // printf("Get Msg: %s\n",  test.wbuf);

    int send_cnt = send(test->fd,test->wbuf,test->wlen,0);
    test->wlen -= send_cnt;

    if (test->wlen == 0) {
        memset(test->wbuf, 0, sizeof(test->wbuf));
    }
}
