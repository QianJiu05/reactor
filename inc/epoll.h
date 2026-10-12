#ifndef __EPOLL_H__
#define __EPOLL_H__

/*
 * 修改 reactor 上 fd 的监听事件。返回 0 成功，-1 失败。
 * 允许跨线程调用（只做 epoll_ctl 改事件掩码，不取值）。
 */
int set_epoll(struct reactor* reactor, int EVENT, int OPERATION, int fd);

#endif
