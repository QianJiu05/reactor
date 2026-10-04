#ifndef __RECORDER_H__
#define __RECORDER_H__

/* ============================================================
 *  服务器本地落盘存储（对应课题要求 4）
 *
 *  服务器在把帧转发给查看端的同时，把 JPEG 帧按连接分别落盘：
 *
 *      <RECORD_DIR>/cam<fd>_00001.jpg
 *      <RECORD_DIR>/cam<fd>_00002.jpg
 *      ...
 *
 *  每写满 RECORD_FILE_FRAMES 帧就换一个新文件，避免单个文件过大。
 *  落盘失败只打印告警，不影响转发链路——存储是旁路，不能拖垮直播。
 * ============================================================ */

#include <stdint.h>

#include "protocol.h"

/* 初始化存储目录（不存在则创建）。成功返回 0 */
int  recorder_init(const char *dir);

/* 写入一帧。cam_fd 用于区分不同的相机连接。失败返回 -1 */
int  recorder_write_frame(int cam_fd, const struct frame_header *hdr,
                          const uint8_t *jpeg, uint32_t jpeg_len);

/* 相机连接断开时释放其上下文 */
void recorder_close_camera(int cam_fd);

/* 累计落盘帧数，用于运行状态打印 */
unsigned long recorder_total_frames(void);

#endif /* __RECORDER_H__ */
