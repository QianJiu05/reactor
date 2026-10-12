#ifndef __HTTP_PARSE_H__
#define __HTTP_PARSE_H__

/* ============================================================
 *  报文首行的纯解析函数
 *
 *  这里只放【不依赖 socket / 全局状态】的字符串处理，
 *  目的是：(1) 逻辑集中一处，采集端握手与查看端路由共用同一套边界处理；
 *          (2) 可以脱离网络单独做单元测试。
 *
 *  设计约定：调用方传入的首行必须是【已经收到完整 \r\n】的那一行
 *  （由 recv.c 的 find_line_end() 保证），因此这里不必再处理分片。
 * ============================================================ */

#include <stddef.h>
#include <string.h>

#include "config.h"
#include "protocol.h"
#include "account_name.h"

/*
 * 解析 "CAM <account>\r\n" 里的账号名。
 * 成功返回 1 并写入 out；前缀不匹配 / 没有账号名 / 参数非法返回 0。
 *
 * 注意 "CAM" 之后必须紧跟空白：否则 "CAME alice" 会被误判成账号 "E"。
 */
static inline int http_parse_cam_account(const char *line, char *out, int out_cap)
{
    const char *p;
    int n = 0;

    if (line == NULL || out == NULL || out_cap <= 0)
        return 0;

    if (strncmp(line, PROTO_CMD_CAM, strlen(PROTO_CMD_CAM)) != 0)
        return 0;

    p = line + strlen(PROTO_CMD_CAM);

    /* 前缀之后必须立刻是空白或行尾，避免 "CAME" 这类误匹配 */
    if (*p != ' ' && *p != '\t' && *p != '\0' && *p != '\r' && *p != '\n')
        return 0;

    /* 跳过 "CAM" 与账号名之间的空白 */
    while (*p == ' ' || *p == '\t')
        p++;

    while (*p != '\0' && *p != '\r' && *p != '\n' &&
           *p != ' '  && *p != '\t' && n < out_cap - 1) {
        out[n++] = *p++;
    }
    out[n] = '\0';

    return (n > 0) ? 1 : 0;
}

/*
 * 解析 HTTP 请求行 "GET <path> <version>"。
 *
 * 成功返回 1，并把方法与路径写入 out_method / out_path；
 * 报文格式不对（不是三段、路径不以 '/' 开头、超长）返回 0。
 */
static inline int http_parse_request_line(const char *line,
                                          char *out_method, int method_cap,
                                          char *out_path,   int path_cap)
{
    const char *p = line;
    const char *q;
    int m = 0, n = 0;

    if (line == NULL || out_method == NULL || out_path == NULL ||
        method_cap <= 0 || path_cap <= 0)
        return 0;

    /* --- 第一段：方法 --- */
    while (*p == ' ')
        p++;
    q = p;
    while (*q != '\0' && *q != ' ' && *q != '\r' && *q != '\n')
        q++;
    if (q == p)
        return 0;
    if ((int)(q - p) >= method_cap)
        return 0;
    while (p < q)
        out_method[m++] = *p++;
    out_method[m] = '\0';

    /* --- 第二段：路径 --- */
    p = q;
    while (*p == ' ')
        p++;
    q = p;
    while (*q != '\0' && *q != ' ' && *q != '\r' && *q != '\n')
        q++;
    if (q == p)
        return 0;                       /* 只有方法、没有路径 */
    if (*p != '/')
        return 0;                       /* HTTP 路径必须以 / 开头 */
    if ((int)(q - p) >= path_cap)
        return 0;
    while (p < q)
        out_path[n++] = *p++;
    out_path[n] = '\0';

    return 1;
}

/*
 * 从 URL 路径里切出账号名。
 *
 * 只支持一层路径（设计文档 §5.5.6）：
 *      "/alice"   → 成功，out = "alice"
 *      "/"        → 失败（没有账号）
 *      "/alice/x" → 失败（含第二层）
 *      "/favicon.ico" → 失败
 *
 * 切出的名字还会再过一遍 account_name_valid()，这样"URL 里能出现什么"
 * 与"配置里能写什么、握手能发什么"严格一致 —— 三处共用同一个校验函数，
 * 不会出现某处接受、另一处拒绝的自相矛盾。
 *
 * 成功返回 1，失败返回 0。
 */
static inline int http_path_to_account(const char *path, char *out, int out_cap)
{
    const char *p;
    int n = 0;

    if (path == NULL || out == NULL || out_cap <= 0)
        return 0;
    if (path[0] != '/')
        return 0;

    p = path + 1;
    if (*p == '\0')
        return 0;                       /* 只有 "/" */

    while (*p != '\0' && *p != '/' && n < out_cap - 1)
        out[n++] = *p++;

    if (*p == '/')
        return 0;                       /* 还有第二层，不支持 */
    out[n] = '\0';

    if (n == 0)
        return 0;

    return (account_name_valid(out) == 0) ? 1 : 0;
}

#endif /* __HTTP_PARSE_H__ */
