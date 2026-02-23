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
#include <time.h>

#include "config.h"
#include "connect_pool.h"
#include "reactor.h"
#include "http_handler.h"
#include "recv_resource.h"
#include "server_init.h"
#include "epoll.h"
#include "accept.h"

static int sem_wait_timeout(sem_t *sem, int timeout_ms);

static struct reactor main_reactor;
static struct epoll_event *events = main_reactor.events;

sem_t sub_init;

int main (void) {
    connect_pool_init();

    int serverfd;

    struct sockaddr_in *server_addr = get_sockaddr_in();
    serverfd = server_init(serverfd);
    // set_sockaddr_in(&server_addr);
    set_sockaddr_in(server_addr);
    // server_bind(serverfd, &server_addr);
    server_bind(serverfd);

    sem_init(&sub_init, 0, 0);
    
    //创建子线程运行sub reactor
    init_sub_reactor();
    sem_wait_timeout(&sub_init, 500);
    
    /* 开始监听 */
    if (listen(serverfd, BACKLOG) == -1)
        printf("listen");

    main_reactor.epfd = epoll_create(1);
    /* 主线程只监听输入：新事件 */
    set_epoll(&main_reactor, EPOLLIN, EPOLL_CTL_ADD, serverfd);

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


// 等待指定毫秒，成功返回0，超时返回-1
int sem_wait_timeout(sem_t *sem, int timeout_ms) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (timeout_ms % 1000) * 1000000;
    // 处理进位
    if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000;
    }
    return sem_timedwait(sem, &ts);
}









