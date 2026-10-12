#ifndef __ACCOUNT_H__
#define __ACCOUNT_H__

/* ============================================================
 *  账号表
 *
 *  一个账号 = 一路摄像头通道：
 *      · 最多绑定 1 个采集端（第二个用同账号注册会被拒绝）
 *      · 可被多个查看端同时订阅
 *
 *  ⚠ 本版【不校验密码】：account.password 字段保留但不读取，
 *    "知道账号名"即可访问该通道。这不是访问控制，只是通道标识，
 *    详见 docs/账号体系设计.md 的风险表 R1~R3。
 *
 *  线程安全：账号表由 g_account_lock 保护，可被任意 reactor 线程访问。
 *  但【帧转发热路径不要持锁】—— viewer_broadcast() 应当先持锁取快照、
 *  放锁后再逐个推送。
 * ============================================================ */

#include <stdbool.h>
#include <stdint.h>

#include "account_name.h"   /* account_name_valid() */

#ifdef __cplusplus
extern "C" {
#endif

/* account_cam_bind() 的返回值 */
#define ACCOUNT_BIND_OK    0
#define ACCOUNT_BIND_BUSY  1
#define ACCOUNT_BIND_FAIL  2

/*
 * 加载账号表。file 为 NULL 时使用 config.h 的 ACCOUNT_FILE。
 * 返回成功加载的账号数量；配置文件打不开返回 -1（服务器仍可启动，
 * 但所有采集端握手都会失败、所有查看端路径都会 404）。
 */
int account_init(const char *file);

/* 由账号名查下标，找不到返回 -1 */
int account_lookup(const char *name);

/* 取账号名，下标非法返回 NULL */
const char *account_name(int idx);

/* 当前已加载的账号数量 */
int account_count(void);

/*
 * 采集端绑定 / 解绑。
 *
 * 若该账号已有采集端且距上一帧未超过 CAM_STALE_MS，返回 ACCOUNT_BIND_BUSY；
 * 超过 CAM_STALE_MS 视为僵死连接，本次直接顶替（旧连接由调用方负责关闭）。
 * 顶替发生时通过 *stale_fd 返回应被关闭的旧 fd，否则置 -1。
 */
int account_cam_bind(int idx, int cam_fd, int *stale_fd);
void account_cam_unbind(int fd);

/* 由采集端 fd 反查账号下标，找不到返回 -1（线性扫描，账号数上限 64） */
int account_cam_find(int cam_fd);

/* 该账号的采集端是否在线 */
bool account_cam_online(int idx);

/* 采集端是否有帧活动（用于日志与状态展示） */
int64_t account_cam_last_frame_ms(int idx);

/* 记录收到一帧：更新 last_frame_ms 与 frames_in */
void account_touch_frame(int idx);

/*
 * 查看端订阅管理。
 * 返回 0 成功；-1 表示账号下标非法或订阅数已达上限。
 */
int account_viewer_add(int idx, int fd);
void account_viewer_remove(int fd);

/*
 * 取该账号查看端 fd 的快照（调用方提供的数组需能容纳 MAX_VIEWERS_PER 个）。
 * 返回实际拷贝的数量。持锁时间极短，帧转发路径用它避免长时间持锁。
 */
int account_viewer_snapshot(int idx, int *out, int out_cap);

int account_viewer_count(int idx);

/* 累计转发帧数 +1 */
void account_add_frames_out(int idx, unsigned long n);

/* 释放账号表（进程退出时调用，非必需） */
void account_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* __ACCOUNT_H__ */
