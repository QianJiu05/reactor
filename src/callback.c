#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "connect_pool.h"
#include "epoll.h"
#include "reactor.h"
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
    printf("%d Get Msg: %s\n", conn->fd, conn->outbuf);
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
    printf("close fd:%d\n",conn->fd);
    set_epoll(conn->sub, 0, EPOLL_CTL_DEL,conn->fd);
    close(conn->fd);
    memset(conn, 0, sizeof(struct connect));
}