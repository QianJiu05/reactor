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



/* epoll */
void connect_init(struct connect* conn, int fd) ;
void set_epoll(int EVENT, int OPERATION, int fd) ;


void* func_reactor (void* arg) {
    struct reactor* sub = (struct reactor*) arg;
    struct epoll_event events[MAX_EVENTS]; 

    connect_pool_init(sub);

    sub->epfd = epoll_create(1);

    while (1)
    {
        int nready;//就绪了多少个事件
        nready = epoll_wait(sub->epfd, events, MAX_EVENTS, -1);

                /* events[i].data.fd为已有的fd */
        for (int i = 0; i < nready; i++) {

            struct connect* this = get_connector(events[i].data.fd, sub);

            /* 有新msg进入 */
            if (events[i].events & EPOLLIN) {
                this->recv_func.recv_cb(this);

            /* outbuf已经发送完，发送新的outbuf */
            } else if (events[i].events & EPOLLOUT) {
                this->send_cb(this);
            }
        }
    }
        
}

