
#ifndef __RECV_H__
#define __RECV_H__

#include "connect_pool.h"


#define SERVE_NOT_INIT     -1
#define SERVE_ECHO          0
#define SERVE_HTTP          1
#define SERVE_GET_RESOURCE  2

/*
 * 连接所处阶段。
 *
 * 原来只用 serve_type 一个字段同时表达「角色」和「阶段」，加账号体系后不够用：
 * 必须先明确区分「还没收到完整首行」和「已经解析出角色」，
 * 否则首行被 TCP 分片时就会误判（设计文档 D6）。
 */
enum conn_phase {
    PHASE_PRE_AUTH = 0,     /* 已 accept，等待完整首行（\r\n） */
    PHASE_CAM,              /* 采集端已注册，收到的是自定义协议帧 */
    PHASE_VIEWER,           /* 查看端已登记，可能已开始推流 */
    PHASE_CLOSED
};

/* 首行最大长度：超过仍未出现 \r\n 判定为非法，防止慢速攻击占用缓冲 */
#define MAX_HANDSHAKE_LEN  128

int recv_callback(struct connect* conn);

#endif
