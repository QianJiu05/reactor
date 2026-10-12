#include <sys/epoll.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "reactor.h"
#include "recv.h"
#include "callback.h"

/*
 *  修改某个 reactor 上 fd 的监听事件。
 *
 *  EVENT:     EPOLLIN / EPOLLOUT；传 0 表示不关心事件（EPOLL_CTL_DEL 时用）
 *  OPERATION: EPOLL_CTL_ADD / EPOLL_CTL_MOD / EPOLL_CTL_DEL
 *
 *  ★ 允许【跨线程】调用：采集端线程发现目标查看端不在自己的 reactor 上时，
 *    需要唤醒对方把 outbuf 发出去。Linux 的 epoll_ctl() 对 epoll 实例是
 *    线程安全的，但 epoll_wait() 不是 —— 所以这里只改事件掩码，
 *    绝不替目标线程做取值动作。
 *
 *  返回 0 成功，-1 失败。调用方必须检查：注册失败意味着这条连接再也收不到事件。
 */
int set_epoll(struct reactor* reactor, int EVENT, int OPERATION, int fd) {
    struct epoll_event ev;

    if (reactor == NULL || reactor->epfd < 0) {
        fprintf(stderr, "set_epoll: reactor 未就绪 (fd=%d)\n", fd);
        return -1;
    }

    memset(&ev, 0, sizeof(ev));
    if (EVENT == EPOLLIN || EVENT == EPOLLOUT)
        ev.events = EVENT;
    ev.data.fd = fd;

    if (epoll_ctl(reactor->epfd, OPERATION, fd, &ev) < 0) {
        /*
         * EEXIST / ENOENT 在正常的 ADD/MOD/DEL 流程里也会出现
         * （比如重复登记、重复删除同一个 fd），不影响连接继续工作，
         * 因此只忽略这两种；其它 errno 说明真的出问题了。
         */
        if (errno != EEXIST && errno != ENOENT) {
            fprintf(stderr, "set_epoll: epoll_ctl(op=%d, fd=%d) 失败: %s\n",
                    OPERATION, fd, strerror(errno));
            return -1;
        }
    }
    return 0;
}
