# reactor —— 服务器端

毕业设计「客户端采集 → 服务器 → 查看端」链路中的**服务器**，
基于 epoll 的多 reactor 并发模型，带账号体系。

## 一、对应课题要求

| 课题要求 | 实现位置 |
| --- | --- |
| 4. 接收客户端视频流并完成本地落盘存储 | `src/recorder.c` |
| 5. 把视频流转发给查看端，形成完整转播链路 | `src/http/recv_resource.c`、`src/http/http_handler.c` |
| 3. 自定义协议帧头的解析（粘包/拆包状态机） | `src/http/http_handler.c` 的 `handle_camera_frame()` |
| 研究重点 4：高并发服务器基于 epoll 的实现 | `src/reactor.c`、`src/epoll.c`、`example/main.c` |

账号体系（把采集端与查看端绑定）的完整设计见
**[docs/账号体系设计.md](docs/账号体系设计.md)**。

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
        └─────────────────┘         └─────────────────┘
```

- 连接池 `connect_pool` 按 fd 直接索引（`fd / NUM_OF_CONNECTOR` 定位节点，
  `fd % NUM_OF_CONNECTOR` 定位槽位），O(1) 取连接对象，无需哈希表；
- 主线程只做 accept，读写全部下放到 sub reactor；
- **`init_sub_reactor()` 返回时保证所有 `epfd` 已创建**，因此可以安全地
  跨线程调用 `set_epoll()`（采集端线程唤醒别的 reactor 上的查看端）。

## 三、编译与运行

```bash
make                # 生成 reactor 和 fake_cam
make clean

./reactor                       # 默认端口 8888，落盘 ./records
./reactor 9000 /data/records    # 指定端口和落盘目录
```

`make placeholder-image` 会提示占位图当前的状态（见 §六）。

## 四、账号体系

### 4.1 配置文件 `accounts.conf`

```
# 一行一个账号
alice
bob:
camera01:ignored_password_column
```

- 账号名约束：`[A-Za-z0-9_]{1,31}`，且不能是保留字 `favicon.ico`
- 冒号后的密码列**本版保留但不校验**，可省略冒号也可留空
- 空行与 `#` 注释行跳过；名字非法或重复的行跳过并打印行号

> ⚠ **本版不校验密码**：账号名即访问凭据。这不是访问控制，只是通道标识。
> 详见设计文档风险表 R1~R3。

### 4.2 一个账号 = 一路通道

```
        账号 (account)
             │
      ┌──────┴──────┐
      │             │
   采集端          查看端
 （0 或 1 个）   （0 ~ 32 个）
```

- 采集端启动时发 `CAM <账号>` 注册
- 查看端用浏览器访问 `http://<服务器IP>:8888/<账号>`
- 第二个用同账号注册的采集端会被拒绝（`ERR BUSY`），
  但**超过 10 秒没有帧**的僵死连接会被新连接顶替

### 4.3 完整使用流程

```bash
# 1. 服务器
./reactor 8888 ./records

# 2. 查看端（浏览器）
#    http://127.0.0.1:8888/alice

# 3. 采集端（开发板）
./cam /dev/video1 1024 768 30
```

时序：

1. 浏览器 `GET /alice` → 服务器查账号 → 回 `multipart/x-mixed-replace`
   响应头，把连接登记为 **alice 的查看端**；
2. 采集端连上 8888，发 `CAM alice\r\n` → 服务器校验并绑定 → 回 `OK\r\n`；
3. 采集端持续推送 `帧头 + JPEG`；
4. 服务器解析每一帧 → **落盘** + **广播给 alice 的查看端**；
5. 浏览器持续渲染，形成完整的转播链路。

## 五、HTTP 接口

| 请求 | 响应 | 说明 |
| --- | --- | --- |
| `GET /alice` | `200` + MJPEG | 账号存在，开始推流 |
| `GET /` | `503` | 未指定账号（提示格式，**不列出**账号目录） |
| `GET /nobody` | `404` | 账号不存在 |
| `GET /alice/x` | `404` | 只支持一层路径 |
| `GET /favicon.ico` | `404` | 保留字 |
| `POST /alice` | `405` | 只接受 GET |

> `GET /` 刻意不列出可用账号 —— 列出来等于任何人打开首页就知道所有通道名，
> 与"账号名即访问凭据"的定位冲突。演示时直接访问 `/<账号>` 即可。

## 六、查看端先连、采集端后连

这是常态（先开浏览器再给板子上电）。处理方式：

```
浏览器 GET /alice
      │
      ├─ 采集端在线 ──────────────→ 直接推真实帧
      │
      └─ 采集端未上线 ─→ 立刻回 200 + 流头
                        → 按 VIEWER_IDLE_FPS 循环推「等待设备」占位图
                        → 采集端一上线，下一帧真实数据自然把占位图顶掉
```

查看端侧**完全无感**，服务器也不需要为"等待中"单独维护一条 code path。

- 占位图：`waiting.jpg`（`config.h` 的 `WAITING_IMG`）。
  仓库里放的是 `test/pic2.jpg` 的副本，**仅用于验证链路**，
  建议自己画一张写着「等待设备上线…」的图覆盖它
- 定时器**按需启停**：只有正在等待的查看端才有活跃 `timerfd`，
  采集端上线后立刻解除，不产生空闲唤醒
- 置 `USING_WAITING_IMG 0` 可关闭该功能（此时查看端先连会保持空白）

## 七、本地落盘

```c
/* 每帧 JPEG 追加写入，每 RECORD_FILE_FRAMES 帧滚动一个新文件 */
recorder_write_frame(conn->fd, &hdr, jpeg, hdr.jpeg_len);
```

- 目录由 `RECORD_DIR` 指定，不存在会自动创建
- 文件命名 `cam<fd>_00001.jpg`，按采集端连接分开存
- 内容是连续 JPEG 字节流，按 `FFD8`(SOI) / `FFD9`(EOI) 标记即可离线切帧
- **落盘是旁路**：写失败只打印告警，不影响转发链路

## 八、没有开发板时怎么验证

`fake_cam` 是一个「假相机」：把一个 JPEG 按与采集端**完全相同**的协议
推给服务器，包括 `CAM <账号>` 握手与读取 `OK`/`ERR` 回应。

```bash
# 终端 1：启动服务器
./reactor 8888 ./records

# 终端 2：用 alice 账号推流
./fake_cam alice

# 终端 3：确认在落盘
watch -n1 'ls -l records/'
```

浏览器打开 `http://127.0.0.1:8888/alice` 应看到图片以 MJPEG 流刷新。

**验证账号隔离**：再开一个终端跑 `./fake_cam bob`，
然后浏览器同时打开 `/alice` 和 `/bob` —— 两路互不串流。

**验证握手被拒**：

```bash
./fake_cam nobody        # 期望: 服务器回应 ERR AUTH
./fake_cam alice         # 在 alice 已运行时: 期望 ERR BUSY
```

**验证占位图与无缝切换**：

```bash
# 1. 先只开浏览器访问 /alice（此时 fake_cam 还没跑）
#    → 应看到占位图以 1 fps 刷新
# 2. 再运行 ./fake_cam alice
#    → 画面自动切换成真实帧，浏览器不需要刷新
```

**验证僵死顶替**：

```bash
./fake_cam alice &        # 启动后
kill -STOP %1             # 暂停它（模拟设备断电，但 TCP 连接还在）
sleep 11                  # 等超过 CAM_STALE_MS
./fake_cam alice          # 期望: OK，并顶替掉旧连接
```

## 九、目录结构

```
reactor/
├── makefile
├── config.h               端口、缓冲区、落盘、账号、占位图配置
├── accounts.conf          账号名单
├── waiting.jpg            查看端等待期占位图
├── docs/
│   └── 账号体系设计.md     账号打通两端的设计文档
├── example/main.c         主 reactor 循环 + 启动流程
├── inc/
│   ├── protocol.h         自定义协议（与 webcam/inc/protocol.h 必须一致）
│   ├── account.h          账号表接口
│   ├── account_name.h     账号名合法性（唯一定义处）
│   ├── stream_source.h    真实帧 / 占位图
│   ├── recorder.h         本地落盘
│   ├── utils.h            now_ms()
│   ├── connect_pool.h     连接池、struct connect、enum conn_phase
│   ├── reactor.h / server_init.h / accept.h / recv.h / callback.h / epoll.h
│   └── http/
│       ├── http_handler.h   HTTP 入口与回调
│       ├── http_parse.h     请求行/握手行的纯解析函数（可单测）
│       ├── recv_resource.h  账号注册表与广播
│       ├── struct_http.h    HTTP 连接上下文
│       └── http_helper.h    响应报文常量
├── src/                   各模块实现
└── test/
    ├── fake_cam.c         假相机：无开发板时验证整条链路
    ├── c10k_client.py     C10K 并发连接测试脚本
    ├── echo.c / echo_th.c / fork_echo.c   几种 echo 服务端对比
    └── *.jpg              测试图片
```

## 十、关键设计说明

**为什么不用哈希表索引账号**

账号数上限是编译期常量 64，反查采集端 fd 采用线性扫描，
`O(64)` 即 `O(1)`。刻意不额外维护 fd→账号 索引表，
就少一处需要跨线程同步、可能变脏的状态。详见设计文档 §3.3。

**锁粒度**

账号表由一把全局互斥锁保护。但转发是每帧都要走的热路径，
所以严格遵循「**持锁取快照 → 放锁 → 逐个推送**」：
锁内只做一次 fd 拷贝，数据搬运、磁盘 I/O、epoll 通知全在锁外。
另外明确规定**锁内不做任何网络 I/O 与连接关闭动作**
（`send()` 可能阻塞；`close_callback()` 将来若加账号清理会重入同一把锁）。

**查看端网络慢怎么办**

转发时「塞得下就写、塞不下就丢」，不做跨连接反压：
直播场景下丢帧比让采集链路卡住更可接受。

**跨线程唤醒**

`set_epoll()` 允许跨线程调用（Linux 的 `epoll_ctl` 对 epoll 实例线程安全），
但只改事件掩码，绝不替目标线程做取值动作（`epoll_wait` 不是线程安全的）。
解除别的线程上的占位图定时器时只做 `timerfd_settime()`，
**不 close 也不 EPOLL_CTL_DEL** —— 关闭一个正被其他线程 epoll 监视的 fd
会带来难以排查的竞态；定时器 fd 保留到宿主连接自己断开时释放。

**帧解析状态机**

`handle_camera_frame()` 循环执行：
搜魔数 → 判帧头完整性 → 校验 `jpeg_len` → 判载荷完整性 → 校验校验和 → 取走一帧。
不足一帧的字节留在 `inbuf` 等下次 `recv`（拆包），
一次 `recv` 收到多帧则循环取完（粘包）。
伪魔数（JPEG 数据里恰好出现 `0xAA55`）通过 `jpeg_len` 范围检查和校验和排除。

**首行按完整行解析**

只有在缓冲区里找到 `\r\n` 之后才判定连接角色；
超过 `MAX_HANDSHAKE_LEN` 仍无 `\r\n` 判为畸形数据并断开（防慢速攻击）。
消费首行时只 `memmove` 出该行长度，**不清空整个缓冲区** ——
对端完全可能把握手行和第一帧放在同一个 `send()` 里发出。

**丢帧统计**

服务器用帧头的 `seq` 判断序号是否连续，跳变时打印丢失帧数；
采集端也会统计驱动丢帧和队列溢出丢帧，两端日志可对照定位瓶颈。

## 十一、测试脚本

```bash
# C10K 并发连接测试
python3 test/c10k_client.py
```
