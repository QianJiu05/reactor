#ifndef __CONNECT_POOL_H__
#define __CONNECT_POOL_H__

#include <stdbool.h>
#include <sys/types.h>

#include "config.h"

struct http_context {
    int file_fd;
    off_t remain;       // 还需要从file_fd中读多少字节
    bool header_sent;   
    int status_code;    // 200, 404 ...
    int content_type;   // jpg//text//...
    bool keep_alive;    // Connection: close -- Connection: keep-alive
    char path[256];     //请求路径
    bool stream_mode;  
};
/* 相机端: SEND <目标http_fd>
    例如:   SEND 7 */
struct cam_terminal {
    // int64_t remain;
    // char name[64];
    int dest;// <目标http的fd> get_connector(fd);
};

struct connect{
    int fd;
    int serve_type;

    /* 缓冲区 */
    char rbuf[CONNECT_BUF_LEN];
    int rlen;
    char wbuf[CONNECT_BUF_LEN];
    int wlen;

    /* 回调函数 */
    union recv_func
    {
        int (*accept_cb)(struct connect*);
        int (*recv_cb)(struct connect*);
    }recv_func;
    int (*send_cb)(struct connect*);
    void (*close)(struct connect*);

    /* APP */
    union {
        struct http_context http;
        struct cam_terminal cam;
    }app;//应用层内容
};
struct connect_node{
    struct connect pool[NUM_OF_CONNECTOR];
    struct connect_node* next;
};
struct connect_pool{
    int num_of_pool;
    struct connect_node* start;
    struct connect_node* last;
};

/* structure */
void connect_pool_init(void);
struct connect* get_connector(int);
// struct connect_node* get_pool (int);

#endif
