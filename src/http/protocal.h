
// 外设:dht11，sg90(应该用不上)，sr01
// 通信协议：[frame_header][jpeg_payload]
// 使用 TCP，应用层只需一次校验即可

#ifndef __PROTOCAL_H__
#define __PROTOCAL_H__

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define PROTO_MAGIC  0xAA55   // 帧头魔数，便于服务器定位帧起始
#define PROTO_VERSION 0x01

// 模块启用标志位（用 bit field 节省空间）
#define FLAG_DHT11   (1 << 0)
#define FLAG_OPENCV  (1 << 1)
#define FLAG_SR01    (1 << 2)

// opencv 识别结果
struct opencv_info {
    uint16_t face_x;       // 人脸框左上角 x
    uint16_t face_y;       // 人脸框左上角 y
    uint16_t face_w;       // 人脸框宽
    uint16_t face_h;       // 人脸框高
    uint8_t  face_count;   // 检测到的人脸数
    uint8_t  reserved;
};

// 帧头结构体，紧凑排列，避免平台差异
// 协议格式：| frame_header | jpeg_data[jpeg_len] |
struct __attribute__((packed)) frame_header {
    uint16_t magic;        // 魔数 PROTO_MAGIC
    uint8_t  version;      // 协议版本
    uint8_t  flags;        // 模块启用标志 FLAG_DHT11 | FLAG_OPENCV | ...

    uint32_t jpeg_len;     // JPEG 数据长度（紧跟在帧头后面）

    // dht11
    float    temperature;
    float    humidity;

    // opencv
    struct opencv_info opencv;

    // sr01
    uint8_t  detect_human; // 0=未检测到, 1=检测到

    uint8_t  reserved[3];  // 预留对齐

    uint32_t checksum;     // 对 header + jpeg_data 的 CRC32 或累加和
};

// 简单累加校验（够用，TCP 已保证传输完整性，这里主要防打包 bug）
static inline uint32_t calc_checksum(const uint8_t *data, uint32_t len) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i < len; i++) {
        sum += data[i];
    }
    return sum;
}
static inline void pack_frame (const void* buffer, struct frame_header* frame, int frame_len) {
    memcpy(buffer, frame, frame_len);
}
/* 解包frame到buffer，返回校验和是否正确
 * frame:  指向接收缓冲区起始（header + jpeg_data 连续存放）
 * buffer: 输出，解析后的帧头拷贝到这里
 */
static inline bool depack_frame(const void *frame, struct frame_header *buffer) {
    const struct frame_header *hdr = (const struct frame_header *)frame;

    // 1. 校验魔数
    if (hdr->magic != PROTO_MAGIC)
        return false;

    // 2. 校验版本
    if (hdr->version != PROTO_VERSION)
        return false;

    // 3. 校验 jpeg_len 合法性（防止伪造超大长度）
    if (hdr->jpeg_len == 0)
        return false;

    // 4. 计算校验和：header(checksum字段置0) + jpeg_data
    uint32_t saved_sum = hdr->checksum;

    // 临时拷贝 header，把 checksum 置 0 后参与计算
    struct frame_header tmp;
    memcpy(&tmp, hdr, sizeof(struct frame_header));
    tmp.checksum = 0;

    uint32_t sum = calc_checksum((const uint8_t *)&tmp, sizeof(struct frame_header));
    // jpeg_data 紧跟在 header 后面
    const uint8_t *jpeg_data = (const uint8_t *)frame + sizeof(struct frame_header);
    sum += calc_checksum(jpeg_data, hdr->jpeg_len);

    if (sum != saved_sum)
        return false;

    // 5. 校验通过，拷贝帧头到输出 buffer
    memcpy(buffer, hdr, sizeof(struct frame_header));
    return true;
}

#endif // __PROTOCAL_H__