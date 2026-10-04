#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "connect_pool.h"
#include "epoll.h"
#include "reactor.h"
#include "recv_resource.h"
#include "recorder.h"
int print_callback(struct connect* conn) {

    strncpy(conn->outbuf, conn->inbuf, conn->inlen);
    conn->outlen = conn->inlen;
    
    // printf("%d Get Msg: %s\n", conn->fd, conn->outbuf);
    printf("sub:%ld fd:%d Get Msg: %s\n", conn->sub->tid, conn->fd, conn->outbuf);
    conn->outlen = 0;
    conn->inlen = 0;

    memset(conn->outbuf, 0, sizeof(conn->outbuf));
    return 0;
}


int echo_callback(struct connect* conn) {
    strncpy(conn->outbuf, conn->inbuf, conn->inlen);
    conn->outlen = conn->inlen;
#ifdef DEBUG    
    printf("%d Get Msg: %s\n", conn->fd, conn->outbuf);
#endif

    int send_cnt = send(conn->fd, conn->outbuf, conn->outlen, 0);
    conn->outlen -= send_cnt;
    conn->inlen -= send_cnt;

    if (conn->outlen != 0) {
        memmove(conn->outbuf, conn->outbuf + send_cnt, conn->outlen);
    } else {
        memset(conn->outbuf, 0, sizeof(conn->outbuf));
    }
    return conn->outlen;
}

void close_callback(struct connect* conn) {
#ifdef DEBUG
    printf("close fd:%d\n", conn->fd);
#endif
    set_epoll(conn->sub, 0, EPOLL_CTL_DEL, conn->fd);

    if (conn->app.cam.dest >= 0)
        recorder_close_camera(conn->fd);   /* 相机端断开：收尾落盘上下文 */
    else
        viewer_unregister(conn);           /* 查看端断开：从注册表摘掉 */

    conn->state = STATE_CLOSED;
    if (conn->node != NULL && conn->node->using > 0)
        conn->node->using--;
    close(conn->fd);
    conn->fd = -1;
}