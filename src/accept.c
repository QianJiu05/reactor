#include <sys/types.h>          /* See NOTES */
#include <sys/socket.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <errno.h>
#include <arpa/inet.h>

#include "server_init.h"
#include "callback.h"
#include "recv.h"
#include "epoll.h"
#include "config.h"


/*
 * 从监听套接字接收一个新连接。
 * 返回新连接的 fd；没有更多连接（或被信号打断）返回 -1。
 */
int accept_callback(int fd) {
    struct sockaddr_in peer;
    socklen_t peer_len = sizeof(peer);

    int new_fd = accept(fd, (struct sockaddr *)&peer, &peer_len);
    if (new_fd == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return -1;      // 没有更多连接
        }
        perror("accept");
        return -1;
    }

#ifdef DEBUG
    printf("新连接 fd:%d 来自 %s:%d\n", new_fd,
           inet_ntoa(peer.sin_addr), ntohs(peer.sin_port));
#endif

    return new_fd;
}
