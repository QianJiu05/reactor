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
#include <semaphore.h>

#include "config.h"
#include "connect_pool.h"
#include "reactor.h"
#include "http_handler.h"
#include "recv_resource.h"
#include "server_init.h"
#include "config.h"
#include "epoll.h"

extern sem_t sub_init;

static struct reactor sub_reactor[NUM_OF_REACTOR];
static int g_next_reactor;      /* 轮询分配用的下标 */

/* epoll */
// void connect_init(struct connect* conn, int fd) ;

void* func_reactor (void* arg) {
    struct reactor* sub = (struct reactor*) arg;
    // struct epoll_event *events = sub->events;
    struct epoll_event *events = malloc(sizeof(struct epoll_event)* MAX_EVENTS);
    if (events == NULL) {
        printf("reactor %ld: malloc events failed\n", (long)sub->tid);
        return NULL;
    }

    sub->epfd = epoll_create(1);

    while (1)
    {
        int nready;//就绪了多少个事件
        nready = epoll_wait(sub->epfd, events, MAX_EVENTS, -1);
        if (nready < 0) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            break;
        }

        /* events[i].data.fd为已有的fd */
        for (int i = 0; i < nready; i++) {
            struct connect* this = get_connector(events[i].data.fd);
            if (this == NULL || this->state == STATE_CLOSED)
                continue;

            /*
             * 三种事件都交给 recv_cb / send_cb 处理：
             *   EPOLLIN  → 收到对端数据（相机推送的帧 / 查看端的 HTTP 请求）
             *   EPOLLOUT → 发送缓冲区可写，把 outbuf 里剩下的数据发完
             * 用 if 而不是 else if：两者同时就绪时要都处理，
             * 否则先处理完发送、残留的接收数据要等下一次事件才被读取。
             */
            if (events[i].events & (EPOLLIN | EPOLLOUT)) {
                if (this->send_cb != NULL)
                    this->send_cb(this);
                else if (this->recv_func.recv_cb != NULL)
                    this->recv_func.recv_cb(this);
            }
        }
    }
}

void init_sub_reactor(void) {
    memset(sub_reactor, 0, sizeof sub_reactor);
    for (int i = 0; i < NUM_OF_REACTOR; i++) {
        struct reactor* sub =  &sub_reactor[i];
        if (0 != pthread_create(&sub->tid, NULL, func_reactor, sub)) {
            printf("thread create failed\n");
            // return false;   
            exit(-1);
        }
    }
    // printf("finish\n");
    sem_post(&sub_init);
    // return true;
}



struct reactor* get_next_reactor(void){
    /* 从 0 开始轮询分配：原来先自增再取模，会跳过 sub_reactor[0] */
    struct reactor* r = &sub_reactor[g_next_reactor];
    g_next_reactor = (g_next_reactor + 1) % NUM_OF_REACTOR;
    return r;
}

void patch_connect(struct reactor* target, int fd) {
    struct connect* connector = get_connector(fd);
    if (connector == NULL) {
        printf("patch_connect: get_connector(%d) failed\n", fd);
        close(fd);
        return;
    }
    connect_init(connector, fd);
    connector->sub = target;
    set_epoll(target, EPOLLIN, EPOLL_CTL_ADD, fd);
}