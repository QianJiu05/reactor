#ifndef __REACTOR_H__
#define __REACTOR_H__



#include <sys/epoll.h>
#include <pthread.h>
#include "connect_pool.h"
#include <sys/epoll.h>


struct reactor{
    pthread_t tid;
    int epfd;
};


/*
 * 创建 NUM_OF_REACTOR 个 sub reactor 线程。
 * 返回时保证所有 sub reactor 的 epfd 都已创建完毕 —— 这一点很关键：
 * 账号体系里采集端线程会通过 set_epoll() 去唤醒【别的】线程上的查看端，
 * 如果 epfd 还没建好，那次 epoll_ctl 就会失败。
 */
void init_sub_reactor(void);
void* func_reactor (void*);
struct reactor* get_next_reactor(void);
void patch_connect(struct reactor* target, int fd);

#endif
