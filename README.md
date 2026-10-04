# reactor —— 服务器端

毕业设计「客户端采集 → 服务器 → 查看端」链路中的**服务器**，
基于 epoll 的多 reactor 并发模型。

## 一、对应课题要求

| 课题要求 | 实现位置 |
| --- | --- |
| 4. 接收客户端视频流并完成本地落盘存储 | `src/recorder.c` |
| 5. 把视频流转发给查看端，形成完整转播链路 | `src/http/recv_resource.c`、`src/http/http_handler.c` |
| 3. 自定义协议帧头的解析（粘包/拆包状态机） | `src/http/http_handler.c: handle_camera_frame()` |
| 研究重点 4：高并发服务器基于 epoll 的实现 | `src/reactor.c`、`src/epoll.c`、`example/main.c` |

## 二、并发模型

```
                    ┌─────────────────────────────┐
                    │  main reactor（主线程）      │
                    │  只监听 listen fd 的 EPOLLIN │
                    │  accept 后轮询分发给 sub      │
                    └──────────┬──────────────────┘
                               │ patch_connect()
                 ┌─────────────┴─────────────┐
                 ▼                           ▼
        ┌─────────────────┐         ┌─────────────────┐
        │ sub reactor 0   │         │ sub reactor 1   │
        │ 自己的 epoll 实例│         │ 自己的 epoll 实例│
        │ 处理已连接 socket│         │ 处理已连接 socket│
        └─────────────────┘         └─────────────────┘
```

- 连接池 `connect_pool` 按 fd 直接索引（`fd / NUM_OF_CONNECTOR` 定位节点，
  `fd % NUM_OF_CONNECTOR` 定位槽位），O(1) 取连接对象，无需哈希表；
- 主线程只做 accept，读写全部下放到 sub reactor，避免 accept 与 I/O 相互阻塞。

## 三、编译与运行

```bash
make                # 生成 reactor 和 fake_cam
make clean

./reactor                       # 默认端口 8888，落盘目录 ./records
./reactor 9000 /data/records    # 指定端口和落盘目录
```

### 没有开发板时怎么验证

`test/fake_cam.c` 编译出的 `fake_cam` 是一个「假相机」：
它把一张 JPEG 按与采集端**完全相同**的协议反复推给服务器，
用来在没有板子、没有摄像头的情况下验证整条链路。

```bash
# 终端 1：启动服务器
./reactor 8888 ./records

# 终端 2：推流（默认用 test/featured.jpg，10 fps）
./fake_cam

# 终端 3：确认在落盘
watch -n1 'ls -l records/'
```

浏览器打开 `http://127.0.0.1:8888/`，能看到图片以 MJPEG 流持续刷新，
就说明「接收 → 落盘 → 转发」三步都通了。

## 四、完整使用流程

```bash
# 1. 服务器
./reactor 8888 ./records

# 2. 查看端（浏览器直接打开，MJPEG 流）
#    http://<服务器IP>:8888/

# 3. 采集端（开发板上）
./cam /dev/video1 1024 768 30
```

时序：

1. 浏览器 `GET /` → 服务器回 `multipart/x-mixed-replace` 响应头，
   把该连接登记进查看端注册表；
2. 采集端连上 8888，发送 `CAM\r\n` 声明自己是相机客户端；
3. 采集端持续推送 `帧头 + JPEG`；
4. 服务器解析每一帧 → **落盘** + **广播给所有查看端**（包成 MJPEG 分段）；
5. 浏览器持续渲染，实现「采集 → 服务器 → 查看端」的完整转播链路。

## 五、目录结构

```
reactor/
├── makefile
├── config.h               端口、缓冲区、落盘目录等配置
├── example/main.c         主 reactor 循环 + 启动流程
├── inc/
│   ├── protocol.h         自定义协议（与 webcam/inc/protocol.h 必须一致）
│   ├── recorder.h         本地落盘接口
│   ├── connect_pool.h     连接池与 struct connect
│   ├── reactor.h          sub reactor
│   ├── server_init.h / accept.h / recv.h / callback.h / epoll.h
│   └── http/
│       ├── http_handler.h  HTTP 响应与帧解析
│       ├── recv_resource.h 查看端注册表与广播
│       ├── struct_http.h   HTTP 连接上下文
│       └── http_helper.h   响应报文常量
├── src/                   各模块实现（对应上表）
└── test/
    ├── fake_cam.c         假相机：无开发板时验证「落盘 + 转发」链路
    ├── c10k_client.py     C10K 并发连接测试脚本
    ├── echo.c / echo_th.c / fork_echo.c   几种 echo 服务端对比（阻塞/线程/进程）
    └── *.jpg              测试图片
```

## 六、本地落盘（课题要求 4）

```c
/* 每帧 JPEG 追加写入，每 RECORD_FILE_FRAMES 帧滚动一个新文件 */
recorder_write_frame(conn->fd, &hdr, jpeg, hdr.jpeg_len);
```

- 落盘目录由 `config.h` 的 `RECORD_DIR` 指定，不存在会自动创建；
- 文件命名 `cam<fd>_00001.jpg`，按相机连接分开存，
  `RECORD_FILE_FRAMES`（默认 500）控制单个文件大小；
- 文件内容是连续的 JPEG 字节流，按 `FFD8`(SOI) / `FFD9`(EOI) 标记即可离线切帧；
- **落盘是旁路**：写失败只打印告警，不影响转发链路——存储故障不能拖垮直播。

## 七、关键设计说明

**为什么用查看端注册表，而不是让相机指定目标 fd**

早期协议是 `SEND <查看端fd>`，需要人先把查看端的 socket fd 报给相机，
查看端一断线整个链路就失效。现在相机只发一次 `CAM\r\n`，
服务器用注册表维护查看端集合，两端彻底解耦，可以随时接入/退出多个查看端。

**查看端网络慢怎么办**

转发时「塞得下就写、塞不下就丢」，不做跨连接反压：
直播场景下丢帧比让相机链路卡住更可接受。
查看端的 `outbuf` 大小为 `CONNECT_BUF_LEN`（默认 512 KB）。

**帧解析状态机**

`handle_camera_frame()` 循环执行：
搜魔数 → 判帧头完整性 → 校验 `jpeg_len` → 判载荷完整性 → 校验校验和 → 取走一帧。
不足一帧的字节留在 `inbuf` 等下次 `recv`（拆包），
一次 `recv` 收到多帧则循环取完（粘包）。
伪魔数（JPEG 数据里恰好出现 `0xAA55`）通过 `jpeg_len` 范围检查和校验和排除。

**丢帧统计**

服务器用帧头的 `seq` 判断序号是否连续，跳变时打印丢失帧数；
采集端也会统计驱动丢帧和队列溢出丢帧，两端日志可对照定位瓶颈。

## 八、测试脚本

```bash
# C10K 并发连接测试
python3 test/c10k_client.py
```
