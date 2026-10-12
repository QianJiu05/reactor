/*
 *  服务器端主程序（epoll 多 reactor 并发模型）
 *
 *  两条链路：
 *    相机客户端 --(自定义协议帧)--> 解析 --> 本地落盘
 *                                      └--> 广播给所有查看端
 *    查看端     --(HTTP GET)------> 回 multipart/x-mixed-replace 头，
 *                                   之后持续收 MJPEG 分段，浏览器直接播放
 *
 *  用法:
 *      ./reactor                 使用 config.h 里的端口与落盘目录
 *      ./reactor <端口> <落盘目录>
 *
 *  查看方式: 浏览器打开 http://<服务器IP>:8888/
 */

#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
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
#include "recorder.h"

int g_server_port = SERVER_PORT;

static struct reactor main_reactor;
static struct epoll_event main_ev;

sem_t sub_init;

int main (int argc, char **argv) {
    int port = SERVER_PORT;
    const char *record_dir = RECORD_DIR;

    if (argc >= 2)
        port = atoi(argv[1]);
    if (argc >= 3)
        record_dir = argv[2];
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "端口非法: %s\n", argv[1]);
        return 1;
    }
    g_server_port = port;

    /* 忽略 SIGPIPE：对端断开时靠 send/recv 的返回值处理，
     * 否则一个查看端掉线就会把整个进程带走 */
    signal(SIGPIPE, SIG_IGN);

    printf("=========== 视频转播服务器 ===========\n");
    printf("监听端口 : %d\n", port);
    printf("查看地址 : http://<本机IP>:%d/\n", port);
    printf("落盘目录 : %s\n", record_dir);
    printf("=====================================\n");

    /* 落盘初始化失败不致命：转发链路照常工作 */
    recorder_init(record_dir);

    connect_pool_init();

    int serverfd;

    struct sockaddr_in *server_addr = get_sockaddr_in();
    serverfd = server_init(serverfd);
    set_sockaddr_in(server_addr);
    server_bind(serverfd);

    sem_init(&sub_init, 0, 0);

    /* 创建子线程运行 sub reactor。
     * init_sub_reactor() 返回时保证所有 sub->epfd 已就绪，
     * 因此后续可以安全地跨线程调用 set_epoll()。 */
    init_sub_reactor();

    /* 开始监听 */
    if (listen(serverfd, BACKLOG) == -1) {
        perror("listen");
        return 1;
    }

    main_reactor.epfd = epoll_create(1);
    if (main_reactor.epfd < 0) {
        perror("epoll_create");
        return 1;
    }
    /* 主线程只监听输入：新事件 */
    if (set_epoll(&main_reactor, EPOLLIN, EPOLL_CTL_ADD, serverfd) < 0) {
        fprintf(stderr, "无法把 listen fd 注册到 epoll\n");
        return 1;
    }
    printf("服务器已就绪，等待连接...\n");

    while (1)
    {
        int nready;//就绪了多少个事件
        nready = epoll_wait(main_reactor.epfd, &main_ev, 1, -1);
        if (nready < 0) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            break;
        }

        if (serverfd == main_ev.data.fd) {
            /*  listen到新连接，通过accept(serverfd)建立新连接
                批量 accept，直到返回 EAGAIN         */
            while (1) {
                int client_fd = accept_callback(serverfd);
                if (client_fd == -1)
                    break;  // 没有更多连接了

                /* 把client_fd分发给sub_reactor */
                struct reactor* target = get_next_reactor();
                patch_connect(target, client_fd);
            }
        }
    }

    close(serverfd);
    return 0;
}
