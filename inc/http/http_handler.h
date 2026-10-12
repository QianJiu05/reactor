#ifndef __HTTP_HANDLER_H__
#define __HTTP_HANDLER_H__

#include "connect_pool.h"
#include "struct_http.h"
#include "protocol.h"

/*
 * 查看端请求的处理入口。
 *
 * 按 URL 路径第一段取账号（见 docs/账号体系设计.md §5.5）：
 *      GET /            → 503（未指定账号）
 *      GET /alice       → 200 + MJPEG 流（账号存在）
 *      GET /alice       → 404（账号不存在）
 *      GET /alice/x     → 404（只支持一层路径）
 *      GET /favicon.ico → 404
 *      POST /alice      → 405
 *
 * 返回 0  已通过（连接保持，进入流模式）
 * 返回 -1 已回错误响应（调用方负责关闭连接）
 */
int generate_http_response(struct connect* conn);

/*
 * 已建立连接的数据回调。
 * 采集端：解析自定义协议帧 → 落盘 + 转发
 * 查看端：把转发进来的帧作为 MJPEG 分段发出去
 */
int http_callback(struct connect* conn);

#endif
