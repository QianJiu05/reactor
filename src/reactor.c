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
#include "config.h"

struct reactor sub_reactor[NUM_OF_REACTOR];

/* epoll */
// void connect_init(struct connect* conn, int fd) ;

void* func_reactor (void* arg) {
    struct reactor* sub = (struct reactor*) arg;
    struct epoll_event *events = sub->events;

    sub->epfd = epoll_create(1);

    while (1)
    {
        int nready;//就绪了多少个事件
        nready = epoll_wait(sub->epfd, events, MAX_EVENTS, -1);

        /* events[i].data.fd为已有的fd */
        for (int i = 0; i < nready; i++) {
            struct connect* this = get_connector(events[i].data.fd);

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

bool init_sub_reactor(void) {
    for (int i = 0; i < NUM_OF_REACTOR; i++) {
        struct reactor* sub =  &sub_reactor[i];
        if (0 != pthread_create(&sub->tid, NULL, func_reactor, sub)) {
            printf("thread create failed\n");
            return false;   
        }
    }
    printf("finish\n");
    return true;
}



struct reactor* get_next_reactor(void){
    static idx = 0;
    idx++;
    if (idx >= NUM_OF_REACTOR) idx = 0;

    return &sub_reactor[idx];
}

void patch_connect(struct reactor* target, int fd) {
    struct connect* connector = get_connector(fd);
    connector->sub = target;
    connect_init(connector, fd);
    set_epoll(target, EPOLLIN, EPOLL_CTL_ADD, fd);
}