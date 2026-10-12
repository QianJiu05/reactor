#ifndef __PROTOCOL_H__
#define __PROTOCOL_H__

/* ============================================================
 *  自定义应用层传输协议 —— 服务器端副本
 *
 *  ⚠ 本文件必须与采集端 codes/webcam/inc/protocol.h 保持一致，
 *    任何字段改动都要两边同步，否则帧头解析会错位。
 *
 *  协议格式：
 *      +------------------+---------------------------+
 *      |  frame_header    |  payload[jpeg_len]        |
 *      |  (固定 38 字节)  |  (JPEG 压缩帧数据)        |
 *      +------------------+---------------------------+
 *
 *  接收端状态机（解决粘包 / 拆包）：
 *      1. 在接收缓冲区里搜索魔数 PROTO_MAGIC，丢弃其之前的所有字节；
 *      2. 不足 sizeof(frame_header) 字节 → 继续收（拆包）；
 *      3. 读 jpeg_len，校验合法性；
 *      4. 不足 header + jpeg_len 字节 → 继续收（拆包）；
 *      5. 校验校验和，通过则取走一帧，剩余字节前移，回到第 1 步（粘包）。
 * ============================================================ */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define PROTO_MAGIC    0xAA55   /* 帧头魔数，用于在字节流中定位帧起点 */
#define PROTO_VERSION  0x01     /* 协议版本，收发双方必须一致 */

/* 服务端控制命令：采集端连接后发送的第一行 */
#define PROTO_CMD_CAM   "CAM"           /* "CAM <account>\r\n" 注册为采集端 */

/* ===== 服务端对采集端握手的回应 =====
 * 采集端必须按行读取这些回应，读到 OK 才进入推流循环。
 * 定义放在协议头里，保证服务器与采集端用的是同一份字面量。 */
#define PROTO_REPLY_OK        "OK\r\n"          /* 接受，开始收帧 */
#define PROTO_REPLY_ERR_AUTH  "ERR AUTH\r\n"    /* 账号不存在 / 名字非法 */
#define PROTO_REPLY_ERR_BUSY  "ERR BUSY\r\n"    /* 该账号已有采集端在线 */

#define PROTO_REPLY_LEN_MAX   32                /* 回应最大长度（含 CRLF） */

/* 最大载荷长度：按此值判断 jpeg_len 是否被伪魔数污染 */
#define PROTO_MAX_PAYLOAD  (1024 * 1024)

/* ===== flags 位定义（随帧上报的状态标记） ===== */
#define FLAG_PIR      (1 << 0)   /* 客户端接了 PIR，且本帧采集时有人体感应 */
#define FLAG_OPENCV   (1 << 1)   /* 客户端启用了 OpenCV 检测 */
#define FLAG_FACE     (1 << 2)   /* 本帧检测到人体/人脸目标 */

/* OpenCV 检测结果，随帧头上报 */
struct opencv_info {
    uint16_t x;        /* 包围框左上角 x（原图像素坐标） */
    uint16_t y;        /* 包围框左上角 y */
    uint16_t w;        /* 包围框宽 */
    uint16_t h;        /* 包围框高 */
    uint8_t  count;    /* 本帧检测到的目标个数 */
    uint8_t  reserved; /* 对齐填充 */
};

/* -------- 帧头（固定 38 字节，__attribute__((packed))） --------
 * magic(2) version(1) flags(1)   = 4
 * seq(4) jpeg_len(4)             = 8
 * ts_ms(8)                       = 8
 * pir(1) reserved(3)             = 4
 * opencv(10)                     = 10
 * checksum(4)                    = 4
 */
struct __attribute__((packed)) frame_header {
    uint16_t magic;        /* PROTO_MAGIC */
    uint8_t  version;      /* PROTO_VERSION */
    uint8_t  flags;        /* FLAG_PIR | FLAG_OPENCV | FLAG_FACE */

    uint32_t seq;          /* 帧序号，从 0 递增，接收端据此统计丢帧 */
    uint32_t jpeg_len;     /* 紧随帧头之后的 JPEG 数据长度 */

    uint64_t ts_ms;        /* 采集时刻的单调递增毫秒时间戳 */

    uint8_t  pir;          /* PIR 状态：0=无人, 1=检测到人体 */
    uint8_t  reserved[3];  /* 预留，填 0 */

    struct opencv_info opencv;  /* OpenCV 检测结果 */

    uint32_t checksum;     /* 校验和，计算时本字段先置 0 */
};

/* 编译期断言：帧头必须固定 38 字节，与采集端布局一致 */
typedef char proto_header_size_assert[
    (sizeof(struct frame_header) == 38) ? 1 : -1];

/* -------- 协议辅助函数 -------- */

static inline uint32_t proto_checksum(const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t sum = 0;
    for (uint32_t i = 0; i < len; i++)
        sum += p[i];
    return sum;
}

static inline void proto_fill_header(struct frame_header *hdr,
                                     uint32_t seq, uint32_t jpeg_len)
{
    hdr->magic    = PROTO_MAGIC;
    hdr->version  = PROTO_VERSION;
    hdr->seq      = seq;
    hdr->jpeg_len = jpeg_len;
    hdr->checksum = 0;
    hdr->checksum = proto_checksum(hdr, sizeof(*hdr));
}

/* 打包一帧到 out（帧头 + 载荷），返回总长度；空间不足返回 -1。
 * 服务器侧本身不发送视频，但 test/fake_cam.c 要用它构造测试帧。 */
static inline int proto_pack(void *out, size_t out_cap,
                             const struct frame_header *hdr,
                             const void *payload)
{
    size_t total = sizeof(*hdr) + hdr->jpeg_len;

    if (total > out_cap)
        return -1;

    memcpy(out, hdr, sizeof(*hdr));
    if (hdr->jpeg_len > 0)
        memcpy((uint8_t *)out + sizeof(*hdr), payload, hdr->jpeg_len);
    return (int)total;
}

/* 校验帧：魔数、版本、长度、校验和 */
static inline bool proto_check(const void *buf, size_t buf_len,
                               const struct frame_header *hdr)
{
    struct frame_header tmp;
    uint32_t saved, sum;

    if (buf_len < sizeof(*hdr))
        return false;
    if (hdr->magic != PROTO_MAGIC || hdr->version != PROTO_VERSION)
        return false;
    if (hdr->jpeg_len == 0 || hdr->jpeg_len > PROTO_MAX_PAYLOAD)
        return false;
    if (buf_len < sizeof(*hdr) + hdr->jpeg_len)
        return false;

    memcpy(&tmp, hdr, sizeof(tmp));
    saved = tmp.checksum;
    tmp.checksum = 0;

    sum  = proto_checksum(&tmp, sizeof(tmp));
    sum += proto_checksum((const uint8_t *)buf + sizeof(*hdr), hdr->jpeg_len);

    return sum == saved;
}

#endif /* __PROTOCOL_H__ */
