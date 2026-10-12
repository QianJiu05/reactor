#ifndef __UTILS_H__
#define __UTILS_H__

#include <stdint.h>
#include <time.h>

/* 单调递增毫秒时间戳。
 * 用 CLOCK_MONOTONIC 而不是 CLOCK_REALTIME：不受系统对时跳变影响，
 * 判断"采集端多久没来帧"这类时间差更可靠。 */
static inline uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

#endif /* __UTILS_H__ */
