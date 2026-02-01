#ifndef __REACTOR_H__
#define __REACTOR_H__

#define SERVE_NOT_INIT     -1
#define SERVE_ECHO          0
#define SERVE_HTTP          1
#define SERVE_GET_RESOURCE  2


void set_epoll(int EVENT, int OPERATION, int fd);

#endif
