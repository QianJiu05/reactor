#ifndef __STREAM_SOURCE_H__
#define __STREAM_SOURCE_H__

/* ============================================================
 *  查看端的帧来源：真实采集帧 或 等待占位图
 *
 *  查看端先连、采集端后连是常态（先开浏览器再给板子上电）。
 *  本模块把「往查看端 outbuf 里塞一段 MJPEG」收敛到一处，
 *  真实帧与占位图走同一条路径，查看端侧只有一种行为：
 *  持续收到 --frame 分段。
 * ============================================================ */

#include "connect_pool.h"

/* 启动时载入占位图。成功返回 0；图片缺失返回 -1（功能降级，不影响启动） */
int stream_source_init(const char* img_path);

/* 是否成功载入了占位图 */
int stream_source_ready(void);

/*
 * 给某个查看端塞一张占位图。
 * 返回 0 成功（已排入 outbuf 并触发 EPOLLOUT）；-1 表示无占位图或目标无效。
 */
int stream_source_push_placeholder(struct connect* conn);

/*
 * 维护某个查看端的占位图定时器。
 *
 * need_placeholder 为真时启动定时器（按 VIEWER_IDLE_FPS 周期触发，
 * 后续由 epoll 事件驱动继续推占位图）；
 * 为假时解除定时 —— 采集端已上线，不该再浪费 CPU 推占位图。
 *
 * 定时器按需启停，因此只有"正在等待"的那些查看端会产生额外事件。
 */
void stream_source_arm_idle_timer(struct connect* conn, int need_placeholder);

/* 关闭并释放某个查看端的定时器（连接断开时调用） */
void stream_source_drop_idle_timer(struct connect* conn);

/*
 * 处理一个就绪的占位图定时器 fd。
 *
 * 由 reactor 的主事件循环调用 —— 它拿到的是 events[i].data.fd，
 * 分不清那是连接的 socket 还是某个查看端的定时器，所以需要本函数
 * 通过内部映射判断并完成"消费定时器 + 推一张占位图"。
 *
 * 返回 1 表示该 fd 确实是本模块管理的定时器（已处理）；
 * 返回 0 表示不是，调用方应按普通连接继续处理。
 */
int stream_source_handle_timer(int tfd);

/* 采集端上线时调用：停止该账号所有查看端的占位图 */
void stream_source_stop_idle_for_account(int account_idx);

#endif /* __STREAM_SOURCE_H__ */
