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

/*
 * 连接所处阶段。
 *
 * 原来只用 serve_type 一个字段同时表达「角色」和「阶段」，加账号体系后不够用：
 * 必须先明确区分「还没收到完整首行」和「已经解析出角色」，
 * 否则首行被 TCP 分片时就会误判（设计文档 D6）。
 *
 * 定义放在这里而不是 recv.h：所有拿到 struct connect 的模块
 * （http_handler / callback / recv_resource）都要用它判断角色。
 */
enum conn_phase {
    PHASE_PRE_AUTH = 0,     /* 已 accept，等待完整首行（\r\n） */
    PHASE_CAM,              /* 采集端已注册，收到的是自定义协议帧 */
    PHASE_VIEWER,           /* 查看端已登记，可能已开始推流 */
    PHASE_CLOSED
};


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

    /*
     * 查看端等待采集端上线时用的占位图定时器，-1 表示未创建。
     * 只有"正在等待"的查看端才有活跃定时器，采集端上线后即解除。
     */
    int idle_tfd;

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
