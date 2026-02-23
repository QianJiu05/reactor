#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>          /* See NOTES */
#include <sys/socket.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>

#include "config.h"
#include "connect_pool.h"
#include "reactor.h"
#include "http_handler.h"
#include "recv_resource.h"
#include "server_init.h"
#include "epoll.h"
#include "accept.h"

struct reactor main_reactor;

int main (void) {
    connect_pool_init();

    int serverfd;

    struct sockaddr *server_addr = get_sockaddr_in();
    serverfd = server_init(serverfd);
    set_sockaddr_in(&server_addr);
    server_bind(serverfd, (struct sockaddr *) &server_addr);
    
    /* 开始监听 */
    if (listen(serverfd, BACKLOG) == -1)
        printf("listen");

    //创建子线程运行sub reactor
    if (!init_sub_reactor()) {
        printf("sub reactor run failed\n");
        exit(-1);
    }

    struct epoll_event *events = main_reactor.events;

    main_reactor.epfd = epoll_create(1);
    set_epoll(&main_reactor, EPOLLIN | EPOLLOUT, EPOLL_CTL_ADD, serverfd);

    while (1)
    {
        int nready;//就绪了多少个事件
        nready = epoll_wait(main_reactor.epfd, events, MAX_EVENTS, -1);

        for (int i = 0; i < nready; i++) {
            if (serverfd == events[i].data.fd) {
                /* listen到新连接，通过accept(serverfd)建立新连接 */
                int client_fd = accept_callback(serverfd);

                /* 把client_fd分发给sub_reactor */
                struct reactor* target = get_next_reactor();
                patch_connect(target, client_fd);
            } 
        }
        
    }	
    return 0;
}












