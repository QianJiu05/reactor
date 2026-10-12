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


/* 相机客户端: 连接后发送 "CAM <account>\r\n" 注册，
 * 服务器只把帧转发给同一账号下的查看端。 */
struct cam_terminal {
    int      dest;          /* 已废弃的角色标记，仅为兼容保留；请用 conn->phase */
    uint32_t last_seq;      /* 上一帧序号，用于统计丢帧 */
    int      seq_inited;
};

struct connect{
    int fd;
    int state;
    int serve_type;

    /* 连接所处阶段，取值见 recv.h 的 enum conn_phase */
    int phase;

    /* 归属账号在账号表中的下标，-1 表示尚未归属任何账号 */
    int account_idx;

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
