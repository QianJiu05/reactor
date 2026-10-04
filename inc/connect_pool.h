#ifndef __CONNECT_POOL_H__
#define __CONNECT_POOL_H__

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#include "config.h"
#include "struct_http.h"
#include "protocol.h"


#define STATE_CLOSED 0
#define STATE_USING 1


/* 相机客户端: 连接后发送 "CAM\r\n" 注册，服务器自动把帧转发给所有查看端。
 * dest >= 0 表示这是相机连接（复用该字段做角色标记），-1 表示查看端。 */
struct cam_terminal {
    int      dest;          /* 角色标记，见上 */
    uint32_t last_seq;      /* 上一帧序号，用于统计丢帧 */
    int      seq_inited;
};

struct connect{
    int fd;
    int state;
    int serve_type;
    struct reactor* sub;
    struct connect_node* node;

    /* 缓冲区 */
    char inbuf[CONNECT_BUF_LEN];
    int inlen;
    char outbuf[CONNECT_BUF_LEN];
    int outlen;

    /* 回调函数 */
    union recv_func
    {
        int (*accept_cb)(int);
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
    int using;
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
void connect_init(struct connect* conn, int fd);

#endif
