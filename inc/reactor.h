#ifndef __REACTOR_H__
#define __REACTOR_H__



#include <sys/epoll.h>
#include <pthread.h>
#include "connect_pool.h"


struct reactor{
    pthread_t tid;
    int epfd;          
    // int event_fd;
    struct epoll_event events[MAX_EVENTS]; 
};


void init_sub_reactor(void);
void* func_reactor (void*);
struct reactor* get_next_reactor(void);
void patch_connect(struct reactor* target, int fd);

void reactor_init(struct reactor*);
#endif
