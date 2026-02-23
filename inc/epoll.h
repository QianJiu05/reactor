#ifndef __EPOLL_H__
#define __EPOLL_H__

void set_epoll(struct reactor* reactor, int EVENT, int OPERATION, int fd);
void connect_init(struct connect* conn, int fd);

#endif 