/*
 *  查看端的帧来源：真实采集帧 或 等待占位图
 *
 *  为什么要有占位图：
 *      查看端先连、采集端后连是常态。如果建流后什么都不发，
 *      浏览器会一直转圈，用户不知道是在等还是坏了。
 *      先推一张「等待设备上线」的占位图，采集端一上线真实帧自然顶上，
 *      查看端侧完全无感 —— 这也省掉了"等待中"这条单独的分支。
 *
 *  定时器按需启停：
 *      只有正在等待的查看端才有活跃的 timerfd。采集端上线后立刻解除，
 *      不会白白产生每秒一次的唤醒。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/timerfd.h>

#include "config.h"
#include "stream_source.h"
#include "connect_pool.h"
#include "reactor.h"
#include "epoll.h"
#include "protocol.h"
#include "account.h"

/* 占位图读进内存，避免每次推送都读盘 */
static unsigned char* g_waiting_jpeg;
static int            g_waiting_len;

/*
 * timerfd → 宿主查看端 fd 的映射。
 *
 * 为什么需要它：reactor 的事件循环从 events[i].data.fd 拿到的是一个裸 fd，
 * 分不清那是连接的 socket 还是某个查看端的定时器。有了这张表，
 * stream_source_handle_timer() 就能 O(n) 判定并找到宿主连接。
 * 查看端数量上限是 MAX_VIEWERS_PER × MAX_ACCOUNTS，规模很小，
 * 而且定时器只在"等待采集端"期间存在，这个查找频率极低。
 */
#define TIMER_MAP_MAX  (MAX_VIEWERS_PER * MAX_ACCOUNTS)

static struct {
    int tfd;        /* -1 表示空槽 */
    int owner_fd;   /* 宿主的查看端 fd */
} g_timer_map[TIMER_MAP_MAX];

static void timer_map_init(void)
{
    static int inited;
    if (inited)
        return;
    for (int i = 0; i < TIMER_MAP_MAX; i++) {
        g_timer_map[i].tfd = -1;
        g_timer_map[i].owner_fd = -1;
    }
    inited = 1;
}

static void timer_map_add(int tfd, int owner_fd)
{
    timer_map_init();
    for (int i = 0; i < TIMER_MAP_MAX; i++) {
        if (g_timer_map[i].tfd < 0) {
            g_timer_map[i].tfd = tfd;
            g_timer_map[i].owner_fd = owner_fd;
            return;
        }
    }
    fprintf(stderr, "[stream] 定时器映射表已满\n");
}

static void timer_map_remove_tfd(int tfd)
{
    timer_map_init();
    for (int i = 0; i < TIMER_MAP_MAX; i++) {
        if (g_timer_map[i].tfd == tfd) {
            g_timer_map[i].tfd = -1;
            g_timer_map[i].owner_fd = -1;
            return;
        }
    }
}

static int timer_map_owner(int tfd)
{
    timer_map_init();
    for (int i = 0; i < TIMER_MAP_MAX; i++) {
        if (g_timer_map[i].tfd == tfd)
            return g_timer_map[i].owner_fd;
    }
    return -1;
}

/* ===================== 载入占位图 ===================== */

int stream_source_init(const char* img_path)
{
    int   fd;
    long  size;
    unsigned char* buf;
    ssize_t n;

#if !USING_WAITING_IMG
    (void)img_path;
    printf("[stream] 等待占位图功能已关闭 (USING_WAITING_IMG=0)\n");
    return -1;
#else
    const char* path = (img_path != NULL && img_path[0] != '\0')
                       ? img_path : WAITING_IMG;

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        printf("[stream] 未找到等待占位图 %s: %s\n", path, strerror(errno));
        printf("[stream] 查看端先连时将保持空白，直到采集端上线\n");
        return -1;
    }

    size = lseek(fd, 0, SEEK_END);
    if (size <= 0 || size > 4 * 1024 * 1024) {
        fprintf(stderr, "[stream] %s 大小异常 (%ld)，忽略\n", path, size);
        close(fd);
        return -1;
    }
    lseek(fd, 0, SEEK_SET);

    buf = malloc((size_t)size);
    if (buf == NULL) {
        perror("malloc");
        close(fd);
        return -1;
    }

    n = read(fd, buf, (size_t)size);
    close(fd);

    if (n != size) {
        fprintf(stderr, "[stream] 读取 %s 不完整\n", path);
        free(buf);
        return -1;
    }

    /* 必须真的是 JPEG，否则查看端会一直解析失败 */
    if (buf[0] != 0xFF || buf[1] != 0xD8) {
        fprintf(stderr, "[stream] %s 不是 JPEG 文件，忽略\n", path);
        free(buf);
        return -1;
    }

    free(g_waiting_jpeg);
    g_waiting_jpeg = buf;
    g_waiting_len  = (int)size;

    printf("[stream] 已载入等待占位图 %s (%d 字节, %d fps)\n",
           path, g_waiting_len, VIEWER_IDLE_FPS);
    return 0;
#endif
}

int stream_source_ready(void)
{
    return (g_waiting_jpeg != NULL && g_waiting_len > 0);
}

/* ===================== 推送占位图 ===================== */

int stream_source_push_placeholder(struct connect* conn)
{
    char head[128];
    int  head_len;
    int  need;
    int  space;

    if (conn == NULL || !stream_source_ready())
        return -1;
    if (conn->state != STATE_USING || conn->fd < 0)
        return -1;

    head_len = snprintf(head, sizeof(head),
                        "--frame\r\n"
                        "Content-Type: image/jpeg\r\n"
                        "Content-Length: %d\r\n\r\n",
                        g_waiting_len);
    if (head_len <= 0)
        return -1;

    need  = head_len + g_waiting_len + 2;   /* +2 是结尾的 \r\n */
    space = CONNECT_BUF_LEN - conn->outlen;

    if (need > space) {
        /* 上一拍还没发完（查看端网络慢）。占位图不是关键数据，
         * 这一拍直接跳过，等定时器下次触发即可。 */
        return -1;
    }

    memcpy(conn->outbuf + conn->outlen, head, head_len);
    conn->outlen += head_len;
    memcpy(conn->outbuf + conn->outlen, g_waiting_jpeg, g_waiting_len);
    conn->outlen += g_waiting_len;
    memcpy(conn->outbuf + conn->outlen, "\r\n", 2);
    conn->outlen += 2;

    set_epoll(conn->sub, EPOLLOUT, EPOLL_CTL_MOD, conn->fd);
    return 0;
}

/* ===================== 定时器管理 ===================== */

static void idle_timer_disarm(struct connect* conn)
{
    struct itimerspec its;

    if (conn->idle_tfd < 0)
        return;

    memset(&its, 0, sizeof(its));       /* 全 0 = 解除定时，fd 仍保留复用 */
    timerfd_settime(conn->idle_tfd, 0, &its, NULL);
}

void stream_source_arm_idle_timer(struct connect* conn, int need_placeholder)
{
#if !USING_WAITING_IMG
    (void)conn;
    (void)need_placeholder;
    return;
#else
    struct itimerspec its;

    if (conn == NULL || conn->phase != PHASE_VIEWER)
        return;

    if (!need_placeholder) {
        if (conn->idle_tfd >= 0) {
#ifdef DEBUG
            printf("[stream] fd=%d 采集端已上线，停止占位图\n", conn->fd);
#endif
            idle_timer_disarm(conn);
        }
        return;
    }

    if (!stream_source_ready())
        return;                                 /* 没有占位图，无从推送 */

    /* 首次进入等待态时创建 timerfd，之后复用 */
    if (conn->idle_tfd < 0) {
        conn->idle_tfd = timerfd_create(CLOCK_MONOTONIC,
                                        TFD_NONBLOCK | TFD_CLOEXEC);
        if (conn->idle_tfd < 0) {
            perror("timerfd_create");
            return;
        }

        if (set_epoll(conn->sub, EPOLLIN, EPOLL_CTL_ADD, conn->idle_tfd) < 0) {
            fprintf(stderr, "[stream] fd=%d 占位图定时器注册失败\n", conn->fd);
            close(conn->idle_tfd);
            conn->idle_tfd = -1;
            return;
        }
        timer_map_add(conn->idle_tfd, conn->fd);
    }

    memset(&its, 0, sizeof(its));
    if (VIEWER_IDLE_FPS > 0) {
        unsigned int usec = 1000000u / (unsigned int)VIEWER_IDLE_FPS;

        its.it_interval.tv_sec  = usec / 1000000u;
        its.it_interval.tv_nsec = (long)(usec % 1000000u) * 1000L;
        if (its.it_interval.tv_sec == 0 && its.it_interval.tv_nsec == 0)
            its.it_interval.tv_nsec = 1000000L;  /* 至少 1ms，防止零周期 */
    } else {
        its.it_interval.tv_sec = 1;
    }
    its.it_value = its.it_interval;              /* 立即开始周期性触发 */

    if (timerfd_settime(conn->idle_tfd, 0, &its, NULL) < 0) {
        perror("timerfd_settime");
        return;
    }

#ifdef DEBUG
    printf("[stream] fd=%d 采集端未上线，开始推占位图 (%d fps)\n",
           conn->fd, VIEWER_IDLE_FPS);
#endif
#endif
}

void stream_source_drop_idle_timer(struct connect* conn)
{
    if (conn == NULL)
        return;

    if (conn->idle_tfd >= 0) {
        set_epoll(conn->sub, 0, EPOLL_CTL_DEL, conn->idle_tfd);
        timer_map_remove_tfd(conn->idle_tfd);
        close(conn->idle_tfd);
        conn->idle_tfd = -1;
    }
}

void stream_source_stop_idle_for_account(int account_idx)
{
    int fds[MAX_VIEWERS_PER];
    int n;

    if (account_idx < 0)
        return;

    /*
     * 注意：本函数可能被【采集端所在线程】调用，而要操作的定时器属于
     * 别的线程上的查看端连接。因此这里只做 timerfd_settime()（解除定时），
     * 【不能】调 stream_source_drop_idle_timer() 去 close/EPOLL_CTL_DEL ——
     * 关闭一个正被其他线程 epoll 实例监视的 fd 会带来难以排查的竞态。
     * 定时器 fd 一直保留到查看端自己断开时再释放。
     *
     * 万一在解除生效前后定时器恰好触发了一次，stream_source_handle_timer()
     * 里还有一层「采集端已上线就不再推占位图」的兜底，多推一张也无害。
     */
    n = account_viewer_snapshot(account_idx, fds, MAX_VIEWERS_PER);
    for (int i = 0; i < n; i++) {
        struct connect* c = get_connector(fds[i]);
        if (c != NULL && c->state == STATE_USING && c->fd == fds[i] &&
            c->idle_tfd >= 0) {
            struct itimerspec its;
            memset(&its, 0, sizeof(its));
            timerfd_settime(c->idle_tfd, 0, &its, NULL);
#ifdef DEBUG
            printf("[stream] fd=%d 账号采集端上线，解除占位图定时器\n", c->fd);
#endif
        }
    }
}

/* ===================== 定时器事件处理 ===================== */

int stream_source_handle_timer(int tfd)
{
    int owner_fd = timer_map_owner(tfd);
    struct connect* conn;
    uint64_t expirations;

    if (owner_fd < 0)
        return 0;                   /* 不是本模块管理的 fd */

    /* 必须把到期计数读走，否则 epoll 会持续报告可读（水平触发） */
    if (read(tfd, &expirations, sizeof(expirations)) < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            perror("[stream] read(timerfd)");
    }

    conn = get_connector(owner_fd);
    if (conn == NULL || conn->state != STATE_USING ||
        conn->fd != owner_fd || conn->idle_tfd != tfd) {
        /* 宿主连接已经消失或换了定时器：顺手清理，避免残留 */
        if (conn != NULL && conn->idle_tfd == tfd)
            stream_source_drop_idle_timer(conn);
        return 1;
    }

    /* 采集端已上线的话就不该再推占位图了（理论上定时器已解除，
     * 这里再兜一层，防止竞态下多推一张） */
    if (conn->account_idx >= 0 && account_cam_online(conn->account_idx)) {
        stream_source_arm_idle_timer(conn, 0);
        return 1;
    }

    stream_source_push_placeholder(conn);
    return 1;
}
