#include <sys/epoll.h>


#include "reactor.h"
#include "recv.h"
#include "callback.h"

/*  EPOLLIN, EPOLL_CTL_ADD, fd --> 新增事件：监听 fd 的 EPOLLIN
    EVENT: EPOLLIN, EPOLL_OUT
    OPERATION: EPOLL_CTL_ADD, EPOLL_CTL_DEL */
void set_epoll(struct reactor* reactor, int EVENT, int OPERATION, int fd) {
    struct epoll_event ev;

    if (EVENT == EPOLLIN || EVENT == EPOLLOUT) { ev.events = EVENT;}
    ev.data.fd = fd;

    epoll_ctl(reactor->epfd, OPERATION, fd, &ev);
}
