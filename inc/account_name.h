#ifndef __ACCOUNT_NAME_H__
#define __ACCOUNT_NAME_H__

/* ============================================================
 *  账号名合法性 —— 【唯一定义处】
 *
 *  账号名同时出现在三个地方，三处的规则必须完全一致，
 *  否则会出现"配置加载时接受、握手时拒绝"这类自相矛盾的行为：
 *      1. accounts.conf 加载
 *      2. 采集端握手 "CAM <account>"
 *      3. 查看端 URL 路径 "/<account>"
 *
 *  因此本函数做成纯函数放在头文件里，谁都能调用，
 *  不依赖账号表、不需要加锁。
 * ============================================================ */

#include <string.h>

#include "config.h"     /* ACCOUNT_NAME_LEN */

/*
 * 返回 0 合法，-1 非法。
 *
 * 规则：^[A-Za-z0-9_]{1,ACCOUNT_NAME_LEN-1}$
 *   · 只允许字母、数字、下划线
 *   · 排除 '/' '?' '#' 空格 ':' '.' 等会破坏 URL 路径或配置解析的字符
 *   · 不等于保留字 favicon.ico
 */
static inline int account_name_valid(const char *name)
{
    size_t len;

    if (name == NULL)
        return -1;

    len = strlen(name);
    if (len == 0 || len >= ACCOUNT_NAME_LEN)
        return -1;

    for (size_t i = 0; i < len; i++) {
        char c = name[i];
        int ok = (c >= 'a' && c <= 'z') ||
                 (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9') ||
                 (c == '_');
        if (!ok)
            return -1;
    }

    /*
     * 保留字：浏览器打开任何页面都会自动请求 /favicon.ico。
     * 当前字符集已经挡掉了 '.'，这里仍然显式写出来 ——
     * 一是表达意图，二是将来若放宽字符集，这行还在。
     */
    if (strcmp(name, "favicon.ico") == 0)
        return -1;

    return 0;
}

#endif /* __ACCOUNT_NAME_H__ */
