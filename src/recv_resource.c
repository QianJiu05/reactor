/* 
    get_resource_callback接收相机客户端的数据，然后转发给HTTP client
    cam_resource_callback
*/
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>


#include "recv_resource.h"
#include "reactor.h"
#include "connect_pool.h"

/* 
    get_resource_callback接收相机客户端的数据
    1. write (conn->inbuf ----> dest->inbuf)
    2. dest->inbuf ----> dest->outbuf
    3. dest->outbuf ----> http_client
*/
int get_resource_callback(struct connect* conn) {
    // printf("resource callback\n");
    struct connect* dest = get_connector(conn->app.cam.dest);
    if (dest == NULL) {
        printf("No destination connection\n");
        return -1;
    }
    if (dest->fd <= 0) {
        printf("Destination fd:%d is closed\n", conn->app.cam.dest);
        conn->close(conn);
        return -1;
    }

    // 将数据复制到目标连接的 inbuf
    int to_copy = conn->inlen;
    if (to_copy > CONNECT_BUF_LEN - dest->inlen) {
        to_copy = CONNECT_BUF_LEN - dest->inlen;  // 防止溢出
    }

    if (to_copy == 0) {
        // 目标缓冲区满了，等待其发送完毕,不要继续读取相机数据
        // printf("WARNING: Destination buffer full, pausing camera fd:%d\n", conn->fd);
        return 0;
    }

    memcpy(dest->inbuf+ dest->inlen, conn->inbuf, to_copy);    
    dest->inlen += to_copy;
    
    
    // printf("Forwarded %d bytes from cam fd:%d to HTTP fd:%d (dest outlen=%d)\n", 
        // to_copy, conn->fd, dest->fd, dest->outlen);

    if (to_copy < conn->inlen) {
        // 还有数据未复制，移动到缓冲区前面
        // memmove(conn->inbuf, conn->inbuf + to_copy, conn->inlen - to_copy);
        memmove(conn->inbuf, conn->inbuf + to_copy, to_copy);
        conn->inlen -= to_copy;
        printf("Buffered %d bytes in camera fd:%d\n", conn->inlen, conn->fd);
        // 目标缓冲区有空间后，会再次调用这个函数
    } else {
        // 全部数据已复制
        conn->inlen = 0;
        memset(conn->inbuf, 0, CONNECT_BUF_LEN);
    }

    // 通知 epoll 目标连接可以发送数据
    set_epoll(EPOLLOUT, EPOLL_CTL_MOD, dest->fd);
    
    return to_copy;
}

/* TYPE = SEND_RESOURCE: 清理inlen，不能把第一次连接的内容写到buf里 */
int handle_resource(struct connect* conn) {
    // 格式: "SEND 7"
    int dest_fd;
    if (sscanf(conn->inbuf, "SEND %d", &dest_fd) != 1) {
        printf("Invalid SEND format\n");
        return -1;
    }
    
    conn->app.cam.dest = dest_fd;
    
    // 验证目标连接是否存在
    struct connect* dest = get_connector(dest_fd);
    if (dest == NULL || dest->fd != dest_fd) {
        printf("Invalid destination fd:%d\n", dest_fd);
        return -1;
    }
    
    printf("Camera fd:%d -> HTTP client fd:%d\n", conn->fd, dest_fd);
    
    // 清空 inbuf
    memset(conn->inbuf, 0, conn->inlen);
    conn->inlen = 0;
    return 0;
}