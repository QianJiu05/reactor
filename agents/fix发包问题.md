# Bug 修复总结 — MJPEG 流转发问题

**日期：** 2026-03-22  
**模块：** `test/cam_pic_sender.c` / `src/http/http_handler.c` / `src/http/recv_resource.c`

---

## 问题现象

- 服务端日志持续输出 `magic not found, inlen=1`，始终找不到帧头
- 修复打包逻辑后，日志输出 `INVALID jpeg_len=192935190`，服务端能收到数据但无法解码帧，也无法转发给 HTTP 浏览器

---

## 根因分析与修复

### Bug 1 — `cam_pic_sender.c`：发包逻辑完全错误

**文件：** `test/cam_pic_sender.c`

| 项 | 内容 |
|----|------|
| **原代码** | `len1 = snprintf(buffer, BUFFER_SIZE, picture);` |
| **问题** | 把 JPEG 二进制数据当**格式字符串**传给 `snprintf`，完全覆盖了刚用 `pack_frame` 写入的帧头；JPEG 数据中第一个 `\0` 字节截断输出，导致实际发送只有 11 字节 |
| **checksum** | `calc_checksum(picture, len1 + sizeof(frame_header))` 计算范围错误，且 `checksum` 字段未先置 0，与 `depack_frame` 的验证逻辑不匹配 |
| **修复** | 删除 `snprintf`；先 `memset(&header1, 0)` 置零，再对 header（checksum=0）和 jpeg data 分别累加计算 checksum；用 `memcpy` 将 jpeg 数据追加在帧头之后；发送长度改为 `sizeof(frame_header) + len1` |

```c
// 修复后
memset(&header1, 0, sizeof(header1));
header1.magic   = PROTO_MAGIC;
header1.version = PROTO_VERSION;
header1.jpeg_len = (uint32_t)len1;

uint32_t sum = calc_checksum((const uint8_t*)&header1, sizeof(struct frame_header));
sum += calc_checksum((const uint8_t*)picture, (uint32_t)len1);
header1.checksum = sum;

pack_frame(buffer, &header1, sizeof(struct frame_header));
memcpy(buffer + sizeof(struct frame_header), picture, len1);
int total_len = (int)sizeof(struct frame_header) + len1;

send(fd, buffer, total_len, 0);
```

---

### Bug 2 — `http_handler.c`：`MJPEG_SENDBUF_SIZE` 小于实际 JPEG 文件大小

**文件：** `src/http/http_handler.c`

| 项 | 内容 |
|----|------|
| **原代码** | `#define MJPEG_SENDBUF_SIZE (256 * 1024)  // 256KB` |
| **问题** | 测试图片 `featured.jpg` 大小为 **341,314 字节（≈333KB）**，超过 256KB 上限。服务端对真正的帧头也判定为 INVALID，然后在 JPEG 二进制数据内部乱找恰好出现的 `0xAA55` 伪魔数，导致 `jpeg_len` 读到随机天文数字，永远无法解码 |
| **修复** | 改为 `512KB`，与 `CONNECT_BUF_LEN` 保持一致，保证任何合法帧都能容纳 |

```c
// 修复后
#define MJPEG_SENDBUF_SIZE (512 * 1024)  // 512KB，与 CONNECT_BUF_LEN 保持一致
```

---

### Bug 3 — `recv_resource.c`：`memmove` 长度参数错误

**文件：** `src/http/recv_resource.c`

| 项 | 内容 |
|----|------|
| **原代码** | `memmove(conn->inbuf, conn->inbuf + to_copy, to_copy);` |
| **问题** | 第三个参数（移动长度）应为**剩余未复制的字节数** `conn->inlen - to_copy`，写成 `to_copy` 导致只移动了已复制部分的长度，剩余数据发生错位或丢失 |
| **修复** | 改为 `conn->inlen - to_copy` |

```c
// 修复后
memmove(conn->inbuf, conn->inbuf + to_copy, conn->inlen - to_copy);
```

---

## 数据流说明

```
cam_pic_sender
  └─ send: [frame_header][jpeg_data]   (TCP)
       │
       ▼
reactor server (recv_resource.c)
  get_resource_callback:
    conn->inbuf ──memcpy──▶ dest->inbuf (HTTP client 的 inbuf)
    set_epoll(dest, EPOLLOUT)
       │
       ▼
  http_callback (http_handler.c):
    1. 在 inbuf 搜索 PROTO_MAGIC (0xAA55)
    2. 校验 frame_header（魔数、版本、jpeg_len、checksum）
    3. pack_jpg: 在 jpeg_data 前后加 MJPEG multipart 边界头
    4. send 到浏览器
```

---

## 经验教训

1. **`snprintf` 不能用于二进制数据**：JPEG 是二进制，`\0` 会截断，`%` 会被误解析为格式符，必须用 `memcpy`。
2. **校验和计算顺序**：计算 checksum 前必须先将 checksum 字段置 0，否则校验永远不通过。
3. **缓冲区上限要与实际数据大小匹配**：硬编码的 buffer size 要根据业务数据的实际最大值设置，或直接引用统一的宏（如 `CONNECT_BUF_LEN`）。
4. **`memmove` 的第三个参数是"要移动的字节数"**，不是"已处理的字节数"，两者容易混淆。
