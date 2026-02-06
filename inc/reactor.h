#ifndef __REACTOR_H__
#define __REACTOR_H__

#define SERVE_NOT_INIT     -1
#define SERVE_ECHO          0
#define SERVE_HTTP          1
#define SERVE_GET_RESOURCE  2

#include <sys/epoll.h>
#include <pthread.h>
#include "connect_pool.h"


struct reactor{
    pthread_t tid;
    int epfd;          
    int event_fd;
    struct connect_pool pool;


};

void connect_init(struct connect* conn, int fd);
void set_epoll(int EVENT, int OPERATION, int fd);
int accept_callback(struct connect* conn);

void reactor_init(struct reactor*);
#endif
