#include <sys/epoll.h>
#include <fcntl.h>
#include <errno.h>

#include "reactor.h"
#include "recv.h"
#include "callback.h"

/* 
    EPOLLIN, EPOLL_CTL_ADD, fd --> 新增事件：监听 fd 的 EPOLLIN
    EVENT: EPOLLIN, EPOLL_OUT
    OPERATION: EPOLL_CTL_ADD, EPOLL_CTL_DEL
*/
void set_epoll(struct reactor* reactor, int EVENT, int OPERATION, int fd) {
    struct epoll_event ev;

    if (EVENT == EPOLLIN || EVENT == EPOLLOUT) { ev.events = EVENT;}
    ev.data.fd = fd;

    epoll_ctl(reactor->epfd, OPERATION, fd, &ev);
}


/******************************* epoll  *******************************/
void connect_init(struct connect* conn, int fd) {
    conn->fd = fd;
    conn->inlen = 0;
    conn->outlen = 0;
    conn->recv_func.recv_cb = recv_callback;
    conn->close = close_callback;
    conn->serve_type = SERVE_NOT_INIT;

    // 设置为非阻塞模式，没消息直接返回
    // 如果是阻塞，send/recv会在没有消息时一直阻塞等待，直到有消息
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}