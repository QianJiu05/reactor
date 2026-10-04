#ifndef __RECV_RESOURCE_H__
#define __RECV_RESOURCE_H__

#include <stdint.h>

#include "connect_pool.h"
#include "protocol.h"

/* 相机客户端握手：解析 "CAM" 并做角色标记。返回 0 成功 */
int handle_resource(struct connect* conn);

/*
 * 查看端注册表
 *
 * 早期实现要求相机客户端把「查看端的 fd」写进 SEND 命令，链路很脆：
 * 查看端一断，相机就得换一个 fd 重发。改成注册表后，
 * 相机只需发一次 "CAM\r\n"，服务器自己维护查看端集合。
 */
void viewer_register(struct connect* conn);
void viewer_unregister(struct connect* conn);

/* 把一帧打包成 MJPEG 分段，广播给所有查看端 */
void viewer_broadcast(const struct frame_header* hdr, const uint8_t* jpeg);

/* 当前查看端数量 */
int  viewer_count(void);

#endif
