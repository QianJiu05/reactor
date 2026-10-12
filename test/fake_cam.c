/*
 *  服务器测试用假相机
 *
 *  作用：在没有开发板、没有摄像头的情况下验证服务器链路。
 *  它按与采集端完全相同的自定义协议，把一张 JPEG 图片反复发送给服务器，
 *  服务器应当完成「落盘 + 转发给该账号的查看端」两件事。
 *
 *  用法:
 *      ./fake_cam [账号] [图片路径] [服务器IP] [端口]
 *  默认:
 *      ./fake_cam                             账号 alice，用 test/featured.jpg
 *      ./fake_cam bob                         账号 bob
 *      ./fake_cam bob pic.jpg 192.168.1.10 8888
 *
 *  验证步骤:
 *      1. 另开终端启动服务器:  ./reactor 8888 ./records
 *      2. 浏览器打开:         http://127.0.0.1:8888/alice
 *      3. 运行本程序，浏览器应能看到图片以 MJPEG 流持续刷新，
 *         同时 ./records 目录下会不断生成 cam<fd>_xxxxx.jpg
 *      4. 换一个账号再跑一份 fake_cam，确认两路互不串流
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include "protocol.h"

#define DEFAULT_ACCOUNT "alice"
#define DEFAULT_IMG     "../test/featured.jpg"
#define DEFAULT_IP      "127.0.0.1"
#define DEFAULT_PORT    8888
#define SEND_INTERVAL_MS 100        /* 10 fps，方便肉眼观察 */
#define HANDSHAKE_TIMEOUT_MS 5000   /* 等服务器回应 OK/ERR 的超时 */

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

/* 一次 send 可能只写出去一部分，必须循环补齐（与采集端 send_all 一致） */
static int send_all(int fd, const void *buf, int len)
{
    const char *p = (const char *)buf;
    int sent = 0;

    while (sent < len) {
        ssize_t n = send(fd, p + sent, len - sent, 0);
        if (n > 0) {
            sent += (int)n;
        } else if (n < 0 && (errno == EINTR || errno == EAGAIN)) {
            continue;
        } else {
            return -1;
        }
    }
    return 0;
}

/*
 * 等待服务器对握手的回应，最多等 HANDSHAKE_TIMEOUT_MS。
 * 成功读到一行返回 0，并把该行写入 out；超时/断连返回 -1。
 *
 * 采用逐字节读、遇到 '\n' 结束的方式：回应只有几个字节，
 * 不需要缓冲管理，也不会把紧随其后的数据误当成回应。
 */
static int read_reply_line(int sock, char *out, int cap)
{
    int  n = 0;
    uint64_t deadline = now_ms() + HANDSHAKE_TIMEOUT_MS;

    while (n < cap - 1) {
        char c;
        ssize_t r = recv(sock, &c, 1, 0);

        if (r == 1) {
            if (c == '\n')
                break;
            if (c != '\r')
                out[n++] = c;
            continue;
        }

        if (r == 0)
            return -1;                          /* 服务器关闭了连接 */
        if (errno == EINTR)
            continue;
        if ((errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (now_ms() > deadline)
                return -1;
            usleep(10 * 1000);
            continue;
        }
        return -1;
    }

    out[n] = '\0';
    return (n > 0) ? 0 : -1;
}

int main(int argc, char *argv[])
{
    const char *account  = (argc >= 2) ? argv[1] : DEFAULT_ACCOUNT;
    const char *img_path = (argc >= 3) ? argv[2] : DEFAULT_IMG;
    const char *ip       = (argc >= 4) ? argv[3] : DEFAULT_IP;
    int         port     = (argc >= 5) ? atoi(argv[4]) : DEFAULT_PORT;

    /* ---- 读取图片 ---- */
    int fd = open(img_path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "打开图片 %s 失败: %s\n", img_path, strerror(errno));
        return 1;
    }

    static unsigned char jpeg[PROTO_MAX_PAYLOAD];
    ssize_t jpeg_len = read(fd, jpeg, sizeof(jpeg));
    close(fd);

    if (jpeg_len <= 0) {
        fprintf(stderr, "读取图片失败\n");
        return 1;
    }
    if (jpeg[0] != 0xFF || jpeg[1] != 0xD8) {
        fprintf(stderr, "%s 不是 JPEG 文件\n", img_path);
        return 1;
    }
    printf("已载入 %s (%ld 字节)\n", img_path, (long)jpeg_len);

    /* ---- 连接服务器 ---- */
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((uint16_t)port);
    addr.sin_addr.s_addr = inet_addr(ip);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "连接 %s:%d 失败: %s\n", ip, port, strerror(errno));
        close(sock);
        return 1;
    }

    int one = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    /* ---- 握手：CAM <account>\r\n ---- */
    {
        char line[128];
        int  len = snprintf(line, sizeof(line), "%s %s\r\n",
                            PROTO_CMD_CAM, account);

        if (send_all(sock, line, len) < 0) {
            perror("send handshake");
            close(sock);
            return 1;
        }
        printf("已连接 %s:%d，已发送握手: %s %s\n", ip, port, PROTO_CMD_CAM, account);
    }

    /* ---- 等服务器回应 ---- */
    {
        char reply[64];

        if (read_reply_line(sock, reply, sizeof(reply)) != 0) {
            fprintf(stderr, "等待服务器握手回应超时或连接断开\n");
            close(sock);
            return 1;
        }

        printf("服务器回应: %s\n", reply);

        if (strcmp(reply, "OK") != 0) {
            if (strcmp(reply, "ERR AUTH") == 0)
                fprintf(stderr, "账号 \"%s\" 不存在或名字非法，"
                                "请检查 accounts.conf\n", account);
            else if (strcmp(reply, "ERR BUSY") == 0)
                fprintf(stderr, "账号 \"%s\" 已有采集端在线\n", account);
            else
                fprintf(stderr, "未识别的回应\n");
            close(sock);
            return 1;
        }
    }

    printf("握手成功，开始推流 (Ctrl+C 退出)\n");

    /* ---- 循环发送 ---- */
    static char frame_buf[sizeof(struct frame_header) + PROTO_MAX_PAYLOAD];
    uint32_t seq = 0;
    uint64_t t0  = now_ms();
    uint64_t next = t0;

    for (;;) {
        struct frame_header hdr;

        memset(&hdr, 0, sizeof(hdr));
        hdr.flags = FLAG_OPENCV;
        hdr.ts_ms = now_ms();               /* 用单调时钟，与采集端一致 */
        proto_fill_header(&hdr, seq++, (uint32_t)jpeg_len);

        int total = proto_pack(frame_buf, sizeof(frame_buf), &hdr, jpeg);
        if (total < 0) {
            fprintf(stderr, "打包失败\n");
            break;
        }

        if (send_all(sock, frame_buf, total) < 0) {
            fprintf(stderr, "发送失败，服务器可能已关闭: %s\n", strerror(errno));
            break;
        }

        if (seq % 10 == 0) {
            printf("\r已发送 %u 帧, 运行 %.1f 秒", seq,
                   (now_ms() - t0) / 1000.0);
            fflush(stdout);
        }

        /* 按固定间隔发送，模拟摄像头的帧率 */
        next += SEND_INTERVAL_MS;
        uint64_t now = now_ms();
        if (next > now)
            usleep((useconds_t)((next - now) * 1000));
        else
            next = now;
    }

    printf("\n");
    close(sock);
    return 0;
}
