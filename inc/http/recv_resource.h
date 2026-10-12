#ifndef __RECV_RESOURCE_H__
#define __RECV_RESOURCE_H__

#include <stdint.h>

#include "connect_pool.h"
#include "protocol.h"
#include "account.h"

/*
 * 采集端握手：解析 "CAM <account>\r\n"，校验账号并把本连接绑定到该账号。
 *
 * 服务器【必须回一行】结果，采集端靠它判断自己有没有被接受：
 *   ACCOUNT_BIND_OK   → 回 "OK\r\n"，进入帧接收状态
 *   ACCOUNT_BIND_BUSY → 回 "ERR BUSY\r\n" 后关闭连接
 *   其它              → 回 "ERR AUTH\r\n" 后关闭连接
 *
 * 返回 0 表示已接受（已回 OK），-1 表示已拒绝（已回错误行并关闭连接）。
 */
int handle_resource(struct connect* conn);

/*
 * 查看端注册表
 *
 * 早期实现要求采集端把「查看端的 fd」写进握手命令，链路很脆：
 * 查看端一断，采集端就得换一个 fd 重发。改成注册表后，
 * 采集端只需发一次账号握手，服务器自己维护每个账号的查看端集合。
 */
void viewer_register(struct connect* conn);
void viewer_unregister(struct connect* conn);

/* 把一帧打包成 MJPEG 分段，广播给【指定账号】下的所有查看端 */
void viewer_broadcast(int account_idx, const struct frame_header* hdr,
                      const uint8_t* jpeg);

/* 当前所有账号的查看端总数（仅用于状态展示） */
int  viewer_count(void);

#endif
