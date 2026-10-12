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
#include "callback.h"
#include "http_handler.h"
#include "recv_resource.h"
#include "stream_source.h"
#include "server_init.h"
#include "epoll.h"

extern sem_t sub_init;

static struct reactor sub_reactor[NUM_OF_REACTOR];
static int g_next_reactor;      /* 轮询分配用的下标 */

/* epoll */
// void connect_init(struct connect* conn, int fd) ;

void* func_reactor (void* arg) {
    struct reactor* sub = (struct reactor*) arg;
    struct epoll_event *events = malloc(sizeof(struct epoll_event)* MAX_EVENTS);
    if (events == NULL) {
        printf("reactor %ld: malloc events failed\n", (long)sub->tid);
        sem_post(&sub_init);        /* 也要放行，否则 init_sub_reactor 会一直等 */
        return NULL;
    }

    sub->epfd = epoll_create(1);
    if (sub->epfd < 0) {
        perror("epoll_create");
        free(events);
        sem_post(&sub_init);
        return NULL;
    }

    /*
     * epfd 建好之后才能发信号。
     * main() 里 init_sub_reactor() 会等满 NUM_OF_REACTOR 次，
     * 因此它返回时所有 sub->epfd 一定可用 —— 这是跨线程 set_epoll() 的前提。
     */
    sem_post(&sub_init);

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
            struct connect* this;

            /*
             * 这个 fd 可能是某个查看端的「等待占位图」定时器，
             * 而不是连接 socket —— 先交给 stream_source 判定。
             * 它返回 1 表示已消化该事件。
             */
            if (stream_source_handle_timer(events[i].data.fd))
                continue;

            this = get_connector(events[i].data.fd);
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
        sub->epfd = -1;
        if (0 != pthread_create(&sub->tid, NULL, func_reactor, sub)) {
            printf("thread create failed\n");
            exit(-1);
        }
    }

    /*
     * 等满 NUM_OF_REACTOR 次「epfd 已就绪」信号再返回。
     * 原来是创建完线程就直接 sem_post，main() 又只等一次，
     * 于是返回时 epfd 可能还是 0，后续 epoll_ctl 会静默失败。
     */
    for (int i = 0; i < NUM_OF_REACTOR; i++)
        sem_wait(&sub_init);

    for (int i = 0; i < NUM_OF_REACTOR; i++) {
        if (sub_reactor[i].epfd < 0) {
            printf("sub reactor %d 初始化失败\n", i);
            exit(-1);
        }
    }
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
    if (set_epoll(target, EPOLLIN, EPOLL_CTL_ADD, fd) < 0)
        close_callback(connector);      /* 注册失败就别留着这条连接 */
}