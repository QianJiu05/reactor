/*
 *  账号表实现
 *
 *  配置格式（accounts.conf），一行一个账号：
 *
 *      # 注释行
 *      alice
 *      bob:
 *      carol:whatever      # 冒号后的密码列本期保留但不校验
 *
 *  设计要点：
 *    1. 账号在启动时【预分配】，运行期只做查找，不新增 —— 于是
 *       "账号表"实际上是一张只读名单 + 每项的运行态，无需考虑动态扩容。
 *    2. 所有对外接口都持 g_account_lock，可被任意 reactor 线程调用。
 *    3. 反查采集端 fd 用线性扫描而非额外索引：账号数上限 64（编译期常量），
 *       扫描是 O(64)，而少维护一张索引表就少一处可能不同步的状态。
 *       详见 docs/账号体系设计.md §3.3。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>

#include "config.h"
#include "account.h"
#include "utils.h"

struct account {
    char     name[ACCOUNT_NAME_LEN];
    char     password[64];                /* 预留：本期不校验 */
    int      used;

    int      cam_fd;                      /* 绑定的采集端 fd，-1 = 未绑定 */
    int64_t  last_frame_ms;               /* 最近一帧时刻，用于僵死判定 */
    int64_t  cam_online_since;            /* 采集端上线时刻，0 = 离线 */

    int      viewers[MAX_VIEWERS_PER];    /* 查看端 fd，-1 = 空槽 */
    int      viewer_count;

    unsigned long frames_in;
    unsigned long frames_out;
};

static struct account g_accounts[MAX_ACCOUNTS];
static pthread_mutex_t g_account_lock = PTHREAD_MUTEX_INITIALIZER;

/* ===================== 内部工具 ===================== */

static int idx_valid(int idx)
{
    return idx >= 0 && idx < MAX_ACCOUNTS && g_accounts[idx].used;
}

/* 去掉行首尾的空白与换行 */
static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t')
        s++;

    char *end = s + strlen(s);
    while (end > s && (end[-1] == '\n' || end[-1] == '\r' ||
                       end[-1] == ' '  || end[-1] == '\t'))
        end--;
    *end = '\0';
    return s;
}

/* ===================== 账号名合法性 =====================
 * account_name_valid() 是 static inline，定义在 inc/account_name.h，
 * 与握手解析、URL 路径解析共用同一份实现。这里不再重复定义。 */

/* ===================== 加载配置 ===================== */

int account_init(const char *file)
{
    const char *path = (file != NULL && file[0] != '\0') ? file : ACCOUNT_FILE;
    FILE *fp;
    char  line[256];
    int   lineno = 0;
    int   loaded = 0;

    memset(g_accounts, 0, sizeof(g_accounts));
    for (int i = 0; i < MAX_ACCOUNTS; i++) {
        g_accounts[i].cam_fd = -1;
        for (int j = 0; j < MAX_VIEWERS_PER; j++)
            g_accounts[i].viewers[j] = -1;
    }

    fp = fopen(path, "r");
    if (fp == NULL) {
        fprintf(stderr, "[account] 无法打开账号文件 %s\n", path);
        fprintf(stderr, "[account] 服务器仍会启动，但所有采集端握手都会失败、"
                        "所有查看端路径都会返回 404\n");
        return -1;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        char *p;
        char *colon;

        lineno++;

        p = trim(line);
        if (*p == '\0' || *p == '#')
            continue;                       /* 空行与注释 */

        /* 冒号后是密码列，本期保留但不使用 */
        colon = strchr(p, ':');
        if (colon != NULL)
            *colon = '\0';

        p = trim(p);

        if (account_name_valid(p) != 0) {
            fprintf(stderr, "[account] 第 %d 行账号名非法，已跳过: \"%s\"\n",
                    lineno, p);
            continue;
        }

        if (account_lookup(p) >= 0) {
            fprintf(stderr, "[account] 第 %d 行账号名重复，已跳过: \"%s\"\n",
                    lineno, p);
            continue;
        }

        if (loaded >= MAX_ACCOUNTS) {
            fprintf(stderr, "[account] 账号数已达上限 %d，第 %d 行及之后被忽略\n",
                    MAX_ACCOUNTS, lineno);
            break;
        }

        struct account *a = &g_accounts[loaded];
        snprintf(a->name, sizeof(a->name), "%s", p);
        if (colon != NULL) {
            /* 密码列读进来备用；本期不校验 */
            const char *pw = trim(colon + 1);
            snprintf(a->password, sizeof(a->password), "%s", pw);
        }
        a->used = 1;
        a->cam_fd = -1;
        loaded++;
    }

    fclose(fp);

    printf("[account] 已加载 %d 个账号", loaded);
    if (loaded > 0) {
        printf(": ");
        for (int i = 0; i < loaded; i++)
            printf("%s%s", g_accounts[i].name, (i + 1 < loaded) ? ", " : "");
    }
    printf("\n");

    if (loaded == 0)
        fprintf(stderr, "[account] 账号表为空，任何采集端/查看端都会被拒绝\n");

    return loaded;
}

/* ===================== 查找 ===================== */

int account_lookup(const char *name)
{
    int found = -1;

    if (name == NULL)
        return -1;

    pthread_mutex_lock(&g_account_lock);
    for (int i = 0; i < MAX_ACCOUNTS; i++) {
        if (g_accounts[i].used && strcmp(g_accounts[i].name, name) == 0) {
            found = i;
            break;
        }
    }
    pthread_mutex_unlock(&g_account_lock);

    return found;
}

const char *account_name(int idx)
{
    if (idx < 0 || idx >= MAX_ACCOUNTS || !g_accounts[idx].used)
        return NULL;
    return g_accounts[idx].name;
}

int account_count(void)
{
    int n = 0;

    pthread_mutex_lock(&g_account_lock);
    for (int i = 0; i < MAX_ACCOUNTS; i++) {
        if (g_accounts[i].used)
            n++;
    }
    pthread_mutex_unlock(&g_account_lock);

    return n;
}

/* ===================== 采集端绑定 ===================== */

int account_cam_bind(int idx, int cam_fd, int *stale_fd)
{
    int ret = ACCOUNT_BIND_OK;

    if (stale_fd != NULL)
        *stale_fd = -1;

    pthread_mutex_lock(&g_account_lock);

    if (!idx_valid(idx)) {
        pthread_mutex_unlock(&g_account_lock);
        return ACCOUNT_BIND_FAIL;
    }

    struct account *a = &g_accounts[idx];

    if (a->cam_fd >= 0) {
        /* 已有采集端：判断是不是僵死连接 */
        int64_t now = (int64_t)now_ms();
        int64_t idle = now - a->last_frame_ms;

        if (a->last_frame_ms == 0 || idle > CAM_STALE_MS) {
            /* 从未收到过帧，或长时间无帧 —— 视为僵死，允许顶替 */
            if (stale_fd != NULL)
                *stale_fd = a->cam_fd;
            printf("[account] 账号 \"%s\" 的原采集端 fd=%d 已 %lld ms 无帧，"
                   "判定僵死，由 fd=%d 顶替\n",
                   a->name, a->cam_fd, (long long)idle, cam_fd);
        } else {
            printf("[account] 账号 \"%s\" 已有采集端 fd=%d（%lld ms 前有帧），"
                   "拒绝 fd=%d\n",
                   a->name, a->cam_fd, (long long)idle, cam_fd);
            pthread_mutex_unlock(&g_account_lock);
            return ACCOUNT_BIND_BUSY;
        }
    }

    a->cam_fd = cam_fd;
    a->cam_online_since = (int64_t)now_ms();
    a->last_frame_ms = 0;       /* 等第一帧到来再开始计时 */
    a->frames_in = 0;
    a->frames_out = 0;

    pthread_mutex_unlock(&g_account_lock);
    return ret;
}

void account_cam_unbind(int fd)
{
    if (fd < 0)
        return;

    pthread_mutex_lock(&g_account_lock);
    for (int i = 0; i < MAX_ACCOUNTS; i++) {
        if (g_accounts[i].used && g_accounts[i].cam_fd == fd) {
            printf("[account] 账号 \"%s\" 的采集端 fd=%d 断开"
                   "（本次共收到 %lu 帧）\n",
                   g_accounts[i].name, fd, g_accounts[i].frames_in);
            g_accounts[i].cam_fd = -1;
            g_accounts[i].cam_online_since = 0;
            g_accounts[i].last_frame_ms = 0;
            break;
        }
    }
    pthread_mutex_unlock(&g_account_lock);
}

int account_cam_find(int cam_fd)
{
    int found = -1;

    if (cam_fd < 0)
        return -1;

    /* 线性扫描：账号数上限是编译期常量 64，O(64) 即 O(1)，
     * 刻意不额外维护 fd→账号 索引表，避免多一处可能不同步的状态 */
    pthread_mutex_lock(&g_account_lock);
    for (int i = 0; i < MAX_ACCOUNTS; i++) {
        if (g_accounts[i].used && g_accounts[i].cam_fd == cam_fd) {
            found = i;
            break;
        }
    }
    pthread_mutex_unlock(&g_account_lock);

    return found;
}

bool account_cam_online(int idx)
{
    bool online = false;

    pthread_mutex_lock(&g_account_lock);
    if (idx_valid(idx))
        online = (g_accounts[idx].cam_fd >= 0);
    pthread_mutex_unlock(&g_account_lock);

    return online;
}

int64_t account_cam_last_frame_ms(int idx)
{
    int64_t t = 0;

    pthread_mutex_lock(&g_account_lock);
    if (idx_valid(idx))
        t = g_accounts[idx].last_frame_ms;
    pthread_mutex_unlock(&g_account_lock);

    return t;
}

void account_touch_frame(int idx)
{
    pthread_mutex_lock(&g_account_lock);
    if (idx_valid(idx)) {
        g_accounts[idx].last_frame_ms = (int64_t)now_ms();
        g_accounts[idx].frames_in++;
    }
    pthread_mutex_unlock(&g_account_lock);
}

/* ===================== 查看端订阅 ===================== */

int account_viewer_add(int idx, int fd)
{
    int ret = -1;

    pthread_mutex_lock(&g_account_lock);

    if (idx_valid(idx)) {
        struct account *a = &g_accounts[idx];

        /* 已经加过就不重复加 */
        for (int i = 0; i < MAX_VIEWERS_PER; i++) {
            if (a->viewers[i] == fd) {
                ret = 0;
                goto out;
            }
        }

        for (int i = 0; i < MAX_VIEWERS_PER; i++) {
            if (a->viewers[i] < 0) {
                a->viewers[i] = fd;
                a->viewer_count++;
                ret = 0;
                goto out;
            }
        }

        fprintf(stderr, "[account] 账号 \"%s\" 查看端已达上限 %d，拒绝 fd=%d\n",
                a->name, MAX_VIEWERS_PER, fd);
    }

out:
    pthread_mutex_unlock(&g_account_lock);
    return ret;
}

void account_viewer_remove(int fd)
{
    if (fd < 0)
        return;

    pthread_mutex_lock(&g_account_lock);

    /*
     * 摘掉【所有】账号里该 fd 的记录，而不只是第一个匹配。
     *
     * 正常情况下一个 fd 只会出现在一个账号的列表里（fd 在系统内唯一），
     * 但如果因为某个 bug 留下了重复登记，只摘第一个会让残留的槽位
     * 一直指向一个已关闭的 fd —— 而 push_frame_to_viewer() 恰好会
     * 跳过 state != STATE_USING 的目标，于是这个错误会被静默掩盖。
     * 与其留个隐患，不如一次清干净。
     */
    for (int i = 0; i < MAX_ACCOUNTS; i++) {
        struct account *a = &g_accounts[i];
        if (!a->used)
            continue;

        for (int j = 0; j < MAX_VIEWERS_PER; j++) {
            if (a->viewers[j] == fd) {
                a->viewers[j] = -1;
                if (a->viewer_count > 0)
                    a->viewer_count--;
            }
        }
    }

    pthread_mutex_unlock(&g_account_lock);
}

int account_viewer_snapshot(int idx, int *out, int out_cap)
{
    int n = 0;

    if (out == NULL || out_cap <= 0)
        return 0;

    pthread_mutex_lock(&g_account_lock);

    /* 只做拷贝。fd 是否仍然有效由调用方校验（get_connector + state）——
     * 查看端断开时会调用 account_viewer_remove() 把自己摘掉。 */
    if (idx_valid(idx)) {
        struct account *a = &g_accounts[idx];
        for (int i = 0; i < MAX_VIEWERS_PER && n < out_cap; i++) {
            if (a->viewers[i] >= 0)
                out[n++] = a->viewers[i];
        }
    }

    pthread_mutex_unlock(&g_account_lock);
    return n;
}

int account_viewer_count(int idx)
{
    int n = 0;

    pthread_mutex_lock(&g_account_lock);
    if (idx_valid(idx))
        n = g_accounts[idx].viewer_count;
    pthread_mutex_unlock(&g_account_lock);

    return n;
}

void account_add_frames_out(int idx, unsigned long n)
{
    pthread_mutex_lock(&g_account_lock);
    if (idx_valid(idx))
        g_accounts[idx].frames_out += n;
    pthread_mutex_unlock(&g_account_lock);
}

void account_cleanup(void)
{
    pthread_mutex_destroy(&g_account_lock);
}
